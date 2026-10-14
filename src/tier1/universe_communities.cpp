#include "cantordust/universe.h"
#include "cantordust/scalar_types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cantordust
{

UniverseCommunityResult_s Universe_BuildCommunities( const UniverseGraph_s& source )
{
    UniverseCommunityResult_s result;
    result.graph.communityMode = true;
    result.graph.candidateCount = source.candidateCount;
    result.graph.referenceCount = source.referenceCount;
    result.graph.absoluteHeatMaximum = source.absoluteHeatMaximum;
    result.graph.truncated = source.truncated;

    const usize count = source.nodes.size();

    if ( count == 0 )
        return result;
    // One deterministic, weighted modularity pass (iterated to convergence).
    // This is deliberately bounded: each sweep is O(nodes + links) and the
    // maximum number of sweeps is fixed for busy 4096-node IDA tiers.
    using WeightMap = std::map< usize, f64 >;
    std::vector< WeightMap > adjacent( count );
    std::vector< f64 > degree( count, 0.0 );

    for ( const UniverseLink_s& link : source.links )
    {
        const usize a = link.caller;
        const usize b = link.callee;

        if ( a >= count || b >= count || a == b ||
             source.nodes[ a ].kind != UNK_FUNCTION ||
             source.nodes[ b ].kind != UNK_FUNCTION )
        {
            continue;
        }

        const f64 weight = 1.0 + std::log2( static_cast< f64 >(
            std::max( 1u, link.count ) ) );

        adjacent[ a ][ b ] += weight;
        adjacent[ b ][ a ] += weight;
        degree[ a ] += weight;
        degree[ b ] += weight;
    }

    const f64 totalDegree = std::accumulate(
        degree.begin(), degree.end(), 0.0 );

    std::vector< usize > community( count );
    std::iota( community.begin(), community.end(), 0 );
    std::vector< f64 > volume = degree;
    std::vector< usize > order( count );
    std::iota( order.begin(), order.end(), 0 );
    std::sort( order.begin(), order.end(), [&source]( usize a, usize b ) {
        return source.nodes[ a ].address < source.nodes[ b ].address;
    } );

    if ( totalDegree > 0.0 )
    {
        for ( int sweep = 0; sweep < 12; ++sweep )
        {
            bool changed = false;

            for ( const usize node : order )
            {
                if ( source.nodes[ node ].kind != UNK_FUNCTION ||
                     degree[ node ] <= 0.0 )
                {
                    continue;
                }

                const usize original = community[ node ];
                std::map< usize, f64 > support;

                for ( const auto& edge : adjacent[ node ] )
                    support[ community[ edge.first ] ] += edge.second;

                volume[ original ] -= degree[ node ];
                // A fixed penalty for disconnected membership avoids
                // absorption of long weakly-linked chains into one blob.
                const auto quality = [&]( usize label ) {
                    return support[ label ] - degree[ node ] *
                        volume[ label ] / totalDegree;
                };
                usize best = original;
                f64 score = quality( original );

                for ( const auto& candidate : support )
                {
                    const f64 value = quality( candidate.first );

                    if ( value > score + 1e-8 ||
                         ( value >= score - 1e-8 && value <= score + 1e-8 &&
                           candidate.first < best ) )
                    {
                        best = candidate.first;
                        score = value;
                    }
                }

                community[ node ] = best;
                volume[ best ] += degree[ node ];
                changed |= best != original;
            }

            if ( !changed )
                break;
        }
    }

    std::map< usize, std::vector< usize > > groups;

    for ( usize i = 0; i < count; ++i )
        groups[ community[ i ] ].push_back( i );
    // Order by representative address rather than by iterative label IDs.
    std::vector< std::vector< usize > > ordered;

    for ( auto& group : groups )
    {
        std::vector< usize >& members = group.second;
        std::sort( members.begin(), members.end(), [&source]( usize a, usize b ) {
            return source.nodes[ a ].address < source.nodes[ b ].address;
        } );
        ordered.push_back( std::move( members ) );
    }

    std::sort( ordered.begin(), ordered.end(), [&source]( const auto& a, const auto& b ) {
        return source.nodes[ a.front() ].address < source.nodes[ b.front() ].address;
    } );

    std::vector< usize > remap( count );

    for ( const std::vector< usize >& members : ordered )
    {
        const usize groupIndex = result.graph.nodes.size();

        for ( const usize original : members )
            remap[ original ] = groupIndex;
        // Representative chosen from the busiest function in the group.
        const usize representative = *std::max_element( members.begin(), members.end(),
            [&source]( usize a, usize b ) {
                const UniverseNode_s& x = source.nodes[ a ];
                const UniverseNode_s& y = source.nodes[ b ];
                const u64 heatX = static_cast< u64 >( x.incoming ) + x.outgoing;
                const u64 heatY = static_cast< u64 >( y.incoming ) + y.outgoing;
                return heatX == heatY ? x.address > y.address : heatX < heatY;
            } );

        UniverseNode_s node = source.nodes[ representative ];
        node.incoming = 0;
        node.outgoing = 0;
        node.communitySize = static_cast< u32 >( members.size() );
        node.x = node.y = node.z = 0.0f;
        std::vector< u64 > addresses;
        addresses.reserve( members.size() );

        for ( const usize index : members )
        {
            const UniverseNode_s& original = source.nodes[ index ];
            addresses.push_back( original.address );
            node.incoming = static_cast< u32 >(
                std::min< u64 >( UINT32_MAX,
                    static_cast< u64 >( node.incoming ) + original.incoming ) );

            node.outgoing = static_cast< u32 >(
                std::min< u64 >( UINT32_MAX,
                    static_cast< u64 >( node.outgoing ) + original.outgoing ) );

            node.x += original.x;
            node.y += original.y;
            node.z += original.z;
        }

        const f32 div = 1.0f / static_cast< f32 >( members.size() );
        node.x *= div;
        node.y *= div;
        node.z *= div;

        if ( members.size() > 1 )
        {
            node.kind = UNK_COMMUNITY;
            node.name = "Community (" + std::to_string( members.size() ) +
                "): " + node.name;
        }

        result.members.emplace( node.address, std::move( addresses ) );
        result.graph.nodes.push_back( std::move( node ) );
    }

    std::map< std::pair< u32, u32 >, usize > merged;

    for ( const UniverseLink_s& sourceLink : source.links )
    {
        if ( sourceLink.caller >= count || sourceLink.callee >= count )
            continue;

        const u32 a = static_cast< u32 >( remap[ sourceLink.caller ] );
        const u32 b = static_cast< u32 >( remap[ sourceLink.callee ] );

        if ( a == b )
            continue; // internal calls belong to the community
        const auto key = std::make_pair( a, b );
        auto it = merged.find( key );

        if ( it == merged.end() )
        {
            UniverseLink_s link;
            link.caller = a;
            link.callee = b;
            it = merged.emplace( key, result.graph.links.size() ).first;
            result.graph.links.push_back( std::move( link ) );
        }

        UniverseLink_s& link = result.graph.links[ it->second ];
        link.count = static_cast< u32 >( std::min< u64 >(
            UINT32_MAX, static_cast< u64 >( link.count ) + sourceLink.count ) );
    }

    return result;
}

} // namespace cantordust
