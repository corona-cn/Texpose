#pragma once

/* === 强制内联 === */
#if defined(_MSC_VER)
    #define MACRO_FORCE_INLINE \
        __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define MACRO_FORCE_INLINE \
        inline __attribute__((always_inline))
#else
    #define MACRO_FORCE_INLINE \
        inline
#endif