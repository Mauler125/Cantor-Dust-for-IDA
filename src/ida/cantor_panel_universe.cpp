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

#include <QTimer>
#include <QSettings>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpressionValidator>
#include <QWheelEvent>
#include <QToolTip>
#include <QCursor>
#include <QJsonDocument>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QSaveFile>
#include <QMessageBox>
#include <QSignalBlocker>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <utility>

namespace cantordust
{

void CantorPanel::LoadUniverse()
{
    if ( m_universeLoaded )
        return;

    m_universeData = Ida_ReadUniverseData( m_showGlobalsToggle->isChecked() );
    m_inspectorIndex = Universe_BuildInspectorIndex( m_universeData );
    m_inspectorResult = {};
    m_lastHoveredAddress.reset();

    const usize categories = std::max< usize >( 1u,
        ( m_universeData.functions.size() + CANTORDUST_UNIVERSE_MAX_FUNCTIONS - 1u ) /
        CANTORDUST_UNIVERSE_MAX_FUNCTIONS );

    {
        const QSignalBlocker blocker( m_universeCategoryBox );
        m_universeCategoryBox->clear();

        for ( usize i = 0; i < categories; ++i )
        {
            const usize first = i * CANTORDUST_UNIVERSE_MAX_FUNCTIONS;
            const usize last = std::min(
                m_universeData.functions.size(),
                first + CANTORDUST_UNIVERSE_MAX_FUNCTIONS );

            m_universeCategoryBox->addItem(
                QString( "Tier %1 · #%2-%3" )
                    .arg( static_cast< qulonglong >( i ) )
                    .arg( static_cast< qulonglong >( first + 1 ) )
                    .arg( static_cast< qulonglong >( last ) ) );
        }

        m_universeCategoryBox->setCurrentIndex( 0 );
    }

    m_universeLoaded = true;

    if ( m_inspectorAddress.has_value() )
    {
        if ( m_inspectorIndex.nodes.count( *m_inspectorAddress ) )
            UpdateUniverseInspector( *m_inspectorAddress );

        else
            HideUniverseInspector();
    }

    BuildUniverseCategory();
    RefreshUniverseSearch();
}

void CantorPanel::BuildUniverseCategory()
{
    const usize tier = static_cast< usize >(
        std::max( 0, m_universeCategoryBox->currentIndex() ) );

    m_universeGraph = Universe_BuildGraph(
        m_universeData.functions, m_universeData.references,
        CANTORDUST_UNIVERSE_MAX_FUNCTIONS, CANTORDUST_UNIVERSE_MAX_LINKS,
        tier * CANTORDUST_UNIVERSE_MAX_FUNCTIONS,
        m_distinctCallersToggle->isChecked(),
        m_globalFieldsToggle->isChecked() );

    m_universeGraph.truncated |= m_universeData.truncated;
    Universe_SetCountMode( m_universeGraph,
        m_distinctCallersToggle->isChecked(),
        m_globalFieldsToggle->isChecked() );

    if ( m_organicLayoutToggle->isChecked() )
    {
        Universe_RelayoutGraph( m_universeGraph, true );
    }
}

void CantorPanel::RefreshUniverseSearch()
{
    if ( !m_universeLoaded )
        return;

    const std::optional< u64 > previous = SearchResultAddress();
    const std::vector< UniverseSearchHit_s > results = Universe_SearchFunctions( m_universeData,
        m_universeSearchEdit->text().toStdString(),
        static_cast< u32 >( m_universeMinRefsEdit->value() ) );

    const QSignalBlocker blocker( m_universeSearchResults );
    m_universeSearchResults->clear();
    int selected = -1;

    for ( const UniverseSearchHit_s& result : results )
    {
        const QString label = QString( "%1 | 0x%2 | %3 refs" )
            .arg( QString::fromUtf8( result.name.c_str() ) )
            .arg( static_cast< qulonglong >( result.address ), 0, 16 )
            .arg( result.incoming );

        const int index = m_universeSearchResults->count();
        m_universeSearchResults->addItem( label,
            QVariant::fromValue< qulonglong >( result.address ) );

        if ( previous.has_value() && *previous == result.address )
            selected = index;
    }

    if ( selected >= 0 )
        m_universeSearchResults->setCurrentIndex( selected );
}

std::optional< u64 > CantorPanel::SearchResultAddress() const
{
    if ( m_universeSearchResults->currentIndex() < 0 )
        return std::nullopt;

    return static_cast< u64 >(
        m_universeSearchResults->currentData().toULongLong() );
}

std::optional< UniversePathEndpoint_s > CantorPanel::PathEndpointSelection() const
{
    std::optional< u64 > idaFunction;
    // Read the screen EA *now* rather than relying on a preceding Qt/UI
    // notification; clicking a dock button should not select a stale
    // search result when the user just navigated in disassembly.
    if ( const func_t* const function = get_func( get_screen_ea() ) )
        idaFunction = static_cast< u64 >( function->start_ea );

    return Universe_ChoosePathEndpoint( idaFunction,
        SearchResultAddress(), m_pathPreferSearchResult );
}

void CantorPanel::UpdateUniversePathToolTips()
{
    const auto tooltip = []( const char* const button,
        const std::optional< u64 > address ) {
        QString text = QString( "Set Path %1 from the current IDA function "
            "or a selected search result." ).arg( QString::fromLatin1( button ) );

        if ( address.has_value() )
        {
            text += QString( " Current %1: 0x%2." )
                .arg( QString::fromLatin1( button ) )
                .arg( static_cast< qulonglong >( *address ), 0, 16 );
        }

        return text;
    };

    m_universePathFromButton->setToolTip(
        tooltip( "Start", m_universePathEndpoints.start ) );
    m_universePathToButton->setToolTip(
        tooltip( "End", m_universePathEndpoints.end ) );
}

void CantorPanel::SetUniversePathEndpoint( const bool isStart )
{
    const std::optional< UniversePathEndpoint_s > endpoint = PathEndpointSelection();

    if ( !endpoint.has_value() )
    {
        m_hintLabel->setText( "Navigate to a function in IDA or select "
            "a search result before setting a call path endpoint." );
        return;
    }

    const UniversePathSelectionState_e state = isStart
        ? m_universePathEndpoints.SetStart( endpoint->address )
        : m_universePathEndpoints.SetEnd( endpoint->address );

    UpdateUniversePathToolTips();
    const QString chosenLabel = isStart ? QStringLiteral( "Start" )
        : QStringLiteral( "End" );
    const QString otherLabel = isStart ? QStringLiteral( "End" )
        : QStringLiteral( "Start" );
    const QString origin = endpoint->fromSearch
        ? QStringLiteral( "search result" ) : QStringLiteral( "IDA cursor" );

    if ( state == UniversePathSelectionState_e::Waiting )
    {
        m_hintLabel->setText( QString( "Path %1: 0x%2 (%3). "
            "Set Path %4 to find directed call routes." )
            .arg( chosenLabel )
            .arg( static_cast< qulonglong >( endpoint->address ), 0, 16 )
            .arg( origin )
            .arg( otherLabel ) );
        return;
    }

    if ( state == UniversePathSelectionState_e::SameFunction )
    {
        ClearUniverseRoutes();
        RenderCurrent();
        m_hintLabel->setText( "Path Start and Path End are the same "
            "function. Choose a different function with either action." );
        return;
    }

    // Both endpoints are now available, regardless of selection order.
    ShowUniversePath( *m_universePathEndpoints.start,
        *m_universePathEndpoints.end );
}

void CantorPanel::ClearUniverseRoutes()
{
    m_universeActivePath = {};
    m_universeRoutes.clear();
    m_universeSelectedRoute = 0;

    const QSignalBlocker blocker( m_universeRouteBox );
    m_universeRouteBox->clear();
    m_universeRouteBox->setVisible( false );
}

void CantorPanel::SelectUniverseRoute( const int route )
{
    if ( route < 0 || static_cast< usize >( route ) >= m_universeRoutes.size() )
        return;

    m_universeSelectedRoute = static_cast< usize >( route );
    m_universeActivePath = m_universeRoutes[ m_universeSelectedRoute ];
    RenderCurrent();
    m_hintLabel->setText( QString( "Route %1/%2 (%3 calls). Left-click a "
        "node to navigate along the selected route." )
        .arg( route + 1 ).arg( static_cast< qulonglong >( m_universeRoutes.size() ) )
        .arg( static_cast< qulonglong >( m_universeActivePath.callSites.size() ) ) );
}

u64 CantorPanel::VisibleCommunityAddress(
    const u64 actual, const bool overview ) const
{
    const auto& groups = overview ? m_overviewCommunities : m_detailCommunities;

    for ( const auto& pair : groups )
    {
        if ( std::find( pair.second.begin(), pair.second.end(), actual ) !=
             pair.second.end() )
        {
            return pair.first;
        }
    }

    return actual;
}

void CantorPanel::OpenCommunity( const u64 representative,
    const bool overview )
{
    const auto& groups = overview ? m_overviewCommunities : m_detailCommunities;
    const auto group = groups.find( representative );

    if ( group == groups.end() || group->second.size() <= 1u )
    {
        FocusUniverse( representative );
        return;
    }

    const auto display = [this]( const u64 address ) {
        const auto it = m_inspectorIndex.nodes.find( address );
        const QString name = it == m_inspectorIndex.nodes.end()
            ? QStringLiteral( "function" ) : QString::fromStdString( it->second.name );

        return QString( "%1 @ 0x%2" ).arg( name )
            .arg( static_cast< qulonglong >( address ), 0, 16 );
    };
    QMenu menu( this );
    menu.setTitle( QString( "Community: %1 functions" )
        .arg( static_cast< qulonglong >( group->second.size() ) ) );

    const usize shown = std::min< usize >( 25u, group->second.size() );

    for ( usize i = 0; i < shown; ++i )
    {
        const u64 address = group->second[ i ];
        QAction* const action = menu.addAction( display( address ) );
        QObject::connect( action, &QAction::triggered,
            this, [this, address]() { FocusUniverse( address ); } );
    }

    if ( group->second.size() > shown )
    {
        menu.addSeparator();
        QAction* const more = menu.addAction( "More functions..." );
        // Capture the address vector before menus are torn down on focus.
        const std::vector< u64 >& members = group->second;
        QObject::connect( more, &QAction::triggered, this, [this, members, display]() {
            QStringList choices;

            for ( const u64 address : members )
                choices << display( address );
            bool accepted = false;

            const QString answer = QInputDialog::getItem( this,
                "Community functions", "Choose function:", choices,
                0, false, &accepted );

            if ( !accepted )
                return;

            const int chosen = choices.indexOf( answer );

            if ( chosen >= 0 )
                FocusUniverse( members[ static_cast< usize >( chosen ) ] );
        } );
    }

    menu.exec( QCursor::pos() );
}

void CantorPanel::ShowUniversePath( const u64 from,
    const u64 to )
{
    LoadUniverse();
    std::vector< UniverseCallPath_s > routes = Universe_FindCallPaths( m_universeData, from, to, 5 );
    ClearUniverseRoutes();

    if ( routes.empty() )
    {
        // A failed new search must not leave the previous route on screen.
        RenderCurrent();
        m_hintLabel->setText( "No directed call path in extracted references. "
            "Indirect calls may be absent. Change Set Path Start or Set Path End." );
        return;
    }
    m_universeRoutes = std::move( routes );
    m_universeActivePath = m_universeRoutes.front();
    {
        const QSignalBlocker blocker( m_universeRouteBox );

        for ( usize i = 0; i < m_universeRoutes.size(); ++i )
            m_universeRouteBox->addItem( QString( "Route %1 (%2)" )
                .arg( static_cast< int >( i + 1u ) )
                    .arg( static_cast< qulonglong >( m_universeRoutes[ i ].callSites.size() ) ) );
    }

    m_universeRouteBox->setVisible( true );
    m_universeFocusAddress.reset();
    m_universeFocusRangeToggle->setVisible( false );
    m_callerClickOrdinals.clear();

    if ( m_viewBox->currentData().toInt() != CDV_UNIVERSE3D )
        m_viewBox->setCurrentIndex( m_viewBox->findData( CDV_UNIVERSE3D ) );

    else
        RenderCurrent();
    m_universePlot->ReframeToHubs();
    m_hintLabel->setText( QString(
        "%1 directed route(s) found. Select a route to emphasize it; "
        "left-click a node to follow its callsite." )
        .arg( static_cast< qulonglong >( m_universeRoutes.size() ) ) );
}

std::vector< UniverseAddressRange_s > CantorPanel::SelectedUniverseRanges() const
{
    std::vector< UniverseAddressRange_s > windows;

    const u64 begin = m_windowOffset;
    const u64 end = begin + m_detailSpan;

    for ( const ByteRange_s& range : m_ranges )
    {
        const u64 lower = std::max( begin, range.offset );
        const u64 upper = std::min( end,
            range.offset + range.length );

        if ( lower < upper )
        {
            windows.push_back({
                range.address + lower - range.offset,
                range.address + upper - range.offset });
        }
    }

    return windows;
}

void CantorPanel::RebuildDetailUniverse()
{
    const std::vector< UniverseAddressRange_s > windows = SelectedUniverseRanges();
    m_detailCommunities.clear();

    if ( !m_universeActivePath.addresses.empty() )
    {
        m_detailUniverseGraph = m_universeRoutes.empty()
            ? Universe_BuildPathGraph( m_universeData, m_universeActivePath )
            : Universe_BuildMultiPathGraph( m_universeData,
                m_universeRoutes, m_universeSelectedRoute );

        return;
    }

    if ( m_universeFocusAddress.has_value() )
    {
        // Extracted xrefs remain available even when a tier omits the caller.
        // The focused callers always use raw reference counts, while the
        // center retains the user's selected counting mode.
        const bool distinct = m_distinctCallersToggle->isChecked();
        const UniverseFocusMode_e mode = static_cast< UniverseFocusMode_e >(
            m_universeFocusModeBox->currentData().toInt() );

        std::vector< u64 > collapsed(
            m_universeCollapsedNodes.begin(), m_universeCollapsedNodes.end() );

        m_detailUniverseGraph = Universe_FocusMultiHopFromData(
            m_universeData, *m_universeFocusAddress, mode,
            static_cast< u8 >( m_universeHopsEdit->value() ),
            m_universeFocusRangeToggle->isChecked() ? &windows : nullptr,
            distinct, m_globalFieldsToggle->isChecked(), &collapsed,
            { static_cast< usize >( m_universeNodeBudgetEdit->value() ),
              static_cast< usize >( m_universeLinkBudgetEdit->value() ) } );

        m_detailUniverseGraph = Universe_FilterGraphAdvanced(
            m_detailUniverseGraph, m_universeData, CurrentAdvancedFilters() );

        return;
    }

    m_detailUniverseGraph = m_universeRangeHeatToggle->isChecked()
        ? Universe_FilterGraphRangeHeat( m_universeGraph, windows,
            m_distinctCallersToggle->isChecked(),
            m_globalFieldsToggle->isChecked() )
        : Universe_FilterGraph( m_universeGraph, windows );

    if ( m_organicLayoutToggle->isChecked() )
    {
        Universe_RelayoutGraph( m_detailUniverseGraph, true );
    }

    if ( m_universeFilterToggle->isChecked() )
    {
        m_detailUniverseGraph = Universe_FilterGraphSearch(
            m_detailUniverseGraph,
            m_universeSearchEdit->text().toStdString(),
            static_cast< u32 >( m_universeMinRefsEdit->value() ) );
    }

    m_detailUniverseGraph = Universe_FilterGraphAdvanced(
        m_detailUniverseGraph, m_universeData, CurrentAdvancedFilters() );

    m_detailCommunities.clear();

    if ( m_universeClusterToggle->isChecked() )
    {
        UniverseCommunityResult_s community = Universe_BuildCommunities( m_detailUniverseGraph );
        m_detailUniverseGraph = std::move( community.graph );
        m_detailCommunities = std::move( community.members );
    }
}

void CantorPanel::ApplyUniverseFocus(
    const std::optional< u64 > address )
{
    ClearUniverseRoutes();

    if ( address.has_value() && m_universeLoaded )
    {
        const auto found = std::find_if( m_universeGraph.nodes.begin(),
            m_universeGraph.nodes.end(), [address]( const UniverseNode_s& node ) {
                return node.address == *address;
            } );

        if ( found == m_universeGraph.nodes.end() )
        {
            const usize rank = Universe_RankForAddress(
                m_universeData, *address, m_distinctCallersToggle->isChecked(),
                m_globalFieldsToggle->isChecked() );

            if ( rank != CANTORDUST_NO_SOURCE )
            {
                const QSignalBlocker blocker( m_universeCategoryBox );
                m_universeCategoryBox->setCurrentIndex( static_cast< int >(
                    rank / CANTORDUST_UNIVERSE_MAX_FUNCTIONS ) );

                BuildUniverseCategory();
            }
        }
    }

    if ( m_universeFocusAddress != address )
        m_universeCollapsedNodes.clear();

    m_universeFocusAddress = address;
    m_universeFocusRangeToggle->setVisible( address.has_value() );
    m_universeRangeHeatToggle->setVisible( !address.has_value() &&
        m_viewBox->currentData().toInt() == CDV_UNIVERSE3D );

    m_callerClickOrdinals.clear();
    m_universePlot->ClearSelection();
    m_overviewUniverse->ClearSelection();

    if ( address.has_value() &&
         m_viewBox->currentData().toInt() != CDV_UNIVERSE3D )
    {
        m_viewBox->setCurrentIndex( m_viewBox->findData( CDV_UNIVERSE3D ) );
    }
    else
    {
        RenderCurrent();
    }

    m_universePlot->ReframeToHubs();
}

void CantorPanel::FocusUniverse( const u64 address )
{
    ClearUniverseRoutes();
    LoadUniverse();
    // Caller-only retains legacy in-range behavior. Both/Callees defaults
    // to all references so outgoing edges aren't silently excluded.
    {
        const QSignalBlocker blocker( m_universeFocusRangeToggle );
        m_universeFocusRangeToggle->setChecked(
            m_universeFocusModeBox->currentData().toInt() == UFM_CALLERS &&
            m_universeHopsEdit->value() == 1 );
    }

    m_universeHistory.Push( address );
    ApplyUniverseFocus( address );
}

void CantorPanel::ToggleUniverseBranch( const u64 address )
{
    if ( !m_universeFocusAddress.has_value() ||
         address == *m_universeFocusAddress ||
         m_universeHopsEdit->value() <= 1 )
    {
        m_hintLabel->setText(
            "Set Hops to 2-10, then Shift + right-click a neighbor "
            "to collapse or expand its branch." );

        return;
    }

    const auto found = std::find_if( m_detailUniverseGraph.nodes.begin(),
        m_detailUniverseGraph.nodes.end(),
        [address]( const UniverseNode_s& node ) {
            return node.address == address;
        } );

    if ( found == m_detailUniverseGraph.nodes.end() ||
         found->focusDepth >= m_universeHopsEdit->value() )
    {
        return;
    }

    if ( !m_universeCollapsedNodes.insert( address ).second )
        m_universeCollapsedNodes.erase( address );

    RenderCurrent();
    // No camera reset: the same functions retain their graph positions.
}

void CantorPanel::ClearUniverseFocus()
{
    if ( !m_universeActivePath.addresses.empty() )
    {
        ClearUniverseRoutes();
        RenderCurrent();
        m_universePlot->ReframeToHubs();
        return;
    }

    if ( !m_universeFocusAddress.has_value() )
    {
        m_universePlot->ResetCamera();
        return;
    }

    m_universeHistory.Push( std::nullopt );
    ApplyUniverseFocus( std::nullopt );
}

void CantorPanel::UniverseBack()
{
    std::optional< u64 > address;

    if ( m_universeHistory.GoBack( address ) )
    {
        ApplyUniverseFocus( address );
    }
}

void CantorPanel::UniverseForward()
{
    std::optional< u64 > address;

    if ( m_universeHistory.GoForward( address ) )
    {
        ApplyUniverseFocus( address );
    }
}

} // namespace cantordust
