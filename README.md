# Texpose

> **Thread Expose** — A thin abstraction layer for thread-local data registration and cross-thread access.

`Texpose` is a thin abstraction over the OS thread-local storage primitives — Windows FLS/TLS and POSIX `pthread_key_t` — that a thread uses to register the data it owns. Its scope is deliberately narrow: registration, O(1) lookup, linear iteration and lifetime callbacks inside a single thread's registry, plus a handle through which other threads can read the registered entry. The implementation stays close to the underlying primitives instead of layering policy on top of them.

This makes it useful wherever a thread needs to publish a small, well-defined set of resources — connections, contexts, memory pools — and have them reachable from other threads without locks.

---

## Core Concepts

In `Texpose`, concurrent lifecycles are managed through a single, narrow model:

- **`ThreadExposer` (The Registry)**: A thread-local registry that owns the entries the current thread has exposed. Because it lives in TLS/FLS, its operations take no internal lock. When the thread exits, the registry runs the registered callbacks for the entries it still owns and then releases them.
- **`Exposee` (The Exposed Entity)**: The abstract unit a user registers. In the C++ wrapper it is represented by the **`ThreadExposee`** handle. The user data is referenced indirectly — through a pointer address (`void**`) and a size address (`usize*`) — so an entry remains valid when the user replaces the object behind it. The handle may be read from any thread while the entry is registered; the lifetime of the data it points to follows the owner's policy, as described in *Scope and Guarantees*.

**Design Philosophy**: `ThreadExposer` owns **where** an entry lives and **when** it is released. `ThreadExposee` describes **what** the entry is and how it can be **read**. Ownership of the pointee, synchronization of concurrent access, and hand-off between threads are application policy, not library semantics.

---

## Core Features

- **No Internal Locking**: Registration, lookup, iteration and removal run without internal locks. Fast paths are a small number of loads on data the owning thread already holds in its cache.
- **Cache-Line Isolation**: The registry and entry structures are explicitly aligned to avoid false sharing between a thread's local mutations and other threads' reads.
- **Handle-Based Cross-Thread Reads**: `peekExposee()` performs one load of the stored pointer. No lock, no atomic operation, and no dependency on the owner's current state.
- **Identity Binding with O(1) Lookup**: A unique identity pointer indexes a dynamically resized hash table, giving precise O(1) lookup within the owning registry. Exposing an identity that is already registered replaces the previous entry instead of creating a second one.
- **Lifetime Callbacks**: A destroy callback runs when an entry is removed; a thread-exit callback runs for every entry still registered when the thread terminates, before the entry is released.

---

## Scope and Guarantees

**Guaranteed by the library**

- Internal bookkeeping stays consistent: a registered entry appears exactly once in exactly one registry, and remains reachable by its identity until it is removed.
- `find`, `unexpose` and iteration have the complexity stated in *Performance Characteristics*, independent of allocator behavior or registry size.
- A registered entry's destroy callback runs exactly once — on `unexpose`, on replacement by a duplicate identity, or on thread exit.
- A thread-exit callback runs for every entry still registered when the thread terminates, before that entry is released.
- While thread-exit callbacks run, `current()` returns an invalid registry on every platform: the thread-local slot is set to a sentinel before the callbacks execute. Callbacks must not depend on the registry API.
- Move-assigning a `ThreadExposee` on the owning thread releases the entry the destination previously referenced, then takes the source handle. It is the only place the C++ wrapper removes an entry implicitly.
- Allocation failure is reported to the caller and leaves existing registrations untouched; no operation fails leaving the registry half-updated.

**Not guaranteed by the library**

- The validity of the object behind `exposeePtr` and `exposeeSizePtr`. The caller owns it and its lifetime.
- Any ordering or mutual exclusion between concurrent reads and writes of that object. `Texpose` makes the pointer reachable; it does not arbitrate access.
- The validity of any `ThreadExposee` handle — including copies held by other threads — once the entry is removed, replaced, or released by thread exit.
- The behavior of passing a handle to a `ThreadExposer` other than the one that created it; `unexpose` is scoped to the owning registry.

**Extension model**

The callbacks are the intended extension points. Reference counting, generation tags, connection hand-off and similar lifetime policies can be built on top of them without changes to the library.

---

## Quick Start

```cpp
#include <texpose/expose/ThreadExposer.hpp>
#include <iostream>

using namespace Texpose::include::expose;

auto workerThread() -> void {
    // Acquire the thread-local registry (created on first use, no lock).
    auto threadExposer = ThreadExposer::current();

    int data = 42;
    int* dataPtr = &data;
    usize dataSize = sizeof(int);
    int identity = 1001;

    // Register the data; the handle may be read from other threads.
    auto threadExposee = threadExposer.expose((void**) &dataPtr, &dataSize, &identity);

    // Cross-thread read: one load of the stored pointer, no lock.
    if (threadExposee.isValid()) {
        std::cout << *((int*) threadExposee.peekExposee()) << std::endl;
    }

    // O(1) retrieval within the owning thread.
    auto found = threadExposer.find(&identity);

    // Linear iteration over the entries owned by this thread.
    for (auto it = threadExposer.begin(); it != threadExposer.end(); ++it) {
        auto currentThreadExposee = *it;
        // Process currentThreadExposee...
    }

    // O(1) removal; thread exit also releases the remaining entries.
    threadExposer.unexpose(threadExposee);
}
```

---

## Performance Characteristics

Rather than pinning to specific benchmark numbers, `Texpose` is designed around a set of properties that hold across versions and platforms:

| Characteristic      | Nature                                                                                      |
|:--------------------|:--------------------------------------------------------------------------------------------|
| **Registration**    | Amortized constant time; the array grows geometrically, so cost is bounded by the allocator |
| **Cached Lookup**   | One pointer comparison against the last-used identity, including a negative result          |
| **General Lookup**  | Hash-based, amortized O(1); the bucket count grows with the number of identities            |
| **Unexpose**        | O(1) with respect to registry size; each entry records its own position in the array        |
| **Iteration**       | Contiguous walk over a pointer array, friendly to hardware prefetchers                      |
| **Synchronization** | No internal locks; mutual exclusion is unnecessary because each registry is thread-local    |

These properties are stated per operation and hold regardless of registry size. Absolute timings depend on the allocator, the cache hierarchy and the number of live identities, and are measured by benchmarks rather than promised by the interface.

---

## Architecture and Lifecycle

### 1. Data Structure Design
- **`ThreadExposer`**: A thread-local context that maintains two complementary views of the exposed set — a geometrically growing contiguous array (for linear traversal) and a dynamically expanding hash index (for O(1) retrieval).
- **`ThreadExposee`**: A cache-line-aligned handle holding the pointer address (`void**`), the size address (`usize*`), the identity, the lifecycle callbacks, and its own position in the owning array, which is what makes removal O(1).
- **TLS / FLS**: On Windows a TLS slot holds the registry for the hot path while an FLS slot drives destruction at thread exit; on POSIX a single `pthread_key_t` serves both roles. The fast path performs one check on a published initialization flag, with the read barrier the platform requires.

### 2. Cross-Thread Reads
Because an entry stores the address of the user's pointer rather than the pointer value, a reader performs a single load to obtain the current object, without touching the owner's registry. That load is not ordered against the owner's writes: if the owner may replace the pointee while others are reading, the caller needs its own publication protocol. `Texpose` guarantees reachability and visibility of the pointer; it does not guarantee that the object it selects is the one the caller expects.

### 3. Lifecycle Management
- **`DestroyExposeeCallback`**: Runs when an entry is removed — by explicit `unexpose`, by replacement under a duplicate identity, or during thread exit. Receives the current exposed object and the entry's external pointer, and returns nothing. Intended for releasing business resources.
- **`ThreadExitCallback`**: Registered through the platform's thread-exit mechanism — `FlsAlloc` on Windows, `pthread_key_create` on POSIX. Runs for every entry still registered when the thread terminates, before that entry is released. It receives the current exposed object and the external pointer and returns nothing; it cannot replace the stored pointer. The registry sets the thread-local slot to a sentinel before these callbacks run, so `current()` returns an invalid registry while they execute.

---

## License

This project is open-sourced under the [MIT License](LICENSE.txt).
Copyright © 2026 CoronaCN.