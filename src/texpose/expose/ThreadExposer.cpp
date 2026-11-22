#include "ThreadExposer.hpp"
#include "ThreadExposee.hpp"
#include "../../../common/CommonPrimitives.hpp"

#include <stdlib.h>

#ifdef _WIN32
    #include <windows.h>
    #include <intrin.h>
    #include <winternl.h>
    #include <malloc.h>
#else
    #include <pthread.h>
#endif

namespace Texpose::src::expose {
    namespace {
        namespace Internal {
            namespace {
                constexpr usize DEFAULT_IDENTIFIED_LIST_COUNT = 8;
            }

            namespace {
                volatile bool globalExposerValid = false;

                #ifdef _WIN32
                    DWORD globalExposerTlsSlot = TLS_OUT_OF_INDEXES;
                    DWORD globalExposerFlsSlot = FLS_OUT_OF_INDEXES;
                    INIT_ONCE globalExposerOnce = INIT_ONCE_STATIC_INIT;
                #else
                    pthread_key_t globalExposerKey;
                    pthread_once_t globalExposerOnce = PTHREAD_ONCE_INIT;
                #endif

                ThreadExposer* const globalThreadExitSentinel = (ThreadExposer*) ((usize) 1);
            }

            auto allocateAligned(const usize inSize, const usize inAlignment) -> void* {
                #ifdef _WIN32
                    return _aligned_malloc(inSize, inAlignment);
                #else
                    void* memory = nullptr;
                    if (posix_memalign(&memory, inAlignment, inSize) != 0) {
                        return nullptr;
                    }

                    return memory;
                #endif
            }
            auto freeAligned(void* const inMemory) -> void {
                #ifdef _WIN32
                    _aligned_free(inMemory);
                #else
                    free(inMemory);
                #endif
            }

            auto hashThreadExposeeRelativeIdentity(const void* const inRelativeIdentity) -> usize {
                #if UINTPTR_MAX > 0xFFFFFFFFU
                    u64 hash = ((u64) ((usize) inRelativeIdentity)) + 0x9E3779B97F4A7C15ULL;
                    hash = (hash ^ (hash >> 30)) * 0xBF58476D1CE4E5B9ULL;
                    hash = (hash ^ (hash >> 27)) * 0x94D049BB133111EBULL;

                    return (usize) (hash ^ (hash >> 31));
                #else
                    u32 hash = ((u32) ((usize) inRelativeIdentity)) + 0x9E3779B9U;
                    hash = (hash ^ (hash >> 16)) * 0x21F0AAADU;
                    hash = (hash ^ (hash >> 15)) * 0x735A2D97U;

                    return (usize) (hash ^ (hash >> 15));
                #endif
            }

            auto destroyCurrentThreadExposer(ThreadExposer* const inSelf) -> void {
                auto** const threadExposees = inSelf->ownedThreadExposees;
                const usize threadExposeeCount = inSelf->ownedThreadExposeeCount;

                inSelf->ownedThreadExposees = nullptr;
                inSelf->ownedThreadExposeeCount = 0;
                inSelf->ownedThreadExposeeCapacity = 0;

                if (inSelf->identifiedThreadExposeeLists) {
                    free(inSelf->identifiedThreadExposeeLists);
                }

                inSelf->identifiedThreadExposeeLists = nullptr;
                inSelf->identifiedThreadExposeeCount = 0;

                inSelf->cachedThreadExposeeRelativeIdentity = nullptr;
                inSelf->cachedThreadExposee = nullptr;

                for (usize i = 0; i < threadExposeeCount; ++i) {
                    auto* const threadExposee = threadExposees[i];

                    auto* const threadExitCallback = threadExposee->threadExitCallback;
                    if (threadExitCallback) {
                        threadExitCallback(ThreadExposee_accessExposee(threadExposee), threadExposee->external);
                    }

                    auto* const destroyExposeeCallback = threadExposee->destroyExposeeCallback;
                    if (destroyExposeeCallback) {
                        destroyExposeeCallback(ThreadExposee_accessExposee(threadExposee), threadExposee->external);
                    }

                    freeAligned(threadExposee);
                }

                if (threadExposees) {
                    free(threadExposees);
                }

                freeAligned(inSelf);
            }
        }
    }

    auto ThreadExposer_current() -> ThreadExposer* {
        #ifdef _WIN32
            #if defined(_MSC_VER) && !defined(__clang__)
                const bool valid = Internal::globalExposerValid;
                #if defined(_M_ARM64)
                    __dmb(_ARM64_BARRIER_ISH);
                #else
                    _ReadBarrier();
                #endif
            #else
                const bool valid = __atomic_load_n(&Internal::globalExposerValid, __ATOMIC_ACQUIRE);
            #endif

            if (!valid) {
                InitOnceExecuteOnce(
                    &Internal::globalExposerOnce,
                    (PINIT_ONCE_FN) (+[](PINIT_ONCE, PVOID, PVOID*) -> BOOL {
                        Internal::globalExposerTlsSlot = TlsAlloc();
                        if (Internal::globalExposerTlsSlot == TLS_OUT_OF_INDEXES) {
                            return TRUE;
                        }

                        Internal::globalExposerFlsSlot = FlsAlloc(+[](void* const inSelf) -> void {
                            auto* const self = (ThreadExposer*) inSelf;
                            if (!self || self == Internal::globalThreadExitSentinel) {
                                return;
                            }

                            TlsSetValue(Internal::globalExposerTlsSlot, Internal::globalThreadExitSentinel);
                            FlsSetValue(Internal::globalExposerFlsSlot, Internal::globalThreadExitSentinel);

                            Internal::destroyCurrentThreadExposer(self);
                        });

                        if (Internal::globalExposerFlsSlot == FLS_OUT_OF_INDEXES) {
                            TlsFree(Internal::globalExposerTlsSlot);
                            Internal::globalExposerTlsSlot = TLS_OUT_OF_INDEXES;
                            return TRUE;
                        }

                        #if defined(_MSC_VER) && !defined(__clang__)
                            #if defined(_M_ARM64)
                                Internal::globalExposerValid = true;
                                __dmb(_ARM64_BARRIER_ISH);
                            #else
                                _ReadWriteBarrier();
                                Internal::globalExposerValid = true;
                            #endif
                        #else
                            __atomic_store_n(&Internal::globalExposerValid, true, __ATOMIC_RELEASE);
                        #endif

                        return TRUE;
                    }),
                    nullptr, nullptr
                );

                if (!Internal::globalExposerValid) {
                    return nullptr;
                }
            }
        #else
            if (!__atomic_load_n(&Internal::globalExposerValid, __ATOMIC_ACQUIRE)) {
                pthread_once(
                    &Internal::globalExposerOnce,
                    +[]() -> void {
                        if (pthread_key_create(&Internal::globalExposerKey, +[](void* const inSelf) -> void {
                            auto* const self = (ThreadExposer*) inSelf;
                            if (!self || self == Internal::globalThreadExitSentinel) {
                                return;
                            }

                            pthread_setspecific(Internal::globalExposerKey, Internal::globalThreadExitSentinel);

                            Internal::destroyCurrentThreadExposer(self);
                        }) != 0) {
                            return;
                        }

                        __atomic_store_n(&Internal::globalExposerValid, true, __ATOMIC_RELEASE);
                    }
                );

                if (!__atomic_load_n(&Internal::globalExposerValid, __ATOMIC_ACQUIRE)) {
                    return nullptr;
                }
            }
        #endif

        #ifdef _WIN32
            #if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
                auto* const slotValue = (Internal::globalExposerTlsSlot < 64) ? ((ThreadExposer*) NtCurrentTeb()->TlsSlots[Internal::globalExposerTlsSlot]) : ((ThreadExposer*) TlsGetValue(Internal::globalExposerTlsSlot));
            #else
                auto* const slotValue = (ThreadExposer*) TlsGetValue(Internal::globalExposerTlsSlot);
            #endif
        #else
            auto* const slotValue = (ThreadExposer*) pthread_getspecific(Internal::globalExposerKey);
        #endif

        if (slotValue == Internal::globalThreadExitSentinel) {
            return nullptr;
        }

        if (slotValue) {
            return slotValue;
        }

        auto* const self = (ThreadExposer*) Internal::allocateAligned(sizeof(ThreadExposer), 64);
        if (!self) {
            return nullptr;
        }

        self->identifiedThreadExposeeLists = (ThreadExposee**) calloc(Internal::DEFAULT_IDENTIFIED_LIST_COUNT, sizeof(ThreadExposee*));
        if (!self->identifiedThreadExposeeLists) {
            Internal::freeAligned(self);
            return nullptr;
        }

        self->ownedThreadExposees = nullptr;
        self->ownedThreadExposeeCount = 0;
        self->ownedThreadExposeeCapacity = 0;
        self->identifiedThreadExposeeListCount = Internal::DEFAULT_IDENTIFIED_LIST_COUNT;
        self->identifiedThreadExposeeCount = 0;
        self->cachedThreadExposeeRelativeIdentity = nullptr;
        self->cachedThreadExposee = nullptr;

        #ifdef _WIN32
            if (!TlsSetValue(Internal::globalExposerTlsSlot, self)) {
                free(self->identifiedThreadExposeeLists);
                Internal::freeAligned(self);
                return nullptr;
            }

            if (!FlsSetValue(Internal::globalExposerFlsSlot, self)) {
                TlsSetValue(Internal::globalExposerTlsSlot, nullptr);
                free(self->identifiedThreadExposeeLists);
                Internal::freeAligned(self);
                return nullptr;
            }
        #else
            if (pthread_setspecific(Internal::globalExposerKey, self) != 0) {
                free(self->identifiedThreadExposeeLists);
                Internal::freeAligned(self);
                return nullptr;
            }
        #endif

        return self;
    }

    auto ThreadExposer_expose(
        ThreadExposer* const inSelf,
        void** const inExposeePtr, usize* const inExposeeSizePtr,
        const void* const inThreadExposeeRelativeIdentity,
        const ThreadExposee::ThreadExitCallback inThreadExitCallback,
        const ThreadExposee::DestroyExposeeCallback inDestroyExposeeCallback,
        void* const inExternal
    ) -> ThreadExposee* {
        if (!inSelf || !inExposeePtr || !inExposeeSizePtr || !inSelf->identifiedThreadExposeeLists) {
            return nullptr;
        }

        if (inThreadExposeeRelativeIdentity && inSelf->identifiedThreadExposeeCount >= inSelf->identifiedThreadExposeeListCount) {
            const usize newListCount = inSelf->identifiedThreadExposeeListCount * 2;
            auto** const newLists = (ThreadExposee**) calloc(newListCount, sizeof(ThreadExposee*));
            if (!newLists) {
                return nullptr;
            }

            const usize count = inSelf->ownedThreadExposeeCount;
            auto** const threadExposees = inSelf->ownedThreadExposees;
            for (usize i = 0; i < count; ++i) {
                auto* const threadExposee = threadExposees[i];
                const void* const relativeIdentity = threadExposee->relativeIdentity;
                if (!relativeIdentity) {
                    continue;
                }

                const usize index = Internal::hashThreadExposeeRelativeIdentity(relativeIdentity) & (newListCount - 1);
                auto* const headThreadExposee = newLists[index];

                threadExposee->prevInIdentifiedThreadExposeeLists = nullptr;
                threadExposee->nextInIdentifiedThreadExposeeLists = headThreadExposee;

                if (headThreadExposee) {
                    headThreadExposee->prevInIdentifiedThreadExposeeLists = threadExposee;
                }

                newLists[index] = threadExposee;
            }

            free(inSelf->identifiedThreadExposeeLists);

            inSelf->identifiedThreadExposeeLists = newLists;
            inSelf->identifiedThreadExposeeListCount = newListCount;
        }

        auto* const threadExposee = (ThreadExposee*) Internal::allocateAligned(sizeof(ThreadExposee), 64);
        if (!threadExposee) {
            return nullptr;
        }

        threadExposee->ownerThreadExposer = inSelf;
        threadExposee->relativeIdentity = inThreadExposeeRelativeIdentity;
        threadExposee->relativePosition = inSelf->ownedThreadExposeeCount;
        threadExposee->exposeePtr = inExposeePtr;
        threadExposee->exposeeSizePtr = inExposeeSizePtr;
        threadExposee->threadExitCallback = inThreadExitCallback;
        threadExposee->destroyExposeeCallback = inDestroyExposeeCallback;
        threadExposee->external = inExternal;
        threadExposee->nextInIdentifiedThreadExposeeLists = nullptr;
        threadExposee->prevInIdentifiedThreadExposeeLists = nullptr;

        if (inSelf->ownedThreadExposeeCount == inSelf->ownedThreadExposeeCapacity) {
            const usize newCapacity = inSelf->ownedThreadExposeeCapacity == 0 ? 8 : inSelf->ownedThreadExposeeCapacity * 2;
            auto** const newThreadExposees = (ThreadExposee**) realloc(inSelf->ownedThreadExposees, newCapacity * sizeof(ThreadExposee*));
            if (!newThreadExposees) {
                Internal::freeAligned(threadExposee);
                return nullptr;
            }

            inSelf->ownedThreadExposees = newThreadExposees;
            inSelf->ownedThreadExposeeCapacity = newCapacity;
        }

        inSelf->ownedThreadExposees[inSelf->ownedThreadExposeeCount++] = threadExposee;

        if (inThreadExposeeRelativeIdentity) {
            const usize index = Internal::hashThreadExposeeRelativeIdentity(inThreadExposeeRelativeIdentity) & (inSelf->identifiedThreadExposeeListCount - 1);
            auto* const headThreadExposee = inSelf->identifiedThreadExposeeLists[index];

            threadExposee->nextInIdentifiedThreadExposeeLists = headThreadExposee;

            if (headThreadExposee) {
                headThreadExposee->prevInIdentifiedThreadExposeeLists = threadExposee;
            }

            inSelf->identifiedThreadExposeeLists[index] = threadExposee;

            ++inSelf->identifiedThreadExposeeCount;
        }

        if (inThreadExposeeRelativeIdentity) {
            inSelf->cachedThreadExposeeRelativeIdentity = inThreadExposeeRelativeIdentity;
            inSelf->cachedThreadExposee = threadExposee;
        }

        return threadExposee;
    }
    auto ThreadExposer_unexpose(ThreadExposer* const inSelf, ThreadExposee* const inThreadExposee) -> void {
        if (!inSelf || !inThreadExposee || inThreadExposee->ownerThreadExposer != inSelf) {
            return;
        }

        const usize threadExposeeCount = inSelf->ownedThreadExposeeCount;
        auto** const threadExposees = inSelf->ownedThreadExposees;
        const usize removeIndex = inThreadExposee->relativePosition;
        if (removeIndex >= threadExposeeCount || threadExposees[removeIndex] != inThreadExposee) {
            return;
        }

        if (inThreadExposee->relativeIdentity) {
            if (inThreadExposee->prevInIdentifiedThreadExposeeLists) {
                inThreadExposee->prevInIdentifiedThreadExposeeLists->nextInIdentifiedThreadExposeeLists = inThreadExposee->nextInIdentifiedThreadExposeeLists;
            } else {
                const usize index = Internal::hashThreadExposeeRelativeIdentity(inThreadExposee->relativeIdentity) & (inSelf->identifiedThreadExposeeListCount - 1);
                if (inSelf->identifiedThreadExposeeLists[index] != inThreadExposee) {
                    return;
                }

                inSelf->identifiedThreadExposeeLists[index] = inThreadExposee->nextInIdentifiedThreadExposeeLists;
            }

            if (inThreadExposee->nextInIdentifiedThreadExposeeLists) {
                inThreadExposee->nextInIdentifiedThreadExposeeLists->prevInIdentifiedThreadExposeeLists = inThreadExposee->prevInIdentifiedThreadExposeeLists;
            }

            --inSelf->identifiedThreadExposeeCount;
        }

        if (removeIndex != threadExposeeCount - 1) {
            auto* const lastThreadExposee = threadExposees[threadExposeeCount - 1];
            threadExposees[removeIndex] = lastThreadExposee;
            lastThreadExposee->relativePosition = removeIndex;
        }

        threadExposees[threadExposeeCount - 1] = nullptr;
        inSelf->ownedThreadExposeeCount = threadExposeeCount - 1;

        if (inSelf->cachedThreadExposee == inThreadExposee) {
            inSelf->cachedThreadExposee = nullptr;
            inSelf->cachedThreadExposeeRelativeIdentity = nullptr;
        }

        if (inThreadExposee->destroyExposeeCallback) {
            inThreadExposee->destroyExposeeCallback(ThreadExposee_accessExposee(inThreadExposee), inThreadExposee->external);
        }

        Internal::freeAligned(inThreadExposee);
    }

    auto ThreadExposer_begin(ThreadExposer* const inSelf) -> ThreadExposee** {
        if (!inSelf || !inSelf->ownedThreadExposees) {
            return nullptr;
        }

        return inSelf->ownedThreadExposees;
    }
    auto ThreadExposer_end(ThreadExposer* const inSelf) -> ThreadExposee** {
        if (!inSelf || !inSelf->ownedThreadExposees) {
            return nullptr;
        }

        return inSelf->ownedThreadExposees + inSelf->ownedThreadExposeeCount;
    }

    auto ThreadExposer_find(ThreadExposer* const inSelf, const void* const inThreadExposeeRelativeIdentity) -> ThreadExposee* {
        if (!inSelf || !inThreadExposeeRelativeIdentity) {
            return nullptr;
        }

        if (inSelf->cachedThreadExposeeRelativeIdentity == inThreadExposeeRelativeIdentity) {
            return inSelf->cachedThreadExposee;
        }

        if (!inSelf->identifiedThreadExposeeLists) {
            return nullptr;
        }

        const usize index = Internal::hashThreadExposeeRelativeIdentity(inThreadExposeeRelativeIdentity) & (inSelf->identifiedThreadExposeeListCount - 1);
        auto* threadExposee = inSelf->identifiedThreadExposeeLists[index];
        while (threadExposee) {
            if (threadExposee->relativeIdentity == inThreadExposeeRelativeIdentity) {
                inSelf->cachedThreadExposeeRelativeIdentity = inThreadExposeeRelativeIdentity;
                inSelf->cachedThreadExposee = threadExposee;
                return threadExposee;
            }

            threadExposee = threadExposee->nextInIdentifiedThreadExposeeLists;
        }

        inSelf->cachedThreadExposeeRelativeIdentity = inThreadExposeeRelativeIdentity;
        inSelf->cachedThreadExposee = nullptr;

        return nullptr;
    }
}