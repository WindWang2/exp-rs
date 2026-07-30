#include <filesystem>
#include "histogram_widget.h"
#include "core/sicnu_logging.h"

#include <raster/qgsrasterlayer.h>
#include <raster/qgsrasterdataprovider.h>
#include <qgsproject.h>

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_error.h>

#include <cmath>
#include <limits>
#include <algorithm>

HistogramWidget::HistogramWidget( QWidget *parent )
    : QWidget( parent )
{
    setMinimumSize( 340, 240 );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    setMouseTracking( true );

    connect( QgsProject::instance(), &QgsProject::layerRemoved,
             this, [this]( const QString &layerId ) {
                 if ( m_rasterLayer && m_rasterLayer->id() == layerId ) {
                     m_rasterLayer = nullptr;
                     closeDataset();
                     update();
                 }
             } );
}

HistogramWidget::~HistogramWidget()
{
    closeDataset();
}

void HistogramWidget::closeDataset()
{
    if ( m_cachedDataset ) {
        GDALClose( m_cachedDataset );
        m_cachedDataset = nullptr;
        m_cachedSource.clear();
    }
}

void HistogramWidget::setRasterLayer( QgsRasterLayer *layer )
{
    m_rasterLayer = layer;
    if ( m_rasterLayer )
    {
        if ( m_rasterLayer->bandCount() >= 3 )
        {
            m_redBand = 1;
            m_greenBand = 2;
            m_blueBand = 3;
        }
        computeHistograms();
    }
    else
    {
        m_singleBandData.valid = false;
        m_redData.valid = false;
        m_greenData.valid = false;
        m_blueData.valid = false;
    }
    update();
}

void HistogramWidget::setBand( int bandNumber )
{
    m_band = bandNumber;
    if ( m_rasterLayer )
    {
        computeHistograms();
    }
    update();
}

void HistogramWidget::setChannelMode( ChannelMode mode )
{
    if ( m_channelMode == mode )
        return;
    m_channelMode = mode;
    if ( m_rasterLayer )
    {
        computeHistograms();
    }
    resetPiecewisePoints();
    update();
}

void HistogramWidget::setRgbBands( int rBand, int gBand, int bBand )
{
    m_redBand = rBand;
    m_greenBand = gBand;
    m_blueBand = bBand;
    if ( m_rasterLayer )
    {
        computeHistograms();
    }
    update();
}

void HistogramWidget::setCutoffs( double black, double white, double gamma )
{
    m_blackCutoff = black;
    m_whiteCutoff = white;
    m_gamma = std::clamp( gamma, 0.1, 10.0 );
    update();
}

void HistogramWidget::setEnablePiecewise( bool enable )
{
    m_enablePiecewise = enable;
    if ( m_enablePiecewise && m_piecewisePoints.isEmpty() )
        resetPiecewisePoints();
    update();
}

void HistogramWidget::setPiecewisePoints( const QVector<QPointF> &points )
{
    m_piecewisePoints = points;
    std::sort( m_piecewisePoints.begin(), m_piecewisePoints.end(), []( const QPointF &a, const QPointF &b ) {
        return a.x() < b.x();
    } );
    if ( m_piecewisePoints.size() >= 2 )
    {
        const BandData &data = activeBandData();
        m_piecewisePoints.front() = QPointF( data.minVal, 0.0 );
        m_piecewisePoints.back() = QPointF( data.maxVal, 255.0 );
    }
    update();
}

void HistogramWidget::resetPiecewisePoints()
{
    const BandData &data = activeBandData();
    m_piecewisePoints = {
        QPointF( data.minVal, 0.0 ),
        QPointF( data.maxVal, 255.0 )
    };
    m_selectedPiecewiseIndex = -1;
    update();
}

const HistogramWidget::BandData &HistogramWidget::activeBandData() const
{
    switch ( m_channelMode )
    {
      case ChannelMode::Red: return m_redData;
      case ChannelMode::Green: return m_greenData;
      case ChannelMode::Blue: return m_blueData;
      case ChannelMode::MasterRGB:
      case ChannelMode::SingleBand:
        return m_singleBandData;
    }
    return m_singleBandData;
}

void HistogramWidget::computeSingleBandHistogram( int bandNum, BandData &data )
{
    data.valid = false;
    if ( !m_rasterLayer )
        return;

    const QString source = m_rasterLayer->source();
    if ( !m_cachedDataset || m_cachedSource != source )
    {
        closeDataset();
        m_cachedDataset = GDALOpen( source.toUtf8().constData(), GA_ReadOnly );
        m_cachedSource = source;
    }

    if ( !m_cachedDataset )
        return;

    GDALRasterBandH hBand = GDALGetRasterBand( m_cachedDataset, bandNum );
    if ( !hBand )
        return;

    int bGotMin = 0, bGotMax = 0;
    double dfMin = GDALGetRasterMinimum( hBand, &bGotMin );
    double dfMax = GDALGetRasterMaximum( hBand, &bGotMax );

    if ( !bGotMin || !bGotMax || dfMax <= dfMin )
    {
        double adfMinMax[2];
        if ( GDALComputeRasterMinMax( hBand, TRUE, adfMinMax ) == CE_None && adfMinMax[1] > adfMinMax[0] )
        {
            dfMin = adfMinMax[0];
            dfMax = adfMinMax[1];
        }
        else
        {
            dfMin = 0.0;
            dfMax = 255.0;
        }
    }

    data.minVal = dfMin;
    data.maxVal = dfMax;

    double dfMean = 0.0, dfStdDev = 0.0;
    if ( GDALGetRasterStatistics( hBand, FALSE, TRUE, &dfMin, &dfMax, &dfMean, &dfStdDev ) == CE_None )
    {
        data.mean = dfMean;
        data.stddev = dfStdDev;
    }

    const int nBuckets = 256;
    GUIntBig panHistogram[256];
    CPLErr err = GDALGetRasterHistogramEx( hBand, dfMin, dfMax, nBuckets, panHistogram, FALSE, TRUE, nullptr, nullptr );

    data.histogram.resize( nBuckets );
    data.maxFreq = 0.0;
    data.binCount = nBuckets;

    if ( err == CE_None )
    {
        for ( int i = 0; i < nBuckets; ++i )
        {
            data.histogram[i] = static_cast<double>( panHistogram[i] );
            if ( data.histogram[i] > data.maxFreq )
                data.maxFreq = data.histogram[i];
        }
        data.valid = true;
    }
}

void HistogramWidget::computeHistograms()
{
    if ( !m_rasterLayer )
        return;

    computeSingleBandHistogram( m_band, m_singleBandData );

    if ( m_rasterLayer->bandCount() >= 3 )
    {
        computeSingleBandHistogram( m_redBand, m_redData );
        computeSingleBandHistogram( m_greenBand, m_greenData );
        computeSingleBandHistogram( m_blueBand, m_blueData );
    }

    if ( m_blackCutoff == 0.0 && m_whiteCutoff == 255.0 )
    {
        m_blackCutoff = m_singleBandData.minVal;
        m_whiteCutoff = m_singleBandData.maxVal;
    }

    if ( m_piecewisePoints.isEmpty() )
        resetPiecewisePoints();
}

QRect HistogramWidget::getChartRect() const
{
    const int margin = 45;
    const int bottomMargin = 40;
    const int statsHeight = 25;
    return QRect( margin, 25, width() - margin - 20, height() - statsHeight - 25 - bottomMargin );
}

double HistogramWidget::valToX( double val, const QRect &chartRect ) const
{
    const BandData &data = activeBandData();
    double minV = data.minVal;
    double maxV = data.maxVal;
    if ( std::abs( maxV - minV ) < 1e-6 ) return chartRect.left();
    double ratio = ( val - minV ) / ( maxV - minV );
    return chartRect.left() + ratio * chartRect.width();
}

double HistogramWidget::xToVal( int x, const QRect &chartRect ) const
{
    const BandData &data = activeBandData();
    double minV = data.minVal;
    double maxV = data.maxVal;
    double ratio = static_cast<double>( x - chartRect.left() ) / chartRect.width();
    ratio = std::clamp( ratio, 0.0, 1.0 );
    return minV + ratio * ( maxV - minV );
}

double HistogramWidget::displayToY( double displayVal, const QRect &chartRect ) const
{
    double ratio = displayVal / 255.0;
    return chartRect.bottom() - ratio * chartRect.height();
}

double HistogramWidget::yToDisplay( int y, const QRect &chartRect ) const
{
    double ratio = static_cast<double>( chartRect.bottom() - y ) / chartRect.height();
    ratio = std::clamp( ratio, 0.0, 1.0 );
    return ratio * 255.0;
}

void HistogramWidget::paintEvent( QPaintEvent * )
{
    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing );

    QRect chartRect = getChartRect();
    QRect statsRect( chartRect.left(), height() - 25, chartRect.width(), 20 );

    if ( !m_singleBandData.valid && !m_redData.valid )
    {
        painter.setPen( QColor( 160, 160, 160 ) );
        painter.setFont( QFont( "sans-serif", 11 ) );
        painter.drawText( rect(), Qt::AlignCenter,
                          m_rasterLayer ? tr( "No histogram data available" ) : tr( "No raster layer selected" ) );
        return;
    }

    // Title & Real Data Bounds Indicator
    painter.setPen( QColor( 220, 220, 220 ) );
    painter.setFont( QFont( "sans-serif", 10, QFont::Bold ) );
    QString title = ( m_channelMode == ChannelMode::MasterRGB ) ? tr( "RGB Combined Histogram" ) : tr( "Band %1 Histogram" ).arg( m_band );
    if ( m_rasterLayer )
        title = m_rasterLayer->name() + " — " + title;
    painter.drawText( QRect( chartRect.left(), 4, chartRect.width(), 20 ), Qt::AlignLeft | Qt::AlignVCenter, title );

    drawChart( painter, chartRect );

    if ( m_enablePiecewise )
    {
        drawPiecewiseCurve( painter, chartRect );
    }
    else
    {
        drawHandles( painter, chartRect );
    }

    drawStats( painter, statsRect );
}

void HistogramWidget::drawChart( QPainter &painter, const QRect &chartRect )
{
    painter.fillRect( chartRect, QColor( 28, 28, 30 ) );
    painter.setPen( QPen( QColor( 70, 70, 75 ), 1 ) );
    painter.drawRect( chartRect.adjusted( 0, 0, -1, -1 ) );

    painter.setPen( QPen( QColor( 50, 50, 55 ), 1, Qt::DashLine ) );
    for ( int i = 1; i <= 4; ++i )
    {
        int y = chartRect.top() + chartRect.height() * i / 5;
        painter.drawLine( chartRect.left(), y, chartRect.right(), y );
    }

    drawBars( painter, chartRect );
    drawAxes( painter, chartRect );
}

void HistogramWidget::drawBars( QPainter &painter, const QRect &chartRect )
{
    if ( m_channelMode == ChannelMode::MasterRGB && m_redData.valid && m_greenData.valid && m_blueData.valid )
    {
        auto drawChannelBars = [&]( const BandData &data, const QColor &color ) {
            if ( data.binCount <= 0 || data.maxFreq <= 0.0 ) return;
            const double barWidth = static_cast<double>( chartRect.width() ) / data.binCount;
            for ( int i = 0; i < data.binCount; ++i )
            {
                double fraction = data.histogram[i] / data.maxFreq;
                int barHeight = static_cast<int>( fraction * chartRect.height() );
                if ( barHeight < 1 && data.histogram[i] > 0 ) barHeight = 1;
                int x = chartRect.left() + static_cast<int>( i * barWidth );
                int w = std::max( 1, static_cast<int>( ( i + 1 ) * barWidth ) - static_cast<int>( i * barWidth ) );
                int y = chartRect.bottom() - barHeight;

                painter.fillRect( x, y, w, barHeight, color );
            }
        };

        drawChannelBars( m_redData, QColor( 255, 77, 77, 120 ) );
        drawChannelBars( m_greenData, QColor( 77, 255, 77, 120 ) );
        drawChannelBars( m_blueData, QColor( 77, 148, 255, 120 ) );
    }
    else
    {
        const BandData &data = ( m_channelMode == ChannelMode::Red ) ? m_redData :
                               ( m_channelMode == ChannelMode::Green ) ? m_greenData :
                               ( m_channelMode == ChannelMode::Blue ) ? m_blueData : m_singleBandData;

        if ( data.binCount <= 0 || data.maxFreq <= 0.0 ) return;
        const double barWidth = static_cast<double>( chartRect.width() ) / data.binCount;

        QColor barColor( 100, 180, 255 );
        if ( m_channelMode == ChannelMode::Red ) barColor = QColor( 255, 90, 90 );
        else if ( m_channelMode == ChannelMode::Green ) barColor = QColor( 90, 220, 90 );
        else if ( m_channelMode == ChannelMode::Blue ) barColor = QColor( 90, 150, 255 );

        barColor.setAlpha( 210 );

        for ( int i = 0; i < data.binCount; ++i )
        {
            double fraction = data.histogram[i] / data.maxFreq;
            int barHeight = static_cast<int>( fraction * chartRect.height() );
            if ( barHeight < 1 && data.histogram[i] > 0 ) barHeight = 1;
            int x = chartRect.left() + static_cast<int>( i * barWidth );
            int w = std::max( 1, static_cast<int>( ( i + 1 ) * barWidth ) - static_cast<int>( i * barWidth ) );
            int y = chartRect.bottom() - barHeight;

            painter.fillRect( x, y, w, barHeight, barColor );
        }
    }
}

void HistogramWidget::drawAxes( QPainter &painter, const QRect &chartRect )
{
    painter.setPen( QColor( 170, 170, 170 ) );
    painter.setFont( QFont( "sans-serif", 8 ) );

    const BandData &data = activeBandData();
    const double minV = data.minVal;
    const double maxV = data.maxVal;

    // Real Physical Data Bounds X Axis Labels
    int yLabel = chartRect.bottom() + 18;
    painter.drawText( chartRect.left(), yLabel, QString::number( minV, 'g', 5 ) );
    painter.drawText( chartRect.right() - 50, yLabel, QString::number( maxV, 'g', 5 ) );
}

void HistogramWidget::drawPiecewiseCurve( QPainter &painter, const QRect &chartRect )
{
    if ( m_piecewisePoints.size() < 2 ) return;

    QPen linePen( QColor( 255, 193, 7 ), 2 ); // Bright Amber Curve Line
    painter.setPen( linePen );

    for ( int i = 0; i < m_piecewisePoints.size() - 1; ++i )
    {
        double x1 = valToX( m_piecewisePoints[i].x(), chartRect );
        double y1 = displayToY( m_piecewisePoints[i].y(), chartRect );
        double x2 = valToX( m_piecewisePoints[i+1].x(), chartRect );
        double y2 = displayToY( m_piecewisePoints[i+1].y(), chartRect );
        painter.drawLine( QPointF( x1, y1 ), QPointF( x2, y2 ) );
    }

    // Draw control node circles (〇)
    for ( int i = 0; i < m_piecewisePoints.size(); ++i )
    {
        double px = valToX( m_piecewisePoints[i].x(), chartRect );
        double py = displayToY( m_piecewisePoints[i].y(), chartRect );

        QColor nodeColor = ( i == m_selectedPiecewiseIndex ) ? QColor( 255, 87, 34 ) : QColor( 255, 235, 59 );
        painter.setBrush( QBrush( nodeColor ) );
        painter.setPen( QPen( QColor( 0, 0, 0 ), 1.5 ) );
        painter.drawEllipse( QPointF( px, py ), 6, 6 );
    }
}

void HistogramWidget::drawHandles( QPainter &painter, const QRect &chartRect )
{
    int blackX = static_cast<int>( valToX( m_blackCutoff, chartRect ) );
    int whiteX = static_cast<int>( valToX( m_whiteCutoff, chartRect ) );

    double gammaRatio = std::pow( 0.5, 1.0 / m_gamma );
    double gammaVal = m_blackCutoff + gammaRatio * ( m_whiteCutoff - m_blackCutoff );
    int gammaX = static_cast<int>( valToX( gammaVal, chartRect ) );

    int handleY = chartRect.bottom() + 2;

    QPolygon blackTriangle;
    blackTriangle << QPoint( blackX, handleY )
                  << QPoint( blackX - 7, handleY + 12 )
                  << QPoint( blackX + 7, handleY + 12 );
    painter.setBrush( QBrush( QColor( 0, 0, 0 ) ) );
    painter.setPen( QPen( QColor( 255, 255, 255 ), 1 ) );
    painter.drawPolygon( blackTriangle );

    QPolygon gammaTriangle;
    gammaTriangle << QPoint( gammaX, handleY )
                  << QPoint( gammaX - 7, handleY + 12 )
                  << QPoint( gammaX + 7, handleY + 12 );
    painter.setBrush( QBrush( QColor( 140, 140, 140 ) ) );
    painter.setPen( QPen( QColor( 255, 255, 255 ), 1 ) );
    painter.drawPolygon( gammaTriangle );

    QPolygon whiteTriangle;
    whiteTriangle << QPoint( whiteX, handleY )
                  << QPoint( whiteX - 7, handleY + 12 )
                  << QPoint( whiteX + 7, handleY + 12 );
    painter.setBrush( QBrush( QColor( 255, 255, 255 ) ) );
    painter.setPen( QPen( QColor( 0, 0, 0 ), 1 ) );
    painter.drawPolygon( whiteTriangle );
}

void HistogramWidget::drawStats( QPainter &painter, const QRect &statsRect )
{
    const BandData &data = activeBandData();
    painter.setPen( QColor( 180, 180, 185 ) );
    painter.setFont( QFont( "sans-serif", 8 ) );
    QString statsStr = tr( "Real Data Range: [%1 ~ %2] | Mean: %3" )
                           .arg( data.minVal, 0, 'g', 5 )
                           .arg( data.maxVal, 0, 'g', 5 )
                           .arg( data.mean, 0, 'g', 5 );
    painter.drawText( statsRect, Qt::AlignLeft | Qt::AlignVCenter, statsStr );
}

int HistogramWidget::hitTestPiecewisePoint( const QPoint &pos ) const
{
    QRect chartRect = getChartRect();
    for ( int i = 0; i < m_piecewisePoints.size(); ++i )
    {
        double px = valToX( m_piecewisePoints[i].x(), chartRect );
        double py = displayToY( m_piecewisePoints[i].y(), chartRect );
        double distSq = ( pos.x() - px ) * ( pos.x() - px ) + ( pos.y() - py ) * ( pos.y() - py );
        if ( distSq <= 64.0 ) // 8px radius
        {
            return i;
        }
    }
    return -1;
}

HistogramWidget::ActiveHandle HistogramWidget::hitTestHandle( const QPoint &pos ) const
{
    if ( m_enablePiecewise )
    {
        int idx = hitTestPiecewisePoint( pos );
        if ( idx >= 0 ) return ActiveHandle::PiecewisePoint;
        return ActiveHandle::None;
    }

    QRect chartRect = getChartRect();
    int handleY = chartRect.bottom() + 2;

    int blackX = static_cast<int>( valToX( m_blackCutoff, chartRect ) );
    int whiteX = static_cast<int>( valToX( m_whiteCutoff, chartRect ) );

    double gammaRatio = std::pow( 0.5, 1.0 / m_gamma );
    double gammaVal = m_blackCutoff + gammaRatio * ( m_whiteCutoff - m_blackCutoff );
    int gammaX = static_cast<int>( valToX( gammaVal, chartRect ) );

    if ( pos.y() >= handleY - 2 && pos.y() <= handleY + 16 )
    {
        if ( std::abs( pos.x() - blackX ) <= 8 ) return ActiveHandle::Black;
        if ( std::abs( pos.x() - gammaX ) <= 8 ) return ActiveHandle::Gamma;
        if ( std::abs( pos.x() - whiteX ) <= 8 ) return ActiveHandle::White;
    }
    return ActiveHandle::None;
}

void HistogramWidget::mousePressEvent( QMouseEvent *event )
{
    QRect chartRect = getChartRect();

    if ( m_enablePiecewise )
    {
        int idx = hitTestPiecewisePoint( event->pos() );
        if ( event->button() == Qt::LeftButton )
        {
            if ( idx >= 0 )
            {
                m_selectedPiecewiseIndex = idx;
                m_isDragging = true;
                m_activeHandle = ActiveHandle::PiecewisePoint;
                update();
            }
        }
        else if ( event->button() == Qt::RightButton )
        {
            // Delete intermediate point on right click
            if ( idx > 0 && idx < m_piecewisePoints.size() - 1 )
            {
                m_piecewisePoints.removeAt( idx );
                m_selectedPiecewiseIndex = -1;
                update();
                emit piecewisePointsChanged( m_piecewisePoints );
            }
        }
        return;
    }

    if ( event->button() == Qt::LeftButton )
    {
        m_activeHandle = hitTestHandle( event->pos() );
        if ( m_activeHandle != ActiveHandle::None )
        {
            m_isDragging = true;
            setCursor( Qt::SizeHorCursor );
        }
    }
}

void HistogramWidget::mouseDoubleClickEvent( QMouseEvent *event )
{
    if ( m_enablePiecewise && event->button() == Qt::LeftButton )
    {
        QRect chartRect = getChartRect();
        if ( chartRect.contains( event->pos() ) )
        {
            double newX = xToVal( event->pos().x(), chartRect );
            double newY = yToDisplay( event->pos().y(), chartRect );

            m_piecewisePoints.append( QPointF( newX, newY ) );
            std::sort( m_piecewisePoints.begin(), m_piecewisePoints.end(), []( const QPointF &a, const QPointF &b ) {
                return a.x() < b.x();
            } );

            update();
            emit piecewisePointsChanged( m_piecewisePoints );
        }
    }
}

void HistogramWidget::mouseMoveEvent( QMouseEvent *event )
{
    QRect chartRect = getChartRect();

    if ( m_enablePiecewise && m_isDragging && m_selectedPiecewiseIndex >= 0 )
    {
        const BandData &data = activeBandData();
        double valX = xToVal( event->pos().x(), chartRect );
        double valY = yToDisplay( event->pos().y(), chartRect );

        // Constrain X between neighboring points
        if ( m_selectedPiecewiseIndex == 0 )
        {
            valX = data.minVal;
            valY = 0.0;
        }
        else if ( m_selectedPiecewiseIndex == m_piecewisePoints.size() - 1 )
        {
            valX = data.maxVal;
            valY = 255.0;
        }
        else
        {
            double minBound = m_piecewisePoints[m_selectedPiecewiseIndex - 1].x() + 0.001;
            double maxBound = m_piecewisePoints[m_selectedPiecewiseIndex + 1].x() - 0.001;
            valX = std::clamp( valX, minBound, maxBound );
        }

        m_piecewisePoints[m_selectedPiecewiseIndex] = QPointF( valX, valY );
        // Preview only while dragging — commit to the layer on mouse release.
        // Emitting on every move re-sets the live renderer and races map jobs.
        update();
        return;
    }

    if ( m_isDragging && m_activeHandle != ActiveHandle::None )
    {
        double val = xToVal( event->pos().x(), chartRect );
        if ( m_activeHandle == ActiveHandle::Black )
        {
            m_blackCutoff = std::min( val, m_whiteCutoff - 0.001 );
        }
        else if ( m_activeHandle == ActiveHandle::White )
        {
            m_whiteCutoff = std::max( val, m_blackCutoff + 0.001 );
        }
        else if ( m_activeHandle == ActiveHandle::Gamma )
        {
            double ratio = ( val - m_blackCutoff ) / std::max( 0.001, m_whiteCutoff - m_blackCutoff );
            ratio = std::clamp( ratio, 0.05, 0.95 );
            m_gamma = std::clamp( std::log( 0.5 ) / std::log( ratio ), 0.1, 10.0 );
        }
        // Preview only; commit on release.
        update();
    }
    else
    {
        ActiveHandle h = hitTestHandle( event->pos() );
        if ( h != ActiveHandle::None )
            setCursor( Qt::SizeHorCursor );
        else
            unsetCursor();
    }
}

void HistogramWidget::mouseReleaseEvent( QMouseEvent *event )
{
    if ( event->button() == Qt::LeftButton )
    {
        if ( m_isDragging )
        {
            if ( m_enablePiecewise && m_selectedPiecewiseIndex >= 0 )
                emit piecewisePointsChanged( m_piecewisePoints );
            else if ( m_activeHandle != ActiveHandle::None )
                emit cutoffsChanged( m_blackCutoff, m_whiteCutoff, m_gamma );
        }
        m_isDragging = false;
        m_activeHandle = ActiveHandle::None;
        unsetCursor();
    }
}
