#include "pch/ida_pch.h"
#include "ida/cantor_panel.h"
#include "ida/cantor_panel_detail.h"
#include "ida/universe_search_layout.h"
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

#include <QMenuBar>
#include <QScrollArea>
#include <QEvent>
#include <QScreen>

#include <cstdlib>

namespace cantordust
{

namespace
{
// Preserve the full size hint for normal horizontal layout while allowing
// the parent dock to shrink far enough for responsive row wrapping to run.
class FlexibleWidthPanel final : public QWidget
{
public:
    using QWidget::QWidget;

    QSize minimumSizeHint() const override
    {
        QSize hint = QWidget::minimumSizeHint();
        hint.setWidth( 0 );
        return hint;
    }
};

constexpr int kUniverseSpacingMinPercent = 25;
constexpr int kUniverseSpacingMaxPercent = 10000;
constexpr int kUniverseSpacingDefaultPercent = 1000;

// Choose a themed hyperlink color with readable contrast against the
// panel background. Some IDA light themes use very pale link accents.
f64 ColorChannelLinear( const int channel )
{
    const f64 value = static_cast< f64 >( channel ) / 255.0;

    if ( value <= 0.04045 )
        return value / 12.92;

    return std::pow( ( value + 0.055 ) / 1.055, 2.4 );
}

f64 ColorLuminance( const QColor& color )
{
    return 0.2126 * ColorChannelLinear( color.red() ) +
        0.7152 * ColorChannelLinear( color.green() ) +
        0.0722 * ColorChannelLinear( color.blue() );
}

f64 ColorContrast( const QColor& first, const QColor& second )
{
    const f64 firstLuminance = ColorLuminance( first );
    const f64 secondLuminance = ColorLuminance( second );
    const f64 lighter = std::max( firstLuminance, secondLuminance );
    const f64 darker = std::min( firstLuminance, secondLuminance );
    return ( lighter + 0.05 ) / ( darker + 0.05 );
}

QColor ReadableLinkColor( const QPalette& palette )
{
    const QColor background = palette.color( QPalette::Window );
    const QColor foreground = palette.color( QPalette::WindowText );
    const QColor link = palette.color( QPalette::Link );
    constexpr f64 minimumContrast = 4.5;

    if ( ColorContrast( link, background ) >= minimumContrast )
        return link;

    // Preserve as much of the theme's link tint as possible; move toward
    // the native text color only when necessary for legibility.
    for ( int step = 1; step <= 16; ++step )
    {
        const QColor candidate(
            ( link.red() * ( 16 - step ) + foreground.red() * step ) / 16,
            ( link.green() * ( 16 - step ) + foreground.green() * step ) / 16,
            ( link.blue() * ( 16 - step ) + foreground.blue() * step ) / 16 );

        if ( ColorContrast( candidate, background ) >= minimumContrast )
            return candidate;
    }

    return foreground;
}
}

static void Ida_AddViewOptions( QComboBox* const box )
{
    box->addItem( "Linear Bitmap", QVariant::fromValue< int >( CDV_LINEAR ) );
    box->addItem( "Byte-Pair Heatmap", QVariant::fromValue< int >( CDV_DIGRAPH ) );
    box->addItem( "2D Histogram", QVariant::fromValue< int >( CDV_HISTOGRAM ) );
    box->addItem( "Hilbert Curve", QVariant::fromValue< int >( CDV_HILBERT ) );
    box->setCurrentIndex( CDV_HILBERT );
}

CantorPanel::CantorPanel( QWidget* const parent )
    : QWidget( parent )
    , m_aboutDialog( new QDialog( this ) )
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
    , m_universeFanoutEdit( new QSpinBox( m_settingsDialog ) )
    , m_supplementalFeatures( new QWidget( this ) )
    , m_cubeFeaturesPanel( new QWidget( m_supplementalFeatures ) )
    , m_bytePlotFeaturesPanel( new QWidget( m_supplementalFeatures ) )
    , m_histogramFeaturesRow( new QWidget( m_bytePlotFeaturesPanel ) )
    , m_digraphFeaturesRow( new QWidget( m_bytePlotFeaturesPanel ) )
    , m_histogramScaleBox( new QComboBox( m_histogramFeaturesRow ) )
    , m_digraphIntensityBox( new QComboBox( m_digraphFeaturesRow ) )
    , m_digraphDistanceEdit( new QSpinBox( m_digraphFeaturesRow ) )
    , m_universeExplorerPanel( new QWidget( m_supplementalFeatures ) )
    , m_universeSearchOptions( new FlexibleWidthPanel( m_universeExplorerPanel ) )
    , m_universeNeighborsControls( new FlexibleWidthPanel( m_universeSearchOptions ) )
    , m_universeTierControls( new FlexibleWidthPanel( m_universeSearchOptions ) )
    , m_universeWrapPanel( new FlexibleWidthPanel( m_universeExplorerPanel ) )
    , m_universePathControls( new QWidget( this ) )
    , m_universeSearchEdit( new QLineEdit( this ) )
    , m_universeMinRefsEdit( new QSpinBox( this ) )
    , m_universeFilterToggle( new QCheckBox( "Filter view", this ) )
    , m_universeClusterToggle( new QCheckBox( "Clusters", this ) )
    , m_universeSearchResults( new QComboBox( this ) )
    , m_universePathFromButton( new QPushButton( "Set Path Start", this ) )
    , m_universePathToButton( new QPushButton( "Set Path End", this ) )
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
    , m_cubeGeometryBox( new QComboBox( m_cubeFeaturesPanel ) )
    , m_cubeValuesBox( new QComboBox( m_cubeFeaturesPanel ) )
    , m_cubeStrideEdit( new QSpinBox( m_cubeFeaturesPanel ) )
    , m_cubeAlignmentEdit( new QSpinBox( m_cubeFeaturesPanel ) )
    , m_cubeBitPlaneEdit( new QSpinBox( m_cubeFeaturesPanel ) )
    , m_cubeSizeSlider( new QSlider( Qt::Horizontal, m_cubeFeaturesPanel ) )
    , m_cubeBrightnessSlider( new QSlider( Qt::Horizontal, m_cubeFeaturesPanel ) )
    , m_cubeAutoBrightnessToggle( new QCheckBox( "Auto", m_cubeFeaturesPanel ) )
    , m_universeSpacingSlider( new QSlider( Qt::Horizontal, this ) )
    , m_universeSpacingEdit( new QSpinBox( this ) )
    , m_infoLabel( new QLabel( this ) )
    , m_rangeAddressLabel( new QLabel( this ) )
    , m_supportLink( new QLabel( this ) )
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
    QMenuBar* const menuBar = new QMenuBar( this );
    root->setMenuBar( menuBar );

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

    QHBoxLayout* const modeRow = new QHBoxLayout();
    modeRow->setSpacing( 5 );
    root->addLayout( modeRow );
    modeRow->addWidget( new QLabel( "Overview:", this ) );
    Ida_AddViewOptions( m_overviewViewBox );
    m_overviewViewBox->addItem( "3D Byte Cloud",
        QVariant::fromValue< int >( CDV_CUBE3D ) );

    m_overviewViewBox->addItem( "3D Universe",
        QVariant::fromValue< int >( CDV_UNIVERSE3D ) );

    modeRow->addWidget( m_overviewViewBox, 1 );
    modeRow->addWidget( new QLabel( "Detail:", this ) );
    Ida_AddViewOptions( m_viewBox );
    m_viewBox->addItem( "3D Byte Cloud",
        QVariant::fromValue< int >( CDV_CUBE3D ) );

    m_viewBox->addItem( "3D Universe",
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
    // Only render supplemental controls for modes that have them. Both
    // groups may be shown if Overview and Detail use different 3D modes.
    QVBoxLayout* const supplementalLayout =
        new QVBoxLayout( m_supplementalFeatures );
    supplementalLayout->setContentsMargins( 0, 0, 0, 0 );
    supplementalLayout->setSpacing( 4 );
    supplementalLayout->addWidget( m_cubeFeaturesPanel );
    supplementalLayout->addWidget( m_bytePlotFeaturesPanel );
    supplementalLayout->addWidget( m_universeExplorerPanel );
    root->addWidget( m_supplementalFeatures );
    // Shared analysis controls apply to both Detail and Overview when either
    // canvas uses the associated 2D visualization.
    QVBoxLayout* const analysisLayout = new QVBoxLayout( m_bytePlotFeaturesPanel );
    analysisLayout->setContentsMargins( 0, 0, 0, 0 );
    analysisLayout->setSpacing( 3 );
    QHBoxLayout* const histogramRow = new QHBoxLayout( m_histogramFeaturesRow );
    histogramRow->setContentsMargins( 0, 0, 0, 0 );
    histogramRow->addWidget( new QLabel( "Histogram scale:", m_histogramFeaturesRow ) );
    m_histogramScaleBox->addItems( { "Logarithmic", "Linear", "Square Root" } );
    m_histogramScaleBox->setToolTip( "Scale histogram bar heights. "
        "Bars are colored using the selected byte palette." );
    histogramRow->addWidget( m_histogramScaleBox, 1 );
    analysisLayout->addWidget( m_histogramFeaturesRow );

    QHBoxLayout* const digraphRow = new QHBoxLayout( m_digraphFeaturesRow );
    digraphRow->setContentsMargins( 0, 0, 0, 0 );
    digraphRow->addWidget( new QLabel( "Byte pairs:", m_digraphFeaturesRow ) );
    m_digraphIntensityBox->addItems( { "Frequency (log)",
        "Conditional Probability" } );
    m_digraphIntensityBox->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Fixed );
    m_digraphIntensityBox->setMinimumWidth( 100 );
    m_digraphIntensityBox->setToolTip( "Frequency: raw byte-pair counts. "
        "Probability: P(second byte | first byte)." );
    digraphRow->addWidget( m_digraphIntensityBox, 1 );
    digraphRow->addWidget( new QLabel( "Distance:", m_digraphFeaturesRow ) );
    m_digraphDistanceEdit->setRange( 1, 256 );
    m_digraphDistanceEdit->setMinimumWidth( 64 );
    m_digraphDistanceEdit->setToolTip( "Distance in observed sample cells "
        "between the first and second byte. 1 means adjacent bytes. "
        "When Overview samples a large range, this distance counts samples." );
    digraphRow->addWidget( m_digraphDistanceEdit );
    analysisLayout->addWidget( m_digraphFeaturesRow );

    const QSettings bytePlotSettings( "CantorDust", "IDA" );
    m_histogramScaleBox->setCurrentIndex( std::clamp(
        bytePlotSettings.value( "histogramScale", 0 ).toInt(), 0, 2 ) );
    m_digraphIntensityBox->setCurrentIndex( std::clamp(
        bytePlotSettings.value( "digraphIntensity", 0 ).toInt(), 0, 1 ) );
    m_digraphDistanceEdit->setValue( std::clamp(
        bytePlotSettings.value( "digraphDistance", 1 ).toInt(), 1, 256 ) );
    const auto updateBytePlots = [this]() {
        QSettings settings( "CantorDust", "IDA" );
        settings.setValue( "histogramScale", m_histogramScaleBox->currentIndex() );
        settings.setValue( "digraphIntensity", m_digraphIntensityBox->currentIndex() );
        settings.setValue( "digraphDistance", m_digraphDistanceEdit->value() );
        m_overviewImageDirty = true;
        const View_e detail = static_cast< View_e >(
            m_viewBox->currentData().toInt() );
        if ( detail == CDV_HISTOGRAM || detail == CDV_DIGRAPH )
            RenderCurrent();
        else
            RenderOverview();
    };
    QObject::connect( m_histogramScaleBox,
        QOverload< int >::of( &QComboBox::currentIndexChanged ),
        this, [updateBytePlots]( int ) { updateBytePlots(); } );
    QObject::connect( m_digraphIntensityBox,
        QOverload< int >::of( &QComboBox::currentIndexChanged ),
        this, [updateBytePlots]( int ) { updateBytePlots(); } );
    QObject::connect( m_digraphDistanceEdit,
        QOverload< int >::of( &QSpinBox::valueChanged ),
        this, [updateBytePlots]( int ) { updateBytePlots(); } );

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
    // These are state holders for Edit > Color Palettes, not floating dock widgets.
    m_paletteBox->hide();
    m_overviewPaletteBox->hide();

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
    visualLayout->addWidget( m_overviewToggle );
    visualLayout->addWidget( m_entropyToggle );
    QHBoxLayout* const spacingRow = new QHBoxLayout();
    spacingRow->addWidget( m_universeSpacingSlider, 1 );
    spacingRow->addWidget( m_universeSpacingEdit );
    settingsForm->addRow( "Universe spacing", spacingRow );
    visualLayout->addStretch();
    settingsTabs->addTab( visualPage, "Display" );

    QWidget* const universeStylePage = new QWidget( settingsTabs );
    QVBoxLayout* const universeStyleLayout = new QVBoxLayout( universeStylePage );
    universeStyleLayout->addWidget( m_showGlobalsToggle );
    universeStyleLayout->addWidget( m_distinctCallersToggle );
    universeStyleLayout->addWidget( m_globalFieldsToggle );
    universeStyleLayout->addWidget( m_universeFocusRangeToggle );
    universeStyleLayout->addWidget( m_universeRangeHeatToggle );
    universeStyleLayout->addWidget( m_relativeNodeHeatToggle );
    universeStyleLayout->addWidget( m_relativeNodeSizeToggle );
    universeStyleLayout->addWidget( m_organicLayoutToggle );
    universeStyleLayout->addWidget( m_antiAliasingToggle );
    universeStyleLayout->addStretch();
    settingsTabs->addTab( universeStylePage, "Universe Style" );

    // The same Byte Cloud controls apply to Overview and Detail. Keep
    // them beside the active visualization, not buried in All Settings.
    QWidget* const cubePage = m_cubeFeaturesPanel;
    QVBoxLayout* const cubeLayout = new QVBoxLayout( cubePage );
    cubeLayout->setContentsMargins( 0, 0, 0, 0 );
    cubeLayout->setSpacing( 4 );

    QHBoxLayout* const cubeSelectionRow = new QHBoxLayout();
    cubeSelectionRow->setContentsMargins( 0, 0, 0, 0 );
    cubeSelectionRow->setSpacing( 5 );
    cubeSelectionRow->addWidget( new QLabel( "3D Byte:", cubePage ) );
    cubeSelectionRow->addWidget( new QLabel( "Geometry:", cubePage ) );
    m_cubeGeometryBox->addItems(
        { "Cube (XYZ)", "Sphere", "Cylinder", "Tetrahedron" } );
    m_cubeGeometryBox->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Fixed );
    m_cubeGeometryBox->setMinimumWidth( 90 );
    cubeSelectionRow->addWidget( m_cubeGeometryBox, 1 );
    cubeSelectionRow->addWidget( new QLabel( "Values:", cubePage ) );
    m_cubeValuesBox->addItems( { "u8 (trigraph)", "u16 little endian",
        "u16 big endian", "u32 little endian", "u32 big endian",
        "u8 difference", "u8 popcount", "u8 XOR", "u8 nibbles",
        "f32 little endian", "f32 big endian", "u8 bit-plane",
        "u16 little endian (low)", "u16 big endian (low)",
        "u32 little endian (low)", "u32 big endian (low)",
        "u8 signed difference", "u64 little endian",
        "u64 big endian", "u64 little endian (low)",
        "u64 big endian (low)" } );
    m_cubeValuesBox->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Fixed );
    m_cubeValuesBox->setMinimumWidth( 105 );
    m_cubeValuesBox->setToolTip(
        "Interpret consecutive binary values as XYZ coordinates. Integer "
        "modes use the high or low byte of each scalar. Float32 modes "
        "normalize finite XYZ triples to a shared robust scale. "
        "XOR, difference, nibbles and bit-plane reveal bit patterns." );
    cubeSelectionRow->addWidget( m_cubeValuesBox, 1 );
    cubeLayout->addLayout( cubeSelectionRow );

    // Keep sampling controls in a compact grid, independent of the value
    // selector's width. Zero stride preserves each mode's original default.
    QGridLayout* const cubeSamplingRow = new QGridLayout();
    cubeSamplingRow->setContentsMargins( 0, 0, 0, 0 );
    cubeSamplingRow->setHorizontalSpacing( 5 );
    cubeSamplingRow->setVerticalSpacing( 3 );

    m_cubeStrideEdit->setRange( 0, 4096 );
    m_cubeStrideEdit->setSpecialValueText( "Auto" );
    m_cubeStrideEdit->setToolTip(
        "Sampling step in bytes; Auto uses 1, 2, 4 or 8 according "
        "to the selected value mode." );

    m_cubeAlignmentEdit->setRange( 0, 4095 );
    m_cubeAlignmentEdit->setToolTip(
        "Logical byte-offset alignment, modulo the effective stride. "
        "The same alignment applies in Detail and Overview." );

    m_cubeBitPlaneEdit->setRange( 0, 7 );
    m_cubeBitPlaneEdit->setToolTip(
        "Bit index (0-7) for the u8 bit-plane interpretation." );

    cubeSamplingRow->addWidget( new QLabel( "Stride:", cubePage ), 0, 0 );
    cubeSamplingRow->addWidget( m_cubeStrideEdit, 0, 1 );
    cubeSamplingRow->addWidget( new QLabel( "Align:", cubePage ), 0, 2 );
    cubeSamplingRow->addWidget( m_cubeAlignmentEdit, 0, 3 );
    cubeSamplingRow->addWidget( new QLabel( "Bit:", cubePage ), 0, 4 );
    cubeSamplingRow->addWidget( m_cubeBitPlaneEdit, 0, 5 );

    cubeSamplingRow->setColumnStretch( 1, 1 );
    cubeSamplingRow->setColumnStretch( 3, 1 );
    cubeSamplingRow->setColumnStretch( 5, 1 );

    cubeLayout->addLayout( cubeSamplingRow );

    QHBoxLayout* const cubeAppearanceRow = new QHBoxLayout();
    cubeAppearanceRow->setContentsMargins( 0, 0, 0, 0 );
    cubeAppearanceRow->setSpacing( 5 );
    cubeAppearanceRow->addWidget( new QLabel( "Size:", cubePage ) );
    m_cubeSizeSlider->setRange( 50, 200 );
    m_cubeSizeSlider->setToolTip(
        "Point diameter, 50-200%; 100% is the original size." );
    cubeAppearanceRow->addWidget( m_cubeSizeSlider, 1 );

    QLabel* const cubeSizePercent = new QLabel( cubePage );
    cubeSizePercent->setMinimumWidth( 42 );
    cubeSizePercent->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
    cubeAppearanceRow->addWidget( cubeSizePercent );
    QObject::connect( m_cubeSizeSlider, &QSlider::valueChanged,
        cubeSizePercent, [cubeSizePercent]( const int value ) {
            cubeSizePercent->setText( QString::number( value ) + "%" );
        } );

    cubeAppearanceRow->addWidget( new QLabel( "Brightness:", cubePage ) );
    m_cubeBrightnessSlider->setRange( 25, 300 );
    m_cubeBrightnessSlider->setToolTip(
        "Manual point exposure, 25-300%; 100% is the original brightness. "
        "Ignored while Auto is enabled." );
    cubeAppearanceRow->addWidget( m_cubeBrightnessSlider, 1 );

    m_cubeAutoBrightnessToggle->setToolTip(
        "Automatically adapt 3D Byte Cloud exposure to the visible cloud. "
        "Disable this to use the manual brightness slider." );
    cubeAppearanceRow->addWidget( m_cubeAutoBrightnessToggle );

    QLabel* const cubeBrightnessPercent = new QLabel( cubePage );
    cubeBrightnessPercent->setMinimumWidth( 42 );
    cubeBrightnessPercent->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
    cubeAppearanceRow->addWidget( cubeBrightnessPercent );
    QObject::connect( m_cubeBrightnessSlider, &QSlider::valueChanged,
        cubeBrightnessPercent, [cubeBrightnessPercent]( const int value ) {
            cubeBrightnessPercent->setText( QString::number( value ) + "%" );
        } );

    QPushButton* const resetCube = new QPushButton( "Reset", cubePage );
    resetCube->setToolTip( "Reset 3D Byte Cloud geometry, values, "
        "point size, and brightness to their defaults." );
    cubeAppearanceRow->addWidget( resetCube );
    cubeLayout->addLayout( cubeAppearanceRow );
    QObject::connect( resetCube, &QPushButton::clicked, this, [this]() {
        m_cubeGeometryBox->setCurrentIndex( 0 );
        m_cubeValuesBox->setCurrentIndex( 0 );
        m_cubeStrideEdit->setValue( 0 );
        m_cubeAlignmentEdit->setValue( 0 );
        m_cubeBitPlaneEdit->setValue( 0 );
        m_cubeSizeSlider->setValue( 100 );
        m_cubeBrightnessSlider->setValue( 100 );
        m_cubeAutoBrightnessToggle->setChecked( true );
    } );

    const QSettings cubeSettings( "CantorDust", "IDA" );
    m_cubeGeometryBox->setCurrentIndex( std::clamp(
        cubeSettings.value( "cubeGeometry", 0 ).toInt(), 0, 3 ) );

    m_cubeValuesBox->setCurrentIndex( std::clamp(
        cubeSettings.value( "cubeValues", 0 ).toInt(), 0,
        static_cast< int >( CCV_COUNT ) - 1 ) );
    m_cubeStrideEdit->setValue( std::clamp(
        cubeSettings.value( "cubeStride", 0 ).toInt(), 0, 4096 ) );
    m_cubeAlignmentEdit->setValue( std::clamp(
        cubeSettings.value( "cubeAlignment", 0 ).toInt(), 0, 4095 ) );
    m_cubeBitPlaneEdit->setValue( std::clamp(
        cubeSettings.value( "cubeBitPlane", 0 ).toInt(), 0, 7 ) );
    m_cubeBitPlaneEdit->setEnabled( m_cubeValuesBox->currentIndex() == CCV_BIT_PLANE );

    m_cubeSizeSlider->setValue( std::clamp(
        cubeSettings.value( "cubePointSize", 100 ).toInt(), 50, 200 ) );

    m_cubeBrightnessSlider->setValue( std::clamp(
        cubeSettings.value( "cubeBrightness", 100 ).toInt(), 25, 300 ) );
    m_cubeAutoBrightnessToggle->setChecked( cubeSettings.contains( "cubeAutoBrightness" )
        ? cubeSettings.value( "cubeAutoBrightness" ).toBool() : true );
    cubeSizePercent->setText(
        QString::number( m_cubeSizeSlider->value() ) + "%" );
    cubeBrightnessPercent->setText(
        QString::number( m_cubeBrightnessSlider->value() ) + "%" );
    m_cubeBrightnessSlider->setEnabled( !m_cubeAutoBrightnessToggle->isChecked() );

    const auto updateCubeRender = [this]() {
        const CubeRenderOptions_s opts{
            static_cast< CubeGeometry_e >( m_cubeGeometryBox->currentIndex() ),
            m_cubeSizeSlider->value() / 100.0f,
            m_cubeBrightnessSlider->value() / 100.0f,
            m_cubeAutoBrightnessToggle->isChecked() };

        m_cubePlot->SetRenderOptions( opts );
        m_overviewCube->SetRenderOptions( opts );
        m_cubeBrightnessSlider->setEnabled( !m_cubeAutoBrightnessToggle->isChecked() );
        QSettings preferences( "CantorDust", "IDA" );
        preferences.setValue( "cubeGeometry", m_cubeGeometryBox->currentIndex() );
        preferences.setValue( "cubePointSize", m_cubeSizeSlider->value() );
        preferences.setValue( "cubeBrightness", m_cubeBrightnessSlider->value() );
        preferences.setValue( "cubeAutoBrightness", m_cubeAutoBrightnessToggle->isChecked() );
    };
    QObject::connect( m_cubeGeometryBox, QOverload<int>::of( &QComboBox::currentIndexChanged ),
        this, [updateCubeRender]( int ) { updateCubeRender(); } );

    QObject::connect( m_cubeSizeSlider, &QSlider::valueChanged,
        this, [updateCubeRender]( int ) { updateCubeRender(); } );

    QObject::connect( m_cubeBrightnessSlider, &QSlider::valueChanged,
        this, [updateCubeRender]( int ) { updateCubeRender(); } );

    QObject::connect( m_cubeAutoBrightnessToggle, &QCheckBox::toggled,
        this, [updateCubeRender]( bool ) { updateCubeRender(); } );

    const auto updateCubeSampling = [this]() {
        QSettings preferences( "CantorDust", "IDA" );
        preferences.setValue( "cubeValues", m_cubeValuesBox->currentIndex() );
        preferences.setValue( "cubeStride", m_cubeStrideEdit->value() );
        preferences.setValue( "cubeAlignment", m_cubeAlignmentEdit->value() );
        preferences.setValue( "cubeBitPlane", m_cubeBitPlaneEdit->value() );
        m_cubeBitPlaneEdit->setEnabled(
            m_cubeValuesBox->currentIndex() == CCV_BIT_PLANE );
        m_overviewCubeLoaded = false;
        m_overviewImageDirty = true;
        if ( m_totalLength != 0 )
            Reload();
    };
    QObject::connect( m_cubeValuesBox,
        QOverload<int>::of( &QComboBox::currentIndexChanged ),
        this, [updateCubeSampling]( int ) { updateCubeSampling(); } );
    for ( QSpinBox* const control : { m_cubeStrideEdit,
            m_cubeAlignmentEdit, m_cubeBitPlaneEdit } )
        QObject::connect( control, QOverload<int>::of( &QSpinBox::valueChanged ),
            this, [updateCubeSampling]( int ) { updateCubeSampling(); } );

    updateCubeRender();

    // Keep exploration budgets out of the compact main toolbar. Changes
    // apply immediately to an existing focus and persist between sessions.
    QWidget* const explorerPage = new QWidget( settingsTabs );
    QVBoxLayout* const explorerLayout = new QVBoxLayout( explorerPage );
    QFormLayout* const explorerForm = new QFormLayout();
    explorerLayout->addLayout( explorerForm );
    m_universeNodeBudgetEdit->setRange( 64,
        CANTORDUST_UNIVERSE_HOP_MAX_FUNCTIONS );

    m_universeNodeBudgetEdit->setSingleStep( 128 );
    // Upgrade the old 768/3,500 and 7,680/35,000 defaults once while
    // preserving custom budgets. The v2 marker distinguishes an old default
    // from an intentional setting of 768 or 3,500 in newer installations.
    const bool migratedBudgetsV2 = settings.value(
        "explorerBudgetsV2", false ).toBool();
    const bool migratedBudgetsV3 = settings.value(
        "explorerBudgetsV3", false ).toBool();

    const int storedNodeBudget = settings.value(
        "explorerNodeBudget", static_cast< int >(
            CANTORDUST_UNIVERSE_HOP_DEFAULT_FUNCTIONS ) ).toInt();

    const bool previousNodeDefault = storedNodeBudget == 7680 ||
        ( !migratedBudgetsV2 && storedNodeBudget == 768 );
    const int nodeBudget = !migratedBudgetsV3 && previousNodeDefault
        ? static_cast< int >( CANTORDUST_UNIVERSE_HOP_DEFAULT_FUNCTIONS )
        : storedNodeBudget;

    m_universeNodeBudgetEdit->setValue( std::clamp( nodeBudget, 64,
        static_cast< int >( CANTORDUST_UNIVERSE_HOP_MAX_FUNCTIONS ) ) );

    m_universeNodeBudgetEdit->setToolTip(
        "Maximum visible nodes when adding hops. Existing direct neighbors "
        "are always retained, even if they exceed this budget." );

    explorerForm->addRow( "Node budget", m_universeNodeBudgetEdit );
    m_universeLinkBudgetEdit->setRange( 128,
        CANTORDUST_UNIVERSE_HOP_MAX_LINKS );

    m_universeLinkBudgetEdit->setSingleStep( 512 );
    const int storedLinkBudget = settings.value(
        "explorerLinkBudget", static_cast< int >(
            CANTORDUST_UNIVERSE_HOP_DEFAULT_LINKS ) ).toInt();

    const bool previousLinkDefault = storedLinkBudget == 35000 ||
        ( !migratedBudgetsV2 && storedLinkBudget == 3500 );
    const int linkBudget = !migratedBudgetsV3 && previousLinkDefault
        ? static_cast< int >( CANTORDUST_UNIVERSE_HOP_DEFAULT_LINKS )
        : storedLinkBudget;

    m_universeLinkBudgetEdit->setValue( std::clamp( linkBudget, 128,
        static_cast< int >( CANTORDUST_UNIVERSE_HOP_MAX_LINKS ) ) );

    if ( !migratedBudgetsV3 )
    {
        QSettings updated( "CantorDust", "IDA" );
        updated.setValue( "explorerBudgetsV2", true );
        updated.setValue( "explorerBudgetsV3", true );
        updated.setValue( "explorerNodeBudget", m_universeNodeBudgetEdit->value() );
        updated.setValue( "explorerLinkBudget", m_universeLinkBudgetEdit->value() );
    }

    m_universeLinkBudgetEdit->setToolTip(
        "Maximum visible directed links when adding hops. Existing "
        "direct connections are always retained." );

    explorerForm->addRow( "Link budget", m_universeLinkBudgetEdit );
    m_universeFanoutEdit->setRange( 1,
        CANTORDUST_UNIVERSE_HOP_MAX_FANOUT );

    m_universeFanoutEdit->setSingleStep( 128 );
    m_universeFanoutEdit->setValue( std::clamp( settings.value(
        "explorerFanout", static_cast< int >(
            CANTORDUST_UNIVERSE_HOP_DEFAULT_FANOUT ) ).toInt(), 1,
        static_cast< int >( CANTORDUST_UNIVERSE_HOP_MAX_FANOUT ) ) );

    m_universeFanoutEdit->setToolTip(
        "Maximum newly discovered functions added per expanded node at "
        "hops 2-10. The global node and link budgets still apply." );

    explorerForm->addRow( "Neighbors per function", m_universeFanoutEdit );

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
        "Follow IDA is in Navigate and the quick controls. When off, IDA navigation "
        "highlights visible function nodes without moving the graph. "
        "Hops 1-10 are selected in the main explorer. These budgets limit "
        "graph growth at hops 2-10. Neighbors per function limits new "
        "nodes on each expanded branch. A limit warning appears in the "
        "detail status when expansion is cut short. Increasing budgets may slow "
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

    QObject::connect( m_universeFanoutEdit,
        QOverload< int >::of( &QSpinBox::valueChanged ), this,
        [this]( const int value ) {
            QSettings( "CantorDust", "IDA" ).setValue(
                "explorerFanout", value );

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

    QWidget* const searchPage = new QWidget( settingsTabs );
    QVBoxLayout* const searchLayout = new QVBoxLayout( searchPage );
    QFormLayout* const searchForm = new QFormLayout();
    searchForm->addRow( "Minimum references", m_universeMinRefsEdit );
    searchLayout->addLayout( searchForm );
    searchLayout->addWidget( m_universeFilterToggle );
    searchLayout->addWidget( m_universeClusterToggle );
    QLabel* const searchHelp = new QLabel(
        "Filter view limits the displayed universe to the search "
        "results. Clusters groups related functions into communities. "
        "Both can also be toggled from Tools > Search & Graph.", searchPage );
    searchHelp->setWordWrap( true );
    searchLayout->addWidget( searchHelp );
    searchLayout->addStretch();
    settingsTabs->addTab( searchPage, "Search" );

    m_aboutDialog->setWindowTitle( "About Cantor Dust" );
    QVBoxLayout* const aboutDialogLayout = new QVBoxLayout( m_aboutDialog );
    QScrollArea* const aboutScroller = new QScrollArea( m_aboutDialog );
    aboutScroller->setFrameShape( QFrame::NoFrame );
    aboutScroller->setWidgetResizable( true );
    QWidget* const aboutPage = new QWidget( aboutScroller );
    QVBoxLayout* const aboutLayout = new QVBoxLayout( aboutPage );
    QLabel* const aboutLabel = new QLabel( aboutPage );
    aboutLabel->setAlignment( Qt::AlignCenter );
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
    aboutScroller->setWidget( aboutPage );
    aboutDialogLayout->addWidget( aboutScroller );
    QPushButton* const closeAbout = new QPushButton( "Close", m_aboutDialog );
    aboutDialogLayout->addWidget( closeAbout, 0, Qt::AlignRight );
    QObject::connect( closeAbout, &QPushButton::clicked,
        m_aboutDialog, &QDialog::hide );

    // Fit the About dialog to its wrapped text, rather than leaving a large
    // blank area below the content. Keep scrolling available when the IDA
    // font is enlarged or the screen is too short to show everything.
    constexpr int aboutWidth = 510;
    const QMargins dialogMargins = aboutDialogLayout->contentsMargins();
    const QMargins pageMargins = aboutLayout->contentsMargins();
    const int labelWidth = aboutWidth - dialogMargins.left() - dialogMargins.right()
        - pageMargins.left() - pageMargins.right();
    const int contentHeight = aboutLabel->heightForWidth( labelWidth )
        + pageMargins.top() + pageMargins.bottom();
    const int idealHeight = contentHeight + dialogMargins.top()
        + dialogMargins.bottom() + aboutDialogLayout->spacing()
        + closeAbout->sizeHint().height() + 4;
    const QScreen* const aboutScreen = m_aboutDialog->screen();
    const int maxHeight = aboutScreen != nullptr
        ? std::max( 1, aboutScreen->availableGeometry().height() - 64 )
        : idealHeight;
    m_aboutDialog->resize( aboutWidth, std::min( idealHeight, maxHeight ) );

    QPushButton* const closeSettings = new QPushButton( "Close", m_settingsDialog );
    settingsLayout->addWidget( closeSettings );
    QObject::connect( closeSettings, &QPushButton::clicked,
        m_settingsDialog, &QDialog::hide );

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
    m_universeCategoryBox->setMinimumWidth( 80 );
    // The Tier selector must fill the remaining width, just like Neighbors.
    // The previous cap left a wide gap and could clip the arrow in IDA docks.
    m_universeCategoryBox->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed );
    m_universeCategoryBox->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon );

    m_universeCategoryBox->setVisible( false );
    QVBoxLayout* const exploreLayout = new QVBoxLayout(
        m_universeExplorerPanel );

    exploreLayout->setContentsMargins( 0, 0, 0, 0 );
    exploreLayout->setSpacing( 4 );

    // Keep Find, Neighbors and Tier on one line when the dock is wide.
    // At narrower widths move the latter two as a group to a second row:
    // reducing a combo's size hint alone still left Tier outside the dock.
    m_universeSearchRow = new QHBoxLayout();
    m_universeSearchRow->setSpacing( 5 );
    m_universeSearchRow->addWidget( new QLabel( "Find:", this ) );
    m_universeSearchEdit->setPlaceholderText( "Function name / hex address" );
    m_universeSearchEdit->setClearButtonEnabled( true );
    m_universeSearchEdit->setMinimumWidth( 120 );
    m_universeSearchRow->addWidget( m_universeSearchEdit, 1 );

    // Keep the labels with their respective selectors. The pair groups can
    // move from one horizontal row to two rows independently of Find; even
    // narrower docks can stack each label above its selector.
    m_universeOptionsLayout = new QBoxLayout(
        QBoxLayout::LeftToRight, m_universeSearchOptions );
    m_universeOptionsLayout->setContentsMargins( 0, 0, 0, 0 );
    m_universeOptionsLayout->setSpacing( 5 );

    m_universeNeighborsLayout = new QBoxLayout(
        QBoxLayout::LeftToRight, m_universeNeighborsControls );
    m_universeNeighborsLayout->setContentsMargins( 0, 0, 0, 0 );
    m_universeNeighborsLayout->setSpacing( 5 );
    QLabel* const neighborsLabel = new QLabel(
        "Neighbors:", m_universeNeighborsControls );
    neighborsLabel->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
    m_universeNeighborsLayout->addWidget( neighborsLabel );
    m_universeFocusModeBox->setMinimumWidth( 70 );
    m_universeFocusModeBox->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed );
    m_universeNeighborsLayout->addWidget( m_universeFocusModeBox, 1 );

    m_universeTierLayout = new QBoxLayout(
        QBoxLayout::LeftToRight, m_universeTierControls );
    m_universeTierLayout->setContentsMargins( 0, 0, 0, 0 );
    m_universeTierLayout->setSpacing( 5 );
    QLabel* const tierLabel = new QLabel( "Tier:", m_universeTierControls );
    tierLabel->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
    m_universeTierLayout->addWidget( tierLabel );
    m_universeTierLayout->addWidget( m_universeCategoryBox, 1 );

    // Both pairs use the full row when stacked, with the dropdown flush to
    // the right edge. When side by side, each dropdown grows within its pair.
    m_universeNeighborsControls->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed );
    m_universeTierControls->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed );
    m_universeOptionsLayout->addWidget( m_universeNeighborsControls, 1 );
    m_universeOptionsLayout->addWidget( m_universeTierControls, 1 );
    m_universeSearchOptions->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed );
    m_universeSearchRow->addWidget( m_universeSearchOptions );
    exploreLayout->addLayout( m_universeSearchRow );

    m_universeWrapRow = new QHBoxLayout( m_universeWrapPanel );
    m_universeWrapRow->setContentsMargins( 0, 0, 0, 0 );
    m_universeWrapRow->setSpacing( 5 );
    // Align wrapped controls from the left. IDA can clip a dock below its
    // contents' minimum width; right alignment would hide Tier again.
    m_universeWrapPanel->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed );
    exploreLayout->addWidget( m_universeWrapPanel );
    m_universeWrapPanel->hide();
    m_universeExplorerPanel->installEventFilter( this );
    // IDA's TWidget is the actual visible dock. Other panel rows can have a
    // larger minimum width, so watch dock resizes as well as our own.
    if ( parentWidget() != nullptr )
        parentWidget()->installEventFilter( this );

    m_universeMinRefsEdit->setRange( 0, 250000 );
    m_universeMinRefsEdit->setMaximumWidth( 100 );
    m_universeClusterToggle->setToolTip(
        "Group strongly interconnected functions in the Overview and "
        "unfocused Detail. Right-click a group to choose a member function." );

    m_universeClusterToggle->setChecked(
        settings.value( "communityMode", false ).toBool() );

    QObject::connect( m_universeClusterToggle, &QCheckBox::toggled,
        this, [this]( bool checked ) {
            QSettings preferences( "CantorDust", "IDA" );
            preferences.setValue( "communityMode", checked );
            RenderCurrent();
            RenderOverview();
            m_universePlot->ReframeToHubs();
            m_overviewUniverse->ReframeToHubs();
        } );

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
    resultRow->addWidget( new QLabel( "Hops:", this ) );
    m_universeHopsEdit->setRange( 1, 10 );
    m_universeHopsEdit->setValue( 1 );
    m_universeHopsEdit->setFixedWidth( 52 );
    m_universeHopsEdit->setToolTip(
        "1: original focused neighbors. 2-10: expand in the selected "
        "direction. Node/link budgets are configurable under Tools > "
        "Explorer Budgets. Shift + right-click a node to toggle its branch." );

    resultRow->addWidget( m_universeHopsEdit );
    exploreLayout->addLayout( resultRow );
    jumpResult->setToolTip( "Jump to the selected function in IDA." );
    focusResult->setToolTip( "Focus the selected function in the 3D universe." );
    UpdateUniversePathToolTips();

    m_universeClearPathButton->setToolTip(
        "Exit the call path view and restore the universe graph." );


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
        this, [this]() { SetUniversePathEndpoint( true ); } );

    QObject::connect( m_universePathToButton, &QPushButton::clicked,
        this, [this]() { SetUniversePathEndpoint( false ); } );

    QObject::connect( m_universeClearPathButton, &QPushButton::clicked,
        this, [this]() {
            ClearUniverseRoutes();
            m_universePathEndpoints.Clear();
            UpdateUniversePathToolTips();
            RenderCurrent();
            m_universePlot->ReframeToHubs();
            m_hintLabel->setText( "Call path cleared. Set Path Start and "
                "Set Path End in either order." );
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
    // Path actions are available in Tools > Call Paths. Keep only the
    // alternate-route selector in the dock when multiple routes exist.
    m_universePathFromButton->hide();
    m_universePathToButton->hide();
    m_universeClearPathButton->hide();
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
    m_supportLink->setTextFormat( Qt::RichText );
    m_supportLink->setTextInteractionFlags( Qt::TextBrowserInteraction );
    m_supportLink->setOpenExternalLinks( true );
    m_supportLink->setToolTip(
        "Support Kawe Mazidjatari on Ko-fi (opens browser)" );
    m_supportLink->setSizePolicy(
        QSizePolicy::Fixed, QSizePolicy::Preferred );
    UpdateSupportLink();
    rangeAddressRow->addWidget( m_supportLink, 0, Qt::AlignRight );
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
    m_plot->SetAddressFormatter( [this]( const usize index ) {
        const u64 logical = m_detailSampled ? index : m_windowOffset + index;
        u64 address = 0;
        return Math_OffsetToAddress( m_ranges, logical, address )
            ? Ida_FormatAddress( static_cast< ea_t >( address ) )
            : QStringLiteral( "Unknown" );
    } );
    m_overviewPlot->SetAddressFormatter( [this]( const usize offset ) {
        u64 address = 0;
        return Math_OffsetToAddress( m_ranges, offset, address )
            ? Ida_FormatAddress( static_cast< ea_t >( address ) )
            : QStringLiteral( "Unknown" );
    } );
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
            // A segment change is a new view: start at the full content
            // extent instead of inheriting the previous segment's zoom.
            m_windowOffset = 0;
            m_detailSpan = 0;
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

            UpdateSupplementalFeatures();

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
            UpdateSupplementalFeatures();

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
            m_universePathEndpoints.Clear();
            UpdateUniversePathToolTips();
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

    QObject::connect( refreshButton, &QPushButton::clicked,
        this, [this]() { RefreshData(); } );
    QObject::connect( cursorButton, &QPushButton::clicked,
        this, [this]() { GoToIdaCursor(); } );

    BuildMenus( menuBar, visualPage, explorerPage,
        filterPage, searchPage );
    UpdateSupplementalFeatures();

    LoadInvestigationBookmarks();
    UpdateOverviewVisibility();
    ListSegments( true );
    RebuildRanges();
    PositionAtCursor();
    Reload();
    OnIdaCursorChanged( get_screen_ea() );
}

BytePlotOptions_s CantorPanel::CurrentBytePlotOptions() const
{
    BytePlotOptions_s options;
    options.histogramScale = static_cast< HistogramScale_e >(
        m_histogramScaleBox->currentIndex() );
    options.digraphIntensity = static_cast< DigraphIntensity_e >(
        m_digraphIntensityBox->currentIndex() );
    options.digraphDistance = static_cast< usize >(
        m_digraphDistanceEdit->value() );
    return options;
}

QWidget* CantorPanel::SupplementalFeaturesForMode( const View_e mode ) const
{
    switch ( mode )
    {
        case CDV_HISTOGRAM:
        case CDV_DIGRAPH:
            return m_bytePlotFeaturesPanel;

        case CDV_CUBE3D:
            return m_cubeFeaturesPanel;

        case CDV_UNIVERSE3D:
            return m_universeExplorerPanel;

        default:
            return nullptr;
    }
}

void CantorPanel::UpdateSupplementalFeatures()
{
    const View_e detailMode = static_cast< View_e >(
        m_viewBox->currentData().toInt() );
    const View_e overviewMode = static_cast< View_e >(
        m_overviewViewBox->currentData().toInt() );

    QWidget* const detailFeatures = SupplementalFeaturesForMode( detailMode );
    QWidget* const overviewFeatures = SupplementalFeaturesForMode( overviewMode );

    const bool showCube = detailFeatures == m_cubeFeaturesPanel ||
        overviewFeatures == m_cubeFeaturesPanel;
    const bool showHistogram = detailMode == CDV_HISTOGRAM ||
        overviewMode == CDV_HISTOGRAM;
    const bool showDigraph = detailMode == CDV_DIGRAPH ||
        overviewMode == CDV_DIGRAPH;
    const bool showBytePlot = showHistogram || showDigraph;
    const bool showUniverse = detailFeatures == m_universeExplorerPanel ||
        overviewFeatures == m_universeExplorerPanel;

    m_cubeFeaturesPanel->setVisible( showCube );
    m_histogramFeaturesRow->setVisible( showHistogram );
    m_digraphFeaturesRow->setVisible( showDigraph );
    m_bytePlotFeaturesPanel->setVisible( showBytePlot );
    m_universeExplorerPanel->setVisible( showUniverse );
    m_supplementalFeatures->setVisible(
        showCube || showUniverse || showBytePlot );

    if ( showUniverse )
        UpdateUniverseSearchLayout( m_universeExplorerPanel->width() );
}

void CantorPanel::UpdateUniverseSearchLayout( const int availableWidth )
{
    if ( m_universeSearchRow == nullptr || m_universeWrapRow == nullptr ||
         m_universeOptionsLayout == nullptr )
    {
        return;
    }

    const QWidget* const findLabel = m_universeSearchRow->itemAt( 0 )->widget();
    const QWidget* const neighborsLabel =
        m_universeNeighborsLayout->itemAt( 0 )->widget();
    const QWidget* const tierLabel = m_universeTierLayout->itemAt( 0 )->widget();

    // Use the widths of both selectors even while Tier is initially hidden.
    // Never impose the combined width as a minimum on a narrow dock: that
    // was the remaining cause of Tier being painted outside the panel.
    const int neighborsWidth = neighborsLabel->sizeHint().width()
        + m_universeFocusModeBox->minimumWidth()
        + m_universeNeighborsLayout->spacing();
    const int tierWidth = tierLabel->sizeHint().width()
        + m_universeCategoryBox->minimumWidth()
        + m_universeTierLayout->spacing();
    const int findWidth = findLabel->sizeHint().width()
        + m_universeSearchEdit->minimumWidth();
    // The panel's Qt minimum size can exceed the *visible* IDA dock width
    // because of unrelated fixed-width toolbar controls. Size the options
    // against the dock's viewport, not merely this child widget's geometry.
    int visibleWidth = availableWidth;
    if ( parentWidget() != nullptr )
    {
        const QMargins margins = this->layout()->contentsMargins();
        const int dockWidth = parentWidget()->contentsRect().width()
            - margins.left() - margins.right();
        visibleWidth = std::min( visibleWidth, std::max( 1, dockWidth ) );
    }

    const UniverseSearchLayout_s layout = Universe_SearchLayoutForWidth(
        visibleWidth, findWidth, neighborsWidth, tierWidth,
        2 * m_universeSearchRow->spacing() + 8,
        m_universeOptionsLayout->spacing() );
    // When the panel is larger than the clipped dock, a stretched Find box
    // can otherwise push inline Tier outside the visible area.
    const bool wrap = layout.wrap || availableWidth > visibleWidth + 2;
    const bool stackOptions = layout.stackOptions;
    const bool stackLabels = layout.stackLabels;

    const bool unchanged = wrap == m_universeOptionsWrapped &&
        stackOptions == m_universeOptionsStacked &&
        stackLabels == m_universeLabelsStacked;
    m_universeOptionsWrapped = wrap;
    m_universeOptionsStacked = stackOptions;
    m_universeLabelsStacked = stackLabels;

    // Even if another toolbar row makes the parent panel too wide, Qt must
    // never allocate the wrapped dropdowns beyond the dock's visible edge.
    m_universeSearchOptions->setMaximumWidth( wrap
        ? std::max( 1, visibleWidth ) : QWIDGETSIZE_MAX );

    if ( unchanged )
        return;

    // On very narrow docks each pair also stacks internally, keeping the
    // selectors clickable instead of allowing either group to overflow.
    m_universeNeighborsLayout->setDirection( stackLabels
        ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight );
    m_universeTierLayout->setDirection( stackLabels
        ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight );
    m_universeOptionsLayout->setDirection( stackOptions
        ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight );

    if ( wrap )
    {
        m_universeSearchRow->removeWidget( m_universeSearchOptions );
        m_universeWrapRow->addWidget( m_universeSearchOptions, 1 );
        m_universeWrapPanel->show();
    }
    else
    {
        m_universeWrapRow->removeWidget( m_universeSearchOptions );
        m_universeSearchRow->addWidget( m_universeSearchOptions );
        m_universeWrapPanel->hide();
    }

    // Layout moves may reparent the widget. Its selectors and their signal
    // connections are retained, and no artificial minimum width is applied.
    m_universeSearchOptions->show();
}

void CantorPanel::UpdateSupportLink()
{
    const QColor color = ReadableLinkColor( m_supportLink->palette() );
    m_supportLink->setText( QStringLiteral(
        "<a href='https://ko-fi.com/amos0' "
        "style='color:%1; text-decoration:underline; "
        "font-weight:600;'>Support developer &#9825;</a>" )
        .arg( color.name( QColor::HexRgb ) ) );
}

void CantorPanel::changeEvent( QEvent* const event )
{
    QWidget::changeEvent( event );

    if ( event->type() == QEvent::PaletteChange ||
         event->type() == QEvent::ApplicationPaletteChange ||
         event->type() == QEvent::StyleChange )
    {
        UpdateSupportLink();
    }
}

// Keep snapshots inside the IDB, not QSettings shared by every binary.
// A single versioned JSON blob permits names and arbitrary counts of snapshots
// without the 1 KiB supval record limitation.

} // namespace cantordust
