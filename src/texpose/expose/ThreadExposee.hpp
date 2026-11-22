#pragma once
#include "../../../common/CommonPrimitives.hpp"

namespace Texpose::src::expose {
    typedef struct alignas(64) ThreadExposee {
        typedef void* (*ThreadExitCallback)(void* inExposee, void* inExternal);
        typedef void (*DestroyExposeeCallback)(void* inExposee, void* inExternal);

        struct ThreadExposer* ownerThreadExposer;
        const void* relativeIdentity;
        usize relativePosition;

        void** exposeePtr;
        usize* exposeeSizePtr;

        ThreadExitCallback threadExitCallback;
        DestroyExposeeCallback destroyExposeeCallback;
        void* external;

        alignas(64) struct ThreadExposee* prevInIdentifiedThreadExposeeLists;
        struct ThreadExposee* nextInIdentifiedThreadExposeeLists;
    } ThreadExposee;

    auto ThreadExposee_accessExposee(const ThreadExposee* inSelf) -> void*;
    auto ThreadExposee_peekExposee(const ThreadExposee* inSelf) -> const void*;
    auto ThreadExposee_peekExposeeSize(const ThreadExposee* inSelf) -> usize;
}