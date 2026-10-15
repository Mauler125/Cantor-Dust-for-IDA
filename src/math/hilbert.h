#ifndef MATH_HILBERT_H
#define MATH_HILBERT_H


#include "cantordust/scalar_types.h"
#include "cantordust/types.h"

#include <vector>

namespace cantordust
{

Point_s Math_HilbertPosition( usize index, usize side );
Point_s Math_HilbertPosition( usize index );
usize Math_HilbertIndex( Point_s point, usize side );
// Pixel offsets in Hilbert traversal order. Built lazily for each resolution.
// Use 32-bit offsets: the largest supported raster has 1,048,576 pixels.
const std::vector< u32 >& Math_HilbertPixelOffsets( usize side );

} // namespace cantordust

#endif // MATH_HILBERT_H
