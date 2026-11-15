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
                            if (!self) {
                                return;
                            }

                            if (self->ownedThreadExposees) {
                                for (usize i = 0; i < self->ownedThreadExposeeCount; ++i) {
                                    ThreadExposee* const threadExposee = self->ownedThreadExposees[i];

                                    auto* const threadExitCallback = threadExposee->threadExitCallback;
                                    auto** const exposeePtr = threadExposee->exposeePtr;
                                    if (threadExitCallback && exposeePtr) {
                                        *exposeePtr = threadExitCallback(ThreadExposee_accessExposee(threadExposee), threadExposee->external);
                                    }

                                    _aligned_free(threadExposee);
                                }

                                free(self->ownedThreadExposees);
                            }

                            if (self->identifiedThreadExposeeLists) {
                                free(self->identifiedThreadExposeeLists);
                            }

                            _aligned_free(self);
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
                            if (!self) {
                                return;
                            }

                            if (self->ownedThreadExposees) {
                                for (usize i = 0; i < self->ownedThreadExposeeCount; ++i) {
                                    ThreadExposee* const threadExposee = self->ownedThreadExposees[i];

                                    auto* const threadExitCallback = threadExposee->threadExitCallback;
                                    auto** const exposeePtr = threadExposee->exposeePtr;
                                    if (threadExitCallback && exposeePtr) {
                                        *exposeePtr = threadExitCallback(ThreadExposee_accessExposee(threadExposee), threadExposee->external);
                                    }

                                    free(threadExposee);
                                }

                                free(self->ownedThreadExposees);
                            }

                            if (self->identifiedThreadExposeeLists) {
                                free(self->identifiedThreadExposeeLists);
                            }

                            free(self);
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
                auto* self = (Internal::globalExposerTlsSlot < 64) ? ((ThreadExposer*) NtCurrentTeb()->TlsSlots[Internal::globalExposerTlsSlot]) : ((ThreadExposer*) TlsGetValue(Internal::globalExposerTlsSlot));
            #else
                auto* self = (ThreadExposer*) TlsGetValue(Internal::globalExposerTlsSlot);
            #endif
        #else
            auto* self = (ThreadExposer*) pthread_getspecific(Internal::globalExposerKey);
        #endif

        if (self) {
            return self;
        }

        #ifdef _WIN32
            self = (ThreadExposer*) _aligned_malloc(sizeof(ThreadExposer), 64);
        #else
            self = (ThreadExposer*) aligned_alloc(64, sizeof(ThreadExposer));
        #endif

        if (!self) {
            return nullptr;
        }

        self->identifiedThreadExposeeLists = (ThreadExposee**) calloc(Internal::DEFAULT_IDENTIFIED_LIST_COUNT, sizeof(ThreadExposee*));
        if (!self->identifiedThreadExposeeLists) {
            #ifdef _WIN32
                _aligned_free(self);
            #else
                free(self);
            #endif
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
                _aligned_free(self);
                return nullptr;
            }

            if (!FlsSetValue(Internal::globalExposerFlsSlot, self)) {
                TlsSetValue(Internal::globalExposerTlsSlot, nullptr);
                free(self->identifiedThreadExposeeLists);
                _aligned_free(self);
                return nullptr;
            }
        #else
            if (pthread_setspecific(Internal::globalExposerKey, self) != 0) {
                free(self->identifiedThreadExposeeLists);
                free(self);
                return nullptr;
            }
        #endif

        return self;
    }

    auto ThreadExposer_expose(
        ThreadExposer* const inSelf,
        void** const inExposeePtr, usize* const inExposeeSizePtr,
        const void* const inThreadExposeeRelativeIdentity,
        void* (*inThreadExitCallback)(void* inExposee, void* inExternal),
        void (*inDestroyExposeeCallback)(void* inExposee, void* inExternal),
        void* const inExternal
    ) -> ThreadExposee* {
        if (!inSelf || !inExposeePtr || !inExposeeSizePtr) {
            return nullptr;
        }

        if (inThreadExposeeRelativeIdentity && inSelf->identifiedThreadExposeeCount >= inSelf->identifiedThreadExposeeListCount) {
            const usize newListCount = inSelf->identifiedThreadExposeeListCount * 2;
            auto** const newLists = (ThreadExposee**) calloc(newListCount, sizeof(ThreadExposee*));
            if (!newLists) {
                return nullptr;
            }

            const usize count = inSelf->ownedThreadExposeeCount;
            ThreadExposee** const threadExposees = inSelf->ownedThreadExposees;
            for (usize i = 0; i < count; ++i) {
                ThreadExposee* const threadExposee = threadExposees[i];
                const void* const relativeIdentity = threadExposee->relativeIdentity;
                if (!relativeIdentity) {
                    continue;
                }

                const usize hash = ((usize) relativeIdentity) >> 3;
                const usize index = hash & (newListCount - 1);
                ThreadExposee* const head = newLists[index];

                threadExposee->prevInIdentifiedThreadExposeeLists = nullptr;
                threadExposee->nextInIdentifiedThreadExposeeLists = head;

                if (head) {
                    head->prevInIdentifiedThreadExposeeLists = threadExposee;
                }

                newLists[index] = threadExposee;
            }

            free(inSelf->identifiedThreadExposeeLists);

            inSelf->identifiedThreadExposeeLists = newLists;
            inSelf->identifiedThreadExposeeListCount = newListCount;
        }

        #ifdef _WIN32
            auto* const threadExposee = (ThreadExposee*) _aligned_malloc(sizeof(ThreadExposee), 64);
        #else
            auto* const threadExposee = (ThreadExposee*) aligned_alloc(64, sizeof(ThreadExposee));
        #endif

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
                #ifdef _WIN32
                    _aligned_free(threadExposee);
                #else
                    free(threadExposee);
                #endif
                return nullptr;
            }

            inSelf->ownedThreadExposees = newThreadExposees;
            inSelf->ownedThreadExposeeCapacity = newCapacity;
        }

        inSelf->ownedThreadExposees[inSelf->ownedThreadExposeeCount++] = threadExposee;

        if (inThreadExposeeRelativeIdentity) {
            const usize hash = ((usize) inThreadExposeeRelativeIdentity) >> 3;
            const usize index = hash & (inSelf->identifiedThreadExposeeListCount - 1);
            ThreadExposee* const head = inSelf->identifiedThreadExposeeLists[index];

            threadExposee->nextInIdentifiedThreadExposeeLists = head;

            if (head) {
                head->prevInIdentifiedThreadExposeeLists = threadExposee;
            }

            inSelf->identifiedThreadExposeeLists[index] = threadExposee;

            ++inSelf->identifiedThreadExposeeCount;
        }

        inSelf->cachedThreadExposeeRelativeIdentity = inThreadExposeeRelativeIdentity;
        inSelf->cachedThreadExposee = threadExposee;

        return threadExposee;
    }
    auto ThreadExposer_unexpose(ThreadExposer* const inSelf, ThreadExposee* const inThreadExposee) -> void {
        if (!inSelf || !inThreadExposee || inThreadExposee->ownerThreadExposer != inSelf) {
            return;
        }

        if (inThreadExposee->destroyExposeeCallback) {
            inThreadExposee->destroyExposeeCallback(ThreadExposee_accessExposee(inThreadExposee), inThreadExposee->external);
        }

        if (inThreadExposee->relativeIdentity) {
            if (inThreadExposee->prevInIdentifiedThreadExposeeLists) {
                inThreadExposee->prevInIdentifiedThreadExposeeLists->nextInIdentifiedThreadExposeeLists = inThreadExposee->nextInIdentifiedThreadExposeeLists;
            } else {
                const usize hash = ((usize) inThreadExposee->relativeIdentity) >> 3;
                const usize index = hash & (inSelf->identifiedThreadExposeeListCount - 1);
                inSelf->identifiedThreadExposeeLists[index] = inThreadExposee->nextInIdentifiedThreadExposeeLists;
            }

            if (inThreadExposee->nextInIdentifiedThreadExposeeLists) {
                inThreadExposee->nextInIdentifiedThreadExposeeLists->prevInIdentifiedThreadExposeeLists = inThreadExposee->prevInIdentifiedThreadExposeeLists;
            }

            --inSelf->identifiedThreadExposeeCount;
        }

        const usize count = inSelf->ownedThreadExposeeCount;
        ThreadExposee** const threadExposees = inSelf->ownedThreadExposees;
        const usize removeIndex = inThreadExposee->relativePosition;

        if (removeIndex < count && threadExposees[removeIndex] == inThreadExposee) {
            if (removeIndex != count - 1) {
                ThreadExposee* const lastExposee = threadExposees[count - 1];
                threadExposees[removeIndex] = lastExposee;
                lastExposee->relativePosition = removeIndex;
            }

            inSelf->ownedThreadExposeeCount = count - 1;
        }

        if (inSelf->cachedThreadExposee == inThreadExposee) {
            inSelf->cachedThreadExposee = nullptr;
            inSelf->cachedThreadExposeeRelativeIdentity = nullptr;
        }

        #ifdef _WIN32
            _aligned_free(inThreadExposee);
        #else
            free(inThreadExposee);
        #endif
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

        const usize hash = ((usize) inThreadExposeeRelativeIdentity) >> 3;
        const usize index = hash & (inSelf->identifiedThreadExposeeListCount - 1);
        ThreadExposee* threadExposee = inSelf->identifiedThreadExposeeLists[index];
        while (threadExposee) {
            if (threadExposee->relativeIdentity == inThreadExposeeRelativeIdentity) {
                inSelf->cachedThreadExposeeRelativeIdentity = inThreadExposeeRelativeIdentity;
                inSelf->cachedThreadExposee = threadExposee;
                return threadExposee;
            }

            threadExposee = threadExposee->nextInIdentifiedThreadExposeeLists;
        }

        return nullptr;
    }
}