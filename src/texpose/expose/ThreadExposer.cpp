#include "ThreadExposer.hpp"
#include "ThreadExposee.hpp"
#include "../../../common/CommonPrimitives.hpp"

#ifdef _WIN32
    #include <windows.h>
    #include <intrin.h>
    #include <winternl.h>
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
                bool globalExposerValid = false;

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

                                free(threadExposee);
                            }

                            free(self->ownedThreadExposees);
                        }

                        if (self->identifiedThreadExposeeLists) {
                            free(self->identifiedThreadExposeeLists);
                        }

                        free(self);
                    });

                    if (Internal::globalExposerFlsSlot == FLS_OUT_OF_INDEXES) {
                        TlsFree(Internal::globalExposerTlsSlot);
                        Internal::globalExposerTlsSlot = TLS_OUT_OF_INDEXES;
                        return TRUE;
                    }

                    Internal::globalExposerValid = true;
                    return TRUE;
                }),
                nullptr, nullptr
            );
        #else
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

                    Internal::globalExposerValid = true;
                }
            );
        #endif

        if (!Internal::globalExposerValid) {
            return nullptr;
        }

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

        self = (ThreadExposer*) calloc(1, sizeof(ThreadExposer));
        if (!self) {
            return nullptr;
        }

        self->identifiedThreadExposeeLists = (ThreadExposee**) calloc(Internal::DEFAULT_IDENTIFIED_LIST_COUNT, sizeof(ThreadExposee*));
        if (!self->identifiedThreadExposeeLists) {
            free(self);
            return nullptr;
        }

        self->ownedThreadExposees = nullptr;
        self->ownedThreadExposeeCount = 0;
        self->ownedThreadExposeeCapacity = 0;
        self->identifiedThreadExposeeListCount = Internal::DEFAULT_IDENTIFIED_LIST_COUNT;
        self->cachedThreadExposeeIdentity = nullptr;
        self->cachedThreadExposee = nullptr;

        #ifdef _WIN32
            if (!TlsSetValue(Internal::globalExposerTlsSlot, self)) {
                free(self->identifiedThreadExposeeLists);
                free(self);
                return nullptr;
            }

            if (!FlsSetValue(Internal::globalExposerFlsSlot, self)) {
                TlsSetValue(Internal::globalExposerTlsSlot, nullptr);
                free(self->identifiedThreadExposeeLists);
                free(self);
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
        const void* const inThreadExposeeIdentity,
        void* (*inThreadExitCallback)(void* inExposee, void* inExternal),
        void (*inDestroyExposeeCallback)(void* inExposee, void* inExternal),
        void* const inExternal
    ) -> ThreadExposee* {
        if (!inSelf || !inExposeePtr || !inExposeeSizePtr) {
            return nullptr;
        }

        auto* const threadExposee = (ThreadExposee*) malloc(sizeof(ThreadExposee));
        if (!threadExposee) {
            return nullptr;
        }

        threadExposee->ownerThreadExposer = inSelf;
        threadExposee->identity = inThreadExposeeIdentity;
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
                free(threadExposee);
                return nullptr;
            }

            inSelf->ownedThreadExposees = newThreadExposees;
            inSelf->ownedThreadExposeeCapacity = newCapacity;
        }

        inSelf->ownedThreadExposees[inSelf->ownedThreadExposeeCount++] = threadExposee;

        if (inThreadExposeeIdentity) {
            const usize hash = ((usize) inThreadExposeeIdentity) >> 3;
            const usize index = hash & (inSelf->identifiedThreadExposeeListCount - 1);
            ThreadExposee* const head = inSelf->identifiedThreadExposeeLists[index];
            threadExposee->nextInIdentifiedThreadExposeeLists = head;
            if (head) {
                head->prevInIdentifiedThreadExposeeLists = threadExposee;
            }
            inSelf->identifiedThreadExposeeLists[index] = threadExposee;
        }

        inSelf->cachedThreadExposeeIdentity = inThreadExposeeIdentity;
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

        if (inThreadExposee->identity) {
            if (inThreadExposee->prevInIdentifiedThreadExposeeLists) {
                inThreadExposee->prevInIdentifiedThreadExposeeLists->nextInIdentifiedThreadExposeeLists = inThreadExposee->nextInIdentifiedThreadExposeeLists;
            } else {
                const usize hash = ((usize) inThreadExposee->identity) >> 3;
                const usize index = hash & (inSelf->identifiedThreadExposeeListCount - 1);
                inSelf->identifiedThreadExposeeLists[index] = inThreadExposee->nextInIdentifiedThreadExposeeLists;
            }

            if (inThreadExposee->nextInIdentifiedThreadExposeeLists) {
                inThreadExposee->nextInIdentifiedThreadExposeeLists->prevInIdentifiedThreadExposeeLists = inThreadExposee->prevInIdentifiedThreadExposeeLists;
            }
        }

        const usize count = inSelf->ownedThreadExposeeCount;
        ThreadExposee** const threadExposees = inSelf->ownedThreadExposees;
        usize removeIndex = 0;

        while (removeIndex < count) {
            if (threadExposees[removeIndex] == inThreadExposee) {
                break;
            }

            ++removeIndex;
        }

        if (removeIndex < count) {
            if (removeIndex != count - 1) {
                ThreadExposee* const lastExposee = threadExposees[count - 1];
                threadExposees[removeIndex] = lastExposee;
            }

            inSelf->ownedThreadExposeeCount = count - 1;
        }

        if (inSelf->cachedThreadExposee == inThreadExposee) {
            inSelf->cachedThreadExposee = nullptr;
            inSelf->cachedThreadExposeeIdentity = nullptr;
        }

        free(inThreadExposee);
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

    auto ThreadExposer_find(ThreadExposer* const inSelf, const void* const inThreadExposeeIdentity) -> ThreadExposee* {
        if (!inSelf || !inThreadExposeeIdentity) {
            return nullptr;
        }

        if (inSelf->cachedThreadExposeeIdentity == inThreadExposeeIdentity) {
            return inSelf->cachedThreadExposee;
        }

        if (!inSelf->identifiedThreadExposeeLists) {
            return nullptr;
        }

        const usize hash = ((usize) inThreadExposeeIdentity) >> 3;
        const usize index = hash & (inSelf->identifiedThreadExposeeListCount - 1);
        ThreadExposee* threadExposee = inSelf->identifiedThreadExposeeLists[index];
        while (threadExposee) {
            if (threadExposee->identity == inThreadExposeeIdentity) {
                inSelf->cachedThreadExposeeIdentity = inThreadExposeeIdentity;
                inSelf->cachedThreadExposee = threadExposee;
                return threadExposee;
            }

            threadExposee = threadExposee->nextInIdentifiedThreadExposeeLists;
        }

        return nullptr;
    }
}