#ifndef TIER0_COMPILER_H
#define TIER0_COMPILER_H

#if defined( __clang__ ) || defined( __GNUC__ )
    #define V_LIKELY( expression ) \
        ( __builtin_expect( !!( expression ), 1 ) )
    #define V_UNLIKELY( expression ) \
        ( __builtin_expect( !!( expression ), 0 ) )
#else
    #define V_LIKELY( expression ) ( expression )
    #define V_UNLIKELY( expression ) ( expression )
#endif

#endif // TIER0_COMPILER_H
