#include "cantordust/universe.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace cantordust;

static bool Contains( const std::string& text, const std::string& value )
{
    return text.find( value ) != std::string::npos;
}

int main()
{
    UniverseGraph_s graph;
    graph.pathMode = true;
    graph.multiPathMode = true;
    graph.activePathRoute = 1;
    UniverseNode_s start;
    start.address = 0x140000123ull;
    start.name = "start<&\"\\call";
    start.x = -0.25f;
    start.pathMask = 3u;
    UniverseNode_s end;
    end.address = 0x140001234ull;
    end.name = "target";
    end.x = 0.25f;
    end.pathMask = 2u;
    graph.nodes = { start, end };
    UniverseLink_s edge;
    edge.caller = 0;
    edge.callee = 1;
    edge.count = 3;
    edge.callSites = { 0x140000222ull, 0x140000333ull };
    edge.pathMask = 2;
    graph.links = { edge };

    const std::string dot = Universe_ExportDot( graph );
    assert( Contains( dot, "digraph \"Cantor Dust\"" ) );
    assert( Contains( dot, "n0 -> n1" ) );
    assert( Contains( dot, "count=3" ) );
    assert( Contains( dot, "0x140000222,0x140000333" ) );
    assert( Contains( dot, "0x140000123" ) );
    assert( Contains( dot, "path_mask=2" ) );
    assert( Contains( dot, "start<&\\\"\\\\call" ) );
    assert( Contains( dot, "color=\"#FFC857\"" ) );

    const std::string svg = Universe_ExportSvg( graph, {}, {}, {},
        CDP_REFERENCE, true, CANTORDUST_NO_SOURCE, 1920, 1080 );

    assert( Contains( svg, "<svg xmlns=" ) );
    assert( Contains( svg, "width=\"1920\"" ) );
    assert( Contains( svg, "start&lt;&amp;&quot;\\call" ) );
    assert( Contains( svg, "<circle" ) );
    assert( Contains( svg, "<path" ) );
    assert( Contains( svg, "#FFC857" ) );
    assert( !Contains( svg, "data:image" ) );
    assert( Universe_ExportSvg( graph, {}, {}, {}, CDP_REFERENCE,
        true, CANTORDUST_NO_SOURCE, 10, 1080 ).empty() );

    assert( Universe_ExportSvg( graph, {}, {}, {}, CDP_REFERENCE,
        true, CANTORDUST_NO_SOURCE, 9000, 1080 ).empty() );
    // Bad edge indices must never leak invalid edges into either format.
    graph.links[ 0 ].callee = 99;
    assert( !Contains( Universe_ExportDot( graph ), " -> " ) );
    assert( !Contains( Universe_ExportSvg( graph, {}, {}, {},
        CDP_REFERENCE, true, CANTORDUST_NO_SOURCE, 1024, 768 ),
        "(3 calls)" ) );

    std::cout << "Graph export tests passed\n";
}
