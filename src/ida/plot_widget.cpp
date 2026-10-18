#include "pch/ida_pch.h"
#include "ida/plot_widget.h"
#include "cantordust/scalar_types.h"
#include "ida/range_selection_style.h"
#include "math/anomaly_palette.h"

#include "math/pixel.h"
#include "math/hilbert.h"

#include <QRegion>
#include <QToolTip>

namespace cantordust
{

PlotWidget::PlotWidget( QWidget* const parent ) : QWidget( parent )
{
    setMinimumSize( 64, 64 );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    setMouseTracking( true );
    setCursor( Qt::CrossCursor );

    // Do not regenerate a large byte raster for every intermediate dock
    // dimension during a splitter drag. The existing image fills the
    // viewport immediately; the byte layout reflows after resizing settles.
    m_resizeTimer.setSingleShot( true );
    m_resizeTimer.setInterval( 60 );
    QObject::connect( &m_resizeTimer, &QTimer::timeout,
        this, [this]() { RebuildDisplayRaster(); update(); } );
}

void PlotWidget::SetImage( Image_s image )
{
    m_image = std::move( image );
    m_dragging = false;
    setCursor( Qt::CrossCursor );
    m_navigationGate.Reset();
    ClearHover();
    m_resizeTimer.stop();
    RebuildDisplayRaster();
    update();
}

void PlotWidget::SetPalette( const Palette_e palette )
{
    if ( m_palette == palette )
        return;

    m_palette = palette;
    update();
}

void PlotWidget::SetViewMode( const View_e mode )
{
    if ( m_viewMode == mode )
        return;

    m_viewMode = mode;
    ClearHover();
    m_resizeTimer.stop();
    RebuildDisplayRaster();
    update();
}

void PlotWidget::SetOnWheel(
    std::function< void( usize, usize,
        usize, int ) > callback )
{
    m_onWheel = std::move( callback );
}

void PlotWidget::SetHighlights( std::vector< QRect > highlights )
{
    m_highlights = std::move( highlights );
    RebuildDisplayHighlights();
    update();
}

void PlotWidget::SetAnomalyMarkers( std::vector< AnomalyMarker_s > markers )
{
    std::sort( markers.begin(), markers.end(),
        []( const AnomalyMarker_s& a, const AnomalyMarker_s& b ) {
            return a.ordinal < b.ordinal ||
                ( a.ordinal == b.ordinal && a.kind < b.kind );
        } );
    markers.erase( std::unique( markers.begin(), markers.end(),
        []( const AnomalyMarker_s& a, const AnomalyMarker_s& b ) {
            return a.ordinal == b.ordinal && a.kind == b.kind;
        } ), markers.end() );
    if ( m_anomalyMarkers.size() == markers.size() &&
         std::equal( m_anomalyMarkers.begin(), m_anomalyMarkers.end(),
             markers.begin(), []( const AnomalyMarker_s& a,
                 const AnomalyMarker_s& b ) {
                 return a.ordinal == b.ordinal && a.kind == b.kind;
             } ) )
        return;
    m_anomalyMarkers = std::move( markers );
    RebuildAnomalyMarkers();
    update();
}

void PlotWidget::RebuildAnomalyMarkers()
{
    m_displayAnomalyMarkers.clear();
    const Image_s& display = DisplayImage();
    if ( m_anomalyMarkers.empty() || display.width == 0 ||
         ( m_viewMode != CDV_LINEAR && m_viewMode != CDV_HILBERT ) )
        return;

    if ( !m_fittedRaster.ordinal.empty() &&
         ( display.width != m_image.width ||
           display.height != m_image.height ) )
    {
        // The fitted Gilbert/Hilbert traversal may be non-square. Match
        // ordinals to every finding category without losing palette colors.
        for ( usize i = 0; i < m_fittedRaster.ordinal.size(); ++i )
        {
            const usize ordinal = m_fittedRaster.ordinal[ i ];
            auto marker = std::lower_bound( m_anomalyMarkers.begin(),
                m_anomalyMarkers.end(), ordinal,
                []( const AnomalyMarker_s& a, const usize value ) {
                    return a.ordinal < value;
                } );
            while ( marker != m_anomalyMarkers.end() &&
                    marker->ordinal == ordinal )
            {
                m_displayAnomalyMarkers.push_back({
                    QPoint( static_cast< int >( i % display.width ),
                        static_cast< int >( i / display.width ) ),
                    marker->kind });
                ++marker;
            }
        }
    }
    else
    {
        for ( const AnomalyMarker_s& marker : m_anomalyMarkers )
        {
            if ( marker.ordinal >= m_image.width * m_image.height ) continue;
            const Point_s p = m_viewMode == CDV_HILBERT
                ? Math_HilbertPosition( marker.ordinal, m_image.width )
                : Point_s{ marker.ordinal % m_image.width,
                    marker.ordinal / m_image.width };
            m_displayAnomalyMarkers.push_back({
                QPoint( static_cast< int >( p.x ),
                    static_cast< int >( p.y ) ), marker.kind });
        }
    }
}

void PlotWidget::SetOnNavigate(
    std::function< void( usize ) > callback )
{
    m_onNavigate = std::move( callback );
}

void PlotWidget::SetOnHover(
    std::function< void( usize ) > callback )
{
    m_onHover = std::move( callback );
}

void PlotWidget::SetAddressFormatter(
    std::function< QString( usize ) > callback )
{
    m_addressFormatter = std::move( callback );
}

const Image_s& PlotWidget::DisplayImage() const
{
    return !m_fittedRaster.image.argb.empty()
        ? m_fittedRaster.image : m_image;
}

void PlotWidget::RebuildDisplayRaster()
{
    m_fittedRaster = {};

    if ( ( m_viewMode == CDV_LINEAR || m_viewMode == CDV_HILBERT ) &&
         !m_image.argb.empty() &&
         contentsRect().width() > 0 && contentsRect().height() > 0 &&
         contentsRect().width() != contentsRect().height() )
    {
        m_fittedRaster = Math_FitByteRaster( m_image, m_viewMode,
            contentsRect().width(), contentsRect().height() );
    }

    RebuildDisplayHighlights();
    RebuildAnomalyMarkers();
}

void PlotWidget::RebuildDisplayHighlights()
{
    m_displayHighlights = m_highlights;

    const Image_s& display = DisplayImage();
    if ( m_highlights.empty() || m_fittedRaster.ordinal.empty() ||
         ( display.width == m_image.width &&
           display.height == m_image.height ) )
    {
        return;
    }

    // Overview selections arrive in source-square coordinates. Rebuild the
    // selected runs in the fitted geometry so the contour and navigation
    // remain accurate after either dock is resized.
    std::vector< u8 > selected( m_image.width * m_image.height, 0 );
    const QRect sourceBounds( 0, 0,
        static_cast< int >( m_image.width ),
        static_cast< int >( m_image.height ) );

    for ( const QRect& rect : m_highlights )
    {
        const QRect clipped = rect.intersected( sourceBounds );
        for ( int y = clipped.top(); y <= clipped.bottom(); ++y )
            for ( int x = clipped.left(); x <= clipped.right(); ++x )
                selected[ static_cast< usize >( y ) * m_image.width +
                    static_cast< usize >( x ) ] = 1;
    }

    m_displayHighlights.clear();
    for ( usize y = 0; y < display.height; ++y )
    {
        usize x = 0;
        while ( x < display.width )
        {
            const auto isSelected = [&]( const usize column ) {
                const usize ordinal = m_fittedRaster.ordinal[
                    y * display.width + column ];
                const usize sourcePixel = m_viewMode == CDV_HILBERT
                    ? [&]() {
                        const Point_s point = Math_HilbertPosition(
                            ordinal, m_image.width );
                        return point.y * m_image.width + point.x;
                    }()
                    : ordinal;
                return selected[ sourcePixel ] != 0;
            };

            if ( !isSelected( x ) )
            {
                ++x;
                continue;
            }

            const usize start = x;
            while ( x < display.width && isSelected( x ) )
                ++x;

            m_displayHighlights.emplace_back(
                static_cast< int >( start ), static_cast< int >( y ),
                static_cast< int >( x - start ), 1 );
        }
    }
}

QRect PlotWidget::TargetRect() const
{
    const QRect bounds = contentsRect();

    if ( bounds.isEmpty() || DisplayImage().width == 0 ||
         DisplayImage().height == 0 )
    {
        return {};
    }

    return bounds;
}

QRect PlotWidget::MapImageRect(
    const QRect& imageRect,
    const QRect& target ) const
{
    const Image_s& image = DisplayImage();
    const i64 left = target.x() +
        static_cast< i64 >( imageRect.x() ) * target.width() /
        static_cast< i64 >( image.width );

    const i64 top = target.y() +
        static_cast< i64 >( imageRect.y() ) * target.height() /
        static_cast< i64 >( image.height );

    const i64 right = target.x() +
        static_cast< i64 >( imageRect.x() + imageRect.width() ) *
        target.width() /
        static_cast< i64 >( image.width );

    const i64 bottom = target.y() +
        static_cast< i64 >( imageRect.y() + imageRect.height() ) *
        target.height() /
        static_cast< i64 >( image.height );

    return QRect(
        static_cast< int >( left ),
        static_cast< int >( top ),
        std::max< int >( 1, static_cast< int >( right - left ) ),
        std::max< int >( 1, static_cast< int >( bottom - top ) ) );
}

usize PlotWidget::SourceAt(
    const QPoint& position,
    QPoint* const pixel ) const
{
    const QRect target = TargetRect();
    const Image_s& image = DisplayImage();
    Point_s mapped;

    if ( !Math_MapPointToImage(
             position.x(), position.y(),
             target.x(), target.y(), target.width(), target.height(),
             image.width, image.height, mapped ) )
    {
        return CANTORDUST_NO_SOURCE;
    }

    if ( pixel != nullptr )
    {
        *pixel = QPoint(
            static_cast< int >( mapped.x ),
            static_cast< int >( mapped.y ) );
    }

    return image.SourceAt( mapped.x, mapped.y );
}

void PlotWidget::ClearHover()
{
    if ( m_inspectedBucket != CANTORDUST_NO_SOURCE )
    {
        m_inspectedBucket = CANTORDUST_NO_SOURCE;
        QToolTip::hideText();
    }
    if ( m_hoverPixel != QPoint( -1, -1 ) )
    {
        m_hoverPixel = QPoint( -1, -1 );
        update();
    }

    if ( m_hoverSource != CANTORDUST_NO_SOURCE )
    {
        m_hoverSource = CANTORDUST_NO_SOURCE;

        if ( m_onHover )
        {
            m_onHover( CANTORDUST_NO_SOURCE );
        }
    }
}

void PlotWidget::UpdateHover( const QPoint& position )
{
    QPoint pixel( -1, -1 );

    const usize source = SourceAt( position, &pixel );

    if ( source == CANTORDUST_NO_SOURCE )
    {
        if ( pixel.x() < 0 )
        {
            ClearHover();
            return;
        }
        // Empty heatmap bins still have meaningful zero-count tooltips.
        // Don't clear the inspection bucket on each mouse move, otherwise
        // Qt repeatedly hides and reopens the same tooltip (flicker).
        if ( m_hoverPixel != QPoint( -1, -1 ) )
        {
            m_hoverPixel = QPoint( -1, -1 );
            update();
        }
        if ( m_hoverSource != CANTORDUST_NO_SOURCE )
        {
            m_hoverSource = CANTORDUST_NO_SOURCE;
            if ( m_onHover ) m_onHover( CANTORDUST_NO_SOURCE );
        }
        ShowBucketInspection( position, pixel );
        return;
    }

    ShowBucketInspection( position, pixel );
    if ( pixel != m_hoverPixel )
    {
        m_hoverPixel = pixel;
        update();
    }

    if ( source != m_hoverSource )
    {
        m_hoverSource = source;

        if ( m_onHover )
        {
            m_onHover( source );
        }
    }
}

void PlotWidget::ShowBucketInspection(
    const QPoint& position, const QPoint& pixel )
{
    if ( pixel.x() < 0 || pixel.y() < 0 ||
         m_image.bucketCounts.empty() ||
         ( m_viewMode != CDV_HISTOGRAM && m_viewMode != CDV_DIGRAPH ) )
        return;

    const Image_s& image = DisplayImage();
    const usize first = static_cast< usize >( pixel.x() ) * 256 / image.width;
    const usize second = 255 -
        static_cast< usize >( pixel.y() ) * 256 / image.height;
    const usize bucket = m_viewMode == CDV_HISTOGRAM
        ? first : ( 255 - second ) * 256 + first;
    if ( bucket == m_inspectedBucket || bucket >= m_image.bucketCounts.size() )
        return;

    m_inspectedBucket = bucket;
    const u32 count = m_image.bucketCounts[ bucket ];
    const f64 percent = m_image.observationCount
        ? 100.0 * static_cast< f64 >( count ) / m_image.observationCount
        : 0.0;
    QString text;
    if ( m_viewMode == CDV_HISTOGRAM )
    {
        const QChar printable = first >= 0x20 && first <= 0x7e
            ? QChar( static_cast< ushort >( first ) ) : QChar( '.' );
        text = QString( "Byte 0x%1 (%2, '%3')\n"
            "Count: %4 (%5% of valid bytes)" )
            .arg( static_cast< uint >( first ), 2, 16, QChar( '0' ) )
            .arg( static_cast< uint >( first ) )
            .arg( printable )
            .arg( count )
            .arg( percent, 0, 'f', 2 );
    }
    else
    {
        const u32 rowTotal = m_image.firstByteTotals[ first ];
        const f64 conditional = rowTotal
            ? 100.0 * static_cast< f64 >( count ) / rowTotal : 0.0;
        text = QString( "Pair 0x%1 -> 0x%2 | distance %3\n"
            "Count: %4 (%5% of observed pairs)\n"
            "P(next | first): %6% (%7 starting with 0x%8)" )
            .arg( static_cast< uint >( first ), 2, 16, QChar( '0' ) )
            .arg( static_cast< uint >( second ), 2, 16, QChar( '0' ) )
            .arg( static_cast< qulonglong >( m_image.pairDistance ) )
            .arg( count )
            .arg( percent, 0, 'f', 2 )
            .arg( conditional, 0, 'f', 2 )
            .arg( rowTotal )
            .arg( static_cast< uint >( first ), 2, 16, QChar( '0' ) );
    }
    const usize source = m_image.SourceAt(
        static_cast< usize >( pixel.x() ),
        static_cast< usize >( pixel.y() ) );
    if ( source != CANTORDUST_NO_SOURCE && m_addressFormatter )
        text += "\nFirst match: " + m_addressFormatter( source );
    if ( count > 0 )
        text += "\nClick or drag to navigate.";
    QToolTip::showText( mapToGlobal( position ), text, this );
}

void PlotWidget::NavigateAt( const QPoint& position, const bool force )
{
    const usize source = SourceAt( position, nullptr );

    if ( source == CANTORDUST_NO_SOURCE || !m_onNavigate )
    {
        return;
    }

    const u64 elapsedMs =
        static_cast< u64 >( m_navigationClock.elapsed() );

    if ( m_navigationGate.ShouldNavigate( source, elapsedMs, force ) )
    {
        m_onNavigate( source );
    }
}

void PlotWidget::paintEvent( QPaintEvent* )
{
    QPainter painter( this );
    painter.fillRect( rect(),
        QColor::fromRgba( Palette_Get( m_palette ).background ) );

    const QRect target = TargetRect();
    const Image_s& image = DisplayImage();

    if ( target.isEmpty() || image.argb.empty() )
    {
        return;
    }

    const QImage bitmap(
        reinterpret_cast< const uchar* >( image.argb.data() ),
        static_cast< int >( image.width ),
        static_cast< int >( image.height ),
        static_cast< int >( image.width * sizeof( u32 ) ),
        QImage::Format_ARGB32 );

    painter.setRenderHint( QPainter::SmoothPixmapTransform, false );
    painter.drawImage( target, bitmap );

    // Highlights are delivered as one-pixel-high runs (especially in the
    // Hilbert overview). Drawing a border around every run produces dense,
    // opaque stripes inside the selection. Union the runs in screen space,
    // then fill once and draw only the actual outside contour.
    QRegion selection;

    for ( const QRect& imageRect : m_displayHighlights )
    {
        const QRect overlay = MapImageRect( imageRect, target ).intersected( target );

        if ( !overlay.isEmpty() )
            selection += overlay;
    }

    if ( !selection.isEmpty() )
    {
        painter.save();
        painter.setClipRegion( selection );
        painter.fillRect( target, RangeSelection_Fill( m_palette ) );
        painter.restore();

        // A one-device-pixel border inside the union, with no seams where
        // adjoining runs meet. It remains 1 px regardless of zoom/resolution.
        const QRegion interior = selection
            .intersected( selection.translated( -1, 0 ) )
            .intersected( selection.translated(  1, 0 ) )
            .intersected( selection.translated( 0, -1 ) )
            .intersected( selection.translated( 0,  1 ) );

        painter.save();
        painter.setClipRegion( selection.subtracted( interior ) );
        painter.fillRect( target, RangeSelection_Border( m_palette ) );
        painter.restore();
    }

    painter.setPen( QColor::fromRgba(
        Palette_Get( m_palette ).missing ) );
    painter.drawRect( target.adjusted( 0, 0, -1, -1 ) );

    // Small high-contrast markers, drawn over the byte raster but under
    // its hover indicator. These represent heuristic findings, not proof.
    if ( !m_displayAnomalyMarkers.empty() )
    {
        painter.save();
        for ( const DisplayAnomalyMarker_s& marker : m_displayAnomalyMarkers )
        {
            const usize pixelIndex =
                static_cast< usize >( marker.pixel.y() ) * image.width +
                static_cast< usize >( marker.pixel.x() );
            painter.setPen( QPen( QColor::fromRgba(
                Anomaly_OutlineColor( image.argb[ pixelIndex ] ) ), 1.7 ) );
            painter.setBrush( QColor::fromRgba(
                Anomaly_PaletteColor( marker.kind, m_palette ) ) );
            const int x = target.x() + static_cast< int >(
                ( static_cast< i64 >( marker.pixel.x() ) * 2 + 1 ) *
                target.width() / static_cast< i64 >( image.width * 2 ) );
            const int y = target.y() + static_cast< int >(
                ( static_cast< i64 >( marker.pixel.y() ) * 2 + 1 ) *
                target.height() / static_cast< i64 >( image.height * 2 ) );
            painter.drawEllipse( QPoint( x, y ), 4, 4 );
        }
        painter.restore();
    }

    if ( m_hoverPixel.x() >= 0 && m_hoverPixel.y() >= 0 )
    {
        const int x = target.x() + static_cast< int >(
            ( static_cast< i64 >( m_hoverPixel.x() ) * 2 + 1 ) *
            target.width() /
            static_cast< i64 >( image.width * 2 ) );

        const int y = target.y() + static_cast< int >(
            ( static_cast< i64 >( m_hoverPixel.y() ) * 2 + 1 ) *
            target.height() /
            static_cast< i64 >( image.height * 2 ) );

        painter.setPen( QPen( QColor::fromRgba(
            Palette_Get( m_palette ).selection ), 1 ) );
        painter.setBrush( Qt::NoBrush );
        painter.drawEllipse( QPoint( x, y ), 4, 4 );
    }
}

void PlotWidget::resizeEvent( QResizeEvent* const event )
{
    QWidget::resizeEvent( event );
    ClearHover();
    m_resizeTimer.start();
    update();
}

void PlotWidget::mousePressEvent( QMouseEvent* const event )
{
    if ( event->button() != Qt::LeftButton ||
         !TargetRect().contains( event->pos() ) )
    {
        QWidget::mousePressEvent( event );
        return;
    }

    m_dragging = true;
    setCursor( Qt::ClosedHandCursor );
    m_navigationGate.Reset();
    m_navigationClock.start();
    UpdateHover( event->pos() );
    NavigateAt( event->pos(), true );
    event->accept();
}

void PlotWidget::mouseMoveEvent( QMouseEvent* const event )
{
    UpdateHover( event->pos() );

    if ( m_dragging && event->buttons().testFlag( Qt::LeftButton ) )
    {
        NavigateAt( event->pos(), false );
    }
    else if ( m_dragging )
    {
        m_dragging = false;
        setCursor( Qt::CrossCursor );
    }

    event->accept();
}

void PlotWidget::mouseReleaseEvent( QMouseEvent* const event )
{
    if ( event->button() != Qt::LeftButton || !m_dragging )
    {
        QWidget::mouseReleaseEvent( event );
        return;
    }

    UpdateHover( event->pos() );
    NavigateAt( event->pos(), true );
    m_dragging = false;
    setCursor( Qt::CrossCursor );
    event->accept();
}

void PlotWidget::leaveEvent( QEvent* const event )
{
    ClearHover();
    QWidget::leaveEvent( event );
}

void PlotWidget::wheelEvent( QWheelEvent* const event )
{
    if ( !m_onWheel || DisplayImage().argb.empty() ||
         !TargetRect().contains( event->position().toPoint() ) )
    {
        QWidget::wheelEvent( event );
        return;
    }

    m_wheelRemainder += event->angleDelta().y();

    const int steps = m_wheelRemainder / 120;
    m_wheelRemainder %= 120;

    if ( steps != 0 )
    {
        const QPoint position = event->position().toPoint();
        QPoint pixel( -1, -1 );

        const usize source = SourceAt( position, &pixel );

        if ( pixel.x() >= 0 )
        {
            const Image_s& image = DisplayImage();
            usize ordinal = 0;

            if ( !m_fittedRaster.ordinal.empty() )
            {
                ordinal = m_fittedRaster.ordinal[
                    static_cast< usize >( pixel.y() ) * image.width +
                    static_cast< usize >( pixel.x() ) ];
            }
            else if ( m_viewMode == CDV_HILBERT )
            {
                ordinal = Math_HilbertIndex(
                    { static_cast< usize >( pixel.x() ),
                      static_cast< usize >( pixel.y() ) },
                    image.width );
            }
            else
            {
                ordinal = static_cast< usize >( pixel.y() ) *
                    image.width + static_cast< usize >( pixel.x() );
            }

            m_onWheel( source, ordinal, m_image.width * m_image.height,
                steps );
        }
    }

    event->accept();
}

} // namespace cantordust
