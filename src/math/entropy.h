#ifndef MATH_ENTROPY_H
#define MATH_ENTROPY_H


#include "cantordust/scalar_types.h"
#include "cantordust/types.h"

#include <cstddef>
#include <vector>

namespace cantordust
{

struct EntropySample_s
{
    f64 bits = 0.0;
    bool valid = false;
};

f64 Math_ShannonEntropy( const std::vector< ByteCell_s >& cells );
std::vector< EntropySample_s > Math_EntropyProfile(
    const std::vector< ByteCell_s >& cells,
    usize count );

} // namespace cantordust

#endif // MATH_ENTROPY_H
