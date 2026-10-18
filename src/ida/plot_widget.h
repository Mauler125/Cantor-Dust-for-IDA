#ifndef IDA_PLOT_WIDGET_H
#define IDA_PLOT_WIDGET_H


#include "cantordust/scalar_types.h"
#include "cantordust/types.h"
#include "cantordust/palette.h"
#include "ida/navigation_gate.h"
#include "math/anomaly.h"
#include "math/raster_fit.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QWheelEvent>
#include <QtWidgets>

#include <cstddef>
#include <functional>
#include <vector>

namespace cantordust
{

class PlotWidget final : public QWidget
{
public:
    explicit PlotWidget( QWidget* parent = nullptr );

    void SetImage( Image_s image );
    void SetPalette( Palette_e palette );
    void SetHighlights( std::vector< QRect > highlights );
    // Anomaly positions are source-image traversal ordinals, not addresses.
    void SetAnomalyMarkers( std::vector< AnomalyMarker_s > markers );
    void SetViewMode( View_e mode );
    void SetOnWheel(
        std::function< void( usize, usize,
            usize, int ) > callback );

    void SetOnNavigate( std::function< void( usize ) > callback );
    void SetOnHover( std::function< void( usize ) > callback );
    void SetAddressFormatter( std::function< QString( usize ) > callback );

protected:
    void paintEvent( QPaintEvent* event ) override;
    void resizeEvent( QResizeEvent* event ) override;
    void mousePressEvent( QMouseEvent* event ) override;
    void mouseMoveEvent( QMouseEvent* event ) override;
    void mouseReleaseEvent( QMouseEvent* event ) override;
    void leaveEvent( QEvent* event ) override;
    void wheelEvent( QWheelEvent* event ) override;

private:
    const Image_s& DisplayImage() const;
    void RebuildDisplayRaster();
    void RebuildDisplayHighlights();
    void RebuildAnomalyMarkers();
    QRect TargetRect() const;
    QRect MapImageRect( const QRect& imageRect, const QRect& target ) const;
    usize SourceAt( const QPoint& position, QPoint* pixel ) const;
    void UpdateHover( const QPoint& position );
    void ClearHover();
    void NavigateAt( const QPoint& position, bool force );
    void ShowBucketInspection( const QPoint& position, const QPoint& pixel );

    Palette_e m_palette = CDP_REFERENCE;
    Image_s m_image;
    FittedRaster_s m_fittedRaster;
    std::vector< QRect > m_highlights;
    std::vector< QRect > m_displayHighlights;
    struct DisplayAnomalyMarker_s
    {
        QPoint pixel;
        AnomalyKind_e kind = AnomalyKind_e::HighEntropy;
    };
    std::vector< AnomalyMarker_s > m_anomalyMarkers;
    std::vector< DisplayAnomalyMarker_s > m_displayAnomalyMarkers;
    View_e m_viewMode = CDV_HILBERT;
    std::function< void( usize, usize,
        usize, int ) > m_onWheel;

    int m_wheelRemainder = 0;
    QPoint m_hoverPixel{ -1, -1 };
    usize m_hoverSource = CANTORDUST_NO_SOURCE;
    usize m_inspectedBucket = CANTORDUST_NO_SOURCE;
    std::function< QString( usize ) > m_addressFormatter;
    std::function< void( usize ) > m_onNavigate;
    std::function< void( usize ) > m_onHover;
    NavigationGate m_navigationGate;
    QElapsedTimer m_navigationClock;
    QTimer m_resizeTimer;
    bool m_dragging = false;
};

} // namespace cantordust

#endif // IDA_PLOT_WIDGET_H
