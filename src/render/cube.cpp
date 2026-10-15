#include "pch/core_pch.h"
#include "cantordust/cube.h"
#include "cantordust/scalar_types.h"
#include "cantordust/constants.h"

#include "math/color.h"
#include "math/cube_lighting.h"
#include "math/cube.h"
#include "tier0/compiler.h"

#define CANTORDUST_CUBE_VOXELS ( CANTORDUST_CUBE_GRID * \
    CANTORDUST_CUBE_GRID * CANTORDUST_CUBE_GRID )

#include <array>
#include <cstring>

namespace cantordust
{

using std::ceil;
using std::sin;
using std::cos;
using std::sqrt;

bool Cube_IsFloatMode( const CubeValueMode_e mode )
{
    return mode == CCV_FLOAT32_LE || mode == CCV_FLOAT32_BE;
}

usize Cube_SampleLength( const CubeValueMode_e mode )
{
    switch ( mode )
    {
    case CCV_U16_LE: case CCV_U16_BE:
    case CCV_U16_LE_LOW: case CCV_U16_BE_LOW: return 6u;
    case CCV_U32_LE: case CCV_U32_BE:
    case CCV_U32_LE_LOW: case CCV_U32_BE_LOW:
    case CCV_FLOAT32_LE: case CCV_FLOAT32_BE: return 12u;
    case CCV_U64_LE: case CCV_U64_BE:
    case CCV_U64_LE_LOW: case CCV_U64_BE_LOW: return 24u;
    default: return 4u;
    }
}

usize Cube_DefaultStride( const CubeValueMode_e mode )
{
    const usize length = Cube_SampleLength( mode );
    return length == 24u ? 8u : length == 12u ? 4u :
        length == 6u ? 2u : 1u;
}

u64 Cube_FirstAligned( const u64 start, const u64 stride, const u64 alignment )
{
    if ( stride == 0 )
        return start;

    const u64 desired = alignment % stride;
    const u64 remainder = start % stride;
    const u64 delta = desired >= remainder ? desired - remainder :
        stride - ( remainder - desired );
    return delta > UINT64_MAX - start ? UINT64_MAX : start + delta;
}

static bool Cube_ReadFloat( const u8* const bytes,
    const bool bigEndian, f32& value )
{
    u32 bits = 0;

    for ( unsigned int i = 0; i < 4u; ++i )
        bits |= static_cast< u32 >( bytes[ i ] ) <<
            ( bigEndian ? 8u * ( 3u - i ) : 8u * i );

    static_assert( sizeof( f32 ) == sizeof( u32 ), "Float32 required" );
    std::memcpy( &value, &bits, sizeof( value ) );
    return std::isfinite( value );
}

bool Cube_DecodeFloatTriplet( const u8* const bytes,
    const CubeValueMode_e mode, f32& x, f32& y, f32& z )
{
    if ( bytes == nullptr || !Cube_IsFloatMode( mode ) )
        return false;

    const bool big = mode == CCV_FLOAT32_BE;
    return Cube_ReadFloat( bytes, big, x ) &&
        Cube_ReadFloat( bytes + 4, big, y ) &&
        Cube_ReadFloat( bytes + 8, big, z );
}

void Cube_ObserveFloat( CubeFloatScale_s& scale,
    const f32 x, const f32 y, const f32 z )
{
    for ( const f32 value : { x, y, z } )
    {
        if ( value == 0.0f || !std::isfinite( value ) )
            continue;

        int exponent = 0;
        std::frexp( std::abs( value ), &exponent );
        const usize index = static_cast< usize >(
            std::clamp( exponent + 149, 0, 279 ) );
        ++scale.histogram[ index ];
        ++scale.samples;
    }
}

f32 Cube_FloatScale( const CubeFloatScale_s& scale )
{
    if ( scale.samples == 0 )
        return 1.0f;

    // The 97th percentile avoids allowing rare exponent outliers to
    // flatten every ordinary coordinate. The same scale is used on XYZ.
    const u64 target = scale.samples - scale.samples / 33u;
    u64 total = 0;

    for ( usize index = 0; index < scale.histogram.size(); ++index )
    {
        total += scale.histogram[ index ];
        if ( total >= target )
        {
            const int exponent = static_cast< int >( index ) - 149;
            return std::clamp( std::ldexp( 1.0f, exponent ),
                std::numeric_limits< f32 >::denorm_min(),
                std::numeric_limits< f32 >::max() );
        }
    }
    return 1.0f;
}

u8 Cube_FloatCoordinate( const f32 value, const f32 scale )
{
    // Divide in double precision to remain finite for the full f32 range.
    const f64 normalized = static_cast< f64 >( value ) /
        static_cast< f64 >( scale );
    return static_cast< u8 >( std::clamp( 127.5 + 127.5 * normalized,
        0.0, 255.0 ) + 0.5 );
}

bool Cube_DecodeTriplet( const u8* const bytes,
    const CubeValueMode_e mode, CubeTriplet_s& result,
    const unsigned int bitPlane, const f32 floatScale )
{
    if ( bytes == nullptr || mode < CCV_U8 || mode >= CCV_COUNT ||
         bitPlane > 7u )
        return false;

    if ( Cube_IsFloatMode( mode ) )
    {
        f32 x = 0.0f, y = 0.0f, z = 0.0f;
        if ( !Cube_DecodeFloatTriplet( bytes, mode, x, y, z ) )
            return false;
        result = { Cube_FloatCoordinate( x, floatScale ),
            Cube_FloatCoordinate( y, floatScale ),
            Cube_FloatCoordinate( z, floatScale ), bytes[ 0 ] };
        return true;
    }

    if ( mode == CCV_NIBBLE8 )
    {
        result = { static_cast< u8 >( ( bytes[ 1 ] >> 4 ) * 17u ),
            static_cast< u8 >( ( bytes[ 1 ] & 0xFu ) * 17u ),
            static_cast< u8 >( ( bytes[ 2 ] >> 4 ) * 17u ), bytes[ 0 ] };
        return true;
    }

    const unsigned int width = static_cast< unsigned int >(
        Cube_DefaultStride( mode ) );
    const bool big = mode == CCV_U16_BE || mode == CCV_U32_BE ||
        mode == CCV_U64_BE || mode == CCV_U16_BE_LOW ||
        mode == CCV_U32_BE_LOW || mode == CCV_U64_BE_LOW;
    const bool low = mode == CCV_U16_LE_LOW || mode == CCV_U16_BE_LOW ||
        mode == CCV_U32_LE_LOW || mode == CCV_U32_BE_LOW ||
        mode == CCV_U64_LE_LOW || mode == CCV_U64_BE_LOW;

    const auto decode = [&]( const unsigned int offset ) -> u8 {
        if ( mode == CCV_DELTA8 )
            return static_cast< u8 >( bytes[ offset ] - bytes[ offset - 1u ] );
        if ( mode == CCV_SIGNED_DELTA8 )
        {
            const int delta = static_cast< int >( bytes[ offset ] ) -
                static_cast< int >( bytes[ offset - 1u ] );
            return static_cast< u8 >( ( delta + 255 ) / 2 );
        }
        if ( mode == CCV_XOR8 )
            return bytes[ offset ] ^ bytes[ offset - 1u ];
        if ( mode == CCV_BIT_PLANE )
            return ( bytes[ offset ] & ( 1u << bitPlane ) ) ? 255u : 0u;
        if ( mode == CCV_POPCOUNT8 )
        {
            static constexpr auto counts = []() constexpr {
                std::array< u8, 256 > table{};
                for ( unsigned int byte = 0; byte < table.size(); ++byte )
                {
                    unsigned int value = byte;
                    unsigned int count = 0;
                    while ( value != 0 )
                    {
                        count += value & 1u;
                        value >>= 1u;
                    }
                    table[ byte ] = static_cast< u8 >( count * 255u / 8u );
                }
                return table;
            }();
            return counts[ bytes[ offset ] ];
        }
        const unsigned int index = big
            ? ( low ? width - 1u : 0u )
            : ( low ? 0u : width - 1u );
        return bytes[ offset + index ];
    };

    const unsigned int first = width == 1u ? 1u : 0u;
    result = { decode( first ), decode( first + width ),
        decode( first + 2u * width ), bytes[ 0 ] };
    return true;
}

struct CubeAccumulator_s
{
    u32 count = 0;
    u32 sumX = 0;
    u32 sumY = 0;
    u32 sumZ = 0;
    usize firstIndex = CANTORDUST_NO_SOURCE;
    u8 firstByte = 0;
};

static void Cube_Accumulate(
    std::vector< CubeAccumulator_s >& bins,
    CubeCloud_s& cloud,
    const unsigned int x, const unsigned int y, const unsigned int z,
    const u8 colorByte, const usize sourceIndex )
{
    const usize key =
        ( ( x >> 1 ) << 14 ) | ( ( y >> 1 ) << 7 ) | ( z >> 1 );

    CubeAccumulator_s& bin = bins[ key ];

    if ( bin.count == 0 )
    {
        bin.firstIndex = sourceIndex;
        bin.firstByte = colorByte;
        ++cloud.occupiedVoxels;
    }
    ++bin.count;
    bin.sumX += x;
    bin.sumY += y;
    bin.sumZ += z;
    ++cloud.acceptedTriplets;
}

static void Cube_FinishCloud(
    CubeCloud_s& cloud,
    const std::vector< CubeAccumulator_s >& bins,
    const Palette_e palette,
    const usize maximumPoints )
{
    std::vector< u32 > occupied;
    occupied.reserve( cloud.occupiedVoxels );

    for ( u32 i = 0; i < CANTORDUST_CUBE_VOXELS; ++i )
    {
        if ( bins[ i ].count != 0 )
        {
            occupied.push_back( i );
        }
    }

    const usize pointLimit = std::min< usize >(
        maximumPoints, CANTORDUST_CUBE_MAX_POINTS );

    if ( occupied.size() > pointLimit )
    {
        std::nth_element( occupied.begin(),
            occupied.begin() + static_cast< std::ptrdiff_t >( pointLimit ),
            occupied.end(), [&bins]( const u32 a,
                                     const u32 b ) {
                return bins[ a ].count != bins[ b ].count
                    ? bins[ a ].count > bins[ b ].count : a < b;
            } );

        occupied.resize( pointLimit );
    }

    cloud.points.reserve( occupied.size() );

    const MathCubeAxes_s axes = Math_CubeAxes( palette );

    for ( const u32 key : occupied )
    {
        const CubeAccumulator_s& bin = bins[ key ];
        const f32 invCount = 1.0f / static_cast< f32 >( bin.count );
        const f32 x = static_cast< f32 >( bin.sumX ) * invCount;
        const f32 y = static_cast< f32 >( bin.sumY ) * invCount;
        const f32 z = static_cast< f32 >( bin.sumZ ) * invCount;
        cloud.points.push_back({
            x / 127.5f - 1.0f,
            y / 127.5f - 1.0f,
            z / 127.5f - 1.0f,
            bin.count,
            bin.firstIndex,
            Math_ColorPack( Math_CubeAxisColor(
                bin.sumX / bin.count, bin.sumY / bin.count,
                bin.sumZ / bin.count, axes ) ),
            bin.firstByte
        });
    }
}

CubeCloud_s Cube_BuildCloud(
    const std::vector< ByteCell_s >& cells,
    const Palette_e palette,
    const usize maximumPoints,
    const std::vector< usize >& boundaryOffsets,
    const CubeValueMode_e mode,
    const usize requestedStride,
    const usize alignment,
    const unsigned int bitPlane,
    const u64 logicalOffset )
{
    CubeCloud_s cloud;
    if ( mode < CCV_U8 || mode >= CCV_COUNT || bitPlane > 7u )
        return cloud;

    const usize sampleLength = Cube_SampleLength( mode );
    if ( cells.size() < sampleLength || maximumPoints == 0 )
        return cloud;

    const usize stride = requestedStride == 0
        ? Cube_DefaultStride( mode ) : requestedStride;
    if ( stride == 0 )
        return cloud;

    const u64 first = Cube_FirstAligned( logicalOffset, stride, alignment );
    if ( first < logicalOffset || first - logicalOffset >
         cells.size() - sampleLength )
        return cloud;

    const usize begin = static_cast< usize >( first - logicalOffset );
    // Reuse identical validation and boundary rules for both float-scale
    // analysis and cloud accumulation. This avoids an extra sample buffer.
    const auto scan = [&]( auto&& accept ) {
        usize boundary = 0;
        for ( usize i = begin; i <= cells.size() - sampleLength; )
        {
            while ( boundary < boundaryOffsets.size() &&
                    boundaryOffsets[ boundary ] <= i )
                ++boundary;

            if ( boundary == boundaryOffsets.size() ||
                 boundaryOffsets[ boundary ] >= i + sampleLength )
            {
                u8 bytes[ 24 ]{};
                bool valid = true;
                for ( usize j = 0; j < sampleLength; ++j )
                {
                    if ( V_UNLIKELY( !cells[ i + j ].valid ) )
                    {
                        valid = false;
                        break;
                    }
                    bytes[ j ] = cells[ i + j ].value;
                }
                if ( valid )
                    accept( bytes, i );
            }
            if ( stride > cells.size() - sampleLength - i )
                break;
            i += stride;
        }
    };

    f32 floatScale = 1.0f;
    if ( Cube_IsFloatMode( mode ) )
    {
        CubeFloatScale_s histogram;
        scan( [&]( const u8* bytes, const usize ) {
            f32 x = 0.0f, y = 0.0f, z = 0.0f;
            if ( Cube_DecodeFloatTriplet( bytes, mode, x, y, z ) )
                Cube_ObserveFloat( histogram, x, y, z );
        } );
        floatScale = Cube_FloatScale( histogram );
    }

    std::vector< CubeAccumulator_s > bins( CANTORDUST_CUBE_VOXELS );
    scan( [&]( const u8* bytes, const usize i ) {
        CubeTriplet_s decoded;
        if ( Cube_DecodeTriplet( bytes, mode, decoded,
                bitPlane, floatScale ) )
            Cube_Accumulate( bins, cloud, decoded.x, decoded.y, decoded.z,
                decoded.colorByte, i );
    } );

    Cube_FinishCloud( cloud, bins, palette, maximumPoints );
    return cloud;
}

CubeCloud_s Cube_BuildTriplets(
    const std::vector< CubeTriplet_s >& triplets,
    const Palette_e palette,
    const usize maximumPoints )
{
    CubeCloud_s cloud;

    if ( triplets.empty() || maximumPoints == 0 )
    {
        return cloud;
    }

    std::vector< CubeAccumulator_s > bins( CANTORDUST_CUBE_VOXELS );

    for ( const CubeTriplet_s& triplet : triplets )
    {
        if ( triplet.sourceIndex == CANTORDUST_NO_SOURCE )
        {
            continue;
        }

        Cube_Accumulate( bins, cloud, triplet.x, triplet.y,
            triplet.z, triplet.colorByte, triplet.sourceIndex );
    }

    Cube_FinishCloud( cloud, bins, palette, maximumPoints );
    return cloud;
}

static void Cube_PickPixel( Image_s& image,
    std::vector< f32 >& depthBuffer,
    const usize pixel, const f32 depth,
    const usize source )
{
    if ( depth <= depthBuffer[ pixel ] )
    {
        return;
    }

    depthBuffer[ pixel ] = depth;
    image.sourceIndex[ pixel ] = source;
}

static void Cube_BlendPixel( Image_s& image,
    const int x, const int y,
    const u32 color, const f32 alpha )
{
    if ( x < 0 || y < 0 ||
         x >= static_cast< int >( image.width ) ||
         y >= static_cast< int >( image.height ) )
    {
        return;
    }

    const usize pixel = static_cast< usize >( y ) * image.width +
        static_cast< usize >( x );

    const MathColor_s dst = Math_ColorUnpack( image.argb[ pixel ] );
    const MathColor_s src = Math_ColorUnpack( color );
    image.argb[ pixel ] = Math_ColorPack({
        dst.red + ( src.red - dst.red ) * alpha,
        dst.green + ( src.green - dst.green ) * alpha,
        dst.blue + ( src.blue - dst.blue ) * alpha
    });
}

static void Cube_DrawLine( Image_s& image,
    const int ax, const int ay, const int bx, const int by,
    const u32 color, const f32 alpha )
{
    const int dx = bx - ax;
    const int dy = by - ay;
    const int steps = std::max( std::abs( dx ), std::abs( dy ) );

    if ( steps == 0 || steps > 10000 )
    {
        return;
    }

    for ( int i = 0; i <= steps; ++i )
    {
        const int x = ax + dx * i / steps;
        const int y = ay + dy * i / steps;
        Cube_BlendPixel( image, x, y, color, alpha );
    }
}

static f32 Cube_AutoExposure( const f32* const red,
    const f32* const green, const f32* const blue, const usize pixelCount )
{
    constexpr usize bins = 512;
    std::array< u32, bins > histogram{};
    usize occupied = 0;
    f32 maximum = 0.0f;

    for ( usize i = 0; i < pixelCount; ++i )
    {
        const f32 luminance = ( red[ i ] + green[ i ] + blue[ i ] ) / 3.0f;

        if ( luminance <= 0.0f )
            continue;

        maximum = std::max( maximum, luminance );
        ++occupied;
    }

    if ( occupied == 0 || maximum <= 0.0f )
        return 1.0f;

    for ( usize i = 0; i < pixelCount; ++i )
    {
        const f32 luminance = ( red[ i ] + green[ i ] + blue[ i ] ) / 3.0f;

        if ( luminance <= 0.0f )
            continue;

        const f32 normalized = std::clamp( luminance / maximum, 0.0f, 1.0f );
        const usize index = std::min< usize >( bins - 1u,
            static_cast< usize >( normalized * static_cast< f32 >( bins - 1u ) ) );
        ++histogram[ index ];
    }

    const usize highTarget = std::max< usize >( 1u, occupied * 97u / 100u );
    const usize middleTarget = std::max< usize >( 1u, occupied * 70u / 100u );
    usize cumulative = 0;
    f32 high = maximum;
    f32 middle = maximum;
    bool middleFound = false;

    for ( usize index = 0; index < bins; ++index )
    {
        cumulative += histogram[ index ];

        if ( !middleFound && cumulative >= middleTarget )
        {
            middle = maximum * ( static_cast< f32 >( index ) + 0.5f ) /
                static_cast< f32 >( bins );
            middleFound = true;
        }

        if ( cumulative >= highTarget )
        {
            high = maximum * ( static_cast< f32 >( index ) + 0.5f ) /
                static_cast< f32 >( bins );
            break;
        }
    }

    const f32 highExposure = 0.82f / std::max( high, 0.0001f );
    const f32 middleExposure = 0.22f / std::max( middle, 0.0001f );
    return std::clamp( std::min( highExposure, middleExposure * 2.35f ),
        0.35f, 6.0f );
}

static void Cube_MapGeometry( const CubePoint_s& point,
    const CubeGeometry_e mode, f32& x, f32& y, f32& z )
{
    x = point.x;
    y = point.y;
    z = point.z;

    if ( mode == CCG_SPHERE )
    {
        const f32 angle = CANTORDUST_PI_F * x;
        const f32 elevation = CANTORDUST_PI_F * ( y + 1.0f ) * 0.5f;
        const f32 radius = ( z + 1.0f ) * 0.5f;
        x = radius * sin( elevation ) * cos( angle );
        z = radius * sin( elevation ) * sin( angle );
        y = radius * cos( elevation );
    }
    else if ( mode == CCG_CYLINDER )
    {
        const f32 angle = CANTORDUST_PI_F * x;
        const f32 radius = ( z + 1.0f ) * 0.5f;
        x = radius * cos( angle );
        z = radius * sin( angle );
    }
    else if ( mode == CCG_TETRAHEDRON )
    {
        Math_CubeMapTetrahedron( x, y, z );
    }
}

void Cube_RenderInto( Image_s& image,
    std::vector< f32 >& depthBuffer,
    CubeRenderScratch_s& scratch,
    const CubeCloud_s& cloud, const CubeCamera_s& camera,
    const usize width, const usize height,
    const Palette_e palette, const CubeRenderOptions_s options )
{
    image.width = width;
    image.height = height;

    if ( width == 0 || height == 0 || width > 2048 || height > 2048 )
    {
        image.argb.clear();
        image.sourceIndex.clear();
        depthBuffer.clear();
        scratch.red.clear();
        scratch.green.clear();
        scratch.blue.clear();
        return;
    }

    const usize pixelCount = width * height;
    const Palette_s& colors = Palette_Get( palette );
    image.argb.assign( pixelCount, colors.background3d );
    image.sourceIndex.assign( pixelCount, CANTORDUST_NO_SOURCE );
    depthBuffer.assign( pixelCount,
        -std::numeric_limits< f32 >::infinity() );

    scratch.red.assign( pixelCount, 0.0f );
    scratch.green.assign( pixelCount, 0.0f );
    scratch.blue.assign( pixelCount, 0.0f );
    f32* const red = scratch.red.data();
    f32* const green = scratch.green.data();
    f32* const blue = scratch.blue.data();

    const CubeTransform_s transform = Math_CubeTransform( camera );
    const f32 scale = static_cast< f32 >( std::min( width, height ) ) * 0.44f;
    const f32 centerX = static_cast< f32 >( width ) * 0.5f;
    const f32 centerY = static_cast< f32 >( height ) * 0.5f;
    const f32 zoomExposure = Math_CubeZoomExposure( camera.zoom );
    const f32 pointSize = std::clamp( options.pointSize, 0.5f, 2.0f );
    const f32 brightness = options.autoBrightness ? 1.0f :
        std::clamp( options.brightness, 0.25f, 3.0f );
    const int radius = std::clamp( static_cast< int >( ceil( 2.0f * pointSize ) ), 1, 4 );

    static const f32 kernel[ 5 ][ 5 ] = {
        { 0.00f, 0.010f, 0.028f, 0.010f, 0.00f },
        { 0.010f, 0.040f, 0.115f, 0.040f, 0.010f },
        { 0.028f, 0.115f, 1.000f, 0.115f, 0.028f },
        { 0.010f, 0.040f, 0.115f, 0.040f, 0.010f },
        { 0.00f, 0.010f, 0.028f, 0.010f, 0.00f }
    };

    // Point size is fixed for a frame. Compute the custom-size kernel once
    // rather than evaluating exp() for every visible voxel and pixel.
    std::array< f32, 81 > weights{};
    const int diameter = 2 * radius + 1;

    for ( int ky = -radius; ky <= radius; ++ky )
    {
        for ( int kx = -radius; kx <= radius; ++kx )
        {
            weights[ ( ky + radius ) * diameter + kx + radius ] =
                pointSize == 1.0f ? kernel[ ky + 2 ][ kx + 2 ] :
                std::exp( -2.16f * static_cast< f32 >( kx * kx + ky * ky ) /
                    ( pointSize * pointSize ) );
        }
    }

    for ( const CubePoint_s& point : cloud.points )
    {
        f32 x = 0, y = 0, z = 0;
        Cube_MapGeometry( point, options.geometry, x, y, z );

        const CubeProjection_s projected = Math_CubeProject(
            x, y, z, transform );

        const int px = static_cast< int >( std::lround( centerX + projected.x * scale ) );
        const int py = static_cast< int >( std::lround( centerY - projected.y * scale ) );
        const MathColor_s base = Math_ColorUnpack( point.argb );
        const f32 density = ( 0.16f + 0.070f * std::log2(
            static_cast< f32 >( point.count ) + 1.0f ) ) * zoomExposure;

        const f32 front = 0.80f + 0.34f * ( projected.depth + 1.732f ) / 3.464f;
        const f32 sparkle = 0.92f + 0.08f * sqrt(
            static_cast< f32 >( point.count ) );

        const bool inside = px >= radius && py >= radius &&
            px < static_cast< int >( width ) - radius &&
            py < static_cast< int >( height ) - radius;

        for ( int ky = -radius; ky <= radius; ++ky )
        {
            for ( int kx = -radius; kx <= radius; ++kx )
            {
                const f32 weight = weights[
                    ( ky + radius ) * diameter + kx + radius ];

                if ( weight <= 0.0f )
                {
                    continue;
                }

                const int sx = px + kx;
                const int sy = py + ky;

                if ( !inside && ( sx < 0 || sy < 0 ||
                     sx >= static_cast< int >( width ) ||
                     sy >= static_cast< int >( height ) ) )
                {
                    continue;
                }

                const usize pixel = static_cast< usize >( sy ) * width +
                    static_cast< usize >( sx );

                const f32 contribution = density * front * weight * sparkle * brightness;
                red[ pixel ] += base.red * contribution;
                green[ pixel ] += base.green * contribution;
                blue[ pixel ] += base.blue * contribution;
                Cube_PickPixel( image, depthBuffer, pixel,
                    projected.depth + weight * 0.0001f, point.sourceIndex );
            }
        }
    }

    const MathColor_s background = Math_ColorUnpack( colors.background3d );
    const MathColor_s whiteHot = Math_ColorUnpack( colors.full );
    const f32 exposure = options.autoBrightness
        ? Cube_AutoExposure( red, green, blue, pixelCount ) : 1.0f;

    for ( usize i = 0; i < pixelCount; ++i )
    {
        if ( red[ i ] <= 0.0f && green[ i ] <= 0.0f && blue[ i ] <= 0.0f )
        {
            continue;
        }

        const MathColor_s light = { red[ i ] * exposure, green[ i ] * exposure, blue[ i ] * exposure };
        image.argb[ i ] = Math_ColorPack(
            Math_CubeComposite( light, background, whiteHot ) );
    }

    if ( options.geometry != CCG_CUBE )
    {
        // Subdued geometry guides, rendered after the cloud like the cube
        // boundary. They are never written into the picking buffer.
        const u32 outline = Math_BlendColor(
            colors.missing, colors.full, 0.18 );

        auto edge = [&]( const f32 ax, const f32 ay, const f32 az,
                         const f32 bx, const f32 by, const f32 bz ) {
            const CubeProjection_s a = Math_CubeProject( ax, ay, az, transform );
            const CubeProjection_s b = Math_CubeProject( bx, by, bz, transform );
            Cube_DrawLine( image,
                static_cast< int >( centerX + a.x * scale ),
                static_cast< int >( centerY - a.y * scale ),
                static_cast< int >( centerX + b.x * scale ),
                static_cast< int >( centerY - b.y * scale ),
                outline, 0.26f );
        };

        constexpr int segments = 48;

        if ( options.geometry == CCG_SPHERE )
        {
            for ( int k = 0; k < segments; ++k )
            {
                const f32 a = CANTORDUST_TAU_F * k / segments;
                const f32 b = CANTORDUST_TAU_F * ( k + 1 ) / segments;
                edge( cos( a ), sin( a ), 0,
                      cos( b ), sin( b ), 0 );

                edge( cos( a ), 0, sin( a ),
                      cos( b ), 0, sin( b ) );

                edge( 0, cos( a ), sin( a ),
                      0, cos( b ), sin( b ) );
            }
        }
        else if ( options.geometry == CCG_CYLINDER )
        {
            for ( int k = 0; k < segments; ++k )
            {
                const f32 a = CANTORDUST_TAU_F * k / segments;
                const f32 b = CANTORDUST_TAU_F * ( k + 1 ) / segments;

                for ( const f32 y : { -1.0f, 1.0f } )
                    edge( cos( a ), y, sin( a ),
                          cos( b ), y, sin( b ) );

                if ( k % 12 == 0 )
                    edge( cos( a ), -1.0f, sin( a ),
                          cos( a ), 1.0f, sin( a ) );
            }
        }
        else if ( options.geometry == CCG_TETRAHEDRON )
        {
            const auto& vertices = Math_CubeTetrahedronVertices();

            for ( int a = 0; a < 4; ++a )
                for ( int b = a + 1; b < 4; ++b )
                    edge( vertices[ a ][ 0 ], vertices[ a ][ 1 ], vertices[ a ][ 2 ],
                          vertices[ b ][ 0 ], vertices[ b ][ 1 ], vertices[ b ][ 2 ] );
        }

        return;
    }

    std::array< CubeProjection_s, 8 > vertices{};

    for ( unsigned int i = 0; i < 8; ++i )
    {
        vertices[ i ] = Math_CubeProject(
            i & 1 ? 1.0f : -1.0f,
            i & 2 ? 1.0f : -1.0f,
            i & 4 ? 1.0f : -1.0f, transform );
    }

    const u32 frame = Math_BlendColor( colors.missing,
        colors.full, 0.18 );

    for ( unsigned int i = 0; i < 8; ++i )
    {
        for ( unsigned int axis = 0; axis < 3; ++axis )
        {
            const unsigned int other = i ^ ( 1u << axis );

            if ( other < i )
            {
                continue;
            }

            const CubeProjection_s& a = vertices[ i ];
            const CubeProjection_s& b = vertices[ other ];
            Cube_DrawLine( image,
                static_cast< int >( centerX + a.x * scale ),
                static_cast< int >( centerY - a.y * scale ),
                static_cast< int >( centerX + b.x * scale ),
                static_cast< int >( centerY - b.y * scale ),
                frame, 0.40f );
        }
    }
}

void Cube_RenderInto( Image_s& image,
    std::vector< f32 >& depthBuffer,
    const CubeCloud_s& cloud, const CubeCamera_s& camera,
    const usize width, const usize height,
    const Palette_e palette, const CubeRenderOptions_s options )
{
    CubeRenderScratch_s scratch;
    Cube_RenderInto( image, depthBuffer, scratch,
        cloud, camera, width, height, palette, options );
}

Image_s Cube_Render( const CubeCloud_s& cloud,
    const CubeCamera_s& camera, const usize width,
    const usize height, const Palette_e palette,
    const CubeRenderOptions_s options )
{
    Image_s image;
    std::vector< f32 > depthBuffer;
    Cube_RenderInto( image, depthBuffer, cloud, camera,
        width, height, palette, options );

    return image;
}

} // namespace cantordust
