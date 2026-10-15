#ifndef MATH_RASTER_FIT_H
#define MATH_RASTER_FIT_H

#include "cantordust/types.h"

#include <vector>

namespace cantordust
{

// The render resolution controls the number of byte samples, not the
// on-screen aspect ratio. A fitted raster retains every sample while
// distributing it over a rectangle close to the current viewport shape.
struct FittedRaster_s
{
    Image_s image;
    // For navigation and selections, one sample ordinal per output pixel.
    std::vector< usize > ordinal;
};

FittedRaster_s Math_FitByteRaster(
    const Image_s& source, View_e mode,
    int viewportWidth, int viewportHeight );

} // namespace cantordust

#endif // MATH_RASTER_FIT_H
