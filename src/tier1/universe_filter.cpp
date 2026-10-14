#include "cantordust/universe.h"
#include "cantordust/scalar_types.h"

#include <algorithm>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <vector>

namespace cantordust
{

UniverseGraph_s Universe_FilterGraphAdvanced( const UniverseGraph_s& graph,
    const UniverseData_s& data, const UniverseFilterOptions_s options )
{
    if ( graph.pathMode || ( !options.hideImports && !options.hideThunks &&
         !options.hideLibraries && !options.hideGlobals &&
         options.minimumIncoming == 0 && options.minimumOutgoing == 0 ) )
    {
        return graph;
    }

    std::unordered_map< u64, u8 > flags;
    flags.reserve( data.functions.size() );

    for ( const UniverseFunction_s& function : data.functions )
        flags.emplace( function.address, function.flags );

    struct Counts { u32 incoming = 0, outgoing = 0; };
    std::unordered_map< u64, Counts > counts;
    counts.reserve( data.functions.size() );

    for ( const UniverseReference_s& ref : data.references )
    {
        u32& incoming = counts[ ref.callee ].incoming;
        u32& outgoing = counts[ ref.caller ].outgoing;

        if ( incoming < UINT32_MAX )
            ++incoming;

        if ( outgoing < UINT32_MAX )
            ++outgoing;
    }

    std::vector< bool > visible( graph.nodes.size(), false );

    for ( usize i = 0; i < graph.nodes.size(); ++i )
    {
        const UniverseNode_s& node = graph.nodes[ i ];

        if ( i == graph.focusIndex )
        {
            visible[ i ] = true;
            continue;
        }

        if ( node.kind == UNK_GLOBAL )
        {
            visible[ i ] = !options.hideGlobals; // Functions thresholds do not hide globals.
            continue;
        }

        const auto meta = flags.find( node.address );
        const u8 type = meta != flags.end() ? meta->second : 0;

        if ( ( options.hideImports && ( type & UFF_IMPORT ) ) ||
             ( options.hideThunks && ( type & UFF_THUNK ) ) ||
             ( options.hideLibraries && ( type & UFF_LIBRARY ) ) )
        {
            continue;
        }

        const Counts count = counts[ node.address ];
        visible[ i ] = count.incoming >= options.minimumIncoming &&
            count.outgoing >= options.minimumOutgoing;
    }

    // In an expanded focus, keep only nodes still reachable from the root
    // after filters remove intermediate branches. Avoid orphaned shell nodes.
    if ( graph.focusIndex < graph.nodes.size() )
    {
        std::vector< std::vector< usize > > adjacency( graph.nodes.size() );

        for ( const UniverseLink_s& link : graph.links )
        {
            if ( link.caller >= visible.size() || link.callee >= visible.size() ||
                 !visible[ link.caller ] || !visible[ link.callee ] )
            {
                continue;
            }

            adjacency[ link.caller ].push_back( link.callee );
            adjacency[ link.callee ].push_back( link.caller );
        }

        std::vector< bool > reached( visible.size(), false );
        std::queue< usize > pending;
        pending.push( graph.focusIndex );
        reached[ graph.focusIndex ] = true;

        while ( !pending.empty() )
        {
            const usize parent = pending.front();
            pending.pop();

            for ( const usize child : adjacency[ parent ] )
                if ( !reached[ child ] )
                {
                    reached[ child ] = true;
                    pending.push( child );
                }
        }

        for ( usize i = 0; i < visible.size(); ++i )
            visible[ i ] = visible[ i ] && reached[ i ];
    }

    UniverseGraph_s result = graph;
    result.nodes.clear();
    result.links.clear();
    result.focusIndex = CANTORDUST_NO_SOURCE;
    std::vector< usize > remap( visible.size(), CANTORDUST_NO_SOURCE );
    result.nodes.reserve( graph.nodes.size() );

    for ( usize i = 0; i < graph.nodes.size(); ++i )
    {
        if ( !visible[ i ] )
            continue;

        remap[ i ] = result.nodes.size();
        result.nodes.push_back( graph.nodes[ i ] );

        if ( i == graph.focusIndex )
            result.focusIndex = remap[ i ];
    }

    for ( UniverseNode_s& node : result.nodes )
    {
        if ( node.focusParent < remap.size() &&
             remap[ node.focusParent ] != CANTORDUST_NO_SOURCE )
        {
            node.focusParent = remap[ node.focusParent ];
        }
        else
            node.focusParent = CANTORDUST_NO_SOURCE;
    }

    for ( const UniverseLink_s& link : graph.links )
    {
        if ( link.caller >= remap.size() || link.callee >= remap.size() ||
             remap[ link.caller ] == CANTORDUST_NO_SOURCE ||
             remap[ link.callee ] == CANTORDUST_NO_SOURCE )
        {
            continue;
        }

        UniverseLink_s kept = link;
        kept.caller = static_cast< u32 >( remap[ link.caller ] );
        kept.callee = static_cast< u32 >( remap[ link.callee ] );
        result.links.push_back( std::move( kept ) );
    }

    result.focusDepthReached = 0;

    for ( const UniverseNode_s& node : result.nodes )
        result.focusDepthReached = std::max( result.focusDepthReached, node.focusDepth );

    return result;
}

} // namespace cantordust
