#include "pch/core_pch.h"
#include "cantordust/universe.h"
#include "cantordust/scalar_types.h"

namespace cantordust
{

UniverseInspectorIndex_s Universe_BuildInspectorIndex( const UniverseData_s& data )
{
    UniverseInspectorIndex_s index;
    index.nodes.reserve( data.functions.size() );

    for ( const UniverseFunction_s& function : data.functions )
    {
        UniverseInspectorNode_s& node = index.nodes[ function.address ];
        node.address = function.address;
        node.name = function.name;
        node.kind = function.kind;
    }

    for ( usize i = 0; i < data.references.size(); ++i )
    {
        const UniverseReference_s& ref = data.references[ i ];
        // Only retained endpoints; no fabricated functions / references.
        const auto caller = index.nodes.find( ref.caller );
        const auto callee = index.nodes.find( ref.callee );

        if ( caller == index.nodes.end() || callee == index.nodes.end() )
            continue;

        caller->second.outgoingReferences.push_back( i );
        callee->second.incomingReferences.push_back( i );
    }

    return index;
}

UniverseInspectorResult_s Universe_InspectFunction(
    const UniverseData_s& data, const UniverseInspectorIndex_s& index,
    const u64 address )
{
    UniverseInspectorResult_s result;

    const auto it = index.nodes.find( address );

    if ( it == index.nodes.end() )
        return result;

    const UniverseInspectorNode_s& node = it->second;
    result.address = address;
    result.name = node.name;
    result.kind = node.kind;
    result.found = true;
    result.incomingReferences = node.incomingReferences.size();
    result.outgoingReferences = node.outgoingReferences.size();

    // Group by *direction* and peer: reciprocal links are two distinct
    // entries, and each group retains every real callsite, including duplicates
    // present in the extracted dataset.
    std::unordered_map< u64, usize > incoming;
    std::unordered_map< u64, usize > outgoing;

    const auto add = [&]( const usize refIndex, const bool isIncoming,
        std::unordered_map< u64, usize >& byPeer ) {
        if ( refIndex >= data.references.size() )
            return;

        const UniverseReference_s& ref = data.references[ refIndex ];
        const u64 peer = isIncoming ? ref.caller : ref.callee;
        auto position = byPeer.find( peer );

        if ( position == byPeer.end() )
        {
            const auto peerIt = index.nodes.find( peer );

            if ( peerIt == index.nodes.end() )
                return;

            const usize next = result.connections.size();
            position = byPeer.emplace( peer, next ).first;
            UniverseInspectorConnection_s connection;
            connection.peerAddress = peer;
            connection.peerName = peerIt->second.name;
            connection.peerKind = peerIt->second.kind;
            connection.incoming = isIncoming;
            result.connections.push_back( std::move( connection ) );
        }

        result.connections[ position->second ].callSites.push_back(
            ref.callSite );
    };

    for ( const usize i : node.incomingReferences )
        add( i, true, incoming );

    for ( const usize i : node.outgoingReferences )
        add( i, false, outgoing );

    std::sort( result.connections.begin(), result.connections.end(),
        []( const UniverseInspectorConnection_s& a,
            const UniverseInspectorConnection_s& b ) {
            if ( a.incoming != b.incoming )
                return a.incoming > b.incoming;

            if ( a.callSites.size() != b.callSites.size() )
                return a.callSites.size() > b.callSites.size();

            return a.peerAddress < b.peerAddress;
        } );

    for ( UniverseInspectorConnection_s& connection : result.connections )
        std::sort( connection.callSites.begin(), connection.callSites.end() );

    return result;
}

UniverseInspectorResult_s Universe_InspectGraphFunction(
    const UniverseGraph_s& graph, const u64 address )
{
    UniverseInspectorResult_s result;

    const auto nodeIt = std::find_if( graph.nodes.begin(), graph.nodes.end(),
        [address]( const UniverseNode_s& node ) {
            return node.address == address;
        } );

    if ( nodeIt == graph.nodes.end() )
        return result;

    result.found = true;
    result.address = address;
    result.name = nodeIt->name;
    result.kind = nodeIt->kind;

    // Graph links can have counts aggregated in distinct-caller or field
    // modes. Keep the actual retained callsite addresses, not those weights.
    std::unordered_map< u64, usize > incoming;
    std::unordered_map< u64, usize > outgoing;

    const auto add = [&]( const UniverseNode_s& peer,
        const std::vector< u64 >& sites, const bool isIncoming,
        std::unordered_map< u64, usize >& byPeer ) {
        auto position = byPeer.find( peer.address );

        if ( position == byPeer.end() )
        {
            const usize next = result.connections.size();
            position = byPeer.emplace( peer.address, next ).first;
            UniverseInspectorConnection_s connection;
            connection.peerAddress = peer.address;
            connection.peerName = peer.name;
            connection.peerKind = peer.kind;
            connection.incoming = isIncoming;
            result.connections.push_back( std::move( connection ) );
        }

        std::vector< u64 >& dest = result.connections[ position->second ].callSites;
        dest.insert( dest.end(), sites.begin(), sites.end() );

        if ( isIncoming )
            result.incomingReferences += sites.size();

        else
            result.outgoingReferences += sites.size();
    };

    for ( const UniverseLink_s& link : graph.links )
    {
        if ( link.caller >= graph.nodes.size() ||
             link.callee >= graph.nodes.size() )
        {
            continue;
        }

        const UniverseNode_s& caller = graph.nodes[ link.caller ];
        const UniverseNode_s& callee = graph.nodes[ link.callee ];

        if ( callee.address == address )
            add( caller, link.callSites, true, incoming );

        if ( caller.address == address )
            add( callee, link.callSites, false, outgoing );
    }

    std::sort( result.connections.begin(), result.connections.end(),
        []( const UniverseInspectorConnection_s& a,
            const UniverseInspectorConnection_s& b ) {
            if ( a.incoming != b.incoming )
                return a.incoming > b.incoming;

            if ( a.callSites.size() != b.callSites.size() )
                return a.callSites.size() > b.callSites.size();

            return a.peerAddress < b.peerAddress;
        } );

    for ( UniverseInspectorConnection_s& connection : result.connections )
        std::sort( connection.callSites.begin(), connection.callSites.end() );

    return result;
}

} // namespace cantordust
