#include "math/entropy.h"
#include "cantordust/scalar_types.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cantordust
{

static EntropySample_s Entropy_Analyze(
    const ByteCell_s* const begin,
    const usize count )
{
    std::array< usize, 256 > frequencies{};
    usize validCount = 0;

    for ( usize i = 0; i < count; ++i )
    {
        if ( begin[ i ].valid )
        {
            ++frequencies[ begin[ i ].value ];
            ++validCount;
        }
    }

    EntropySample_s result;

    if ( validCount == 0 )
    {
        return result;
    }

    result.valid = true;

    for ( const usize frequency : frequencies )
    {
        if ( frequency == 0 )
        {
            continue;
        }

        const f64 probability = static_cast< f64 >( frequency ) /
            static_cast< f64 >( validCount );

        result.bits -= probability * std::log2( probability );
    }

    return result;
}

f64 Math_ShannonEntropy( const std::vector< ByteCell_s >& cells )
{
    return Entropy_Analyze( cells.data(), cells.size() ).bits;
}

std::vector< EntropySample_s > Math_EntropyProfile(
    const std::vector< ByteCell_s >& cells,
    const usize count )
{
    std::vector< EntropySample_s > result( count );

    if ( cells.empty() || count == 0 )
    {
        return result;
    }

    const usize quotient = cells.size() / count;
    const usize remainder = cells.size() % count;

    for ( usize i = 0; i < count; ++i )
    {
        const usize begin = quotient * i + remainder * i / count;
        const usize end = quotient * ( i + 1 ) +
            remainder * ( i + 1 ) / count;

        if ( begin < end )
        {
            result[ i ] = Entropy_Analyze( cells.data() + begin, end - begin );
        }
    }

    return result;
}

} // namespace cantordust
