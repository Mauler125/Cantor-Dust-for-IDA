#include "ida/ida_memory.h"
#include "cantordust/scalar_types.h"

#include "math/window.h"

#include <bytes.hpp>

#include <algorithm>

#define CANTORDUST_OVERVIEW_SAMPLE_BUDGET 1048576u

namespace cantordust
{

bool Ida_SupportsEightBitBytes( const ea_t address )
{
    return nbits( address ) == 8;
}

bool Ida_InitializedExtent(
    const ea_t start, const ea_t end,
    ea_t& loadedStart, ea_t& loadedEnd )
{
    loadedStart = BADADDR;
    loadedEnd = BADADDR;

    if ( start >= end )
    {
        return false;
    }

    const ea_t first = is_loaded( start )
        ? start : next_inited( start, end );

    if ( first == BADADDR || first >= end )
    {
        return false;
    }

    const ea_t last = is_loaded( end - 1 )
        ? end - 1 : prev_inited( end, first );

    if ( last == BADADDR || last < first )
    {
        return false;
    }

    loadedStart = first;
    loadedEnd = last + 1;
    return true;
}

IdaWindow_s Ida_ReadWindow( const ea_t address, const usize count )
{
    IdaWindow_s sample;
    sample.cells.resize( count );

    for ( usize i = 0; i < count; ++i )
    {
        const ea_t currentAddress = address + static_cast< ea_t >( i );

        if ( is_loaded( currentAddress ) )
        {
            sample.cells[ i ] = { get_db_byte( currentAddress ), true };
            ++sample.loadedCount;
        }
    }

    return sample;
}

IdaWindow_s Ida_ReadWindowRanges(
    const std::vector< ByteRange_s >& ranges,
    const u64 offset,
    const usize count )
{
    IdaWindow_s sample;
    sample.cells.resize( count );

    for ( usize i = 0; i < count; ++i )
    {
        u64 address = 0;

        if ( Math_OffsetToAddress( ranges, offset + i, address ) &&
             is_loaded( static_cast< ea_t >( address ) ) )
        {
            sample.cells[ i ] = {
                get_db_byte( static_cast< ea_t >( address ) ), true };
            ++sample.loadedCount;
        }
    }

    return sample;
}

IdaOverview_s Ida_ReadOverviewRanges(
    const std::vector< ByteRange_s >& ranges,
    const usize side,
    const u64 requestedOffset,
    const u64 requestedSpan )
{
    IdaOverview_s sample;

    if ( side != 256 && side != 512 && side != 1024 )
    {
        return sample;
    }

    const usize pixels = side * side;
    sample.cells.resize( pixels );
    sample.classes.resize( pixels );
    sample.sourceOffsets.assign( pixels, CANTORDUST_NO_SOURCE );

    const u64 total = Math_RangeLength( ranges );

    if ( requestedOffset >= total )
    {
        return sample;
    }

    const u64 count = std::min(
        requestedSpan, total - requestedOffset );

    if ( count == 0 )
    {
        return sample;
    }

    const usize maxProbes = std::max< usize >(
        1, std::min< usize >(
            8, CANTORDUST_OVERVIEW_SAMPLE_BUDGET / pixels ) );

    u64 previousBegin = count;
    u64 previousNext = count;

    for ( usize i = 0; i < pixels; ++i )
    {
        const u64 begin = Math_ScaledPosition(
            count, static_cast< u32 >( i ),
            static_cast< u32 >( pixels ) );

        const u64 next = Math_ScaledPosition(
            count, static_cast< u32 >( i + 1 ),
            static_cast< u32 >( pixels ) );

        if ( begin >= count )
        {
            continue;
        }

        if ( i > 0 && begin == previousBegin && next == previousNext )
        {
            sample.cells[ i ] = sample.cells[ i - 1 ];
            sample.classes[ i ] = sample.classes[ i - 1 ];
            sample.sourceOffsets[ i ] = sample.sourceOffsets[ i - 1 ];
            sample.loadedCount += sample.cells[ i ].valid ? 1 : 0;
            continue;
        }

        previousBegin = begin;
        previousNext = next;

        const u64 span = std::max< u64 >(
            1, next - begin );

        const u64 probes = std::min< u64 >(
            span, maxProbes );

        ByteClassSummary_s& classes = sample.classes[ i ];

        for ( u64 j = 0; j < probes; ++j )
        {
            const u64 offset = requestedOffset + begin +
                Math_ScaledPosition(
                    span, static_cast< u32 >( j ),
                    static_cast< u32 >( probes ) );

            u64 address = 0;

            if ( !Math_OffsetToAddress( ranges, offset, address ) ||
                 !is_loaded( static_cast< ea_t >( address ) ) )
            {
                continue;
            }

            const u8 byte =
                get_db_byte( static_cast< ea_t >( address ) );

            classes.Add( byte );

            if ( !sample.cells[ i ].valid )
            {
                sample.cells[ i ] = { byte, true };
                sample.sourceOffsets[ i ] =
                    static_cast< usize >( offset );
                ++sample.loadedCount;
            }
        }
    }

    return sample;
}

std::vector< CubeTriplet_s > Ida_ReadCubeOverviewRanges(
    const std::vector< ByteRange_s >& ranges,
    const u64 offset,
    const u64 span,
    const usize maximumSamples, const CubeValueMode_e valueMode )
{
    std::vector< CubeTriplet_s > triplets;

    const u64 total = Math_RangeLength( ranges );

    if ( offset >= total || maximumSamples == 0 )
    {
        return triplets;
    }

    const u64 count = std::min( span, total - offset );
    const usize sampleLength = Cube_SampleLength( valueMode );

    if ( count < sampleLength )
    {
        return triplets;
    }

    const usize samples = static_cast< usize >(
        std::min< u64 >(
            count - sampleLength + 1, std::min< usize >( maximumSamples, 65536 ) ) );

    triplets.reserve( samples );

    for ( usize i = 0; i < samples; ++i )
    {
        const u64 logical = offset + Math_ScaledPosition(
            count - sampleLength, static_cast< u32 >( i ),
            static_cast< u32 >( samples - 1 ) );

        // A color byte and its coordinate triplet share one segment.
        const auto it = std::upper_bound( ranges.begin(), ranges.end(),
            logical, []( const u64 value,
                        const ByteRange_s& range ) {
                return value < range.offset;
            } );

        if ( it == ranges.begin() )
        {
            continue;
        }

        const ByteRange_s& range = *( it - 1 );

        if ( logical < range.offset ||
             logical - range.offset >= range.length ||
             range.length - ( logical - range.offset ) < sampleLength )
        {
            continue;
        }

        const ea_t address = static_cast< ea_t >(
            range.address + logical - range.offset );

        u8 bytes[ 13 ]{};
        bool valid = true;

        for ( usize j = 0; j < sampleLength; ++j )
        {
            const ea_t byteAddress = address + static_cast< ea_t >( j );

            if ( !is_loaded( byteAddress ) )
            {
                valid = false;
                break;
            }

            bytes[ j ] = get_db_byte( byteAddress );
        }

        if ( !valid )
            continue;

        CubeTriplet_s decoded;

        if ( !Cube_DecodeTriplet( bytes, valueMode, decoded ) )
            continue;

        decoded.sourceIndex = static_cast< usize >( logical );
        triplets.push_back( decoded );
    }

    return triplets;
}

IdaOverview_s Ida_ReadOverview(
    const ea_t address, const u64 count )
{
    std::vector< ByteRange_s > ranges;

    if ( count != 0 )
    {
        Math_AddRange( ranges, address, count );
    }

    return Ida_ReadOverviewRanges( ranges, CANTORDUST_SIDE );
}

} // namespace cantordust
