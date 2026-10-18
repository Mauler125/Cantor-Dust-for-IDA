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

bool CantorPanel::IsAllSegments() const
{
    const QVariant selected = m_segmentBox->currentData();
    return selected.isValid() && selected.toULongLong() ==
        std::numeric_limits< qulonglong >::max();
}

Palette_e CantorPanel::CurrentPalette() const
{
    return static_cast< Palette_e >( m_paletteBox->currentData().toInt() );
}

Palette_e CantorPanel::CurrentOverviewPalette() const
{
    return static_cast< Palette_e >(
        m_overviewPaletteBox->currentData().toInt() );
}

usize CantorPanel::Resolution() const
{
    return static_cast< usize >( m_resolutionBox->currentData().toInt() );
}

usize CantorPanel::WindowCapacity() const
{
    const usize side = Resolution();
    return side * side;
}

void CantorPanel::ListSegments( const bool followCursor )
{
    const ea_t cursor = get_screen_ea();
    const QVariant old = m_segmentBox->currentData();
    const QSignalBlocker blocker( m_segmentBox );
    m_segmentBox->clear();

    int target = -1;

    const int segmentCount = get_segm_qty();

    for ( int i = 0; i < segmentCount; ++i )
    {
        const segment_t* const segment = getnseg( i );

        if ( segment == nullptr || segment->end_ea <= segment->start_ea )
        {
            continue;
        }

        qstring name;
#if IDA_SDK_VERSION >= 940
        get_segment_name( &name, segment->start_ea );
#else
        // IDA 9.2/9.3 accept a segment pointer, not a start address.
        get_segm_name( &name, segment );
#endif
        m_segmentBox->addItem(
            QString( "%1 [%2 - %3]" )
                .arg( QString::fromUtf8( name.c_str() ) )
                .arg( Ida_FormatAddress( segment->start_ea ) )
                .arg( Ida_FormatAddress( segment->end_ea ) ),
            QVariant::fromValue< qulonglong >( segment->start_ea ) );

        const int row = m_segmentBox->count() - 1;

        if ( followCursor && cursor >= segment->start_ea &&
             cursor < segment->end_ea )
        {
            target = row;
        }
        else if ( !followCursor && old.isValid() &&
                  old.toULongLong() == segment->start_ea )
        {
            target = row;
        }
    }

    if ( m_segmentBox->count() != 0 )
    {
        m_segmentBox->addItem( "All segments (initialized extents)",
            QVariant::fromValue< qulonglong >(
                std::numeric_limits< qulonglong >::max() ) );

        if ( !followCursor && old.isValid() && old.toULongLong() ==
             std::numeric_limits< qulonglong >::max() )
        {
            target = m_segmentBox->count() - 1;
        }
    }

    if ( target < 0 && m_segmentBox->count() != 0 )
    {
        target = 0;
    }

    // ListSegments() also handles follow-cursor selection with Qt signals
    // blocked. Apply the same fit reset as a manual segment change.
    if ( target >= 0 && old.isValid() &&
         m_segmentBox->itemData( target ) != old )
    {
        m_detailSpan = 0;
    }

    m_segmentBox->setCurrentIndex( target );
}

void CantorPanel::RebuildRanges()
{
    ClearAnomalies();
    m_ranges.clear();
    m_cells.clear();
    m_totalLength = 0;
    m_exactRequestedOffset.reset();
    InvalidateOverviewSamples();

    const QVariant selected = m_segmentBox->currentData();

    if ( !selected.isValid() )
    {
        return;
    }

    if ( IsAllSegments() )
    {
        for ( int i = 0; i < get_segm_qty(); ++i )
        {
            const segment_t* const segment = getnseg( i );

            if ( segment == nullptr || segment->end_ea <= segment->start_ea ||
                 !Ida_SupportsEightBitBytes( segment->start_ea ) )
            {
                continue;
            }

            ea_t loadedStart = BADADDR;
            ea_t loadedEnd = BADADDR;

            if ( !Ida_InitializedExtent(
                     segment->start_ea, segment->end_ea,
                     loadedStart, loadedEnd ) )
            {
                continue;
            }

            Math_AddRange( m_ranges, loadedStart, loadedEnd - loadedStart );
        }
    }
    else
    {
        const segment_t* const segment = getseg(
            static_cast< ea_t >( selected.toULongLong() ) );

        if ( segment != nullptr && segment->end_ea > segment->start_ea &&
             Ida_SupportsEightBitBytes( segment->start_ea ) )
        {
            // Exclude uninitialized padding at the beginning and end of
            // a segment. Otherwise a small, populated segment can appear
            // mostly empty and users can zoom out into that empty padding.
            // Keep wholly uninitialized segments available for inspection.
            ea_t begin = segment->start_ea;
            ea_t end = segment->end_ea;
            ea_t initializedBegin = BADADDR;
            ea_t initializedEnd = BADADDR;

            if ( Ida_InitializedExtent( begin, end,
                     initializedBegin, initializedEnd ) )
            {
                begin = initializedBegin;
                end = initializedEnd;
            }

            Math_AddRange( m_ranges, begin, end - begin );
        }
    }

    m_totalLength = Math_RangeLength( m_ranges );
    m_detailSpan = m_detailSpan == 0
        ? std::min< u64 >( m_totalLength, WindowCapacity() )
        : std::min( m_totalLength, m_detailSpan );

    m_overviewOffset = 0;
    m_overviewSpan = m_totalLength;
    SyncRangeControl();
}

void CantorPanel::UpdateRangeAddressLabel()
{
    if ( m_ranges.empty() || m_totalLength == 0 || m_detailSpan == 0 )
    {
        m_rangeAddressLabel->setText( "Range: no addresses selected" );
        return;
    }

    const std::vector< UniverseAddressRange_s > ranges = SelectedUniverseRanges();

    if ( ranges.empty() )
    {
        m_rangeAddressLabel->setText( "Range: no addresses selected" );
        return;
    }

    const u64 first = ranges.front().start;
    const u64 last = ranges.back().end - 1u;
    m_rangeAddressLabel->setText( QString( "Range: %1 - %2 (%3 bytes%4)" )
        .arg( Ida_FormatAddress( static_cast< ea_t >( first ) ) )
        .arg( Ida_FormatAddress( static_cast< ea_t >( last ) ) )
        .arg( static_cast< qulonglong >( m_detailSpan ) )
        .arg( ranges.size() > 1 ? ", across segments" : "" ) );
}

void CantorPanel::SyncRangeControl()
{
    m_detailSpan = m_totalLength == 0 ? 0 : std::clamp< u64 >(
        m_detailSpan, 1u, m_totalLength );

    const QSignalBlocker blocker( m_rangeEdit );
    m_rangeEdit->setText( QString::number(
        static_cast< qulonglong >( m_detailSpan ) ) );

    UpdateRangeAddressLabel();
}

void CantorPanel::CommitRangeText()
{
    if ( m_totalLength == 0 )
    {
        SyncRangeControl();
        return;
    }

    bool valid = false;

    const u64 requested = m_rangeEdit->text().toULongLong( &valid );
    const u64 normalized = valid
        ? std::clamp< u64 >( requested, 1u, m_totalLength )
        : m_totalLength;

    if ( normalized == m_detailSpan )
    {
        // An explicit re-commit must also recover a previously zoomed-away
        // Overview, but leave the existing 3D camera untouched when the
        // requested range and its coverage have not changed.
        const u64 maximum = m_totalLength > m_detailSpan
            ? m_totalLength - m_detailSpan : 0;
        const u64 detailStart = std::min( m_windowOffset, maximum );
        const ZoomRange_s containing = Math_OverviewContainingDetail(
            m_totalLength, { m_overviewOffset, m_overviewSpan },
            { detailStart, m_detailSpan } );

        SyncRangeControl();

        if ( containing.start != m_overviewOffset ||
             containing.length != m_overviewSpan )
        {
            SetWindowOffset( detailStart );
        }

        return;
    }

    m_detailSpan = normalized;
    SetWindowOffset( m_windowOffset );
}

bool CantorPanel::eventFilter( QObject* const watched, QEvent* const event )
{
    if ( watched == m_universeExplorerPanel || watched == parentWidget() )
    {
        if ( event->type() == QEvent::Resize ||
             event->type() == QEvent::FontChange ||
             event->type() == QEvent::StyleChange )
        {
            UpdateUniverseSearchLayout( m_universeExplorerPanel->width() );
        }

        return QWidget::eventFilter( watched, event );
    }

    if ( event->type() != QEvent::Wheel )
    {
        return QWidget::eventFilter( watched, event );
    }

    QWheelEvent* const wheel = static_cast< QWheelEvent* >( event );

    const QPoint delta = wheel->angleDelta();

    if ( delta.x() == 0 || std::abs( delta.x() ) < std::abs( delta.y() ) )
    {
        return QWidget::eventFilter( watched, event );
    }

    m_horizontalWheelRemainder += delta.x();

    const int steps = m_horizontalWheelRemainder / 120;
    m_horizontalWheelRemainder -= steps * 120;

    if ( steps != 0 )
    {
        WheelRangeLarge( steps );
    }

    wheel->accept();
    return true;
}

void CantorPanel::WheelRangeLarge( const int steps )
{
    if ( m_totalLength == 0 || steps == 0 )
    {
        return;
    }

    const u64 oldSpan = m_detailSpan;
    u64 next = oldSpan;

    const int count = std::min( std::abs( steps ), 16 );

    for ( int i = 0; i < count; ++i )
    {
        if ( steps > 0 )
        {
            next = std::max< u64 >( 1u, next / 2u );
        }
        else
        {
            next = next >= m_totalLength / 2u
                ? m_totalLength : std::min( m_totalLength, next * 2u );
        }
    }

    if ( next == oldSpan )
    {
        return;
    }

    const u64 center = m_windowOffset + oldSpan / 2u;
    m_detailSpan = next;

    const u64 offset = center > next / 2u
        ? center - next / 2u : 0u;

    SetWindowOffset( offset );
}

std::optional< u64 > CantorPanel::ResolveCursorUniverseAddress() const
{
    const ea_t cursor = get_screen_ea();

    if ( cursor == BADADDR )
        return std::nullopt;

    // Data can be selected outside any function (including .data/.rdata).
    // Resolve member/tail addresses back to their owning data item.
    {
        const ea_t item = get_item_head( cursor );

        if ( item != BADADDR && is_data( get_flags( item ) ) &&
             get_func( item ) == nullptr )
        {
            return static_cast< u64 >( item );
        }
    }
    // A highlighted global identifier can come from a disassembly or
    // pseudocode viewer, whose screen EA often still belongs to a function.
    if ( TWidget* const viewer = get_current_viewer() )
    {
        qstring highlighted;
        uint32 flags = 0;

        if ( get_highlight( &highlighted, viewer, &flags ) &&
             !highlighted.empty() )
        {
            const ea_t named = get_name_ea( cursor, highlighted.c_str() );

            if ( named != BADADDR )
            {
                const ea_t item = get_item_head( named );

                if ( item != BADADDR && is_data( get_flags( item ) ) &&
                     get_func( item ) == nullptr )
                {
                    return static_cast< u64 >( item );
                }
            }
        }
    }
    // When the cursor is on a disassembly operand referencing global data,
    // prefer that global over the instruction's containing function.
    if ( get_opnum() >= 0 )
    {
        for ( ea_t target = get_first_dref_from( cursor );
              target != BADADDR;
              target = get_next_dref_from( cursor, target ) )
        {
            const ea_t item = get_item_head( target );

            if ( item != BADADDR && is_data( get_flags( item ) ) &&
                 get_func( item ) == nullptr )
            {
                return static_cast< u64 >( item );
            }
        }
    }

    const func_t* const function = get_func( cursor );

    if ( function != nullptr )
    {
        return static_cast< u64 >( function->start_ea );
    }

    return std::nullopt;
}

void CantorPanel::OnIdaCursorChanged( const ea_t address )
{
    // A non-address widget can transiently report BADADDR when it gains
    // focus. Keep the last meaningful IDA function highlight in that case.
    if ( address == BADADDR )
        return;
    // This notification is for the current function, never an operand's
    // referenced global. Resolve tail chunks to the function entry point.
    std::optional< u64 > functionAddress;

    if ( const func_t* const function = get_func( address ) )
        functionAddress = static_cast< u64 >(
            function->start_ea );

    if ( functionAddress == m_idaFunctionAddress )
        return;

    m_pathPreferSearchResult = false;
    m_idaFunctionAddress = functionAddress;
    // Updating the halo never resets the camera or rebuilds the graph.
    m_universePlot->SetIdaSyncAddress( functionAddress.has_value()
        ? std::optional< u64 >(
            VisibleCommunityAddress( *functionAddress, false ) ) : std::nullopt );

    m_overviewUniverse->SetIdaSyncAddress( functionAddress.has_value()
        ? std::optional< u64 >(
            VisibleCommunityAddress( *functionAddress, true ) ) : std::nullopt );

    m_idaFollowTimer->stop();

    if ( functionAddress.has_value() &&
         m_followIdaToggle->isChecked() )
    {
        m_idaFollowTimer->start();
    }
}

void CantorPanel::FollowIdaFunction()
{
    if ( !m_followIdaToggle->isChecked() ||
         !m_idaFunctionAddress.has_value() || !m_universeLoaded ||
         !m_universeActivePath.addresses.empty() ||
         m_viewBox->currentData().toInt() != CDV_UNIVERSE3D )
    {
        return;
    }

    const u64 address = *m_idaFunctionAddress;
    // The IDA hook can report addresses missing from a bounded extraction.
    // Don't replace an existing investigation with an empty focus sphere.
    if ( m_inspectorIndex.nodes.find( address ) ==
         m_inspectorIndex.nodes.end() )
    {
        return;
    }

    if ( m_universeFocusAddress == address )
        return;
    // Debounced and idempotent: browsing instructions inside one function
    // never adds history entries or causes repeated camera jumps.
    m_universeHistory.Push( address );
    ApplyUniverseFocus( address );
}

void CantorPanel::CursorToUniverseFunction()
{
    const std::optional< u64 > target = ResolveCursorUniverseAddress();

    if ( !target.has_value() )
    {
        m_hintLabel->setText(
            "Cursor is not on a retained function or global data item." );

        return;
    }

    const u64 address = *target;

    if ( get_func( static_cast< ea_t >( address ) ) == nullptr &&
         !m_showGlobalsToggle->isChecked() )
    {
        // Cursor navigation should find a global without requiring users
        // to manually enable globals first.
        m_showGlobalsToggle->setChecked( true );
    }

    if ( m_lastCursorFunction == address )
    {
        FocusUniverse( address );
        return;
    }

    LoadUniverse();

    const usize rank = Universe_RankForAddress(
        m_universeData, address, m_distinctCallersToggle->isChecked(),
        m_globalFieldsToggle->isChecked() );

    if ( rank == CANTORDUST_NO_SOURCE )
    {
        m_hintLabel->setText(
            "Node was not retained by the bounded IDA scan. "
            "Enable Show globals in Settings for data nodes." );

        return;
    }

    const int category = static_cast< int >(
        rank / CANTORDUST_UNIVERSE_MAX_FUNCTIONS );

    if ( category != m_universeCategoryBox->currentIndex() )
    {
        const QSignalBlocker blocker( m_universeCategoryBox );
        m_universeCategoryBox->setCurrentIndex( category );
        BuildUniverseCategory();
    }

    ListSegments( true );
    // The IDA cursor may be on a pseudocode expression in .text while its
    // referenced global lives in .data. Select the TARGET's segment.
    if ( const segment_t* const segment = getseg(
             static_cast< ea_t >( address ) ) )
    {
        for ( int row = 0; row < m_segmentBox->count(); ++row )
        {
            if ( m_segmentBox->itemData( row ).toULongLong() ==
                 static_cast< qulonglong >( segment->start_ea ) )
            {
                const QSignalBlocker blocker( m_segmentBox );
                m_segmentBox->setCurrentIndex( row );
                break;
            }
        }
    }

    RebuildRanges();
    u64 offset = 0;

    if ( !Math_AddressToOffset( m_ranges, address, offset ) )
    {
        m_hintLabel->setText( "Node is outside the selected segment range." );
        return;
    }

    if ( m_universeFocusAddress.has_value() )
    {
        m_universeHistory.Push( std::nullopt );
        ApplyUniverseFocus( std::nullopt );
    }

    const u64 half = m_detailSpan / 2u;
    SetWindowOffset( offset > half ? offset - half : 0u );

    if ( !m_universePlot->CenterOnAddress( address ) )
    {
        m_hintLabel->setText( QString( "Node %1 was not retained in the tier." )
            .arg( Ida_FormatAddress( static_cast< ea_t >( address ) ) ) );

        return;
    }

    m_overviewUniverse->CenterOnAddress( address );
    m_lastCursorFunction = address;
    m_hintLabel->setText( QString( "Centered on %1" )
        .arg( Ida_FormatAddress( static_cast< ea_t >( address ) ) ) );
}

void CantorPanel::PositionAtCursor()
{
    u64 offset = 0;

    if ( !Math_AddressToOffset( m_ranges, get_screen_ea(), offset ) )
    {
        return;
    }

    const u64 maxOffset = m_totalLength > m_detailSpan
        ? m_totalLength - m_detailSpan : 0;

    m_exactRequestedOffset = std::min( offset, maxOffset );
}

void CantorPanel::ResetEmpty( const QString& message )
{
    m_infoLabel->setText( message );
    m_infoLabel->setToolTip( message );
    m_cells.clear();
    m_detailSampled = false;
    m_detailSampleOffsets.clear();
    m_detailSampleClasses.clear();
    m_detailCubeTriplets.clear();
    m_byteCount = 0;
    m_loadedCount = 0;
    m_windowOffset = 0;
    m_detailSpan = 0;
    m_overviewSpan = 0;
    m_overviewOffset = 0;
    SyncRangeControl();
    m_plot->SetImage( Render( CDV_LINEAR, {}, Resolution(), CurrentPalette() ) );
    m_cubePlot->SetCloud( {}, CurrentPalette() );
    m_overviewCube->SetCloud( {}, CurrentOverviewPalette() );
    m_overviewTriplets.clear();
    m_overviewCubeLoaded = false;
    m_overviewCells.clear();
    m_overviewPlot->SetImage( Render( CDV_LINEAR, {}, Resolution(), CurrentOverviewPalette() ) );
    m_overviewPlot->SetHighlights( {} );
    m_plot->SetAnomalyMarkers( {} );
    m_overviewPlot->SetAnomalyMarkers( {} );
    m_entropyStrip->SetSamples( {} );
}

void CantorPanel::LoadOverview()
{
    if ( !m_overviewCells.empty() || m_totalLength == 0 )
    {
        return;
    }

    IdaOverview_s sample = Ida_ReadOverviewRanges(
        m_ranges, Resolution(), m_overviewOffset, m_overviewSpan );

    m_overviewCells = std::move( sample.cells );
    m_overviewClasses = std::move( sample.classes );
    m_overviewOffsets = std::move( sample.sourceOffsets );
    m_overviewImageDirty = true;
}

void CantorPanel::Reload()
{
    if ( m_totalLength == 0 )
    {
        ResetEmpty( "No supported 8-bit segments available." );
        return;
    }

    if ( m_detailSpan == 0 )
    {
        m_detailSpan = std::min< u64 >(
            m_totalLength, WindowCapacity() );
    }

    const u64 cachedOffset = m_windowOffset;
    const bool cachedSampled = m_detailSampled;
    const usize cachedCount = m_cells.size();
    const u64 maxOffset = m_totalLength > m_detailSpan
        ? m_totalLength - m_detailSpan : 0;
    // Retain the exact 64-bit window offset. Navigation via the canvases,
    // cursor, and address range no longer needs a lossy UI slider.
    m_windowOffset = std::min(
        m_exactRequestedOffset.value_or( m_windowOffset ), maxOffset );

    m_exactRequestedOffset.reset();

    // Refresh the editable range and address label from the final, clamped
    // window. Horizontal wheel, navigation and bookmark restore all reload
    // through here, not just vertical wheel zoom.
    SyncRangeControl();

    const View_e mode = static_cast< View_e >(
        m_viewBox->currentData().toInt() );

    m_detailSampled = m_detailSpan > WindowCapacity() &&
        mode != CDV_UNIVERSE3D;

    m_detailSampleOffsets.clear();
    m_detailSampleClasses.clear();
    m_detailCubeTriplets.clear();

    usize loadedCount = 0;

    if ( m_detailSampled && mode == CDV_CUBE3D )
    {
        // A 3D byte cloud is a bounded point sample of the entire requested
        // range; do not silently restrict it to the first grid's worth.
        m_detailCubeTriplets = Ida_ReadCubeOverviewRanges(
            m_ranges, m_windowOffset, m_detailSpan, 65536,
            static_cast< CubeValueMode_e >(
                m_cubeValuesBox->currentIndex() ),
            static_cast< usize >( m_cubeStrideEdit->value() ),
            static_cast< usize >( m_cubeAlignmentEdit->value() ),
            static_cast< unsigned int >( m_cubeBitPlaneEdit->value() ) );

        m_cells.clear();
        m_byteCount = m_detailCubeTriplets.size();
        loadedCount = m_byteCount;
    }
    else if ( m_detailSampled )
    {
        // Render *all* of the selected logical range at the available pixel
        // resolution. Samples carry absolute logical offsets for accurate
        // hover, navigation and anchored wheel zooming.
        IdaOverview_s sample = Ida_ReadOverviewRanges(
            m_ranges, Resolution(), m_windowOffset, m_detailSpan );

        loadedCount = sample.loadedCount;
        m_cells = std::move( sample.cells );
        m_detailSampleOffsets = std::move( sample.sourceOffsets );
        m_detailSampleClasses = std::move( sample.classes );
        m_byteCount = m_cells.size();
    }
    else
    {
        m_byteCount = static_cast< usize >(
            std::min< u64 >({ m_totalLength - m_windowOffset,
                m_detailSpan, static_cast< u64 >( WindowCapacity() ) }) );

        if ( mode == CDV_UNIVERSE3D )
        {
            // Universe navigation needs addresses, not a byte array.
            m_cells.clear();
        }
        else if ( !cachedSampled && cachedCount != 0 &&
             m_windowOffset >= cachedOffset &&
             m_windowOffset - cachedOffset <= cachedCount &&
             m_byteCount <= cachedCount -
                 static_cast< usize >( m_windowOffset - cachedOffset ) )
        {
            const usize begin = static_cast< usize >(
                m_windowOffset - cachedOffset );

            std::vector< ByteCell_s > cached(
                m_cells.begin() + static_cast< std::ptrdiff_t >( begin ),
                m_cells.begin() + static_cast< std::ptrdiff_t >(
                    begin + m_byteCount ) );

            loadedCount = static_cast< usize >( std::count_if(
                cached.begin(), cached.end(),
                []( const ByteCell_s& cell ) { return cell.valid; } ) );

            m_cells = std::move( cached );
        }
        else
        {
            IdaWindow_s sample = Ida_ReadWindowRanges(
                m_ranges, m_windowOffset, m_byteCount );

            loadedCount = sample.loadedCount;
            m_cells = std::move( sample.cells );
        }
    }

    m_loadedCount = loadedCount;
    RenderCurrent();
    UpdateWindowInfoLabel();
}

void CantorPanel::UpdateWindowInfoLabel()
{
    if ( m_totalLength == 0 || m_detailSpan == 0 )
        return;

    u64 start = 0;
    u64 last = 0;
    Math_OffsetToAddress( m_ranges, m_windowOffset, start );
    Math_OffsetToAddress(
        m_ranges, m_windowOffset + m_detailSpan - 1, last );

    m_infoLabel->setText(
        QString( "%1%2 - %3 | %4/%5 loaded | %6 logical bytes" )
            .arg( IsAllSegments() ? "All: " : "" )
            .arg( Ida_FormatAddress( start ) )
            .arg( Ida_FormatAddress( last ) )
            .arg( static_cast< qulonglong >( m_loadedCount ) )
            .arg( static_cast< qulonglong >( m_byteCount ) )
            .arg( static_cast< qulonglong >( m_totalLength ) ) +
        ( m_detailSampled ? QStringLiteral( " (sampled across selection)" )
                          : QString() ) +
        QString( " | Overview %1 bytes | Detail %2 bytes" )
            .arg( static_cast< qulonglong >( m_overviewSpan ) )
            .arg( static_cast< qulonglong >( m_detailSpan ) ) );

    if ( m_viewBox->currentData().toInt() == CDV_UNIVERSE3D )
    {
        m_infoLabel->setText( QString(
            "Universe detail: %1 functions / %2 links | Global: %3 functions%4" )
            .arg( static_cast< qulonglong >( m_detailUniverseGraph.nodes.size() ) )
            .arg( static_cast< qulonglong >( m_detailUniverseGraph.links.size() ) )
            .arg( static_cast< qulonglong >( m_universeGraph.nodes.size() ) )
            .arg( m_universeFocusAddress.has_value()
                ? ( m_universeFocusRangeToggle->isChecked()
                    ? " | Focused connections (range only)" : " | Focused connections (global)" )
                : " | Selected range" ) );
    }

    m_infoLabel->setToolTip( m_infoLabel->text() );
}

} // namespace cantordust
