#include "ida/ida_memory.h"
#include "cantordust/renderer.h"
#include "math/window.h"
#include "math/hilbert.h"
#include "math/raster_fit.h"
#include "math/zoom.h"

#ifdef NDEBUG
    #undef NDEBUG
#endif
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <iostream>
#include <map>

static std::map< ea_t, unsigned char > g_loadedBytes;
static std::size_t g_readCount = 0;
static std::size_t g_probeCount = 0;
static int g_bitsPerByte = 8;

bool is_loaded( const ea_t address )
{
    ++g_probeCount;
    return g_loadedBytes.find( address ) != g_loadedBytes.end();
}

unsigned char get_db_byte( const ea_t address )
{
    ++g_readCount;
    return g_loadedBytes.at( address );
}

int nbits( const ea_t )
{
    return g_bitsPerByte;
}

ea_t next_inited( const ea_t address, const ea_t maxAddress )
{
    const auto it = g_loadedBytes.upper_bound( address );
    return it != g_loadedBytes.end() && it->first < maxAddress
        ? it->first : BADADDR;
}

ea_t prev_inited( const ea_t address, const ea_t minAddress )
{
    auto it = g_loadedBytes.lower_bound( address );

    if ( it == g_loadedBytes.begin() )
    {
        return BADADDR;
    }

    --it;
    return it->first >= minAddress ? it->first : BADADDR;
}

static void Tests_InitializedExtents()
{
    g_loadedBytes = {
        { 0x1004u, 0x20u }, { 0x1005u, 0x21u },
        { 0x1006u, 0x22u }, { 0x2000u, 0x40u }
    };

    ea_t start = BADADDR;
    ea_t end = BADADDR;
    assert( cantordust::Ida_InitializedExtent(
        0x1000u, 0x1010u, start, end ) );

    assert( start == 0x1004u && end == 0x1007u );

    std::vector< cantordust::ByteRange_s > ranges;
    assert( cantordust::Math_AddRange( ranges, start, end - start ) );
    assert( cantordust::Math_AddRange( ranges, 0x2000u, 1u ) );
    assert( cantordust::Math_RangeLength( ranges ) == 4 );
    std::uint64_t address = 0;
    assert( cantordust::Math_OffsetToAddress( ranges, 3, address ) );
    assert( address == 0x2000u );

    assert( !cantordust::Ida_InitializedExtent(
        0x3000u, 0x4000u, start, end ) );

    assert( start == BADADDR && end == BADADDR );
    assert( cantordust::Ida_InitializedExtent(
        0x2000u, 0x2001u, start, end ) );

    assert( start == 0x2000u && end == 0x2001u );
    assert( !cantordust::Ida_InitializedExtent(
        0x2000u, 0x2000u, start, end ) );
}

static void Tests_ReadMemoryWindow()
{
    g_loadedBytes = { { 0x1000u, 0u }, { 0x1002u, 0x41u } };
    g_readCount = 0;

    const cantordust::IdaWindow_s sample =
        cantordust::Ida_ReadWindow( 0x1000u, 4 );

    assert( sample.cells.size() == 4 );
    assert( sample.loadedCount == 2 );
    assert( sample.cells[ 0 ].valid && sample.cells[ 0 ].value == 0 );
    assert( !sample.cells[ 1 ].valid );
    assert( sample.cells[ 2 ].valid && sample.cells[ 2 ].value == 0x41 );
    assert( !sample.cells[ 3 ].valid );
    assert( g_readCount == 2 );

    const cantordust::IdaWindow_s empty =
        cantordust::Ida_ReadWindow( 0x1000u, 0 );

    assert( empty.cells.empty() && empty.loadedCount == 0 );
    assert( g_readCount == 2 );
}

static void Tests_ReadOverview()
{
    g_loadedBytes = {
        { 0x2000u, 0x11u },
        { 0x2003u, 0x22u },
        { 0x2007u, 0x33u }
    };
    g_readCount = 0;

    const cantordust::IdaOverview_s sample =
        cantordust::Ida_ReadOverview( 0x2000u, 8 );

    assert( sample.cells.size() == CANTORDUST_WINDOW_SIZE );
    assert( sample.sourceOffsets.size() == CANTORDUST_WINDOW_SIZE );
    assert( sample.loadedCount >= 3 );
    assert( sample.classes.size() == CANTORDUST_WINDOW_SIZE );
    assert( sample.classes[ 0 ].low == 1 );
    assert( sample.cells[ 0 ].valid && sample.cells[ 0 ].value == 0x11 );
    assert( sample.sourceOffsets[ 0 ] == 0 );

    const std::size_t pixelFor3 =
        static_cast< std::size_t >( 3ull * CANTORDUST_WINDOW_SIZE / 8ull );

    assert( sample.cells[ pixelFor3 ].valid );
    assert( sample.sourceOffsets[ pixelFor3 ] == 3 );
    assert( g_readCount >= 3 );
}

static void Tests_ScopedOverview()
{
    std::vector< cantordust::ByteRange_s > ranges;
    assert( cantordust::Math_AddRange( ranges, 0x1000u, 4 ) );
    assert( cantordust::Math_AddRange( ranges, 0x5000u, 4 ) );
    g_loadedBytes = {
        { 0x1000u, 0x41 }, { 0x1003u, 0x42 },
        { 0x5000u, 0x43 }, { 0x5003u, 0x44 }
    };

    const cantordust::IdaOverview_s slice =
        cantordust::Ida_ReadOverviewRanges( ranges, 256, 4, 4 );

    assert( slice.sourceOffsets[ 0 ] == 4 );
    assert( slice.cells[ 0 ].value == 0x43 );
    assert( slice.sourceOffsets[ 3 * 65536 / 4 ] == 7 );
    assert( slice.cells[ 3 * 65536 / 4 ].value == 0x44 );

    const cantordust::IdaOverview_s empty =
        cantordust::Ida_ReadOverviewRanges( ranges, 256, 8, 4 );

    assert( empty.loadedCount == 0 );
}

static void Tests_BoundedOverviewSampling()
{
    g_loadedBytes = { { 0x5000u, 0x20u }, { 0x5001u, 0x80u } };
    g_readCount = 0;
    g_probeCount = 0;

    const cantordust::IdaOverview_s sample =
        cantordust::Ida_ReadOverview( 0x5000u, 131072u );

    assert( sample.loadedCount == 1 );
    assert( sample.classes[ 0 ].printable == 1 );
    assert( sample.classes[ 0 ].high == 1 );
    assert( sample.sourceOffsets[ 0 ] == 0 );
    assert( sample.sourceOffsets[ 1 ] == CANTORDUST_NO_SOURCE );
    assert( g_readCount == 2 );
    assert( g_probeCount <= 8 * CANTORDUST_WINDOW_SIZE );
}

static void Tests_ReadCubeOverview()
{
    std::vector< cantordust::ByteRange_s > ranges;
    assert( cantordust::Math_AddRange( ranges, 0x3000u, 5 ) );
    assert( cantordust::Math_AddRange( ranges, 0x9000u, 5 ) );
    g_loadedBytes = {
        { 0x3000u, 0x10 }, { 0x3001u, 0x20 },
        { 0x3002u, 0x30 }, { 0x3003u, 0x40 },
        { 0x3004u, 0x50 },
        { 0x9000u, 0x80 }, { 0x9001u, 0x90 },
        { 0x9002u, 0xA0 }, { 0x9003u, 0xB0 },
        { 0x9004u, 0xC0 }
    };
    g_readCount = 0;
    g_probeCount = 0;

    const std::vector< cantordust::CubeTriplet_s > sampled =
        cantordust::Ida_ReadCubeOverviewRanges( ranges, 0, 10, 8 );

    assert( sampled.size() == 4 );
    assert( sampled[ 0 ].sourceIndex == 0 );
    assert( sampled[ 0 ].colorByte == 0x10 );
    assert( sampled[ 0 ].x == 0x20 && sampled[ 0 ].z == 0x40 );
    assert( sampled[ 1 ].sourceIndex == 1 );
    assert( sampled[ 2 ].sourceIndex == 5 );
    assert( sampled[ 2 ].colorByte == 0x80 );
    assert( sampled[ 2 ].x == 0x90 && sampled[ 2 ].z == 0xB0 );
    assert( sampled[ 3 ].sourceIndex == 6 );
    assert( g_probeCount <= 18 );
    assert( g_readCount == 16 );

    const std::vector< cantordust::CubeTriplet_s > slice =
        cantordust::Ida_ReadCubeOverviewRanges( ranges, 5, 5, 2 );

    assert( slice.size() == 2 );
    assert( slice[ 0 ].sourceIndex == 5 );
    assert( slice[ 1 ].sourceIndex == 6 );

    assert( cantordust::Ida_ReadCubeOverviewRanges(
        ranges, 0, 3 ).empty() );

    assert( cantordust::Ida_ReadCubeOverviewRanges(
        ranges, 10, 10 ).empty() );

    g_loadedBytes.erase( 0x9001u );
    assert( cantordust::Ida_ReadCubeOverviewRanges(
        ranges, 4, 4, 2 ).empty() );
}

static void Tests_CubeIntegerOverview()
{
    std::vector< cantordust::ByteRange_s > ranges;
    assert( cantordust::Math_AddRange( ranges, 0xA000u, 12 ) );
    assert( cantordust::Math_AddRange( ranges, 0xB000u, 6 ) );
    g_loadedBytes.clear();

    for ( std::size_t i = 0; i < 12; ++i )
        g_loadedBytes[ 0xA000u + i ] = static_cast< unsigned char >( i + 1 );

    for ( std::size_t i = 0; i < 6; ++i )
        g_loadedBytes[ 0xB000u + i ] = static_cast< unsigned char >( i + 17 );

    const auto le = cantordust::Ida_ReadCubeOverviewRanges(
        ranges, 0, 18, 64, cantordust::CCV_U16_LE );

    assert( !le.empty() );
    assert( le.front().sourceIndex == 0 );
    assert( le.front().x == 2 && le.front().y == 4 && le.front().z == 6 );

    for ( const auto& sample : le )
    {
        // A single sample must never bridge the gap between segments.
        assert( sample.sourceIndex <= 6 || sample.sourceIndex >= 12 );
    }

    const auto be = cantordust::Ida_ReadCubeOverviewRanges(
        ranges, 0, 18, 64, cantordust::CCV_U32_BE );

    assert( !be.empty() );
    assert( be.front().x == 1 && be.front().y == 5 && be.front().z == 9 );
    assert( be.front().sourceIndex == 0 );
    assert( cantordust::Ida_ReadCubeOverviewRanges(
        ranges, 12, 6, 8, cantordust::CCV_U32_LE ).empty() );
}

static void Tests_CubeSamplingModesAndAlignment()
{
    using namespace cantordust;
    std::vector< ByteRange_s > ranges;
    assert( Math_AddRange( ranges, 0xE000u, 20 ) );
    assert( Math_AddRange( ranges, 0xF000u, 20 ) );
    g_loadedBytes.clear();
    for ( usize i = 0; i < 20; ++i )
    {
        g_loadedBytes[ 0xE000u + i ] = static_cast< u8 >( i + 1u );
        g_loadedBytes[ 0xF000u + i ] = static_cast< u8 >( i + 21u );
    }

    // Offset alignment is relative to the entire logical range, not the
    // currently visible window. A decoded record may not cross a range join.
    const auto aligned = Ida_ReadCubeOverviewRanges(
        ranges, 5, 34, 64, CCV_XOR8, 7, 3 );
    assert( !aligned.empty() );
    for ( const auto& record : aligned )
    {
        assert( record.sourceIndex % 7 == 3 );
        assert( record.sourceIndex != 17 && record.sourceIndex != 18 &&
            record.sourceIndex != 19 );
    }
    // Other bit-plane positions change the actual decoded coordinates.
    const auto bit0 = Ida_ReadCubeOverviewRanges(
        ranges, 0, 20, 64, CCV_BIT_PLANE, 1, 0, 0 );
    const auto bit7 = Ida_ReadCubeOverviewRanges(
        ranges, 0, 20, 64, CCV_BIT_PLANE, 1, 0, 7 );
    assert( !bit0.empty() && bit0.size() == bit7.size() );
    assert( bit0[ 0 ].x == 0 && bit7[ 0 ].x == 0 );
    assert( bit0[ 0 ].y == 255 && bit7[ 0 ].y == 0 );

    const auto low64 = Ida_ReadCubeOverviewRanges(
        ranges, 0, 40, 64, CCV_U64_LE_LOW );
    assert( low64.empty() ); // 24 bytes must not span 20-byte segments.

    std::vector< ByteRange_s > floats;
    assert( Math_AddRange( floats, 0x11000u, 24 ) );
    g_loadedBytes.clear();
    const auto writeFloat = []( const ea_t where, const f32 value ) {
        u32 bits = 0;
        std::memcpy( &bits, &value, sizeof( bits ) );
        for ( unsigned int i = 0; i < 4; ++i )
            g_loadedBytes[ where + i ] =
                static_cast< u8 >( bits >> ( 8u * i ) );
    };
    writeFloat( 0x11000u, 1.0f );
    writeFloat( 0x11004u, 2.0f );
    writeFloat( 0x11008u, 4.0f );
    writeFloat( 0x1100Cu, -1.0f );
    writeFloat( 0x11010u, -2.0f );
    writeFloat( 0x11014u, -4.0f );

    const auto normalized = Ida_ReadCubeOverviewRanges(
        floats, 0, 24, 64, CCV_FLOAT32_LE, 12 );
    assert( normalized.size() == 2 );
    assert( normalized[ 0 ].sourceIndex == 0 &&
        normalized[ 1 ].sourceIndex == 12 );
    assert( normalized[ 0 ].x > 128 && normalized[ 1 ].x < 128 );
    assert( normalized[ 0 ].z > normalized[ 0 ].x );

    writeFloat( 0x11000u, std::numeric_limits< f32 >::infinity() );
    const auto invalid = Ida_ReadCubeOverviewRanges(
        floats, 0, 24, 64, CCV_FLOAT32_LE, 12 );
    assert( invalid.size() == 1 );
    assert( invalid[ 0 ].sourceIndex == 12 );
}

static void Tests_ByteWidth()
{
    g_bitsPerByte = 8;
    assert( cantordust::Ida_SupportsEightBitBytes( 0x1000u ) );

    g_bitsPerByte = 16;
    assert( !cantordust::Ida_SupportsEightBitBytes( 0x1000u ) );
}

static void Tests_RangeReadsAcrossGaps()
{
    std::vector< cantordust::ByteRange_s > ranges;
    assert( cantordust::Math_AddRange( ranges, 0x1000, 2 ) );
    assert( cantordust::Math_AddRange( ranges, 0x5000, 2 ) );
    g_loadedBytes = {
        { 0x1000, 0x41 }, { 0x1001, 0x42 },
        { 0x5000, 0x43 }, { 0x5001, 0x44 }
    };

    const auto window = cantordust::Ida_ReadWindowRanges( ranges, 1, 3 );
    assert( window.loadedCount == 3 );
    assert( window.cells[ 0 ].value == 0x42 );
    assert( window.cells[ 1 ].value == 0x43 );
    assert( window.cells[ 2 ].value == 0x44 );

    const auto overview = cantordust::Ida_ReadOverviewRanges( ranges, 256 );
    assert( overview.sourceOffsets[ 0 ] == 0 );
    assert( overview.sourceOffsets[ 16384 ] == 1 );
    assert( overview.sourceOffsets[ 32768 ] == 2 );
    assert( overview.cells[ 32768 ].value == 0x43 );
}

static void Tests_LargeDetailSelectionSamplesEntireRange()
{
    using namespace cantordust;
    // Wider than a 256x256 image, starting in the middle of a segment.
    // The last pixel must represent the far end of the selection, rather
    // than bytes immediately after its beginning.
    std::vector< ByteRange_s > ranges;

    const std::uint64_t base = 0x140000000ull;
    const std::uint64_t length = 6ull * CANTORDUST_WINDOW_SIZE;
    assert( Math_AddRange( ranges, base, length ) );

    const std::uint64_t offset = 32768;
    const std::uint64_t span = 4ull * CANTORDUST_WINDOW_SIZE;
    const std::uint64_t midpoint = offset + span / 2;
    const std::uint64_t endSample = offset + Math_ScaledPosition(
        span, CANTORDUST_WINDOW_SIZE - 1, CANTORDUST_WINDOW_SIZE );

    g_loadedBytes = {
        { static_cast< ea_t >( base + offset ), 0x20u },
        { static_cast< ea_t >( base + midpoint ), 0x80u },
        { static_cast< ea_t >( base + endSample ), 0xFFu }
    };
    g_probeCount = 0;

    const IdaOverview_s sampled = Ida_ReadOverviewRanges(
        ranges, 256, offset, span );

    assert( sampled.cells.size() == CANTORDUST_WINDOW_SIZE );
    assert( sampled.sourceOffsets[ 0 ] == offset );
    assert( sampled.sourceOffsets[ CANTORDUST_WINDOW_SIZE / 2 ] == midpoint );
    assert( sampled.sourceOffsets.back() == endSample );
    assert( sampled.cells.back().value == 0xFFu );
    assert( g_probeCount <= 8u * CANTORDUST_WINDOW_SIZE );

    const Image_s linear = RenderOverview( CDV_LINEAR,
        sampled.classes, sampled.cells, sampled.sourceOffsets, 256 );

    const Image_s hilbert = RenderOverview( CDV_HILBERT,
        sampled.classes, sampled.cells, sampled.sourceOffsets, 256 );

    assert( linear.SourceAt( 0, 0 ) == offset );
    assert( linear.SourceAt( 255, 255 ) == endSample );

    const Point_s last = Math_HilbertPosition(
        CANTORDUST_WINDOW_SIZE - 1, 256 );

    assert( hilbert.SourceAt( last.x, last.y ) == endSample );
    std::uint64_t address = 0;
    assert( Math_OffsetToAddress(
        ranges, hilbert.SourceAt( last.x, last.y ), address ) );

    assert( address == base + endSample );

    // 3D Byte Cloud uses the same full logical selection, sampled to a
    // bounded number of triplets rather than its first 65536 bytes.
    const std::uint64_t lastTriplet = offset + span - 4;
    g_loadedBytes.clear();

    for ( std::uint64_t i = 0; i != 4; ++i )
    {
        g_loadedBytes[ static_cast< ea_t >( base + offset + i ) ] =
            static_cast< unsigned char >( 0x10 + i );

        g_loadedBytes[ static_cast< ea_t >( base + lastTriplet + i ) ] =
            static_cast< unsigned char >( 0x20 + i );
    }

    const auto cloudSamples = Ida_ReadCubeOverviewRanges(
        ranges, offset, span, 32 );

    assert( cloudSamples.size() == 2 );
    assert( cloudSamples.front().sourceIndex == offset );
    assert( cloudSamples.back().sourceIndex == lastTriplet );

    // The far end must remain accessible with offsets beyond 32 bits too.
    ranges.clear();

    const std::uint64_t wideLength = ( 1ull << 34 ) + 1000;
    assert( Math_AddRange( ranges, 0x180000000ull, wideLength ) );

    const std::uint64_t far = Math_ScaledPosition(
        wideLength, CANTORDUST_WINDOW_SIZE - 1, CANTORDUST_WINDOW_SIZE );

    g_loadedBytes = {
        { static_cast< ea_t >( 0x180000000ull + far ), 0x41u }
    };

    const IdaOverview_s wide = Ida_ReadOverviewRanges(
        ranges, 256, 0, wideLength );

    assert( wide.sourceOffsets.back() == far );
    assert( wide.sourceOffsets.back() > 0xFFFFFFFFull );
}

static void Tests_HigherResolutionBudget()
{
    std::vector< cantordust::ByteRange_s > ranges;
    assert( cantordust::Math_AddRange( ranges, 0x8000, 65536 ) );
    g_loadedBytes = { { 0x8000, 0x20 } };
    g_probeCount = 0;

    const auto high = cantordust::Ida_ReadOverviewRanges( ranges, 512 );
    assert( high.cells.size() == 512u * 512u );
    assert( g_probeCount <= 524288 );
}

static void Tests_SmallSegmentFitAndZoomLimit()
{
    using namespace cantordust;

    // A segment can have a large virtual range around a small initialized
    // region. The panel now fits that initialized region on selection.
    constexpr ea_t segmentStart = 0x3000;
    constexpr ea_t loadedStart = 0x3800;
    constexpr usize count = 17;
    g_loadedBytes.clear();
    for ( usize i = 0; i < count; ++i )
        g_loadedBytes[ loadedStart + i ] = static_cast< u8 >( 0x30 + i );

    ea_t first = BADADDR;
    ea_t end = BADADDR;
    assert( Ida_InitializedExtent(
        segmentStart, 0x8000, first, end ) );
    assert( first == loadedStart && end == loadedStart + count );

    std::vector< ByteRange_s > ranges;
    assert( Math_AddRange( ranges, first, end - first ) );
    const u64 fitLength = Math_RangeLength( ranges );
    assert( fitLength == count );

    const IdaWindow_s window = Ida_ReadWindowRanges(
        ranges, 0, static_cast< usize >( fitLength ) );
    assert( window.loadedCount == fitLength );

    for ( const View_e mode : { CDV_LINEAR, CDV_HILBERT } )
    {
        const Image_s detail = RenderDetail(
            mode, window.cells, 256 );
        const FittedRaster_s fitted = Math_FitByteRaster(
            detail, mode, 1400, 260 );
        const Image_s& image = fitted.image;

        // No blank tail in the default square view or after a wide resize.
        for ( const usize ordinal : { 0u, 1u, 35000u, 65535u } )
        {
            const Point_s point = mode == CDV_HILBERT
                ? Math_HilbertPosition( ordinal, 256 )
                : Point_s{ ordinal % 256, ordinal / 256 };
            assert( detail.SourceAt( point.x, point.y ) !=
                CANTORDUST_NO_SOURCE );
        }

        for ( usize pixel = 0; pixel < image.sourceIndex.size(); ++pixel )
            assert( image.sourceIndex[ pixel ] != CANTORDUST_NO_SOURCE );

        // Repeated display pixels preserve real byte addresses, not a
        // synthetic offset into the larger segment's padding.
        const usize last = image.sourceIndex.back();
        assert( last < count );
        u64 address = 0;
        assert( Math_OffsetToAddress( ranges, last, address ) );
        assert( address >= loadedStart && address < loadedStart + count );
    }

    const ZoomRange_s fit{ 0, fitLength };
    assert( Math_WheelZoom( fitLength, fit, 8, 1,
        fitLength, -32 ).length == fitLength );
    assert( Math_WheelZoom( fitLength, fit, 8, 1,
        fitLength, -32 ).start == 0 );
    const ZoomRange_s closer = Math_WheelZoom(
        fitLength, fit, 8, 1, fitLength, 1 );
    assert( closer.length < fitLength );
    const ZoomRange_s back = Math_WheelZoom(
        fitLength, closer, 8, 1, fitLength, -32 );
    assert( back.start == 0 && back.length == fitLength );
}

int main()
{
    Tests_InitializedExtents();
    Tests_ReadMemoryWindow();
    Tests_ReadOverview();
    Tests_ScopedOverview();
    Tests_BoundedOverviewSampling();
    Tests_ReadCubeOverview();
    Tests_CubeIntegerOverview();
    Tests_CubeSamplingModesAndAlignment();
    Tests_ByteWidth();
    Tests_RangeReadsAcrossGaps();
    Tests_LargeDetailSelectionSamplesEntireRange();
    Tests_HigherResolutionBudget();
    Tests_SmallSegmentFitAndZoomLimit();
    std::cout << "IDA memory adapter mock tests passed.\n";
    return 0;
}
