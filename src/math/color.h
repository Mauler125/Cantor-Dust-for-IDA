#ifndef MATH_COLOR_H
#define MATH_COLOR_H


#include "cantordust/scalar_types.h"
#include <cstddef>
#include <cstdint>

namespace cantordust
{

u32 Math_BlendColor(
    u32 start, u32 end, f64 weight );

u32 Math_WeightedColor(
    const u32* colors,
    const u32* weights,
    usize count );

} // namespace cantordust

#endif // MATH_COLOR_H
