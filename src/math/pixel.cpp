#include "math/pixel.h"
#include "cantordust/scalar_types.h"

#include <cstdint>

namespace cantordust
{

bool Math_MapPointToImage(
    const int x,
    const int y,
    const int viewportX,
    const int viewportY,
    const int viewportWidth,
    const int viewportHeight,
    const usize imageWidth,
    const usize imageHeight,
    Point_s& result )
{
    if ( viewportWidth <= 0 || viewportHeight <= 0 ||
         imageWidth == 0 || imageHeight == 0 )
    {
        return false;
    }

    const i64 relativeX =
        static_cast< i64 >( x ) - viewportX;

    const i64 relativeY =
        static_cast< i64 >( y ) - viewportY;

    if ( relativeX < 0 || relativeY < 0 ||
         relativeX >= viewportWidth || relativeY >= viewportHeight )
    {
        return false;
    }

    result.x = static_cast< usize >(
        static_cast< u64 >( relativeX ) * imageWidth /
        static_cast< u64 >( viewportWidth ) );

    result.y = static_cast< usize >(
        static_cast< u64 >( relativeY ) * imageHeight /
        static_cast< u64 >( viewportHeight ) );

    return true;
}

} // namespace cantordust
