#ifndef CANTORDUST_TYPES_H
#define CANTORDUST_TYPES_H


#include "cantordust/scalar_types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#define CANTORDUST_SIDE 256u
#define CANTORDUST_WINDOW_SIZE ( CANTORDUST_SIDE * CANTORDUST_SIDE )
#define CANTORDUST_NO_SOURCE ( static_cast< std::size_t >( -1 ) )

namespace cantordust
{

struct ByteCell_s
{
    u8 value = 0;
    bool valid = false;
};

struct ByteClassSummary_s
{
    u8 zero = 0;
    u8 low = 0;
    u8 printable = 0;
    u8 high = 0;
    u8 full = 0;

    void Add( const u8 value )
    {
        if ( value == 0 )
        {
            ++zero;
        }
        else if ( value < 0x20 )
        {
            ++low;
        }
        else if ( value <= 0x7F )
        {
            ++printable;
        }
        else if ( value < 0xFF )
        {
            ++high;
        }
        else
        {
            ++full;
        }
    }

    unsigned int SampleCount() const
    {
        return zero + low + printable + high + full;
    }
};

enum View_e
{
    CDV_LINEAR = 0,
    CDV_DIGRAPH,
    CDV_HISTOGRAM,
    CDV_HILBERT,
    CDV_CUBE3D,
    CDV_UNIVERSE3D,
};

struct Point_s
{
    usize x = 0;
    usize y = 0;
};

struct Image_s
{
    usize width = CANTORDUST_SIDE;
    usize height = CANTORDUST_SIDE;
    std::vector< u32 > argb;
    std::vector< usize > sourceIndex;
    // Populated for Histogram (256 bins) and Digraph (65,536 bins).
    // Counts describe the actual rendered sample, including Overview.
    std::vector< u32 > bucketCounts;
    std::array< u32, 256 > firstByteTotals{};
    usize observationCount = 0;
    usize pairDistance = 1;
    bool conditionalProbability = false;

    usize SourceAt( const usize x, const usize y ) const
    {
        if ( x >= width || y >= height )
        {
            return CANTORDUST_NO_SOURCE;
        }

        const usize index = y * width + x;
        return index < sourceIndex.size()
            ? sourceIndex[ index ] : CANTORDUST_NO_SOURCE;
    }
};

} // namespace cantordust

#endif // CANTORDUST_TYPES_H
