#ifndef MATH_DENSITY_H
#define MATH_DENSITY_H


#include "cantordust/scalar_types.h"
#include <cstddef>
#include <cstdint>

namespace cantordust
{

f64 Math_LogDensity( u32 count, u32 highest );
usize Math_HistogramHeight(
    u32 count,
    u32 highest,
    usize maximumHeight );

} // namespace cantordust

#endif // MATH_DENSITY_H
