#include "cantordust/universe.h"
#include "cantordust/scalar_types.h"
#include "cantordust/constants.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cantordust
{

using std::sin;
using std::cos;
using std::sqrt;

std::optional< UniversePathEndpoint_s > Universe_ChoosePathEndpoint(
    const std::optional< u64 > idaFunction,
    const std::optional< u64 > searchResult,
    const bool preferSearch )
{
    if ( preferSearch && searchResult.has_value() )
        return UniversePathEndpoint_s{ *searchResult, true };

    if ( idaFunction.has_value() )
        return UniversePathEndpoint_s{ *idaFunction, false };

    if ( searchResult.has_value() )
        return UniversePathEndpoint_s{ *searchResult, true };

    return std::nullopt;
}

namespace
{
using Address = u64;
struct Edge { Address to; Address site; };
using Adjacent = std::map< Address, std::vector< Edge > >;

// Deterministic shortest directed spur, with a banned root and root edge.
UniverseCallPath_s ShortestSpur( const Adjacent& adjacent, Address start,
    Address finish, const std::set< Address >& blockedNodes,
    const std::set< std::pair< Address, Address > >& blockedEdges )
{
    UniverseCallPath_s result;

    if ( blockedNodes.count( start ) || blockedNodes.count( finish ) )
        return result;

    std::map< Address, Edge > parent;
    std::queue< Address > pending;
    parent.emplace( start, Edge{ start, 0 } );
    pending.push( start );

    while ( !pending.empty() && !parent.count( finish ) )
    {
        const Address current = pending.front();
        pending.pop();

        const auto it = adjacent.find( current );

        if ( it == adjacent.end() )
            continue;

        for ( const Edge edge : it->second )
        {
            if ( blockedNodes.count( edge.to ) ||
                 blockedEdges.count({ current, edge.to }) ||
                 parent.count( edge.to ) )
            {
                continue;
            }

            parent.emplace( edge.to, Edge{ current, edge.site } );
            pending.push( edge.to );

            if ( edge.to == finish )
                break;
        }
    }

    if ( !parent.count( finish ) )
        return result;

    for ( Address at = finish; at != start; at = parent.at( at ).to )
    {
        result.addresses.push_back( at );
        result.callSites.push_back( parent.at( at ).site );
    }

    result.addresses.push_back( start );
    std::reverse( result.addresses.begin(), result.addresses.end() );
    std::reverse( result.callSites.begin(), result.callSites.end() );
    return result;
}

// A path's identity is its function sequence, not which of several callsites
// happened to be chosen for the same directed edge.
struct PathLess
{
    bool operator()( const UniverseCallPath_s& a,
        const UniverseCallPath_s& b ) const
    {
        if ( a.addresses.size() != b.addresses.size() )
            return a.addresses.size() < b.addresses.size();

        return a.addresses < b.addresses;
    }
};
}

std::vector< UniverseCallPath_s > Universe_FindCallPaths(
    const UniverseData_s& data, const u64 from,
    const u64 to, const usize requested )
{
    std::vector< UniverseCallPath_s > routes;

    const usize limit = std::min< usize >( requested, 5u );

    if ( limit == 0 )
        return routes;

    std::set< Address > valid;

    for ( const UniverseFunction_s& node : data.functions )
        if ( node.kind == UNK_FUNCTION )
            valid.insert( node.address );

    if ( !valid.count( from ) || !valid.count( to ) )
        return routes;

    Adjacent adjacent;

    for ( const UniverseReference_s& ref : data.references )
        if ( valid.count( ref.caller ) && valid.count( ref.callee ) &&
             ref.caller != ref.callee )
        {
            adjacent[ ref.caller ].push_back({ ref.callee, ref.callSite });
        }

    for ( auto& entry : adjacent )
    {
        std::vector< Edge >& edges = entry.second;
        std::sort( edges.begin(), edges.end(), []( const Edge& a, const Edge& b ) {
            return a.to != b.to ? a.to < b.to : a.site < b.site;
        } );
        edges.erase( std::unique( edges.begin(), edges.end(),
            []( const Edge& a, const Edge& b ) { return a.to == b.to; } ),
            edges.end() );
    }

    const UniverseCallPath_s first = ShortestSpur(
        adjacent, from, to, {}, {} );

    if ( first.addresses.empty() )
        return routes;

    routes.push_back( first );
    // Extremely long shortest chains are still navigable, but computing
    // every spur of a thousands-node chain would monopolize IDA's UI thread.
    constexpr usize maxAlternativeDepth = 64;

    if ( first.addresses.size() > maxAlternativeDepth )
        return routes;

    std::set< UniverseCallPath_s, PathLess > candidates;
    // Yen's K-shortest loopless paths. A small K bounds the number of BFS
    // spurs; no exponential enumeration of every route in the binary.
    for ( usize rank = 1; rank < limit; ++rank )
    {
        const UniverseCallPath_s& previous = routes.back();

        for ( usize spur = 0; spur + 1 < previous.addresses.size(); ++spur )
        {
            std::set< Address > blockedNodes(
                previous.addresses.begin(), previous.addresses.begin() + spur );

            std::set< std::pair< Address, Address > > blockedEdges;

            for ( const UniverseCallPath_s& chosen : routes )
            {
                if ( chosen.addresses.size() <= spur + 1 )
                    continue;

                if ( !std::equal( previous.addresses.begin(),
                        previous.addresses.begin() + spur + 1,
                        chosen.addresses.begin() ) )
                {
                    continue;
                }

                blockedEdges.insert({ chosen.addresses[ spur ],
                    chosen.addresses[ spur + 1 ] });
            }

            const UniverseCallPath_s tail = ShortestSpur( adjacent,
                previous.addresses[ spur ], to, blockedNodes, blockedEdges );

            if ( tail.addresses.empty() ||
                 spur + tail.addresses.size() > maxAlternativeDepth )
            {
                continue;
            }

            UniverseCallPath_s candidate;
            candidate.addresses.assign( previous.addresses.begin(),
                previous.addresses.begin() + spur );

            candidate.addresses.insert( candidate.addresses.end(),
                tail.addresses.begin(), tail.addresses.end() );

            candidate.callSites.assign( previous.callSites.begin(),
                previous.callSites.begin() + spur );

            candidate.callSites.insert( candidate.callSites.end(),
                tail.callSites.begin(), tail.callSites.end() );

            if ( std::none_of( routes.begin(), routes.end(),
                [&candidate]( const auto& existing ) {
                    return existing.addresses == candidate.addresses;
                } ) )
            {
                candidates.insert( std::move( candidate ) );
            }
        }

        if ( candidates.empty() )
            break;

        routes.push_back( *candidates.begin() );
        candidates.erase( candidates.begin() );
    }

    return routes;
}

UniverseGraph_s Universe_BuildMultiPathGraph( const UniverseData_s& data,
    const std::vector< UniverseCallPath_s >& routes, usize selectedRoute )
{
    UniverseGraph_s result;
    result.pathMode = true;
    result.multiPathMode = true;

    const usize count = std::min< usize >( routes.size(), 5u );

    if ( count == 0 )
        return result;

    result.activePathRoute = static_cast< u8 >(
        std::min( selectedRoute, count - 1u ) );

    std::map< Address, const UniverseFunction_s* > functions;

    for ( const UniverseFunction_s& f : data.functions )
        if ( f.kind == UNK_FUNCTION )
            functions.emplace( f.address, &f );

    std::map< Address, usize > nodes;
    std::map< std::pair< Address, Address >, usize > edges;
    std::map< Address, usize > level;

    for ( usize route = 0; route < count; ++route )
    {
        const UniverseCallPath_s& path = routes[ route ];

        if ( path.addresses.empty() ||
             path.callSites.size() + 1u != path.addresses.size() )
        {
            return {};
        }

        for ( usize j = 0; j < path.addresses.size(); ++j )
        {
            const Address address = path.addresses[ j ];

            if ( !functions.count( address ) )
                return {};

            if ( !nodes.count( address ) )
            {
                UniverseNode_s node;
                node.address = address;
                node.name = functions.at( address )->name;
                node.incoming = 1;
                nodes.emplace( address, result.nodes.size() );
                result.nodes.push_back( std::move( node ) );
            }

            result.nodes[ nodes.at( address ) ].pathMask |=
                static_cast< u8 >( 1u << route );

            if ( !level.count( address ) )
                level[ address ] = j;

            else
                level[ address ] = std::min( level.at( address ), j );

            if ( j == 0 )
                continue;

            const auto key = std::make_pair( path.addresses[ j - 1 ], address );

            if ( !edges.count( key ) )
            {
                UniverseLink_s link;
                link.caller = static_cast< u32 >( nodes.at( key.first ) );
                link.callee = static_cast< u32 >( nodes.at( key.second ) );
                link.count = 1;
                link.callSites.push_back( path.callSites[ j - 1 ] );
                edges[ key ] = result.links.size();
                result.links.push_back( std::move( link ) );
            }

            result.links[ edges.at( key ) ].pathMask |=
                static_cast< u8 >( 1u << route );
        }
    }

    std::map< usize, std::vector< usize > > layers;

    for ( const auto& entry : nodes )
        layers[ level.at( entry.first ) ].push_back( entry.second );

    const f32 maxLayer = static_cast< f32 >( std::max< usize >( layers.rbegin()->first, 1u ) );

    for ( auto& pair : layers )
    {
        std::vector< usize >& indices = pair.second;
        std::sort( indices.begin(), indices.end(), [&result]( usize a, usize b ) {
            return result.nodes[ a ].address < result.nodes[ b ].address;
        } );

        for ( usize j = 0; j < indices.size(); ++j )
        {
            const f32 angle = CANTORDUST_TAU_F * static_cast< f32 >( j ) /
                static_cast< f32 >( indices.size() );

            const f32 radius = indices.size() == 1 ? 0.0f : 0.23f +
                0.05f * sqrt( static_cast< f32 >( indices.size() ) );

            UniverseNode_s& node = result.nodes[ indices[ j ] ];
            node.x = ( static_cast< f32 >( pair.first ) - maxLayer * 0.5f ) * CANTORDUST_UNIVERSE_PATH_LAYER_SPACING;
            node.y = cos( angle ) * radius;
            node.z = sin( angle ) * radius;
        }
    }

    return result;
}

} // namespace cantordust
