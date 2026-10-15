#include "cantordust/universe.h"
#include "math/universe.h"
#include "math/color.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>
#include <set>

using namespace cantordust;

// Keep exploration limits in the agreed power-of-two tiers.
static_assert( CANTORDUST_UNIVERSE_HOP_DEFAULT_FUNCTIONS == 8192u );
static_assert( CANTORDUST_UNIVERSE_HOP_MAX_FUNCTIONS == 16384u );
static_assert( CANTORDUST_UNIVERSE_HOP_DEFAULT_LINKS == 32768u );
static_assert( CANTORDUST_UNIVERSE_HOP_MAX_LINKS == 65536u );
static_assert( CANTORDUST_UNIVERSE_HOP_MAX_FANOUT == 1024u );

static UniverseData_s Fixture()
{
    UniverseData_s data;
    data.functions = {
        { 0x1000, "NetworkReceive", UNK_FUNCTION },
        { 0x2000, "DecodePacket", UNK_FUNCTION },
        { 0x3000, "SensitiveRoutine", UNK_FUNCTION },
        { 0x4000, "DebugLogger", UNK_FUNCTION },
        { 0x5000, "g_Configuration", UNK_GLOBAL }
    };
    data.references = {
        { 0x1000, 0x2000, 0x1010 },
        { 0x1000, 0x2000, 0x1020 },
        { 0x2000, 0x3000, 0x2010 },
        { 0x4000, 0x1000, 0x4010 },
        { 0x2000, 0x1000, 0x2020 },
        { 0x1000, 0x5000, 0x1030 },
        { 0x5000, 0x3000, 0x5010 }
    };
    return data;
}

static const UniverseNode_s* FindNode( const UniverseGraph_s& graph,
    const std::uint64_t address )
{
    const auto it = std::find_if( graph.nodes.begin(), graph.nodes.end(),
        [address]( const UniverseNode_s& n ) {
            return n.address == address;
        } );

    return it == graph.nodes.end() ? nullptr : &*it;
}

static void TestDirectedFocus()
{
    const UniverseData_s data = Fixture();
    const UniverseGraph_s both = Universe_FocusConnectionsFromData(
        data, 0x1000, UFM_BOTH );

    assert( both.focusIndex == 0 );
    assert( both.nodes.size() == 4 ); // center + caller + callee + global
    const auto* logger = FindNode( both, 0x4000 );
    const auto* decoder = FindNode( both, 0x2000 );
    const auto* global = FindNode( both, 0x5000 );
    assert( logger && logger->focusDirection == 1 );
    assert( decoder && decoder->focusDirection == 3 ); // reciprocal
    assert( global && global->focusDirection == 2 );
    assert( Universe_CallSiteAt( both, 0x1000, 0x2000, 0 ) == 0x1010 );
    assert( Universe_CallSiteAt( both, 0x2000, 0x1000, 0 ) == 0x2020 );

    const UniverseGraph_s callees = Universe_FocusConnectionsFromData(
        data, 0x1000, UFM_CALLEES );

    assert( callees.nodes.size() == 3 );
    assert( FindNode( callees, 0x4000 ) == nullptr );
    assert( FindNode( callees, 0x2000 )->focusDirection == 2 );

    const UniverseGraph_s callers = Universe_FocusConnectionsFromData(
        data, 0x1000, UFM_CALLERS );

    assert( callers.nodes.size() == 3 );
    assert( FindNode( callers, 0x5000 ) == nullptr );

    const std::vector< UniverseAddressRange_s > ranges = {
        { 0x1000, 0x1020 } };

    const UniverseGraph_s local = Universe_FocusConnectionsFromData(
        data, 0x1000, UFM_BOTH, &ranges );

    assert( local.nodes.size() == 2 ); // only 1010 outgoing call
    assert( local.links.size() == 1 );
    assert( local.nodes[ 1 ].focusDirection == 2 );
}

static void TestFocusSphereAndPalette()
{
    // Exercise both hemispheres with enough neighbors of every direction
    // to detect the v49 direction-separated clamping, regardless of rank.
    UniverseData_s data;

    constexpr std::uint64_t center = 0x1000;
    data.functions.push_back({ center, "center" });

    for ( std::uint32_t i = 1; i <= 48; ++i )
    {
        const std::uint64_t neighbor = 0x2000 + i * 0x100;
        data.functions.push_back({ neighbor, "neighbor" });

        const bool caller = i % 2 == 0;
        data.references.push_back({ caller ? neighbor : center,
            caller ? center : neighbor, neighbor + 0x10 });

        if ( i % 7 == 0 )
            data.references.push_back({ caller ? center : neighbor,
                caller ? neighbor : center, neighbor + 0x20 });
    }

    for ( const UniverseFocusMode_e mode : { UFM_BOTH, UFM_CALLEES } )
    {
        const UniverseGraph_s graph = Universe_FocusConnectionsFromData(
            data, center, mode );

        std::uint64_t minGap = UINT64_MAX;
        std::uint64_t maxGap = 0;

        for ( std::size_t i = 1; i < graph.nodes.size(); ++i )
        {
            const auto gap = Math_UniverseAddressGap(
                graph.nodes[ i ].address, center );

            minGap = std::min( minGap, gap );
            maxGap = std::max( maxGap, gap );
        }

        std::uint32_t positiveCallers = 0, negativeCallers = 0;
        std::uint32_t positiveCallees = 0, negativeCallees = 0;

        for ( std::size_t i = 1; i < graph.nodes.size(); ++i )
        {
            const UniverseNode_s& node = graph.nodes[ i ];
            const float radius = Math_UniverseFocusDistance(
                Math_UniverseAddressGap( node.address, center ),
                minGap, maxGap );

            const UniversePosition_s oldLayout = Math_UniverseFocusPosition(
                i - 1, graph.nodes.size() - 1, node.address, radius );

            assert( std::abs( node.x - oldLayout.x ) < 1e-5f );
            assert( std::abs( node.y - oldLayout.y ) < 1e-5f );
            assert( std::abs( node.z - oldLayout.z ) < 1e-5f );

            if ( node.focusDirection == 1 )
            {
                positiveCallers += node.x > 0;
                negativeCallers += node.x < 0;
            }

            if ( node.focusDirection == 2 )
            {
                positiveCallees += node.x > 0;
                negativeCallees += node.x < 0;
            }
        }

        if ( mode == UFM_BOTH )
            assert( positiveCallers > 2 && negativeCallers > 2 );

        assert( positiveCallees > 2 && negativeCallees > 2 );
    }

    UniverseGraph_s graph = Universe_FocusConnectionsFromData(
        Fixture(), 0x1000, UFM_BOTH );

    const auto indexOf = [&graph]( const std::uint64_t address ) {
        for ( std::size_t i = 0; i < graph.nodes.size(); ++i )
            if ( graph.nodes[ i ].address == address )
                return i;

        return CANTORDUST_NO_SOURCE;
    };

    const std::size_t caller = indexOf( 0x4000 );
    const std::size_t callee = indexOf( 0x5000 );
    const std::size_t reciprocal = indexOf( 0x2000 );
    assert( caller != CANTORDUST_NO_SOURCE );
    assert( callee != CANTORDUST_NO_SOURCE );
    assert( reciprocal != CANTORDUST_NO_SOURCE );

    const std::uint32_t paletteCaller = Universe_HeatColor(
        graph, caller, CDP_GHIDRA_VIOLET );

    graph.nodes[ caller ].focusDirection = 0;
    assert( Universe_HeatColor( graph, caller, CDP_GHIDRA_VIOLET ) ==
        paletteCaller );

    graph.nodes[ caller ].focusDirection = 1;
    assert( Universe_HeatColor( graph, callee, CDP_GHIDRA_VIOLET ) ==
        0xFFEFFF24u );

    assert( Universe_HeatColor( graph, callee, CDP_REFERENCE ) ==
        0xFFFFFF00u );

    const std::uint32_t reciprocalColor = Universe_HeatColor(
        graph, reciprocal, CDP_GHIDRA_VIOLET );

    graph.nodes[ reciprocal ].focusDirection = 1;

    const std::uint32_t unblended = Universe_HeatColor(
        graph, reciprocal, CDP_GHIDRA_VIOLET );

    assert( reciprocalColor == Math_BlendColor(
        unblended, 0xFFEFFF24u, 0.50 ) );
    // Every preset must have an intentional callee accent. The first two
    // exactly match the requested RGB yellow and the existing Ghidra neon.
    const std::uint32_t accents[ CDP_COUNT ] = {
        0xFFFFFF00u, 0xFFEFFF24u, 0xFFFFD76Au, 0xFF58EFFFu,
        0xFFFFC36Au, 0xFFFF57D8u, 0xFF65FFD1u, 0xFFFFD052u,
        0xFFFF9274u, 0xFF54F2FFu, 0xFFEDEDEDu, 0xFF57DFFFu,
        0xFFFFEBA8u, 0xFF4BF3FFu, 0xFFFFC857u, 0xFFE4A2FFu
    };

    for ( std::size_t p = 0; p < Palette_Count(); ++p )
    {
        const Palette_e palette = static_cast< Palette_e >( p );
        const std::uint32_t base = Universe_HeatColor( graph, caller, palette );
        const std::uint32_t outgoing = Universe_HeatColor(
            graph, callee, palette );

        assert( outgoing == accents[ p ] );
        assert( outgoing != base );
        graph.nodes[ reciprocal ].focusDirection = 3;

        const std::uint32_t mixed = Universe_HeatColor(
            graph, reciprocal, palette );

        graph.nodes[ reciprocal ].focusDirection = 1;

        const std::uint32_t callerOnly = Universe_HeatColor(
            graph, reciprocal, palette );

        assert( mixed == Math_BlendColor(
            callerOnly, accents[ p ], 0.50 ) );
        // Neither the original caller heat color nor center should change
        // based on the focused callee accent.
        assert( Universe_HeatColor( graph, caller, palette ) == base );

        const std::size_t center = graph.focusIndex;
        const std::uint32_t rootColor = Universe_HeatColor(
            graph, center, palette );

        graph.nodes[ center ].focusDirection = 2;
        assert( Universe_HeatColor( graph, center, palette ) == rootColor );
        graph.nodes[ center ].focusDirection = 0;
        graph.nodes[ reciprocal ].focusDirection = 3;
    }

    graph.focusIndex = CANTORDUST_NO_SOURCE;

    for ( std::size_t p = 0; p < Palette_Count(); ++p )
    {
        const Palette_e palette = static_cast< Palette_e >( p );
        graph.nodes[ callee ].focusDirection = 0;

        const auto withoutDirection = Universe_HeatColor(
            graph, callee, palette );

        graph.nodes[ callee ].focusDirection = 2;
        assert( Universe_HeatColor(
            graph, callee, palette ) == withoutDirection );
    }
}

static void TestFocusedLinkFade()
{
    UniverseGraph_s graph;
    graph.focusIndex = 0;
    graph.nodes = {
        { 0x1000, "center", 12, 12, 0, -0.8f, 0.0f, 0.0f },
        { 0x2000, "callee", 0, 0, 12, 0.8f, 0.0f, 0.0f }
    };
    graph.nodes[ 1 ].focusDirection = 2;
    graph.links.push_back({ 0, 1, 12, { 0x1010 } });
    CubeCamera_s camera;
    camera.yaw = 0;
    camera.pitch = 0;
    UniverseRenderStyle_s style;
    style.antiAliasing = false;
    Image_s image;
    Universe_RenderInto( image, graph, camera, CANTORDUST_NO_SOURCE,
        true, 900, 600, CDP_GHIDRA_VIOLET, {}, style );

    const auto pixel = [&image]( const std::size_t x ) {
        return image.argb[ 300 * image.width + x ];
    };

    const std::uint32_t nearCenter = pixel( 254 );
    const std::uint32_t middle = pixel( 450 );
    const std::uint32_t nearCallee = pixel( 646 );
    const auto red = []( const std::uint32_t c ) { return ( c >> 16 ) & 255u; };
    const auto green = []( const std::uint32_t c ) { return ( c >> 8 ) & 255u; };
    const auto blue = []( const std::uint32_t c ) { return c & 255u; };
    const auto brightness = [&]( const std::uint32_t c ) {
        return red( c ) + green( c ) + blue( c );
    };
    // Palette pink at the focused center transitions to neon yellow at
    // the callee, with the v48 brighter ends / subdued middle glow.
    assert( blue( nearCenter ) > green( nearCenter ) );
    assert( green( nearCallee ) > blue( nearCallee ) * 2 );
    assert( green( nearCallee ) > green( middle ) );
    assert( brightness( nearCenter ) > brightness( middle ) );
    assert( brightness( nearCallee ) > brightness( middle ) );

    // Palette switches must carry through to the rendered link endpoint,
    // retaining the brighter near-node glow instead of leaving fixed yellow
    // pixels from the previous Detail palette.
    for ( const Palette_e palette : { CDP_REFERENCE, CDP_EMBER,
        CDP_MATRIX, CDP_OCEANIC, CDP_MONOCHROME } )
    {
        Universe_RenderInto( image, graph, camera, CANTORDUST_NO_SOURCE,
            true, 900, 600, palette, {}, style );

        const std::uint32_t near = pixel( 646 );
        const std::uint32_t mid = pixel( 450 );
        assert( brightness( near ) > brightness( mid ) );

        switch ( palette )
        {
            case CDP_REFERENCE: // Pure yellow on RGB Reference
                assert( red( near ) > blue( near ) * 2 );
                assert( green( near ) > blue( near ) * 2 );
                break;

            case CDP_EMBER: // Cyan against ember red
                assert( blue( near ) > red( near ) * 2 );
                break;

            case CDP_MATRIX: // Orchid against matrix green
                assert( red( near ) > green( near ) * 2 );
                break;

            case CDP_OCEANIC: // Coral against ocean teal
                assert( red( near ) > blue( near ) * 1.5 );
                break;

            case CDP_MONOCHROME: // Remain truly neutral
                assert( red( near ) == green( near ) &&
                    green( near ) == blue( near ) );

                break;

            default: break;
        }
    }
}

static void TestMultiHop()
{
    UniverseData_s data;

    for ( const std::uint64_t address : { 0x1000ull, 0x2000ull,
        0x3000ull, 0x4000ull, 0x5000ull, 0x6000ull, 0x7000ull,
        0x8000ull, 0x9000ull, 0xA000ull } )
        data.functions.push_back({ address, "function" });

    data.functions[ 7 ].kind = UNK_GLOBAL;
    data.references = {
        { 0x1000, 0x2000, 0x1010 }, // first-hop callee
        { 0x1000, 0x3000, 0x1020 }, // independent first-hop branch
        { 0x2000, 0x4000, 0x2010 }, // second hop
        { 0x4000, 0x5000, 0x4010 }, // third hop
        { 0x3000, 0x6000, 0x3010 }, // independent second hop
        { 0x6000, 0x7000, 0x6010 }, // independent third hop
        { 0x2000, 0x8000, 0x2020 }, // globals should be leaves
        { 0x8000, 0x7000, 0x8010 },
        { 0x9000, 0x1000, 0x9010 }, // caller direction
        { 0xA000, 0x9000, 0xA010 }  // caller's caller
    };

    const UniverseGraph_s old = Universe_FocusConnectionsFromData(
        data, 0x1000, UFM_CALLEES );

    const UniverseGraph_s one = Universe_FocusMultiHopFromData(
        data, 0x1000, UFM_CALLEES, 1 );

    assert( one.nodes.size() == old.nodes.size() );
    assert( one.links.size() == old.links.size() );

    for ( std::size_t i = 0; i < old.nodes.size(); ++i )
    {
        assert( one.nodes[ i ].address == old.nodes[ i ].address );
        assert( one.nodes[ i ].x == old.nodes[ i ].x );
        assert( one.nodes[ i ].y == old.nodes[ i ].y );
        assert( one.nodes[ i ].z == old.nodes[ i ].z );
    }

    const UniverseGraph_s two = Universe_FocusMultiHopFromData(
        data, 0x1000, UFM_CALLEES, 2 );

    const UniverseGraph_s three = Universe_FocusMultiHopFromData(
        data, 0x1000, UFM_CALLEES, 3 );

    assert( two.nodes.size() == 6 ); // center, 2 direct, 3 second-hop
    assert( three.nodes.size() == 8 ); // + 2 third-hop leaves
    assert( FindNode( two, 0x4000 )->focusDepth == 2 );
    assert( FindNode( three, 0x5000 )->focusDepth == 3 );
    assert( FindNode( three, 0x7000 )->focusDepth == 3 );
    assert( FindNode( three, 0x9000 ) == nullptr );

    for ( const UniverseNode_s& node : one.nodes )
    {
        const UniverseNode_s* const grown = FindNode( three, node.address );
        assert( grown && grown->x == node.x && grown->y == node.y &&
            grown->z == node.z );

        const std::size_t oldIndex = static_cast< std::size_t >(
            &node - one.nodes.data() );

        const std::size_t newIndex = static_cast< std::size_t >(
            grown - three.nodes.data() );

        assert( Universe_HeatColor( one, oldIndex, CDP_GHIDRA_VIOLET ) ==
            Universe_HeatColor( three, newIndex, CDP_GHIDRA_VIOLET ) );

        assert( Universe_NodeRadius( one, oldIndex ) ==
            Universe_NodeRadius( three, newIndex ) );
    }

    for ( const UniverseNode_s& node : two.nodes )
    {
        const UniverseNode_s* const grown = FindNode( three, node.address );
        assert( grown && grown->x == node.x && grown->y == node.y &&
            grown->z == node.z );
    }

    const UniverseNode_s* child = FindNode( three, 0x5000 );
    assert( child && child->focusParent < three.nodes.size() );
    assert( three.nodes[ child->focusParent ].address == 0x4000 );
    assert( Universe_CallSiteAt( three, 0x4000, 0x5000, 0 ) == 0x4010 );
    assert( FindNode( three, 0x8000 )->focusDepth == 2 );
    // Globals never expand even if the data contains a global->callee edge.
    assert( FindNode( two, 0x7000 ) == nullptr );

    const std::vector< std::uint64_t > collapsed = { 0x2000 };
    const UniverseGraph_s pruned = Universe_FocusMultiHopFromData(
        data, 0x1000, UFM_CALLEES, 3, nullptr, true, false, &collapsed );

    assert( FindNode( pruned, 0x2000 ) != nullptr );
    assert( FindNode( pruned, 0x4000 ) == nullptr );
    assert( FindNode( pruned, 0x5000 ) == nullptr );
    assert( FindNode( pruned, 0x6000 ) != nullptr );
    assert( FindNode( pruned, 0x7000 ) != nullptr );
    assert( FindNode( pruned, 0x6000 )->x == FindNode( three, 0x6000 )->x );
    assert( FindNode( pruned, 0x6000 )->z == FindNode( three, 0x6000 )->z );

    const UniverseGraph_s incoming = Universe_FocusMultiHopFromData(
        data, 0x1000, UFM_CALLERS, 3 );

    assert( incoming.nodes.size() == 3 );
    assert( FindNode( incoming, 0x9000 )->focusDepth == 1 );
    assert( FindNode( incoming, 0xA000 )->focusDepth == 2 );
    assert( FindNode( incoming, 0x2000 ) == nullptr );
    assert( Universe_CallSiteAt( incoming, 0xA000, 0x9000, 0 ) == 0xA010 );

    const UniverseGraph_s both = Universe_FocusMultiHopFromData(
        data, 0x1000, UFM_BOTH, 3 );

    assert( FindNode( both, 0xA000 )->focusDepth == 2 );
    assert( FindNode( both, 0x5000 ) != nullptr );
    // Range applies to each call instruction at every depth.
    const std::vector< UniverseAddressRange_s > range = { { 0x1000, 0x3100 } };
    const UniverseGraph_s local = Universe_FocusMultiHopFromData(
        data, 0x1000, UFM_CALLEES, 3, &range );

    assert( FindNode( local, 0x4000 ) != nullptr );
    assert( FindNode( local, 0x5000 ) == nullptr );
    assert( FindNode( local, 0x7000 ) == nullptr );
    // Depth 0 is safely interpreted as the unchanged one-hop graph.
    assert( Universe_FocusMultiHopFromData( data, 0x1000, UFM_CALLEES, 0 )
        .nodes.size() == one.nodes.size() );
    // Graph generation must not depend on unordered_map/reference order.
    std::reverse( data.references.begin(), data.references.end() );

    const UniverseGraph_s reversed = Universe_FocusMultiHopFromData(
        data, 0x1000, UFM_CALLEES, 3 );

    for ( const UniverseNode_s& node : three.nodes )
    {
        const UniverseNode_s* const other = FindNode( reversed, node.address );
        assert( other && other->x == node.x && other->y == node.y &&
            other->z == node.z && other->focusDepth == node.focusDepth );
    }
    // A rendered third-hop callee has a dimmer endpoint than the direct
    // neon-yellow callee, while preserving the existing color family.
    const std::uint32_t direct = Universe_HeatColor( three,
        static_cast< std::size_t >( FindNode( three, 0x2000 ) -
            three.nodes.data() ), CDP_GHIDRA_VIOLET );

    const std::uint32_t distant = Universe_HeatColor( three,
        static_cast< std::size_t >( child - three.nodes.data() ),
        CDP_GHIDRA_VIOLET );

    assert( direct == 0xFFEFFF24u );
    assert( ( ( distant >> 8 ) & 255u ) < ( ( direct >> 8 ) & 255u ) );
}

static void TestMultiHopLimits()
{
    UniverseData_s data;
    data.functions.push_back({ 0x1000, "center" });

    for ( std::uint32_t first = 0; first < 100; ++first )
    {
        const std::uint64_t parent = 0x10000 + first * 0x100;
        data.functions.push_back({ parent, "parent" });
        data.references.push_back({ 0x1000, parent, 0x1010 + first });

        for ( std::uint32_t child = 0; child < 170; ++child )
        {
            const std::uint64_t address =
                0x100000 + first * 0x10000 + child * 0x100;

            data.functions.push_back({ address, "child" });
            data.references.push_back({ parent, address, parent + child });
        }
    }

    const UniverseGraph_s graph = Universe_FocusMultiHopFromData(
        data, 0x1000, UFM_CALLEES, 3 );

    // The 17,101-node tree exceeds both the new default and maximum
    // exploration capacities, without hitting the per-parent fanout limit.
    assert( graph.nodes.size() ==
        CANTORDUST_UNIVERSE_HOP_DEFAULT_FUNCTIONS );
    assert( graph.nodes.size() > CANTORDUST_UNIVERSE_MAX_FUNCTIONS );
    assert( graph.links.size() <= CANTORDUST_UNIVERSE_HOP_DEFAULT_LINKS );
    assert( graph.truncated && graph.focusNodeLimitReached );
    assert( !graph.focusFanoutLimitReached );

    const UniverseGraph_s expanded = Universe_FocusMultiHopFromData(
        data, 0x1000, UFM_CALLEES, 3, nullptr, true, false, nullptr,
        { CANTORDUST_UNIVERSE_HOP_MAX_FUNCTIONS,
          CANTORDUST_UNIVERSE_HOP_MAX_LINKS } );

    assert( expanded.nodes.size() ==
        CANTORDUST_UNIVERSE_HOP_MAX_FUNCTIONS );
    assert( expanded.focusNodeLimitReached && !expanded.focusFanoutLimitReached );
    // No dangling endpoints, duplicate node IDs or duplicate directed edges.
    std::unordered_set< std::uint64_t > addresses;
    std::unordered_set< std::uint64_t > links;

    for ( const UniverseNode_s& node : graph.nodes )
        assert( addresses.insert( node.address ).second );

    for ( const UniverseLink_s& link : graph.links )
    {
        assert( link.caller < graph.nodes.size() );
        assert( link.callee < graph.nodes.size() );
        assert( links.insert( ( std::uint64_t( link.caller ) << 32 ) |
            link.callee ).second );
    }
}

static void TestDeepExplorationBudgets()
{
    UniverseData_s chain;

    for ( std::uint64_t i = 0; i <= 12; ++i )
    {
        const std::uint64_t address = 0x1000 + i * 0x100;
        chain.functions.push_back({ address, "chain" });

        if ( i )
            chain.references.push_back({
            address - 0x100, address, address - 0x100 + 0x10 });
    }

    const auto explore = [&]( const std::uint8_t hops,
        const UniverseExploreLimits_s limits = {} ) {
        return Universe_FocusMultiHopFromData( chain, 0x1000,
            UFM_CALLEES, hops, nullptr, true, false, nullptr, limits );
    };

    const UniverseGraph_s one = explore( 1 );
    const UniverseGraph_s ten = explore( 10 );
    assert( one.nodes.size() == 2 );
    assert( ten.nodes.size() == 11 && ten.links.size() == 10 );
    assert( ten.focusDepthReached == 10 && ten.focusDepthRequested == 10 );
    assert( !ten.truncated && !ten.focusNodeLimitReached &&
        !ten.focusLinkLimitReached && !ten.focusFanoutLimitReached );

    assert( FindNode( ten, 0x1A00 )->focusDepth == 10 );
    assert( FindNode( ten, 0x1B00 ) == nullptr );
    // Newly supported deeper shells dim gradually but remain visible.
    const auto greenAt = [&]( const std::uint64_t address ) {
        const UniverseNode_s* node = FindNode( ten, address );
        assert( node != nullptr );
        return ( Universe_HeatColor( ten,
            static_cast< std::size_t >( node - ten.nodes.data() ),
            CDP_GHIDRA_VIOLET ) >> 8 ) & 255u;
    };
    assert( greenAt( 0x1200 ) > greenAt( 0x1300 ) );
    assert( greenAt( 0x1300 ) > greenAt( 0x1600 ) );
    assert( greenAt( 0x1600 ) > greenAt( 0x1A00 ) );
    assert( greenAt( 0x1A00 ) > 50u );
    // Verify the tenth shell remains on screen and pickable with the UI's
    // automatic high-depth framing, not merely present in the data model.
    Image_s deepImage;
    CubeCamera_s camera;
    UniverseRenderStyle_s style;
    style.focusScale = 1.0f / ( 1.0f + 0.68f * 9.0f );
    Universe_RenderInto( deepImage, ten, camera, CANTORDUST_NO_SOURCE,
        true, 1024, 768, CDP_GHIDRA_VIOLET, {}, style );

    const UniverseNode_s* last = FindNode( ten, 0x1A00 );
    assert( last != nullptr );

    const std::size_t lastIndex = static_cast< std::size_t >(
        last - ten.nodes.data() );

    assert( std::find( deepImage.sourceIndex.begin(),
        deepImage.sourceIndex.end(), lastIndex ) !=
        deepImage.sourceIndex.end() );

    const UniverseGraph_s oversized = explore( 255 );
    assert( oversized.nodes.size() == ten.nodes.size() );
    assert( oversized.focusDepthRequested == 10 );

    for ( std::uint8_t hops = 2; hops <= 10; ++hops )
    {
        const UniverseGraph_s current = explore( hops );
        assert( current.nodes.size() == hops + 1 );
        assert( current.focusDepthReached == hops );

        for ( const UniverseNode_s& node : current.nodes )
        {
            const UniverseNode_s* const deep = FindNode( ten, node.address );
            assert( deep != nullptr && deep->focusDepth == node.focusDepth );
            assert( deep->x == node.x && deep->y == node.y && deep->z == node.z );
        }
    }

    const UniverseGraph_s earlyEnd = explore( 10,
        { 100, 100 } );

    assert( earlyEnd.nodes.size() == 11 && !earlyEnd.truncated );

    const UniverseGraph_s nodeLimited = explore( 10, { 5, 100 } );
    assert( nodeLimited.nodes.size() == 5 && nodeLimited.links.size() == 4 );
    assert( nodeLimited.focusDepthReached == 4 );
    assert( nodeLimited.focusNodeLimitReached && !nodeLimited.focusLinkLimitReached );
    assert( nodeLimited.truncated );

    const UniverseGraph_s linkLimited = explore( 10, { 100, 3 } );
    assert( linkLimited.nodes.size() == 4 && linkLimited.links.size() == 3 );
    assert( linkLimited.focusDepthReached == 3 );
    assert( linkLimited.focusLinkLimitReached && !linkLimited.focusNodeLimitReached );
    assert( linkLimited.truncated );
    // A too-small budget cannot remove v51 direct neighbors or their links.
    const UniverseGraph_s tiny = explore( 10, { 1, 0 } );
    assert( tiny.nodes.size() == one.nodes.size() );
    assert( tiny.links.size() == one.links.size() );
    assert( tiny.focusDepthReached == 1 );
    assert( tiny.focusNodeLimitReached && tiny.focusLinkLimitReached );
    assert( tiny.truncated );

    // Terminating naturally short of the requested depth is not truncation.
    chain.functions.resize( 4 );
    chain.references.resize( 3 );

    const UniverseGraph_s exhausted = explore( 10 );
    assert( exhausted.focusDepthReached == 3 &&
        exhausted.focusDepthRequested == 10 && !exhausted.truncated );

    // A branch fanout safeguard is reported separately from user budgets.
    UniverseData_s fanout;
    fanout.functions.push_back({ 0x1000, "root" });
    fanout.functions.push_back({ 0x2000, "branch" });
    fanout.references.push_back({ 0x1000, 0x2000, 0x1010 });

    for ( std::uint64_t i = 0; i < CANTORDUST_UNIVERSE_HOP_MAX_FANOUT + 1u; ++i )
    {
        const std::uint64_t address = 0x3000 + i * 0x100;
        fanout.functions.push_back({ address, "leaf" });
        fanout.references.push_back({ 0x2000, address, 0x2010 + i });
    }

    const UniverseGraph_s cappedFanout = Universe_FocusMultiHopFromData(
        fanout, 0x1000, UFM_CALLEES, 10, nullptr, true, false,
        nullptr, { 4096, 4096 } );

    assert( cappedFanout.nodes.size() ==
        CANTORDUST_UNIVERSE_HOP_MAX_FANOUT + 2 );
    assert( cappedFanout.focusDepthReached == 2 );
    assert( cappedFanout.focusFanoutLimitReached &&
        !cappedFanout.focusNodeLimitReached &&
        !cappedFanout.focusLinkLimitReached );

    // Exactly 1024 new children is permitted without a fanout warning.
    fanout.functions.resize( CANTORDUST_UNIVERSE_HOP_MAX_FANOUT + 2 );
    fanout.references.resize( CANTORDUST_UNIVERSE_HOP_MAX_FANOUT + 1 );
    const UniverseGraph_s exactFanout = Universe_FocusMultiHopFromData(
        fanout, 0x1000, UFM_CALLEES, 10, nullptr, true, false,
        nullptr, { 4096, 4096 } );
    assert( exactFanout.nodes.size() ==
        CANTORDUST_UNIVERSE_HOP_MAX_FANOUT + 2 );
    assert( !exactFanout.truncated && !exactFanout.focusFanoutLimitReached );
}

static void TestHighLinkBudget()
{
    // Dense connections among already-discovered second-hop functions must
    // not be clipped by the ordinary 20,000-link tier limit.
    UniverseData_s data;
    data.functions.push_back({ 0x1000, "root" });
    data.functions.push_back({ 0x2000, "hub" });
    data.references.push_back({ 0x1000, 0x2000, 0x1010 });

    constexpr std::uint64_t leaves = 200;

    for ( std::uint64_t i = 0; i < leaves; ++i )
    {
        const std::uint64_t address = 0x100000 + i * 0x100;
        data.functions.push_back({ address, "leaf" });
        data.references.push_back({ 0x2000, address, 0x2010 + i });
    }

    for ( std::uint64_t i = 0; i < leaves; ++i )
    {
        for ( std::uint64_t step = 1; step <= 190; ++step )
        {
            const std::uint64_t caller = 0x100000 + i * 0x100;
            const std::uint64_t callee =
                0x100000 + ( ( i + step ) % leaves ) * 0x100;

            data.references.push_back({ caller, callee, caller + step });
        }
    }

    const UniverseGraph_s standard = Universe_FocusMultiHopFromData(
        data, 0x1000, UFM_CALLEES, 3 );

    assert( standard.nodes.size() == leaves + 2 );
    assert( standard.links.size() ==
        CANTORDUST_UNIVERSE_HOP_DEFAULT_LINKS );
    assert( standard.focusLinkLimitReached && standard.truncated );
    assert( !standard.focusFanoutLimitReached &&
        !standard.focusNodeLimitReached );

    const UniverseGraph_s maximum = Universe_FocusMultiHopFromData(
        data, 0x1000, UFM_CALLEES, 3, nullptr, true, false, nullptr,
        { CANTORDUST_UNIVERSE_HOP_MAX_FUNCTIONS,
          CANTORDUST_UNIVERSE_HOP_MAX_LINKS } );

    assert( maximum.links.size() == 1 + leaves + leaves * 190 );
    assert( maximum.links.size() > CANTORDUST_UNIVERSE_MAX_LINKS );
    assert( !maximum.truncated && !maximum.focusLinkLimitReached );
}

static void TestAlternativePaths()
{
    UniverseData_s data;

    for ( std::uint64_t i = 1; i <= 7; ++i )
        data.functions.push_back({ i * 0x1000, "function" });

    const auto edge = [&data]( std::uint64_t a, std::uint64_t b ) {
        data.references.push_back({ a * 0x1000, b * 0x1000,
            a * 0x1000 + b });
    };
    edge( 1, 2 );
    edge( 2, 7 );
    edge( 1, 3 );
    edge( 3, 7 );
    edge( 1, 4 );
    edge( 4, 5 );
    edge( 5, 7 );
    edge( 2, 3 );
    edge( 3, 2 ); // cycles aren't valid routes
    edge( 1, 2 ); // duplicate callsite must not form another route
    const std::vector< UniverseCallPath_s > routes = Universe_FindCallPaths( data, 0x1000, 0x7000 );
    assert( routes.size() == 5 );
    assert( routes.front().addresses ==
        std::vector< std::uint64_t >( { 0x1000, 0x2000, 0x7000 } ) );

    std::set< std::vector< std::uint64_t > > seen;

    for ( const UniverseCallPath_s& route : routes )
    {
        assert( seen.insert( route.addresses ).second );
        assert( route.callSites.size() + 1 == route.addresses.size() );
        std::set< std::uint64_t > unique(
            route.addresses.begin(), route.addresses.end() );

        assert( unique.size() == route.addresses.size() );
    }

    const UniverseGraph_s graph = Universe_BuildMultiPathGraph( data, routes, 2 );
    assert( graph.pathMode && graph.multiPathMode );
    assert( graph.activePathRoute == 2 );
    assert( graph.nodes.size() <= data.functions.size() );
    assert( graph.links.size() >= 5 );
    assert( Universe_FindCallPaths( data, 0x7000, 0x1000 ).empty() );
    assert( Universe_FindCallPaths( data, 0x1000, 0x7000, 0 ).empty() );
    assert( Universe_FindCallPaths( data, 0x1000, 0x7000, 1 ).size() == 1 );

    const UniverseGraph_s second = Universe_BuildMultiPathGraph( data, routes, 4 );
    assert( second.nodes.size() == graph.nodes.size() );

    for ( std::size_t i = 0; i < graph.nodes.size(); ++i )
    {
        assert( graph.nodes[ i ].x == second.nodes[ i ].x );
        assert( graph.nodes[ i ].y == second.nodes[ i ].y );
        assert( graph.nodes[ i ].z == second.nodes[ i ].z );
    }
}

static void TestPaths()
{
    const UniverseData_s data = Fixture();
    const UniverseCallPath_s path = Universe_FindCallPath(
        data, 0x1000, 0x3000 );

    assert( ( path.addresses == std::vector< std::uint64_t >{
        0x1000, 0x2000, 0x3000 } ) );

    assert( ( path.callSites == std::vector< std::uint64_t >{
        0x1010, 0x2010 } ) );

    const UniverseGraph_s graph = Universe_BuildPathGraph( data, path );
    assert( graph.pathMode );
    assert( graph.nodes.size() == 3 && graph.links.size() == 2 );
    assert( graph.links[ 0 ].caller == 0 && graph.links[ 0 ].callee == 1 );
    assert( Universe_CallSiteAt( graph, 0x2000, 0x3000, 0 ) == 0x2010 );
    Image_s image;
    CubeCamera_s camera;
    Universe_RenderInto( image, graph, camera, CANTORDUST_NO_SOURCE,
        true, 950, 550, CDP_GHIDRA_VIOLET );

    std::unordered_set< std::size_t > visible;

    for ( const std::size_t picked : image.sourceIndex )
        if ( picked != CANTORDUST_NO_SOURCE )
            visible.insert( picked );
    // The isolated path must stay pickable, even if it spans tiers.
    assert( visible.size() == graph.nodes.size() );
    // There is no reverse call path from the sensitive routine.
    assert( Universe_FindCallPath( data, 0x3000, 0x1000 ).addresses.empty() );
    // Data globals cannot masquerade as executable call intermediates.
    assert( Universe_FindCallPath( data, 0x5000, 0x3000 ).addresses.empty() );

    const UniverseCallPath_s self = Universe_FindCallPath(
        data, 0x2000, 0x2000 );

    assert( self.addresses.size() == 1 && self.callSites.empty() );
    assert( Universe_BuildPathGraph( data, self ).nodes.size() == 1 );
    UniverseCallPath_s malformed = path;
    malformed.callSites.clear();
    assert( Universe_BuildPathGraph( data, malformed ).nodes.empty() );
}

static void TestPathEndpointSelection()
{
    constexpr std::uint64_t cursor = 0x140012345u;
    constexpr std::uint64_t search = 0x180045678u;
    const std::optional< UniversePathEndpoint_s > idaChoice = Universe_ChoosePathEndpoint( cursor, search, false );
    assert( idaChoice.has_value() && idaChoice->address == cursor );
    assert( !idaChoice->fromSearch );

    const std::optional< UniversePathEndpoint_s > searchChoice = Universe_ChoosePathEndpoint( cursor, search, true );
    assert( searchChoice.has_value() && searchChoice->address == search );
    assert( searchChoice->fromSearch );

    const std::optional< UniversePathEndpoint_s > idaFallback = Universe_ChoosePathEndpoint(
        cursor, std::nullopt, true );

    assert( idaFallback.has_value() && idaFallback->address == cursor );
    assert( !idaFallback->fromSearch );

    const std::optional< UniversePathEndpoint_s > searchFallback = Universe_ChoosePathEndpoint(
        std::nullopt, search, false );

    assert( searchFallback.has_value() && searchFallback->address == search );
    assert( searchFallback->fromSearch );
    assert( !Universe_ChoosePathEndpoint(
        std::nullopt, std::nullopt, true ).has_value() );
}

static void TestPathEndpointOrder()
{
    constexpr std::uint64_t source = 0x1000u;
    constexpr std::uint64_t destination = 0x3000u;
    UniversePathEndpoints_s selection;
    using State = UniversePathSelectionState_e;

    // Start first, then end.
    assert( selection.Status() == State::Waiting );
    assert( selection.SetStart( source ) == State::Waiting );
    assert( selection.start == source && !selection.end.has_value() );
    assert( selection.SetEnd( destination ) == State::Ready );
    assert( selection.start == source && selection.end == destination );
    assert( !Universe_FindCallPath( Fixture(),
        *selection.start, *selection.end ).addresses.empty() );

    selection.Clear();
    assert( selection.Status() == State::Waiting );
    assert( !selection.start.has_value() && !selection.end.has_value() );

    // End first, then start, with the same directed route.
    assert( selection.SetEnd( destination ) == State::Waiting );
    assert( selection.end == destination && !selection.start.has_value() );
    assert( selection.SetStart( source ) == State::Ready );
    assert( selection.start == source && selection.end == destination );
    assert( !Universe_FindCallPath( Fixture(),
        *selection.start, *selection.end ).addresses.empty() );

    // Replacing either endpoint keeps the opposite endpoint intact.
    assert( selection.SetEnd( source ) == State::SameFunction );
    assert( selection.SetStart( 0x2000u ) == State::Ready );
    assert( selection.start == 0x2000u && selection.end == source );
    assert( selection.SetStart( source ) == State::SameFunction );
    assert( selection.SetEnd( destination ) == State::Ready );
    assert( selection.start == source && selection.end == destination );

    selection.Clear();
    assert( selection.SetEnd( destination ) == State::Waiting );
    assert( selection.SetEnd( source ) == State::Waiting );
    assert( selection.SetStart( source ) == State::SameFunction );
}

static void TestSearch()
{
    const UniverseData_s data = Fixture();
    const std::vector< UniverseSearchHit_s > results = Universe_SearchFunctions( data, "PACKET" );
    assert( results.size() == 1 && results.front().address == 0x2000 );
    assert( results.front().incoming == 2 );
    assert( Universe_SearchFunctions( data, "0x3000" ).front().address ==
        0x3000 );

    assert( Universe_SearchFunctions( data, "2000", 3 ).empty() );
    assert( Universe_SearchFunctions( data, "", 0, 2 ).size() == 2 );

    const UniverseGraph_s graph = Universe_BuildGraph(
        data.functions, data.references );

    const UniverseGraph_s filtered = Universe_FilterGraphSearch(
        graph, "routine" );

    assert( filtered.nodes.size() == 1 );
    assert( filtered.nodes[ 0 ].address == 0x3000 );
    assert( filtered.links.empty() );

    const UniverseGraph_s empty = Universe_FilterGraphSearch(
        graph, "no such function" );

    assert( empty.nodes.empty() );
}

int main()
{
    TestDirectedFocus();
    TestFocusSphereAndPalette();
    TestFocusedLinkFade();
    TestMultiHop();
    TestMultiHopLimits();
    TestDeepExplorationBudgets();
    TestHighLinkBudget();
    TestPaths();
    TestAlternativePaths();
    TestPathEndpointSelection();
    TestPathEndpointOrder();
    TestSearch();
    std::cout << "Universe explorer regressions passed\n";
}
