# Texpose

> **Thread Expose** - A unified, thin abstraction layer for thread-local data sharing and interaction.

`Texpose` aims to establish a minimalist, thin abstraction layer for data sharing in multithreaded environments. It abstracts away the tedious details of underlying OS primitives (such as Windows FLS/TLS or POSIX `pthread_key_t`), providing clear, safe semantics for upper-level applications while maintaining extreme performance and a low cognitive load.

Whether you are building a network engine, a game server, or high-performance middleware, `Texpose` helps you effortlessly manage thread-local resources (e.g., connections, contexts, memory pools) and traverse or retrieve them at near-physical limits.

---

## Core Concepts

In `Texpose`, we manage concurrent lifecycles through a minimalist model:

- **`ThreadExposer` (The Registry)**: A thread-local registry that manages all data exposed by the current thread. Being thread-local (TLS/FLS) ensures lock-free operations, and cascade destruction automatically triggers upon thread exit, completely preventing memory leaks.
- **`Exposee` (The Exposed Entity)**: This is the abstract "thing" the user wishes to expose. In the C++ wrapper layer, it is concretely encapsulated by the **`ThreadExposee`** handle. The underlying data is represented as a **pointer** (`void**`) and features **global visibility**. Once a thread `exposes` data, any other thread can safely access it directly by obtaining the `ThreadExposee` handle (or its internal data pointer) without locking or caring about the owner thread's lifecycle.

**Design Philosophy**: `ThreadExposer` determines **"where"** the data is, while `ThreadExposee` determines **"what"** the data is and how it can be safely **"accessed across threads"**.

---

## Core Features

- **Extreme Performance**: Lookup, iteration, and registration all avoid internal locking. Fast paths are dominated by a single memory load, and iteration walks a contiguous pointer array that the hardware prefetcher can fully pipeline.
- **Cache-Line Isolation**: Core structures are explicitly aligned to prevent false sharing, ensuring that cross-thread reads and local mutations never invalidate each other's CPU caches.
- **Pointer-level Cross-Thread Sharing**: Encapsulates internal data pointers, sizes, and identities into a `ThreadExposee` handle, facilitating lock-free direct reads across threads.
- **Flexible Identity Binding and Retrieval**: Precise O(1) lookup within the current thread's exposed list via `Identity` (a unique identifier pointer), backed by dynamic hash bucket resizing.
- **Fully Automatic Lifecycle Management**: Supports **Destroy Callbacks** and **Thread Exit Callbacks**. Automatically cleans up all `ThreadExposee` instances upon thread termination and triggers user logic, eliminating dangling pointers.

---

## Quick Start

```cpp
#include <texpose/expose/ThreadExposer.hpp>
#include <iostream>

using namespace Texpose::include::expose;

auto workerThread() -> void {
    // Acquire the thread-local registry (auto-initialized, lock-free).
    auto exposer = ThreadExposer::current();

    int data = 42;
    int* dataPtr = &data;
    usize dataSize = sizeof(int);
    int identity = 1001;

    // Expose the data; the returned ThreadExposee handle is safe to pass to other threads.
    auto threadExposee = exposer.expose((void**) &dataPtr, &dataSize, &identity);

    // Cross-thread read: a single pointer dereference, no lock.
    if (threadExposee.isValid()) {
        std::cout << *(int*) threadExposee.peekExposee() << std::endl;
    }

    // O(1) retrieval within the current thread.
    auto found = exposer.find(&identity);

    // Linear iteration over all ThreadExposee handles owned by this thread.
    for (auto it = exposer.begin(); it != exposer.end(); ++it) {
        auto currentThreadExposee = *it;
        // Process currentThreadExposee...
    }

    // O(1) manual cleanup; thread exit also cleans up automatically.
    exposer.unexpose(threadExposee);
}
```

---

## Performance Characteristics

Rather than pinning to a specific benchmark number, `Texpose` is designed around a set of stable performance properties that hold across versions and platforms:

| Characteristic      | Nature                                                                                |
|:--------------------|:--------------------------------------------------------------------------------------|
| **Registration**    | Amortized constant-time; bounded only by the underlying allocator                     |
| **Cached Lookup**   | A single pointer comparison — effectively free                                        |
| **General Lookup**  | Hash-based, amortized O(1), with dynamic bucket resizing to prevent degradation       |
| **Unexpose**        | Strict O(1) regardless of registry size, avoiding linear array scans                  |
| **Iteration**       | Contiguous memory walk, fully friendly to hardware prefetchers                        |
| **Synchronization** | Zero internal locks; thread-local isolation keeps hot paths free of atomic contention |

In short: no operation on the exposed data itself ever takes a lock, no internal traversal touches non-contiguous memory, and cache lines are strictly isolated to maximize multicore scaling. The cost profile is dominated by the user's memory layout, not by `Texpose`.

---

## Architecture and Lifecycle

### 1. Data Structure Design
- **`ThreadExposer`**: A thread-local context that maintains two complementary views of the exposed set — a dynamically resizing contiguous array (for linear traversal) and a dynamically expanding hash index (for O(1) retrieval).
- **`ThreadExposee`**: A cache-line-isolated handle wrapping the actual user data, including the data pointer address (`void**`), the size address (`usize*`), the identity, and lifecycle callbacks. It tracks its own relative position in the registry to achieve strict O(1) removal.
- **TLS / FLS**: Uses OS-level thread-local storage to access the owning `ThreadExposer` directly, with zero locking overhead and an atomic fast path for initialization.

### 2. Safe Cross-Thread Access
Because a `ThreadExposee` stores a pointer to the user data pointer (`void**`), cross-thread reads (`peekExposee()`) involve only a single memory load. Combined with a reasonable user-level design (e.g., passing `ThreadExposee` handles only during specific lifecycles), highly efficient lock-free cross-thread data interaction can be achieved without triggering cache invalidation on the owner thread.

### 3. Lifecycle Management
- **`DestroyExposeeCallback`**: Triggered when `unexpose` is explicitly called, used to clean up business resources.
- **`ThreadExitCallback`**: Registered via `FlsAlloc` (Windows) or `pthread_key_create` (POSIX). Ensures that all undestroyed `ThreadExposee` instances under the thread are safely reclaimed upon thread exit, allowing user cleanup logic to execute before destruction (returning a new pointer or null).

---

## License

This project is open-sourced under the [MIT License](LICENSE.txt).
Copyright © 2026 CoronaCN.