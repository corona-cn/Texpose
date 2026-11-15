#pragma once
#include "../../../common/CommonPrimitives.hpp"

namespace Texpose::src::expose {
    struct ThreadExposee {
        struct ThreadExposer* ownerThreadExposer;
        const void* relativeIdentity;
        usize relativePosition;

        void** exposeePtr;
        usize* exposeeSizePtr;

        void* (*threadExitCallback)(void* inExposee, void* inExternal);
        void (*destroyExposeeCallback)(void* inExposee, void* inExternal);
        void* external;

        struct ThreadExposee* prevInIdentifiedThreadExposeeLists;
        struct ThreadExposee* nextInIdentifiedThreadExposeeLists;
    };

    auto ThreadExposee_accessExposee(const ThreadExposee* inSelf) -> void*;
    auto ThreadExposee_peekExposee(const ThreadExposee* inSelf) -> const void*;
    auto ThreadExposee_peekExposeeSize(const ThreadExposee* inSelf) -> usize;
}