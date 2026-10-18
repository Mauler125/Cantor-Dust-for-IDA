#include "pch/ida_pch.h"
#include "ida/cantor_panel.h"
#include "ida/cantor_panel_detail.h"
#include "ida/ida_memory.h"
#include "math/window.h"
#include "math/anomaly_palette.h"

#include <kernwin.hpp>
#include <QCoreApplication>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QProgressDialog>
#include <QTableWidget>

namespace cantordust
{

namespace
{
// Keep the UI's scan sizes bounded even if preferences were hand-edited.
constexpr usize kMiB = 1024u * 1024u;

void LoadAnomalyOptions( AnomalyOptions_s& options )
{
    QSettings settings( "CantorDust", "IDA" );
    settings.beginGroup( "anomaly" );
    const auto flag = [&]( const char* name, bool fallback ) {
        return settings.value( name, fallback ).toBool();
    };
    const auto threshold = [&]( const char* name, f64 fallback,
        f64 minimum, f64 maximum ) {
        return std::clamp( settings.value( name, fallback ).toDouble(),
            minimum, maximum );
    };
    options.highEntropy = flag( "highEntropy", options.highEntropy );
    options.entropyChange = flag( "entropyChange", options.entropyChange );
    options.padding = flag( "padding", options.padding );
    options.dominantByte = flag( "dominantByte", options.dominantByte );
    options.repeatedPattern = flag( "repeatedPattern", options.repeatedPattern );
    options.highEntropyBits = threshold( "highEntropyBits",
        options.highEntropyBits, 0.0, 8.0 );
    options.entropyChangeBits = threshold( "entropyChangeBits",
        options.entropyChangeBits, 0.0, 8.0 );
    options.paddingFraction = threshold( "paddingFraction",
        options.paddingFraction, 0.5, 1.0 );
    options.dominantFraction = threshold( "dominantFraction",
        options.dominantFraction, 0.01, 1.0 );
    options.patternFraction = threshold( "patternFraction",
        options.patternFraction, 0.5, 1.0 );
    const int window = settings.value( "windowBytes",
        static_cast< int >( options.windowBytes ) ).toInt();
    if ( window == 1024 || window == 4096 || window == 16384 )
        options.windowBytes = static_cast< usize >( window );
    const int budget = settings.value( "scanBudgetMiB",
        static_cast< int >( options.scanBudgetMiB ) ).toInt();
    if ( budget == 4 || budget == 16 || budget == 32 || budget == 64 )
        options.scanBudgetMiB = static_cast< usize >( budget );
    const int limit = settings.value( "maxFindingsPerKind",
        static_cast< int >( options.maxFindingsPerKind ) ).toInt();
    if ( limit == 25 || limit == 100 || limit == 250 || limit == 500 )
        options.maxFindingsPerKind = static_cast< usize >( limit );
}

void SaveAnomalyOptions( const AnomalyOptions_s& options )
{
    QSettings settings( "CantorDust", "IDA" );
    settings.beginGroup( "anomaly" );
    settings.setValue( "highEntropy", options.highEntropy );
    settings.setValue( "entropyChange", options.entropyChange );
    settings.setValue( "padding", options.padding );
    settings.setValue( "dominantByte", options.dominantByte );
    settings.setValue( "repeatedPattern", options.repeatedPattern );
    settings.setValue( "highEntropyBits", options.highEntropyBits );
    settings.setValue( "entropyChangeBits", options.entropyChangeBits );
    settings.setValue( "paddingFraction", options.paddingFraction );
    settings.setValue( "dominantFraction", options.dominantFraction );
    settings.setValue( "patternFraction", options.patternFraction );
    settings.setValue( "windowBytes", static_cast< int >( options.windowBytes ) );
    settings.setValue( "scanBudgetMiB", static_cast< int >( options.scanBudgetMiB ) );
    settings.setValue( "maxFindingsPerKind",
        static_cast< int >( options.maxFindingsPerKind ) );
}
}

void CantorPanel::ClearAnomalies()
{
    m_anomalies.clear();
    m_anomalyScanned = false;
    if ( m_anomalyTable )
        m_anomalyTable->setRowCount( 0 );
    if ( m_anomalyStatus )
        m_anomalyStatus->setText(
            "Selected address ranges changed. Run Scan again." );
    m_plot->SetAnomalyMarkers( {} );
    m_overviewPlot->SetAnomalyMarkers( {} );
}

std::vector< AnomalyMarker_s > CantorPanel::AnomalyMarkers(
    const u64 start, const u64 span, const View_e mode ) const
{
    std::vector< AnomalyMarker_s > markers;
    if ( ( mode != CDV_LINEAR && mode != CDV_HILBERT ) ||
         span == 0 || m_anomalies.empty() )
        return markers;

    const usize pixels = Resolution() * Resolution();
    const u64 end = start + span;
    markers.reserve( m_anomalies.size() );
    for ( const AnomalyFinding_s& finding : m_anomalies )
    {
        if ( finding.offset >= end ||
             finding.offset + finding.length <= start )
            continue;
        const u64 clippedStart = std::max( start, finding.offset );
        const u64 clippedEnd = std::min( end,
            finding.offset + finding.length );
        const u64 middle = clippedStart + ( clippedEnd - clippedStart ) / 2;
        markers.push_back({ Math_OverviewPixelForOffset(
            span, middle - start, pixels ), finding.kind });
    }
    std::sort( markers.begin(), markers.end(),
        []( const AnomalyMarker_s& a, const AnomalyMarker_s& b ) {
            return a.ordinal < b.ordinal ||
                ( a.ordinal == b.ordinal && a.kind < b.kind );
        } );
    markers.erase( std::unique( markers.begin(), markers.end(),
        []( const AnomalyMarker_s& a, const AnomalyMarker_s& b ) {
            return a.ordinal == b.ordinal && a.kind == b.kind;
        } ), markers.end() );
    return markers;
}

void CantorPanel::RefreshAnomalyPalette()
{
    if ( !m_anomalyTable )
        return;

    // Table swatches follow the Detail palette; Overview markers use its
    // independently chosen palette in PlotWidget::paintEvent().
    const Palette_e palette = CurrentPalette();
    for ( int row = 0; row < m_anomalyTable->rowCount(); ++row )
    {
        QTableWidgetItem* const item = m_anomalyTable->item( row, 1 );
        if ( !item ) continue;
        const auto kind = static_cast< AnomalyKind_e >(
            item->data( Qt::UserRole ).toInt() );
        QPixmap swatch( 16, 16 );
        swatch.fill( Qt::transparent );
        QPainter painter( &swatch );
        painter.setRenderHint( QPainter::Antialiasing );
        painter.setBrush( QColor::fromRgba(
            Anomaly_PaletteColor( kind, palette ) ) );
        painter.setPen( QPen( QColor::fromRgba(
            Anomaly_OutlineColor( Palette_Get( palette ).background ) ), 1.5 ) );
        painter.drawEllipse( QRect( 2, 2, 12, 12 ) );
        painter.end();
        item->setIcon( QIcon( swatch ) );
        item->setToolTip( QString( "%1 | %2 palette" )
            .arg( item->text() )
            .arg( QString::fromUtf8( Palette_Get( palette ).name ) ) );
    }
}

void CantorPanel::GoToAnomaly( const u64 offset )
{
    u64 address = 0;
    if ( !Math_OffsetToAddress( m_ranges, offset, address ) )
        return;
    if ( !jumpto( static_cast< ea_t >( address ) ) )
    {
        m_hintLabel->setText( "Could not navigate to anomaly address." );
        return;
    }
    if ( m_detailSpan != 0 )
    {
        const u64 half = m_detailSpan / 2;
        SetWindowOffset( offset > half ? offset - half : 0 );
    }
}

void CantorPanel::ShowAnomalySettings()
{
    QDialog dialog( m_anomalyDialog );
    dialog.setWindowTitle( "Anomaly Explorer - Options" );
    dialog.setMinimumWidth( 485 );
    QVBoxLayout* const layout = new QVBoxLayout( &dialog );
    QLabel* const explanation = new QLabel(
        "Choose which indicators to detect and how sensitive they are. "
        "Changing these options rescans the selected ranges.", &dialog );
    explanation->setWordWrap( true );
    layout->addWidget( explanation );

    QGroupBox* const detectorGroup = new QGroupBox( "Detectors", &dialog );
    QGridLayout* const detectors = new QGridLayout( detectorGroup );
    detectors->addWidget( new QLabel( "Enabled", detectorGroup ), 0, 0 );
    detectors->addWidget( new QLabel( "Threshold", detectorGroup ), 0, 1 );

    const auto makeToggle = [&]( int row, const char* caption, bool value,
        const char* tip ) {
        QCheckBox* const toggle = new QCheckBox( caption, detectorGroup );
        toggle->setChecked( value );
        toggle->setToolTip( tip );
        detectors->addWidget( toggle, row, 0 );
        return toggle;
    };
    const auto makeThreshold = [&]( int row, double value, double minimum,
        double maximum, double step, const QString& suffix,
        QCheckBox* toggle ) {
        QDoubleSpinBox* const spin = new QDoubleSpinBox( detectorGroup );
        spin->setDecimals( 2 );
        spin->setRange( minimum, maximum );
        spin->setSingleStep( step );
        spin->setSuffix( suffix );
        spin->setValue( value );
        spin->setEnabled( toggle->isChecked() );
        QObject::connect( toggle, &QCheckBox::toggled,
            spin, &QWidget::setEnabled );
        detectors->addWidget( spin, row, 1 );
        return spin;
    };

    QCheckBox* const high = makeToggle( 1, "High entropy",
        m_anomalyOptions.highEntropy, "Report high-entropy byte windows." );
    QDoubleSpinBox* const highThreshold = makeThreshold( 1,
        m_anomalyOptions.highEntropyBits, 0.0, 8.0, 0.05,
        " bits/byte", high );
    QCheckBox* const jump = makeToggle( 2, "Entropy change",
        m_anomalyOptions.entropyChange,
        "Compare adjacent contiguous windows; segment gaps are excluded." );
    QDoubleSpinBox* const jumpThreshold = makeThreshold( 2,
        m_anomalyOptions.entropyChangeBits, 0.0, 8.0, 0.1,
        " bits/byte", jump );
    QCheckBox* const padding = makeToggle( 3, "00/FF padding",
        m_anomalyOptions.padding, "Detect mostly zero or FF bytes." );
    QDoubleSpinBox* const paddingThreshold = makeThreshold( 3,
        m_anomalyOptions.paddingFraction * 100.0, 50.0, 100.0, 1.0,
        "% matching", padding );
    QCheckBox* const dominant = makeToggle( 4, "Dominant byte",
        m_anomalyOptions.dominantByte,
        "Detect a single byte that dominates the window." );
    QDoubleSpinBox* const dominantThreshold = makeThreshold( 4,
        m_anomalyOptions.dominantFraction * 100.0, 1.0, 100.0, 1.0,
        "% matching", dominant );
    QCheckBox* const patterns = makeToggle( 5, "Repeated pattern",
        m_anomalyOptions.repeatedPattern,
        "Detect motifs of length 2, 4, 8 or 16 bytes." );
    QDoubleSpinBox* const patternThreshold = makeThreshold( 5,
        m_anomalyOptions.patternFraction * 100.0, 50.0, 100.0, 1.0,
        "% matching", patterns );
    layout->addWidget( detectorGroup );

    QGroupBox* const scanGroup = new QGroupBox( "Scan limits", &dialog );
    QFormLayout* const scanForm = new QFormLayout( scanGroup );
    QComboBox* const windowBox = new QComboBox( scanGroup );
    for ( const int size : { 1024, 4096, 16384 } )
        windowBox->addItem( QString( "%1 KiB" ).arg( size / 1024 ), size );
    windowBox->setCurrentIndex( windowBox->findData(
        static_cast< int >( m_anomalyOptions.windowBytes ) ) );
    windowBox->setToolTip( "Smaller windows locate shorter patterns; "
        "larger windows provide more stable statistics." );
    scanForm->addRow( "Window size", windowBox );

    QComboBox* const budgetBox = new QComboBox( scanGroup );
    for ( const int size : { 4, 16, 32, 64 } )
        budgetBox->addItem( QString( "%1 MiB" ).arg( size ), size );
    budgetBox->setCurrentIndex( budgetBox->findData(
        static_cast< int >( m_anomalyOptions.scanBudgetMiB ) ) );
    budgetBox->setToolTip( "Maximum bytes read per scan; larger selections "
        "are distributed-sampled, not fully scanned." );
    scanForm->addRow( "Read budget", budgetBox );

    QComboBox* const limitBox = new QComboBox( scanGroup );
    for ( const int size : { 25, 100, 250, 500 } )
        limitBox->addItem( QString::number( size ), size );
    limitBox->setCurrentIndex( limitBox->findData(
        static_cast< int >( m_anomalyOptions.maxFindingsPerKind ) ) );
    scanForm->addRow( "Max findings per type", limitBox );
    layout->addWidget( scanGroup );

    QDialogButtonBox* const buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
    QPushButton* const defaults = buttons->addButton(
        "Restore defaults", QDialogButtonBox::ResetRole );
    QObject::connect( defaults, &QPushButton::clicked, &dialog, [&]() {
        const AnomalyOptions_s initial;
        high->setChecked( initial.highEntropy );
        jump->setChecked( initial.entropyChange );
        padding->setChecked( initial.padding );
        dominant->setChecked( initial.dominantByte );
        patterns->setChecked( initial.repeatedPattern );
        highThreshold->setValue( initial.highEntropyBits );
        jumpThreshold->setValue( initial.entropyChangeBits );
        paddingThreshold->setValue( initial.paddingFraction * 100.0 );
        dominantThreshold->setValue( initial.dominantFraction * 100.0 );
        patternThreshold->setValue( initial.patternFraction * 100.0 );
        windowBox->setCurrentIndex( windowBox->findData(
            static_cast< int >( initial.windowBytes ) ) );
        budgetBox->setCurrentIndex( budgetBox->findData(
            static_cast< int >( initial.scanBudgetMiB ) ) );
        limitBox->setCurrentIndex( limitBox->findData(
            static_cast< int >( initial.maxFindingsPerKind ) ) );
    } );
    QObject::connect( buttons, &QDialogButtonBox::accepted,
        &dialog, &QDialog::accept );
    QObject::connect( buttons, &QDialogButtonBox::rejected,
        &dialog, &QDialog::reject );
    layout->addWidget( buttons );
    if ( dialog.exec() != QDialog::Accepted )
        return;

    m_anomalyOptions.highEntropy = high->isChecked();
    m_anomalyOptions.entropyChange = jump->isChecked();
    m_anomalyOptions.padding = padding->isChecked();
    m_anomalyOptions.dominantByte = dominant->isChecked();
    m_anomalyOptions.repeatedPattern = patterns->isChecked();
    m_anomalyOptions.highEntropyBits = highThreshold->value();
    m_anomalyOptions.entropyChangeBits = jumpThreshold->value();
    m_anomalyOptions.paddingFraction = paddingThreshold->value() / 100.0;
    m_anomalyOptions.dominantFraction = dominantThreshold->value() / 100.0;
    m_anomalyOptions.patternFraction = patternThreshold->value() / 100.0;
    m_anomalyOptions.windowBytes = static_cast< usize >(
        windowBox->currentData().toInt() );
    m_anomalyOptions.scanBudgetMiB = static_cast< usize >(
        budgetBox->currentData().toInt() );
    m_anomalyOptions.maxFindingsPerKind = static_cast< usize >(
        limitBox->currentData().toInt() );
    SaveAnomalyOptions( m_anomalyOptions );
    ScanAnomalies();
}

void CantorPanel::ShowAnomalyExplorer()
{
    if ( !m_anomalyDialog )
    {
        LoadAnomalyOptions( m_anomalyOptions );
        m_anomalyDialog = new QDialog( this );
        m_anomalyDialog->setWindowTitle( "Cantor Dust - Anomaly Explorer" );
        m_anomalyDialog->resize( 820, 460 );
        QVBoxLayout* const layout = new QVBoxLayout( m_anomalyDialog );
        QLabel* const help = new QLabel(
            "Heuristic indicators only, not proof of encryption or malicious code. "
            "Palette-colored markers appear in Linear Bitmap and Hilbert Curve. "
            "Click a finding to jump to its address.", m_anomalyDialog );
        help->setWordWrap( true );
        layout->addWidget( help );

        QHBoxLayout* const header = new QHBoxLayout();
        QPushButton* const rescan = new QPushButton( "Scan again", m_anomalyDialog );
        header->addWidget( rescan );
        QPushButton* const options = new QPushButton(
            "Options...", m_anomalyDialog );
        header->addWidget( options );
        m_anomalyStatus = new QLabel( m_anomalyDialog );
        m_anomalyStatus->setWordWrap( true );
        header->addWidget( m_anomalyStatus, 1 );
        layout->addLayout( header );

        m_anomalyTable = new QTableWidget( m_anomalyDialog );
        m_anomalyTable->setColumnCount( 6 );
        m_anomalyTable->setHorizontalHeaderLabels({
            "IDA address", "Finding", "Length", "Entropy", "Signal", "Detail" });
        m_anomalyTable->setSelectionBehavior( QAbstractItemView::SelectRows );
        m_anomalyTable->setSelectionMode( QAbstractItemView::SingleSelection );
        m_anomalyTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
        m_anomalyTable->verticalHeader()->setVisible( false );
        m_anomalyTable->horizontalHeader()->setSectionResizeMode(
            5, QHeaderView::Stretch );
        m_anomalyTable->setSortingEnabled( true );
        layout->addWidget( m_anomalyTable, 1 );

        QObject::connect( rescan, &QPushButton::clicked,
            this, [this]() { ScanAnomalies(); } );
        QObject::connect( options, &QPushButton::clicked,
            this, [this]() { ShowAnomalySettings(); } );
        QObject::connect( m_anomalyTable, &QTableWidget::cellClicked,
            this, [this]( const int row, int ) {
                if ( const QTableWidgetItem* const item =
                        m_anomalyTable->item( row, 0 ) )
                    GoToAnomaly( item->data( Qt::UserRole ).toULongLong() );
            } );
    }
    m_anomalyDialog->show();
    m_anomalyDialog->raise();
    m_anomalyDialog->activateWindow();
    if ( !m_anomalyScanned )
        ScanAnomalies();
}

void CantorPanel::ScanAnomalies()
{
    m_anomalyScanned = true;
    m_anomalies.clear();
    m_anomalyTable->setRowCount( 0 );
    m_plot->SetAnomalyMarkers( {} );
    m_overviewPlot->SetAnomalyMarkers( {} );

    const usize blockBytes = m_anomalyOptions.windowBytes;
    const usize scanBudget = m_anomalyOptions.scanBudgetMiB * kMiB / blockBytes;
    if ( !m_anomalyOptions.highEntropy && !m_anomalyOptions.entropyChange &&
         !m_anomalyOptions.padding && !m_anomalyOptions.dominantByte &&
         !m_anomalyOptions.repeatedPattern )
    {
        m_anomalyStatus->setText(
            "All detectors disabled. Enable at least one in Options." );
        return;
    }

    // Count complete windows per segment separately. Do not join unrelated
    // segment ends even if their logical offsets are adjacent in the UI.
    u64 totalBlocks = 0;
    for ( const ByteRange_s& range : m_ranges )
        totalBlocks += range.length / blockBytes;
    if ( totalBlocks == 0 )
    {
        m_anomalyStatus->setText(
            QString( "Not enough bytes to scan (minimum one %1 KiB window)." )
            .arg( static_cast< qulonglong >( blockBytes / 1024 ) ) );
        return;
    }

    const usize sampleCount = static_cast< usize >(
        std::min< u64 >( totalBlocks, scanBudget ) );
    const bool sampled = totalBlocks > scanBudget;
    QProgressDialog progress( "Inspecting selected byte ranges...",
        "Cancel", 0, static_cast< int >( sampleCount ), m_anomalyDialog );
    progress.setWindowTitle( "Anomaly scan" );
    progress.setWindowModality( Qt::WindowModal );
    progress.setMinimumDuration( 0 );

    AnomalyDetector detector( m_anomalyOptions );
    usize previousRange = m_ranges.size();
    u64 previousLogicalEnd = 0;
    bool canceled = false;
    for ( usize i = 0; i < sampleCount; ++i )
    {
        if ( ( i % 16 ) == 0 )
        {
            progress.setValue( static_cast< int >( i ) );
            QCoreApplication::processEvents();
            if ( progress.wasCanceled() )
            {
                canceled = true;
                break;
            }
        }
        u64 blockIndex = Math_ScaledPosition( totalBlocks,
            static_cast< u32 >( i ), static_cast< u32 >( sampleCount ) );
        usize rangeIndex = 0;
        for ( ; rangeIndex < m_ranges.size(); ++rangeIndex )
        {
            const u64 blocks = m_ranges[ rangeIndex ].length / blockBytes;
            if ( blockIndex < blocks ) break;
            blockIndex -= blocks;
        }
        if ( rangeIndex >= m_ranges.size() ) break;
        const ByteRange_s& range = m_ranges[ rangeIndex ];
        const u64 logical = range.offset + blockIndex * blockBytes;
        if ( rangeIndex != previousRange || logical != previousLogicalEnd )
            detector.Break();
        const IdaWindow_s window = Ida_ReadWindow(
            static_cast< ea_t >( range.address + blockIndex * blockBytes ),
            blockBytes );
        detector.Analyze( window.cells, logical );
        previousRange = rangeIndex;
        previousLogicalEnd = logical + blockBytes;
    }
    progress.setValue( static_cast< int >( sampleCount ) );
    if ( canceled )
    {
        m_anomalyScanned = false;
        m_anomalyStatus->setText( "Scan canceled; no partial results retained." );
        return;
    }

    // Fairly limit results by category so one long high-entropy region
    // doesn't crowd every other kind of anomaly out of the explorer.
    std::vector< AnomalyFinding_s > ordered = detector.Findings();
    std::stable_sort( ordered.begin(), ordered.end(),
        []( const AnomalyFinding_s& a, const AnomalyFinding_s& b ) {
            return a.strength > b.strength;
        } );
    std::array< usize, 5 > categoryCount{};
    for ( const AnomalyFinding_s& item : ordered )
    {
        usize& count = categoryCount[ static_cast< usize >( item.kind ) ];
        if ( count++ < m_anomalyOptions.maxFindingsPerKind )
            m_anomalies.push_back( item );
    }
    std::sort( m_anomalies.begin(), m_anomalies.end(),
        []( const AnomalyFinding_s& a, const AnomalyFinding_s& b ) {
            return a.offset < b.offset ||
                ( a.offset == b.offset && a.kind < b.kind );
        } );

    m_anomalyTable->setSortingEnabled( false );
    m_anomalyTable->setRowCount( static_cast< int >( m_anomalies.size() ) );
    for ( usize i = 0; i < m_anomalies.size(); ++i )
    {
        const AnomalyFinding_s& item = m_anomalies[ i ];
        u64 address = 0;
        if ( !Math_OffsetToAddress( m_ranges, item.offset, address ) )
            continue;
        const int row = static_cast< int >( i );
        const auto put = [&]( const int col, const QString& value ) {
            m_anomalyTable->setItem( row, col, new QTableWidgetItem( value ) );
        };
        put( 0, Ida_FormatAddress( static_cast< ea_t >( address ) ) );
        m_anomalyTable->item( row, 0 )->setData( Qt::UserRole,
            QVariant::fromValue< qulonglong >( item.offset ) );
        put( 1, QString::fromLatin1( Anomaly_KindName( item.kind ) ) );
        m_anomalyTable->item( row, 1 )->setData( Qt::UserRole,
            static_cast< int >( item.kind ) );
        put( 2, QString::number( item.length ) );
        put( 3, QString::number( item.entropy, 'f', 2 ) + " / 8" );
        put( 4, QString::number( item.strength * 100.0, 'f', 0 ) + "%" );
        QString detail;
        if ( item.kind == AnomalyKind_e::Padding ||
             item.kind == AnomalyKind_e::DominantByte )
            detail = QString( "Dominant 0x%1" ).arg(
                static_cast< uint >( item.value ), 2, 16, QChar( '0' ) );
        else if ( item.kind == AnomalyKind_e::RepeatedPattern )
            detail = QString( "Period %1 bytes" ).arg( static_cast< uint >( item.value ) );
        else if ( item.kind == AnomalyKind_e::EntropyChange )
            detail = "Abrupt change from previous contiguous window";
        else
            detail = "High randomness-like byte distribution";
        put( 5, detail );
    }
    m_anomalyTable->setSortingEnabled( true );
    RefreshAnomalyPalette();
    m_anomalyStatus->setText( QString(
        "%1 finding(s) shown | %2 / %3 windows scanned%4 | "
        "%5 KiB windows, up to %6 findings/type" )
        .arg( static_cast< qulonglong >( m_anomalies.size() ) )
        .arg( static_cast< qulonglong >( sampleCount ) )
        .arg( static_cast< qulonglong >( totalBlocks ) )
        .arg( sampled ? " (distributed sampling, not exhaustive)" : "" )
        .arg( static_cast< qulonglong >( blockBytes / 1024 ) )
        .arg( static_cast< qulonglong >(
            m_anomalyOptions.maxFindingsPerKind ) ) );
    // Update overlays without rebuilding expensive 3D views or re-reading
    // the byte range. The next ordinary render will retain these markers.
    m_plot->SetAnomalyMarkers( AnomalyMarkers(
        m_windowOffset, m_detailSpan, static_cast< View_e >(
            m_viewBox->currentData().toInt() ) ) );
    m_overviewPlot->SetAnomalyMarkers( AnomalyMarkers(
        m_overviewOffset, m_overviewSpan, static_cast< View_e >(
            m_overviewViewBox->currentData().toInt() ) ) );
}

} // namespace cantordust
