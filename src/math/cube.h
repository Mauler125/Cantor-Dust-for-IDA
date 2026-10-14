#ifndef MATH_CUBE_H
#define MATH_CUBE_H


#include "cantordust/scalar_types.h"
#include "cantordust/cube.h"

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

} // namespace cantordust

#endif // MATH_CUBE_H
