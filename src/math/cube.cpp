#include "math/cube.h"

#include "cantordust/scalar_types.h"

#include "cantordust/constants.h"

#include <algorithm>
#include <cmath>

namespace cantordust
{

using std::sin;
using std::cos;

CubeTransform_s Math_CubeTransform( const CubeCamera_s& camera )
{
    return { cos( camera.yaw ), sin( camera.yaw ),
             cos( camera.pitch ), sin( camera.pitch ),
             camera.zoom };
}

CubeProjection_s Math_CubeProject(
    const f32 x, const f32 y, const f32 z,
    const CubeTransform_s& transform )
{
    const f32 cy = transform.cy;
    const f32 sy = transform.sy;
    const f32 cp = transform.cp;
    const f32 sp = transform.sp;

    const f32 rotatedX = x * cy - z * sy;
    const f32 rotatedZ = x * sy + z * cy;
    const f32 rotatedY = y * cp - rotatedZ * sp;
    const f32 depth = y * sp + rotatedZ * cp;
    const f32 perspective = transform.zoom * CANTORDUST_CAMERA_PERSPECTIVE_ZOOM /
        ( CANTORDUST_CAMERA_PERSPECTIVE_DISTANCE -
            CANTORDUST_CAMERA_PERSPECTIVE_DEPTH_WEIGHT * depth );

    return { rotatedX * perspective,
             rotatedY * perspective, depth };
}

void Cube_Rotate(
    CubeCamera_s& camera, const f32 horizontal, const f32 vertical )
{
    // Keep trigonometry numerically stable without limiting rotation.
    constexpr f32 radiansPerPixel = 0.008f;

    camera.yaw = std::remainder( camera.yaw + horizontal * radiansPerPixel,
        CANTORDUST_TAU_F );

    camera.pitch = std::remainder( camera.pitch + vertical * radiansPerPixel,
        CANTORDUST_TAU_F );
}

void Cube_Zoom( CubeCamera_s& camera, const int wheelSteps,
    const f32 minZoom, const f32 maxZoom )
{
    const int clamped = std::clamp( wheelSteps, -16, 16 );
    constexpr f32 zoomMultiplierPerStep = 1.14f;

    camera.zoom = std::clamp( camera.zoom *
        std::pow( zoomMultiplierPerStep, static_cast< f32 >( clamped ) ),
        minZoom, maxZoom );
}

} // namespace cantordust
