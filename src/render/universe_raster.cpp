#include "cantordust/universe.h"
#include "cantordust/scalar_types.h"
#include "cantordust/constants.h"
#include "render/universe_detail.h"

#include "math/color.h"
#include "math/cube.h"
#include "math/universe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

namespace cantordust
{

using std::ceil;
using std::sin;
using std::cos;
using std::sqrt;

struct UniverseProjected_s
{
    int x = 0;
    int y = 0;
    f32 depth = 0;
    f32 cameraX = 0;
    f32 cameraY = 0;
};

struct UniverseRaster_s
{
    std::vector< f32 > depth;
};

static bool Universe_BlendAt( Image_s& image, UniverseRaster_s& raster,
    const int x, const int y, const f32 depth,
    const u32 color, const unsigned int alpha,
    const bool updateDepth )
{
    if ( x < 0 || y < 0 || x >= static_cast< int >( image.width ) ||
         y >= static_cast< int >( image.height ) )
    {
        return false;
    }

    const usize pixel = static_cast< usize >( y ) * image.width +
        static_cast< usize >( x );

    if ( depth < raster.depth[ pixel ] )
    {
        return false;
    }

    const u32 prior = image.argb[ pixel ];
    const unsigned int inv = 255u - alpha;
    const unsigned int red = ( ( ( color >> 16 ) & 255u ) * alpha +
        ( ( prior >> 16 ) & 255u ) * inv ) / 255u;

    const unsigned int green = ( ( ( color >> 8 ) & 255u ) * alpha +
        ( ( prior >> 8 ) & 255u ) * inv ) / 255u;

    const unsigned int blue = ( ( color & 255u ) * alpha +
        ( prior & 255u ) * inv ) / 255u;

    image.argb[ pixel ] = 0xFF000000u | red << 16 | green << 8 | blue;

    if ( updateDepth )
    {
        raster.depth[ pixel ] = depth;
    }

    return true;
}

static void Universe_DrawConnection( Image_s& image,
    UniverseRaster_s& raster, const UniverseProjected_s& from,
    const UniverseProjected_s& to, const u32 fromColor,
    const u32 toColor, const u32 count,
    const bool focused, const f32 crowding,
    const bool selectedLink, const u32 selectionColor,
    const f32 zoom, const f32 screenScale,
    const f32 selectedRoutePulse )
{
    // Clip in camera space before perspective division. A node behind the
    // camera does not invalidate the visible part of its connection.
    const f32 nearDepth = 7.0f;

    if ( !std::isfinite( from.depth ) || !std::isfinite( to.depth ) ||
         !std::isfinite( from.cameraX ) || !std::isfinite( to.cameraX ) ||
         !std::isfinite( from.cameraY ) || !std::isfinite( to.cameraY ) ||
         ( from.depth >= nearDepth && to.depth >= nearDepth ) )
    {
        return;
    }

    f32 worldFirst = 0.0f;
    f32 worldLast = 1.0f;

    if ( from.depth >= nearDepth || to.depth >= nearDepth )
    {
        const f32 crossing = ( nearDepth - from.depth ) /
            ( to.depth - from.depth );

        if ( from.depth >= nearDepth )
            worldFirst = crossing;

        else
            worldLast = crossing;
    }

    const auto interpolate = [worldFirst, worldLast]( const f32 a,
        const f32 b, const bool last ) {
        return a + ( b - a ) * ( last ? worldLast : worldFirst );
    };

    const f32 ax = interpolate( from.cameraX, to.cameraX, false );
    const f32 ay = interpolate( from.cameraY, to.cameraY, false );
    const f32 az = interpolate( from.depth, to.depth, false );
    const f32 bx = interpolate( from.cameraX, to.cameraX, true );
    const f32 by = interpolate( from.cameraY, to.cameraY, true );
    const f32 bz = interpolate( from.depth, to.depth, true );

    const f32 perspectiveA = zoom * CANTORDUST_CAMERA_PERSPECTIVE_ZOOM /
        ( CANTORDUST_CAMERA_PERSPECTIVE_DISTANCE -
            CANTORDUST_CAMERA_PERSPECTIVE_DEPTH_WEIGHT * az );
    const f32 perspectiveB = zoom * CANTORDUST_CAMERA_PERSPECTIVE_ZOOM /
        ( CANTORDUST_CAMERA_PERSPECTIVE_DISTANCE -
            CANTORDUST_CAMERA_PERSPECTIVE_DEPTH_WEIGHT * bz );
    const f32 x0 = static_cast< f32 >( image.width ) * 0.5f +
        ax * perspectiveA * screenScale;

    const f32 y0 = static_cast< f32 >( image.height ) * 0.5f -
        ay * perspectiveA * screenScale;

    const f32 x1 = static_cast< f32 >( image.width ) * 0.5f +
        bx * perspectiveB * screenScale;

    const f32 y1 = static_cast< f32 >( image.height ) * 0.5f -
        by * perspectiveB * screenScale;

    if ( !std::isfinite( x0 ) || !std::isfinite( y0 ) ||
         !std::isfinite( x1 ) || !std::isfinite( y1 ) )
    {
        return;
    }

    f32 visibleFirst = 0.0f;
    f32 visibleLast = 1.0f;

    if ( !Math_UniverseClipLine( x0, y0, x1, y1,
        static_cast< f32 >( image.width - 1 ),
        static_cast< f32 >( image.height - 1 ),
        visibleFirst, visibleLast ) )
    {
        return;
    }

    const f32 dx = x1 - x0;
    const f32 dy = y1 - y0;
    const f32 length = std::max( std::abs( dx ), std::abs( dy ) ) *
        ( visibleLast - visibleFirst );

    const int steps = std::max( 1, static_cast< int >(
        ceil( std::min( length, 4096.0f ) ) ) );

    const f32 perspectiveDenomA = CANTORDUST_CAMERA_PERSPECTIVE_DISTANCE -
        CANTORDUST_CAMERA_PERSPECTIVE_DEPTH_WEIGHT * az;
    const f32 perspectiveDenomB = CANTORDUST_CAMERA_PERSPECTIVE_DISTANCE -
        CANTORDUST_CAMERA_PERSPECTIVE_DEPTH_WEIGHT * bz;
    const f32 weight = std::log2( 1.0f + static_cast< f32 >( count ) );
    const f32 minAlpha = selectedLink ? 80.0f : ( focused ? 44.0f : 29.0f );
    const f32 maxAlpha = selectedLink ? 245.0f : ( focused ? 195.0f : 168.0f );
    const f32 alphaBase = std::clamp(
        ( selectedLink ? 157.0f : ( focused ? 100.0f : 77.0f ) ) +
            weight * ( selectedLink ? 19.0f : ( focused ? 18.0f : 18.0f ) ),
        minAlpha, maxAlpha ) * crowding;

    const bool secondary = weight >= 3.0f;
    const bool primarilyHorizontal = std::abs( dx ) > std::abs( dy );

    for ( int i = 0; i <= steps; ++i )
    {
        const f32 screenT = visibleFirst + ( visibleLast - visibleFirst ) *
            static_cast< f32 >( i ) / static_cast< f32 >( steps );

        const int x = static_cast< int >( std::lround( x0 + dx * screenT ) );
        const int y = static_cast< int >( std::lround( y0 + dy * screenT ) );
        // Perspective-correct interpolation preserves the original endpoint
        // colors and per-pixel depth after both clipping stages.
        const f32 denom = ( 1.0f - screenT ) * perspectiveDenomA +
            screenT * perspectiveDenomB;

        const f32 segmentT = screenT * perspectiveDenomB / denom;
        const f32 t = worldFirst + ( worldLast - worldFirst ) * segmentT;
        const f32 depth = from.depth +
            ( to.depth - from.depth ) * t - 0.008f;

        const f32 endpoint = std::pow( std::abs( t * 2.0f - 1.0f ), 1.25f );
        const f32 brightness = 0.62f + endpoint * 0.68f;
        // In multi-route mode only, softly breathe the selected route's
        // *line opacity* as well as its color. All other lines stay static.
        const f32 pulseOpacity = selectedRoutePulse >= 0.0f
            ? 0.78f + 0.22f * std::clamp( selectedRoutePulse, 0.0f, 1.0f )
            : 1.0f;

        const unsigned int alpha = static_cast< unsigned int >( std::clamp(
            alphaBase * brightness, minAlpha, maxAlpha ) * pulseOpacity );

        u32 color = Math_BlendColor(
            fromColor, toColor, static_cast< f64 >( t ) );

        color = Math_BlendColor( color,
            t < 0.5f ? fromColor : toColor,
            static_cast< f64 >( 0.16f + endpoint * 0.28f ) );

        if ( selectedLink )
        {
            color = Math_BlendColor( color, selectionColor,
                static_cast< f64 >( 0.52f + endpoint * 0.30f ) );
        }

        Universe_BlendAt( image, raster, x, y, depth, color, alpha, true );

        if ( secondary )
        {
            Universe_BlendAt( image, raster,
                x + ( primarilyHorizontal ? 0 : 1 ),
                y + ( primarilyHorizontal ? 1 : 0 ),
                depth - 0.002f, color,
                static_cast< unsigned int >( alpha / 4u ), false );
        }
    }
}

u32 Universe_SelectionColor( const Palette_e palette )
{
    return Palette_Get( palette ).selection;
}

static u32 Universe_NodeColor( const f32 strength,
    const Palette_s& palette )
{
    if ( palette.heatMidpoint != 0u )
    {
        return strength < 0.55f
            ? Math_BlendColor( palette.low, palette.heatMidpoint,
                std::clamp( strength / 0.55f, 0.0f, 1.0f ) )
            : Math_BlendColor( palette.heatMidpoint, palette.high,
                std::clamp( ( strength - 0.55f ) / 0.45f, 0.0f, 1.0f ) );
    }

    return Math_BlendColor( palette.low, palette.high,
        static_cast< f64 >( strength ) );
}

static u32 Universe_FocusDirectionColor(
    const UniverseGraph_s& graph, const usize index,
    const u32 paletteColor, const Palette_e palette )
{
    if ( graph.focusIndex == CANTORDUST_NO_SOURCE ||
         index == graph.focusIndex )
    {
        return paletteColor;
    }

    const u8 direction = graph.nodes[ index ].focusDirection;
    u32 color = paletteColor;

    if ( direction == 2u )
        color = Palette_Get( palette ).focusCallee;

    else if ( direction == 3u )
        color = Math_BlendColor( paletteColor,
            Palette_Get( palette ).focusCallee, 0.50 );
    // Dim the outer hop shells, including their link endpoints, without
    // touching the first-hop palette/gradient.
    const u8 depth = graph.nodes[ index ].focusDepth;

    if ( depth > 1 )
        color = Math_BlendColor( color, CANTORDUST_UNIVERSE_OUTER_HOP_DIM,
            depth == 2 ? 0.18 : std::min( 0.58,
                0.33 + 0.035 * static_cast< f64 >( depth - 3 ) ) );

    return color;
}

static u32 Universe_MaximumHeat( const UniverseGraph_s& graph )
{
    u32 highest = 0;

    for ( usize i = 0; i < graph.nodes.size(); ++i )
    {
        const UniverseNode_s& node = graph.nodes[ i ];
        // The first-hop heat scale is fixed as branches unfold; otherwise a
        // hot third-hop node would recolor all already-visible neighbors.
        if ( graph.focusIndex != CANTORDUST_NO_SOURCE &&
             node.focusDepth > 1 )
        {
            continue;
        }

        if ( graph.focusIndex != CANTORDUST_NO_SOURCE &&
             i != graph.focusIndex )
        {
            highest = std::max( highest, node.focusCalls );
        }
        else if ( graph.focusIndex == CANTORDUST_NO_SOURCE )
        {
            highest = std::max( highest, node.incoming );
        }
    }

    return highest;
}

static u32 Universe_HeatMaximumForStyle(
    const UniverseGraph_s& graph, const UniverseRenderStyle_s& style )
{
    if ( !style.relativeNodeHeat )
    {
        const u32 absolute = style.absoluteHeatMaximum != 0
            ? style.absoluteHeatMaximum : graph.absoluteHeatMaximum;

        if ( absolute != 0 )
            return absolute;
    }

    return Universe_MaximumHeat( graph );
}

u32 Universe_HeatColor( const UniverseGraph_s& graph,
    const usize index, const Palette_e palette,
    const UniverseRenderStyle_s& style )
{
    if ( index >= graph.nodes.size() )
    {
        return Palette_Get( palette ).low;
    }

    const bool center = graph.focusIndex == index;
    const UniverseNode_s& node = graph.nodes[ index ];
    const u32 count = graph.focusIndex != CANTORDUST_NO_SOURCE &&
        !center ? node.focusCalls : node.incoming;

    const f32 heat = center ? 1.0f : Math_UniverseHeat(
        count, Universe_HeatMaximumForStyle( graph, style ) );

    return Universe_FocusDirectionColor( graph, index,
        Universe_NodeColor( heat, Palette_Get( palette ) ), palette );
}

static f32 Universe_SizeWeight( const UniverseGraph_s& graph,
    const usize index )
{
    const UniverseNode_s& node = graph.nodes[ index ];

    if ( graph.focusIndex != CANTORDUST_NO_SOURCE )
    {
        return sqrt( static_cast< f32 >( node.focusCalls ) );
    }
    // Match absolute sizing's emphasis without its hard radius cap.
    return 0.53f * sqrt( static_cast< f32 >( node.incoming ) ) +
        std::min( 0.7f, 0.12f *
            sqrt( static_cast< f32 >( node.outgoing ) ) );
}

struct UniverseSizeRange_s
{
    f32 minimum = 0.0f;
    f32 maximum = 0.0f;
    usize count = 0;
};

static UniverseSizeRange_s Universe_VisibleSizeRange(
    const UniverseGraph_s& graph )
{
    UniverseSizeRange_s range;

    for ( usize i = 0; i < graph.nodes.size(); ++i )
    {
        // Compare first-hop sizes against the original sphere, even when
        // additional branches appear. Outer weights simply saturate.
        if ( i == graph.focusIndex ||
             ( graph.focusIndex != CANTORDUST_NO_SOURCE &&
               graph.nodes[ i ].focusDepth > 1 ) )
        {
            continue;
        }

        const f32 weight = Universe_SizeWeight( graph, i );

        if ( range.count == 0 )
        {
            range.minimum = weight;
            range.maximum = weight;
        }
        else
        {
            range.minimum = std::min( range.minimum, weight );
            range.maximum = std::max( range.maximum, weight );
        }
        ++range.count;
    }

    return range;
}

static f32 Universe_BaseRadius( const UniverseGraph_s& graph,
    const usize index, const UniverseRenderStyle_s& style,
    const UniverseSizeRange_s& range )
{
    const UniverseNode_s& node = graph.nodes[ index ];

    if ( index == graph.focusIndex )
    {
        return std::clamp( 4.5f +
            sqrt( static_cast< f32 >( node.incoming ) ) * 0.55f,
            4.5f, 17.0f );
    }

    if ( !style.relativeNodeSize )
    {
        return graph.focusIndex == CANTORDUST_NO_SOURCE
            ? Math_UniverseNodeRadius( node.incoming, node.outgoing )
            : Math_UniverseFocusCallerRadius( node.focusCalls );
    }

    if ( range.count == 1 )
        return 8.0f;

    if ( range.count == 0 || range.maximum <= range.minimum )
    {
        return 6.5f;
    }

    const f32 weight = Universe_SizeWeight( graph, index );
    const f32 factor = std::clamp( ( weight - range.minimum ) /
        ( range.maximum - range.minimum ), 0.0f, 1.0f );

    return 2.5f + factor * 9.5f;
}

f32 Universe_NodeRadius( const UniverseGraph_s& graph,
    const usize index, const UniverseRenderStyle_s& style )
{
    if ( index >= graph.nodes.size() )
        return 0.0f;

    const f32 clusterScale = graph.communityMode
        ? std::min( 1.75f, 1.0f + 0.16f * std::log2(
            static_cast< f32 >( std::max( 1u, graph.nodes[ index ].communitySize ) ) ) )
        : 1.0f;

    return Universe_BaseRadius( graph, index, style,
        Universe_VisibleSizeRange( graph ) ) * clusterScale;
}

std::vector< f32 > Universe_NodeRadii( const UniverseGraph_s& graph,
    const UniverseRenderStyle_s& style )
{
    const UniverseSizeRange_s range = Universe_VisibleSizeRange( graph );
    std::vector< f32 > radii;
    radii.reserve( graph.nodes.size() );

    for ( usize i = 0; i < graph.nodes.size(); ++i )
    {
        const f32 clusterScale = graph.communityMode
            ? std::min( 1.75f, 1.0f + 0.16f * std::log2(
                static_cast< f32 >(
                    std::max( 1u, graph.nodes[ i ].communitySize ) ) ) )
            : 1.0f;

        radii.push_back( Universe_BaseRadius( graph, i, style, range ) *
            clusterScale );
    }

    return radii;
}

// Match the perspective used by Math_CubeProject. A far node has a
// smaller screen-space radius; bound the near-plane magnification so neither
// a sphere nor a tetrahedron can become a screen-covering primitive.
static f32 Universe_DepthSize( const f32 depth )
{
    if ( !std::isfinite( depth ) )
        return 0.0f;

    const f32 denominator = CANTORDUST_CAMERA_PERSPECTIVE_DISTANCE -
        CANTORDUST_CAMERA_PERSPECTIVE_DEPTH_WEIGHT * depth;

    if ( denominator <= 0.0f )
        return 0.0f;

    return std::clamp(
        CANTORDUST_CAMERA_PERSPECTIVE_DISTANCE / denominator, 0.38f, 1.85f );
}

static f32 Universe_DrawRadius( const UniverseGraph_s& graph,
    const usize index, const CubeCamera_s& camera,
    const f32 depth, const UniverseRenderStyle_s& style,
    const UniverseSizeRange_s& range )
{
    const f32 clusterScale = graph.communityMode
        ? std::min( 1.75f, 1.0f + 0.16f * std::log2(
            static_cast< f32 >( std::max( 1u, graph.nodes[ index ].communitySize ) ) ) )
        : 1.0f;

    return Universe_BaseRadius( graph, index, style, range ) * clusterScale *
        std::clamp( sqrt( camera.zoom ), 0.70f, 1.65f ) *
        Universe_DepthSize( depth );
}

struct UniverseFraming_s
{
    UniverseView_s center;
    f32 extent = 0.58f;
};

static UniverseFraming_s Universe_ProminentFraming(
    const UniverseGraph_s& graph )
{
    UniverseFraming_s framing;

    if ( graph.nodes.empty() )
        return framing;

    // Frame the actual hubs rather than distant, low-traffic outliers.
    // A small fixed cap keeps mouse dragging inexpensive even in large tiers.
    const usize candidateCount = std::min< usize >(
        graph.nodes.size(), std::clamp< usize >(
            graph.nodes.size() / 32u, 3u, 24u ) );

    std::vector< usize > ranked( graph.nodes.size() );
    std::iota( ranked.begin(), ranked.end(), 0u );

    const auto moreProminent = [&graph]( const usize a,
        const usize b ) {
        const f32 left = Universe_NodeProminence( graph.nodes[ a ] );
        const f32 right = Universe_NodeProminence( graph.nodes[ b ] );
        return left != right ? left > right :
            graph.nodes[ a ].address < graph.nodes[ b ].address;
    };
    std::partial_sort( ranked.begin(), ranked.begin() +
        static_cast< std::ptrdiff_t >( candidateCount ),
        ranked.end(), moreProminent );

    const bool hasReferences = Universe_NodeProminence(
        graph.nodes[ ranked.front() ] ) > 0.0f;

    if ( !hasReferences )
    {
        // No hubs to follow: use the traditional full-cloud framing.
        f32 extent = 0.0f;

        for ( const UniverseNode_s& node : graph.nodes )
        {
            extent = std::max( { extent, std::abs( node.x ),
                std::abs( node.y ), std::abs( node.z ) } );
        }

        framing.extent = std::max( extent, 0.58f );
        return framing;
    }

    f32 weightSum = 0.0f;

    for ( usize i = 0; i < candidateCount; ++i )
    {
        const UniverseNode_s& node = graph.nodes[ ranked[ i ] ];
        const f32 prominence = Universe_NodeProminence( node );

        if ( prominence <= 0.0f )
            break;

        const f32 weight = sqrt( prominence );
        weightSum += weight;
        framing.center.panX += node.x * weight;
        framing.center.panY += node.y * weight;
        framing.center.panZ += node.z * weight;
    }

    framing.center.panX /= weightSum;
    framing.center.panY /= weightSum;
    framing.center.panZ /= weightSum;

    f32 extent = 0.0f;

    for ( usize i = 0; i < candidateCount; ++i )
    {
        const UniverseNode_s& node = graph.nodes[ ranked[ i ] ];

        if ( Universe_NodeProminence( node ) <= 0.0f )
            break;

        extent = std::max( { extent,
            std::abs( node.x - framing.center.panX ),
            std::abs( node.y - framing.center.panY ),
            std::abs( node.z - framing.center.panZ ) } );
    }

    framing.extent = std::max( extent * 1.18f, 0.58f );
    return framing;
}

UniverseView_s Universe_DefaultView( const UniverseGraph_s& graph )
{
    if ( !graph.adaptiveLayout ||
         graph.focusIndex != CANTORDUST_NO_SOURCE )
    {
        return {};
    }

    return Universe_ProminentFraming( graph ).center;
}

f32 Universe_FrameScale( const UniverseGraph_s& graph,
    const f32 spacing )
{
    if ( graph.pathMode )
    {
        f32 extent = 0.8f;

        if ( graph.multiPathMode )
        {
            for ( const UniverseNode_s& node : graph.nodes )
                extent = std::max( extent, std::abs( node.x ) );

            return 1.35f / extent;
        }

        extent = 0.45f * static_cast< f32 >(
            std::max< usize >( graph.nodes.size(), 2u ) );

        return 2.8f / std::max( extent, 0.8f );
    }

    if ( !graph.adaptiveLayout || graph.nodes.empty() ||
         graph.focusIndex != CANTORDUST_NO_SOURCE )
    {
        return 1.0f;
    }

    const f32 extent = Universe_ProminentFraming( graph ).extent;
    const f32 fitted = 0.70f / extent;
    // Linear expansion across the whole 25%-10000% control range. The old
    // atan(log(spacing)) curve flattened almost completely after 200%.
    const f32 requested = std::clamp( spacing, 0.25f, 100.0f );
    const f32 expansion = 1.20f + ( requested - 1.0f ) * 0.30f;
    return fitted * expansion;
}

static int Universe_ScreenCoordinate( const f32 projected )
{
    // Large spacing values intentionally put outliers off-screen. Saturate
    // before integer conversion to avoid overflow at perspective poles.
    if ( !std::isfinite( projected ) )
    {
        return std::signbit( projected ) ? -8192 : 8192;
    }

    return static_cast< int >( std::lround( std::clamp(
        projected, -8192.0f, 8192.0f ) ) );
}

static void Universe_DrawGlobal( Image_s& image, UniverseRaster_s& raster,
    const UniverseNode_s& node, const usize index,
    const UniverseProjected_s& center, const f32 radius,
    const u32 color, const CubeTransform_s& transform )
{
    // Keep the camera's perspective pole behind the near clipping plane.
    // Spheres already render at a fixed pixel radius; tetrahedra must also
    // have a bounded footprint instead of stretching across the viewport.
    if ( !std::isfinite( center.depth ) || center.depth >= 7.0f )
    {
        return;
    }

    const f32 bound = radius * 1.5f + 2.0f;

    if ( static_cast< f32 >( center.x ) < -bound ||
         static_cast< f32 >( center.y ) < -bound ||
         static_cast< f32 >( center.x ) >=
            static_cast< f32 >( image.width ) + bound ||
         static_cast< f32 >( center.y ) >=
            static_cast< f32 >( image.height ) + bound )
    {
        return;
    }

    // A tetrahedron has four 3D vertices and four triangular faces.
    static const UniversePosition_s vertices[ 4 ] = {
        { 0.0f, 1.2f, 0.0f },
        { -0.95f, -0.70f, -0.65f },
        { 0.95f, -0.70f, -0.65f },
        { 0.0f, -0.70f, 1.05f }
    };
    static const int faces[ 4 ][ 3 ] = {
        { 0, 1, 2 }, { 0, 2, 3 }, { 0, 3, 1 }, { 1, 3, 2 }
    };
    // Rotate the local shape in 3D, then draw it around the projected node
    // with a screen-space radius. At extreme universe spacing the old per-
    // vertex perspective could cross the pole and explode a face to infinity.
    const f32 localScale = radius * 0.94f;
    // Per-session random orientation, derived per global. Keep the seed fixed
    // during the session so camera movement and picking remain stable.
    static const u64 s_rotationSeed = []() {
        std::random_device device;
        return ( static_cast< u64 >( device() ) << 32 ) |
            static_cast< u64 >( device() );
    }();

    const UniversePosition_s spin = Math_UniverseDirection(
        node.address ^ s_rotationSeed );

    const f32 ax = spin.x * CANTORDUST_PI_F;
    const f32 ay = spin.y * CANTORDUST_PI_F;
    const f32 az = spin.z * CANTORDUST_PI_F;
    const f32 cx = cos( ax );
    const f32 sx = sin( ax );
    const f32 cy = cos( ay );
    const f32 sy = sin( ay );
    const f32 cz = cos( az );
    const f32 sz = sin( az );
    std::array< UniverseProjected_s, 4 > projected{};

    for ( usize i = 0; i < projected.size(); ++i )
    {
        const UniversePosition_s& vertex = vertices[ i ];
        const f32 x1 = vertex.x;
        const f32 y1 = vertex.y * cx - vertex.z * sx;
        const f32 z1 = vertex.y * sx + vertex.z * cx;
        const f32 x2 = x1 * cy + z1 * sy;
        const f32 z2 = -x1 * sy + z1 * cy;
        const f32 x3 = x2 * cz - y1 * sz;
        const f32 y3 = x2 * sz + y1 * cz;
        const f32 rotatedX = x3 * transform.cy - z2 * transform.sy;
        const f32 rotatedZ = x3 * transform.sy + z2 * transform.cy;
        const f32 rotatedY = y3 * transform.cp - rotatedZ * transform.sp;
        const f32 localDepth = y3 * transform.sp +
            rotatedZ * transform.cp;

        projected[ i ] = {
            Universe_ScreenCoordinate( static_cast< f32 >( center.x ) +
                rotatedX * localScale ),
            Universe_ScreenCoordinate( static_cast< f32 >( center.y ) -
                rotatedY * localScale ),
            center.depth + localDepth * 0.015f
        };
    }

    for ( int face = 0; face < 4; ++face )
    {
        const UniverseProjected_s& a = projected[ faces[ face ][ 0 ] ];
        const UniverseProjected_s& b = projected[ faces[ face ][ 1 ] ];
        const UniverseProjected_s& c = projected[ faces[ face ][ 2 ] ];
        const f32 denominator = static_cast< f32 >(
            ( b.y - c.y ) * ( a.x - c.x ) +
            ( c.x - b.x ) * ( a.y - c.y ) );

        if ( std::abs( denominator ) < 0.1f )
        {
            continue;
        }

        const int x0 = std::max( 0, std::min( { a.x, b.x, c.x } ) );
        const int x1 = std::min( static_cast< int >( image.width ) - 1,
            std::max( { a.x, b.x, c.x } ) );

        const int y0 = std::max( 0, std::min( { a.y, b.y, c.y } ) );
        const int y1 = std::min( static_cast< int >( image.height ) - 1,
            std::max( { a.y, b.y, c.y } ) );

        const u32 faceColor = Math_BlendColor(
            color, 0xFFFFFFFFu, static_cast< f64 >( face + 1 ) * 0.075 );

        for ( int y = y0; y <= y1; ++y )
        {
            for ( int x = x0; x <= x1; ++x )
            {
                const f32 w0 = static_cast< f32 >(
                    ( b.y - c.y ) * ( x - c.x ) +
                    ( c.x - b.x ) * ( y - c.y ) ) / denominator;

                const f32 w1 = static_cast< f32 >(
                    ( c.y - a.y ) * ( x - c.x ) +
                    ( a.x - c.x ) * ( y - c.y ) ) / denominator;

                const f32 w2 = 1.0f - w0 - w1;

                if ( w0 < 0.0f || w1 < 0.0f || w2 < 0.0f )
                {
                    continue;
                }

                const f32 depth = w0 * a.depth + w1 * b.depth +
                    w2 * c.depth;

                if ( Universe_BlendAt( image, raster,
                     x, y, depth, faceColor, 250u, true ) )
                {
                    const usize pixel =
                        static_cast< usize >( y ) * image.width +
                        static_cast< usize >( x );

                    image.sourceIndex[ pixel ] = index;
                }
            }
        }
    }
}

static void Universe_DrawFunction( Image_s& image,
    UniverseRaster_s& raster, const UniverseGraph_s& graph,
    const usize index, const UniverseProjected_s& point,
    const usize selectedIndex, const CubeCamera_s& camera,
    const UniverseRenderStyle_s& style, const UniverseSizeRange_s& sizeRange,
    const Palette_s& palette, const u32 color,
    const u32 selectionColor, const CubeTransform_s& transform )
{
    const bool hub = graph.focusIndex == index;
    const bool highlighted = index == selectedIndex;
    const u32 displayColor = highlighted ? selectionColor : color;
    // The same conservative near plane protects both node shapes.
    if ( !std::isfinite( point.depth ) || point.depth >= 7.0f )
        return;

    const f32 radius = Universe_DrawRadius(
        graph, index, camera, point.depth, style, sizeRange );

    if ( graph.nodes[ index ].kind == UNK_GLOBAL )
    {
        Universe_DrawGlobal( image, raster, graph.nodes[ index ], index,
            point, radius, displayColor, transform );

        return;
    }

    const f32 radius2 = radius * radius;
    const f32 outerRadius = radius + ( highlighted ? 3.2f :
        ( hub ? 1.8f : 0.75f ) );

    const int bound = static_cast< int >( ceil( outerRadius ) );
    // Most nodes can be offscreen at 10000% spacing. Cull whole spheres
    // before visiting pixels; preserve all projected positions and links.
    if ( point.x + bound < 0 || point.y + bound < 0 ||
         point.x - bound >= static_cast< int >( image.width ) ||
         point.y - bound >= static_cast< int >( image.height ) )
    {
        return;
    }

    for ( int y = -bound; y <= bound; ++y )
    {
        for ( int x = -bound; x <= bound; ++x )
        {
            const f32 distance2 = static_cast< f32 >( x * x + y * y );

            if ( distance2 > outerRadius * outerRadius )
            {
                continue;
            }

            if ( distance2 > radius2 )
            {
                // Keep the halo deliberately faint and narrow.
                const bool outline = highlighted &&
                    distance2 <= ( radius + 1.9f ) * ( radius + 1.9f );

                Universe_BlendAt( image, raster, point.x + x, point.y + y,
                    point.depth - 0.002f,
                    outline ? selectionColor : displayColor,
                    outline ? 220u : ( highlighted ? 45u : ( hub ? 22u : 6u ) ),
                    false );

                continue;
            }

            const f32 surface = sqrt( std::max( 0.0f,
                1.0f - distance2 / std::max( radius2, 0.01f ) ) );

            const f32 depth = point.depth + 0.018f * surface;
            const unsigned int alpha = static_cast< unsigned int >(
                175.0f + surface * 80.0f );

            const bool visible = Universe_BlendAt( image, raster,
                point.x + x, point.y + y, depth, displayColor, alpha, true );

            if ( !visible )
            {
                continue;
            }

            const int px = point.x + x;
            const int py = point.y + y;
            const usize pixel = static_cast< usize >( py ) *
                image.width + static_cast< usize >( px );

            image.sourceIndex[ pixel ] = index;

            if ( surface > 0.76f && ( hub || index == selectedIndex ) )
            {
                Universe_BlendAt( image, raster, px, py,
                    depth + 0.0005f,
                    highlighted ? selectionColor : palette.full,
                    highlighted ? 205u : 75u, true );
            }
        }
    }
}

// A second, non-interactive selection channel for IDA's active function.
// Draw a thin, broken halo *outside* the node's silhouette. It intentionally
// changes neither depth/sourceIndex nor the node/link palette colors, so the
// user's own selection and every callsite hit-test keep working.
static void Universe_DrawIdaSyncHalo( Image_s& image,
    const UniverseGraph_s& graph, const usize index,
    const UniverseProjected_s& point, const CubeCamera_s& camera,
    const UniverseRenderStyle_s& style, const UniverseSizeRange_s& sizeRange,
    const u32 color, const u8 alpha )
{
    if ( index >= graph.nodes.size() || !std::isfinite( point.depth ) ||
         point.depth >= 7.0f )
    {
        return;
    }

    const f32 nodeRadius = Universe_DrawRadius(
        graph, index, camera, point.depth, style, sizeRange );

    const f32 haloRadius = nodeRadius *
        ( graph.nodes[ index ].kind == UNK_GLOBAL ? 1.55f : 1.0f ) + 4.0f;

    const int bound = static_cast< int >( ceil( haloRadius + 2.0f ) );

    if ( point.x + bound < 0 || point.y + bound < 0 ||
         point.x - bound >= static_cast< int >( image.width ) ||
         point.y - bound >= static_cast< int >( image.height ) )
    {
        return;
    }

    for ( int y = -bound; y <= bound; ++y )
    {
        for ( int x = -bound; x <= bound; ++x )
        {
            const f32 radius = sqrt( static_cast< f32 >(
                x * x + y * y ) );

            if ( std::fabs( radius - haloRadius ) > 1.25f )
                continue;
            // An eight-dash outline is distinct from a clicked node's
            // continuous selection glow and readable at low zoom.
            const f32 angle = std::atan2( static_cast< f32 >( y ),
                static_cast< f32 >( x ) ) + CANTORDUST_PI_F;

            const int sector = static_cast< int >( angle * 16.0f /
                CANTORDUST_TAU_F );

            if ( sector % 2 != 0 )
                continue;

            const int px = point.x + x;
            const int py = point.y + y;

            if ( px < 0 || py < 0 || px >= static_cast< int >( image.width ) ||
                 py >= static_cast< int >( image.height ) )
            {
                continue;
            }

            const usize pixel = static_cast< usize >( py ) *
                image.width + static_cast< usize >( px );

            const u32 old = image.argb[ pixel ];
            const unsigned int alphaValue = alpha;
            const unsigned int inv = 255u - alphaValue;
            const auto channel = [old, color, inv, alphaValue]( const unsigned int shift ) {
                return ( ( ( color >> shift ) & 255u ) * alphaValue +
                    ( ( old >> shift ) & 255u ) * inv ) / 255u;
            };
            image.argb[ pixel ] = 0xFF000000u |
                ( channel( 16u ) << 16 ) | ( channel( 8u ) << 8 ) |
                channel( 0u );
        }
    }
}

// Optional contrast-guided edge smoothing. Applied only to ARGB, never to
// sourceIndex: pixel-perfect IDA address picking is unchanged by the toggle.
static void Universe_AntiAlias( Image_s& image )
{
    if ( image.width < 3 || image.height < 3 )
        return;

    const std::vector< u32 > original = image.argb;
    const usize stride = image.width;

    for ( usize y = 1; y + 1 < image.height; ++y )
    {
        for ( usize x = 1; x + 1 < image.width; ++x )
        {
            const usize pixel = y * stride + x;
            const u32 c = original[ pixel ];
            const u32 left = original[ pixel - 1 ];
            const u32 right = original[ pixel + 1 ];
            const u32 up = original[ pixel - stride ];
            const u32 down = original[ pixel + stride ];
            const auto brightness = []( const u32 rgb ) {
                return ( ( rgb >> 16 ) & 255u ) * 54u +
                    ( ( rgb >> 8 ) & 255u ) * 183u +
                    ( rgb & 255u ) * 19u;
            };

            const unsigned int center = brightness( c );
            const unsigned int neighbors[ 4 ] = {
                brightness( left ), brightness( right ),
                brightness( up ), brightness( down ) };

            unsigned int low = center;
            unsigned int high = center;

            for ( const unsigned int value : neighbors )
            {
                low = std::min( low, value );
                high = std::max( high, value );
            }

            if ( high - low < 32u * 256u )
                continue;

            const auto channel = [c, left, right, up, down](
                const unsigned int shift ) {
                // Half of the color remains at the current pixel, with
                // the other half blended from its immediate neighbors.
                return ( 4u * ( ( c >> shift ) & 255u ) +
                    ( ( left >> shift ) & 255u ) +
                    ( ( right >> shift ) & 255u ) +
                    ( ( up >> shift ) & 255u ) +
                    ( ( down >> shift ) & 255u ) + 4u ) / 8u;
            };
            image.argb[ pixel ] = 0xFF000000u |
                ( channel( 16u ) << 16 ) |
                ( channel( 8u ) << 8 ) | channel( 0u );
        }
    }
}

void Universe_RenderInto( Image_s& image, const UniverseGraph_s& graph,
    const CubeCamera_s& camera, const usize selectedIndex,
    const bool allLinks, const usize width,
    const usize height, const Palette_e palette,
    const UniverseView_s& view, const UniverseRenderStyle_s& style,
    const usize idaSyncIndex, const u8 idaSyncAlpha,
    const f32 selectedPathPulse )
{
    image.width = width;
    image.height = height;

    if ( width == 0 || height == 0 || width > 2048 || height > 2048 )
    {
        image.argb.clear();
        image.sourceIndex.clear();
        return;
    }

    const Palette_s& colors = Palette_Get( palette );
    const usize count = width * height;
    image.argb.assign( count, colors.background3d );
    image.sourceIndex.assign( count, CANTORDUST_NO_SOURCE );
    UniverseRaster_s raster;
    raster.depth.assign( count, -std::numeric_limits< f32 >::infinity() );

    const CubeTransform_s transform = Math_CubeTransform( camera );
    const f32 frameScale = Universe_FrameScale(
        graph, style.universeSpacing ) * style.focusScale;

    const f32 panX = std::clamp( view.panX, -32.0f, 32.0f );
    const f32 panY = std::clamp( view.panY, -32.0f, 32.0f );
    const f32 panZ = std::clamp( view.panZ, -32.0f, 32.0f );
    const f32 scale = static_cast< f32 >( std::min( width, height ) ) * 0.94f;
    std::vector< UniverseProjected_s > screen;
    screen.reserve( graph.nodes.size() );

    for ( const UniverseNode_s& node : graph.nodes )
    {
        const CubeProjection_s p = Math_CubeProject(
            ( node.x - panX ) * frameScale,
            ( node.y - panY ) * frameScale,
            ( node.z - panZ ) * frameScale, transform );

        const f32 x = ( node.x - panX ) * frameScale;
        const f32 y = ( node.y - panY ) * frameScale;
        const f32 z = ( node.z - panZ ) * frameScale;
        const f32 rotatedZ = x * transform.sy + z * transform.cy;
        screen.push_back({
            Universe_ScreenCoordinate( static_cast< f32 >( width ) *
                0.5f + p.x * scale ),
            Universe_ScreenCoordinate( static_cast< f32 >( height ) *
                0.5f - p.y * scale ), p.depth,
            x * transform.cy - z * transform.sy,
            y * transform.cp - rotatedZ * transform.sp });
    }

    const bool selected = selectedIndex < graph.nodes.size();
    const u32 maxHeat = Universe_HeatMaximumForStyle( graph, style );
    const UniverseSizeRange_s sizeRange = Universe_VisibleSizeRange( graph );
    const u32 accent = Universe_SelectionColor( palette );
    // Five distinct high-contrast route accents. On shared edges/nodes the
    // selected route takes precedence; others remain visible but subdued.

    const u8 activeRouteBit = static_cast< u8 >(
        1u << graph.activePathRoute );

    const auto pathColor = [&]( const u8 mask ) {
        usize route = graph.activePathRoute;

        if ( !( mask & activeRouteBit ) )
        {
            for ( route = 0; route < 5u; ++route )
                if ( mask & ( 1u << route ) )
                    break;

            if ( route == 5u )
                route = 0;
        }

        const u32 base = CANTORDUST_UNIVERSE_ROUTE_COLORS[ route ];

        if ( mask & activeRouteBit )
        {
            // A small white tint barely shows on saturated cyan/yellow. Fade
            // between a noticeably darker version of the SAME hue and a
            // slightly brighter peak instead. The route is always visible,
            // and unselected routes never participate in this animation.
            const f32 pulse = std::isfinite( selectedPathPulse )
                ? std::clamp( selectedPathPulse, 0.0f, 1.0f ) : 0.0f;

            if ( pulse < 0.5f )
                return Math_BlendColor( base, colors.background3d,
                    static_cast< f64 >( ( 1.0f - 2.0f * pulse ) * 0.32f ) );

            return Math_BlendColor( base, 0xFFFFFFFFu,
                static_cast< f64 >( ( 2.0f * pulse - 1.0f ) * 0.10f ) );
        }

        return Math_BlendColor( base, colors.background3d, 0.43 );
    };
    std::vector< u32 > nodeColors;
    nodeColors.reserve( graph.nodes.size() );

    for ( usize i = 0; i < graph.nodes.size(); ++i )
    {
        const bool center = i == graph.focusIndex;
        const u32 references = graph.focusIndex != CANTORDUST_NO_SOURCE &&
            !center ? graph.nodes[ i ].focusCalls : graph.nodes[ i ].incoming;

        u32 nodeColor = Universe_FocusDirectionColor( graph, i,
            Universe_NodeColor(
                center ? 1.0f : Math_UniverseHeat( references, maxHeat ),
                colors ), palette );

        if ( graph.communityMode && graph.nodes[ i ].kind == UNK_COMMUNITY )
        {
            // Distinct palette bands distinguish neighboring communities
            // without replacing the palette with arbitrary RGB colors.
            const u32 bands[] = {
                colors.high, colors.printable, colors.full, colors.low };

            const usize band = static_cast< usize >(
                graph.nodes[ i ].address % 4u );

            nodeColor = Math_BlendColor( nodeColor, bands[ band ], 0.48 );
        }

        if ( graph.multiPathMode )
            nodeColor = pathColor( graph.nodes[ i ].pathMask );

        else if ( graph.pathMode )
        {
            nodeColor = i == 0 ? CANTORDUST_UNIVERSE_PATH_START_COLOR :
                i + 1 == graph.nodes.size() ? CANTORDUST_UNIVERSE_PATH_END_COLOR
                    : CANTORDUST_UNIVERSE_PATH_MIDDLE_COLOR;
        }

        nodeColors.push_back( nodeColor );
    }

    std::vector< usize > links;
    links.reserve( graph.links.size() );

    for ( usize i = 0; i < graph.links.size(); ++i )
    {
        const UniverseLink_s& link = graph.links[ i ];

        if ( link.caller >= screen.size() || link.callee >= screen.size() )
        {
            continue;
        }

        if ( allLinks || graph.focusIndex != CANTORDUST_NO_SOURCE ||
             ( selected && ( link.caller == selectedIndex ||
                             link.callee == selectedIndex ) ) )
        {
            links.push_back( i );
        }
    }

    std::sort( links.begin(), links.end(), [&graph, &screen](
        const usize a, const usize b ) {
        const UniverseLink_s& left = graph.links[ a ];
        const UniverseLink_s& right = graph.links[ b ];
        return screen[ left.caller ].depth + screen[ left.callee ].depth <
            screen[ right.caller ].depth + screen[ right.callee ].depth;
    } );

    const f32 crowding = graph.focusIndex == CANTORDUST_NO_SOURCE &&
        allLinks && links.size() > 2500u
        ? std::clamp( sqrt( 2500.0f /
            static_cast< f32 >( links.size() ) ), 0.45f, 1.0f )
        : 1.0f;

    for ( const usize i : links )
    {
        const UniverseLink_s& link = graph.links[ i ];
        const bool prominent = graph.focusIndex != CANTORDUST_NO_SOURCE ||
            ( selected && ( link.caller == selectedIndex ||
                            link.callee == selectedIndex ) );

        const bool selectedLink = selected &&
            ( link.caller == selectedIndex || link.callee == selectedIndex );
        // Always fade toward each node's *actual* rendered color. v49's
        // direction-based fixed endpoints broke the v48 near-node glow,
        // particularly when the palette changed or links were reciprocal.
        const bool activeRoute = !graph.multiPathMode ||
            ( link.pathMask & activeRouteBit ) != 0;

        const u32 connectionColor = graph.multiPathMode
            ? pathColor( link.pathMask ) : 0u;
        // Keep route identity while grading toward the actual node hue at
        // each endpoint, including intersections shared by several paths.
        const u32 fromColor = graph.multiPathMode
            ? Math_BlendColor( connectionColor,
                nodeColors[ link.caller ], 0.65 ) : nodeColors[ link.caller ];

        const u32 toColor = graph.multiPathMode
            ? Math_BlendColor( connectionColor,
                nodeColors[ link.callee ], 0.65 ) : nodeColors[ link.callee ];

        Universe_DrawConnection( image, raster,
            screen[ link.caller ], screen[ link.callee ],
            fromColor, toColor,
            graph.pathMode ? ( activeRoute ? 12u : 3u ) :
                ( graph.distinctCountMode ? 1u : link.count ),
            prominent || graph.pathMode, crowding, selectedLink, accent,
            transform.zoom, scale,
            graph.multiPathMode && activeRoute &&
                std::isfinite( selectedPathPulse )
                ? selectedPathPulse : -1.0f );
    }

    std::vector< usize > order( screen.size() );
    std::iota( order.begin(), order.end(), 0 );
    std::sort( order.begin(), order.end(), [&screen](
        const usize a, const usize b ) {
        return screen[ a ].depth < screen[ b ].depth;
    } );

    for ( const usize index : order )
    {
        Universe_DrawFunction( image, raster, graph, index,
            screen[ index ], selectedIndex, camera, style, sizeRange, colors,
            nodeColors[ index ], accent, transform );
    }

    if ( style.antiAliasing )
        Universe_AntiAlias( image );

    if ( idaSyncIndex < screen.size() && idaSyncAlpha > 0u )
    {
        Universe_DrawIdaSyncHalo( image, graph, idaSyncIndex,
            screen[ idaSyncIndex ], camera, style, sizeRange,
            // Use the palette's high-contrast selection color, not a fixed
            // yellow that would clash with certain callee accents.
            accent, idaSyncAlpha );
    }
}

} // namespace cantordust
