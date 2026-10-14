#include "cantordust/constants.h"
#include "cantordust/universe.h"

#include "math/cube.h"

#include <cmath>

using namespace cantordust;

// Preserve the *original* binary float values, not just decimal intent.
static_assert( CANTORDUST_PI_F == 3.14159265358979323846f );
static_assert( CANTORDUST_PI_F == 3.14159265f );
static_assert( CANTORDUST_TAU_F == 2.0f * 3.14159265358979323846f );
static_assert( CANTORDUST_TAU_F == 6.283185307179586f );
static_assert( CANTORDUST_TAU_D == 6.283185307179586 );
static_assert( CANTORDUST_HALF_PI_F == 1.57079632679489661923f );
static_assert( CANTORDUST_BYTE_MAX_F == 255.0f );
static_assert( CANTORDUST_CAMERA_PERSPECTIVE_ZOOM == 1.55f );
static_assert( CANTORDUST_CAMERA_PERSPECTIVE_DISTANCE == 3.0f );
static_assert( CANTORDUST_CAMERA_PERSPECTIVE_DEPTH_WEIGHT == 0.33f );
static_assert( CANTORDUST_UNIVERSE_PATH_LAYER_SPACING == 0.43f );
static_assert( CANTORDUST_UNIVERSE_SYNC_ALPHA_MIN == 48u );
static_assert( CANTORDUST_UNIVERSE_SYNC_ALPHA_MAX == 235u );

int main()
{
    // Check that the camera projection still uses the original calculation.
    CubeCamera_s camera;
    camera.yaw = 0.3f;
    camera.pitch = -0.7f;
    camera.zoom = 1.4f;

    const CubeTransform_s transform = Math_CubeTransform( camera );
    const float x = 0.5f;
    const float y = -0.2f;
    const float z = 0.8f;
    const CubeProjection_s result = Math_CubeProject( x, y, z, transform );

    const float rotatedX = x * transform.cy - z * transform.sy;
    const float rotatedZ = x * transform.sy + z * transform.cy;
    const float rotatedY = y * transform.cp - rotatedZ * transform.sp;
    const float depth = y * transform.sp + rotatedZ * transform.cp;
    const float perspective = transform.zoom * 1.55f /
        ( 3.0f - 0.33f * depth );

    if ( result.x != rotatedX * perspective ||
         result.y != rotatedY * perspective ||
         result.depth != depth )
    {
        return 1;
    }

    return 0;
}
