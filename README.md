# Texpose

> **Thread Expose** - A unified, thin abstraction layer for thread-local data sharing and interaction.

`Texpose` aims to establish a minimalist, thin abstraction layer for data sharing in multithreaded environments. It abstracts away the tedious details of underlying OS primitives (such as Windows FLS/TLS or POSIX `pthread_key_t`), providing clear, safe semantics for upper-level applications while maintaining extreme performance and a low cognitive load.

Whether you are building a network engine, a game server, or high-performance middleware, `Texpose` helps you effortlessly manage thread-local resources (e.g., connections, contexts, memory pools) and traverse or retrieve them at near-physical limits.

---

## Core Concepts

In `Texpose`, we manage concurrent lifecycles through a minimalist model:

- **`ThreadExposer` (The Registry)**: A thread-local registry that manages all data exposed by the current thread. Being thread-local (TLS/FLS) ensures lock-free operations, and cascade destruction automatically triggers upon thread exit, completely preventing memory leaks.
- **`Exposee` (The Exposed Entity)**: This is the "thing" the user wishes to expose to the thread layer. It is represented as a **pointer** (`void**`) and features **global visibility**. Once a thread `exposes` data, any other thread can safely access it directly by obtaining the `Exposee` pointer (or its internal data pointer) without locking or caring about the owner thread's lifecycle.

**Design Philosophy**: `ThreadExposer` determines **"where"** the data is, while `Exposee` determines **"what"** the data is and how it can be safely **"accessed across threads"**.

---

## Core Features

- **Extreme Performance**: Highly efficient iterator based on contiguous memory (`0.11 ns/op`), cached O(1) lookup (`1.07 ns/op`).
- **Pointer-level Cross-Thread Sharing**: Encapsulates internal data pointers, sizes, and identities into an `Exposee`, facilitating lock-free direct reads across threads.
- **Flexible Identity Binding and Retrieval**: Precise O(1) lookup within the current thread's exposed list via `Identity` (a unique identifier pointer).
- **Fully Automatic Lifecycle Management**: Supports **Destroy Callbacks** and **Thread Exit Callbacks**. Automatically cleans up all `Exposee` instances upon thread termination and triggers user logic, eliminating dangling pointers.

---

## Quick Start

```cpp
#include <texpose/expose/ThreadExposer.hpp>
#include <iostream>
#include <thread>

using namespace Texpose::include::expose;

void worker_thread() {
    // 1. Get the current thread's Exposer (auto-initialized, completely lock-free)
    auto exposer = ThreadExposer::current();

    // 2. Prepare the data to expose to the thread layer (e.g., a connection context)
    int my_data = 42;
    int* data_ptr = &my_data;
    usize data_size = sizeof(int);
    int my_identity = 1001; // Used as a retrieval key

    // 3. Expose the data to the thread layer.
    // The returned exposee handle can be passed to other threads.
    auto exposee = exposer.expose(
        (void**)&data_ptr, 
        &data_size, 
        &my_identity
    );

    // 4. [Assumption] This exposee is passed to another thread. 
    // The other thread simply reads the pointer directly (lock-free cross-thread read):
    if (exposee.isValid()) {
        void* shared_data = exposee.peekExposee(); 
        std::cout << "Other thread accessed data: " << *(int*)shared_data << std::endl;
    }

    // 5. Fast retrieval within the current thread
    auto found = exposer.find(&my_identity);
    
    // 6. Supports highly efficient range-based for loops over all Exposees owned by this thread
    for (auto it = exposer.begin(); it != exposer.end(); ++it) {
        auto current_exposee = *it;
        // Process current_exposee...
    }

    // 7. Manual unexpose (Automatic safe cleanup will also occur upon thread exit)
    exposer.unexpose(exposee);
}
```

---

## Performance Benchmarks

`Texpose` is heavily optimized for low-level caching and memory layout. Below are the benchmarks using `nanobench` on Windows/MSVC in Release mode (x64 architecture):

| Benchmark               | Latency (ns/op) | Throughput (op/s) | Description                                                       |
|:------------------------|:----------------|:------------------|:------------------------------------------------------------------|
| **Expose Registration** | **27.99 ns**    | 35.7 M/s          | Includes memory allocation and hash table insertion               |
| **Find Hit (Cached)**   | **1.07 ns**     | 933.8 M/s         | Single dereference, approaching CPU L1 cache limits               |
| **Find Miss**           | **4.37 ns**     | 228.6 M/s         | Includes hash calculation and linked list traversal               |
| **Iterator Traversal**  | **0.11 ns**     | 9.38 G/s          | Pure contiguous memory pointer stepping, no branch mispredictions |

*Note: The 0.11 ns/element traversal performance benefits from the underlying compact pointer array (`ThreadExposee**`), which perfectly triggers the CPU hardware prefetcher.*

---

## Architecture and Lifecycle

### 1. Data Structure Design
- **`ThreadExposer`**: Thread-local context maintaining a dynamically resizing contiguous array (for ultra-fast traversal) and a chained hash table (for O(1) retrieval).
- **`ThreadExposee`**: Handle wrapping the actual user data, including data pointer address (`void**`), size address (`usize*`), identity, and lifecycle callbacks.
- **TLS / FLS**: Uses OS-level thread-local storage to access `ThreadExposer` directly, with zero locking overhead.

### 2. Safe Cross-Thread Access
Because `Exposee` stores a pointer to the user data pointer (`void**`), cross-thread reads (`peekExposee()`) involve only a single memory load due to the lack of intermediate interception. Combined with a reasonable user-level design (e.g., passing `Exposee` handles only during specific lifecycles), highly efficient lock-free cross-thread data interaction can be achieved.

### 3. Lifecycle Management
- **`DestroyExposeeCallback`**: Triggered when `unexpose` is explicitly called, used to clean up business resources.
- **`ThreadExitCallback`**: Registers destructors using `FlsAlloc` (Windows) or `pthread_key_create` (Linux). Ensures that all undestroyed `Exposee` instances under the thread are safely reclaimed upon thread exit, allowing user cleanup logic to execute before destruction (returning a new pointer or null).

---

## License

This project is open-sourced under the [MIT License](LICENSE.txt).
Copyright © 2026 CoronaCN.