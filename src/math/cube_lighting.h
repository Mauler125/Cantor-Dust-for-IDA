#ifndef MATH_CUBE_LIGHTING_H
#define MATH_CUBE_LIGHTING_H


#include "cantordust/scalar_types.h"
#include "cantordust/palette.h"
#include "cantordust/constants.h"

#include <algorithm>
#include <cstdint>
#include <cmath>

namespace cantordust
{

struct MathColor_s
{
    f32 red = 0;
    f32 green = 0;
    f32 blue = 0;
};

inline MathColor_s Math_ColorUnpack( const u32 argb )
{
    return {
        static_cast< f32 >( ( argb >> 16 ) & 255u ) / CANTORDUST_BYTE_MAX_F,
        static_cast< f32 >( ( argb >> 8 ) & 255u ) / CANTORDUST_BYTE_MAX_F,
        static_cast< f32 >( argb & 255u ) / CANTORDUST_BYTE_MAX_F
    };
}

inline u32 Math_ColorPack( const MathColor_s& color )
{
    const auto channel = []( const f32 value ) {
        return static_cast< u32 >( std::clamp( value, 0.0f, 1.0f ) *
            CANTORDUST_BYTE_MAX_F + 0.5f );
    };
    return 0xFF000000u |
        ( channel( color.red ) << 16 ) |
        ( channel( color.green ) << 8 ) |
        channel( color.blue );
}

inline MathColor_s Math_ColorAddScaled(
    const MathColor_s& left,
    const MathColor_s& right,
    const f32 scale )
{
    return {
        left.red + right.red * scale,
        left.green + right.green * scale,
        left.blue + right.blue * scale
    };
}

inline MathColor_s Math_ColorMix(
    const MathColor_s& left,
    const MathColor_s& right,
    const f32 weight )
{
    const f32 clamped = std::clamp( weight, 0.0f, 1.0f );
    return {
        left.red + ( right.red - left.red ) * clamped,
        left.green + ( right.green - left.green ) * clamped,
        left.blue + ( right.blue - left.blue ) * clamped
    };
}

inline MathColor_s Math_ColorMul(
    const MathColor_s& color, const f32 scale )
{
    return { color.red * scale, color.green * scale, color.blue * scale };
}

inline MathColor_s Math_ColorClamp( const MathColor_s& color )
{
    return {
        std::clamp( color.red, 0.0f, 1.0f ),
        std::clamp( color.green, 0.0f, 1.0f ),
        std::clamp( color.blue, 0.0f, 1.0f )
    };
}

struct MathCubeAxes_s
{
    MathColor_s x;
    MathColor_s y;
    MathColor_s z;
};

f32 Math_CubeZoomExposure( f32 zoom );

inline MathColor_s Math_CubeComposite(
    const MathColor_s& light,
    const MathColor_s& background,
    const MathColor_s& whiteHot )
{
    MathColor_s mapped = {
        1.0f - std::exp( -light.red * 1.95f ),
        1.0f - std::exp( -light.green * 1.95f ),
        1.0f - std::exp( -light.blue * 1.95f )
    };

    const f32 brightness = ( light.red + light.green + light.blue ) / 3.0f;
    const f32 whiteWeight = std::clamp( ( brightness - 0.24f ) * 0.95f,
        0.0f, 0.92f );

    mapped = Math_ColorMix( mapped, whiteHot, whiteWeight );

    const f32 visibility = std::clamp( std::max(
        { mapped.red, mapped.green, mapped.blue } ) + 0.14f,
        0.0f, 1.0f );

    return Math_ColorMix( background, Math_ColorClamp( mapped ), visibility );
}

MathCubeAxes_s Math_CubeAxes( Palette_e palette );
MathColor_s Math_CubeAxisColor(
    u32 x, u32 y, u32 z,
    const MathCubeAxes_s& axes );

MathColor_s Math_CubeAxisColor(
    u32 x, u32 y, u32 z, Palette_e palette );

} // namespace cantordust

#endif // MATH_CUBE_LIGHTING_H
