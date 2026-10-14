#ifndef MATH_WINDOW_H
#define MATH_WINDOW_H


#include "cantordust/scalar_types.h"
#include <cstddef>
#include <cstdint>

namespace cantordust
{

u64 Math_ScaledPosition(
    u64 maximumOffset,
    u32 position,
    u32 maximumPosition );

u32 Math_PositionForOffset(
    u64 maximumOffset,
    u64 offset,
    u32 maximumPosition );

usize Math_OverviewPixelForOffset(
    u64 totalByteCount,
    u64 offset,
    usize pixelCount );

void Math_OverviewPixelSpan(
    u64 totalByteCount,
    u64 offset,
    u64 byteCount,
    usize pixelCount,
    usize& startPixel,
    usize& endPixel );

} // namespace cantordust

#endif // MATH_WINDOW_H
