#include "cantordust/cube.h"
#include "cantordust/scalar_types.h"
#include "cantordust/constants.h"

#include "math/color.h"
#include "math/cube_lighting.h"
#include "math/cube.h"
#include "tier0/compiler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#define CANTORDUST_CUBE_VOXELS ( CANTORDUST_CUBE_GRID * \
    CANTORDUST_CUBE_GRID * CANTORDUST_CUBE_GRID )

namespace cantordust
{

using std::ceil;
using std::sin;
using std::cos;
using std::sqrt;

usize Cube_SampleLength( const CubeValueMode_e mode )
{
    return mode == CCV_U16_LE || mode == CCV_U16_BE ? 6u :
           mode == CCV_U32_LE || mode == CCV_U32_BE ? 12u : 4u;
}

bool Cube_DecodeTriplet( const u8* const bytes,
    const CubeValueMode_e mode, CubeTriplet_s& result )
{
    if ( bytes == nullptr || mode < CCV_U8 || mode > CCV_POPCOUNT8 )
        return false;

    const unsigned int width = mode == CCV_U16_LE || mode == CCV_U16_BE ? 2u :
        mode == CCV_U32_LE || mode == CCV_U32_BE ? 4u : 1u;

    const bool big = mode == CCV_U16_BE || mode == CCV_U32_BE;
    auto decode = [&]( const unsigned int offset ) -> u8 {
        if ( mode == CCV_DELTA8 )
            return static_cast< u8 >( bytes[ offset ] - bytes[ offset - 1u ] );

        if ( mode == CCV_POPCOUNT8 )
        {
            unsigned int value = bytes[ offset ];
            unsigned int count = 0;

            while ( value != 0 )
    {
        count += value & 1u;
        value >>= 1u;
    }
            return static_cast< u8 >( count * 255u / 8u );
        }
        // High byte represents the most significant 8 bits of u16/u32.
        return bytes[ offset + ( big ? 0u : width - 1u ) ];
    };

    const unsigned int first = width == 1u ? 1u : 0u;
    result.x = decode( first );
    result.y = decode( first + width );
    result.z = decode( first + 2u * width );
    result.colorByte = bytes[ 0 ];
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
    const CubeValueMode_e mode )
{
    CubeCloud_s cloud;

    const usize sampleLength = Cube_SampleLength( mode );

    if ( cells.size() < sampleLength || maximumPoints == 0 )
    {
        return cloud;
    }

    std::vector< CubeAccumulator_s > bins( CANTORDUST_CUBE_VOXELS );
    usize boundary = 0;

    const usize stride = sampleLength == 12 ? 4u : sampleLength == 6 ? 2u : 1u;

    for ( usize i = 0; i + sampleLength <= cells.size(); i += stride )
    {
        while ( boundary < boundaryOffsets.size() &&
                boundaryOffsets[ boundary ] <= i )
        {
            ++boundary;
        }

        if ( boundary < boundaryOffsets.size() &&
             boundaryOffsets[ boundary ] < i + sampleLength )
        {
            continue;
        }

        bool valid = true;
        u8 bytes[ 13 ]{};

        for ( usize j = 0; j < sampleLength; ++j )
        {
            if ( V_UNLIKELY( !cells[ i + j ].valid ) )
            {
                valid = false;
                break;
            }

            bytes[ j ] = cells[ i + j ].value;
        }

        if ( !valid )
            continue;

        CubeTriplet_s decoded;

        if ( !Cube_DecodeTriplet( bytes, mode, decoded ) )
            continue;

        Cube_Accumulate( bins, cloud, decoded.x, decoded.y, decoded.z,
            decoded.colorByte, i );
    }

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
        f32 a = ( x + 1.0f ) * 0.5f;
        f32 b = ( y + 1.0f ) * 0.5f;
        f32 c = ( z + 1.0f ) * 0.5f;

        const f32 total = std::max( 1.0f, a + b + c );
        a /= total;
        b /= total;
        c /= total;
        x = -1.0f + 2.0f * a + b + c;
        y = -1.0f + 2.0f * b;
        z = -1.0f + 2.0f * c;
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
    const f32 brightness = std::clamp( options.brightness, 0.25f, 3.0f );
    const int radius = std::clamp( static_cast< int >( ceil( 2.0f * pointSize ) ), 1, 4 );

    static const f32 kernel[ 5 ][ 5 ] = {
        { 0.00f, 0.010f, 0.028f, 0.010f, 0.00f },
        { 0.010f, 0.040f, 0.115f, 0.040f, 0.010f },
        { 0.028f, 0.115f, 1.000f, 0.115f, 0.028f },
        { 0.010f, 0.040f, 0.115f, 0.040f, 0.010f },
        { 0.00f, 0.010f, 0.028f, 0.010f, 0.00f }
    };

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
                const f32 weight = pointSize == 1.0f
                    ? kernel[ ky + 2 ][ kx + 2 ]
                    : std::exp( -2.16f * static_cast< f32 >( kx * kx + ky * ky ) /
                        ( pointSize * pointSize ) );

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

    for ( usize i = 0; i < pixelCount; ++i )
    {
        if ( red[ i ] <= 0.0f && green[ i ] <= 0.0f && blue[ i ] <= 0.0f )
        {
            continue;
        }

        const MathColor_s light = { red[ i ], green[ i ], blue[ i ] };
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
            const f32 vertices[ 4 ][ 3 ] = {
                { -1, -1, -1 }, { 1, -1, -1 }, { 0, 1, -1 }, { 0, -1, 1 }
            };

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
