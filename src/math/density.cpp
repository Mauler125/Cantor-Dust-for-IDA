#include "math/density.h"
#include "cantordust/scalar_types.h"

#include <algorithm>
#include <cmath>

namespace cantordust
{

using std::ceil;

f64 Math_LogDensity(
    const u32 count,
    const u32 highest )
{
    if ( count == 0 || highest == 0 )
    {
        return 0.0;
    }

    return std::log1p( static_cast< f64 >( count ) ) /
           std::log1p( static_cast< f64 >( highest ) );
}

usize Math_HistogramHeight(
    const u32 count,
    const u32 highest,
    const usize maximumHeight )
{
    if ( count == 0 || highest == 0 || maximumHeight == 0 )
    {
        return 0;
    }

    const f64 scaled = ceil(
        static_cast< f64 >( maximumHeight ) * Math_LogDensity( count, highest ) );

    return std::max< usize >( 1, static_cast< usize >( scaled ) );
}

} // namespace cantordust
