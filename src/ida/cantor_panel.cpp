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

namespace
{
constexpr int kUniverseSpacingMinPercent = 25;
constexpr int kUniverseSpacingMaxPercent = 10000;
constexpr int kUniverseSpacingDefaultPercent = 1000;
}

static void Ida_AddViewOptions( QComboBox* const box )
{
    box->addItem( "Linear", QVariant::fromValue< int >( CDV_LINEAR ) );
    box->addItem( "Digraph", QVariant::fromValue< int >( CDV_DIGRAPH ) );
    box->addItem( "Histogram", QVariant::fromValue< int >( CDV_HISTOGRAM ) );
    box->addItem( "Hilbert", QVariant::fromValue< int >( CDV_HILBERT ) );
    box->setCurrentIndex( CDV_HILBERT );
}

CantorPanel::CantorPanel( QWidget* const parent )
    : QWidget( parent )
    , m_settingsButton( new QPushButton( "Settings...", this ) )
    , m_bookmarksButton( new QPushButton( "Bookmarks", this ) )
    , m_exportButton( new QPushButton( "Export...", this ) )
    , m_settingsDialog( new QDialog( this ) )
    , m_settingsTabs( new QTabWidget( m_settingsDialog ) )
    , m_filterImports( new QCheckBox( "Hide imports", m_settingsDialog ) )
    , m_filterThunks( new QCheckBox( "Hide thunks", m_settingsDialog ) )
    , m_filterLibraries( new QCheckBox( "Hide library functions", m_settingsDialog ) )
    , m_filterGlobals( new QCheckBox( "Hide globals", m_settingsDialog ) )
    , m_filterMinimumIncoming( new QSpinBox( m_settingsDialog ) )
    , m_filterMinimumOutgoing( new QSpinBox( m_settingsDialog ) )
    , m_showGlobalsToggle( new QCheckBox( "Show globals (tetrahedra)", this ) )
    , m_distinctCallersToggle( new QCheckBox( "Count unique referring functions", this ) )
    , m_globalFieldsToggle( new QCheckBox( "Rank globals by distinct fields", this ) )
    , m_segmentBox( new QComboBox( this ) )
    , m_universeCategoryBox( new QComboBox( this ) )
    , m_universeFocusModeBox( new QComboBox( this ) )
    , m_universeHopsEdit( new QSpinBox( this ) )
    , m_followIdaToggle( new QCheckBox( "Follow IDA", this ) )
    , m_idaFollowTimer( new QTimer( this ) )
    , m_universeNodeBudgetEdit( new QSpinBox( m_settingsDialog ) )
    , m_universeLinkBudgetEdit( new QSpinBox( m_settingsDialog ) )
    , m_universeExplorerPanel( new QWidget( this ) )
    , m_universePathControls( new QWidget( this ) )
    , m_universeSearchEdit( new QLineEdit( this ) )
    , m_universeMinRefsEdit( new QSpinBox( this ) )
    , m_universeFilterToggle( new QCheckBox( "Filter view", this ) )
    , m_universeClusterToggle( new QCheckBox( "Clusters", this ) )
    , m_universeSearchResults( new QComboBox( this ) )
    , m_universePathFromButton( new QPushButton( "Path from", this ) )
    , m_universePathToButton( new QPushButton( "Find path to", this ) )
    , m_universeClearPathButton( new QPushButton( "Clear path", this ) )
    , m_universeRouteBox( new QComboBox( this ) )
    , m_viewBox( new QComboBox( this ) )
    , m_overviewViewBox( new QComboBox( this ) )
    , m_resolutionBox( new QComboBox( this ) )
    , m_paletteBox( new QComboBox( this ) )
    , m_overviewPaletteBox( new QComboBox( this ) )
    , m_rangeEdit( new QLineEdit( this ) )
    , m_overviewToggle( new QCheckBox( "Overview", this ) )
    , m_entropyToggle( new QCheckBox( "Entropy", this ) )
    , m_universeFocusRangeToggle( new QCheckBox( "Focus: range only", this ) )
    , m_universeRangeHeatToggle( new QCheckBox( "Range node heat/size", this ) )
    , m_relativeNodeHeatToggle( new QCheckBox( "Relative node heat", this ) )
    , m_relativeNodeSizeToggle( new QCheckBox( "Relative node size", this ) )
    , m_organicLayoutToggle( new QCheckBox( "Organic universe layout", this ) )
    , m_antiAliasingToggle( new QCheckBox( "Anti-aliasing (3D universe)", this ) )
    , m_cubeGeometryBox( new QComboBox( m_settingsDialog ) )
    , m_cubeValuesBox( new QComboBox( m_settingsDialog ) )
    , m_cubeSizeSlider( new QSlider( Qt::Horizontal, m_settingsDialog ) )
    , m_cubeBrightnessSlider( new QSlider( Qt::Horizontal, m_settingsDialog ) )
    , m_universeSpacingSlider( new QSlider( Qt::Horizontal, this ) )
    , m_universeSpacingEdit( new QSpinBox( this ) )
    , m_infoLabel( new QLabel( this ) )
    , m_rangeAddressLabel( new QLabel( this ) )
    , m_hintLabel( new QLabel( this ) )
    , m_universeInspector( new QFrame( this ) )
    , m_inspectorTitle( new QLabel( m_universeInspector ) )
    , m_inspectorSummary( new QLabel( m_universeInspector ) )
    , m_inspectorConnections( new QScrollArea( m_universeInspector ) )
    , m_inspectorConnectionContent( new QWidget( m_inspectorConnections ) )
    , m_inspectorConnectionLayout( new QVBoxLayout( m_inspectorConnectionContent ) )
    , m_plotSplitter( new QSplitter( Qt::Horizontal, this ) )
    , m_entropyStrip( new EntropyWidget( this ) )
    , m_overviewPlot( new PlotWidget( this ) )
    , m_overviewCube( new CubeWidget( this ) )
    , m_overviewUniverse( new UniverseWidget( this ) )
    , m_overviewStack( new QStackedWidget( this ) )
    , m_plot( new PlotWidget( this ) )
    , m_cubePlot( new CubeWidget( this ) )
    , m_universePlot( new UniverseWidget( this ) )
    , m_detailStack( new QStackedWidget( this ) )
{
    QVBoxLayout* const root = new QVBoxLayout( this );
    root->setContentsMargins( 6, 6, 6, 4 );
    root->setSpacing( 4 );
    QHBoxLayout* const selectorRow = new QHBoxLayout();
    selectorRow->setSpacing( 5 );
    root->addLayout( selectorRow );
    selectorRow->addWidget( new QLabel( "Segment:", this ) );
    selectorRow->addWidget( m_segmentBox, 1 );
    // Long IDA segment names must not force the entire dock to grow.
    m_segmentBox->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Fixed );
    m_segmentBox->setMinimumWidth( 150 );

    QPushButton* const cursorButton = new QPushButton( "Cursor", this );
    QPushButton* const refreshButton = new QPushButton( "Refresh", this );
    selectorRow->addWidget( cursorButton );
    selectorRow->addWidget( refreshButton );
    // Live sync is a frequent navigation action, not an Explorer setting.
    // Reuse the existing persisted checkbox without adding another row.
    selectorRow->addWidget( m_followIdaToggle );
    m_bookmarksButton->setToolTip( "Save and restore named investigations in this IDA database." );
    selectorRow->addWidget( m_bookmarksButton );
    m_exportButton->setToolTip( "Export the displayed Function Universe as PNG, SVG, or Graphviz DOT." );
    selectorRow->addWidget( m_exportButton );
    selectorRow->addWidget( m_settingsButton );

    QHBoxLayout* const modeRow = new QHBoxLayout();
    modeRow->setSpacing( 5 );
    root->addLayout( modeRow );
    modeRow->addWidget( new QLabel( "Overview:", this ) );
    Ida_AddViewOptions( m_overviewViewBox );
    m_overviewViewBox->addItem( "3D Byte Cloud",
        QVariant::fromValue< int >( CDV_CUBE3D ) );

    m_overviewViewBox->addItem( "3D Function Universe",
        QVariant::fromValue< int >( CDV_UNIVERSE3D ) );

    modeRow->addWidget( m_overviewViewBox, 1 );
    modeRow->addWidget( new QLabel( "Detail:", this ) );
    Ida_AddViewOptions( m_viewBox );
    m_viewBox->addItem( "3D Byte Cloud",
        QVariant::fromValue< int >( CDV_CUBE3D ) );

    m_viewBox->addItem( "3D Function Universe",
        QVariant::fromValue< int >( CDV_UNIVERSE3D ) );

    m_viewBox->setCurrentIndex( m_viewBox->findData( CDV_UNIVERSE3D ) );
    modeRow->addWidget( m_viewBox, 1 );
    // Allow both mode selectors to truncate in narrow IDA docks.
    for ( QComboBox* const box : { m_overviewViewBox, m_viewBox } )
    {
        box->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Fixed );
        box->setMinimumWidth( 105 );
    }

    m_overviewViewBox->setToolTip( "Overview visualization mode." );
    m_viewBox->setToolTip( "Detail visualization mode." );
    modeRow->addWidget( new QLabel( "Range:", this ) );
    m_resolutionBox->addItem( "256 x 256", QVariant::fromValue< int >( 256 ) );
    m_resolutionBox->addItem( "512 x 512", QVariant::fromValue< int >( 512 ) );
    m_resolutionBox->addItem( "1024 x 1024", QVariant::fromValue< int >( 1024 ) );
    m_resolutionBox->setCurrentIndex( 1 );
    // Resolution is configured in Settings.

    m_overviewToggle->setChecked( true );
    m_entropyToggle->setChecked( true );

    m_universeFocusRangeToggle->setChecked( true );
    m_universeFocusRangeToggle->setVisible( false );
    m_universeFocusRangeToggle->setToolTip(
        "Filter the focused caller sphere to call instructions in the selected byte range." );

    m_universeRangeHeatToggle->setChecked( true );
    m_universeRangeHeatToggle->setVisible( false );
    m_universeRangeHeatToggle->setToolTip(
        "Compute node heat and size from references in the selected range." );

    m_relativeNodeHeatToggle->setChecked( true );
    m_relativeNodeHeatToggle->setToolTip(
        "Scale node colors to the highest visible reference count." );

    m_relativeNodeSizeToggle->setChecked( true );
    m_relativeNodeSizeToggle->setToolTip(
        "Scale node sizes between 2.5 and 12 pixels using visible counts. "
        "Disable to restore absolute reference-based sizes." );

    m_organicLayoutToggle->setChecked( false );
    m_organicLayoutToggle->setToolTip(
        "Transform the regular lattice into a rounded, irregular cloud. "
        "Disabled by default." );

    m_antiAliasingToggle->setChecked( true );
    m_antiAliasingToggle->setToolTip(
        "Smooth node edges and graph links. Disable for faster rendering." );

    m_universeSpacingSlider->setRange( kUniverseSpacingMinPercent, kUniverseSpacingMaxPercent );
    m_universeSpacingSlider->setSingleStep( 25 );
    m_universeSpacingSlider->setPageStep( 250 );
    m_universeSpacingEdit->setRange( kUniverseSpacingMinPercent, kUniverseSpacingMaxPercent );
    m_universeSpacingEdit->setSuffix( "%" );
    m_universeSpacingEdit->setToolTip(
        "Exact universe spacing percentage, 25%-10000%." );

    m_universeSpacingSlider->setToolTip(
        "Linear spacing from 25%-10000%. The initial camera frames the "
        "busiest nodes; zoom and pan remain fully adjustable." );

    for ( usize i = 0; i < Palette_Count(); ++i )
    {
        const Palette_s& palette = Palette_Get( static_cast< Palette_e >( i ) );
        QPixmap swatch( 45, 14 );
        swatch.fill( QColor::fromRgba( palette.background ) );
        QPainter painter( &swatch );

        const u32 colors[] = {
            palette.zero, palette.low, palette.printable,
            palette.high, palette.full
        };

        for ( int band = 0; band < 5; ++band )
        {
            painter.fillRect( band * 9, 0, 9, 14,
                QColor::fromRgba( colors[ band ] ) );
        }

        painter.end();

        const QIcon icon( swatch );
        const QString name = QString::fromUtf8( palette.name );
        m_paletteBox->addItem( icon, name, static_cast< int >( i ) );
        m_overviewPaletteBox->addItem( icon, name, static_cast< int >( i ) );
    }

    m_paletteBox->setMinimumContentsLength( 12 );
    m_paletteBox->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon );

    m_paletteBox->setSizePolicy(
        QSizePolicy::Preferred, QSizePolicy::Fixed );

    m_overviewPaletteBox->setMinimumContentsLength( 12 );
    m_overviewPaletteBox->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon );

    m_overviewPaletteBox->setSizePolicy(
        QSizePolicy::Preferred, QSizePolicy::Fixed );

    const QSettings settings( "CantorDust", "IDA" );
    m_universeSpacingSlider->setValue( std::clamp(
        settings.value( "universeSpacing", kUniverseSpacingDefaultPercent ).toInt(),
        kUniverseSpacingMinPercent, kUniverseSpacingMaxPercent ) );

    m_universeSpacingEdit->setValue( m_universeSpacingSlider->value() );
    m_organicLayoutToggle->setChecked(
        settings.value( "organicLayout", false ).toBool() );

    m_antiAliasingToggle->setChecked(
        settings.value( "antiAliasing", true ).toBool() );

    const int storedPalette = settings.value( "palette",
        static_cast< int >( CDP_REFERENCE ) ).toInt();

    const auto validPalette = []( const int value ) {
        return value >= 0 && value < static_cast< int >( Palette_Count() )
            ? value : static_cast< int >( CDP_REFERENCE );
    };
    m_paletteBox->setCurrentIndex( validPalette(
        settings.value( "paletteDetail", storedPalette ).toInt() ) );

    m_overviewPaletteBox->setCurrentIndex( validPalette(
        settings.value( "paletteOverview", storedPalette ).toInt() ) );

    m_entropyStrip->SetPalette( CurrentOverviewPalette() );
    m_plot->SetPalette( CurrentPalette() );
    m_overviewPlot->SetPalette( CurrentOverviewPalette() );
    m_entropyStrip->SetOnDrag( [this]( const f64 fraction ) {
        if ( m_totalLength == 0 || m_overviewSpan == 0 )
            return;

        const u64 maximum = m_totalLength > m_detailSpan
            ? m_totalLength - m_detailSpan : 0;

        const u64 delta = static_cast< u64 >(
            static_cast< long double >( fraction ) * m_overviewSpan );

        SetWindowOffset( std::min( maximum, m_overviewOffset + delta ) );
    } );

    m_distinctCallersToggle->setChecked( true );
    m_showGlobalsToggle->setChecked( true );
    m_distinctCallersToggle->setToolTip(
        "Default: repeated calls from the same function count as one reference." );

    m_showGlobalsToggle->setToolTip(
        "Show referenced globals as tetrahedra (bounded IDA scan)." );

    m_globalFieldsToggle->setChecked( false );
    m_globalFieldsToggle->setToolTip(
        "For globals, count each distinct accessed address per referring function "
        "once, even when the same field is referenced repeatedly." );

    m_settingsDialog->setWindowTitle( "Cantor Dust - Settings" );
    m_settingsDialog->setMinimumWidth( 350 );
    QVBoxLayout* const settingsLayout = new QVBoxLayout( m_settingsDialog );
    QTabWidget* const settingsTabs = m_settingsTabs;
    settingsLayout->addWidget( settingsTabs );

    QWidget* const visualPage = new QWidget( settingsTabs );
    QVBoxLayout* const visualLayout = new QVBoxLayout( visualPage );
    QFormLayout* const settingsForm = new QFormLayout();
    visualLayout->addLayout( settingsForm );
    settingsForm->addRow( "Resolution", m_resolutionBox );
    settingsForm->addRow( "Overview palette", m_overviewPaletteBox );
    settingsForm->addRow( "Detail palette", m_paletteBox );
    visualLayout->addWidget( m_overviewToggle );
    visualLayout->addWidget( m_entropyToggle );
    visualLayout->addWidget( m_showGlobalsToggle );
    visualLayout->addWidget( m_distinctCallersToggle );
    visualLayout->addWidget( m_globalFieldsToggle );
    visualLayout->addWidget( m_universeFocusRangeToggle );
    visualLayout->addWidget( m_universeRangeHeatToggle );
    visualLayout->addWidget( m_relativeNodeHeatToggle );
    visualLayout->addWidget( m_relativeNodeSizeToggle );
    visualLayout->addWidget( m_organicLayoutToggle );
    visualLayout->addWidget( m_antiAliasingToggle );
    QHBoxLayout* const spacingRow = new QHBoxLayout();
    spacingRow->addWidget( m_universeSpacingSlider, 1 );
    spacingRow->addWidget( m_universeSpacingEdit );
    settingsForm->addRow( "Universe spacing", spacingRow );
    visualLayout->addStretch();
    settingsTabs->addTab( visualPage, "Visualization" );

    // Point cloud controls are shared by Detail and Overview cube views.
    QWidget* const cubePage = new QWidget( settingsTabs );
    QVBoxLayout* const cubeLayout = new QVBoxLayout( cubePage );
    QFormLayout* const cubeForm = new QFormLayout();
    cubeLayout->addLayout( cubeForm );
    m_cubeGeometryBox->addItems( { "Cube (XYZ)", "Sphere", "Cylinder", "Tetrahedron" } );
    m_cubeValuesBox->addItems( { "u8 (trigraph)", "u16 little endian", "u16 big endian",
        "u32 little endian", "u32 big endian", "u8 difference", "u8 popcount" } );

    cubeForm->addRow( "Geometry", m_cubeGeometryBox );
    cubeForm->addRow( "Values", m_cubeValuesBox );
    m_cubeSizeSlider->setRange( 50, 200 );
    m_cubeSizeSlider->setToolTip( "Point diameter, 50-200%; 100% is the original size." );
    m_cubeBrightnessSlider->setRange( 25, 300 );
    m_cubeBrightnessSlider->setToolTip( "Point exposure, 25-300%; 100% is the original brightness." );
    auto cubePercentRow = [cubePage]( QSlider* const slider ) {
        QHBoxLayout* const row = new QHBoxLayout();
        row->setContentsMargins( 0, 0, 0, 0 );
        QLabel* const percent = new QLabel( cubePage );
        percent->setMinimumWidth( 46 );
        percent->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
        row->addWidget( slider, 1 );
        row->addWidget( percent );
        QObject::connect( slider, &QSlider::valueChanged,
            percent, [percent]( int value ) {
                percent->setText( QString::number( value ) + "%" );
            } );

        percent->setText( QString::number( slider->value() ) + "%" );
        return row;
    };
    cubeForm->addRow( "Point size", cubePercentRow( m_cubeSizeSlider ) );
    cubeForm->addRow( "Brightness", cubePercentRow( m_cubeBrightnessSlider ) );
    QLabel* const cubeHelp = new QLabel(
        "The XYZ gizmo follows the camera (X red, Y green, Z blue). "
        "Integer modes sample three consecutive scalar values from the "
        "navigable anchor; u16/u32 use the most significant byte per axis. "
        "Byte differences and bit counts can reveal other structures.", cubePage );

    cubeHelp->setWordWrap( true );
    cubeLayout->addWidget( cubeHelp );
    QPushButton* const resetCube = new QPushButton( "Reset 3D byte defaults", cubePage );
    cubeLayout->addWidget( resetCube );
    QObject::connect( resetCube, &QPushButton::clicked, this, [this]() {
        m_cubeGeometryBox->setCurrentIndex( 0 );
        m_cubeValuesBox->setCurrentIndex( 0 );
        m_cubeSizeSlider->setValue( 100 );
        m_cubeBrightnessSlider->setValue( 100 );
    } );
    cubeLayout->addStretch();
    settingsTabs->addTab( cubePage, "3D Byte View" );

    const QSettings cubeSettings( "CantorDust", "IDA" );
    m_cubeGeometryBox->setCurrentIndex( std::clamp(
        cubeSettings.value( "cubeGeometry", 0 ).toInt(), 0, 3 ) );

    m_cubeValuesBox->setCurrentIndex( std::clamp(
        cubeSettings.value( "cubeValues", 0 ).toInt(), 0, 6 ) );

    m_cubeSizeSlider->setValue( std::clamp(
        cubeSettings.value( "cubePointSize", 100 ).toInt(), 50, 200 ) );

    m_cubeBrightnessSlider->setValue( std::clamp(
        cubeSettings.value( "cubeBrightness", 100 ).toInt(), 25, 300 ) );

    const auto updateCubeRender = [this]() {
        const CubeRenderOptions_s opts{
            static_cast< CubeGeometry_e >( m_cubeGeometryBox->currentIndex() ),
            m_cubeSizeSlider->value() / 100.0f,
            m_cubeBrightnessSlider->value() / 100.0f };

        m_cubePlot->SetRenderOptions( opts );
        m_overviewCube->SetRenderOptions( opts );
        QSettings preferences( "CantorDust", "IDA" );
        preferences.setValue( "cubeGeometry", m_cubeGeometryBox->currentIndex() );
        preferences.setValue( "cubePointSize", m_cubeSizeSlider->value() );
        preferences.setValue( "cubeBrightness", m_cubeBrightnessSlider->value() );
    };
    QObject::connect( m_cubeGeometryBox, QOverload<int>::of( &QComboBox::currentIndexChanged ),
        this, [updateCubeRender]( int ) { updateCubeRender(); } );

    QObject::connect( m_cubeSizeSlider, &QSlider::valueChanged,
        this, [updateCubeRender]( int ) { updateCubeRender(); } );

    QObject::connect( m_cubeBrightnessSlider, &QSlider::valueChanged,
        this, [updateCubeRender]( int ) { updateCubeRender(); } );

    QObject::connect( m_cubeValuesBox, QOverload<int>::of( &QComboBox::currentIndexChanged ),
        this, [this]( int value ) {
            QSettings( "CantorDust", "IDA" ).setValue( "cubeValues", value );
            m_overviewCubeLoaded = false;
            m_overviewImageDirty = true;
            // Large cube views are sampled from the whole selected range.
            // Changing the scalar decoder must regenerate those samples.
            if ( m_totalLength != 0 )
                Reload();
        } );

    updateCubeRender();

    // Keep exploration budgets out of the compact main toolbar. Changes
    // apply immediately to an existing focus and persist between sessions.
    QWidget* const explorerPage = new QWidget( settingsTabs );
    QVBoxLayout* const explorerLayout = new QVBoxLayout( explorerPage );
    QFormLayout* const explorerForm = new QFormLayout();
    explorerLayout->addLayout( explorerForm );
    m_universeNodeBudgetEdit->setRange( 64,
        CANTORDUST_UNIVERSE_MAX_FUNCTIONS );

    m_universeNodeBudgetEdit->setSingleStep( 128 );
    m_universeNodeBudgetEdit->setValue( std::clamp(
        settings.value( "explorerNodeBudget", 768 ).toInt(), 64,
        static_cast< int >( CANTORDUST_UNIVERSE_MAX_FUNCTIONS ) ) );

    m_universeNodeBudgetEdit->setToolTip(
        "Maximum visible nodes when adding hops. Existing direct neighbors "
        "are always retained, even if they exceed this budget." );

    explorerForm->addRow( "Node budget", m_universeNodeBudgetEdit );
    m_universeLinkBudgetEdit->setRange( 128,
        CANTORDUST_UNIVERSE_MAX_LINKS );

    m_universeLinkBudgetEdit->setSingleStep( 500 );
    m_universeLinkBudgetEdit->setValue( std::clamp(
        settings.value( "explorerLinkBudget", 3500 ).toInt(), 128,
        static_cast< int >( CANTORDUST_UNIVERSE_MAX_LINKS ) ) );

    m_universeLinkBudgetEdit->setToolTip(
        "Maximum visible directed links when adding hops. Existing "
        "direct connections are always retained." );

    explorerForm->addRow( "Link budget", m_universeLinkBudgetEdit );
    // Passive highlighting remains on regardless of this toolbar toggle.
    // Follow IDA is opt-in and its state persists across plugin restarts.
    m_followIdaToggle->setChecked(
        settings.value( "followIda", false ).toBool() );

    m_followIdaToggle->setToolTip(
        "When IDA changes functions, automatically focus that function in "
        "the Detail universe. Off: highlight only, without moving the "
        "camera, switching tiers, or rebuilding the graph." );

    m_idaFollowTimer->setSingleShot( true );
    m_idaFollowTimer->setInterval( 170 );
    QObject::connect( m_idaFollowTimer, &QTimer::timeout,
        this, [this]() { FollowIdaFunction(); } );

    QObject::connect( m_followIdaToggle, &QCheckBox::toggled,
        this, [this]( const bool enabled ) {
            QSettings( "CantorDust", "IDA" ).setValue(
                "followIda", enabled );

            if ( !enabled )
                m_idaFollowTimer->stop();

            else if ( m_idaFunctionAddress.has_value() )
                m_idaFollowTimer->start();
        } );

    QLabel* const explorerHelp = new QLabel(
        "Follow IDA is in the top toolbar. When off, IDA navigation "
        "highlights visible function nodes without moving the graph. "
        "Hops 1-10 are selected in the main explorer. These budgets limit "
        "graph growth at hops 2-10; at most 24 new functions are added "
        "per expanded node. A limit warning appears in the detail status "
        "when expansion is cut short. Increasing budgets may slow "
        "rendering for large binaries.", explorerPage );

    explorerHelp->setWordWrap( true );
    explorerLayout->addWidget( explorerHelp );
    explorerLayout->addStretch();
    settingsTabs->addTab( explorerPage, "Explorer" );
    QObject::connect( m_universeNodeBudgetEdit,
        QOverload< int >::of( &QSpinBox::valueChanged ), this,
        [this]( const int value ) {
            QSettings( "CantorDust", "IDA" ).setValue(
                "explorerNodeBudget", value );

            if ( m_universeFocusAddress.has_value() )
                RenderCurrent();
        } );

    QObject::connect( m_universeLinkBudgetEdit,
        QOverload< int >::of( &QSpinBox::valueChanged ), this,
        [this]( const int value ) {
            QSettings( "CantorDust", "IDA" ).setValue(
                "explorerLinkBudget", value );

            if ( m_universeFocusAddress.has_value() )
                RenderCurrent();
        } );

    // Advanced filters live in Settings, with a small toolbar shortcut.
    // They are distinct from the text-search "Filter view" checkbox.
    QWidget* const filterPage = new QWidget( settingsTabs );
    QVBoxLayout* const filterLayout = new QVBoxLayout( filterPage );
    QLabel* const filterHelp = new QLabel(
        "Exclude function categories and low-activity functions from the "
        "graph. The full reference extraction and inspector remain intact. "
        "A focused center is always retained, and call-path views are never pruned.",
        filterPage );

    filterHelp->setWordWrap( true );
    filterLayout->addWidget( filterHelp );
    QSettings filterSettings( "CantorDust", "IDA" );

    for ( auto entry : {
        std::make_pair( m_filterImports, "advancedHideImports" ),
        std::make_pair( m_filterThunks, "advancedHideThunks" ),
        std::make_pair( m_filterLibraries, "advancedHideLibraries" ),
        std::make_pair( m_filterGlobals, "advancedHideGlobals" ) } )
    {
        entry.first->setChecked( filterSettings.value( entry.second, false ).toBool() );
        filterLayout->addWidget( entry.first );
        QObject::connect( entry.first, &QCheckBox::toggled,
            this, [this, key = QString( entry.second )]( bool enabled ) {
                QSettings( "CantorDust", "IDA" ).setValue( key, enabled );
                RenderCurrent();
                RenderOverview();
            } );
    }

    QFormLayout* const filterForm = new QFormLayout();

    for ( auto entry : {
        std::make_pair( m_filterMinimumIncoming, "advancedMinimumIncoming" ),
        std::make_pair( m_filterMinimumOutgoing, "advancedMinimumOutgoing" ) } )
    {
        entry.first->setRange( 0, 250000 );
        entry.first->setValue( filterSettings.value( entry.second, 0 ).toInt() );
        QObject::connect( entry.first, QOverload< int >::of( &QSpinBox::valueChanged ),
            this, [this, key = QString( entry.second )]( int value ) {
                QSettings( "CantorDust", "IDA" ).setValue( key, value );
                RenderCurrent();
                RenderOverview();
            } );
    }

    filterForm->addRow( "Minimum incoming", m_filterMinimumIncoming );
    filterForm->addRow( "Minimum outgoing", m_filterMinimumOutgoing );
    filterLayout->addLayout( filterForm );
    QLabel* const filterNote = new QLabel(
        "Counts use extracted references across all functions, not just the "
        "currently visible tier. Globals are unaffected by the function "
        "reference thresholds.", filterPage );

    filterNote->setWordWrap( true );
    filterLayout->addWidget( filterNote );
    filterLayout->addStretch();
    settingsTabs->addTab( filterPage, "Graph Filters" );

    QWidget* const aboutPage = new QWidget( settingsTabs );
    QVBoxLayout* const aboutLayout = new QVBoxLayout( aboutPage );
    QLabel* const aboutLabel = new QLabel( aboutPage );
    aboutLabel->setTextFormat( Qt::RichText );
    aboutLabel->setTextInteractionFlags( Qt::TextBrowserInteraction );
    aboutLabel->setOpenExternalLinks( true );
    aboutLabel->setWordWrap( true );
    aboutLabel->setText(
        "<div align='center'>"

        "<h3 align='center' style='margin-bottom: 2px;'>Cantor Dust</h3>"

        "<p align='center' style='margin-top: 0px; margin-bottom: 18px; "
        "font-size: medium; font-style: italic;'>"
        "Created by Kawe Mazidjatari</p>"

        "<p align='center' style='margin-bottom: 18px;'>"
        "Interactive binary visualization for IDA. Explore raw bytes, "
        "entropy, hidden structures, function relationships, and "
        "cross-references through interactive 2D and 3D views.</p>"

        "<h4 align='center' style='margin-bottom: 6px;'>"
        "Inspiration &amp; acknowledgements</h4>"

        "<p align='center' style='margin-top: 0px; margin-bottom: 10px;'>"
        "<a href='https://github.com/Battelle/cantordust'>"
        "Battelle CantorDust</a> - Original concept by Chris Domas. "
        "Ghidra port by AJ Snedden and Mike Sengelmann.</p>"

        "<p align='center' style='margin-top: 0px; margin-bottom: 18px;'>"
        "<a href='https://github.com/kentavv/binary_viewer'>"
        "binary_viewer</a> - Created by Kent A. Vander Velden. "
        "Inspiration for byte classification and visualization.</p>"

        "<h4 align='center' style='margin-bottom: 6px;'>"
        "Implementation &amp; licensing</h4>"

        "<p align='center' style='margin-top: 0px; margin-bottom: 10px;'>"
        "Independently implemented in C++, using the Hex-Rays IDA SDK "
        "and Qt 6 (separately licensed). No source code from the projects "
        "above is included.</p>"

        "<p align='center' style='margin-top: 0px; margin-bottom: 18px; "
        "font-size: small;'>"
        "Open source under the MIT License.</p>"

        "<hr>"

        "<p align='center' style='margin-top: 12px;'>"
        "<a href='https://ko-fi.com/amos0'>"
        "Support development on Ko-fi</a></p>"

        "</div>" );

    aboutLayout->addWidget( aboutLabel );
    aboutLayout->addStretch();
    settingsTabs->addTab( aboutPage, "About/Credits" );

    QPushButton* const closeSettings = new QPushButton( "Close", m_settingsDialog );
    settingsLayout->addWidget( closeSettings );
    QObject::connect( closeSettings, &QPushButton::clicked,
        m_settingsDialog, &QDialog::hide );

    QObject::connect( m_exportButton, &QPushButton::clicked,
        this, [this]() { ExportUniverseGraph(); } );

    QObject::connect( m_bookmarksButton, &QPushButton::clicked,
        this, [this]() { ShowBookmarkManager(); } );

    QObject::connect( m_settingsButton, &QPushButton::clicked,
        this, [this]() {
            m_settingsDialog->show();
            m_settingsDialog->raise();
            m_settingsDialog->activateWindow();
        } );

    m_rangeEdit->setValidator( new QRegularExpressionValidator(
        QRegularExpression( QStringLiteral( "[0-9]+" ) ), m_rangeEdit ) );

    m_rangeEdit->setMinimumWidth( 100 );
    m_rangeEdit->setMaximumWidth( 135 );
    m_rangeEdit->setToolTip(
        "Enter a byte count and press Enter. Clamped to the selected segment's logical size." );

    modeRow->addWidget( m_rangeEdit );
    modeRow->addWidget( new QLabel( "B", this ) );
    m_universeFocusModeBox->addItem( "Callers",
        QVariant::fromValue< int >( UFM_CALLERS ) );

    m_universeFocusModeBox->addItem( "Both",
        QVariant::fromValue< int >( UFM_BOTH ) );

    m_universeFocusModeBox->addItem( "Callees",
        QVariant::fromValue< int >( UFM_CALLEES ) );

    m_universeFocusModeBox->setCurrentIndex( std::clamp(
        settings.value( "focusMode", static_cast< int >( UFM_BOTH ) ).toInt(),
        0, 2 ) );

    m_universeFocusModeBox->setToolTip(
        "Which neighbors to display when focused: palette-colored callers, "
        "palette-accented callees, or both (including reciprocal functions)." );

    m_universeCategoryBox->setToolTip(
        "Reference tiers: each shows up to 4096 retained nodes, from busiest to quietest." );

    m_universeCategoryBox->setMinimumContentsLength( 9 );
    m_universeCategoryBox->setSizePolicy(
        QSizePolicy::Preferred, QSizePolicy::Fixed );

    m_universeCategoryBox->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon );

    m_universeCategoryBox->setVisible( false );
    QVBoxLayout* const exploreLayout = new QVBoxLayout(
        m_universeExplorerPanel );

    exploreLayout->setContentsMargins( 0, 0, 0, 0 );
    exploreLayout->setSpacing( 4 );
    QHBoxLayout* const searchRow = new QHBoxLayout();
    searchRow->setSpacing( 5 );
    searchRow->addWidget( new QLabel( "Find:", this ) );
    m_universeSearchEdit->setPlaceholderText( "Function name / hex address" );
    m_universeSearchEdit->setClearButtonEnabled( true );
    m_universeSearchEdit->setMinimumWidth( 120 );
    searchRow->addWidget( m_universeSearchEdit, 1 );
    searchRow->addWidget( new QLabel( "Min refs:", this ) );
    m_universeMinRefsEdit->setRange( 0, 250000 );
    m_universeMinRefsEdit->setMaximumWidth( 85 );
    searchRow->addWidget( m_universeMinRefsEdit );
    searchRow->addWidget( m_universeFilterToggle );
    QPushButton* const advancedFilterButton = new QPushButton( "Filters...", this );
    advancedFilterButton->setToolTip( "Function-type and reference-count filters." );
    searchRow->addWidget( advancedFilterButton );
    QObject::connect( advancedFilterButton, &QPushButton::clicked,
        this, [this, filterPage]() {
            m_settingsTabs->setCurrentWidget( filterPage );
            m_settingsDialog->show();
            m_settingsDialog->raise();
            m_settingsDialog->activateWindow();
        } );

    searchRow->addWidget( new QLabel( "Tier:", this ) );
    searchRow->addWidget( m_universeCategoryBox );
    m_universeClusterToggle->setToolTip(
        "Group strongly interconnected functions in the Overview and "
        "unfocused Detail. Right-click a group to choose a member function." );

    m_universeClusterToggle->setChecked(
        settings.value( "communityMode", false ).toBool() );

    searchRow->addWidget( m_universeClusterToggle );
    QObject::connect( m_universeClusterToggle, &QCheckBox::toggled,
        this, [this]( bool checked ) {
            QSettings preferences( "CantorDust", "IDA" );
            preferences.setValue( "communityMode", checked );
            RenderCurrent();
            RenderOverview();
            m_universePlot->ReframeToHubs();
            m_overviewUniverse->ReframeToHubs();
        } );

    exploreLayout->addLayout( searchRow );
    QHBoxLayout* const resultRow = new QHBoxLayout();
    resultRow->setSpacing( 5 );
    m_universeSearchResults->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Fixed );

    m_universeSearchResults->setToolTip(
        "Up to 200 matches from all extracted functions, ranked by references." );

    resultRow->addWidget( m_universeSearchResults, 1 );
    QPushButton* const jumpResult = new QPushButton( "Jump", this );
    QPushButton* const focusResult = new QPushButton( "Focus", this );
    resultRow->addWidget( jumpResult );
    resultRow->addWidget( focusResult );
    resultRow->addWidget( new QLabel( "Neighbors:", this ) );
    m_universeFocusModeBox->setMinimumWidth( 90 );
    resultRow->addWidget( m_universeFocusModeBox );
    resultRow->addWidget( new QLabel( "Hops:", this ) );
    m_universeHopsEdit->setRange( 1, 10 );
    m_universeHopsEdit->setValue( 1 );
    m_universeHopsEdit->setFixedWidth( 52 );
    m_universeHopsEdit->setToolTip(
        "1: original focused neighbors. 2-10: expand in the selected "
        "direction. Node/link budgets are configurable in Settings > "
        "Explorer. Shift + right-click a node to toggle its branch." );

    resultRow->addWidget( m_universeHopsEdit );
    exploreLayout->addLayout( resultRow );
    jumpResult->setToolTip( "Jump to the selected function in IDA." );
    focusResult->setToolTip( "Focus the selected function in the 3D universe." );
    m_universePathFromButton->setToolTip(
        "Set the path start to the current IDA function. To use a search "
        "result instead, type a query or choose a result first." );

    m_universePathToButton->setToolTip(
        "Find up to five directed routes to the current IDA function. "
        "A freshly selected search result overrides the IDA cursor." );

    m_universeClearPathButton->setToolTip(
        "Exit the call path view and restore the universe graph." );

    root->addWidget( m_universeExplorerPanel );

    // Only user interaction with search chooses it as the next path
    // endpoint. Automatic re-ranking of search results must not override
    // navigating in IDA (the default path workflow).
    QObject::connect( m_universeSearchEdit, &QLineEdit::textEdited,
        this, [this]( const QString& ) { m_pathPreferSearchResult = true; } );

    QObject::connect( m_universeSearchResults,
        QOverload< int >::of( &QComboBox::activated ), this,
        [this]( int ) { m_pathPreferSearchResult = true; } );

    QObject::connect( m_universeSearchEdit, &QLineEdit::textChanged,
        this, [this]( const QString& ) {
            RefreshUniverseSearch();

            if ( m_universeFilterToggle->isChecked() )
                RenderCurrent();
        } );

    QObject::connect( m_universeMinRefsEdit,
        QOverload< int >::of( &QSpinBox::valueChanged ),
        this, [this]( const int ) {
            RefreshUniverseSearch();

            if ( m_universeFilterToggle->isChecked() )
                RenderCurrent();
        } );

    QObject::connect( m_universeFilterToggle, &QCheckBox::toggled,
        this, [this]( const bool ) { RenderCurrent(); } );

    QObject::connect( m_universeFocusModeBox,
        QOverload< int >::of( &QComboBox::currentIndexChanged ),
        this, [this]( const int mode ) {
            QSettings settings( "CantorDust", "IDA" );
            settings.setValue( "focusMode", mode );

            if ( m_universeFocusAddress.has_value() )
            {
                m_universeCollapsedNodes.clear();

                const QSignalBlocker blocker( m_universeFocusRangeToggle );
                m_universeFocusRangeToggle->setChecked(
                    m_universeFocusModeBox->currentData().toInt() ==
                    UFM_CALLERS && m_universeHopsEdit->value() == 1 );

                RenderCurrent();
                m_universePlot->ReframeToHubs();
            }
        } );

    QObject::connect( m_universeHopsEdit,
        QOverload< int >::of( &QSpinBox::valueChanged ),
        this, [this]( const int hops ) {
            if ( !m_universeFocusAddress.has_value() )
                return;
            // Range-only callers remain the v51 default at depth 1, but
            // multi-hop exploration normally needs the complete call graph.
            if ( hops > 1 && m_universeFocusRangeToggle->isChecked() )
            {
                const QSignalBlocker blocker( m_universeFocusRangeToggle );
                m_universeFocusRangeToggle->setChecked( false );
            }

            RenderCurrent();
        } );

    QObject::connect( jumpResult, &QPushButton::clicked,
        this, [this]() {
            const std::optional< u64 > address = SearchResultAddress();

            if ( !address.has_value() )
                return;

            LoadUniverse();

            const usize rank = Universe_RankForAddress(
                m_universeData, *address,
                m_distinctCallersToggle->isChecked(),
                m_globalFieldsToggle->isChecked() );

            if ( rank != CANTORDUST_NO_SOURCE )
                m_universeCategoryBox->setCurrentIndex( static_cast< int >(
                    rank / CANTORDUST_UNIVERSE_MAX_FUNCTIONS ) );

            NavigateUniverse( *address );
            m_overviewUniverse->CenterOnAddress( *address );

            if ( !m_universePlot->CenterOnAddress( *address ) )
                m_hintLabel->setText(
                    "Jumped in IDA. Focus to show a node outside the detail range." );
        } );

    QObject::connect( focusResult, &QPushButton::clicked,
        this, [this]() {
            if ( const std::optional< u64 > address = SearchResultAddress() )
                FocusUniverse( *address );
        } );

    QObject::connect( m_universePathFromButton, &QPushButton::clicked,
        this, [this]() {
            const std::optional< UniversePathEndpoint_s > endpoint = PathEndpointSelection();

            if ( !endpoint.has_value() )
            {
                m_hintLabel->setText( "Navigate to a function in IDA or "
                    "choose a function in search first." );

                return;
            }

            m_universePathFrom = endpoint->address;

            const QString origin = endpoint->fromSearch
                ? QStringLiteral( "search result" )
                : QStringLiteral( "IDA cursor" );

            m_universePathFromButton->setToolTip( QString( "Start: %1 (%2)" )
                .arg( Ida_FormatAddress( static_cast< ea_t >( endpoint->address ) ) )
                .arg( origin ) );

            m_hintLabel->setText( QString( "Path start: %1 (%2). Navigate "
                "to another function in IDA or choose a search result, "
                "then click Find path to." )
                .arg( Ida_FormatAddress( static_cast< ea_t >( endpoint->address ) ) )
                .arg( origin ) );
        } );

    QObject::connect( m_universePathToButton, &QPushButton::clicked,
        this, [this]() {
            if ( !m_universePathFrom.has_value() )
            {
                m_hintLabel->setText( "Choose Path from first." );
                return;
            }

            const std::optional< UniversePathEndpoint_s > endpoint = PathEndpointSelection();

            if ( !endpoint.has_value() )
            {
                m_hintLabel->setText( "Navigate to the destination in IDA "
                    "or choose a search result." );

                return;
            }

            if ( *m_universePathFrom == endpoint->address )
            {
                m_hintLabel->setText( "Path start and destination are the "
                    "same function. Navigate to a different function in IDA "
                    "or select another search result." );

                return;
            }

            ShowUniversePath( *m_universePathFrom, endpoint->address );
        } );

    QObject::connect( m_universeClearPathButton, &QPushButton::clicked,
        this, [this]() {
            ClearUniverseRoutes();
            m_universePathFrom.reset();
            m_universePathFromButton->setToolTip( QString() );
            RenderCurrent();
            m_universePlot->ReframeToHubs();
        } );

    // Address text must never change the dock's minimum width.
    m_infoLabel->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred );

    m_infoLabel->setMinimumWidth( 0 );
    QHBoxLayout* const detailStatusRow = new QHBoxLayout();
    detailStatusRow->setContentsMargins( 0, 0, 0, 0 );
    detailStatusRow->addWidget( m_infoLabel, 1 );
    QHBoxLayout* const pathRow = new QHBoxLayout( m_universePathControls );
    pathRow->setContentsMargins( 0, 0, 0, 0 );
    pathRow->setSpacing( 4 );
    pathRow->addWidget( m_universePathFromButton );
    pathRow->addWidget( m_universePathToButton );
    pathRow->addWidget( m_universeClearPathButton );
    m_universeRouteBox->setMinimumWidth( 82 );
    m_universeRouteBox->setMaximumWidth( 145 );
    m_universeRouteBox->setToolTip(
        "Select a directed call route. All alternatives remain visible." );

    m_universeRouteBox->setVisible( false );
    pathRow->addWidget( m_universeRouteBox );
    QObject::connect( m_universeRouteBox,
        QOverload< int >::of( &QComboBox::currentIndexChanged ),
        this, [this]( int index ) { SelectUniverseRoute( index ); } );

    m_universePathControls->setVisible( false );
    detailStatusRow->addWidget( m_universePathControls, 0, Qt::AlignRight );
    root->addLayout( detailStatusRow );
    m_rangeAddressLabel->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred );

    m_rangeAddressLabel->setMinimumWidth( 0 );
    QHBoxLayout* const rangeAddressRow = new QHBoxLayout();
    rangeAddressRow->addWidget( m_rangeAddressLabel, 1 );
    QLabel* const supportLink = new QLabel( this );
    supportLink->setTextFormat( Qt::RichText );
    supportLink->setTextInteractionFlags( Qt::TextBrowserInteraction );
    supportLink->setOpenExternalLinks( true );

    const bool darkBackground =
        supportLink->palette().color( QPalette::Window ).lightness() < 128;

    const QString supportColor = darkBackground
        ? QStringLiteral( "#79EAFF" ) : QStringLiteral( "#005A82" );

    supportLink->setText( QStringLiteral(
        "<a href='https://ko-fi.com/amos0' "
        "style='color:%1; text-decoration:underline; "
        "font-weight:600;'>&#9825; Support developer</a>" )
        .arg( supportColor ) );

    supportLink->setToolTip( "Support Cantor Dust on Ko-fi (opens browser)" );
    supportLink->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Preferred );
    rangeAddressRow->addWidget( supportLink, 0, Qt::AlignRight );
    root->addLayout( rangeAddressRow );

    QHBoxLayout* const canvasRow = new QHBoxLayout();
    canvasRow->setContentsMargins( 0, 0, 0, 0 );
    canvasRow->setSpacing( 2 );
    canvasRow->addWidget( m_entropyStrip );
    canvasRow->addWidget( m_plotSplitter, 1 );
    root->addLayout( canvasRow, 1 );

    // Use normal Qt controls instead of a QTreeWidget: certain IDA themes
    // show the tree scrollbar but fail to paint its item text/rows.
    // This panel is fully hidden until Ctrl+left-click selects a node.
    m_universeInspector->setObjectName( "cantorInspector" );
    m_universeInspector->setFrameShape( QFrame::StyledPanel );
    QVBoxLayout* const inspectorLayout = new QVBoxLayout( m_universeInspector );
    inspectorLayout->setContentsMargins( 6, 4, 6, 4 );
    inspectorLayout->setSpacing( 3 );
    QHBoxLayout* const inspectorHeader = new QHBoxLayout();
    m_inspectorTitle->setObjectName( "cantorInspectorTitle" );
    m_inspectorTitle->setTextFormat( Qt::PlainText );
    m_inspectorTitle->setTextInteractionFlags( Qt::TextSelectableByMouse );
    m_inspectorTitle->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
    m_inspectorTitle->setMinimumWidth( 0 );
    inspectorHeader->addWidget( m_inspectorTitle, 1 );
    QPushButton* const inspectorClose = new QPushButton( "Close", m_universeInspector );
    inspectorClose->setObjectName( "cantorInspectorClose" );
    inspectorClose->setToolTip( "Close the inspector; Ctrl+left-click a node to reopen." );
    inspectorClose->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
    inspectorHeader->addWidget( inspectorClose );
    inspectorLayout->addLayout( inspectorHeader );
    m_inspectorSummary->setObjectName( "cantorInspectorSummary" );
    m_inspectorSummary->setTextFormat( Qt::PlainText );
    m_inspectorSummary->setWordWrap( true );
    m_inspectorSummary->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
    m_inspectorSummary->setMinimumWidth( 0 );
    m_inspectorSummary->setTextInteractionFlags( Qt::TextSelectableByMouse );
    inspectorLayout->addWidget( m_inspectorSummary );
    m_inspectorConnections->setObjectName( "cantorInspectorScroller" );
    m_inspectorConnections->setWidgetResizable( true );
    m_inspectorConnections->setFrameShape( QFrame::NoFrame );
    m_inspectorConnections->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    m_inspectorConnections->setMinimumHeight( 88 );
    m_inspectorConnections->setMaximumHeight( 150 );
    m_inspectorConnections->setToolTip(
        "Expand a connection to reveal callsites; click a callsite to jump in IDA." );

    m_inspectorConnectionContent->setObjectName( "cantorInspectorContent" );
    m_inspectorConnectionContent->setMinimumWidth( 0 );
    m_inspectorConnectionLayout->setContentsMargins( 2, 2, 2, 2 );
    m_inspectorConnectionLayout->setSpacing( 2 );
    m_inspectorConnectionLayout->setAlignment( Qt::AlignTop );
    m_inspectorConnections->setWidget( m_inspectorConnectionContent );
    inspectorLayout->addWidget( m_inspectorConnections );

    // Respect IDA's active Qt palette and stylesheet instead of freezing
    // inspector colors at construction time. Native Qt controls update with
    // live IDA light/dark theme changes; only the title weight is customized.
    // Keep the v53.2 scrollable button rows: the former QTreeWidget was
    // present but its entries were not visible in the user's IDA build.
    QFont inspectorTitleFont = m_inspectorTitle->font();
    inspectorTitleFont.setBold( true );
    m_inspectorTitle->setFont( inspectorTitleFont );
    m_universeInspector->setVisible( false );
    root->addWidget( m_universeInspector );
    QObject::connect( inspectorClose, &QPushButton::clicked,
        this, [this]() { HideUniverseInspector(); } );

    m_plotSplitter->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Expanding );

    m_plotSplitter->setMinimumWidth( 0 );
    m_overviewStack->addWidget( m_overviewPlot );
    m_overviewStack->addWidget( m_overviewCube );
    m_overviewStack->addWidget( m_overviewUniverse );
    m_plotSplitter->addWidget( m_overviewStack );
    m_detailStack->addWidget( m_plot );
    m_detailStack->addWidget( m_cubePlot );
    m_detailStack->addWidget( m_universePlot );
    m_plotSplitter->addWidget( m_detailStack );
    m_plotSplitter->setChildrenCollapsible( false );
    m_plotSplitter->setStretchFactor( 0, 1 );
    m_plotSplitter->setStretchFactor( 1, 1 );

    m_hintLabel->setText(
        "Universe: hover for details; left-click jumps; "
        "Ctrl+left-click inspects; right-click focuses; Esc exits focus." );

    m_hintLabel->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred );

    m_hintLabel->setMinimumWidth( 0 );
    root->addWidget( m_hintLabel );

    m_universePlot->SetOnNavigate( [this]( const u64 address ) {
        NavigateUniverseCaller( address );
    } );
    m_overviewUniverse->SetOnNavigate( [this]( const u64 address ) {
        NavigateUniverseOverview( address );
    } );

    for ( QWidget* const canvas : {
        static_cast< QWidget* >( m_plot ),
        static_cast< QWidget* >( m_overviewPlot ),
        static_cast< QWidget* >( m_cubePlot ),
        static_cast< QWidget* >( m_overviewCube ),
        static_cast< QWidget* >( m_universePlot ),
        static_cast< QWidget* >( m_overviewUniverse ),
        static_cast< QWidget* >( m_entropyStrip ) } )
    {
        canvas->installEventFilter( this );
    }

    m_overviewUniverse->SetIsOverview( true );
    m_overviewUniverse->SetShowAllLinks( true );
    m_universePlot->SetShowAllLinks( true );
    m_universePlot->SetOnFocus( [this]( const u64 address ) {
        if ( m_detailUniverseGraph.communityMode )
            OpenCommunity( address, false );

        else
            FocusUniverse( address );
    } );
    m_universePlot->SetOnToggleBranch(
        [this]( const u64 address ) {
            ToggleUniverseBranch( address );
        } );

    m_overviewUniverse->SetOnFocus( [this]( const u64 address ) {
        if ( m_overviewFilteredGraph.communityMode )
            OpenCommunity( address, true );

        else
            FocusUniverse( address );
    } );
    m_universePlot->SetOnHistoryBack( [this]() { UniverseBack(); } );
    m_universePlot->SetOnHistoryForward( [this]() { UniverseForward(); } );
    m_overviewUniverse->SetOnHistoryBack( [this]() { UniverseBack(); } );
    m_overviewUniverse->SetOnHistoryForward( [this]() { UniverseForward(); } );
    m_universePlot->SetOnResetFocus( [this]() {
        ClearUniverseFocus();
    } );
    m_overviewUniverse->SetOnResetFocus( [this]() {
        ClearUniverseFocus();
    } );
    m_universePlot->SetOnSelect( [this]( const u64 address ) {
        if ( m_detailUniverseGraph.communityMode &&
             m_detailCommunities.count( address ) &&
             m_detailCommunities.at( address ).size() > 1u )
        {
            OpenCommunity( address, false );
        }
        else
            SelectUniverseInspector( address );
    } );
    m_overviewUniverse->SetOnSelect( [this]( const u64 address ) {
        if ( m_overviewFilteredGraph.communityMode &&
             m_overviewCommunities.count( address ) &&
             m_overviewCommunities.at( address ).size() > 1u )
        {
            OpenCommunity( address, true );
        }
        else
            SelectUniverseInspector( address );
    } );
    m_universePlot->SetOnHover( [this]( const usize index ) {
        HoverUniverse( index, false );
    } );
    m_overviewUniverse->SetOnHover( [this]( const usize index ) {
        HoverUniverse( index, true );
    } );
    m_cubePlot->SetOnNavigate( [this]( const usize index ) {
        Navigate( index );
    } );
    m_cubePlot->SetOnHover( [this]( const usize index ) {
        Hover( index );
    } );
    m_plot->SetOnNavigate( [this]( const usize index ) {
        Navigate( index );
    });
    m_plot->SetOnHover( [this]( const usize index ) {
        Hover( index );
    });
    m_overviewCube->SetOnNavigate( [this]( const usize offset ) {
        NavigateOverview( offset );
    } );
    m_overviewCube->SetOnHover( [this]( const usize offset ) {
        HoverOverview( offset );
    } );
    m_overviewPlot->SetOnNavigate( [this]( const usize offset ) {
        NavigateOverview( offset );
    });
    m_overviewPlot->SetOnHover( [this]( const usize offset ) {
        HoverOverview( offset );
    });
    m_plot->SetOnWheel( [this]( const usize source,
        const usize ordinal, const usize pixels,
        const int steps ) {
        WheelDetail( source, ordinal, pixels, steps );
    } );
    m_overviewPlot->SetOnWheel( [this]( const usize source,
        const usize ordinal, const usize pixels,
        const int steps ) {
        WheelOverview( source, ordinal, pixels, steps );
    } );

    QObject::connect(
        m_segmentBox,
        QOverload< int >::of( &QComboBox::currentIndexChanged ),
        this, [this]( int ) {
            m_windowOffset = 0;
            RebuildRanges();
            Reload();
        } );

    QObject::connect(
        m_viewBox,
        QOverload< int >::of( &QComboBox::currentIndexChanged ),
        this, [this]( int ) {
            SyncRangeControl();

            const bool universe =
                m_viewBox->currentData().toInt() == CDV_UNIVERSE3D;

            m_universeExplorerPanel->setVisible( universe ||
                m_overviewViewBox->currentData().toInt() == CDV_UNIVERSE3D );

            m_universePathControls->setVisible( universe );

            if ( !universe &&
                 m_overviewViewBox->currentData().toInt() != CDV_UNIVERSE3D )
            {
                HideUniverseInspector();
                QToolTip::hideText();
            }

            m_universeFocusRangeToggle->setVisible(
                universe && m_universeFocusAddress.has_value() );

            m_universeRangeHeatToggle->setVisible(
                universe && !m_universeFocusAddress.has_value() );

            Reload();
        } );

    QObject::connect(
        m_overviewViewBox,
        QOverload< int >::of( &QComboBox::currentIndexChanged ),
        this, [this]( int ) {
            m_overviewImageDirty = true;
            m_universeExplorerPanel->setVisible(
                m_viewBox->currentData().toInt() == CDV_UNIVERSE3D ||
                m_overviewViewBox->currentData().toInt() == CDV_UNIVERSE3D );

            m_universeCategoryBox->setVisible(
                m_viewBox->currentData().toInt() == CDV_UNIVERSE3D ||
                m_overviewViewBox->currentData().toInt() == CDV_UNIVERSE3D );

            if ( m_viewBox->currentData().toInt() != CDV_UNIVERSE3D &&
                 m_overviewViewBox->currentData().toInt() != CDV_UNIVERSE3D )
            {
                HideUniverseInspector();
                QToolTip::hideText();
            }

            RenderOverview();
        } );

    QObject::connect( m_rangeEdit, &QLineEdit::editingFinished,
        this, [this]() { CommitRangeText(); } );

    QObject::connect( m_universeCategoryBox,
        QOverload< int >::of( &QComboBox::currentIndexChanged ),
        this, [this]( const int ) {
            if ( m_universeLoaded )
            {
                BuildUniverseCategory();
                // A new tier represents a new overview, not a different
                // slice of the currently focused caller sphere.
                if ( m_universeFocusAddress.has_value() )
                {
                    ClearUniverseFocus();
                }
                else
                {
                    m_callerClickOrdinals.clear();
                    m_universePlot->ClearSelection();
                    RenderCurrent();
                }

                RenderOverview();
                // Each tier has different hubs: start it with its own
                // prominent-node framing, not the previous tier's pivot.
                m_universePlot->ReframeToHubs();
                m_overviewUniverse->ReframeToHubs();
            }
        } );

    QObject::connect( m_showGlobalsToggle, &QCheckBox::toggled,
        this, [this]( bool ) {
            m_universeLoaded = false;
            ClearUniverseRoutes();
            m_universePathFrom.reset();
            HideUniverseInspector();
            m_inspectorIndex = {};
            m_universeData = {};
            m_universeGraph = {};
            m_overviewFilteredGraph = {};
            m_detailUniverseGraph = {};
            m_callerClickOrdinals.clear();
            RenderCurrent();
            RenderOverview();
            m_universePlot->ReframeToHubs();
            m_overviewUniverse->ReframeToHubs();
        } );

    QObject::connect( m_distinctCallersToggle, &QCheckBox::toggled,
        this, [this]( const bool distinct ) {
            if ( m_universeLoaded )
            {
                BuildUniverseCategory();
            }

            RenderCurrent();
            RenderOverview();
            m_universePlot->ReframeToHubs();
            m_overviewUniverse->ReframeToHubs();
        } );

    QObject::connect( m_globalFieldsToggle, &QCheckBox::toggled,
        this, [this]( bool ) {
            if ( m_universeLoaded )
                BuildUniverseCategory();

            RenderCurrent();
            RenderOverview();
            m_universePlot->ReframeToHubs();
            m_overviewUniverse->ReframeToHubs();
        } );

    QObject::connect( m_universeFocusRangeToggle, &QCheckBox::toggled,
        this, [this]( bool ) {
            if ( m_universeFocusAddress.has_value() )
            {
                m_callerClickOrdinals.clear();
                RenderCurrent();
                m_universePlot->ReframeToHubs();
            }
        } );

    QObject::connect( m_universeRangeHeatToggle, &QCheckBox::toggled,
        this, [this]( bool ) {
            if ( !m_universeFocusAddress.has_value() &&
                 m_viewBox->currentData().toInt() == CDV_UNIVERSE3D )
            {
                RenderCurrent();
                m_universePlot->ReframeToHubs();
            }
        } );

    const auto updateUniverseStyle = [this]( const bool ) {
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
        m_overviewUniverse->SetRenderStyle( style );
    };
    QObject::connect( m_relativeNodeHeatToggle, &QCheckBox::toggled,
        this, updateUniverseStyle );

    QObject::connect( m_relativeNodeSizeToggle, &QCheckBox::toggled,
        this, updateUniverseStyle );

    QObject::connect( m_antiAliasingToggle, &QCheckBox::toggled,
        this, [updateUniverseStyle]( const bool enabled ) {
            QSettings settings( "CantorDust", "IDA" );
            settings.setValue( "antiAliasing", enabled );
            updateUniverseStyle( enabled );
        } );

    QObject::connect( m_universeSpacingSlider, &QSlider::valueChanged,
        this, [this, updateUniverseStyle]( const int value ) {
            const QSignalBlocker blocker( m_universeSpacingEdit );
            m_universeSpacingEdit->setValue( value );
            QSettings settings( "CantorDust", "IDA" );
            settings.setValue( "universeSpacing", value );
            updateUniverseStyle( true );
            m_universePlot->ReframeToHubs();
            m_overviewUniverse->ReframeToHubs();
        } );

    QObject::connect( m_universeSpacingEdit,
        QOverload< int >::of( &QSpinBox::valueChanged ), this,
        [this]( const int value ) {
            m_universeSpacingSlider->setValue( value );
        } );

    QObject::connect( m_organicLayoutToggle, &QCheckBox::toggled,
        this, [this]( const bool organic ) {
            QSettings settings( "CantorDust", "IDA" );
            settings.setValue( "organicLayout", organic );

            if ( !m_universeLoaded )
                return;

            Universe_RelayoutGraph( m_universeGraph, organic );

            if ( !m_universeFocusAddress.has_value() )
            {
                Universe_RelayoutGraph( m_detailUniverseGraph, organic );
            }

            m_universePlot->SetGraph(
                &m_detailUniverseGraph, CurrentPalette() );

            m_overviewFilteredGraph = Universe_FilterGraphAdvanced(
                m_universeGraph, m_universeData, CurrentAdvancedFilters() );

            m_overviewUniverse->SetGraph(
                &m_overviewFilteredGraph, CurrentOverviewPalette() );

            m_universePlot->ReframeToHubs();
            m_overviewUniverse->ReframeToHubs();
        } );

    QObject::connect(
        m_paletteBox,
        QOverload< int >::of( &QComboBox::currentIndexChanged ),
        this, [this]( int ) {
            QSettings settings( "CantorDust", "IDA" );
            settings.setValue( "paletteDetail", m_paletteBox->currentIndex() );
            m_plot->SetPalette( CurrentPalette() );
            RenderCurrent();
        } );

    QObject::connect( m_overviewPaletteBox,
        QOverload< int >::of( &QComboBox::currentIndexChanged ),
        this, [this]( int ) {
            QSettings settings( "CantorDust", "IDA" );
            settings.setValue( "paletteOverview",
                m_overviewPaletteBox->currentIndex() );

            m_entropyStrip->SetPalette( CurrentOverviewPalette() );
            m_overviewPlot->SetPalette( CurrentOverviewPalette() );
            m_overviewImageDirty = true;
            RenderOverview();
        } );

    QObject::connect(
        m_resolutionBox,
        QOverload< int >::of( &QComboBox::currentIndexChanged ),
        this, [this]( int ) {
            SyncRangeControl();
            m_overviewCells.clear();
            m_overviewTriplets.clear();
            m_overviewCubeLoaded = false;
            m_overviewImageDirty = true;
            Reload();
        } );

    QObject::connect(
        m_overviewToggle, &QCheckBox::toggled,
        this, [this]( bool ) {
            UpdateOverviewVisibility();
            RenderOverview();
        } );

    QObject::connect(
        m_entropyToggle, &QCheckBox::toggled,
        this, [this]( bool ) {
            m_entropyStrip->setVisible(
                m_overviewToggle->isChecked() &&
                m_entropyToggle->isChecked() &&
                m_overviewViewBox->currentData().toInt() != CDV_UNIVERSE3D );
        } );

    QObject::connect(
        refreshButton, &QPushButton::clicked,
        this, [this] {
            m_universeLoaded = false;
            HideUniverseInspector();
            m_inspectorIndex = {};
            m_universeData = {};
            m_universeGraph = {};
            m_overviewFilteredGraph = {};
            m_detailUniverseGraph = {};
            m_universeFocusAddress.reset();
            m_universeHistory.Reset();
            m_lastCursorFunction.reset();
            m_universeFocusRangeToggle->setVisible( false );
            m_universeRangeHeatToggle->setVisible(
                m_viewBox->currentData().toInt() == CDV_UNIVERSE3D );

            m_callerClickOrdinals.clear();
            ListSegments( false );
            RebuildRanges();
            Reload();
        } );

    QObject::connect(
        cursorButton, &QPushButton::clicked,
        this, [this] {
            if ( m_viewBox->currentData().toInt() == CDV_UNIVERSE3D )
            {
                CursorToUniverseFunction();
                return;
            }

            ListSegments( true );
            RebuildRanges();
            PositionAtCursor();
            Reload();
        } );

    LoadInvestigationBookmarks();
    UpdateOverviewVisibility();
    ListSegments( true );
    RebuildRanges();
    PositionAtCursor();
    Reload();
    OnIdaCursorChanged( get_screen_ea() );
}

// Keep snapshots inside the IDB, not QSettings shared by every binary.
// A single versioned JSON blob permits names and arbitrary counts of snapshots
// without the 1 KiB supval record limitation.

} // namespace cantordust
