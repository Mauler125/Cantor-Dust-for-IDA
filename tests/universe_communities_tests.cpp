#include "cantordust/universe.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <set>
#include <vector>

using namespace cantordust;

static UniverseGraph_s TwoGroups()
{
    UniverseGraph_s graph;

    for ( std::uint32_t i = 0; i < 7; ++i )
    {
        UniverseNode_s node;
        node.address = 0x1000 + i * 0x100;
        node.name = "f" + std::to_string( i );
        node.incoming = 7 + i;
        node.x = static_cast< float >( i ) * 0.2f;
        graph.nodes.push_back( node );
    }
    // Dense triangles connected by a weak bridge; the last node is isolated.
    for ( const auto edge : {
        std::pair< int, int >{ 0, 1 }, { 1, 2 }, { 0, 2 },
        { 3, 4 }, { 4, 5 }, { 3, 5 }, { 2, 3 } } )
    {
        UniverseLink_s link;
        link.caller = edge.first;
        link.callee = edge.second;
        link.count = edge.first == 2 && edge.second == 3 ? 1u : 20u;
        graph.links.push_back( link );
    }

    return graph;
}

int main()
{
    const UniverseGraph_s original = TwoGroups();
    const UniverseCommunityResult_s result = Universe_BuildCommunities( original );
    assert( result.graph.communityMode );
    assert( result.graph.nodes.size() == 3 );
    assert( result.graph.links.size() == 1 );
    std::size_t covered = 0;
    std::set< std::uint64_t > addresses;

    for ( const UniverseNode_s& node : result.graph.nodes )
    {
        const auto it = result.members.find( node.address );
        assert( it != result.members.end() );
        assert( it->second.size() == node.communitySize );
        covered += it->second.size();

        for ( const std::uint64_t address : it->second )
            assert( addresses.insert( address ).second );

        if ( node.communitySize == 3 )
            assert( node.kind == UNK_COMMUNITY );
    }

    assert( covered == original.nodes.size() );

    const UniverseLink_s& bridge = result.graph.links.front();
    assert( bridge.count == 1 );
    assert( bridge.caller != bridge.callee );
    UniverseGraph_s reordered = original;
    std::reverse( reordered.links.begin(), reordered.links.end() );

    const UniverseCommunityResult_s again = Universe_BuildCommunities( reordered );
    assert( result.members == again.members );
    assert( result.graph.links.size() == again.graph.links.size() );
    UniverseGraph_s single;
    single.nodes.push_back(original.nodes.front());
    assert( Universe_BuildCommunities(single).graph.nodes.size() == 1 );
    assert( Universe_BuildCommunities({}).graph.nodes.empty() );
    std::cout << "community tests passed\n";
}
