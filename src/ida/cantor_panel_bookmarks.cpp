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

UniverseFilterOptions_s CantorPanel::CurrentAdvancedFilters() const
{
    UniverseFilterOptions_s filters;
    filters.hideImports = m_filterImports->isChecked();
    filters.hideThunks = m_filterThunks->isChecked();
    filters.hideLibraries = m_filterLibraries->isChecked();
    filters.hideGlobals = m_filterGlobals->isChecked();
    filters.minimumIncoming = static_cast< u32 >(
        m_filterMinimumIncoming->value() );

    filters.minimumOutgoing = static_cast< u32 >(
        m_filterMinimumOutgoing->value() );

    return filters;
}

void CantorPanel::LoadInvestigationBookmarks()
{
    m_investigationBookmarks = {};
    netnode store( "$ cantordust.investigations", 0, true );
    qvector< uchar > bytes;

    if ( store.getblob( &bytes, 0, 'B' ) <= 0 || bytes.empty() ||
         bytes.size() > 1024u * 1024u )
    {
        return;
    }

    const QByteArray json( reinterpret_cast< const char* >( bytes.begin() ),
        static_cast< int >( bytes.size() ) );

    const QJsonDocument doc = QJsonDocument::fromJson( json );

    if ( !doc.isObject() || doc.object().value( "version" ).toInt() != 1 )
        return;

    const QJsonArray entries = doc.object().value( "bookmarks" ).toArray();

    for ( const QJsonValue& value : entries )
        if ( value.isObject() && !value.toObject().value( "name" ).toString().isEmpty() &&
             m_investigationBookmarks.size() < 64 )
        {
            m_investigationBookmarks.append( value );
        }
}

bool CantorPanel::SaveInvestigationBookmarks()
{
    QJsonObject root;
    root.insert( "version", 1 );
    root.insert( "bookmarks", m_investigationBookmarks );

    const QByteArray bytes = QJsonDocument( root ).toJson( QJsonDocument::Compact );
    netnode store( "$ cantordust.investigations", 0, true );

    if ( !store.setblob( bytes.constData(), static_cast< usize >( bytes.size() ),
         0, 'B' ) )
    {
        QMessageBox::warning( this, "Bookmarks", "Failed to write bookmarks to this IDA database." );
        return false;
    }

    return true;
}

QJsonObject CantorPanel::CaptureInvestigation() const
{
    QJsonObject item;
    item.insert( "detailMode", m_viewBox->currentData().toInt() );
    item.insert( "overviewMode", m_overviewViewBox->currentData().toInt() );
    item.insert( "tier", m_universeCategoryBox->currentIndex() );
    item.insert( "palette", m_paletteBox->currentIndex() );
    item.insert( "overviewPalette", m_overviewPaletteBox->currentIndex() );
    item.insert( "focus", m_universeFocusAddress.has_value()
        ? QString::number( static_cast< qulonglong >( *m_universeFocusAddress ), 16 )
        : QString() );

    item.insert( "hops", m_universeHopsEdit->value() );
    item.insert( "neighbors", m_universeFocusModeBox->currentData().toInt() );
    item.insert( "focusRange", m_universeFocusRangeToggle->isChecked() );
    item.insert( "rangeHeat", m_universeRangeHeatToggle->isChecked() );
    item.insert( "search", m_universeSearchEdit->text() );
    item.insert( "searchMinRefs", m_universeMinRefsEdit->value() );
    item.insert( "searchFilter", m_universeFilterToggle->isChecked() );
    item.insert( "clusters", m_universeClusterToggle->isChecked() );
    item.insert( "nodeBudget", m_universeNodeBudgetEdit->value() );
    item.insert( "linkBudget", m_universeLinkBudgetEdit->value() );
    item.insert( "fanout", m_universeFanoutEdit->value() );
    item.insert( "hideImports", m_filterImports->isChecked() );
    item.insert( "hideThunks", m_filterThunks->isChecked() );
    item.insert( "hideLibraries", m_filterLibraries->isChecked() );
    item.insert( "hideGlobals", m_filterGlobals->isChecked() );
    item.insert( "minIncoming", m_filterMinimumIncoming->value() );
    item.insert( "minOutgoing", m_filterMinimumOutgoing->value() );
    item.insert( "cubeGeometry", m_cubeGeometryBox->currentIndex() );
    item.insert( "cubeValues", m_cubeValuesBox->currentIndex() );
    item.insert( "cubePointSize", m_cubeSizeSlider->value() );
    item.insert( "cubeBrightness", m_cubeBrightnessSlider->value() );
    item.insert( "segment", m_segmentBox->currentIndex() );
    item.insert( "offset", QString::number( static_cast< qulonglong >( m_windowOffset ), 16 ) );
    item.insert( "span", QString::number( static_cast< qulonglong >( m_detailSpan ), 16 ) );

    const CubeCamera_s camera = m_universePlot->Camera();
    const UniverseView_s view = m_universePlot->View();
    item.insert( "yaw", camera.yaw );
    item.insert( "pitch", camera.pitch );
    item.insert( "zoom", camera.zoom );
    item.insert( "panX", view.panX );
    item.insert( "panY", view.panY );
    item.insert( "panZ", view.panZ );
    QJsonArray collapsed;

    for ( const u64 address : m_universeCollapsedNodes )
        if ( collapsed.size() < 4096 )
            collapsed.append( QString::number(
                static_cast< qulonglong >( address ), 16 ) );

    item.insert( "collapsed", collapsed );

    // Preserve either endpoint even when no route exists or only one has
    // been chosen. This also allows bookmarks to resume end-first selection.
    if ( m_universePathEndpoints.start.has_value() )
        item.insert( "pathFrom", QString::number(
            static_cast< qulonglong >( *m_universePathEndpoints.start ), 16 ) );

    if ( m_universePathEndpoints.end.has_value() )
        item.insert( "pathTo", QString::number(
            static_cast< qulonglong >( *m_universePathEndpoints.end ), 16 ) );

    if ( m_universeActivePath.addresses.size() >= 2 )
        item.insert( "pathRoute", static_cast< int >( m_universeSelectedRoute ) );

    return item;
}

bool CantorPanel::RestoreInvestigation( const QJsonObject& item )
{
    LoadUniverse();
    bool ok = false;

    const QString focusText = item.value( "focus" ).toString();
    const u64 focus = focusText.toULongLong( &ok, 16 );

    if ( !focusText.isEmpty() && ( !ok ||
         m_inspectorIndex.nodes.find( focus ) == m_inspectorIndex.nodes.end() ) )
    {
        QMessageBox::warning( this, "Bookmarks", "The saved focus function is not available in the current extraction." );
        return false;
    }

    const auto selectIndex = []( QComboBox* box, int value ) {
        const int index = box->findData( value );

        if ( index >= 0 )
            box->setCurrentIndex( index );
    };
    {
        const QSignalBlocker blockSegment( m_segmentBox );
        m_segmentBox->setCurrentIndex( std::clamp( item.value( "segment" ).toInt(),
            0, m_segmentBox->count() - 1 ) );
    }

    RebuildRanges();

    const u64 offset = item.value( "offset" ).toString().toULongLong( nullptr, 16 );
    m_windowOffset = std::min( offset, m_totalLength );

    const u64 span = item.value( "span" ).toString().toULongLong( nullptr, 16 );

    if ( span )
        m_detailSpan = std::min( span, m_totalLength );

    {
        const QSignalBlocker blockTier( m_universeCategoryBox );
        const int tier = std::clamp( item.value( "tier" ).toInt(),
            0, std::max( 0, m_universeCategoryBox->count() - 1 ) );

        m_universeCategoryBox->setCurrentIndex( tier );
    }

    BuildUniverseCategory();
    {
        const QSignalBlocker blockMode( m_viewBox );
        const QSignalBlocker blockOverview( m_overviewViewBox );
        const QSignalBlocker blockFocus( m_universeFocusModeBox );
        const QSignalBlocker blockHops( m_universeHopsEdit );
        const QSignalBlocker blockPalette( m_paletteBox );
        const QSignalBlocker blockOverviewPalette( m_overviewPaletteBox );
        selectIndex( m_viewBox, item.value( "detailMode" ).toInt( CDV_UNIVERSE3D ) );
        selectIndex( m_overviewViewBox, item.value( "overviewMode" ).toInt( CDV_HILBERT ) );
        selectIndex( m_universeFocusModeBox, item.value( "neighbors" ).toInt( UFM_BOTH ) );
        m_universeHopsEdit->setValue( std::clamp( item.value( "hops" ).toInt( 1 ), 1, 10 ) );
        m_paletteBox->setCurrentIndex( std::clamp( item.value( "palette" ).toInt(),
            0, m_paletteBox->count() - 1 ) );

        m_overviewPaletteBox->setCurrentIndex( std::clamp( item.value( "overviewPalette" ).toInt(),
            0, m_overviewPaletteBox->count() - 1 ) );
    }

    {
        const QSignalBlocker blockFocusRange( m_universeFocusRangeToggle );
        const QSignalBlocker blockRangeHeat( m_universeRangeHeatToggle );
        const QSignalBlocker blockSearch( m_universeSearchEdit );
        const QSignalBlocker blockMinRefs( m_universeMinRefsEdit );
        const QSignalBlocker blockFilter( m_universeFilterToggle );
        const QSignalBlocker blockCluster( m_universeClusterToggle );
        const QSignalBlocker blockNodeBudget( m_universeNodeBudgetEdit );
        const QSignalBlocker blockLinkBudget( m_universeLinkBudgetEdit );
        const QSignalBlocker blockFanout( m_universeFanoutEdit );
        m_universeFocusRangeToggle->setChecked( item.value( "focusRange" ).toBool() );
        m_universeRangeHeatToggle->setChecked( item.value( "rangeHeat" ).toBool( true ) );
        m_universeSearchEdit->setText( item.value( "search" ).toString() );
        m_universeMinRefsEdit->setValue( item.value( "searchMinRefs" ).toInt() );
        m_universeFilterToggle->setChecked( item.value( "searchFilter" ).toBool() );
        m_universeClusterToggle->setChecked( item.value( "clusters" ).toBool() );
        m_universeNodeBudgetEdit->setValue( item.value( "nodeBudget" ).toInt(
            static_cast< int >( CANTORDUST_UNIVERSE_HOP_DEFAULT_FUNCTIONS ) ) );
        m_universeLinkBudgetEdit->setValue( item.value( "linkBudget" ).toInt(
            static_cast< int >( CANTORDUST_UNIVERSE_HOP_DEFAULT_LINKS ) ) );
        // Older bookmarks have no fanout field; retain the current setting.
        m_universeFanoutEdit->setValue( item.value( "fanout" ).toInt(
            m_universeFanoutEdit->value() ) );

        const QSignalBlocker blockImports( m_filterImports );
        const QSignalBlocker blockThunks( m_filterThunks );
        const QSignalBlocker blockLibraries( m_filterLibraries );
        const QSignalBlocker blockGlobals( m_filterGlobals );
        const QSignalBlocker blockIn( m_filterMinimumIncoming );
        const QSignalBlocker blockOut( m_filterMinimumOutgoing );
        m_filterImports->setChecked( item.value( "hideImports" ).toBool() );
        m_filterThunks->setChecked( item.value( "hideThunks" ).toBool() );
        m_filterLibraries->setChecked( item.value( "hideLibraries" ).toBool() );
        m_filterGlobals->setChecked( item.value( "hideGlobals" ).toBool() );
        m_filterMinimumIncoming->setValue( item.value( "minIncoming" ).toInt() );
        m_filterMinimumOutgoing->setValue( item.value( "minOutgoing" ).toInt() );
    }
    // Newer investigation bookmarks also preserve their byte-cloud settings;
    // older bookmarks leave the current preferences untouched.
    {
        const QSignalBlocker geometryBlock( m_cubeGeometryBox );
        const QSignalBlocker valuesBlock( m_cubeValuesBox );
        const QSignalBlocker sizeBlock( m_cubeSizeSlider );
        const QSignalBlocker brightnessBlock( m_cubeBrightnessSlider );
        m_cubeGeometryBox->setCurrentIndex( std::clamp(
            item.value( "cubeGeometry" ).toInt( m_cubeGeometryBox->currentIndex() ), 0, 3 ) );

        m_cubeValuesBox->setCurrentIndex( std::clamp(
            item.value( "cubeValues" ).toInt( m_cubeValuesBox->currentIndex() ), 0, 6 ) );

        m_cubeSizeSlider->setValue( std::clamp(
            item.value( "cubePointSize" ).toInt( m_cubeSizeSlider->value() ), 50, 200 ) );

        m_cubeBrightnessSlider->setValue( std::clamp(
            item.value( "cubeBrightness" ).toInt( m_cubeBrightnessSlider->value() ), 25, 300 ) );

        const CubeRenderOptions_s opts{
            static_cast< CubeGeometry_e >( m_cubeGeometryBox->currentIndex() ),
            m_cubeSizeSlider->value() / 100.0f,
            m_cubeBrightnessSlider->value() / 100.0f };

        m_cubePlot->SetRenderOptions( opts );
        m_overviewCube->SetRenderOptions( opts );
    }
    // A restored filter configuration becomes the active persisted preference.
    QSettings preferences( "CantorDust", "IDA" );
    preferences.setValue( "advancedHideImports", m_filterImports->isChecked() );
    preferences.setValue( "advancedHideThunks", m_filterThunks->isChecked() );
    preferences.setValue( "advancedHideLibraries", m_filterLibraries->isChecked() );
    preferences.setValue( "advancedHideGlobals", m_filterGlobals->isChecked() );
    preferences.setValue( "advancedMinimumIncoming", m_filterMinimumIncoming->value() );
    preferences.setValue( "advancedMinimumOutgoing", m_filterMinimumOutgoing->value() );
    preferences.setValue( "cubeGeometry", m_cubeGeometryBox->currentIndex() );
    preferences.setValue( "cubeValues", m_cubeValuesBox->currentIndex() );
    preferences.setValue( "cubePointSize", m_cubeSizeSlider->value() );
    preferences.setValue( "cubeBrightness", m_cubeBrightnessSlider->value() );
    preferences.setValue( "communityMode", m_universeClusterToggle->isChecked() );
    ClearUniverseRoutes();
    m_universeFocusAddress = focusText.isEmpty()
        ? std::optional< u64 >() : std::optional< u64 >( focus );

    m_universeCollapsedNodes.clear();

    for ( const QJsonValue& value : item.value( "collapsed" ).toArray() )
    {
        const u64 address = value.toString().toULongLong( &ok, 16 );

        if ( ok )
            m_universeCollapsedNodes.insert( address );
    }

    const QString pathFrom = item.value( "pathFrom" ).toString();
    const QString pathTo = item.value( "pathTo" ).toString();
    ClearUniverseRoutes();
    m_universePathEndpoints.Clear();

    bool validStart = false;
    bool validEnd = false;
    const u64 startAddress = pathFrom.toULongLong( &validStart, 16 );
    const u64 endAddress = pathTo.toULongLong( &validEnd, 16 );

    if ( validStart )
        m_universePathEndpoints.SetStart( startAddress );

    if ( validEnd )
        m_universePathEndpoints.SetEnd( endAddress );

    UpdateUniversePathToolTips();

    if ( m_universePathEndpoints.Status() == UniversePathSelectionState_e::Ready )
    {
        m_universeRoutes = Universe_FindCallPaths( m_universeData,
            startAddress, endAddress );

        if ( !m_universeRoutes.empty() )
        {
            m_universeSelectedRoute = std::min< usize >(
                static_cast< usize >( std::max( 0, item.value( "pathRoute" ).toInt() ) ),
                m_universeRoutes.size() - 1u );

            m_universeActivePath = m_universeRoutes[ m_universeSelectedRoute ];

            const QSignalBlocker blocker( m_universeRouteBox );

            for ( usize i = 0; i < m_universeRoutes.size(); ++i )
                m_universeRouteBox->addItem( QString( "Route %1 (%2)" )
                    .arg( static_cast< int >( i + 1u ) )
                    .arg( static_cast< qulonglong >( m_universeRoutes[ i ].callSites.size() ) ) );

            m_universeRouteBox->setCurrentIndex(
                static_cast< int >( m_universeSelectedRoute ) );

            m_universeRouteBox->setVisible( true );
        }
    }

    UpdateSupplementalFeatures();

    m_universePathControls->setVisible( m_viewBox->currentData().toInt() == CDV_UNIVERSE3D );
    m_universeFocusRangeToggle->setVisible( m_universeFocusAddress.has_value() );
    m_universeRangeHeatToggle->setVisible( !m_universeFocusAddress.has_value() );
    // Read the restored byte range instead of reusing stale cells from
    // the previous segment. Restore the camera after the final rebuild.
    m_cells.clear();
    m_overviewCells.clear();
    m_overviewTriplets.clear();
    m_overviewClasses.clear();
    m_overviewCubeLoaded = false;
    m_overviewImageDirty = true;
    SyncRangeControl();
    RefreshUniverseSearch();
    Reload();
    RenderCurrent();
    RenderOverview();
    m_universePlot->RestoreCamera({
        static_cast< f32 >( item.value( "yaw" ).toDouble( 0.68 ) ),
        static_cast< f32 >( item.value( "pitch" ).toDouble( 0.38 ) ),
        static_cast< f32 >( item.value( "zoom" ).toDouble( 1.0 ) ) }, {
        static_cast< f32 >( item.value( "panX" ).toDouble() ),
        static_cast< f32 >( item.value( "panY" ).toDouble() ),
        static_cast< f32 >( item.value( "panZ" ).toDouble() ) });

    return true;
}

void CantorPanel::ExportUniverseGraph( const UniverseExportFormat_e format )
{
    const bool hasDetail = m_detailStack->currentWidget() == m_universePlot &&
        m_universePlot->HasExportableGraph();

    const bool hasOverview = m_overviewStack->currentWidget() == m_overviewUniverse &&
        m_overviewUniverse->HasExportableGraph();

    if ( !hasDetail && !hasOverview )
    {
        QMessageBox::information( this, "Export Function Universe",
            "Switch the Overview or Detail view to 3D Function Universe "
            "and load a graph before exporting." );

        return;
    }

    bool overview = !hasDetail;

    if ( hasDetail && hasOverview )
    {
        bool accepted = false;

        const QString choice = QInputDialog::getItem( this,
            "Export Function Universe", "Which graph?",
            { "Overview Universe", "Detail Universe" }, 0, false, &accepted );

        if ( !accepted )
            return;

        overview = choice == "Overview Universe";
    }

    const UniverseWidget* const widget = overview ? m_overviewUniverse : m_universePlot;
    const QString defaultName = overview
        ? QStringLiteral( "cantordust-overview.png" )
        : QStringLiteral( "cantordust-detail.png" );

    const QString pngFilter = "PNG Image (*.png)";
    const QString svgFilter = "SVG Vector Image (*.svg)";
    const QString dotFilter = "Graphviz DOT (*.dot)";
    const QString chosenFormat = format == UniverseExportFormat_e::Svg
        ? svgFilter : format == UniverseExportFormat_e::Dot
        ? dotFilter : pngFilter;
    QString chosenFilter = chosenFormat;
    QString suggestedName = defaultName;

    if ( format != UniverseExportFormat_e::Choose )
    {
        suggestedName.chop( 4 );
        suggestedName += format == UniverseExportFormat_e::Svg ? ".svg"
            : format == UniverseExportFormat_e::Dot ? ".dot" : ".png";
    }

    QString filename = QFileDialog::getSaveFileName( this,
        "Export Function Universe", suggestedName,
        format == UniverseExportFormat_e::Choose
            ? pngFilter + ";;" + svgFilter + ";;" + dotFilter
            : chosenFormat,
        &chosenFilter );

    if ( filename.isEmpty() )
        return;

    const QString extension = chosenFilter == svgFilter ? ".svg"
        : chosenFilter == dotFilter ? ".dot" : ".png";
    // The save dialog may retain the initial .png name when the user changes
    // the selected filter. Keep the actual file extension consistent with the
    // encoder, rather than writing SVG/DOT bytes into a .png file.
    const QString suffix = QFileInfo( filename ).suffix();

    if ( suffix.compare( extension.mid( 1 ), Qt::CaseInsensitive ) != 0 )
    {
        if ( !suffix.isEmpty() )
            filename.chop( suffix.size() + 1 );

        filename += extension;
    }

    bool success = false;

    if ( chosenFilter == pngFilter )
    {
        // Use the existing software renderer at the best supported resolution.
        success = widget->SavePng( filename, 2048u );
    }
    else
    {
        const std::string content = chosenFilter == svgFilter
            ? widget->ExportSvg( 2048u ) : widget->ExportDot();

        if ( !content.empty() )
        {
            QSaveFile file( filename );

            if ( file.open( QIODevice::WriteOnly ) &&
                 file.write( content.data(),
                     static_cast< qint64 >( content.size() ) ) ==
                     static_cast< qint64 >( content.size() ) )
            {
                success = file.commit();
            }
        }
    }

    if ( !success )
    {
        QMessageBox::warning( this, "Export Function Universe",
            QString( "Could not export the graph to:\n%1" ).arg( filename ) );

        return;
    }

    m_hintLabel->setText( QString( "Exported %1 graph to %2" )
        .arg( overview ? "Overview" : "Detail" ).arg( filename ) );
}

void CantorPanel::ShowBookmarkManager()
{
    QDialog dialog( this );
    dialog.setWindowTitle( "Investigation Bookmarks - Current IDB" );
    dialog.setMinimumSize( 420, 260 );
    QVBoxLayout* const layout = new QVBoxLayout( &dialog );
    QLabel* const info = new QLabel( "Saved inside this IDA database. Restore a snapshot to return to its graph, controls and camera.", &dialog );
    info->setWordWrap( true );
    layout->addWidget( info );
    QListWidget* const list = new QListWidget( &dialog );
    layout->addWidget( list, 1 );

    const auto refresh = [this, list]() {
        const int index = list->currentRow();
        list->clear();

        for ( const QJsonValue& bookmark : m_investigationBookmarks )
            list->addItem( bookmark.toObject().value( "name" ).toString() );

        if ( !m_investigationBookmarks.isEmpty() )
            list->setCurrentRow( std::clamp( index, 0,
                static_cast< int >( m_investigationBookmarks.size() ) - 1 ) );
    };
    refresh();
    QHBoxLayout* const actions = new QHBoxLayout();
    QPushButton* const save = new QPushButton( "Save current", &dialog );
    QPushButton* const restore = new QPushButton( "Restore", &dialog );
    QPushButton* const rename = new QPushButton( "Rename", &dialog );
    QPushButton* const remove = new QPushButton( "Delete", &dialog );
    QPushButton* const close = new QPushButton( "Close", &dialog );

    for ( QPushButton* button : { save, restore, rename, remove, close } )
        actions->addWidget( button );

    layout->addLayout( actions );
    QObject::connect( save, &QPushButton::clicked, &dialog, [this, refresh, list, &dialog]() {
        if ( m_investigationBookmarks.size() >= 64 )
        {
            QMessageBox::warning( &dialog, "Bookmarks", "Maximum of 64 bookmarks per database." );
            return;
        }

        bool accepted = false;

        const QString name = QInputDialog::getText( &dialog, "Save investigation",
            "Bookmark name:", QLineEdit::Normal, "Investigation", &accepted ).trimmed();

        if ( !accepted || name.isEmpty() )
            return;

        QJsonObject snapshot = CaptureInvestigation();
        snapshot.insert( "name", name.left( 100 ) );
        m_investigationBookmarks.append( snapshot );

        if ( !SaveInvestigationBookmarks() )
            m_investigationBookmarks.removeLast();

        refresh();
        list->setCurrentRow( m_investigationBookmarks.size() - 1 );
    } );
    QObject::connect( restore, &QPushButton::clicked, &dialog, [this, list, &dialog]() {
        const int row = list->currentRow();

        if ( row >= 0 && row < m_investigationBookmarks.size() &&
             RestoreInvestigation( m_investigationBookmarks.at( row ).toObject() ) )
        {
            dialog.accept();
        }
    } );
    QObject::connect( rename, &QPushButton::clicked, &dialog, [this, list, refresh, &dialog]() {
        const int row = list->currentRow();

        if ( row < 0 || row >= m_investigationBookmarks.size() )
            return;

        bool accepted = false;

        const QString name = QInputDialog::getText( &dialog, "Rename bookmark",
            "Bookmark name:", QLineEdit::Normal,
            m_investigationBookmarks.at( row ).toObject().value( "name" ).toString(),
            &accepted ).trimmed();

        if ( !accepted || name.isEmpty() )
            return;

        QJsonObject entry = m_investigationBookmarks.at( row ).toObject();
        entry.insert( "name", name.left( 100 ) );
        m_investigationBookmarks.replace( row, entry );
        SaveInvestigationBookmarks();
        refresh();
        list->setCurrentRow( row );
    } );
    QObject::connect( remove, &QPushButton::clicked, &dialog, [this, list, refresh, &dialog]() {
        const int row = list->currentRow();

        if ( row < 0 || row >= m_investigationBookmarks.size() )
            return;

        if ( QMessageBox::question( &dialog, "Delete bookmark",
            "Delete the selected investigation bookmark?" ) != QMessageBox::Yes )
        {
            return;
        }

        m_investigationBookmarks.removeAt( row );
        SaveInvestigationBookmarks();
        refresh();
    } );
    QObject::connect( close, &QPushButton::clicked, &dialog, &QDialog::reject );
    QObject::connect( list, &QListWidget::itemDoubleClicked, &dialog,
        [this, list, &dialog]( QListWidgetItem* ) {
            const int row = list->currentRow();

            if ( row >= 0 && row < m_investigationBookmarks.size() &&
                 RestoreInvestigation( m_investigationBookmarks.at( row ).toObject() ) )
            {
                dialog.accept();
            }
        } );

    dialog.exec();
}

} // namespace cantordust
