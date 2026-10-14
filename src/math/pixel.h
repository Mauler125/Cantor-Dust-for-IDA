#ifndef MATH_PIXEL_H
#define MATH_PIXEL_H


#include "cantordust/scalar_types.h"
#include "cantordust/types.h"

#include <cstddef>

namespace cantordust
{

bool Math_MapPointToImage(
    int x,
    int y,
    int viewportX,
    int viewportY,
    int viewportWidth,
    int viewportHeight,
    usize imageWidth,
    usize imageHeight,
    Point_s& result );

} // namespace cantordust

#endif // MATH_PIXEL_H
