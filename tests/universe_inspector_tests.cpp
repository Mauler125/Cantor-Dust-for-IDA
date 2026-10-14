#include "cantordust/universe.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace cantordust;

int main()
{
    UniverseData_s data;
    data.functions = {
        { 0x1000, "receiver", UNK_FUNCTION },
        { 0x2000, "parser", UNK_FUNCTION },
        { 0x3000, "logger", UNK_FUNCTION },
        { 0x4000, "g_state", UNK_GLOBAL }
    };
    data.references = {
        { 0x1000, 0x2000, 0x1011 },
        { 0x1000, 0x2000, 0x1010 },
        { 0x2000, 0x1000, 0x2010 },
        { 0x3000, 0x1000, 0x3010 },
        { 0x1000, 0x4000, 0x1020, 0x4004 },
        { 0x2000, 0x2000, 0x2020 },
        // Unknown endpoints must not become imaginary inspector records.
        { 0x9999, 0x1000, 0x9910 }
    };

    const UniverseInspectorIndex_s index = Universe_BuildInspectorIndex( data );
    assert( index.nodes.size() == 4 );
    assert( index.nodes.count( 0x9999 ) == 0 );

    const UniverseInspectorResult_s receiver = Universe_InspectFunction(
        data, index, 0x1000 );

    assert( receiver.found && receiver.name == "receiver" );
    assert( receiver.incomingReferences == 2 );
    assert( receiver.outgoingReferences == 3 );
    assert( receiver.connections.size() == 4 );
    // Incoming first, outgoing second, with reciprocal edges separate.
    assert( receiver.connections[ 0 ].incoming );
    assert( receiver.connections[ 0 ].peerAddress == 0x2000 );
    assert( receiver.connections[ 0 ].callSites[ 0 ] == 0x2010 );
    assert( receiver.connections[ 1 ].incoming );
    assert( receiver.connections[ 1 ].peerAddress == 0x3000 );
    assert( !receiver.connections[ 2 ].incoming );
    assert( receiver.connections[ 2 ].peerAddress == 0x2000 );
    assert( receiver.connections[ 2 ].callSites.size() == 2 );
    assert( receiver.connections[ 2 ].callSites[ 0 ] == 0x1010 );
    assert( receiver.connections[ 2 ].callSites[ 1 ] == 0x1011 );
    assert( !receiver.connections[ 3 ].incoming );
    assert( receiver.connections[ 3 ].peerKind == UNK_GLOBAL );
    assert( receiver.connections[ 3 ].callSites[ 0 ] == 0x1020 );

    const UniverseInspectorResult_s global = Universe_InspectFunction( data, index, 0x4000 );
    assert( global.kind == UNK_GLOBAL && global.incomingReferences == 1 );
    assert( global.outgoingReferences == 0 );

    const UniverseInspectorResult_s self = Universe_InspectFunction( data, index, 0x2000 );
    assert( self.incomingReferences == 3 );
    assert( self.outgoingReferences == 2 );
    // Self-calls occur once in each direction, not silently dropped.
    assert( self.connections.size() == 4 );
    assert( !Universe_InspectFunction( data, index, 0x8888 ).found );

    // The rendered-graph fallback must retain callsite directions for the
    // selected node if the full extraction index is unavailable or stale.
    const UniverseGraph_s graph = Universe_BuildGraph(
        data.functions, data.references, 10u, 100u );

    const UniverseInspectorResult_s visible = Universe_InspectGraphFunction( graph, 0x1000u );
    assert( visible.found && visible.name == "receiver" );
    assert( visible.incomingReferences == 2u );
    assert( visible.outgoingReferences == 3u );
    assert( visible.connections.size() == 4u );
    assert( visible.connections[ 0 ].incoming );
    assert( visible.connections[ 0 ].peerAddress == 0x2000u );
    assert( visible.connections[ 0 ].callSites.front() == 0x2010u );
    assert( visible.connections[ 2 ].peerAddress == 0x2000u );
    assert( visible.connections[ 2 ].callSites.size() == 2u );
    assert( visible.connections[ 2 ].callSites.front() == 0x1010u );
    assert( !Universe_InspectGraphFunction( graph, 0x8888u ).found );
    std::cout << "inspector tests passed\n";
}
