#pragma once
#include "../../../common/CommonPrimitives.hpp"
#include "ThreadExposee.hpp"

namespace Texpose::src::expose {
    typedef struct alignas(64) ThreadExposer {
        struct ThreadExposee** ownedThreadExposees;
        usize ownedThreadExposeeCount;
        usize ownedThreadExposeeCapacity;

        struct ThreadExposee** identifiedThreadExposeeLists;
        usize identifiedThreadExposeeListCount;
        usize identifiedThreadExposeeCount;

        const void* cachedThreadExposeeRelativeIdentity;
        struct ThreadExposee* cachedThreadExposee;
    } ThreadExposer;

    auto ThreadExposer_current() -> ThreadExposer*;

    auto ThreadExposer_expose(
        ThreadExposer* inSelf,
        void** inExposeePtr, usize* inExposeeSizePtr,
        const void* inThreadExposeeRelativeIdentity,
        ThreadExposee::ThreadExitCallback inThreadExitCallback = nullptr,
        ThreadExposee::DestroyExposeeCallback inDestroyExposeeCallback = nullptr,
        void* inExternal = nullptr
    ) -> ThreadExposee*;
    auto ThreadExposer_unexpose(ThreadExposer* inSelf, ThreadExposee* inThreadExposee) -> void;

    auto ThreadExposer_begin(ThreadExposer* inSelf) -> ThreadExposee**;
    auto ThreadExposer_end(ThreadExposer* inSelf) -> ThreadExposee**;

    auto ThreadExposer_find(ThreadExposer* inSelf, const void* inThreadExposeeRelativeIdentity) -> ThreadExposee*;
}