#include "ThreadExposee.hpp"
#include "../../../common/CommonPrimitives.hpp"

namespace Texpose::src::expose {
    auto ThreadExposee_accessExposee(const ThreadExposee* const inSelf) -> void* {
        if (!inSelf || !inSelf->exposeePtr) {
            return nullptr;
        }

        return *inSelf->exposeePtr;
    }
    auto ThreadExposee_peekExposee(const ThreadExposee* const inSelf) -> const void* {
        if (!inSelf || !inSelf->exposeePtr) {
            return nullptr;
        }

        return *inSelf->exposeePtr;
    }
    auto ThreadExposee_peekExposeeSize(const ThreadExposee* const inSelf) -> usize {
        if (!inSelf || !inSelf->exposeeSizePtr) {
            return 0;
        }

        return *inSelf->exposeeSizePtr;
    }
}