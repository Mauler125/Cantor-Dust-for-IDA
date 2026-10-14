#include "cantordust/universe.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace cantordust;

static bool Contains( const UniverseGraph_s& graph, std::uint64_t address )
{
    return std::any_of( graph.nodes.begin(), graph.nodes.end(),
        [address]( const UniverseNode_s& node ) { return node.address == address; } );
}

static void VerifyEdgesAndPositions( const UniverseGraph_s& before,
    const UniverseGraph_s& after )
{
    for ( const UniverseLink_s& link : after.links )
        assert( link.caller < after.nodes.size() && link.callee < after.nodes.size() );

    for ( const UniverseNode_s& node : after.nodes )
    {
        const auto original = std::find_if( before.nodes.begin(), before.nodes.end(),
            [&]( const UniverseNode_s& n ) { return n.address == node.address; } );

        assert( original != before.nodes.end() );
        assert( node.x == original->x && node.y == original->y && node.z == original->z );
        assert( node.focusParent == CANTORDUST_NO_SOURCE ||
                node.focusParent < after.nodes.size() );
    }
}

int main()
{
    UniverseData_s data;
    data.functions = {
        { 0x1000, "root", UNK_FUNCTION, UFF_LIBRARY },
        { 0x2000, "caller", UNK_FUNCTION, 0 },
        { 0x3000, "import", UNK_FUNCTION, UFF_IMPORT },
        { 0x4000, "thunk", UNK_FUNCTION, UFF_THUNK },
        { 0x5000, "library", UNK_FUNCTION, UFF_LIBRARY },
        { 0x6000, "depth-two", UNK_FUNCTION, 0 },
        { 0x7000, "global", UNK_GLOBAL, 0 } };

    data.references = {
        { 0x2000, 0x1000, 0x2010 },
        { 0x2000, 0x1000, 0x2020 },
        { 0x1000, 0x3000, 0x1010 },
        { 0x1000, 0x4000, 0x1020 },
        { 0x5000, 0x1000, 0x5010 },
        { 0x3000, 0x6000, 0x3010 },
        { 0x1000, 0x7000, 0x1030 } };

    const UniverseGraph_s graph = Universe_BuildGraph(
        data.functions, data.references, 100, 100 );

    const UniverseGraph_s baseline = Universe_FilterGraphAdvanced( graph, data );
    assert( baseline.nodes.size() == graph.nodes.size() );
    assert( baseline.links.size() == graph.links.size() );

    UniverseFilterOptions_s options;
    options.hideImports = true;
    options.hideThunks = true;
    options.hideLibraries = true;
    options.hideGlobals = true;

    const UniverseGraph_s filtered = Universe_FilterGraphAdvanced( graph, data, options );
    assert( filtered.nodes.size() == 2 );
    assert( Contains( filtered, 0x2000 ) && Contains( filtered, 0x6000 ) );
    assert( filtered.links.empty() );
    VerifyEdgesAndPositions( graph, filtered );

    options = {};
    options.minimumIncoming = 2;

    const UniverseGraph_s incoming = Universe_FilterGraphAdvanced( graph, data, options );
    assert( Contains( incoming, 0x1000 ) );
    assert( !Contains( incoming, 0x2000 ) );
    assert( Contains( incoming, 0x7000 ) ); // globals exempt
    VerifyEdgesAndPositions( graph, incoming );

    options = {};
    options.minimumOutgoing = 2;

    const UniverseGraph_s outgoing = Universe_FilterGraphAdvanced( graph, data, options );
    assert( Contains( outgoing, 0x1000 ) && Contains( outgoing, 0x2000 ) );
    assert( !Contains( outgoing, 0x3000 ) );
    VerifyEdgesAndPositions( graph, outgoing );

    const UniverseGraph_s focus = Universe_FocusMultiHopFromData(
        data, 0x1000, UFM_BOTH, 3 );

    assert( focus.focusIndex < focus.nodes.size() );
    assert( Contains( focus, 0x3000 ) && Contains( focus, 0x6000 ) );
    options = {};
    options.hideImports = true;
    options.hideLibraries = true;

    const UniverseGraph_s focused = Universe_FilterGraphAdvanced( focus, data, options );
    assert( focused.focusIndex < focused.nodes.size() );
    assert( focused.nodes[ focused.focusIndex ].address == 0x1000 ); // always keep root
    assert( !Contains( focused, 0x3000 ) );
    assert( !Contains( focused, 0x6000 ) ); // no orphaned 2nd-hop neighbor
    VerifyEdgesAndPositions( focus, focused );

    const UniverseCallPath_s path = Universe_FindCallPath( data, 0x1000, 0x6000 );
    const UniverseGraph_s pathGraph = Universe_BuildPathGraph( data, path );
    assert( pathGraph.pathMode && pathGraph.nodes.size() == 3 );
    assert( Universe_FilterGraphAdvanced( pathGraph, data, options ).nodes.size() == 3 );
    return 0;
}
