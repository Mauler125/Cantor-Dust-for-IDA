#include "pch/core_pch.h"
#include "math/color.h"
#include "cantordust/scalar_types.h"

namespace cantordust
{

u32 Math_BlendColor(
    const u32 start,
    const u32 end,
    const f64 weight )
{
    const f64 t = std::clamp( weight, 0.0, 1.0 );
    u32 result = 0xFF000000u;

    for ( const unsigned int shift : { 16u, 8u, 0u } )
    {
        const f64 first = static_cast< f64 >( ( start >> shift ) & 255u );
        const f64 last = static_cast< f64 >( ( end >> shift ) & 255u );
        const u32 channel = static_cast< u32 >(
            first + ( last - first ) * t );

        result |= channel << shift;
    }

    return result;
}

u32 Math_WeightedColor(
    const u32* const colors,
    const u32* const weights,
    const usize count )
{
    u64 total = 0;
    u64 red = 0;
    u64 green = 0;
    u64 blue = 0;

    for ( usize i = 0; i < count; ++i )
    {
        total += weights[ i ];
        red += static_cast< u64 >(
            ( colors[ i ] >> 16 ) & 255u ) * weights[ i ];

        green += static_cast< u64 >(
            ( colors[ i ] >> 8 ) & 255u ) * weights[ i ];

        blue += static_cast< u64 >(
            colors[ i ] & 255u ) * weights[ i ];
    }

    if ( total == 0 )
    {
        return 0xFF000000u;
    }

    return 0xFF000000u |
        ( static_cast< u32 >( red / total ) << 16 ) |
        ( static_cast< u32 >( green / total ) << 8 ) |
        static_cast< u32 >( blue / total );
}

} // namespace cantordust
