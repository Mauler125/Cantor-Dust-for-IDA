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

void CantorPanel::RenderCurrent()
{
    // Bookmarks restore palette selectors with blocked Qt signals. Always
    // refresh the widget's palette when rendering, not only on UI changes.
    m_plot->SetPalette( CurrentPalette() );

    if ( m_byteCount == 0 )
    {
        if ( m_viewBox->currentData().toInt() == CDV_CUBE3D )
        {
            m_detailStack->setCurrentWidget( m_cubePlot );
            m_cubePlot->SetCloud( {}, CurrentPalette() );
        }

        RenderOverview();
        return;
    }

    const View_e mode = static_cast< View_e >(
        m_viewBox->currentData().toInt() );

    m_universeCategoryBox->setVisible(
        mode == CDV_UNIVERSE3D ||
        m_overviewViewBox->currentData().toInt() == CDV_UNIVERSE3D );

    m_universePathControls->setVisible( mode == CDV_UNIVERSE3D );

    if ( mode == CDV_UNIVERSE3D )
    {
        LoadUniverse();
        m_detailStack->setCurrentWidget( m_universePlot );
        RebuildDetailUniverse();
        UniverseRenderStyle_s style;
        style.relativeNodeHeat = m_relativeNodeHeatToggle->isChecked();
        style.relativeNodeSize = m_relativeNodeSizeToggle->isChecked();
        style.antiAliasing = m_antiAliasingToggle->isChecked();
        style.universeSpacing =
            static_cast< f32 >( m_universeSpacingSlider->value() ) / 100.0f;

        if ( m_universeFocusAddress.has_value() &&
             m_universeActivePath.addresses.empty() )
        {
            // Fit an extra shell without moving individual graph nodes.
            // Branch toggles keep the same framing at a selected depth.
            style.focusScale = 1.0f / ( 1.0f + 0.68f *
                static_cast< f32 >( m_universeHopsEdit->value() - 1 ) );
        }

        style.absoluteHeatMaximum = m_universeGraph.absoluteHeatMaximum;
        m_universePlot->SetRenderStyle( style );
        m_universePlot->SetGraph( &m_detailUniverseGraph, CurrentPalette() );

        if ( m_idaFunctionAddress.has_value() )
            m_universePlot->SetIdaSyncAddress(
                VisibleCommunityAddress( *m_idaFunctionAddress, false ) );

        QString status = QString(
            "Detail: %1 functions, %2 links | Global: %3 functions%4" )
            .arg( static_cast< qulonglong >( m_detailUniverseGraph.nodes.size() ) )
            .arg( static_cast< qulonglong >( m_detailUniverseGraph.links.size() ) )
            .arg( static_cast< qulonglong >( m_universeGraph.nodes.size() ) )
            .arg( m_universeFocusAddress.has_value()
                ? ( m_universeFocusRangeToggle->isChecked()
                    ? " | Focused connections (range only)" : " | Focused connections (global)" )
                : " | Selected range" );

        if ( m_universeFocusAddress.has_value() &&
             m_universeActivePath.addresses.empty() )
        {
            const UniverseGraph_s& focus = m_detailUniverseGraph;
            // Put the most useful expansion result first: the dock may be
            // narrow and the path buttons share this same status row.
            QString summary = QString( "Depth %1/%2" )
                .arg( static_cast< int >( focus.focusDepthReached ) )
                .arg( static_cast< int >( focus.focusDepthRequested ) );

            QStringList reasons;

            if ( focus.focusNodeLimitReached )
                reasons << "nodes";

            if ( focus.focusLinkLimitReached )
                reasons << "links";

            if ( focus.focusFanoutLimitReached )
                reasons << "1024/branch";

            if ( !reasons.isEmpty() )
                summary += " | Limit reached (" + reasons.join( ", " ) + ")";

            else if ( focus.focusDepthReached < focus.focusDepthRequested )
                summary += " | No further visible hops";

            if ( m_universeData.truncated )
                summary += " | Source data truncated";

            else if ( focus.truncated && reasons.isEmpty() )
                summary += " | Focus graph truncated";

            status = summary + " | " + status;
        }

        m_infoLabel->setText( status );
        m_infoLabel->setToolTip( status );
    }
    else if ( mode == CDV_CUBE3D )
    {
        m_detailStack->setCurrentWidget( m_cubePlot );
        std::vector< usize > breaks;

        const u64 end = m_windowOffset + m_byteCount;

        for ( const ByteRange_s& range : m_ranges )
        {
            if ( range.offset > m_windowOffset && range.offset < end )
            {
                breaks.push_back( static_cast< usize >(
                    range.offset - m_windowOffset ) );
            }
        }

        if ( m_detailSampled )
        {
            m_cubePlot->SetCloud( Cube_BuildTriplets(
                m_detailCubeTriplets, CurrentPalette() ), CurrentPalette() );
        }
        else
        {
            m_cubePlot->SetCloud( Cube_BuildCloud(
                m_cells, CurrentPalette(), CANTORDUST_CUBE_MAX_POINTS,
                breaks, static_cast< CubeValueMode_e >(
                    m_cubeValuesBox->currentIndex() ) ), CurrentPalette() );
        }
    }
    else
    {
        m_detailStack->setCurrentWidget( m_plot );
        m_plot->SetViewMode( mode );
        m_plot->SetImage( m_detailSampled
            ? cantordust::RenderOverview( mode, m_detailSampleClasses,
                m_cells, m_detailSampleOffsets,
                Resolution(), CurrentPalette() )
            : RenderDetail( mode, m_cells, Resolution(), CurrentPalette() ) );
    }

    RenderOverview();
}

void CantorPanel::RenderOverview()
{
    m_overviewPlot->SetPalette( CurrentOverviewPalette() );
    m_entropyStrip->SetPalette( CurrentOverviewPalette() );

    if ( !m_overviewToggle->isChecked() || m_totalLength == 0 )
    {
        return;
    }

    const View_e mode = static_cast< View_e >(
        m_overviewViewBox->currentData().toInt() );

    m_entropyStrip->setVisible(
        m_entropyToggle->isChecked() && mode != CDV_UNIVERSE3D );

    if ( mode == CDV_UNIVERSE3D )
    {
        LoadUniverse();
        m_overviewStack->setCurrentWidget( m_overviewUniverse );
        UniverseRenderStyle_s style;
        style.relativeNodeHeat = m_relativeNodeHeatToggle->isChecked();
        style.relativeNodeSize = m_relativeNodeSizeToggle->isChecked();
        style.antiAliasing = m_antiAliasingToggle->isChecked();
        style.universeSpacing =
            static_cast< f32 >( m_universeSpacingSlider->value() ) / 100.0f;

        if ( m_universeFocusAddress.has_value() &&
             m_universeActivePath.addresses.empty() )
        {
            // Fit an extra shell without moving individual graph nodes.
            // Branch toggles keep the same framing at a selected depth.
            style.focusScale = 1.0f / ( 1.0f + 0.68f *
                static_cast< f32 >( m_universeHopsEdit->value() - 1 ) );
        }

        style.absoluteHeatMaximum = m_universeGraph.absoluteHeatMaximum;
        m_overviewUniverse->SetRenderStyle( style );
        m_overviewFilteredGraph = Universe_FilterGraphAdvanced(
            m_universeGraph, m_universeData, CurrentAdvancedFilters() );

        m_overviewCommunities.clear();

        if ( m_universeClusterToggle->isChecked() )
        {
            UniverseCommunityResult_s community = Universe_BuildCommunities( m_overviewFilteredGraph );
            m_overviewFilteredGraph = std::move( community.graph );
            m_overviewCommunities = std::move( community.members );
        }

        m_overviewUniverse->SetGraph( &m_overviewFilteredGraph, CurrentOverviewPalette() );

        if ( m_idaFunctionAddress.has_value() )
            m_overviewUniverse->SetIdaSyncAddress(
                VisibleCommunityAddress( *m_idaFunctionAddress, true ) );

        return;
    }

    LoadOverview();
    m_overviewStack->setCurrentWidget(
        mode == CDV_CUBE3D ? static_cast< QWidget* >( m_overviewCube )
                           : static_cast< QWidget* >( m_overviewPlot ) );

    if ( m_overviewImageDirty )
    {
        if ( mode == CDV_CUBE3D )
        {
            if ( !m_overviewCubeLoaded )
            {
                m_overviewTriplets = Ida_ReadCubeOverviewRanges(
                    m_ranges, m_overviewOffset, m_overviewSpan, 65536,
                    static_cast< CubeValueMode_e >(
                        m_cubeValuesBox->currentIndex() ) );

                m_overviewCubeLoaded = true;
            }

            m_overviewCube->SetCloud( Cube_BuildTriplets(
                m_overviewTriplets, CurrentOverviewPalette() ), CurrentOverviewPalette() );
        }
        else
        {
            m_overviewPlot->SetViewMode( mode );
            m_overviewPlot->SetImage( cantordust::RenderOverview(
                mode, m_overviewClasses, m_overviewCells,
                m_overviewOffsets, Resolution(), CurrentOverviewPalette() ) );
        }

        m_entropyStrip->SetSamples( Math_EntropyProfile(
            m_overviewCells, 256 ) );

        m_overviewImageDirty = false;
    }

    m_overviewPlot->SetHighlights( BuildOverviewHighlights() );

    const u64 overviewEnd = m_overviewOffset + m_overviewSpan;
    const u64 detailEnd = m_windowOffset + m_detailSpan;
    const u64 startOffset = std::clamp(
        m_windowOffset, m_overviewOffset, overviewEnd );

    const u64 endOffset = std::clamp(
        detailEnd, m_overviewOffset, overviewEnd );

    const f64 start = static_cast< f64 >(
        static_cast< long double >( startOffset - m_overviewOffset ) /
        m_overviewSpan );

    const f64 end = static_cast< f64 >(
        static_cast< long double >( endOffset - m_overviewOffset ) /
        m_overviewSpan );

    m_entropyStrip->SetSelection( start, end );
}

void CantorPanel::UpdateOverviewVisibility()
{
    const bool enabled = m_overviewToggle->isChecked();
    m_overviewStack->setVisible( enabled );
    m_entropyStrip->setVisible(
        enabled && m_entropyToggle->isChecked() &&
        m_overviewViewBox->currentData().toInt() != CDV_UNIVERSE3D );

    if ( !enabled )
    {
        return;
    }

    QTimer::singleShot( 0, this, [this]() {
        if ( !m_overviewToggle->isChecked() )
        {
            return;
        }

        const int width = std::max(
            0, m_plotSplitter->width() - m_plotSplitter->handleWidth() );

        if ( width > 0 )
        {
            m_plotSplitter->setSizes({ width / 2, width - width / 2 });
        }
    });
}

std::vector< QRect > CantorPanel::BuildOverviewHighlights() const
{
    std::vector< QRect > highlights;

    const View_e mode = static_cast< View_e >(
        m_overviewViewBox->currentData().toInt() );

    if ( m_byteCount == 0 || ( mode != CDV_LINEAR && mode != CDV_HILBERT ) )
    {
        return highlights;
    }

    const usize side = Resolution();
    const usize pixels = side * side;
    usize first = 0;
    usize last = 0;

    const u64 overviewEnd = m_overviewOffset + m_overviewSpan;
    const u64 detailEnd = m_windowOffset + m_detailSpan;
    const u64 overlapStart = std::max(
        m_overviewOffset, m_windowOffset );

    const u64 overlapEnd = std::min(
        overviewEnd, detailEnd );

    if ( overlapStart >= overlapEnd )
    {
        return highlights;
    }

    Math_OverviewPixelSpan(
        m_overviewSpan, overlapStart - m_overviewOffset,
        overlapEnd - overlapStart, pixels, first, last );

    std::vector< bool > selected( pixels, false );

    for ( usize i = first; i < last; ++i )
    {
        const Point_s point = mode == CDV_HILBERT
            ? Math_HilbertPosition( i, side )
            : Point_s{ i % side, i / side };

        selected[ point.y * side + point.x ] = true;
    }

    for ( usize y = 0; y < side; ++y )
    {
        for ( usize x = 0; x < side; )
        {
            if ( !selected[ y * side + x ] )
            {
                ++x;
                continue;
            }

            const usize begin = x;

            while ( x < side && selected[ y * side + x ] )
            {
                ++x;
            }

            highlights.emplace_back(
                static_cast< int >( begin ), static_cast< int >( y ),
                static_cast< int >( x - begin ), 1 );
        }
    }

    return highlights;
}

void CantorPanel::InvalidateOverviewSamples()
{
    m_overviewCells.clear();
    m_overviewTriplets.clear();
    m_overviewCubeLoaded = false;
    m_overviewClasses.clear();
    m_overviewOffsets.clear();
    m_overviewImageDirty = true;
}

void CantorPanel::SetWindowOffset( const u64 offset )
{
    const u64 maxOffset = m_totalLength > m_detailSpan
        ? m_totalLength - m_detailSpan : 0;

    const u64 clamped = std::min( offset, maxOffset );
    const ZoomRange_s containing = Math_OverviewContainingDetail(
        m_totalLength, { m_overviewOffset, m_overviewSpan },
        { clamped, m_detailSpan } );

    if ( containing.start != m_overviewOffset ||
         containing.length != m_overviewSpan )
    {
        m_overviewOffset = containing.start;
        m_overviewSpan = containing.length;
        InvalidateOverviewSamples();
    }

    m_exactRequestedOffset = clamped;
    Reload();

    if ( m_viewBox->currentData().toInt() == CDV_UNIVERSE3D &&
         !m_universeFocusAddress.has_value() )
    {
        m_universePlot->ReframeToHubs();
    }
}

void CantorPanel::WheelDetail(
    const usize source, const usize ordinal,
    const usize pixels, const int steps )
{
    if ( m_totalLength == 0 || m_detailSpan == 0 )
    {
        return;
    }

    const View_e mode = static_cast< View_e >(
        m_viewBox->currentData().toInt() );

    u64 anchor = m_windowOffset + m_detailSpan / 2;

    if ( source != CANTORDUST_NO_SOURCE &&
         ( m_detailSampled
             ? source >= m_windowOffset &&
               source - m_windowOffset < m_detailSpan
             : source < m_byteCount ) )
    {
        anchor = m_detailSampled ? source : m_windowOffset + source;
    }
    else if ( ( mode == CDV_LINEAR || mode == CDV_HILBERT ) && pixels != 0 )
    {
        anchor = m_windowOffset + Math_ScaledPosition(
            m_detailSpan, static_cast< u32 >( ordinal ),
            static_cast< u32 >( pixels ) );
    }

    const ZoomRange_s next = Math_WheelZoom(
        m_totalLength, { m_windowOffset, m_detailSpan },
        anchor, 1, m_totalLength, steps );

    if ( next.start == m_windowOffset && next.length == m_detailSpan )
    {
        return;
    }

    m_detailSpan = next.length;
    SetWindowOffset( next.start );
}

void CantorPanel::WheelOverview(
    const usize source, const usize ordinal,
    const usize pixels, const int steps )
{
    if ( m_totalLength == 0 || m_overviewSpan == 0 )
    {
        return;
    }

    const View_e mode = static_cast< View_e >(
        m_overviewViewBox->currentData().toInt() );

    u64 anchor = m_overviewOffset + m_overviewSpan / 2;

    if ( source != CANTORDUST_NO_SOURCE &&
         source >= m_overviewOffset &&
         source < m_overviewOffset + m_overviewSpan )
    {
        anchor = source;
    }
    else if ( ( mode == CDV_LINEAR || mode == CDV_HILBERT ) && pixels != 0 )
    {
        anchor = m_overviewOffset + Math_ScaledPosition(
            m_overviewSpan, static_cast< u32 >( ordinal ),
            static_cast< u32 >( pixels ) );
    }

    const ZoomRange_s next = Math_WheelZoom(
        m_totalLength, { m_overviewOffset, m_overviewSpan },
        anchor, 1, m_totalLength, steps );

    if ( next.start == m_overviewOffset && next.length == m_overviewSpan )
    {
        return;
    }

    m_overviewOffset = next.start;
    m_overviewSpan = next.length;
    InvalidateOverviewSamples();
    RenderOverview();
    // Overview wheel zoom does not reload Detail, but its byte count is
    // shown in the shared status line and must not stay stale.
    UpdateWindowInfoLabel();
}

void CantorPanel::Navigate( const usize index )
{
    // Exact views use a local cell index; sampled large-range views carry
    // absolute logical offsets through the renderer/picking buffer.
    if ( m_detailSampled
        ? index == CANTORDUST_NO_SOURCE || index < m_windowOffset ||
          index - m_windowOffset >= m_detailSpan
        : index >= m_byteCount )
    {
        return;
    }

    const u64 logical = m_detailSampled
        ? static_cast< u64 >( index ) : m_windowOffset + index;

    u64 address = 0;

    if ( Math_OffsetToAddress( m_ranges, logical, address ) &&
         !jumpto( static_cast< ea_t >( address ) ) )
    {
        m_hintLabel->setText(
            QString( "Cannot navigate to %1" ).arg(
                Ida_FormatAddress( static_cast< ea_t >( address ) ) ) );
    }
}

void CantorPanel::Hover( const usize index )
{
    constexpr const char* hint =
        "2D: wheel zooms range, drag navigates. 3D: drag rotates, "
        "wheel zooms cube, double-click resets.";

    const bool validIndex = m_detailSampled
        ? index != CANTORDUST_NO_SOURCE &&
          index >= m_windowOffset && index - m_windowOffset < m_detailSpan
        : index < m_cells.size() && m_cells[ index ].valid;

    if ( !validIndex )
    {
        m_hintLabel->setText( hint );
        return;
    }

    const u64 logical = m_detailSampled
        ? static_cast< u64 >( index ) : m_windowOffset + index;

    u64 address = 0;

    if ( !Math_OffsetToAddress( m_ranges, logical, address ) )
    {
        m_hintLabel->setText( hint );
        return;
    }

    if ( m_detailSampled &&
         m_viewBox->currentData().toInt() == CDV_CUBE3D )
    {
        const auto it = std::lower_bound( m_detailCubeTriplets.begin(),
            m_detailCubeTriplets.end(), index,
            []( const CubeTriplet_s& triplet, const usize offset ) {
                return triplet.sourceIndex < offset;
            } );

        if ( it == m_detailCubeTriplets.end() || it->sourceIndex != index )
        {
            m_hintLabel->setText( hint );
            return;
        }

        m_hintLabel->setText(
            QString( "3D X=%1 Y=%2 Z=%3 | Address %4 (sampled)" )
                .arg( static_cast< unsigned int >( it->x ), 2, 16, QChar( '0' ) )
                .arg( static_cast< unsigned int >( it->y ), 2, 16, QChar( '0' ) )
                .arg( static_cast< unsigned int >( it->z ), 2, 16, QChar( '0' ) )
                .arg( Ida_FormatAddress( static_cast< ea_t >( address ) ) ) );

        return;
    }

    ByteCell_s cell;

    if ( m_detailSampled )
    {
        // Some sample slots are uninitialized and carry NO_SOURCE, so the
        // source-offset array is not sorted. Read one byte at the picked
        // logical offset rather than binary-searching that array.
        const IdaWindow_s probe = Ida_ReadWindowRanges( m_ranges, logical, 1 );

        if ( probe.cells.empty() || !probe.cells[ 0 ].valid )
        {
            m_hintLabel->setText( hint );
            return;
        }

        cell = probe.cells[ 0 ];
    }
    else
    {
        cell = m_cells[ index ];
    }

    if ( !m_detailSampled &&
         m_viewBox->currentData().toInt() == CDV_CUBE3D &&
         index + 2 < m_cells.size() )
    {
        m_hintLabel->setText(
            QString( "3D X=%1 Y=%2 Z=%3 | Address %4" )
                .arg( static_cast< unsigned int >( cell.value ),
                    2, 16, QChar( '0' ) )
                .arg( static_cast< unsigned int >( m_cells[ index + 1 ].value ),
                    2, 16, QChar( '0' ) )
                .arg( static_cast< unsigned int >( m_cells[ index + 2 ].value ),
                    2, 16, QChar( '0' ) )
                .arg( Ida_FormatAddress( static_cast< ea_t >( address ) ) ) );

        return;
    }

    m_hintLabel->setText(
        QString( "Address %1 | Logical offset +0x%2 | Byte 0x%3%4" )
            .arg( Ida_FormatAddress( static_cast< ea_t >( address ) ) )
            .arg( static_cast< qulonglong >( logical ), 0, 16 )
            .arg( static_cast< qulonglong >( cell.value ), 2, 16, QChar( '0' ) )
            .arg( m_detailSampled ? " (sampled)" : "" ) );
}

void CantorPanel::NavigateOverview( const usize offset )
{
    if ( offset >= m_totalLength )
    {
        return;
    }

    const u64 half = m_detailSpan / 2;
    SetWindowOffset( offset > half ? offset - half : 0 );
}

void CantorPanel::HoverOverview( const usize offset )
{
    u64 address = 0;

    if ( offset == CANTORDUST_NO_SOURCE ||
         !Math_OffsetToAddress( m_ranges, offset, address ) )
    {
        m_hintLabel->setText(
            "2D: wheel zooms range, drag navigates. 3D: drag rotates, wheel zooms cube, double-click resets." );

        return;
    }

    m_hintLabel->setText(
        QString( "Overview %1 | Logical offset +0x%2 (sampled)" )
            .arg( Ida_FormatAddress( static_cast< ea_t >( address ) ) )
            .arg( static_cast< qulonglong >( offset ), 0, 16 ) );
}

} // namespace cantordust
