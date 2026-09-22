#include "experiment_studio/sensitivity_chart_widget.h"

#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>
#include <QtMath>

namespace sicnu::app::experiment_studio
{

SensitivityChartWidget::SensitivityChartWidget( QWidget *parent )
    : QWidget( parent )
{
    setMinimumHeight( 180 );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
}

void SensitivityChartWidget::setSeries( const QString &title, const QVector<ChartSeriesPoint> &points,
                                        bool showBand )
{
    m_title = title;
    m_points = points;
    m_showBand = showBand;
    update();
}

void SensitivityChartWidget::clearSeries()
{
    m_title.clear();
    m_points.clear();
    m_showBand = false;
    update();
}

void SensitivityChartWidget::paintEvent( QPaintEvent * )
{
    QPainter p( this );
    p.fillRect( rect(), palette().base() );
    p.setPen( palette().text().color() );
    p.drawText( QRect( 8, 4, width() - 16, 20 ), Qt::AlignLeft | Qt::AlignVCenter,
                m_title.isEmpty() ? tr( "Sensitivity (no curve)" ) : m_title );

    if ( m_points.size() < 1 )
        return;

    double minX = m_points.first().x;
    double maxX = m_points.first().x;
    double minY = m_points.first().y;
    double maxY = m_points.first().y;
    for ( const auto &pt : m_points )
    {
        minX = qMin( minX, pt.x );
        maxX = qMax( maxX, pt.x );
        minY = qMin( minY, m_showBand ? pt.yMin : pt.y );
        maxY = qMax( maxY, m_showBand ? pt.yMax : pt.y );
    }
    if ( qFuzzyCompare( minX, maxX ) )
    {
        minX -= 1.0;
        maxX += 1.0;
    }
    if ( qFuzzyCompare( minY, maxY ) )
    {
        minY -= 1.0;
        maxY += 1.0;
    }

    const QRect plot( 40, 28, width() - 56, height() - 48 );
    p.setPen( QPen( palette().mid().color(), 1 ) );
    p.drawRect( plot );

    auto mapX = [&]( double x ) {
        return plot.left() + ( x - minX ) / ( maxX - minX ) * plot.width();
    };
    auto mapY = [&]( double y ) {
        return plot.bottom() - ( y - minY ) / ( maxY - minY ) * plot.height();
    };

    if ( m_showBand && m_points.size() >= 2 )
    {
        QPainterPath band;
        band.moveTo( mapX( m_points.first().x ), mapY( m_points.first().yMax ) );
        for ( const auto &pt : m_points )
            band.lineTo( mapX( pt.x ), mapY( pt.yMax ) );
        for ( int i = m_points.size() - 1; i >= 0; --i )
            band.lineTo( mapX( m_points[i].x ), mapY( m_points[i].yMin ) );
        band.closeSubpath();
        QColor fill = palette().highlight().color();
        fill.setAlpha( 60 );
        p.fillPath( band, fill );
    }

    p.setPen( QPen( palette().highlight().color(), 2 ) );
    for ( int i = 1; i < m_points.size(); ++i )
    {
        p.drawLine( QPointF( mapX( m_points[i - 1].x ), mapY( m_points[i - 1].y ) ),
                    QPointF( mapX( m_points[i].x ), mapY( m_points[i].y ) ) );
    }
    p.setBrush( palette().highlight() );
    for ( const auto &pt : m_points )
        p.drawEllipse( QPointF( mapX( pt.x ), mapY( pt.y ) ), 3.5, 3.5 );
}

} // namespace sicnu::app::experiment_studio
