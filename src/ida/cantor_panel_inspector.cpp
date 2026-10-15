#include "pch/ida_pch.h"
#include "ida/cantor_panel.h"
#include "ida/cantor_panel_detail.h"
#include "cantordust/scalar_types.h"

#include "ida/ida_memory.h"
#include "ida/ida_universe.h"
#include "math/window.h"
#include "math/zoom.h"
#include "math/entropy.h"
#include "math/hilbert.h"
#include "cantordust/renderer.h"

#include <kernwin.hpp>
#include <funcs.hpp>
#include <bytes.hpp>
#include <xref.hpp>
#include <name.hpp>
#include <segment.hpp>
#include <netnode.hpp>

#include <cstdlib>

namespace cantordust
{

void CantorPanel::ClearInspectorConnections()
{
    // Delete the actual row widgets, not merely their layout items. Each
    // group owns its callsite widgets, so repeated selections are leak-free.
    while ( QLayoutItem* const entry = m_inspectorConnectionLayout->takeAt( 0 ) )
    {
        delete entry->widget();
        delete entry;
    }

    m_inspectorConnections->verticalScrollBar()->setValue( 0 );
}

void CantorPanel::AddInspectorMessage( const QString& message )
{
    QLabel* const label = new QLabel( message, m_inspectorConnectionContent );
    label->setObjectName( "cantorInspectorMessage" );
    label->setTextFormat( Qt::PlainText );
    label->setWordWrap( true );
    label->setMinimumWidth( 0 );
    label->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
    m_inspectorConnectionLayout->addWidget( label );
}

void CantorPanel::HideUniverseInspector()
{
    m_inspectorAddress.reset();
    m_inspectorResult = {};
    m_universeInspector->hide();
    ClearInspectorConnections();
}

void CantorPanel::UpdateUniverseInspector( const u64 address )
{
    if ( !m_universeLoaded )
        return;

    m_inspectorResult = Universe_InspectFunction(
        m_universeData, m_inspectorIndex, address );

    bool visibleFallback = false;

    if ( m_inspectorResult.connections.empty() )
    {
        // If the full index is stale or lacks a selected graph edge, avoid
        // an empty inspector when exact callsites still exist on the canvas.
        // Prefer full extraction whenever it has actual relationships.
        const UniverseInspectorResult_s detail =
            Universe_InspectGraphFunction( m_detailUniverseGraph, address );

        const UniverseInspectorResult_s overview =
            Universe_InspectGraphFunction( m_universeGraph, address );

        const UniverseInspectorResult_s* const available =
            !detail.connections.empty() ? &detail
            : !overview.connections.empty() ? &overview : nullptr;

        if ( available != nullptr )
        {
            m_inspectorResult = *available;
            visibleFallback = true;
        }
    }

    if ( !m_inspectorResult.found )
    {
        // Never show an unexplained blank pane for a graph node that the
        // inspector index cannot resolve (e.g. an out-of-date extraction).
        m_inspectorAddress = address;
        m_inspectorTitle->setText( QString( "Inspector · %1" )
            .arg( Ida_FormatAddress( static_cast< ea_t >( address ) ) ) );

        m_inspectorSummary->setText(
            "This node is not in the current extracted reference index. "
            "Use Refresh to rescan the database." );

        ClearInspectorConnections();
        AddInspectorMessage( "No indexed function or global record." );
        m_universeInspector->show();
        return;
    }

    m_inspectorAddress = address;

    const bool global = m_inspectorResult.kind == UNK_GLOBAL;
    m_inspectorTitle->setText( QString( "Inspector · %1 · %2%3" )
        .arg( QString::fromUtf8( m_inspectorResult.name.c_str() ) )
        .arg( Ida_FormatAddress( static_cast< ea_t >( address ) ) )
        .arg( global ? QStringLiteral( " · Global" ) : QString() ) );

    usize incomingPeers = 0;
    usize outgoingPeers = 0;

    for ( const UniverseInspectorConnection_s& connection : m_inspectorResult.connections )
    {
        if ( connection.incoming )
            ++incomingPeers;

        else
            ++outgoingPeers;
    }

    QString summary = QString( "%1: %2 incoming (%3 peers) | "
        "%4 outgoing (%5 peers)" )
        .arg( visibleFallback ? QStringLiteral( "Visible graph only" )
                               : QStringLiteral( "Extracted" ) )
        .arg( static_cast< qulonglong >( m_inspectorResult.incomingReferences ) )
        .arg( static_cast< qulonglong >( incomingPeers ) )
        .arg( static_cast< qulonglong >( m_inspectorResult.outgoingReferences ) )
        .arg( static_cast< qulonglong >( outgoingPeers ) );

    if ( !global )
    {
        if ( const func_t* const function = get_func( static_cast< ea_t >( address ) ) )
        {
            // A function may have non-contiguous tails. Report exactly
            // what these entry-chunk bounds measure, not a total size.
            summary += QString( " | Entry chunk: %1 B" )
                .arg( static_cast< qulonglong >(
                    function->end_ea - function->start_ea ) );
        }
    }

    if ( visibleFallback )
        summary += " | Indexed relationships unavailable; showing visible links";

    if ( m_universeData.truncated )
        summary += " | Scan truncated";

    m_inspectorSummary->setText( summary );
    m_inspectorSummary->setToolTip( summary );
    ClearInspectorConnections();

    constexpr usize maxVisibleConnections = 500;
    const usize shown = std::min(
        maxVisibleConnections, m_inspectorResult.connections.size() );

    for ( usize i = 0; i < shown; ++i )
    {
        const UniverseInspectorConnection_s& connection =
            m_inspectorResult.connections[ i ];

        QWidget* const group = new QWidget( m_inspectorConnectionContent );
        group->setMinimumWidth( 0 );
        QVBoxLayout* const groupLayout = new QVBoxLayout( group );
        groupLayout->setContentsMargins( 0, 0, 0, 0 );
        groupLayout->setSpacing( 1 );
        QToolButton* const button = new QToolButton( group );
        button->setObjectName( "cantorInspectorConnection" );
        button->setText( QString( "%1 %2 · %3 · %4 site(s)" )
            .arg( connection.incoming ? "Incoming from" : "Outgoing to" )
            .arg( QString::fromUtf8( connection.peerName.c_str() ) )
            .arg( Ida_FormatAddress( static_cast< ea_t >( connection.peerAddress ) ) )
            .arg( static_cast< qulonglong >( connection.callSites.size() ) ) );

        button->setToolTip( button->text() +
            "\nClick to expand; callsite buttons jump directly to IDA." );

        button->setCheckable( true );
        button->setArrowType( Qt::RightArrow );
        button->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        button->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Fixed );
        button->setMinimumWidth( 0 );
        groupLayout->addWidget( button );
        QWidget* const sitesPanel = new QWidget( group );
        QVBoxLayout* const sitesLayout = new QVBoxLayout( sitesPanel );
        sitesLayout->setContentsMargins( 14, 0, 0, 1 );
        sitesLayout->setSpacing( 1 );
        sitesPanel->hide();
        groupLayout->addWidget( sitesPanel );
        QObject::connect( button, &QToolButton::toggled,
            this, [this, button, sitesPanel, sitesLayout, i]( const bool expanded ) {
                button->setArrowType( expanded ? Qt::DownArrow : Qt::RightArrow );

                if ( expanded && sitesLayout->count() == 0 &&
                     i < m_inspectorResult.connections.size() )
                {
                    const std::vector< u64 >& sites = m_inspectorResult.connections[ i ].callSites;
                    constexpr usize maxVisibleSites = 1000;
                    const usize siteCount = std::min( sites.size(), maxVisibleSites );

                    for ( usize j = 0; j < siteCount; ++j )
                    {
                        const u64 site = sites[ j ];
                        QPushButton* const siteButton = new QPushButton(
                            site == 0 ? QStringLiteral( "Unknown callsite" )
                                      : QString( "Callsite %1" ).arg(
                                          Ida_FormatAddress( static_cast< ea_t >( site ) ) ),
                            sitesPanel );

                        siteButton->setObjectName( "cantorInspectorCallsite" );
                        siteButton->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Fixed );
                        siteButton->setMinimumWidth( 0 );
                        siteButton->setEnabled( site != 0 );
                        siteButton->setToolTip( "Click to navigate to this callsite in IDA." );
                        QObject::connect( siteButton, &QPushButton::clicked,
                            this, [this, site]() { NavigateUniverse( site ); } );

                        sitesLayout->addWidget( siteButton );
                    }

                    if ( sites.size() > siteCount )
                    {
                        QLabel* const more = new QLabel( QString( "%1 more callsites "
                            "(display limit)" ).arg( static_cast< qulonglong >(
                                sites.size() - siteCount ) ), sitesPanel );

                        more->setObjectName( "cantorInspectorMessage" );
                        sitesLayout->addWidget( more );
                    }

                    if ( sites.empty() )
                    {
                        QLabel* const none = new QLabel(
                            "No retained callsite addresses.", sitesPanel );

                        none->setObjectName( "cantorInspectorMessage" );
                        sitesLayout->addWidget( none );
                    }
                }

                sitesPanel->setVisible( expanded );
            } );

        m_inspectorConnectionLayout->addWidget( group );
    }

    if ( shown == 0 )
    {
        AddInspectorMessage( m_universeData.truncated
            ? "No retained references for this node (scan was truncated)."
            : "No incoming/outgoing references in the extracted data." );
    }

    if ( shown < m_inspectorResult.connections.size() )
    {
        AddInspectorMessage( QString( "%1 more connections (display limit)" )
            .arg( static_cast< qulonglong >(
                m_inspectorResult.connections.size() - shown ) ) );
    }

    m_universeInspector->show();
}

void CantorPanel::SelectUniverseInspector( const u64 address )
{
    QToolTip::hideText();
    m_lastHoveredAddress.reset();
    LoadUniverse();
    UpdateUniverseInspector( address );
}

void CantorPanel::HoverUniverse( const usize index,
    const bool overview )
{
    const UniverseGraph_s& graph = overview
        ? m_overviewFilteredGraph : m_detailUniverseGraph;

    if ( index >= graph.nodes.size() )
    {
        m_lastHoveredAddress.reset();
        QToolTip::hideText();
        m_hintLabel->setText(
            "Universe: right-click to focus; Shift + right-click toggles "
            "branches (Hops 2-10). Callers and callees use the active palette." );

        return;
    }

    const UniverseNode_s& node = graph.nodes[ index ];

    if ( graph.communityMode && node.kind == UNK_COMMUNITY )
    {
        if ( m_lastHoveredAddress != node.address ||
             m_lastHoverWasOverview != overview )
        {
            m_lastHoveredAddress = node.address;
            m_lastHoverWasOverview = overview;
            QToolTip::showText( QCursor::pos(),
                QString( "%1\n%2 functions\nRight-click: browse members" )
                    .arg( QString::fromStdString( node.name ) )
                    .arg( node.communitySize ),
                overview ? static_cast< QWidget* >( m_overviewUniverse )
                         : static_cast< QWidget* >( m_universePlot ) );
        }

        m_hintLabel->setText( QString( "%1 | %2 members | "
            "Right-click to choose a function" )
            .arg( QString::fromStdString( node.name ) )
            .arg( node.communitySize ) );

        return;
    }

    if ( m_lastHoveredAddress != node.address ||
         m_lastHoverWasOverview != overview )
    {
        m_lastHoveredAddress = node.address;
        m_lastHoverWasOverview = overview;

        const auto it = m_inspectorIndex.nodes.find( node.address );

        if ( it != m_inspectorIndex.nodes.end() )
        {
            const UniverseInspectorNode_s& record = it->second;
            const QString tooltip = QString( "%1 (%2)\n%3\n"
                "Extracted: %4 incoming · %5 outgoing\n"
                "Left-click: jump | Ctrl+left-click: inspect\n"
                "Expand inspector connections to navigate callsites" )
                .arg( QString::fromUtf8( record.name.c_str() ) )
                .arg( record.kind == UNK_GLOBAL ? "Global" : "Function" )
                .arg( Ida_FormatAddress( static_cast< ea_t >( node.address ) ) )
                .arg( static_cast< qulonglong >( record.incomingReferences.size() ) )
                .arg( static_cast< qulonglong >( record.outgoingReferences.size() ) );

            QToolTip::showText( QCursor::pos(), tooltip,
                overview ? static_cast< QWidget* >( m_overviewUniverse )
                         : static_cast< QWidget* >( m_universePlot ) );
        }
    }

    if ( graph.pathMode )
    {
        m_hintLabel->setText( QString( "Call path: %1 @ 0x%2 | "
            "Left-click to jump to outgoing callsite" )
            .arg( QString::fromUtf8( node.name.c_str() ) )
            .arg( static_cast< qulonglong >( node.address ), 0, 16 ) );

        return;
    }

    QString focusedInfo;

    if ( graph.focusIndex != CANTORDUST_NO_SOURCE &&
         graph.focusIndex != index )
    {
        const QString direction = node.focusDirection == 2
            ? QStringLiteral( "from parent" )
            : node.focusDirection == 3 ? QStringLiteral( "reciprocal" )
            : QStringLiteral( "to parent" );

        focusedInfo = QString( " | Hop %1 | %2 references %3%4" )
            .arg( static_cast< int >( node.focusDepth ) )
            .arg( node.focusCalls ).arg( direction )
            .arg( m_universeCollapsedNodes.count( node.address )
                ? QStringLiteral( " | Branch collapsed" ) : QString() );
    }

    u64 incoming = node.incoming;
    u64 outgoing = node.outgoing;

    if ( graph.focusIndex != CANTORDUST_NO_SOURCE &&
         graph.focusIndex != index )
    {
        // Bidirectional and deeper focus nodes store their reference count
        // on graph edges. Show real visible incoming/outgoing totals.
        incoming = outgoing = 0;

        for ( const UniverseLink_s& link : graph.links )
        {
            if ( link.callee == index )
                incoming += link.count;

            if ( link.caller == index )
                outgoing += link.count;
        }
    }

    m_hintLabel->setText(
        QString( "%1 @ 0x%2 | %3 visible incoming | %4 visible outgoing%5" )
            .arg( QString::fromUtf8( node.name.c_str() ) )
            .arg( static_cast< qulonglong >( node.address ), 0, 16 )
            .arg( static_cast< qulonglong >( incoming ) )
            .arg( static_cast< qulonglong >( outgoing ) )
            .arg( focusedInfo ) );
}

void CantorPanel::NavigateUniverse( const u64 address )
{
    jumpto( static_cast< ea_t >( address ) );
}

void CantorPanel::NavigateUniverseCaller( const u64 address )
{
    if ( !m_universeActivePath.addresses.empty() )
    {
        const auto iter = std::find( m_universeActivePath.addresses.begin(),
            m_universeActivePath.addresses.end(), address );

        if ( iter != m_universeActivePath.addresses.end() )
        {
            const usize step = static_cast< usize >(
                std::distance( m_universeActivePath.addresses.begin(), iter ) );

            if ( step < m_universeActivePath.callSites.size() &&
                 m_universeActivePath.callSites[ step ] != 0 )
            {
                NavigateUniverse( m_universeActivePath.callSites[ step ] );
                return;
            }
        }

        NavigateUniverse( address );
        return;
    }

    if ( !m_universeFocusAddress.has_value() ||
         address == *m_universeFocusAddress )
    {
        NavigateUniverse( address );
        return;
    }

    usize& ordinal = m_callerClickOrdinals[ address ];

    const auto node = std::find_if( m_detailUniverseGraph.nodes.begin(),
        m_detailUniverseGraph.nodes.end(), [address]( const UniverseNode_s& n ) {
            return n.address == address;
        } );

    const bool outgoing = node != m_detailUniverseGraph.nodes.end() &&
        node->focusDirection == 2;

    const u64 parentAddress = node != m_detailUniverseGraph.nodes.end() &&
        node->focusParent < m_detailUniverseGraph.nodes.size()
        ? m_detailUniverseGraph.nodes[ node->focusParent ].address
        : *m_universeFocusAddress;

    u64 site = 0;

    if ( node != m_detailUniverseGraph.nodes.end() &&
         node->focusDirection == 3 )
    {
        // Reciprocal nodes cycle both the incoming and outgoing callsites.
        std::vector< u64 > sites;

        for ( const UniverseLink_s& link : m_detailUniverseGraph.links )
        {
            if ( link.caller >= m_detailUniverseGraph.nodes.size() ||
                 link.callee >= m_detailUniverseGraph.nodes.size() )
            {
                continue;
            }

            const u64 caller =
                m_detailUniverseGraph.nodes[ link.caller ].address;

            const u64 callee =
                m_detailUniverseGraph.nodes[ link.callee ].address;

            if ( ( caller == address &&
                   callee == parentAddress ) ||
                 ( caller == parentAddress &&
                   callee == address ) )
            {
                sites.insert( sites.end(), link.callSites.begin(),
                    link.callSites.end() );
            }
        }

        std::sort( sites.begin(), sites.end() );

        if ( !sites.empty() )
            site = sites[ ordinal % sites.size() ];
    }
    else
    {
        site = outgoing
            ? Universe_CallSiteAt( m_detailUniverseGraph,
                parentAddress, address, ordinal )
            : Universe_CallSiteAt( m_detailUniverseGraph,
                address, parentAddress, ordinal );
    }

    if ( site == 0 )
        site = address;

    if ( jumpto( static_cast< ea_t >( site ) ) )
    {
        ++ordinal;
    }
}

void CantorPanel::NavigateUniverseOverview( const u64 address )
{
    u64 offset = 0;

    if ( Math_AddressToOffset( m_ranges, address, offset ) )
    {
        const u64 half = m_detailSpan / 2u;
        SetWindowOffset( offset > half ? offset - half : 0u );
    }

    NavigateUniverse( address );
}

} // namespace cantordust
