#ifndef IDA_IDA_MEMORY_H
#define IDA_IDA_MEMORY_H


#include "cantordust/scalar_types.h"
#include "cantordust/types.h"
#include "cantordust/cube.h"
#include "math/ranges.h"

#include <ida.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cantordust
{

struct IdaWindow_s
{
    std::vector< ByteCell_s > cells;
    usize loadedCount = 0;
};

struct IdaOverview_s
{
    std::vector< ByteCell_s > cells;
    std::vector< usize > sourceOffsets;
    std::vector< ByteClassSummary_s > classes;
    usize loadedCount = 0;
};

bool Ida_SupportsEightBitBytes( ea_t address );
bool Ida_InitializedExtent(
    ea_t start, ea_t end, ea_t& loadedStart, ea_t& loadedEnd );

IdaWindow_s Ida_ReadWindow( ea_t address, usize count );
IdaWindow_s Ida_ReadWindowRanges(
    const std::vector< ByteRange_s >& ranges,
    u64 offset, usize count );

std::vector< CubeTriplet_s > Ida_ReadCubeOverviewRanges(
    const std::vector< ByteRange_s >& ranges,
    u64 offset, u64 span,
    usize maximumSamples = 65536,
    CubeValueMode_e valueMode = CCV_U8,
    usize stride = 0, usize alignment = 0, unsigned int bitPlane = 0 );

IdaOverview_s Ida_ReadOverview( ea_t address, u64 count );
IdaOverview_s Ida_ReadOverviewRanges(
    const std::vector< ByteRange_s >& ranges, usize side,
    u64 offset = 0, u64 span = UINT64_MAX );

} // namespace cantordust

#endif // IDA_IDA_MEMORY_H
