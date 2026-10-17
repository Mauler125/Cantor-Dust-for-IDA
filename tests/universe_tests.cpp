#include "cantordust/universe.h"
#include "math/universe.h"
#include "math/cube.h"
#include "tier1/universe_history.h"

#ifdef NDEBUG
    #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <vector>

using namespace cantordust;

static void Tests_BuildAndCounts()
{
    const std::vector< UniverseFunction_s > funcs = {
        { 0x1000, "root" }, { 0x2000, "leaf" },
        { 0x3000, "other" }, { 0x4000, "isolated" }
    };

    const std::vector< UniverseReference_s > refs = {
        { 0x1000, 0x2000 }, { 0x1000, 0x2000 },
        { 0x3000, 0x2000 }, { 0x1000, 0x3000 },
        { 0x1234, 0x2000 }
    };

    const UniverseGraph_s graph = Universe_BuildGraph( funcs, refs );
    assert( graph.nodes.size() == 4 );
    assert( graph.links.size() == 3 );
    assert( graph.nodes[ 0 ].address == 0x2000 );
    assert( graph.nodes[ 0 ].incoming == 3 );
    assert( graph.nodes[ 0 ].outgoing == 0 );

    const auto rootIter = std::find_if( graph.nodes.begin(), graph.nodes.end(),
        []( const UniverseNode_s& node ) {
            return node.address == 0x1000;
        } );

    assert( rootIter != graph.nodes.end() );
    assert( rootIter->outgoing == 3 );
    assert( graph.referenceCount == 5 );

    const std::size_t leafIndex = static_cast< std::size_t >(
        std::distance( graph.nodes.begin(), std::find_if( graph.nodes.begin(),
            graph.nodes.end(), []( const UniverseNode_s& node ) {
                return node.address == 0x2000;
            } ) ) );

    const std::size_t rootIndex = static_cast< std::size_t >(
        std::distance( graph.nodes.begin(), std::find_if( graph.nodes.begin(),
            graph.nodes.end(), []( const UniverseNode_s& node ) {
                return node.address == 0x1000;
            } ) ) );

    const auto iter = std::find_if( graph.links.begin(), graph.links.end(),
        [rootIndex, leafIndex]( const UniverseLink_s& link ) {
            return static_cast< std::size_t >( link.caller ) == rootIndex &&
                static_cast< std::size_t >( link.callee ) == leafIndex;
        } );

    assert( iter != graph.links.end() && iter->count == 2 );

    const UniverseGraph_s reduced = Universe_BuildGraph( funcs, refs, 2, 1 );
    assert( reduced.nodes.size() == 2 );
    assert( reduced.links.size() == 1 );
    assert( reduced.truncated );
}

static void Tests_RenderAndPicking()
{
    const UniverseGraph_s graph = Universe_BuildGraph({
        { 0x1000, "one" }, { 0x2000, "two" }, { 0x3000, "three" } }, {
        { 0x1000, 0x2000 }, { 0x2000, 0x3000 } } );

    Image_s image;
    CubeCamera_s camera;
    Universe_RenderInto( image, graph, camera, 0, false, 640, 480,
        CDP_GHIDRA_VIOLET );

    assert( image.width == 640 && image.height == 480 );
    assert( image.sourceIndex.size() == 640u * 480u );

    for ( std::size_t i = 0; i < graph.nodes.size(); ++i )
    {
        assert( std::find( image.sourceIndex.begin(), image.sourceIndex.end(),
            i ) != image.sourceIndex.end() );
    }

    const std::vector< std::size_t > picks = image.sourceIndex;
    Universe_RenderInto( image, graph, camera, 0, false, 640, 480,
        CDP_GHIDRA_VIOLET );

    assert( image.sourceIndex == picks );
    Cube_Rotate( camera, 90, 40 );
    Universe_RenderInto( image, graph, camera, 0, true, 640, 480,
        CDP_REFERENCE );

    assert( image.sourceIndex.size() == picks.size() );
}

// IDA live-sync is an *independent* overlay: it must not select the node,
// color its links, touch hit-testing, or change framing in the CPU renderer.
// Selected multi-route highlighting must change the active route's perceived
// brightness enough to be visible (not merely flip a couple of RGB bits).
// Hit-testing, inactive-only routes, and ordinary graphs remain unchanged.
static void Tests_SelectedRoutePulse()
{
    UniverseData_s data;
    data.functions = {
        { 0x1000, "start" }, { 0x2000, "left" },
        { 0x3000, "right" }, { 0x4000, "target" } };

    data.references = {
        { 0x1000, 0x2000, 0x1010 }, { 0x2000, 0x4000, 0x2010 },
        { 0x1000, 0x3000, 0x1020 }, { 0x3000, 0x4000, 0x3010 } };

    std::vector< UniverseCallPath_s > routes = {
        { { 0x1000, 0x2000, 0x4000 }, { 0x1010, 0x2010 } },
        { { 0x1000, 0x3000, 0x4000 }, { 0x1020, 0x3010 } } };

    UniverseRenderStyle_s style;
    style.antiAliasing = false;
    CubeCamera_s camera;

    for ( std::size_t route = 0; route < 2; ++route )
    {
        const UniverseGraph_s graph = Universe_BuildMultiPathGraph(
            data, routes, route );

        assert( graph.multiPathMode && graph.links.size() == 4 );
        Image_s resting, middle, glowing;
        Universe_RenderInto( resting, graph, camera, CANTORDUST_NO_SOURCE,
            true, 640, 480, CDP_REFERENCE, {}, style,
            CANTORDUST_NO_SOURCE, 235u, 0.0f );

        Universe_RenderInto( middle, graph, camera, CANTORDUST_NO_SOURCE,
            true, 640, 480, CDP_REFERENCE, {}, style,
            CANTORDUST_NO_SOURCE, 235u, 0.5f );

        Universe_RenderInto( glowing, graph, camera, CANTORDUST_NO_SOURCE,
            true, 640, 480, CDP_REFERENCE, {}, style,
            CANTORDUST_NO_SOURCE, 235u, 1.0f );

        assert( resting.argb != glowing.argb );
        assert( resting.sourceIndex == middle.sourceIndex );
        assert( resting.sourceIndex == glowing.sourceIndex );

        const std::uint8_t routeBit = static_cast< std::uint8_t >( 1u << route );
        std::size_t activeOnly = CANTORDUST_NO_SOURCE;

        for ( std::size_t i = 0; i < graph.nodes.size(); ++i )
            if ( graph.nodes[ i ].pathMask == routeBit )
                activeOnly = i;

        assert( activeOnly != CANTORDUST_NO_SOURCE );

        const auto luma = []( const std::uint32_t argb ) {
            return ( ( argb >> 16u ) & 255u ) * 54u +
                ( ( argb >> 8u ) & 255u ) * 183u +
                ( argb & 255u ) * 19u;
        };
        std::uint64_t low = 0u, mid = 0u, high = 0u;
        std::size_t nodePixels = 0u;

        for ( std::size_t i = 0; i < resting.argb.size(); ++i )
        {
            if ( resting.sourceIndex[ i ] != activeOnly )
                continue;
            ++nodePixels;
            low += luma( resting.argb[ i ] );
            mid += luma( middle.argb[ i ] );
            high += luma( glowing.argb[ i ] );
        }

        assert( nodePixels > 16u );
        assert( low < mid && mid < high );
        // A visible luminance swing of >= 18/255 on the path-only sphere;
        // this fails the original 18%-toward-white implementation.
        assert( high - low > nodePixels * ( 18u * 256u ) );
        // On-screen projections can overlap shared nodes and crossing links,
        // so isolate route memberships rather than assuming picked pixels
        // belong exclusively to a single colored primitive.
        UniverseGraph_s inactiveOnly = graph;

        const std::uint8_t activeBit = static_cast< std::uint8_t >(
            1u << route );

        for ( UniverseNode_s& node : inactiveOnly.nodes )
            node.pathMask &= static_cast< std::uint8_t >( ~activeBit );

        for ( UniverseLink_s& link : inactiveOnly.links )
            link.pathMask &= static_cast< std::uint8_t >( ~activeBit );

        Image_s inactiveRest, inactiveGlow;
        Universe_RenderInto( inactiveRest, inactiveOnly, camera,
            CANTORDUST_NO_SOURCE, true, 640, 480, CDP_REFERENCE, {}, style,
            CANTORDUST_NO_SOURCE, 235u, 0.0f );

        Universe_RenderInto( inactiveGlow, inactiveOnly, camera,
            CANTORDUST_NO_SOURCE, true, 640, 480, CDP_REFERENCE, {}, style,
            CANTORDUST_NO_SOURCE, 235u, 1.0f );

        assert( inactiveRest.argb == inactiveGlow.argb );
    }

    const UniverseGraph_s regular = Universe_BuildGraph(
        data.functions, data.references );

    Image_s rest, glow;
    Universe_RenderInto( rest, regular, camera, CANTORDUST_NO_SOURCE,
        true, 640, 480, CDP_REFERENCE, {}, style,
        CANTORDUST_NO_SOURCE, 235u, 0.0f );

    Universe_RenderInto( glow, regular, camera, CANTORDUST_NO_SOURCE,
        true, 640, 480, CDP_REFERENCE, {}, style,
        CANTORDUST_NO_SOURCE, 235u, 1.0f );

    assert( rest.argb == glow.argb );
}

// Both accents are composited in the same rendered frame. This complements
// the headless widget-wiring guard: neither pulse may mask the other, and
// animation must never change the node picking buffer.
static void Tests_CombinedAnimatedAccents()
{
    UniverseData_s data;
    data.functions = {
        { 0x1000u, "start" }, { 0x2000u, "left" },
        { 0x3000u, "right" }, { 0x4000u, "target" } };

    data.references = {
        { 0x1000u, 0x2000u, 0x1010u },
        { 0x2000u, 0x4000u, 0x2010u },
        { 0x1000u, 0x3000u, 0x1020u },
        { 0x3000u, 0x4000u, 0x3010u } };

    const std::vector< UniverseCallPath_s > routes = {
        { { 0x1000u, 0x2000u, 0x4000u }, { 0x1010u, 0x2010u } },
        { { 0x1000u, 0x3000u, 0x4000u }, { 0x1020u, 0x3010u } } };

    const UniverseGraph_s graph = Universe_BuildMultiPathGraph(
        data, routes, 0u );

    std::size_t syncIndex = CANTORDUST_NO_SOURCE;

    for ( std::size_t i = 0; i < graph.nodes.size(); ++i )
        if ( graph.nodes[ i ].address == 0x3000u )
            syncIndex = i;

    assert( syncIndex != CANTORDUST_NO_SOURCE );
    UniverseRenderStyle_s style;
    style.antiAliasing = false;
    Image_s dim, haloBright, pathBright, bothBright;

    const auto render = [&]( Image_s& image, const std::uint8_t alpha,
        const float pathPulse ) {
        Universe_RenderInto( image, graph, {}, CANTORDUST_NO_SOURCE,
            true, 480u, 360u, CDP_REFERENCE, {}, style,
            syncIndex, alpha, pathPulse );
    };
    render( dim, 48u, 0.0f );
    render( haloBright, 235u, 0.0f );
    render( pathBright, 48u, 1.0f );
    render( bothBright, 235u, 1.0f );
    assert( dim.argb != haloBright.argb );
    assert( dim.argb != pathBright.argb );
    assert( haloBright.argb != bothBright.argb );
    assert( pathBright.argb != bothBright.argb );
    assert( dim.sourceIndex == haloBright.sourceIndex );
    assert( dim.sourceIndex == pathBright.sourceIndex );
    assert( dim.sourceIndex == bothBright.sourceIndex );
}

static void Tests_IdaLiveSyncHalo()
{
    const UniverseGraph_s graph = Universe_BuildGraph({
        { 0x1000, "active" }, { 0x2000, "caller" },
        { 0x3000, "callee" } }, {
        { 0x2000, 0x1000 }, { 0x1000, 0x3000 } } );

    const auto findIndex = [&graph]( const std::uint64_t address ) {
        for ( std::size_t i = 0; i < graph.nodes.size(); ++i )
            if ( graph.nodes[ i ].address == address )
                return i;

        return CANTORDUST_NO_SOURCE;
    };

    const std::size_t active = findIndex( 0x1000 );
    const std::size_t other = findIndex( 0x2000 );
    assert( active < graph.nodes.size() && other < graph.nodes.size() );

    const CubeCamera_s camera{};
    UniverseRenderStyle_s style;
    style.antiAliasing = false;

    for ( const Palette_e palette : { CDP_REFERENCE, CDP_GHIDRA_VIOLET,
                                     CDP_MATRIX } )
    {
        Image_s baseline, activeImage, otherImage, outOfBounds;
        Universe_RenderInto( baseline, graph, camera,
            CANTORDUST_NO_SOURCE, false, 480, 360, palette, {}, style );

        Universe_RenderInto( activeImage, graph, camera,
            CANTORDUST_NO_SOURCE, false, 480, 360, palette, {}, style,
            active );

        Universe_RenderInto( otherImage, graph, camera,
            CANTORDUST_NO_SOURCE, false, 480, 360, palette, {}, style,
            other );

        Universe_RenderInto( outOfBounds, graph, camera,
            CANTORDUST_NO_SOURCE, false, 480, 360, palette, {}, style,
            graph.nodes.size() + 3 );

        Image_s hiddenHalo, floorHalo, dimHalo;
        Universe_RenderInto( hiddenHalo, graph, camera,
            CANTORDUST_NO_SOURCE, false, 480, 360, palette, {}, style,
            active, 0u );
        // The widget's new minimum glow remains visible, but never changes
        // the pick buffer or the existing node-selection channel.
        Universe_RenderInto( floorHalo, graph, camera,
            CANTORDUST_NO_SOURCE, false, 480, 360, palette, {}, style,
            active, 48u );

        Universe_RenderInto( dimHalo, graph, camera,
            CANTORDUST_NO_SOURCE, false, 480, 360, palette, {}, style,
            active, 96u );

        assert( baseline.argb == outOfBounds.argb );
        assert( baseline.argb == hiddenHalo.argb );
        assert( baseline.argb != floorHalo.argb );
        assert( baseline.sourceIndex == floorHalo.sourceIndex );
        assert( floorHalo.argb != dimHalo.argb );
        assert( baseline.sourceIndex == activeImage.sourceIndex );
        assert( baseline.sourceIndex == otherImage.sourceIndex );
        assert( baseline.argb != activeImage.argb );
        assert( activeImage.argb != otherImage.argb );
        assert( baseline.argb != dimHalo.argb );
        assert( dimHalo.argb != activeImage.argb );
        // The halo must be local, not a fullscreen color wash.
        std::size_t different = 0;

        for ( std::size_t i = 0; i < baseline.argb.size(); ++i )
            if ( baseline.argb[ i ] != activeImage.argb[ i ] )
                ++different;

        assert( different >= 12 && different < 1500 );
        // A selected node keeps its own separate interaction highlight.
        Image_s selected, selectedWithSync;
        Universe_RenderInto( selected, graph, camera,
            active, true, 480, 360, palette, {}, style );

        Universe_RenderInto( selectedWithSync, graph, camera,
            active, true, 480, 360, palette, {}, style, other );

        assert( selected.sourceIndex == selectedWithSync.sourceIndex );
        assert( selected.argb != selectedWithSync.argb );
    }
}

static void Tests_RelativeHeatAndSelection()
{
    float first = 0.0f;
    float last = 1.0f;
    assert( Math_UniverseClipLine( -10.0f, 10.0f, 20.0f, 10.0f,
        100.0f, 100.0f, first, last ) );

    assert( first > 0.30f && first < 0.35f );
    assert( last == 1.0f );
    assert( !Math_UniverseClipLine( -10.0f, -10.0f,
        -5.0f, -5.0f, 100.0f, 100.0f, first, last ) );

    assert( Math_UniverseClipLine( 10.0f, 10.0f, 30.0f, 30.0f,
        100.0f, 100.0f, first, last ) );

    assert( first == 0.0f && last == 1.0f );
    assert( Math_UniverseHeat( 5u, 5u ) == 1.0f );
    assert( Math_UniverseHeat( 300u, 300u ) == 1.0f );
    assert( Math_UniverseHeat( 0u, 5u ) == 0.0f );
    assert( Math_UniverseHeat( 5u, 300u ) < 0.05f );

    UniverseGraph_s small;
    small.nodes = {
        { 0x1000u, "top", 5u, 0u },
        { 0x2000u, "weak", 1u, 0u },
        { 0x3000u, "none", 0u, 0u }
    };
    UniverseGraph_s large = small;
    large.nodes.front().incoming = 300u;

    const std::uint32_t topSmall = Universe_HeatColor(
        small, 0u, CDP_REFERENCE );

    const std::uint32_t topLarge = Universe_HeatColor(
        large, 0u, CDP_REFERENCE );

    assert( topSmall == topLarge );
    assert( Universe_HeatColor( small, 1u, CDP_REFERENCE ) != topSmall );
    assert( Universe_HeatColor( large, 1u, CDP_REFERENCE ) != topLarge );
    assert( Universe_HeatColor( small, 2u, CDP_GHIDRA_VIOLET ) !=
        Universe_HeatColor( small, 0u, CDP_GHIDRA_VIOLET ) );

    assert( Universe_SelectionColor( CDP_REFERENCE ) == 0xFF39FF70u );
    assert( Universe_SelectionColor( CDP_GHIDRA_VIOLET ) == 0xFF39FF70u );

    UniverseGraph_s graph;
    graph.nodes = {
        { 0x1000u, "callee", 5u, 0u, 0u, -0.3f, 0.0f, 0.0f },
        { 0x2000u, "caller", 0u, 5u, 0u, 0.3f, 0.0f, 0.0f }
    };
    graph.links.push_back({ 1u, 0u, 5u, 0u, {} });
    Image_s without;
    Image_s selected;
    CubeCamera_s camera;
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    Universe_RenderInto( without, graph, camera, CANTORDUST_NO_SOURCE,
        true, 640, 480, CDP_GHIDRA_VIOLET );

    Universe_RenderInto( selected, graph, camera, 0u,
        true, 640, 480, CDP_GHIDRA_VIOLET );

    assert( without.argb != selected.argb );
    assert( without.sourceIndex == selected.sourceIndex );

    const std::size_t linkPixel = 240u * 640u + 320u;
    assert( without.sourceIndex[ linkPixel ] == CANTORDUST_NO_SOURCE );
    assert( without.argb[ linkPixel ] !=
        Palette_Get( CDP_GHIDRA_VIOLET ).background );

    const auto brightness = []( const std::uint32_t color ) {
        return ( ( color >> 16 ) & 255u ) +
            ( ( color >> 8 ) & 255u ) + ( color & 255u );
    };
    assert( brightness( selected.argb[ linkPixel ] ) >
        brightness( without.argb[ linkPixel ] ) );
    // The selected node is entirely accent-colored, including its center.
    const CubeProjection_s center = Math_CubeProject( -0.3f, 0.0f, 0.0f,
        Math_CubeTransform( camera ) );

    const int centerX = static_cast< int >( std::lround(
        320.0f + center.x * 480.0f * 0.94f ) );

    const int centerY = static_cast< int >( std::lround(
        240.0f - center.y * 480.0f * 0.94f ) );

    const std::size_t pixel = static_cast< std::size_t >( centerY ) * 640u +
        static_cast< std::size_t >( centerX );

    assert( selected.sourceIndex[ pixel ] == 0u );
    assert( selected.argb[ pixel ] == Universe_SelectionColor( CDP_GHIDRA_VIOLET ) );

    Image_s referenceSelected;
    Universe_RenderInto( referenceSelected, graph, camera, 0u,
        true, 640, 480, CDP_REFERENCE );

    assert( referenceSelected.sourceIndex[ pixel ] == 0u );
    assert( referenceSelected.argb[ pixel ] == 0xFF39FF70u );
    assert( referenceSelected.argb[ pixel ] !=
        Palette_Get( CDP_REFERENCE ).focusCallee );
}

static void Tests_Bounds()
{
    assert( Universe_BuildGraph({}, {}).nodes.empty() );

    const UniversePosition_s origin = Math_UniversePosition( 0, 1, 123 );
    assert( origin.x >= -1 && origin.x <= 1 );
    assert( Math_UniverseNodeRadius( 100, 0 ) >
        Math_UniverseNodeRadius( 0, 0 ) );

    Image_s image;
    Universe_RenderInto( image, {}, {}, 0, true, 4096, 32,
        CDP_REFERENCE );

    assert( image.argb.empty() );
}

static void Tests_RangeAndCallerFocus()
{
    const std::vector< UniverseFunction_s > funcs = {
        { 0x1000u, "printf" }, { 0x2000u, "heavy caller" },
        { 0x3000u, "light caller" }, { 0x4000u, "unrelated" }
    };

    const std::vector< UniverseReference_s > refs = {
        { 0x2000u, 0x1000u }, { 0x2000u, 0x1000u },
        { 0x2000u, 0x1000u }, { 0x2000u, 0x1000u },
        { 0x3000u, 0x1000u }, { 0x4000u, 0x3000u }
    };

    const UniverseGraph_s global = Universe_BuildGraph( funcs, refs );
    const UniverseGraph_s detail = Universe_FilterGraph( global, {
        { 0x2000u, 0x3000u }, { 0x4000u, 0x5000u } } );

    assert( detail.nodes.size() == 2 );
    assert( detail.links.empty() );
    assert( Universe_FilterGraph( global, { { 0x9000u, 0xA000u } } )
        .nodes.empty() );

    const UniverseGraph_s focus = Universe_FocusCallers( global, 0x1000u );
    assert( focus.focusIndex == 0u );
    assert( focus.nodes.size() == 3 );
    assert( focus.links.size() == 2 );
    assert( focus.nodes[ 0 ].address == 0x1000u );
    assert( focus.nodes[ 1 ].address == 0x2000u );
    assert( focus.nodes[ 1 ].focusCalls == 4u );
    assert( focus.nodes[ 2 ].focusCalls == 1u );
    assert( focus.nodes[ 0 ].x == 0.0f );

    for ( std::size_t i = 1; i < focus.nodes.size(); ++i )
    {
        const UniverseNode_s& node = focus.nodes[ i ];
        const float distance = std::sqrt(
            node.x * node.x + node.y * node.y + node.z * node.z );

        assert( distance > 0.64f && distance < 1.20f );
        assert( focus.links[ i - 1 ].caller == i );
        assert( focus.links[ i - 1 ].callee == 0u );
    }

    assert( Math_UniverseNodeRadius( 400, 0 ) >
        Math_UniverseNodeRadius( 3, 0 ) * 4.0f );

    assert( Math_UniverseFocusCallerRadius( 20 ) >
        Math_UniverseFocusCallerRadius( 1 ) );

    assert( Universe_FocusCallers( global, 0xDEADu ).nodes.empty() );

    Image_s image;
    Universe_RenderInto( image, focus, {}, 0, false,
        640, 480, CDP_GHIDRA_VIOLET );

    for ( std::size_t i = 0; i < focus.nodes.size(); ++i )
    {
        assert( std::find( image.sourceIndex.begin(),
            image.sourceIndex.end(), i ) != image.sourceIndex.end() );
    }
}

static void Tests_CallSiteCyclingAndLayout()
{
    const std::vector< UniverseFunction_s > funcs = {
        { 0x1000u, "printf" }, { 0x2000u, "caller" },
        { 0x3000u, "light" }, { 0x4000u, "unrelated" }
    };

    const std::vector< UniverseReference_s > refs = {
        { 0x2000u, 0x1000u, 0x2030u },
        { 0x2000u, 0x1000u, 0x2010u },
        { 0x2000u, 0x1000u, 0x2020u },
        { 0x2000u, 0x1000u, 0x2018u },
        { 0x2000u, 0x1000u, 0x2040u },
        { 0x3000u, 0x1000u, 0x3010u }
    };

    const UniverseGraph_s graph = Universe_BuildGraph( funcs, refs );
    const UniverseGraph_s focus = Universe_FocusCallers( graph, 0x1000u );
    const std::uint64_t expected[] = {
        0x2010u, 0x2018u, 0x2020u, 0x2030u, 0x2040u,
        0x2010u, 0x2018u
    };

    for ( std::size_t i = 0; i < 7; ++i )
    {
        assert( Universe_CallSiteAt( focus, 0x2000u, 0x1000u, i ) ==
            expected[ i ] );
    }

    assert( Universe_CallSiteAt( graph, 0x3000u, 0x1000u, 0 ) ==
        0x3010u );

    assert( Universe_CallSiteAt( graph, 0x4000u, 0x1000u, 0 ) ==
        0x4000u );

    const UniverseGraph_s filtered = Universe_FilterGraph( graph,
        { { 0x1000u, 0x4000u } } );

    assert( Universe_CallSiteAt( filtered, 0x2000u, 0x1000u, 4 ) ==
        0x2040u );

    std::vector< UniverseFunction_s > many;

    for ( std::uint64_t i = 0; i < 360; ++i )
    {
        many.push_back({ 0x10000u + i * 0x100u, "function" });
    }

    std::vector< UniverseReference_s > calls;

    for ( std::uint64_t i = 1; i < 360; ++i )
    {
        calls.push_back({ many[ i ].address, many[ i % 5u ].address,
            many[ i ].address + 1u });
    }

    const UniverseGraph_s spread = Universe_BuildGraph( many, calls );
    float minX = 10.0f, minY = 10.0f, minZ = 10.0f;
    float maxX = -10.0f, maxY = -10.0f, maxZ = -10.0f;

    for ( const UniverseNode_s& node : spread.nodes )
    {
        minX = std::min( minX, node.x );
        minY = std::min( minY, node.y );
        minZ = std::min( minZ, node.z );
        maxX = std::max( maxX, node.x );
        maxY = std::max( maxY, node.y );
        maxZ = std::max( maxZ, node.z );
    }

    assert( maxX - minX > 0.70f );
    assert( maxY - minY > 0.70f );
    assert( maxZ - minZ > 0.70f );
}

static float Tests_FocusNodeDistance( const UniverseGraph_s& graph,
    const std::uint64_t address )
{
    const auto node = std::find_if( graph.nodes.begin(), graph.nodes.end(),
        [address]( const UniverseNode_s& item ) {
            return item.address == address;
        } );

    assert( node != graph.nodes.end() );
    return std::sqrt( node->x * node->x + node->y * node->y +
        node->z * node->z );
}

static void Tests_AddressProximityFocus()
{
    const std::uint64_t center = 0x100000u;
    const std::uint64_t near = 0x100100u;
    const std::uint64_t middle = 0x120000u;
    const std::uint64_t far = 0x900000u;
    const UniverseData_s data = {
        {
            { center, "center" }, { near, "near" },
            { middle, "middle" }, { far, "far" }
        },
        {
            { far, center, far + 1u },
            { far, center, far + 2u },
            { near, center, near + 1u },
            { middle, center, middle + 1u }
        }
    };

    const UniverseGraph_s graph = Universe_BuildGraph(
        data.functions, data.references );

    const UniverseGraph_s tierFocus = Universe_FocusCallers(
        graph, center );

    const UniverseGraph_s dataFocus = Universe_FocusCallersFromData(
        data, center, true );

    for ( const UniverseGraph_s* const focus : { &tierFocus, &dataFocus } )
    {
        assert( focus->focusIndex == 0u );
        assert( focus->nodes.size() == 4u );
        assert( focus->nodes[ 0 ].address == center );
        assert( focus->nodes[ 0 ].x == 0.0f );

        const float nearDistance = Tests_FocusNodeDistance( *focus, near );
        const float middleDistance = Tests_FocusNodeDistance( *focus, middle );
        const float farDistance = Tests_FocusNodeDistance( *focus, far );
        assert( nearDistance < middleDistance );
        assert( middleDistance < farDistance );
        assert( std::abs( nearDistance - 0.66f ) < 0.001f );
        assert( std::abs( farDistance - 1.16f ) < 0.001f );
        assert( focus->nodes[ 1 ].address == far );
        assert( focus->nodes[ 1 ].focusCalls == 2u );
        assert( Universe_CallSiteAt( *focus, far, center, 0u ) == far + 1u );
        assert( Universe_CallSiteAt( *focus, far, center, 1u ) == far + 2u );
    }

    const UniverseGraph_s rangeFocus = Universe_FocusCallersInRangesFromData(
        data, center, { { near, middle + 2u } }, true );

    assert( rangeFocus.nodes.size() == 3u );
    assert( Tests_FocusNodeDistance( rangeFocus, near ) <
        Tests_FocusNodeDistance( rangeFocus, middle ) );

    assert( std::none_of( rangeFocus.nodes.begin(), rangeFocus.nodes.end(),
        [far]( const UniverseNode_s& node ) {
            return node.address == far;
        } ) );

    // Absolute differences must not overflow at the ends of IDA's EA space.
    const std::uint64_t maximum = UINT64_MAX;
    assert( Math_UniverseAddressGap( maximum, 0u ) == maximum );
    assert( Math_UniverseAddressGap( 0u, maximum ) == maximum );
    assert( Math_UniverseAddressGap( center - 5u, center ) == 5u );
    assert( Math_UniverseAddressGap( center + 5u, center ) == 5u );
    assert( Math_UniverseFocusDistance( 1u, 1u, maximum ) <
        Math_UniverseFocusDistance( maximum, 1u, maximum ) );

    assert( std::abs( Math_UniverseFocusDistance( 10u, 10u, 10u ) -
        0.85f ) < 0.001f );

    const float extremeNear = Math_UniverseFocusDistance(
        maximum - 4u, maximum - 4u, maximum );

    const float extremeFar = Math_UniverseFocusDistance(
        maximum, maximum - 4u, maximum );

    assert( std::isfinite( extremeNear ) && std::isfinite( extremeFar ) );
    assert( extremeNear < extremeFar );

    const UniverseData_s global = {
        {
            { 0x2000u, "near caller" },
            { 0xF0000000u, "far caller" },
            { 0x1000u, "global", UNK_GLOBAL }
        },
        {
            { 0x2000u, 0x1000u, 0x2100u },
            { 0xF0000000u, 0x1000u, 0xF0000010u }
        }
    };

    const UniverseGraph_s globalFocus = Universe_FocusCallersFromData(
        global, 0x1000u, true );

    assert( globalFocus.nodes.front().kind == UNK_GLOBAL );
    assert( Tests_FocusNodeDistance( globalFocus, 0x2000u ) <
        Tests_FocusNodeDistance( globalFocus, 0xF0000000u ) );
}

static void Tests_DepthOcclusionAndCameraTranslation()
{
    UniverseGraph_s graph;
    graph.nodes.push_back({ 0x1000u, "rear", 0u, 0u, 0u,
        0.0f, 0.0f, -0.40f });

    graph.nodes.push_back({ 0x2000u, "front", 0u, 0u, 0u,
        0.0f, 0.0f, 0.40f });

    CubeCamera_s camera;
    camera.yaw = 0;
    camera.pitch = 0;
    Image_s frame;
    Universe_RenderInto( frame, graph, camera, CANTORDUST_NO_SOURCE,
        false, 300, 200, CDP_REFERENCE );

    assert( frame.SourceAt( 150u, 100u ) == 1u );
    Universe_RenderInto( frame, graph, camera, CANTORDUST_NO_SOURCE,
        false, 300, 200, CDP_REFERENCE,
        UniverseView_s{ 0.25f, 0.0f, 0.0f } );

    assert( frame.SourceAt( 150u, 100u ) == CANTORDUST_NO_SOURCE );
}

static void Tests_TierCategoriesAndCursorLookup()
{
    UniverseData_s data;

    for ( std::uint64_t i = 0; i < 4105u; ++i )
    {
        data.functions.push_back({ 0x100000u + i * 0x100u, "tier node" });
    }

    data.references = {
        { 0x100000u, 0x100100u, 0x100020u },
        { 0x100000u, 0x100100u, 0x100030u },
        { 0x100200u, 0x100100u, 0x100210u }
    };

    const UniverseGraph_s first = Universe_BuildGraph( data.functions, data.references );
    assert( first.nodes.size() == CANTORDUST_UNIVERSE_MAX_FUNCTIONS );

    const std::size_t rank = Universe_RankForAddress(
        data, data.functions.back().address );

    assert( rank != CANTORDUST_NO_SOURCE );

    const std::size_t tier = rank / CANTORDUST_UNIVERSE_MAX_FUNCTIONS;
    const UniverseGraph_s second = Universe_BuildGraph(
        data.functions, data.references,
        CANTORDUST_UNIVERSE_MAX_FUNCTIONS,
        CANTORDUST_UNIVERSE_MAX_LINKS,
        tier * CANTORDUST_UNIVERSE_MAX_FUNCTIONS );

    assert( std::any_of( second.nodes.begin(), second.nodes.end(),
        [&data]( const UniverseNode_s& node ) {
            return node.address == data.functions.back().address;
        } ) );

    if ( tier != 0 )
    {
        // Tier 2+ retains major hubs to show incoming links across tiers.
        assert( std::any_of( second.nodes.begin(), second.nodes.end(),
            []( const UniverseNode_s& node ) {
                return node.address == 0x100100u;
            } ) );
    }

    assert( Universe_RankForAddress( data, 0xDEADBEEFu ) ==
        CANTORDUST_NO_SOURCE );

    UniverseData_s distinctData;
    distinctData.functions = {
        { 0x1000u, "many from one" }, { 0x2000u, "few from many" },
        { 0x3000u, "caller a" }, { 0x4000u, "caller b" },
        { 0x5000u, "caller c" }
    };

    for ( std::uint64_t i = 0; i < 50u; ++i )
    {
        distinctData.references.push_back({ 0x3000u, 0x1000u,
            0x3100u + i });
    }

    distinctData.references.push_back({ 0x3000u, 0x2000u, 0x3200u });
    distinctData.references.push_back({ 0x4000u, 0x2000u, 0x4200u });
    distinctData.references.push_back({ 0x5000u, 0x2000u, 0x5200u });
    assert( Universe_RankForAddress( distinctData, 0x2000u, true ) <
        Universe_RankForAddress( distinctData, 0x1000u, true ) );

    assert( Universe_RankForAddress( distinctData, 0x1000u, false ) <
        Universe_RankForAddress( distinctData, 0x2000u, false ) );
}

static void Tests_StrictRangeGraphAndBoundary()
{
    const UniverseGraph_s global = Universe_BuildGraph({
        { 0x1000u, "inside callee" }, { 0x2000u, "inside caller" },
        { 0x4000u, "outside caller" }, { 0x5000u, "outside callee" }
    }, {
        { 0x2000u, 0x1000u, 0x200Au },
        { 0x2000u, 0x1000u, 0x20FFu },
        { 0x2000u, 0x1000u, 0x2100u },
        { 0x4000u, 0x1000u, 0x400Au },
        { 0x2000u, 0x5000u, 0x2010u }
    } );

    const auto ranges = std::vector< UniverseAddressRange_s >{
        { 0x1000u, 0x2100u }
    };

    const UniverseGraph_s local = Universe_FilterGraphRangeHeat(
        global, ranges, false );

    assert( local.nodes.size() == 3u );
    assert( local.links.size() == 2u );

    const auto main = std::find_if( local.links.begin(), local.links.end(),
        [&local]( const UniverseLink_s& link ) {
            return local.nodes[ link.callee ].address == 0x1000u;
        } );

    assert( main != local.links.end() && main->count == 2u );
    assert( main->callSites.size() == 2u );

    const auto center = std::find_if( local.nodes.begin(), local.nodes.end(),
        []( const UniverseNode_s& node ) { return node.address == 0x1000u; } );

    assert( center != local.nodes.end() && center->incoming == 2u );

    const UniverseGraph_s unique = Universe_FilterGraphRangeHeat(
        global, ranges, true );

    assert( unique.links.size() == 2u );

    const auto uniqueMain = std::find_if( unique.links.begin(), unique.links.end(),
        [&unique]( const UniverseLink_s& link ) {
            return unique.nodes[ link.callee ].address == 0x1000u;
        } );

    assert( uniqueMain != unique.links.end() && uniqueMain->count == 1u );
    assert( uniqueMain->callSites.size() == 2u );
    assert( Universe_CallSiteAt( unique, 0x2000u, 0x1000u, 1u ) == 0x20FFu );

    const UniverseGraph_s none = Universe_FilterGraphRangeHeat(
        global, { { 0x1000u, 0x2000u } } );

    assert( none.nodes.size() == 1u );
    assert( none.nodes.front().incoming == 0u );
    assert( none.links.empty() );
    // A caller entering the chosen byte range in its middle is still present.
    const UniverseGraph_s middle = Universe_FilterGraphRangeHeat( global,
        { { 0x400Au, 0x400Bu } }, false );

    assert( middle.nodes.size() == 2u );
    assert( middle.links.size() == 1u );
    assert( middle.links.front().count == 1u );
}

static void Tests_RangeRelativeGraphHeat()
{
    const UniverseGraph_s global = Universe_BuildGraph({
        { 0x1000u, "hot" }, { 0x2000u, "cool" },
        { 0x3000u, "local caller" }, { 0x4000u, "outside caller" }
    }, {
        { 0x3000u, 0x1000u, 0x300Au },
        { 0x3000u, 0x1000u, 0x3020u },
        { 0x3000u, 0x2000u, 0x3030u },
        { 0x4000u, 0x1000u, 0x400Au },
        { 0x4000u, 0x1000u, 0x4020u },
        { 0x4000u, 0x1000u, 0x4030u }
    } );

    const std::vector< UniverseAddressRange_s > windows = {
        { 0x1000u, 0x3800u }
    };

    const UniverseGraph_s plain = Universe_FilterGraph( global, windows );
    const UniverseGraph_s local = Universe_FilterGraphRangeHeat( global, windows );
    assert( local.nodes.size() == plain.nodes.size() );
    assert( local.nodes.size() == 3 );

    const auto findByAddress = []( const UniverseGraph_s& graph,
        const std::uint64_t address ) {
        return std::find_if( graph.nodes.begin(), graph.nodes.end(),
            [address]( const UniverseNode_s& node ) {
                return node.address == address;
            } );
    };
    assert( findByAddress( plain, 0x1000u )->incoming == 5u );
    assert( findByAddress( local, 0x1000u )->incoming == 2u );
    assert( findByAddress( local, 0x2000u )->incoming == 1u );
    assert( findByAddress( local, 0x3000u )->outgoing == 3u );
    assert( findByAddress( global, 0x1000u )->incoming == 5u );
    assert( local.links.size() == 2 );
    assert( Universe_CallSiteAt( local, 0x3000u, 0x1000u, 0u ) == 0x300Au );
    assert( Universe_CallSiteAt( local, 0x3000u, 0x1000u, 1u ) == 0x3020u );

    const UniverseGraph_s empty = Universe_FilterGraphRangeHeat(
        global, { { 0x7000u, 0x7100u } } );

    assert( empty.nodes.empty() );
}

static void Tests_FocusHistory()
{
    UniverseHistory history;
    std::optional< std::uint64_t > focus;
    assert( !history.CanGoBack() && !history.CanGoForward() );
    history.Push( 0x1000u );
    history.Push( 0x2000u );
    history.Push( 0x3000u );
    history.Push( 0x3000u );
    assert( history.CanGoBack() && !history.CanGoForward() );
    assert( history.GoBack( focus ) && focus == 0x2000u );
    assert( history.GoBack( focus ) && focus == 0x1000u );
    assert( history.GoForward( focus ) && focus == 0x2000u );
    assert( history.GoForward( focus ) && focus == 0x3000u );
    assert( !history.GoForward( focus ) );
    assert( history.GoBack( focus ) && focus == 0x2000u );
    history.Push( 0x4000u );
    assert( !history.CanGoForward() );
    assert( history.GoBack( focus ) && focus == 0x2000u );
    assert( history.GoBack( focus ) && focus == 0x1000u );
    assert( history.GoBack( focus ) && !focus.has_value() );
    assert( !history.GoBack( focus ) );
    history.Push( 0x5000u );
    assert( history.Current() == 0x5000u );
    history.Reset();
    assert( !history.Current().has_value() );
    assert( !history.CanGoBack() );
}

static void Tests_FocusedCallSiteRange()
{
    const UniverseGraph_s graph = Universe_BuildGraph({
        { 0x1000u, "printf" }, { 0x2000u, "caller a" },
        { 0x3000u, "caller b" } }, {
        { 0x2000u, 0x1000u, 0x2010u },
        { 0x2000u, 0x1000u, 0x2020u },
        { 0x2000u, 0x1000u, 0x2030u },
        { 0x3000u, 0x1000u, 0x3010u }
    } );

    const UniverseGraph_s all = Universe_FocusCallers( graph, 0x1000u );
    assert( all.nodes.size() == 3 );

    const UniverseGraph_s partial = Universe_FocusCallersInRanges(
        graph, 0x1000u, { { 0x2020u, 0x2031u } } );

    assert( partial.focusIndex == 0u );
    assert( partial.nodes.size() == 2 );
    assert( partial.links.size() == 1 );
    assert( partial.nodes[ 1 ].address == 0x2000u );
    assert( partial.nodes[ 1 ].focusCalls == 2u );
    assert( partial.links[ 0 ].count == 2u );
    assert( Universe_CallSiteAt( partial, 0x2000u, 0x1000u, 0u ) == 0x2020u );
    assert( Universe_CallSiteAt( partial, 0x2000u, 0x1000u, 1u ) == 0x2030u );
    assert( Universe_CallSiteAt( partial, 0x2000u, 0x1000u, 2u ) == 0x2020u );

    const UniverseGraph_s empty = Universe_FocusCallersInRanges(
        graph, 0x1000u, { { 0x2040u, 0x3000u } } );

    assert( empty.nodes.size() == 1 );
    assert( empty.nodes[ 0 ].address == 0x1000u );
    assert( empty.links.empty() );
    // Ranges include the call instruction, not the caller's function entry.
    const UniverseGraph_s entry = Universe_FocusCallersInRanges(
        graph, 0x1000u, { { 0x2000u, 0x2001u } } );

    assert( entry.nodes.size() == 1 );
}

static void Tests_DistinctCallersAndGlobals()
{
    const std::vector< UniverseFunction_s > functions = {
        { 0x1000u, "popular" }, { 0x2000u, "caller_a" },
        { 0x3000u, "caller_b" }, { 0x4000u, "caller_c" },
        { 0xA000u, "global_state", UNK_GLOBAL }
    };
    std::vector< UniverseReference_s > refs;

    for ( std::uint64_t i = 0; i < 1200u; ++i )
    {
        refs.push_back({ 0x2000u, 0x1000u, 0x2000u + i });
    }

    for ( std::uint64_t i = 0; i < 399u; ++i )
    {
        refs.push_back({ 0x3000u, 0x1000u, 0x3000u + i });
    }

    refs.push_back({ 0x4000u, 0x1000u, 0x4010u });
    refs.push_back({ 0x2000u, 0xA000u, 0x2300u });
    refs.push_back({ 0x3000u, 0xA000u, 0x3400u });

    UniverseGraph_s graph = Universe_BuildGraph( functions, refs );

    const auto findNode = [&graph]( const std::uint64_t address ) {
        return std::find_if( graph.nodes.begin(), graph.nodes.end(),
            [address]( const UniverseNode_s& node ) {
                return node.address == address;
            } );
    };
    assert( findNode( 0x1000u )->incoming == 1600u );
    assert( findNode( 0xA000u )->kind == UNK_GLOBAL );
    Universe_SetCountMode( graph, true );
    assert( graph.distinctCountMode );
    assert( findNode( 0x1000u )->incoming == 3u );
    assert( findNode( 0xA000u )->incoming == 2u );
    assert( Universe_CallSiteAt( graph, 0x2000u, 0x1000u, 8u ) ==
        0x2008u );

    Universe_SetCountMode( graph, false );
    assert( !graph.distinctCountMode );
    assert( findNode( 0x1000u )->incoming == 1600u );

    const UniverseGraph_s focused = Universe_FocusCallers( graph, 0x1000u );
    UniverseGraph_s uniqueFocus = focused;
    Universe_SetCountMode( uniqueFocus, true );
    assert( uniqueFocus.links.size() == 3u );

    for ( std::size_t i = 1; i < uniqueFocus.nodes.size(); ++i )
    {
        assert( uniqueFocus.nodes[ i ].focusCalls == 1u );
    }

    Image_s image;
    Universe_RenderInto( image, graph, {}, CANTORDUST_NO_SOURCE,
        true, 960, 720, CDP_GHIDRA_VIOLET );

    const std::size_t globalIndex = static_cast< std::size_t >(
        std::distance( graph.nodes.begin(), findNode( 0xA000u ) ) );

    assert( std::find( image.sourceIndex.begin(), image.sourceIndex.end(),
        globalIndex ) != image.sourceIndex.end() );
}

static void Tests_GlobalLinksAcrossTiers()
{
    const UniverseData_s data = {
        {
            { 0x1000u, "caller_one" },
            { 0x2000u, "caller_two" },
            { 0x3000u, "other" },
            { 0xA000u, "global_counter", UNK_GLOBAL }
        },
        {
            { 0x1000u, 0xA000u, 0x1010u },
            { 0x1000u, 0xA000u, 0x1020u },
            { 0x2000u, 0xA000u, 0x2010u },
            { 0x1000u, 0x3000u, 0x1030u }
        }
    };

    // The global is ranked first and both its callers fall outside the
    // one-node tier. A caller must be retained as a context node.
    const UniverseGraph_s tier = Universe_BuildGraph(
        data.functions, data.references, 1u, 1u );

    assert( tier.nodes.size() == 2u );
    assert( tier.nodes[ 0 ].address == 0xA000u );
    assert( tier.nodes[ 1 ].address == 0x1000u );
    assert( tier.links.size() == 1u );
    assert( tier.links[ 0 ].callee == 0u );
    assert( tier.links[ 0 ].caller == 1u );

    // Focus reads all extracted xrefs, including referring functions absent
    // from the bounded tier. The center is still a tetrahedron.
    const UniverseGraph_s focus = Universe_FocusCallersFromData(
        data, 0xA000u, true );

    assert( focus.focusIndex == 0u );
    assert( focus.nodes.front().kind == UNK_GLOBAL );
    assert( focus.nodes.front().incoming == 2u );
    assert( focus.nodes.size() == 3u );
    assert( focus.links.size() == 2u );
    assert( focus.nodes[ 1 ].address == 0x1000u );
    assert( focus.nodes[ 1 ].focusCalls == 2u );
    assert( Universe_CallSiteAt( focus, 0x1000u, 0xA000u, 0u ) == 0x1010u );
    assert( Universe_CallSiteAt( focus, 0x1000u, 0xA000u, 1u ) == 0x1020u );
    assert( focus.nodes[ 2 ].address == 0x2000u );

    // Global address is outside the range, but the reference instruction
    // is inside. The range filter applies to instructions, not the global.
    const UniverseGraph_s local = Universe_FocusCallersInRangesFromData(
        data, 0xA000u, { { 0x1010u, 0x1020u } }, true );

    assert( local.nodes.size() == 2u );
    assert( local.links.size() == 1u );
    assert( local.links.front().count == 1u );
    assert( local.nodes[ 1 ].focusCalls == 1u );
    assert( Universe_CallSiteAt( local, 0x1000u, 0xA000u, 0u ) == 0x1010u );

    const UniverseGraph_s raw = Universe_FocusCallersFromData(
        data, 0xA000u, false );

    assert( raw.nodes[ 0 ].incoming == 3u );

    // When many strong function links consume the link budget, still
    // preserve at least one connection to a displayed global.
    std::vector< UniverseReference_s > refs = data.references;

    for ( std::uint64_t i = 0; i < 60u; ++i )
    {
        refs.push_back({ 0x1000u, 0x3000u, 0x1100u + i });
    }

    const UniverseGraph_s compact = Universe_BuildGraph(
        data.functions, refs, 4u, 1u, 0u, false );

    assert( compact.links.size() == 1u );
    assert( compact.nodes[ compact.links[ 0 ].callee ].kind == UNK_GLOBAL );

    // Orientation is randomized once per process, not each frame.
    Image_s first;
    Image_s second;
    Universe_RenderInto( first, focus, {}, CANTORDUST_NO_SOURCE,
        true, 700, 500, CDP_GHIDRA_VIOLET );

    Universe_RenderInto( second, focus, {}, CANTORDUST_NO_SOURCE,
        true, 700, 500, CDP_GHIDRA_VIOLET );

    assert( first.argb == second.argb );
    assert( first.sourceIndex == second.sourceIndex );
    assert( std::find( first.sourceIndex.begin(), first.sourceIndex.end(),
        0u ) != first.sourceIndex.end() );
}

static void Tests_GlobalFieldRanking()
{
    const UniverseData_s data = {
        {
            { 0x1000u, "caller1" }, { 0x2000u, "caller2" },
            { 0xA000u, "global_struct", UNK_GLOBAL },
            { 0xB000u, "other_global", UNK_GLOBAL }
        },
        {
            { 0x1000u, 0xA000u, 0x1010u, 0xA004u },
            { 0x1000u, 0xA000u, 0x1018u, 0xA004u },
            { 0x1000u, 0xA000u, 0x1020u, 0xA00Cu },
            { 0x2000u, 0xA000u, 0x2010u, 0xA004u },
            { 0x2000u, 0xB000u, 0x2020u, 0xB000u },
            { 0x2000u, 0xB000u, 0x2028u, 0xB000u }
        }
    };
    UniverseGraph_s graph = Universe_BuildGraph(
        data.functions, data.references, 100u, 100u, 0u, true, true );

    auto findIndex = [&graph]( const std::uint64_t address ) {
        const auto iter = std::find_if( graph.nodes.begin(), graph.nodes.end(),
            [address]( const UniverseNode_s& node ) {
                return node.address == address;
            } );

        assert( iter != graph.nodes.end() );
        return static_cast< std::size_t >(
            std::distance( graph.nodes.begin(), iter ) );
    };

    const std::size_t g = findIndex( 0xA000u );
    const std::size_t other = findIndex( 0xB000u );
    Universe_SetCountMode( graph, true, true );
    assert( graph.nodes[ g ].incoming == 3u );
    assert( graph.nodes[ other ].incoming == 1u );
    assert( Universe_RankForAddress( data, 0xA000u, true, true ) == 0u );
    Universe_SetCountMode( graph, true, false );
    assert( graph.nodes[ g ].incoming == 2u );
    assert( graph.nodes[ other ].incoming == 1u );
    Universe_SetCountMode( graph, false, false );
    assert( graph.nodes[ g ].incoming == 4u );
    assert( graph.nodes[ other ].incoming == 2u );

    const UniverseGraph_s local = Universe_FilterGraphRangeHeat( graph,
        { { 0x1010u, 0x101Cu } }, true, true );

    const auto globalNode = std::find_if( local.nodes.begin(), local.nodes.end(),
        []( const UniverseNode_s& node ) { return node.address == 0xA000u; } );

    assert( globalNode != local.nodes.end() );
    assert( globalNode->incoming == 1u );
    assert( local.links.size() == 1u );
    assert( local.links[ 0 ].callSites.size() == 2u );

    const UniverseGraph_s focus = Universe_FocusCallersFromData(
        data, 0xA000u, true, true );

    assert( focus.focusIndex == 0u );
    assert( focus.nodes[ 0 ].incoming == 3u );
    assert( focus.nodes.size() == 3u );
    assert( focus.nodes[ 1 ].focusCalls == 3u );
    assert( Universe_CallSiteAt( focus, 0x1000u, 0xA000u, 1u ) == 0x1018u );
}

static void Tests_RelativeNodeSize()
{
    UniverseGraph_s graph;
    graph.nodes = {
        { 0x1000u, "local_hub", 4u, 0u },
        { 0x2000u, "local_minor", 1u, 0u },
        { 0x3000u, "unreferenced", 0u, 0u }
    };
    UniverseRenderStyle_s style;
    assert( style.relativeNodeSize );
    assert( style.relativeNodeHeat );
    assert( Universe_NodeRadius( graph, 0u, style ) == 12.0f );
    assert( Universe_NodeRadius( graph, 2u, style ) == 2.5f );
    assert( Universe_NodeRadius( graph, 1u, style ) > 2.5f );
    assert( Universe_NodeRadius( graph, 1u, style ) < 12.0f );
    graph.nodes[ 0 ].x = -0.35f;
    graph.nodes[ 1 ].x = 0.0f;
    graph.nodes[ 2 ].x = 0.35f;
    Image_s relativeImage;
    Image_s absoluteImage;
    CubeCamera_s camera;
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    Universe_RenderInto( relativeImage, graph, camera, CANTORDUST_NO_SOURCE,
        false, 640u, 480u, CDP_GHIDRA_VIOLET, {}, style );

    UniverseRenderStyle_s absoluteStyle = style;
    absoluteStyle.relativeNodeSize = false;
    Universe_RenderInto( absoluteImage, graph, camera, CANTORDUST_NO_SOURCE,
        false, 640u, 480u, CDP_GHIDRA_VIOLET, {}, absoluteStyle );

    assert( std::count( relativeImage.sourceIndex.begin(),
        relativeImage.sourceIndex.end(), 0u ) >
        std::count( absoluteImage.sourceIndex.begin(),
            absoluteImage.sourceIndex.end(), 0u ) );

    assert( Universe_HeatColor( graph, 0u, CDP_GHIDRA_VIOLET ) ==
        Universe_HeatColor( graph, 0u, CDP_GHIDRA_VIOLET, style ) );

    style.relativeNodeSize = false;
    assert( Universe_NodeRadius( graph, 0u, style ) ==
        Math_UniverseNodeRadius( 4u, 0u ) );

    assert( Universe_NodeRadius( graph, 0u, style ) < 5.0f );

    UniverseGraph_s tied;
    tied.nodes = {
        { 0x1000u, "one", 3u, 0u },
        { 0x2000u, "two", 3u, 0u }
    };
    style.relativeNodeSize = true;
    assert( Universe_NodeRadius( tied, 0u, style ) == 6.5f );
    assert( Universe_NodeRadius( tied, 1u, style ) == 6.5f );
    tied.nodes.resize( 1u );
    assert( Universe_NodeRadius( tied, 0u, style ) == 8.0f );
    assert( Universe_NodeRadius( tied, 20u, style ) == 0.0f );

    UniverseGraph_s focused;
    focused.focusIndex = 0u;
    focused.nodes = {
        { 0x1000u, "center", 100u, 0u },
        { 0x2000u, "frequent", 0u, 0u, 9u },
        { 0x3000u, "rare", 0u, 0u, 1u }
    };
    assert( Universe_NodeRadius( focused, 1u, style ) == 12.0f );
    assert( Universe_NodeRadius( focused, 2u, style ) == 2.5f );

    const float centerRadius = Universe_NodeRadius( focused, 0u, style );
    style.relativeNodeSize = false;
    assert( Universe_NodeRadius( focused, 0u, style ) == centerRadius );
    assert( Universe_NodeRadius( focused, 1u, style ) ==
        Math_UniverseFocusCallerRadius( 9u ) );

    assert( Universe_NodeRadius( focused, 2u, style ) ==
        Math_UniverseFocusCallerRadius( 1u ) );

    graph.absoluteHeatMaximum = 1000u;
    style.relativeNodeHeat = false;
    assert( Universe_HeatColor( graph, 0u, CDP_GHIDRA_VIOLET, style ) !=
        Universe_HeatColor( graph, 0u, CDP_GHIDRA_VIOLET ) );

    style.relativeNodeHeat = true;
    assert( Universe_HeatColor( graph, 0u, CDP_GHIDRA_VIOLET, style ) ==
        Universe_HeatColor( graph, 0u, CDP_GHIDRA_VIOLET ) );

    const UniverseGraph_s sliced = Universe_FilterGraphRangeHeat(
        Universe_BuildGraph( {
            { 0x1000u, "caller" }, { 0x2000u, "local" },
            { 0x3000u, "distant" }
        }, {
            { 0x1000u, 0x2000u, 0x1010u },
            { 0x1000u, 0x3000u, 0x1020u },
            { 0x1000u, 0x3000u, 0x3020u },
            { 0x1000u, 0x3000u, 0x3030u }
        } ), { { 0x1010u, 0x1011u } } );

    assert( sliced.nodes.size() == 2u );
    assert( sliced.absoluteHeatMaximum == 1u );

    const std::size_t localIndex = sliced.nodes[ 0 ].address == 0x2000u
        ? 0u : 1u;

    assert( sliced.nodes[ localIndex ].incoming == 1u );
    style.relativeNodeSize = true;
    assert( Universe_NodeRadius( sliced, localIndex, style ) == 12.0f );
}

static float Tests_UniverseExtent( const UniverseGraph_s& graph )
{
    float extent = 0.0f;

    for ( const UniverseNode_s& node : graph.nodes )
    {
        extent = std::max( { extent, std::abs( node.x ),
            std::abs( node.y ), std::abs( node.z ) } );
    }

    return extent;
}

static void Tests_ProminentCameraFraming()
{
    UniverseGraph_s graph;
    graph.adaptiveLayout = true;
    graph.nodes.resize( 5u );
    graph.nodes[ 0 ].address = 0x1000u;
    graph.nodes[ 0 ].incoming = 900u;
    graph.nodes[ 0 ].x = 4.0f;
    graph.nodes[ 0 ].y = 3.0f;
    graph.nodes[ 1 ].address = 0x2000u;
    graph.nodes[ 1 ].incoming = 300u;
    graph.nodes[ 1 ].x = 4.3f;
    graph.nodes[ 1 ].y = 2.9f;
    graph.nodes[ 2 ].address = 0x3000u;
    graph.nodes[ 2 ].incoming = 100u;
    graph.nodes[ 2 ].x = 3.8f;
    graph.nodes[ 2 ].y = 3.1f;
    graph.nodes[ 3 ].address = 0x4000u;
    graph.nodes[ 3 ].x = -14.0f;
    graph.nodes[ 3 ].z = 11.0f;
    graph.nodes[ 4 ].address = 0x5000u;
    graph.nodes[ 4 ].x = 12.0f;
    graph.nodes[ 4 ].z = -13.0f;

    const UniverseView_s center = Universe_DefaultView( graph );
    assert( center.panX > 3.8f && center.panX < 4.3f );
    assert( center.panY > 2.8f && center.panY < 3.2f );
    assert( std::abs( center.panZ ) < 0.001f );

    const float fitted = Universe_FrameScale( graph, 1.0f );
    // Low-traffic outliers should no longer force the hubs to tiny pixels.
    assert( fitted > 0.9f );

    // Percentage spacing has a linear, unsaturated visual scale over the
    // entire range, rather than flattening out above 200%.
    const float low = Universe_FrameScale( graph, 1.0f );
    const float mid = Universe_FrameScale( graph, 2.0f );
    const float high = Universe_FrameScale( graph, 20.0f );
    const float highest = Universe_FrameScale( graph, 21.0f );
    assert( mid > low * 1.20f );
    assert( std::abs( ( mid - low ) - ( highest - high ) ) < 0.0005f );
    assert( Universe_FrameScale( graph, 30.0f ) > high );
    // High slider values must not saturate at the previous 3000% limit.
    const float atThreeThousand = Universe_FrameScale( graph, 30.0f );
    const float atTenThousand = Universe_FrameScale( graph, 100.0f );
    assert( atTenThousand > atThreeThousand * 2.0f );

    const float atNineNine = Universe_FrameScale( graph, 99.0f );
    assert( std::abs( ( atTenThousand - atNineNine ) -
        ( mid - low ) ) < 0.001f );

    Image_s image;
    Universe_RenderInto( image, graph, {}, CANTORDUST_NO_SOURCE,
        false, 640u, 480u, CDP_GHIDRA_VIOLET, center );

    for ( std::size_t i = 0; i < 3; ++i )
    {
        assert( std::find( image.sourceIndex.begin(),
            image.sourceIndex.end(), i ) != image.sourceIndex.end() );
    }

    UniverseRenderStyle_s extreme;
    extreme.universeSpacing = 30.0f;
    Universe_RenderInto( image, graph, {}, CANTORDUST_NO_SOURCE,
        false, 640u, 480u, CDP_GHIDRA_VIOLET, center, extreme );

    assert( image.argb.size() == 640u * 480u );
    assert( image.sourceIndex.size() == image.argb.size() );
    extreme.universeSpacing = 100.0f;
    Universe_RenderInto( image, graph, {}, CANTORDUST_NO_SOURCE,
        true, 640u, 480u, CDP_GHIDRA_VIOLET, center, extreme );

    assert( image.argb.size() == 640u * 480u );
    assert( image.sourceIndex.size() == image.argb.size() );

    // A focus sphere keeps its own camera center and projection scale.
    graph.focusIndex = 0u;
    assert( Universe_FrameScale( graph, 30.0f ) == 1.0f );

    const UniverseView_s focused = Universe_DefaultView( graph );
    assert( focused.panX == 0.0f && focused.panY == 0.0f );

    CubeCamera_s camera;
    Cube_Zoom( camera, -120, 0.045f, 8.0f );
    assert( camera.zoom >= 0.045f && camera.zoom < 0.35f );
    camera.zoom = 4.0f;
    Cube_Zoom( camera, 16, 0.045f, 8.0f );
    assert( camera.zoom <= 8.0f && camera.zoom > 4.0f );
}

static void Tests_OrganicAdaptiveLayout()
{
    std::vector< UniverseFunction_s > few;
    std::vector< UniverseFunction_s > many;

    for ( std::uint64_t i = 0; i < 480u; ++i )
    {
        const UniverseFunction_s function = {
            0x140000000ull + i * 0x100ull, "function" };

        many.push_back( function );

        if ( i < 20u )
            few.push_back( function );
    }

    const UniverseGraph_s sparse = Universe_BuildGraph( few, {} );
    const UniverseGraph_s dense = Universe_BuildGraph( many, {} );
    assert( sparse.adaptiveLayout && dense.adaptiveLayout );

    const float sparseExtent = Tests_UniverseExtent( sparse );
    const float denseExtent = Tests_UniverseExtent( dense );
    assert( denseExtent > sparseExtent * 1.5f );
    assert( Universe_FrameScale( dense ) < Universe_FrameScale( sparse ) );
    assert( Universe_FrameScale( dense, 0.55f ) <
        Universe_FrameScale( dense, 1.70f ) );

    assert( Universe_FrameScale( dense, 0.55f ) > 0.0f );
    assert( Universe_FrameScale( dense, 30.0f ) >
        Universe_FrameScale( dense, 1.70f ) );

    assert( Universe_FrameScale( dense, 0.25f ) > 0.0f );

    // Addresses keep stable locations; independent of a camera/slider redraw.
    const UniverseGraph_s repeat = Universe_BuildGraph( many, {} );
    assert( repeat.nodes.size() == dense.nodes.size() );

    for ( std::size_t i = 0; i < dense.nodes.size(); ++i )
    {
        assert( dense.nodes[ i ].address == repeat.nodes[ i ].address );
        assert( dense.nodes[ i ].x == repeat.nodes[ i ].x );
        assert( dense.nodes[ i ].y == repeat.nodes[ i ].y );
        assert( dense.nodes[ i ].z == repeat.nodes[ i ].z );
    }

    // Default layout is the regular lattice. Organic is opt-in, stable,
    // rounded in all three axes, and preserves every address/link.
    const auto isOnLattice = []( const UniverseGraph_s& graph ) {
        for ( std::size_t i = 1; i < graph.nodes.size(); ++i )
        {
            const float cells = ( graph.nodes[ i ].x -
                graph.nodes[ i - 1u ].x ) / 0.24f;

            if ( std::abs( cells - std::round( cells ) ) > 0.04f )
            {
                return false;
            }
        }

        return true;
    };
    assert( isOnLattice( dense ) );

    UniverseGraph_s organic = dense;
    Universe_RelayoutGraph( organic, true );
    assert( !isOnLattice( organic ) );
    assert( organic.nodes.size() == dense.nodes.size() );
    assert( organic.links.size() == dense.links.size() );
    float minRadius = std::numeric_limits< float >::max();
    float maxRadius = 0.0f;

    for ( std::size_t i = 0; i < dense.nodes.size(); ++i )
    {
        const UniverseNode_s& regular = dense.nodes[ i ];
        const UniverseNode_s& cloud = organic.nodes[ i ];
        assert( regular.address == cloud.address );

        const float radius = std::sqrt( cloud.x * cloud.x +
            cloud.y * cloud.y + cloud.z * cloud.z );

        if ( radius > 0.55f * Tests_UniverseExtent( organic ) )
        {
            minRadius = std::min( minRadius, radius );
            maxRadius = std::max( maxRadius, radius );
        }
    }

    assert( minRadius > 0.0f && maxRadius > minRadius );
    assert( maxRadius / minRadius < 2.5f );
    UniverseGraph_s repeatOrganic = dense;
    Universe_RelayoutGraph( repeatOrganic, true );

    for ( std::size_t i = 0; i < organic.nodes.size(); ++i )
    {
        assert( organic.nodes[ i ].x == repeatOrganic.nodes[ i ].x );
        assert( organic.nodes[ i ].y == repeatOrganic.nodes[ i ].y );
        assert( organic.nodes[ i ].z == repeatOrganic.nodes[ i ].z );
    }

    Universe_RelayoutGraph( repeatOrganic, false );

    for ( std::size_t i = 0; i < dense.nodes.size(); ++i )
    {
        assert( dense.nodes[ i ].x == repeatOrganic.nodes[ i ].x );
        assert( dense.nodes[ i ].y == repeatOrganic.nodes[ i ].y );
        assert( dense.nodes[ i ].z == repeatOrganic.nodes[ i ].z );
    }

    UniverseRenderStyle_s style;
    Image_s compact;
    Image_s spacious;
    style.universeSpacing = 0.55f;
    Universe_RenderInto( compact, sparse, {}, CANTORDUST_NO_SOURCE,
        true, 640u, 480u, CDP_GHIDRA_VIOLET, {}, style );

    style.universeSpacing = 1.70f;
    Universe_RenderInto( spacious, sparse, {}, CANTORDUST_NO_SOURCE,
        true, 640u, 480u, CDP_GHIDRA_VIOLET, {}, style );

    assert( compact.sourceIndex != spacious.sourceIndex );

    for ( std::size_t i = 0; i < sparse.nodes.size(); ++i )
    {
        assert( std::find( compact.sourceIndex.begin(),
            compact.sourceIndex.end(), i ) != compact.sourceIndex.end() );

        assert( std::find( spacious.sourceIndex.begin(),
            spacious.sourceIndex.end(), i ) != spacious.sourceIndex.end() );
    }

    // Focused spheres remain independent of the global density setting.
    const UniverseGraph_s focus = Universe_FocusCallers(
        Universe_BuildGraph( {
            { 0x1000u, "a" }, { 0x2000u, "b" }
        }, { { 0x2000u, 0x1000u, 0x2010u } } ), 0x1000u );

    assert( focus.focusIndex == 0u );
    assert( Universe_FrameScale( focus, 0.55f ) == 1.0f );
    assert( Universe_FrameScale( focus, 1.70f ) == 1.0f );
    UniverseGraph_s focusAfter = focus;
    Universe_RelayoutGraph( focusAfter, true );
    assert( focusAfter.nodes.size() == focus.nodes.size() );

    for ( std::size_t i = 0; i < focus.nodes.size(); ++i )
    {
        assert( focusAfter.nodes[ i ].x == focus.nodes[ i ].x );
        assert( focusAfter.nodes[ i ].y == focus.nodes[ i ].y );
        assert( focusAfter.nodes[ i ].z == focus.nodes[ i ].z );
    }
}

static void Tests_RegularGlobalTetrahedron()
{
    const auto& vertices = Math_CubeTetrahedronVertices();
    constexpr float tolerance = 0.00002f;
    constexpr float edgeSquared = 8.0f / 3.0f;

    // Globals share the upright, unit-radius tetrahedron used by Byte Cloud.
    for ( std::size_t axis = 0; axis < 3; ++axis )
    {
        float center = 0.0f;
        for ( const auto& vertex : vertices )
            center += vertex[ axis ];
        assert( std::abs( center ) < tolerance );
    }

    for ( std::size_t a = 0; a < vertices.size(); ++a )
    {
        for ( std::size_t b = a + 1; b < vertices.size(); ++b )
        {
            float distanceSquared = 0.0f;
            for ( std::size_t axis = 0; axis < 3; ++axis )
            {
                const float d = vertices[ a ][ axis ] - vertices[ b ][ axis ];
                distanceSquared += d * d;
            }
            assert( std::abs( distanceSquared - edgeSquared ) < tolerance );
        }
    }

    // Randomized vertex orientations must still project around the global's
    // location and leave a selectable center at ordinary camera angles.
    UniverseGraph_s graph;
    graph.nodes = { { 0xA000u, "global", 6u, 0u, 0u,
        0.0f, 0.0f, 0.0f, UNK_GLOBAL } };
    UniverseRenderStyle_s style;
    style.antiAliasing = false;
    Image_s image;
    CubeCamera_s camera;

    for ( const auto angles : { std::array< float, 2 >{ 0.0f, 0.0f },
                               std::array< float, 2 >{ 0.68f, 0.38f } } )
    {
        camera.yaw = angles[ 0 ];
        camera.pitch = angles[ 1 ];
        Universe_RenderInto( image, graph, camera, CANTORDUST_NO_SOURCE,
            false, 640u, 480u, CDP_GHIDRA_VIOLET, {}, style );
        assert( image.SourceAt( 320u, 240u ) == 0u );
    }
}

static void Tests_TetrahedronExtremeSpacing()
{
    // At very high spacing, an outlying global can approach or pass the
    // perspective pole. Its tetrahedron must never fill the whole viewport.
    UniverseGraph_s graph;
    graph.adaptiveLayout = true;
    graph.nodes = {
        { 0x1000u, "hub_a", 500u, 0u, 0u, -0.4f, 0.0f, 0.0f },
        { 0x2000u, "hub_b", 300u, 0u, 0u, 0.4f, 0.0f, 0.0f },
        { 0x3000u, "hub_c", 150u, 0u, 0u, 0.0f, 0.4f, 0.0f },
        { 0xA000u, "global", 80u, 0u, 0u, 0.0f, 0.0f, 0.0f,
            UNK_GLOBAL }
    };
    UniverseRenderStyle_s style;
    style.universeSpacing = 30.0f;
    style.relativeNodeSize = false;

    const UniverseView_s view = Universe_DefaultView( graph );
    const float frameScale = Universe_FrameScale( graph, 30.0f );
    assert( frameScale > 1.0f );
    CubeCamera_s camera;
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    Image_s image;

    constexpr std::size_t globalIndex = 3u;

    graph.nodes[ globalIndex ].x = view.panX;
    graph.nodes[ globalIndex ].y = view.panY;
    graph.nodes[ globalIndex ].z = view.panZ + 6.5f / frameScale;
    Universe_RenderInto( image, graph, camera, CANTORDUST_NO_SOURCE,
        false, 800u, 600u, CDP_GHIDRA_VIOLET, view, style );

    std::size_t pixels = 0;

    for ( std::size_t i = 0; i < image.sourceIndex.size(); ++i )
    {
        if ( image.sourceIndex[ i ] != globalIndex )
            continue;

        const int x = static_cast< int >( i % 800u );
        const int y = static_cast< int >( i / 800u );
        assert( std::abs( x - 400 ) < 50 );
        assert( std::abs( y - 300 ) < 50 );
        ++pixels;
    }

    assert( pixels > 0u && pixels < 4000u );

    // With the global near the perspective pole, its faces are clipped out
    // instead of folding over and blocking the rest of the scene.
    graph.nodes[ globalIndex ].z = view.panZ + 9.05f / frameScale;
    Universe_RenderInto( image, graph, camera, CANTORDUST_NO_SOURCE,
        false, 800u, 600u, CDP_GHIDRA_VIOLET, view, style );

    assert( std::find( image.sourceIndex.begin(), image.sourceIndex.end(),
        globalIndex ) == image.sourceIndex.end() );

    // Ordinary function spheres retain their previous render path.
    graph.nodes[ globalIndex ].kind = UNK_FUNCTION;
    graph.nodes[ globalIndex ].z = view.panZ + 6.5f / frameScale;
    Universe_RenderInto( image, graph, camera, CANTORDUST_NO_SOURCE,
        false, 800u, 600u, CDP_GHIDRA_VIOLET, view, style );

    assert( std::find( image.sourceIndex.begin(), image.sourceIndex.end(),
        globalIndex ) != image.sourceIndex.end() );
}

static void Tests_PerspectiveNodeSizeAndAntiAliasing()
{
    UniverseGraph_s graph;
    graph.nodes = {
        { 0x1000u, "far_function", 12u, 0u, 0u, -0.72f, 0.0f, -3.0f },
        { 0x2000u, "near_function", 12u, 0u, 0u, 0.72f, 0.0f, 3.0f }
    };
    CubeCamera_s camera;
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    UniverseRenderStyle_s style;
    style.antiAliasing = false;
    Image_s raw;
    Universe_RenderInto( raw, graph, camera, CANTORDUST_NO_SOURCE,
        false, 640u, 480u, CDP_GHIDRA_VIOLET, {}, style );

    const std::size_t farPixels = static_cast< std::size_t >(
        std::count( raw.sourceIndex.begin(), raw.sourceIndex.end(), 0u ) );

    const std::size_t nearPixels = static_cast< std::size_t >(
        std::count( raw.sourceIndex.begin(), raw.sourceIndex.end(), 1u ) );

    assert( farPixels > 0u );
    assert( nearPixels > farPixels * 2u );

    // Toggle affects final colors only: address picking is byte-for-byte
    // identical, and the optional pass must visibly soften rendered edges.
    Image_s smooth;
    style.antiAliasing = true;
    Universe_RenderInto( smooth, graph, camera, CANTORDUST_NO_SOURCE,
        false, 640u, 480u, CDP_GHIDRA_VIOLET, {}, style );

    assert( smooth.sourceIndex == raw.sourceIndex );
    assert( smooth.argb != raw.argb );

    const Image_s first = smooth;
    Universe_RenderInto( smooth, graph, camera, CANTORDUST_NO_SOURCE,
        false, 640u, 480u, CDP_GHIDRA_VIOLET, {}, style );

    assert( smooth.argb == first.argb );

    // Both tetrahedra and spheres use the same perspective-size policy;
    // keep the near-global bound from Checkpoint 46 intact.
    graph.nodes[ 0 ].kind = UNK_GLOBAL;
    graph.nodes[ 1 ].kind = UNK_GLOBAL;
    Universe_RenderInto( raw, graph, camera, CANTORDUST_NO_SOURCE,
        false, 640u, 480u, CDP_GHIDRA_VIOLET, {}, style );

    const std::size_t farGlobal = static_cast< std::size_t >(
        std::count( raw.sourceIndex.begin(), raw.sourceIndex.end(), 0u ) );

    const std::size_t nearGlobal = static_cast< std::size_t >(
        std::count( raw.sourceIndex.begin(), raw.sourceIndex.end(), 1u ) );

    assert( farGlobal > 0u && nearGlobal > farGlobal );
}

static void Tests_ClippedVisibleConnections()
{
    UniverseGraph_s graph;
    graph.nodes = {
        // One endpoint crosses the near plane; its node must stay culled.
        { 0x1000u, "behind", 0u, 1u, 0u, -0.45f, -0.2f, 8.0f },
        { 0x2000u, "visible", 1u, 0u, 0u, 0.65f, 0.5f, 0.0f }
    };
    graph.links.push_back({ 0u, 1u, 3u, 0u, {} });
    CubeCamera_s camera;
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    UniverseRenderStyle_s style;
    style.antiAliasing = false;
    Image_s connected;
    Image_s noLinks;
    Universe_RenderInto( connected, graph, camera, CANTORDUST_NO_SOURCE,
        true, 640u, 480u, CDP_GHIDRA_VIOLET, {}, style );

    Universe_RenderInto( noLinks, graph, camera, CANTORDUST_NO_SOURCE,
        false, 640u, 480u, CDP_GHIDRA_VIOLET, {}, style );

    assert( std::find( connected.sourceIndex.begin(),
        connected.sourceIndex.end(), 1u ) != connected.sourceIndex.end() );

    assert( std::find( connected.sourceIndex.begin(),
        connected.sourceIndex.end(), 0u ) == connected.sourceIndex.end() );

    std::size_t visibleLinkPixels = 0;

    for ( std::size_t i = 0; i < connected.argb.size(); ++i )
    {
        if ( connected.argb[ i ] != noLinks.argb[ i ] )
        {
            ++visibleLinkPixels;
        }
    }

    assert( visibleLinkPixels > 20u );

    // Neither endpoint is on screen, but their link crosses the view.
    graph.nodes[ 0 ].x = -100.0f;
    graph.nodes[ 0 ].y = 0.0f;
    graph.nodes[ 0 ].z = 0.0f;
    graph.nodes[ 1 ].x = 100.0f;
    graph.nodes[ 1 ].y = 0.0f;
    Universe_RenderInto( connected, graph, camera, CANTORDUST_NO_SOURCE,
        true, 640u, 480u, CDP_GHIDRA_VIOLET, {}, style );

    const std::size_t middle = 240u * 640u + 320u;
    assert( connected.sourceIndex[ middle ] == CANTORDUST_NO_SOURCE );
    assert( connected.argb[ middle ] !=
        Palette_Get( CDP_GHIDRA_VIOLET ).background );

    // Fully behind the near plane: both the nodes and line stay hidden.
    graph.nodes[ 0 ].z = 8.0f;
    graph.nodes[ 1 ].z = 9.0f;
    Universe_RenderInto( connected, graph, camera, CANTORDUST_NO_SOURCE,
        true, 640u, 480u, CDP_GHIDRA_VIOLET, {}, style );

    assert( connected.argb[ middle ] ==
        Palette_Get( CDP_GHIDRA_VIOLET ).background );
}

int main()
{
    Tests_SelectedRoutePulse();
    Tests_CombinedAnimatedAccents();
    Tests_IdaLiveSyncHalo();
    Tests_ClippedVisibleConnections();
    Tests_PerspectiveNodeSizeAndAntiAliasing();
    Tests_TetrahedronExtremeSpacing();
    Tests_RegularGlobalTetrahedron();
    Tests_ProminentCameraFraming();
    Tests_OrganicAdaptiveLayout();
    Tests_RelativeNodeSize();
    Tests_AddressProximityFocus();
    Tests_GlobalLinksAcrossTiers();
    Tests_TierCategoriesAndCursorLookup();
    Tests_StrictRangeGraphAndBoundary();
    Tests_DistinctCallersAndGlobals();
    Tests_RangeRelativeGraphHeat();
    Tests_FocusHistory();
    Tests_FocusedCallSiteRange();
    Tests_BuildAndCounts();
    Tests_GlobalFieldRanking();
    Tests_RenderAndPicking();
    Tests_Bounds();
    Tests_RelativeHeatAndSelection();
    Tests_RangeAndCallerFocus();
    Tests_DepthOcclusionAndCameraTranslation();
    Tests_CallSiteCyclingAndLayout();
    std::cout << "Universe graph tests passed.\n";
    return 0;
}
