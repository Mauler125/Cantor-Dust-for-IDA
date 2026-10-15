#ifndef MATH_CUBE_H
#define MATH_CUBE_H


#include "cantordust/scalar_types.h"
#include "cantordust/cube.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cantordust
{

struct CubeProjection_s
{
    f32 x = 0;
    f32 y = 0;
    f32 depth = 0;
};

struct CubeTransform_s
{
    f32 cy = 0;
    f32 sy = 0;
    f32 cp = 0;
    f32 sp = 0;
    f32 zoom = 1;
};

CubeTransform_s Math_CubeTransform( const CubeCamera_s& camera );
CubeProjection_s Math_CubeProject(
    f32 x, f32 y, f32 z,
    const CubeTransform_s& transform );

// Map the centered byte cube into a regular, upright tetrahedron. The
// tetrahedron's four vertices have equal radii and its centroid is zero.
// Radial mapping keeps all points inside the tetrahedron and leaves the
// source-space origin at the center.
inline void Math_CubeMapTetrahedron( f32& x, f32& y, f32& z )
{
    const f32 extent = std::max( { std::abs( x ), std::abs( y ),
        std::abs( z ) } );

    if ( extent == 0.0f )
        return;

    // Planes of the regular tetrahedron with corners (+,+,+),
    // (+,-,-), (-,+,-), (-,-,+): each inward face is n.p >= -1.
    const f32 face = std::max( { -x - y - z, -x + y + z,
        x - y + z, x + y - z } );
    const f32 scale = extent / face;
    const f32 a = x * scale;
    const f32 b = y * scale;
    const f32 c = z * scale;

    // Rigidly rotate, then scale to unit circumradius. The (+,+,+)
    // vertex becomes the apex (0,1,0); the other three form the base.
    x = ( a - b ) * 0.4082482904638630f; // 1/sqrt(6)
    y = ( a + b + c ) * ( 1.0f / 3.0f );
    z = ( a + b - 2.0f * c ) * 0.2357022603955158f; // 1/sqrt(18)
}

// Shared guide vertices for both 3D Byte Cloud and Function Universe globals.
// Derive the corners from the same mapping as the cloud points so the shape,
// centroid, and edge lengths cannot diverge between renderers.
inline const std::array< std::array< f32, 3 >, 4 >&
Math_CubeTetrahedronVertices()
{
    static const std::array< std::array< f32, 3 >, 4 > vertices = []() {
        std::array< std::array< f32, 3 >, 4 > result = {{
            {{ 1.0f, 1.0f, 1.0f }},
            {{ 1.0f, -1.0f, -1.0f }},
            {{ -1.0f, 1.0f, -1.0f }},
            {{ -1.0f, -1.0f, 1.0f }}
        }};

        for ( auto& vertex : result )
            Math_CubeMapTetrahedron( vertex[ 0 ], vertex[ 1 ], vertex[ 2 ] );

        return result;
    }();

    return vertices;
}


} // namespace cantordust

#endif // MATH_CUBE_H
