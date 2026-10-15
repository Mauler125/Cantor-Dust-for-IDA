#include "pch/core_pch.h"
#include "cantordust/universe.h"
#include "cantordust/scalar_types.h"

#include "math/universe.h"

#include <cctype>
#include <cstdio>

namespace cantordust
{

using std::sin;
using std::cos;

static std::string Universe_Lower( std::string value )
{
    std::transform( value.begin(), value.end(), value.begin(),
        []( const unsigned char c ) {
            return static_cast< char >( std::tolower( c ) );
        } );

    return value;
}

static bool Universe_Matches( const std::string& name,
    const u64 address, const std::string& query )
{
    if ( query.empty() )
        return true;

    if ( Universe_Lower( name ).find( query ) != std::string::npos )
        return true;

    char buffer[ 32 ];
    std::snprintf( buffer, sizeof( buffer ), "0x%llx",
        static_cast< unsigned long long >( address ) );

    const std::string formatted( buffer );
    return formatted.find( query ) != std::string::npos ||
        formatted.substr( 2 ).find( query ) != std::string::npos;
}

std::vector< UniverseSearchHit_s > Universe_SearchFunctions(
    const UniverseData_s& data, const std::string& query,
    const u32 minimumIncoming, const usize limit )
{
    std::unordered_map< u64, u32 > counts;
    counts.reserve( data.functions.size() );

    for ( const UniverseReference_s& ref : data.references )
    {
        u32& count = counts[ ref.callee ];

        if ( count < UINT32_MAX )
            ++count;
    }

    std::vector< UniverseSearchHit_s > hits;

    const std::string needle = Universe_Lower( query );

    for ( const UniverseFunction_s& function : data.functions )
    {
        const u32 incoming = counts[ function.address ];

        if ( incoming < minimumIncoming ||
             !Universe_Matches( function.name, function.address, needle ) )
        {
            continue;
        }

        hits.push_back({ function.address, function.name, incoming });
    }

    std::sort( hits.begin(), hits.end(), []( const auto& a, const auto& b ) {
        return a.incoming != b.incoming ? a.incoming > b.incoming :
            a.address < b.address;
    } );

    if ( hits.size() > limit )
        hits.resize( limit );

    return hits;
}

UniverseGraph_s Universe_FilterGraphSearch( const UniverseGraph_s& graph,
    const std::string& query, const u32 minimumIncoming )
{
    const std::string needle = Universe_Lower( query );

    if ( needle.empty() && minimumIncoming == 0 )
        return graph;

    UniverseGraph_s result;
    result.candidateCount = graph.candidateCount;
    result.referenceCount = graph.referenceCount;
    result.absoluteHeatMaximum = graph.absoluteHeatMaximum;
    result.distinctCountMode = graph.distinctCountMode;
    result.globalFieldMode = graph.globalFieldMode;
    result.truncated = graph.truncated;
    result.adaptiveLayout = graph.adaptiveLayout;
    std::vector< usize > remap( graph.nodes.size(), CANTORDUST_NO_SOURCE );

    for ( usize i = 0; i < graph.nodes.size(); ++i )
    {
        const UniverseNode_s& node = graph.nodes[ i ];

        if ( node.incoming < minimumIncoming ||
             !Universe_Matches( node.name, node.address, needle ) )
        {
            continue;
        }

        remap[ i ] = result.nodes.size();
        result.nodes.push_back( node );
    }

    for ( const UniverseLink_s& link : graph.links )
    {
        if ( link.caller >= remap.size() || link.callee >= remap.size() ||
             remap[ link.caller ] == CANTORDUST_NO_SOURCE ||
             remap[ link.callee ] == CANTORDUST_NO_SOURCE )
        {
            continue;
        }

        UniverseLink_s copy = link;
        copy.caller = static_cast< u32 >( remap[ link.caller ] );
        copy.callee = static_cast< u32 >( remap[ link.callee ] );
        result.links.push_back( std::move( copy ) );
    }

    return result;
}

UniverseGraph_s Universe_FocusConnectionsFromData(
    const UniverseData_s& data, const u64 address,
    const UniverseFocusMode_e mode,
    const std::vector< UniverseAddressRange_s >* const ranges,
    const bool distinctCenter, const bool globalFields )
{
    // Preserve the previous caller-only behavior and its ranking semantics.
    if ( mode == UFM_CALLERS )
    {
        return ranges == nullptr
            ? Universe_FocusCallersFromData(
                data, address, distinctCenter, globalFields )
            : Universe_FocusCallersInRangesFromData(
                data, address, *ranges, distinctCenter, globalFields );
    }

    UniverseGraph_s result;
    result.candidateCount = data.functions.size();
    result.referenceCount = data.references.size();
    result.truncated = data.truncated;
    result.focusIndex = CANTORDUST_NO_SOURCE;
    std::unordered_map< u64, const UniverseFunction_s* > functions;
    functions.reserve( data.functions.size() );

    for ( const UniverseFunction_s& function : data.functions )
        functions.emplace( function.address, &function );

    const auto center = functions.find( address );

    if ( center == functions.end() )
        return result;

    result.focusIndex = 0;
    result.nodes.push_back({});
    result.nodes[ 0 ].address = address;
    result.nodes[ 0 ].name = center->second->name;
    result.nodes[ 0 ].kind = center->second->kind;
    struct Neighbor_s
    {
        u64 address = 0;
        u32 incoming = 0;
        u32 outgoing = 0;
        std::vector< u64 > incomingSites;
        std::vector< u64 > outgoingSites;
    };
    std::unordered_map< u64, Neighbor_s > neighbors;

    for ( const UniverseReference_s& ref : data.references )
    {
        if ( ref.caller == ref.callee )
            continue;

        const bool incoming = ref.callee == address;
        const bool outgoing = ref.caller == address;

        if ( !incoming && !outgoing )
            continue;
        // Ranges are defined by the call instruction address, not the
        // destination function's entry point.
        if ( ranges != nullptr && std::none_of( ranges->begin(), ranges->end(),
            [&ref]( const UniverseAddressRange_s& range ) {
                return ref.callSite >= range.start &&
                    ref.callSite < range.end;
            } ) )
        {
            continue;
        }

        if ( incoming && mode == UFM_CALLEES )
            continue;

        const u64 other = incoming ? ref.caller : ref.callee;

        if ( functions.find( other ) == functions.end() )
            continue;

        Neighbor_s& item = neighbors[ other ];
        item.address = other;

        if ( incoming )
        {
            ++item.incoming;
            item.incomingSites.push_back( ref.callSite );
        }
        else
        {
            ++item.outgoing;
            item.outgoingSites.push_back( ref.callSite );
        }
    }

    std::vector< Neighbor_s > sorted;
    sorted.reserve( neighbors.size() );

    for ( auto& pair : neighbors )
    {
        std::sort( pair.second.incomingSites.begin(),
            pair.second.incomingSites.end() );

        std::sort( pair.second.outgoingSites.begin(),
            pair.second.outgoingSites.end() );

        sorted.push_back( std::move( pair.second ) );
    }

    std::sort( sorted.begin(), sorted.end(), []( const auto& a, const auto& b ) {
        const u64 aCount = a.incoming + a.outgoing;
        const u64 bCount = b.incoming + b.outgoing;
        return aCount != bCount ? aCount > bCount : a.address < b.address;
    } );

    const usize keep = std::min< usize >( sorted.size(),
        std::min< usize >( CANTORDUST_UNIVERSE_MAX_FUNCTIONS - 1u,
            CANTORDUST_UNIVERSE_MAX_LINKS / 2u ) );

    result.truncated |= keep != sorted.size();
    result.nodes.reserve( keep + 1u );

    for ( usize i = 0; i < keep; ++i )
    {
        const Neighbor_s& item = sorted[ i ];
        const UniverseFunction_s* const function = functions.at( item.address );
        UniverseNode_s node;
        node.address = item.address;
        node.name = function->name;
        node.kind = function->kind;
        node.focusCalls = item.incoming + item.outgoing;
        node.focusDirection = static_cast< u8 >(
            ( item.incoming ? 1u : 0u ) | ( item.outgoing ? 2u : 0u ) );

        const u32 idx = static_cast< u32 >( i + 1u );
        result.nodes.push_back( std::move( node ) );

        if ( item.incoming )
        {
            UniverseLink_s link;
            link.caller = idx;
            link.callee = 0;
            link.count = item.incoming;
            link.callSites = item.incomingSites;
            result.links.push_back( std::move( link ) );
            result.nodes[ 0 ].incoming +=
                distinctCenter ? 1u : item.incoming;
        }

        if ( item.outgoing )
        {
            UniverseLink_s link;
            link.caller = 0;
            link.callee = idx;
            link.count = item.outgoing;
            link.callSites = item.outgoingSites;
            result.links.push_back( std::move( link ) );
            result.nodes[ 0 ].outgoing +=
                distinctCenter ? 1u : item.outgoing;
        }
    }
    // Reuse the v48 focused sphere for every neighbor. Direction affects
    // color/navigation only, never the spatial distribution of the nodes.
    u64 minGap = UINT64_MAX;
    u64 maxGap = 0;

    for ( usize i = 1; i < result.nodes.size(); ++i )
    {
        const u64 gap = Math_UniverseAddressGap(
            result.nodes[ i ].address, address );

        minGap = std::min( minGap, gap );
        maxGap = std::max( maxGap, gap );
    }

    for ( usize i = 1; i < result.nodes.size(); ++i )
    {
        UniverseNode_s& node = result.nodes[ i ];

        const f32 radius = Math_UniverseFocusDistance(
            Math_UniverseAddressGap( node.address, address ), minGap, maxGap );

        const UniversePosition_s position = Math_UniverseFocusPosition(
            i - 1u, keep, node.address, radius );

        node.x = position.x;
        node.y = position.y;
        node.z = position.z;
    }

    return result;
}

// Extend the existing first-hop graph rather than relaying it out. This is
// important: the user can change depth/collapse branches without moving any
// of the nodes they were already inspecting.
UniverseGraph_s Universe_FocusMultiHopFromData(
    const UniverseData_s& data, const u64 address,
    const UniverseFocusMode_e mode, const u8 maxDepth,
    const std::vector< UniverseAddressRange_s >* const ranges,
    const bool distinctCenter, const bool globalFields,
    const std::vector< u64 >* const collapsed,
    const UniverseExploreLimits_s limits )
{
    UniverseGraph_s result = Universe_FocusConnectionsFromData(
        data, address, mode, ranges, distinctCenter, globalFields );

    if ( result.focusIndex == CANTORDUST_NO_SOURCE )
        return result;

    const u8 depthLimit = std::clamp< u8 >(
        maxDepth, 1u, 10u );

    result.focusDepthRequested = depthLimit;
    result.focusDepthReached = result.nodes.size() > 1 ? 1u : 0u;

    for ( usize i = 1; i < result.nodes.size(); ++i )
    {
        result.nodes[ i ].focusDepth = 1;
        result.nodes[ i ].focusParent = 0;
    }

    if ( depthLimit == 1 )
        return result;

    const usize maxNodes = std::min< usize >(
        limits.maxNodes, CANTORDUST_UNIVERSE_HOP_MAX_FUNCTIONS );

    const usize maxLinks = std::min< usize >(
        limits.maxLinks, CANTORDUST_UNIVERSE_HOP_MAX_LINKS );

    const usize maxAddedPerParent = std::min< usize >(
        limits.maxFanout, CANTORDUST_UNIVERSE_HOP_MAX_FANOUT );
    std::unordered_set< u64 > disabled;

    if ( collapsed != nullptr )
        disabled.insert( collapsed->begin(), collapsed->end() );

    struct Adjacent_s
    {
        u64 address = 0;
        std::vector< u64 > incomingSites;
        std::vector< u64 > outgoingSites;
        u32 count() const
        {
            return static_cast< u32 >(
                incomingSites.size() + outgoingSites.size() );
        }
    };
    std::unordered_map< u64, const UniverseFunction_s* > functions;
    functions.reserve( data.functions.size() );

    for ( const UniverseFunction_s& function : data.functions )
        functions.emplace( function.address, &function );
    // Scan all references only once, regardless of requested depth.
    std::unordered_map< u64,
        std::unordered_map< u64, Adjacent_s > > edges;

    for ( const UniverseReference_s& ref : data.references )
    {
        if ( ref.caller == ref.callee ||
             !functions.count( ref.caller ) ||
             !functions.count( ref.callee ) )
        {
            continue;
        }

        if ( ranges != nullptr && std::none_of( ranges->begin(), ranges->end(),
            [&ref]( const UniverseAddressRange_s& range ) {
                return ref.callSite >= range.start && ref.callSite < range.end;
            } ) )
        {
            continue;
        }

        if ( mode != UFM_CALLEES )
        {
            Adjacent_s& neighbor = edges[ ref.callee ][ ref.caller ];
            neighbor.address = ref.caller;
            neighbor.incomingSites.push_back( ref.callSite );
        }

        if ( mode != UFM_CALLERS )
        {
            Adjacent_s& neighbor = edges[ ref.caller ][ ref.callee ];
            neighbor.address = ref.callee;
            neighbor.outgoingSites.push_back( ref.callSite );
        }
    }

    std::unordered_map< u64, usize > indices;
    indices.reserve( result.nodes.size() + maxNodes );

    for ( usize i = 0; i < result.nodes.size(); ++i )
        indices.emplace( result.nodes[ i ].address, i );
    // Existing center edges must not be duplicated when traversed again.
    std::unordered_set< u64 > linkKeys;

    for ( const UniverseLink_s& link : result.links )
        linkKeys.insert( ( u64( link.caller ) << 32 ) | link.callee );
    // BFS ensures that the first path to a node is its shortest path.
    for ( usize parentIndex = 0;
          parentIndex < result.nodes.size(); ++parentIndex )
    {
        // Index zero is already expanded by the v51 first-hop builder.
        if ( parentIndex == 0 )
            continue;

        const UniverseNode_s parent = result.nodes[ parentIndex ];

        if ( parent.focusDepth >= depthLimit ||
             disabled.count( parent.address ) ||
             parent.kind != UNK_FUNCTION )
        {
            continue;
        }

        const auto group = edges.find( parent.address );

        if ( group == edges.end() )
            continue;

        std::vector< const Adjacent_s* > sorted;
        sorted.reserve( group->second.size() );

        for ( const auto& entry : group->second )
            sorted.push_back( &entry.second );

        std::sort( sorted.begin(), sorted.end(),
            []( const Adjacent_s* a, const Adjacent_s* b ) {
                return a->count() != b->count()
                    ? a->count() > b->count() : a->address < b->address;
            } );

        usize added = 0;

        for ( const Adjacent_s* neighbor : sorted )
        {
            if ( neighbor->address == address )
                continue;

            auto known = indices.find( neighbor->address );

            if ( known == indices.end() )
            {
                if ( added >= maxAddedPerParent ||
                     result.nodes.size() >= maxNodes ||
                     result.links.size() >= maxLinks )
                {
                    if ( added >= maxAddedPerParent )
                        result.focusFanoutLimitReached = true;

                    if ( result.nodes.size() >= maxNodes )
                        result.focusNodeLimitReached = true;

                    if ( result.links.size() >= maxLinks )
                        result.focusLinkLimitReached = true;

                    result.truncated = true;
                    continue;
                }

                const UniverseFunction_s* function = functions.at(
                    neighbor->address );

                UniverseNode_s node;
                node.address = function->address;
                node.name = function->name;
                node.kind = function->kind;
                node.focusCalls = neighbor->count();
                node.focusDirection = static_cast< u8 >(
                    ( !neighbor->incomingSites.empty() ? 1u : 0u ) |
                    ( !neighbor->outgoingSites.empty() ? 2u : 0u ) );

                node.focusDepth = parent.focusDepth + 1;
                node.focusParent = parentIndex;
                // A stable outward radial shell with address-based fanout.
                // Avoid dependence on number/order of visible siblings.
                const UniversePosition_s centerDirection = Math_UniverseNormalize(
                    { parent.x, parent.y, parent.z } );

                const u64 seed = neighbor->address ^
                    ( parent.address * 0x9E3779B185EBCA87ull );

                const UniversePosition_s jitter = Math_UniverseDirection( seed );
                const UniversePosition_s direction = Math_UniverseNormalize(
                    Math_UniverseAdd( centerDirection,
                        Math_UniverseScale( jitter, 0.75f ) ) );

                const f32 radius = 0.90f +
                    0.95f * static_cast< f32 >( node.focusDepth - 1 );

                const UniversePosition_s placed = Math_UniverseScale(
                    direction, radius );

                node.x = placed.x;
                node.y = placed.y;
                node.z = placed.z;

                const usize index = result.nodes.size();
                indices.emplace( node.address, index );
                result.nodes.push_back( std::move( node ) );
                result.focusDepthReached = std::max(
                    result.focusDepthReached, result.nodes.back().focusDepth );

                known = indices.find( neighbor->address );
                ++added;
            }

            const u32 target = static_cast< u32 >(
                known->second );

            const u32 parentId = static_cast< u32 >(
                parentIndex );

            const auto appendLink = [&]( const u32 caller,
                const u32 callee,
                const std::vector< u64 >& sites ) {
                if ( sites.empty() )
                    return;

                const u64 key =
                    ( u64( caller ) << 32 ) | callee;

                if ( linkKeys.count( key ) )
                    return;

                if ( result.links.size() >= maxLinks )
                {
                    result.focusLinkLimitReached = true;
                    result.truncated = true;
                    return;
                }

                linkKeys.insert( key );
                UniverseLink_s link;
                link.caller = caller;
                link.callee = callee;
                link.count = static_cast< u32 >( sites.size() );
                link.callSites = sites;
                std::sort( link.callSites.begin(), link.callSites.end() );
                result.links.push_back( std::move( link ) );
            };
            appendLink( target, parentId, neighbor->incomingSites );
            appendLink( parentId, target, neighbor->outgoingSites );
        }
    }

    return result;
}

UniverseCallPath_s Universe_FindCallPath( const UniverseData_s& data,
    const u64 from, const u64 to )
{
    UniverseCallPath_s path;
    std::unordered_set< u64 > functions;
    functions.reserve( data.functions.size() );

    for ( const UniverseFunction_s& item : data.functions )
        if ( item.kind == UNK_FUNCTION )
            functions.insert( item.address );

    if ( !functions.count( from ) || !functions.count( to ) )
        return path;

    if ( from == to )
    {
        path.addresses.push_back( from );
        return path;
    }

    struct Edge_s { u64 to; u64 site; };
    std::unordered_map< u64, std::vector< Edge_s > > adjacent;
    adjacent.reserve( functions.size() );

    for ( const UniverseReference_s& ref : data.references )
    {
        if ( functions.count( ref.caller ) && functions.count( ref.callee ) &&
             ref.caller != ref.callee )
        {
            adjacent[ ref.caller ].push_back({ ref.callee, ref.callSite });
        }
    }

    for ( auto& pair : adjacent )
    {
        std::sort( pair.second.begin(), pair.second.end(),
            []( const Edge_s& a, const Edge_s& b ) {
                return a.to != b.to ? a.to < b.to : a.site < b.site;
            } );
    }

    std::unordered_map< u64, Edge_s > parent;
    std::queue< u64 > pending;
    parent.emplace( from, Edge_s{ from, 0 } );
    pending.push( from );

    while ( !pending.empty() && !parent.count( to ) )
    {
        const u64 source = pending.front();
        pending.pop();

        for ( const Edge_s& edge : adjacent[ source ] )
        {
            if ( parent.count( edge.to ) )
                continue;
            // parent stores predecessor in `to` and the exact callsite.
            parent.emplace( edge.to, Edge_s{ source, edge.site } );

            if ( edge.to == to )
                break;

            pending.push( edge.to );
        }
    }

    if ( !parent.count( to ) )
        return path;

    u64 current = to;

    while ( current != from )
    {
        path.addresses.push_back( current );
        path.callSites.push_back( parent.at( current ).site );
        current = parent.at( current ).to;
    }

    path.addresses.push_back( from );
    std::reverse( path.addresses.begin(), path.addresses.end() );
    std::reverse( path.callSites.begin(), path.callSites.end() );
    return path;
}

UniverseGraph_s Universe_BuildPathGraph( const UniverseData_s& data,
    const UniverseCallPath_s& path )
{
    UniverseGraph_s graph;
    graph.focusIndex = CANTORDUST_NO_SOURCE;
    graph.pathMode = true;

    if ( path.addresses.empty() )
        return graph;

    std::unordered_map< u64, const UniverseFunction_s* > functions;

    for ( const UniverseFunction_s& f : data.functions )
        functions.emplace( f.address, &f );

    const usize length = path.addresses.size();

    if ( path.callSites.size() + 1u != length )
        return {};

    for ( usize i = 0; i < length; ++i )
    {
        const auto iter = functions.find( path.addresses[ i ] );

        if ( iter == functions.end() )
            return {};

        UniverseNode_s node;
        node.address = iter->second->address;
        node.name = iter->second->name;
        node.kind = iter->second->kind;
        node.incoming = static_cast< u32 >( length - i );
        node.x = ( static_cast< f32 >( i ) -
            0.5f * static_cast< f32 >( length - 1u ) ) * CANTORDUST_UNIVERSE_PATH_LAYER_SPACING;

        node.y = sin( static_cast< f32 >( i ) * 1.3f ) * 0.18f;
        node.z = cos( static_cast< f32 >( i ) * 1.3f ) * 0.18f;
        graph.nodes.push_back( std::move( node ) );

        if ( i > 0 )
        {
            UniverseLink_s link;
            link.caller = static_cast< u32 >( i - 1u );
            link.callee = static_cast< u32 >( i );
            link.count = 1;
            link.callSites.push_back( path.callSites[ i - 1u ] );
            graph.links.push_back( std::move( link ) );
        }
    }

    return graph;
}

} // namespace cantordust
