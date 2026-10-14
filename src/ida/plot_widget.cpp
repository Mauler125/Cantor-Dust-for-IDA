#include "ida/plot_widget.h"
#include "cantordust/scalar_types.h"
#include "ida/range_selection_style.h"

#include "math/pixel.h"
#include "math/hilbert.h"

#include <algorithm>
#include <cstdint>
#include <QRegion>
#include <utility>

namespace cantordust
{

PlotWidget::PlotWidget( QWidget* const parent ) : QWidget( parent )
{
    setMinimumSize( 64, 64 );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    setMouseTracking( true );
    setCursor( Qt::CrossCursor );
}

void PlotWidget::SetImage( Image_s image )
{
    m_image = std::move( image );
    m_dragging = false;
    setCursor( Qt::CrossCursor );
    m_navigationGate.Reset();
    ClearHover();
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
    m_viewMode = mode;
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
    update();
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

QRect PlotWidget::TargetRect() const
{
    const QRect bounds = contentsRect();

    if ( bounds.isEmpty() || m_image.width == 0 || m_image.height == 0 )
    {
        return {};
    }

    return bounds;
}

QRect PlotWidget::MapImageRect(
    const QRect& imageRect,
    const QRect& target ) const
{
    const i64 left = target.x() +
        static_cast< i64 >( imageRect.x() ) * target.width() /
        static_cast< i64 >( m_image.width );

    const i64 top = target.y() +
        static_cast< i64 >( imageRect.y() ) * target.height() /
        static_cast< i64 >( m_image.height );

    const i64 right = target.x() +
        static_cast< i64 >( imageRect.x() + imageRect.width() ) *
        target.width() /
        static_cast< i64 >( m_image.width );

    const i64 bottom = target.y() +
        static_cast< i64 >( imageRect.y() + imageRect.height() ) *
        target.height() /
        static_cast< i64 >( m_image.height );

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
    Point_s mapped;

    if ( !Math_MapPointToImage(
             position.x(), position.y(),
             target.x(), target.y(), target.width(), target.height(),
             m_image.width, m_image.height, mapped ) )
    {
        return CANTORDUST_NO_SOURCE;
    }

    if ( pixel != nullptr )
    {
        *pixel = QPoint(
            static_cast< int >( mapped.x ),
            static_cast< int >( mapped.y ) );
    }

    return m_image.SourceAt( mapped.x, mapped.y );
}

void PlotWidget::ClearHover()
{
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
        ClearHover();
        return;
    }

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

    if ( target.isEmpty() || m_image.argb.empty() )
    {
        return;
    }

    const QImage bitmap(
        reinterpret_cast< const uchar* >( m_image.argb.data() ),
        static_cast< int >( m_image.width ),
        static_cast< int >( m_image.height ),
        static_cast< int >( m_image.width * sizeof( u32 ) ),
        QImage::Format_ARGB32 );

    painter.setRenderHint( QPainter::SmoothPixmapTransform, false );
    painter.drawImage( target, bitmap );

    // Highlights are delivered as one-pixel-high runs (especially in the
    // Hilbert overview). Drawing a border around every run produces dense,
    // opaque stripes inside the selection. Union the runs in screen space,
    // then fill once and draw only the actual outside contour.
    QRegion selection;

    for ( const QRect& imageRect : m_highlights )
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

    if ( m_hoverPixel.x() >= 0 && m_hoverPixel.y() >= 0 )
    {
        const int x = target.x() + static_cast< int >(
            ( static_cast< i64 >( m_hoverPixel.x() ) * 2 + 1 ) *
            target.width() /
            static_cast< i64 >( m_image.width * 2 ) );

        const int y = target.y() + static_cast< int >(
            ( static_cast< i64 >( m_hoverPixel.y() ) * 2 + 1 ) *
            target.height() /
            static_cast< i64 >( m_image.height * 2 ) );

        painter.setPen( QPen( QColor::fromRgba(
            Palette_Get( m_palette ).selection ), 1 ) );
        painter.setBrush( Qt::NoBrush );
        painter.drawEllipse( QPoint( x, y ), 4, 4 );
    }
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
    if ( !m_onWheel || m_image.argb.empty() ||
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
            usize ordinal = 0;

            if ( m_viewMode == CDV_HILBERT )
            {
                ordinal = Math_HilbertIndex(
                    { static_cast< usize >( pixel.x() ),
                      static_cast< usize >( pixel.y() ) },
                    m_image.width );
            }
            else
            {
                ordinal = static_cast< usize >( pixel.y() ) *
                    m_image.width + static_cast< usize >( pixel.x() );
            }

            m_onWheel( source, ordinal, m_image.width * m_image.height,
                steps );
        }
    }

    event->accept();
}

} // namespace cantordust
