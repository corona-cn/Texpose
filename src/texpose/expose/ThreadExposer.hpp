#pragma once
#include "../../../common/CommonPrimitives.hpp"

namespace Texpose::src::expose {
    struct alignas(64) ThreadExposer {
        struct ThreadExposee** ownedThreadExposees;
        usize ownedThreadExposeeCount;
        usize ownedThreadExposeeCapacity;

        struct ThreadExposee** identifiedThreadExposeeLists;
        usize identifiedThreadExposeeListCount;
        usize identifiedThreadExposeeCount;

        const void* cachedThreadExposeeRelativeIdentity;
        struct ThreadExposee* cachedThreadExposee;
    };

    auto ThreadExposer_current() -> ThreadExposer*;

    auto ThreadExposer_expose(
        ThreadExposer* inSelf,
        void** inExposeePtr, usize* inExposeeSizePtr,
        const void* inThreadExposeeRelativeIdentity,
        void* (*inThreadExitCallback)(void* inExposee, void* inExternal),
        void (*inDestroyExposeeCallback)(void* inExposee, void* inExternal),
        void* inExternal
    ) -> ThreadExposee*;
    auto ThreadExposer_unexpose(ThreadExposer* inSelf, ThreadExposee* inThreadExposee) -> void;

    auto ThreadExposer_begin(ThreadExposer* inSelf) -> ThreadExposee**;
    auto ThreadExposer_end(ThreadExposer* inSelf) -> ThreadExposee**;

    auto ThreadExposer_find(ThreadExposer* inSelf, const void* inThreadExposeeRelativeIdentity) -> ThreadExposee*;
}