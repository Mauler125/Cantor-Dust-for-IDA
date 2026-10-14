#include "cantordust/universe.h"
#include "cantordust/scalar_types.h"
#include "cantordust/constants.h"
#include "render/universe_detail.h"

#include "math/color.h"
#include "math/cube.h"
#include "math/universe.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <iterator>
#include <unordered_set>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cantordust
{

using std::floor;
using std::sin;
using std::sqrt;

struct UniverseAdjacency_s
{
    u32 other = 0;
    u32 count = 0;
};

static u64 Universe_LinkKey(
    const u32 caller, const u32 callee )
{
    return ( static_cast< u64 >( caller ) << 32 ) | callee;
}

static void Universe_BuildAdjacency(
    const std::vector< UniverseLink_s >& links,
    const usize nodeCount,
    std::vector< std::vector< UniverseAdjacency_s > >& undirected,
    std::unordered_map< u64, u32 >& directedCounts )
{
    undirected.assign( nodeCount, {} );
    directedCounts.clear();
    directedCounts.reserve( links.size() * 2u );

    for ( const UniverseLink_s& link : links )
    {
        if ( link.caller >= nodeCount || link.callee >= nodeCount )
        {
            continue;
        }

        undirected[ link.caller ].push_back({ link.callee, link.count });
        undirected[ link.callee ].push_back({ link.caller, link.count });
        directedCounts.emplace( Universe_LinkKey( link.caller, link.callee ),
            link.count );
    }
}

static void Universe_Layout(
    std::vector< UniverseNode_s >& nodes,
    const std::vector< UniverseLink_s >& links, const bool organic = false )
{
    if ( nodes.empty() )
    {
        return;
    }

    std::vector< std::vector< UniverseAdjacency_s > > adjacent;
    std::unordered_map< u64, u32 > directedCounts;
    Universe_BuildAdjacency( links, nodes.size(), adjacent, directedCounts );

    std::vector< usize > rank( nodes.size() );
    std::iota( rank.begin(), rank.end(), 0 );
    std::sort( rank.begin(), rank.end(), [&nodes](
        const usize left, const usize right ) {
        const f32 a = Universe_NodeProminence( nodes[ left ] );
        const f32 b = Universe_NodeProminence( nodes[ right ] );

        if ( a != b )
        {
            return a > b;
        }

        if ( nodes[ left ].incoming != nodes[ right ].incoming )
        {
            return nodes[ left ].incoming > nodes[ right ].incoming;
        }

        return nodes[ left ].address < nodes[ right ].address;
    } );

    std::vector< usize > rankPosition( nodes.size() );

    for ( usize i = 0; i < rank.size(); ++i )
    {
        rankPosition[ rank[ i ] ] = i;
    }

    std::vector< i32 > parent( nodes.size(), -1 );
    std::vector< u32 > parentWeight( nodes.size(), 0 );
    std::vector< usize > groupRoot( nodes.size(), 0 );
    groupRoot.assign( nodes.size(), 0 );

    for ( const usize index : rank )
    {
        f32 bestScore = -1.0f;
        i32 bestParent = -1;
        u32 bestWeight = 0;

        for ( const UniverseAdjacency_s& neighbor : adjacent[ index ] )
        {
            if ( neighbor.other >= nodes.size() ||
                 rankPosition[ neighbor.other ] >= rankPosition[ index ] )
            {
                continue;
            }

            const f32 candidateProminence =
                Universe_NodeProminence( nodes[ neighbor.other ] );

            const f32 score = candidateProminence * 2.0f +
                static_cast< f32 >( neighbor.count ) * 5.5f;

            if ( score > bestScore ||
                 ( score == bestScore && neighbor.count > bestWeight ) )
            {
                bestScore = score;
                bestParent = static_cast< i32 >( neighbor.other );
                bestWeight = neighbor.count;
            }
        }

        parent[ index ] = bestParent;
        parentWeight[ index ] = bestWeight;
    }

    std::vector< usize > rootSet;
    rootSet.reserve( nodes.size() );

    for ( usize i = 0; i < nodes.size(); ++i )
    {
        if ( parent[ i ] < 0 )
        {
            rootSet.push_back( i );
            groupRoot[ i ] = i;
        }
    }

    std::sort( rootSet.begin(), rootSet.end(), [&nodes](
        const usize a, const usize b ) {
        const f32 pa = Universe_NodeProminence( nodes[ a ] );
        const f32 pb = Universe_NodeProminence( nodes[ b ] );
        return pa != pb ? pa > pb : nodes[ a ].address < nodes[ b ].address;
    } );

    for ( const usize index : rank )
    {
        if ( parent[ index ] >= 0 )
        {
            groupRoot[ index ] = groupRoot[ static_cast< usize >( parent[ index ] ) ];
        }
    }

    struct UniverseGridCell_s
    {
        int x = 0;
        int y = 0;
        int z = 0;
        int shell = 0;
    };

    const usize desired = std::max< usize >(
        nodes.size() * 3u / 2u + 64u, nodes.size() + 32u );

    int radius = 0;

    while ( static_cast< usize >( ( radius * 2 + 1 ) *
        ( radius * 2 + 1 ) * ( radius * 2 + 1 ) ) < desired )
    {
        ++radius;
    }

    std::vector< UniverseGridCell_s > cells;
    cells.reserve( static_cast< usize >( ( radius * 2 + 1 ) *
        ( radius * 2 + 1 ) * ( radius * 2 + 1 ) ) );

    for ( int z = -radius; z <= radius; ++z )
    {
        for ( int y = -radius; y <= radius; ++y )
        {
            for ( int x = -radius; x <= radius; ++x )
            {
                const int shell = std::max( { std::abs( x ), std::abs( y ), std::abs( z ) } );
                cells.push_back({ x, y, z, shell });
            }
        }
    }

    std::sort( cells.begin(), cells.end(), []( const UniverseGridCell_s& a,
        const UniverseGridCell_s& b ) {
        if ( a.shell != b.shell )
        {
            return a.shell < b.shell;
        }

        const int da = a.x * a.x + a.y * a.y + a.z * a.z;
        const int db = b.x * b.x + b.y * b.y + b.z * b.z;

        if ( da != db )
        {
            return da < db;
        }

        if ( a.z != b.z )
            return a.z < b.z;

        if ( a.y != b.y )
            return a.y < b.y;

        return a.x < b.x;
    } );

    std::vector< bool > used( cells.size(), false );
    std::vector< UniversePosition_s > positions( nodes.size() );
    std::vector< usize > assignedCell( nodes.size(), 0 );

    const f32 step = 0.24f;
    const f32 shellScale = 0.30f;
    const auto cellPosition = [step]( const UniverseGridCell_s& cell ) {
        return UniversePosition_s{
            static_cast< f32 >( cell.x ) * step,
            static_cast< f32 >( cell.y ) * step,
            static_cast< f32 >( cell.z ) * step * 0.90f };
    };

    auto reserveClosestCell = [&]( const UniversePosition_s& target,
        const int minShell ) {
        usize best = cells.size();
        f32 bestScore = std::numeric_limits< f32 >::max();

        for ( usize i = 0; i < cells.size(); ++i )
        {
            if ( used[ i ] || cells[ i ].shell < minShell )
            {
                continue;
            }

            const UniversePosition_s pos = cellPosition( cells[ i ] );
            const f32 dx = pos.x - target.x;
            const f32 dy = pos.y - target.y;
            const f32 dz = pos.z - target.z;
            const f32 score = dx * dx + dy * dy + dz * dz +
                static_cast< f32 >( cells[ i ].shell ) * 0.001f;

            if ( score < bestScore )
            {
                bestScore = score;
                best = i;
            }
        }

        if ( best == cells.size() )
        {
            for ( usize i = 0; i < cells.size(); ++i )
            {
                if ( !used[ i ] )
                {
                    best = i;
                    break;
                }
            }
        }

        used[ best ] = true;
        return best;
    };

    const usize rootCount = rootSet.size();

    for ( usize i = 0; i < rootCount; ++i )
    {
        const f32 t = rootCount > 1 ?
            static_cast< f32 >( i ) /
                static_cast< f32 >( rootCount - 1 ) : 0.0f;

        const f32 shell = 0.25f + sqrt( t ) *
            static_cast< f32 >( radius ) * shellScale;

        const UniversePosition_s dir = Math_UniverseDirection(
            nodes[ rootSet[ i ] ].address ^ 0x74A7C15Du );

        const UniversePosition_s desiredPos = Math_UniverseScale( dir, shell );
        const usize cell = reserveClosestCell( desiredPos,
            static_cast< int >( floor( shell / step * 0.52f ) ) );

        assignedCell[ rootSet[ i ] ] = cell;
        positions[ rootSet[ i ] ] = cellPosition( cells[ cell ] );
    }

    for ( const usize index : rank )
    {
        if ( parent[ index ] < 0 )
        {
            continue;
        }

        const usize parentIndex = static_cast< usize >( parent[ index ] );
        const UniversePosition_s parentPos = positions[ parentIndex ];
        const UniversePosition_s groupDir = Math_UniverseNormalize(
            Math_UniverseAdd( parentPos,
                Math_UniverseScale( Math_UniverseDirection(
                    nodes[ groupRoot[ index ] ].address ^ nodes[ index ].address ), 0.55f ) ) );

        const f32 localRadius = 0.28f +
            std::log2( 1.0f + static_cast< f32 >( std::max( 1u, parentWeight[ index ] ) ) ) * 0.13f +
            static_cast< f32 >( rankPosition[ index ] ) /
                static_cast< f32 >( std::max< usize >( 1u, nodes.size() ) ) * 1.35f;

        const UniversePosition_s desiredPos = Math_UniverseAdd( parentPos,
            Math_UniverseScale( groupDir, localRadius ) );

        const int minShell = std::min( radius,
            std::max( cells[ assignedCell[ parentIndex ] ].shell,
                static_cast< int >( localRadius / step ) ) );

        const usize cell = reserveClosestCell( desiredPos, minShell );
        assignedCell[ index ] = cell;
        positions[ index ] = cellPosition( cells[ cell ] );
    }

    if ( organic )
    {
        // Map cubic occupancy shells onto an irregular, symmetric sphere.
        // Radial distance follows the Chebyshev shell, not cube faces;
        // the lattice only ensures initial node separation and clustering.
        for ( usize i = 0; i < nodes.size(); ++i )
        {
            const UniversePosition_s p = positions[ i ];
            const f32 squareShell = std::max( {
                std::abs( p.x ), std::abs( p.y ),
                std::abs( p.z / 0.90f ) } );

            const f32 len = sqrt( p.x * p.x + p.y * p.y +
                p.z * p.z );

            if ( len < 0.0001f )
            {
                continue;
            }

            const UniversePosition_s direction = {
                p.x / len, p.y / len, p.z / len };

            const UniversePosition_s drift = Math_UniverseDirection(
                nodes[ i ].address ^ 0xD462B37AF11C5027ull );

            const UniversePosition_s curl = Math_UniverseDirection(
                nodes[ i ].address ^ 0xB5E1394C6A802F11ull );
            // Smooth low-frequency bulges produce a rounded nebula instead
            // of another bounding cube or a perfectly flat sphere.
            const f32 wave = 0.090f * sin(
                direction.x * 4.3f + direction.y * 2.1f ) +
                0.075f * sin( direction.z * 3.6f -
                    direction.x * 1.9f );

            const f32 ring = 1.0f + wave;
            const f32 radius = squareShell * ring;
            const f32 jitter = step * 0.28f;
            positions[ i ] = {
                direction.x * radius + jitter *
                    ( drift.x * 0.80f + curl.y * 0.20f ),
                direction.y * radius + jitter *
                    ( drift.y * 0.80f + curl.z * 0.20f ),
                direction.z * radius * 0.94f + jitter *
                    ( drift.z * 0.80f + curl.x * 0.20f ) };
        }
    }

    f32 minX = std::numeric_limits< f32 >::max();
    f32 minY = minX;
    f32 minZ = minX;
    f32 maxX = -minX;
    f32 maxY = -minX;
    f32 maxZ = -minX;

    for ( const UniversePosition_s& pos : positions )
    {
        minX = std::min( minX, pos.x );
        minY = std::min( minY, pos.y );
        minZ = std::min( minZ, pos.z );
        maxX = std::max( maxX, pos.x );
        maxY = std::max( maxY, pos.y );
        maxZ = std::max( maxZ, pos.z );
    }

    const f32 centerX = ( minX + maxX ) * 0.5f;
    const f32 centerY = ( minY + maxY ) * 0.5f;
    const f32 centerZ = ( minZ + maxZ ) * 0.5f;
    // Do not shrink larger universes back into the same fixed cube.
    // Rendering computes a camera framing scale from this natural extent.
    for ( usize i = 0; i < nodes.size(); ++i )
    {
        nodes[ i ].x = positions[ i ].x - centerX;
        nodes[ i ].y = positions[ i ].y - centerY;
        nodes[ i ].z = positions[ i ].z - centerZ;
    }
}

void Universe_RelayoutGraph( UniverseGraph_s& graph, const bool organic )
{
    if ( graph.focusIndex != CANTORDUST_NO_SOURCE )
    {
        return;
    }

    Universe_Layout( graph.nodes, graph.links, organic );
    graph.adaptiveLayout = true;
}

UniverseGraph_s Universe_BuildGraph(
    const std::vector< UniverseFunction_s >& functions,
    const std::vector< UniverseReference_s >& references,
    const usize maxFunctions, const usize maxLinks,
    const usize rankStart, const bool distinctRanking,
    const bool globalFields )
{
    UniverseGraph_s graph;
    graph.candidateCount = functions.size();
    graph.referenceCount = references.size();

    std::unordered_map< u64, usize > byAddress;
    byAddress.reserve( functions.size() );
    std::vector< UniverseNode_s > all;
    all.reserve( functions.size() );

    for ( const UniverseFunction_s& func : functions )
    {
        if ( !byAddress.emplace( func.address, all.size() ).second )
        {
            continue;
        }

        UniverseNode_s node;
        node.address = func.address;
        node.name = func.name;
        node.kind = func.kind;
        all.push_back( std::move( node ) );
    }

    struct Edge_s
    {
        usize caller = 0;
        usize callee = 0;
        u64 callSite = 0;
        u64 targetAddress = 0;
    };
    std::vector< Edge_s > edges;
    edges.reserve( references.size() );

    for ( const UniverseReference_s& ref : references )
    {
        const auto from = byAddress.find( ref.caller );
        const auto to = byAddress.find( ref.callee );

        if ( from == byAddress.end() || to == byAddress.end() )
        {
            continue;
        }

        edges.push_back({ from->second, to->second, ref.callSite,
            ref.targetAddress != 0 ? ref.targetAddress : ref.callee });

        if ( all[ from->second ].outgoing !=
             std::numeric_limits< u32 >::max() )
        {
            ++all[ from->second ].outgoing;
        }

        if ( all[ to->second ].incoming !=
             std::numeric_limits< u32 >::max() )
        {
            ++all[ to->second ].incoming;
        }
    }

    std::vector< u32 > rankIncoming( all.size() );
    std::vector< u32 > rankOutgoing( all.size() );

    if ( distinctRanking )
    {
        std::unordered_set< u64 > uniquePairs;
        uniquePairs.reserve( edges.size() );

        for ( const Edge_s& edge : edges )
        {
            const u64 key = Universe_LinkKey(
                static_cast< u32 >( edge.caller ),
                static_cast< u32 >( edge.callee ) );

            if ( uniquePairs.insert( key ).second )
            {
                ++rankIncoming[ edge.callee ];
                ++rankOutgoing[ edge.caller ];
            }
        }
    }
    else
    {
        for ( usize i = 0; i < all.size(); ++i )
        {
            rankIncoming[ i ] = all[ i ].incoming;
            rankOutgoing[ i ] = all[ i ].outgoing;
        }
    }

    if ( globalFields )
    {
        // Each (referring function, distinct accessed field) contributes once.
        struct Field_s
        {
            usize caller = 0;
            usize callee = 0;
            u64 target = 0;
            bool operator<( const Field_s& other ) const
            {
                if ( callee != other.callee )
                    return callee < other.callee;

                if ( caller != other.caller )
                    return caller < other.caller;

                return target < other.target;
            }

            bool operator==( const Field_s& other ) const
            {
                return caller == other.caller && callee == other.callee &&
                    target == other.target;
            }
        };
        std::vector< Field_s > fields;

        for ( const Edge_s& edge : edges )
        {
            if ( all[ edge.callee ].kind == UNK_GLOBAL )
            {
                fields.push_back({ edge.caller, edge.callee,
                    edge.targetAddress });
            }
        }

        std::sort( fields.begin(), fields.end() );
        fields.erase( std::unique( fields.begin(), fields.end() ), fields.end() );

        for ( usize i = 0; i < all.size(); ++i )
        {
            if ( all[ i ].kind == UNK_GLOBAL )
                rankIncoming[ i ] = 0;
        }

        for ( const Field_s& field : fields )
            ++rankIncoming[ field.callee ];
    }

    for ( const u32 incoming : rankIncoming )
    {
        graph.absoluteHeatMaximum = std::max(
            graph.absoluteHeatMaximum, incoming );
    }

    std::vector< usize > ranked( all.size() );
    std::iota( ranked.begin(), ranked.end(), 0 );
    std::sort( ranked.begin(), ranked.end(),
        [&all, &rankIncoming, &rankOutgoing](
            const usize a, const usize b ) {
        if ( rankIncoming[ a ] != rankIncoming[ b ] )
        {
            return rankIncoming[ a ] > rankIncoming[ b ];
        }

        if ( rankOutgoing[ a ] != rankOutgoing[ b ] )
        {
            return rankOutgoing[ a ] > rankOutgoing[ b ];
        }

        return all[ a ].address < all[ b ].address;
    } );

    const usize first = std::min( rankStart, ranked.size() );
    const usize keep = std::min( maxFunctions, ranked.size() - first );
    std::vector< usize > anchors;

    if ( first != 0 )
    {
        // Keep a handful of major hubs visible as cross-tier reference points.
        const usize anchorCount = std::min< usize >( 32u, first );
        anchors.assign( ranked.begin(), ranked.begin() +
            static_cast< std::ptrdiff_t >( anchorCount ) );
    }

    ranked.erase( ranked.begin(), ranked.begin() +
        static_cast< std::ptrdiff_t >( first ) );

    ranked.resize( keep );
    ranked.insert( ranked.end(), anchors.begin(), anchors.end() );

    // A referenced global may survive tier ranking while none of its
    // referring functions do. Reserve a small, bounded context budget so
    // visible globals retain at least one actual connection when possible.
    std::vector< bool > selected( all.size(), false );
    std::vector< usize > visibleGlobals;

    for ( const usize source : ranked )
    {
        selected[ source ] = true;

        if ( all[ source ].kind == UNK_GLOBAL )
        {
            visibleGlobals.push_back( source );
        }
    }

    if ( !visibleGlobals.empty() && maxFunctions != 0 )
    {
        std::vector< bool > hasCaller( all.size(), false );
        std::unordered_map< u64, u32 > extraPairs;

        for ( const Edge_s& edge : edges )
        {
            if ( !selected[ edge.callee ] ||
                 all[ edge.callee ].kind != UNK_GLOBAL )
            {
                continue;
            }

            if ( selected[ edge.caller ] )
            {
                hasCaller[ edge.callee ] = true;
            }
            else
            {
                const u64 key = Universe_LinkKey(
                    static_cast< u32 >( edge.caller ),
                    static_cast< u32 >( edge.callee ) );

                u32& count = extraPairs[ key ];

                if ( count != UINT32_MAX )
                    ++count;
            }
        }

        std::vector< usize > bestCaller( all.size(), all.size() );
        std::vector< u32 > bestWeight( all.size(), 0 );

        for ( const auto& pair : extraPairs )
        {
            const usize caller = static_cast< usize >( pair.first >> 32 );
            const usize callee = static_cast< usize >( pair.first & 0xFFFFFFFFu );

            if ( pair.second > bestWeight[ callee ] ||
                 ( pair.second == bestWeight[ callee ] &&
                   ( bestCaller[ callee ] == all.size() ||
                     rankOutgoing[ caller ] > rankOutgoing[ bestCaller[ callee ] ] ) ) )
            {
                bestCaller[ callee ] = caller;
                bestWeight[ callee ] = pair.second;
            }
        }

        const usize budget = std::min< usize >( 256u,
            std::max< usize >( 1u, maxFunctions / 16u ) );

        usize extras = 0;

        for ( const usize global : visibleGlobals )
        {
            const usize caller = bestCaller[ global ];

            if ( hasCaller[ global ] || caller == all.size() ||
                 selected[ caller ] )
            {
                continue;
            }

            if ( extras >= budget )
                break;

            selected[ caller ] = true;
            ranked.push_back( caller );
            ++extras;
        }
    }

    graph.truncated = ranked.size() != all.size();
    std::vector< u32 > remap( all.size(), UINT32_MAX );
    graph.nodes.reserve( ranked.size() );

    for ( usize i = 0; i < ranked.size(); ++i )
    {
        const usize source = ranked[ i ];
        remap[ source ] = static_cast< u32 >( i );
        graph.nodes.push_back( std::move( all[ source ] ) );
    }

    std::unordered_map< u64, UniverseLink_s > pairCounts;
    pairCounts.reserve( edges.size() );

    for ( const Edge_s edge : edges )
    {
        const u32 caller = remap[ edge.caller ];
        const u32 callee = remap[ edge.callee ];

        if ( caller == UINT32_MAX || callee == UINT32_MAX )
        {
            continue;
        }

        const u64 key = Universe_LinkKey( caller, callee );
        UniverseLink_s& link = pairCounts[ key ];
        link.caller = caller;
        link.callee = callee;

        if ( link.count != UINT32_MAX )
        {
            ++link.count;
        }

        if ( edge.callSite != 0 )
        {
            link.callSites.push_back( edge.callSite );
            link.targetAddresses.push_back( edge.targetAddress );
        }
    }

    graph.links.reserve( pairCounts.size() );

    for ( auto& pair : pairCounts )
    {
        UniverseLink_s& link = pair.second;
        std::vector< std::pair< u64, u64 > > sites;

        for ( usize i = 0; i < link.callSites.size(); ++i )
        {
            sites.emplace_back( link.callSites[ i ], link.targetAddresses[ i ] );
        }

        std::sort( sites.begin(), sites.end() );
        sites.erase( std::unique( sites.begin(), sites.end() ), sites.end() );
        link.callSites.clear();
        link.targetAddresses.clear();
        std::unordered_set< u64 > fields;

        for ( const std::pair< u64, u64 >& site : sites )
        {
            link.callSites.push_back( site.first );
            link.targetAddresses.push_back( site.second );
            fields.insert( site.second );
        }

        link.fieldCount = static_cast< u32 >(
            std::min< usize >( fields.size(), UINT32_MAX ) );

        graph.links.push_back( std::move( link ) );
    }

    std::sort( graph.links.begin(), graph.links.end(),
        [&graph]( const UniverseLink_s& a, const UniverseLink_s& b ) {
            const f32 strengthA = Math_UniverseLinkStrength( a.count,
                graph.nodes[ a.caller ].outgoing, graph.nodes[ a.callee ].incoming );

            const f32 strengthB = Math_UniverseLinkStrength( b.count,
                graph.nodes[ b.caller ].outgoing, graph.nodes[ b.callee ].incoming );

            if ( strengthA != strengthB )
                return strengthA > strengthB;

            if ( a.count != b.count )
                return a.count > b.count;

            if ( a.caller != b.caller )
                return a.caller < b.caller;

            return a.callee < b.callee;
        } );

    if ( graph.links.size() > maxLinks )
    {
        // Preserve a bounded number of connections to globals even when
        // higher-weight function calls dominate the link budget.
        const usize reserve = std::min< usize >(
            512u, maxLinks / 8u + ( maxLinks != 0 ? 1u : 0u ) );

        std::vector< bool > used( graph.links.size(), false );
        std::unordered_set< u32 > seenGlobals;
        std::vector< UniverseLink_s > kept;
        kept.reserve( maxLinks );

        for ( usize i = 0; i < graph.links.size() &&
              kept.size() < reserve; ++i )
        {
            const UniverseLink_s& link = graph.links[ i ];

            if ( graph.nodes[ link.callee ].kind == UNK_GLOBAL &&
                 seenGlobals.insert( link.callee ).second )
            {
                used[ i ] = true;
                kept.push_back( std::move( graph.links[ i ] ) );
            }
        }

        for ( usize i = 0; i < graph.links.size() &&
              kept.size() < maxLinks; ++i )
        {
            if ( !used[ i ] )
            {
                kept.push_back( std::move( graph.links[ i ] ) );
            }
        }

        graph.links = std::move( kept );
        graph.truncated = true;
    }

    Universe_Layout( graph.nodes, graph.links );
    graph.adaptiveLayout = true;
    return graph;
}

usize Universe_RankForAddress(
    const UniverseData_s& data, const u64 address,
    const bool distinctRanking, const bool globalFields )
{
    std::unordered_map< u64, usize > byAddress;
    byAddress.reserve( data.functions.size() );
    std::vector< UniverseNode_s > nodes;

    for ( const UniverseFunction_s& function : data.functions )
    {
        if ( byAddress.emplace( function.address, nodes.size() ).second )
        {
            UniverseNode_s node;
            node.address = function.address;
            nodes.push_back( node );
        }
    }

    const auto target = byAddress.find( address );

    if ( target == byAddress.end() )
    {
        return CANTORDUST_NO_SOURCE;
    }

    std::unordered_set< u64 > seenPairs;

    if ( distinctRanking )
        seenPairs.reserve( data.references.size() );

    std::unordered_map< u64, std::unordered_set< u64 > >
        seenFields;

    for ( const UniverseReference_s& ref : data.references )
    {
        const auto from = byAddress.find( ref.caller );
        const auto to = byAddress.find( ref.callee );

        if ( from == byAddress.end() || to == byAddress.end() )
            continue;

        const u64 key = Universe_LinkKey(
            static_cast< u32 >( from->second ),
            static_cast< u32 >( to->second ) );

        if ( globalFields &&
             data.functions[ to->second ].kind == UNK_GLOBAL )
        {
            const u64 target = ref.targetAddress != 0
                ? ref.targetAddress : ref.callee;

            if ( !seenFields[ key ].insert( target ).second )
                continue;
        }
        else if ( distinctRanking && !seenPairs.insert( key ).second )
        {
            continue;
        }
        ++nodes[ from->second ].outgoing;
        ++nodes[ to->second ].incoming;
    }

    const UniverseNode_s& selected = nodes[ target->second ];
    usize rank = 0;

    for ( const UniverseNode_s& node : nodes )
    {
        if ( node.address == selected.address )
            continue;

        if ( node.incoming > selected.incoming ||
             ( node.incoming == selected.incoming &&
               node.outgoing > selected.outgoing ) ||
             ( node.incoming == selected.incoming &&
               node.outgoing == selected.outgoing &&
               node.address < selected.address ) )
        {
            ++rank;
        }
    }

    return rank;
}

void Universe_SetCountMode( UniverseGraph_s& graph,
    const bool distinctFunctions, const bool globalFields )
{
    graph.distinctCountMode = distinctFunctions;
    graph.globalFieldMode = globalFields;
    // Call sites remain on links for address cycling; only heat/layout weights change.
    for ( UniverseNode_s& node : graph.nodes )
    {
        node.incoming = 0;
        node.outgoing = 0;
    }

    for ( UniverseLink_s& link : graph.links )
    {
        if ( link.caller >= graph.nodes.size() ||
             link.callee >= graph.nodes.size() )
        {
            continue;
        }

        const bool fields = globalFields &&
            graph.nodes[ link.callee ].kind == UNK_GLOBAL;

        const u32 weight = fields
            ? ( link.fieldCount != 0 ? link.fieldCount : 1u )
            : distinctFunctions ? 1u : link.count;

        link.renderWeight = weight;
        UniverseNode_s& caller = graph.nodes[ link.caller ];
        UniverseNode_s& callee = graph.nodes[ link.callee ];
        caller.outgoing = static_cast< u32 >( std::min< u64 >(
            static_cast< u64 >( caller.outgoing ) + weight, UINT32_MAX ) );

        callee.incoming = static_cast< u32 >( std::min< u64 >(
            static_cast< u64 >( callee.incoming ) + weight, UINT32_MAX ) );
    }

    if ( graph.focusIndex == CANTORDUST_NO_SOURCE )
    {
        Universe_Layout( graph.nodes, graph.links );
    }
    else
    {
        for ( UniverseLink_s& link : graph.links )
        {
            if ( link.caller < graph.nodes.size() &&
                 link.callee == graph.focusIndex )
            {
                graph.nodes[ link.caller ].focusCalls =
                    distinctFunctions ? 1u : link.count;
            }
        }
    }
}

u64 Universe_CallSiteAt(
    const UniverseGraph_s& graph, const u64 caller,
    const u64 callee, const usize ordinal )
{
    for ( const UniverseLink_s& link : graph.links )
    {
        if ( link.caller >= graph.nodes.size() ||
             link.callee >= graph.nodes.size() ||
             graph.nodes[ link.caller ].address != caller ||
             graph.nodes[ link.callee ].address != callee )
        {
            continue;
        }

        return link.callSites.empty() ? caller
            : link.callSites[ ordinal % link.callSites.size() ];
    }

    return caller;
}

UniverseGraph_s Universe_FilterGraph(
    const UniverseGraph_s& graph,
    const std::vector< UniverseAddressRange_s >& ranges )
{
    UniverseGraph_s filtered;
    filtered.candidateCount = graph.candidateCount;
    filtered.referenceCount = graph.referenceCount;
    filtered.truncated = graph.truncated;
    filtered.distinctCountMode = graph.distinctCountMode;
    filtered.absoluteHeatMaximum = graph.absoluteHeatMaximum;

    if ( ranges.empty() )
    {
        return filtered;
    }

    std::vector< u32 > remap( graph.nodes.size(), UINT32_MAX );

    for ( usize i = 0; i < graph.nodes.size(); ++i )
    {
        const UniverseNode_s& node = graph.nodes[ i ];
        const bool included = std::any_of( ranges.begin(), ranges.end(),
            [&node]( const UniverseAddressRange_s& range ) {
                return node.address >= range.start && node.address < range.end;
            } );

        if ( !included )
        {
            continue;
        }

        remap[ i ] = static_cast< u32 >( filtered.nodes.size() );
        filtered.nodes.push_back( node );
    }

    for ( const UniverseLink_s& link : graph.links )
    {
        if ( link.caller >= remap.size() || link.callee >= remap.size() ||
             remap[ link.caller ] == UINT32_MAX ||
             remap[ link.callee ] == UINT32_MAX )
        {
            continue;
        }

        filtered.links.push_back({
            remap[ link.caller ], remap[ link.callee ],
            link.count, link.callSites });
    }

    if ( filtered.nodes.empty() )
    {
        return filtered;
    }

    f32 lowX = filtered.nodes[ 0 ].x;
    f32 highX = lowX;
    f32 lowY = filtered.nodes[ 0 ].y;
    f32 highY = lowY;
    f32 lowZ = filtered.nodes[ 0 ].z;
    f32 highZ = lowZ;

    for ( const UniverseNode_s& node : filtered.nodes )
    {
        lowX = std::min( lowX, node.x );
        highX = std::max( highX, node.x );
        lowY = std::min( lowY, node.y );
        highY = std::max( highY, node.y );
        lowZ = std::min( lowZ, node.z );
        highZ = std::max( highZ, node.z );
    }

    const f32 centerX = ( lowX + highX ) * 0.5f;
    const f32 centerY = ( lowY + highY ) * 0.5f;
    const f32 centerZ = ( lowZ + highZ ) * 0.5f;

    for ( UniverseNode_s& node : filtered.nodes )
    {
        node.x -= centerX;
        node.y -= centerY;
        node.z -= centerZ;
    }

    filtered.adaptiveLayout = true;
    return filtered;
}

UniverseGraph_s Universe_FilterGraphRangeHeat(
    const UniverseGraph_s& graph,
    const std::vector< UniverseAddressRange_s >& ranges,
    const bool distinctFunctions, const bool globalFields )
{
    UniverseGraph_s local;
    local.candidateCount = graph.candidateCount;
    local.referenceCount = graph.referenceCount;
    local.truncated = graph.truncated;
    local.distinctCountMode = distinctFunctions;
    local.globalFieldMode = globalFields;
    local.absoluteHeatMaximum = graph.absoluteHeatMaximum;

    if ( ranges.empty() )
    {
        return local;
    }

    const auto inside = [&ranges]( const u64 address ) {
        return std::any_of( ranges.begin(), ranges.end(),
            [address]( const UniverseAddressRange_s& range ) {
                return address >= range.start && address < range.end;
            } );
    };

    std::vector< bool > present( graph.nodes.size(), false );

    for ( usize i = 0; i < graph.nodes.size(); ++i )
    {
        present[ i ] = inside( graph.nodes[ i ].address );
    }
    // A call instruction inside the range makes both ends relevant,
    // even if one function's entry address is just outside the window.
    for ( const UniverseLink_s& link : graph.links )
    {
        if ( link.caller >= graph.nodes.size() ||
             link.callee >= graph.nodes.size() )
        {
            continue;
        }

        if ( std::any_of( link.callSites.begin(), link.callSites.end(), inside ) )
        {
            present[ link.caller ] = true;
            present[ link.callee ] = true;
        }
    }

    std::vector< u32 > remap( graph.nodes.size(), UINT32_MAX );

    for ( usize i = 0; i < graph.nodes.size(); ++i )
    {
        if ( !present[ i ] )
            continue;

        remap[ i ] = static_cast< u32 >( local.nodes.size() );
        UniverseNode_s node = graph.nodes[ i ];
        node.incoming = 0;
        node.outgoing = 0;
        local.nodes.push_back( std::move( node ) );
    }

    for ( const UniverseLink_s& link : graph.links )
    {
        if ( link.caller >= remap.size() || link.callee >= remap.size() ||
             remap[ link.caller ] == UINT32_MAX ||
             remap[ link.callee ] == UINT32_MAX )
        {
            continue;
        }

        UniverseLink_s copy;
        copy.caller = remap[ link.caller ];
        copy.callee = remap[ link.callee ];
        std::unordered_set< u64 > fields;

        for ( usize i = 0; i < link.callSites.size(); ++i )
        {
            if ( !inside( link.callSites[ i ] ) )
                continue;

            copy.callSites.push_back( link.callSites[ i ] );

            const u64 target = i < link.targetAddresses.size()
                ? link.targetAddresses[ i ] : graph.nodes[ link.callee ].address;

            copy.targetAddresses.push_back( target );
            fields.insert( target );
        }

        if ( copy.callSites.empty() )
            continue;

        copy.fieldCount = static_cast< u32 >(
            std::min< usize >( fields.size(), UINT32_MAX ) );

        const bool fieldMode = globalFields &&
            graph.nodes[ link.callee ].kind == UNK_GLOBAL;

        copy.count = fieldMode ? copy.fieldCount : distinctFunctions ? 1u
            : static_cast< u32 >(
                std::min< usize >( copy.callSites.size(), UINT32_MAX ) );

        copy.renderWeight = copy.count;
        UniverseNode_s& caller = local.nodes[ copy.caller ];
        UniverseNode_s& callee = local.nodes[ copy.callee ];
        caller.outgoing = static_cast< u32 >(
            std::min< u64 >(
                static_cast< u64 >( caller.outgoing ) + copy.count,
                UINT32_MAX ) );

        callee.incoming = static_cast< u32 >(
            std::min< u64 >(
                static_cast< u64 >( callee.incoming ) + copy.count,
                UINT32_MAX ) );

        local.links.push_back( std::move( copy ) );
    }

    Universe_Layout( local.nodes, local.links );
    local.adaptiveLayout = true;
    return local;
}

static void Universe_LayoutFocusedCallers( UniverseGraph_s& focus )
{
    if ( focus.nodes.size() <= 1u )
    {
        return;
    }

    const u64 center = focus.nodes.front().address;
    u64 minimumGap = UINT64_MAX;
    u64 maximumGap = 0;

    for ( usize i = 1; i < focus.nodes.size(); ++i )
    {
        const u64 gap = Math_UniverseAddressGap(
            focus.nodes[ i ].address, center );

        minimumGap = std::min( minimumGap, gap );
        maximumGap = std::max( maximumGap, gap );
    }

    const usize count = focus.nodes.size() - 1u;

    for ( usize i = 1; i < focus.nodes.size(); ++i )
    {
        UniverseNode_s& node = focus.nodes[ i ];

        const u64 gap = Math_UniverseAddressGap(
            node.address, center );

        const f32 radius = Math_UniverseFocusDistance(
            gap, minimumGap, maximumGap );

        const UniversePosition_s position = Math_UniverseFocusPosition(
            i - 1u, count, node.address, radius );

        node.x = position.x;
        node.y = position.y;
        node.z = position.z;
    }
}

static UniverseGraph_s Universe_FocusCallersImpl(
    const UniverseGraph_s& graph, const u64 address,
    const std::vector< UniverseAddressRange_s >* const ranges )
{
    UniverseGraph_s focus;

    const auto center = std::find_if( graph.nodes.begin(), graph.nodes.end(),
        [address]( const UniverseNode_s& node ) {
            return node.address == address;
        } );

    if ( center == graph.nodes.end() )
    {
        return focus;
    }

    const u32 centerIndex = static_cast< u32 >(
        std::distance( graph.nodes.begin(), center ) );

    focus.focusIndex = 0;
    focus.candidateCount = graph.candidateCount;
    focus.referenceCount = graph.referenceCount;
    focus.truncated = graph.truncated;
    focus.distinctCountMode = graph.distinctCountMode;
    focus.absoluteHeatMaximum = graph.absoluteHeatMaximum;
    focus.nodes.push_back( *center );
    focus.nodes.front().x = 0;
    focus.nodes.front().y = 0;
    focus.nodes.front().z = 0;

    std::vector< UniverseLink_s > incoming;

    for ( const UniverseLink_s& link : graph.links )
    {
        if ( link.callee == centerIndex && link.caller != centerIndex &&
             link.caller < graph.nodes.size() )
        {
            if ( ranges == nullptr )
            {
                incoming.push_back( link );
                continue;
            }

            UniverseLink_s filtered = link;
            filtered.callSites.clear();

            for ( const u64 site : link.callSites )
            {
                if ( std::any_of( ranges->begin(), ranges->end(),
                    [site]( const UniverseAddressRange_s& range ) {
                        return site >= range.start && site < range.end;
                    } ) )
                {
                    filtered.callSites.push_back( site );
                }
            }

            if ( !filtered.callSites.empty() )
            {
                filtered.count = static_cast< u32 >(
                    filtered.callSites.size() );

                incoming.push_back( std::move( filtered ) );
            }
        }
    }

    std::sort( incoming.begin(), incoming.end(),
        [&graph]( const UniverseLink_s& a, const UniverseLink_s& b ) {
            if ( a.count != b.count )
                return a.count > b.count;

            return graph.nodes[ a.caller ].address <
                graph.nodes[ b.caller ].address;
        } );

    focus.nodes.reserve( incoming.size() + 1 );
    focus.links.reserve( incoming.size() );

    for ( usize i = 0; i < incoming.size(); ++i )
    {
        const UniverseLink_s& link = incoming[ i ];
        UniverseNode_s node = graph.nodes[ link.caller ];
        node.focusCalls = link.count;
        focus.nodes.push_back( std::move( node ) );
        focus.links.push_back({
            static_cast< u32 >( i + 1u ), 0u,
            link.count, link.callSites });
    }

    Universe_LayoutFocusedCallers( focus );
    return focus;
}

UniverseGraph_s Universe_FocusCallers(
    const UniverseGraph_s& graph, const u64 address )
{
    return Universe_FocusCallersImpl( graph, address, nullptr );
}

UniverseGraph_s Universe_FocusCallersInRanges(
    const UniverseGraph_s& graph, const u64 address,
    const std::vector< UniverseAddressRange_s >& ranges )
{
    return Universe_FocusCallersImpl( graph, address, &ranges );
}

static UniverseGraph_s Universe_FocusCallersFromDataImpl(
    const UniverseData_s& data, const u64 address,
    const std::vector< UniverseAddressRange_s >* const ranges,
    const bool distinctCenter, const bool globalFields )
{
    UniverseGraph_s focus;
    focus.candidateCount = data.functions.size();
    focus.referenceCount = data.references.size();
    focus.truncated = data.truncated;

    const auto center = std::find_if( data.functions.begin(),
        data.functions.end(), [address]( const UniverseFunction_s& node ) {
            return node.address == address;
        } );

    if ( center == data.functions.end() )
    {
        return focus;
    }

    focus.focusIndex = 0;
    UniverseNode_s root;
    root.address = center->address;
    root.name = center->name;
    root.kind = center->kind;
    focus.nodes.push_back( std::move( root ) );

    std::unordered_map< u64, const UniverseFunction_s* > functions;
    functions.reserve( data.functions.size() );

    for ( const UniverseFunction_s& node : data.functions )
    {
        functions.emplace( node.address, &node );
    }

    struct Caller_s
    {
        const UniverseFunction_s* function = nullptr;
        u32 count = 0;
        std::vector< u64 > sites;
    };
    std::unordered_map< u64, Caller_s > callers;
    std::unordered_set< u64 > allCallers;
    std::unordered_map< u64, std::unordered_set< u64 > >
        accessedFields;

    const auto inRange = [ranges]( const u64 site ) {
        return ranges == nullptr || std::any_of( ranges->begin(),
            ranges->end(), [site]( const UniverseAddressRange_s& range ) {
                return site >= range.start && site < range.end;
            } );
    };

    for ( const UniverseReference_s& reference : data.references )
    {
        if ( reference.callee != address )
        {
            continue;
        }

        const auto from = functions.find( reference.caller );

        if ( from == functions.end() )
        {
            continue;
        }

        allCallers.insert( reference.caller );

        if ( globalFields && center->kind == UNK_GLOBAL )
        {
            accessedFields[ reference.caller ].insert(
                reference.targetAddress != 0 ? reference.targetAddress : address );
        }

        if ( !distinctCenter && !( globalFields && center->kind == UNK_GLOBAL ) &&
             focus.nodes.front().incoming != UINT32_MAX )
        {
            ++focus.nodes.front().incoming;
        }

        if ( reference.caller == address || !inRange( reference.callSite ) )
        {
            continue;
        }

        Caller_s& caller = callers[ reference.caller ];
        caller.function = from->second;

        if ( caller.count != UINT32_MAX )
        {
            ++caller.count;
        }

        if ( reference.callSite != 0 )
        {
            caller.sites.push_back( reference.callSite );
        }
    }

    if ( globalFields && center->kind == UNK_GLOBAL )
    {
        u64 count = 0;

        for ( const auto& caller : accessedFields )
            count += caller.second.size();
        focus.nodes.front().incoming = static_cast< u32 >(
            std::min< u64 >( count, UINT32_MAX ) );
    }
    else if ( distinctCenter )
    {
        focus.nodes.front().incoming = static_cast< u32 >(
            std::min< usize >( allCallers.size(), UINT32_MAX ) );
    }

    std::vector< Caller_s > ordered;
    ordered.reserve( callers.size() );

    for ( auto& pair : callers )
    {
        Caller_s& caller = pair.second;
        std::sort( caller.sites.begin(), caller.sites.end() );
        caller.sites.erase( std::unique( caller.sites.begin(),
            caller.sites.end() ), caller.sites.end() );

        ordered.push_back( std::move( caller ) );
    }

    std::sort( ordered.begin(), ordered.end(), []( const Caller_s& a,
        const Caller_s& b ) {
        return a.count != b.count ? a.count > b.count :
            a.function->address < b.function->address;
    } );

    const usize keep = std::min( ordered.size(),
        std::min< usize >( CANTORDUST_UNIVERSE_MAX_FUNCTIONS - 1u,
            CANTORDUST_UNIVERSE_MAX_LINKS ) );

    focus.truncated |= keep != ordered.size();
    focus.nodes.reserve( keep + 1 );
    focus.links.reserve( keep );

    for ( usize i = 0; i < keep; ++i )
    {
        Caller_s& source = ordered[ i ];
        UniverseNode_s node;
        node.address = source.function->address;
        node.name = source.function->name;
        node.kind = source.function->kind;
        node.focusCalls = source.count;
        focus.nodes.push_back( std::move( node ) );
        focus.links.push_back({ static_cast< u32 >( i + 1u ),
            0u, source.count, std::move( source.sites ) });
    }

    Universe_LayoutFocusedCallers( focus );
    return focus;
}

UniverseGraph_s Universe_FocusCallersFromData(
    const UniverseData_s& data, const u64 address,
    const bool distinctCenter, const bool globalFields )
{
    return Universe_FocusCallersFromDataImpl(
        data, address, nullptr, distinctCenter, globalFields );
}

UniverseGraph_s Universe_FocusCallersInRangesFromData(
    const UniverseData_s& data, const u64 address,
    const std::vector< UniverseAddressRange_s >& ranges,
    const bool distinctCenter, const bool globalFields )
{
    return Universe_FocusCallersFromDataImpl(
        data, address, &ranges, distinctCenter, globalFields );
}

} // namespace cantordust
