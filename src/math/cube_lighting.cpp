#include "pch/core_pch.h"
#include "math/cube_lighting.h"
#include "cantordust/scalar_types.h"

namespace cantordust
{

using std::sqrt;

f32 Math_CubeZoomExposure( const f32 zoom )
{
    return 0.42f + 0.68f * std::pow(
        std::max( zoom, 0.35f ), 0.78f );
}

MathCubeAxes_s Math_CubeAxes( const Palette_e palette )
{
    const Palette_s& colors = Palette_Get( palette );
    return {
        Math_ColorUnpack( colors.high ),
        Math_ColorUnpack( colors.printable ),
        Math_ColorUnpack( colors.low )
    };
}

MathColor_s Math_CubeAxisColor(
    const u32 x, const u32 y,
    const u32 z, const MathCubeAxes_s& axes )
{
    const MathColor_s& axisX = axes.x;
    const MathColor_s& axisY = axes.y;
    const MathColor_s& axisZ = axes.z;

    const f32 fx = static_cast< f32 >( x ) / CANTORDUST_BYTE_MAX_F;
    const f32 fy = static_cast< f32 >( y ) / CANTORDUST_BYTE_MAX_F;
    const f32 fz = static_cast< f32 >( z ) / CANTORDUST_BYTE_MAX_F;

    MathColor_s color = Math_ColorAddScaled( {}, axisX, fx );
    color = Math_ColorAddScaled( color, axisY, fy );
    color = Math_ColorAddScaled( color, axisZ, fz );

    const f32 peak = std::max( { color.red, color.green, color.blue, 1.0f } );
    color = Math_ColorMul( color, 1.0f / peak );

    const f32 energy = 0.18f + 0.82f * sqrt( ( fx + fy + fz ) / 3.0f );
    return Math_ColorMul( color, energy );
}

MathColor_s Math_CubeAxisColor(
    const u32 x, const u32 y,
    const u32 z, const Palette_e palette )
{
    return Math_CubeAxisColor( x, y, z, Math_CubeAxes( palette ) );
}

} // namespace cantordust
