// src/app/widgets/histogram_widget.cpp
#include "histogram_widget.h"

#include <raster/qgsrasterlayer.h>
#include <raster/qgsrasterdataprovider.h>
#include <qgsproject.h>

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QFontMetrics>

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_error.h>

#include <cmath>
#include <limits>

// ── Constructor ──────────────────────────────────────────────────────────────

HistogramWidget::HistogramWidget( QWidget *parent )
    : QWidget( parent )
{
    setMinimumSize( 320, 220 );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );

    // Connect to layer removal to clear dangling pointer
    connect( QgsProject::instance(), &QgsProject::layerRemoved,
             this, [this]( const QString &layerId ) {
                 if ( m_rasterLayer && m_rasterLayer->id() == layerId ) {
                     m_rasterLayer = nullptr;
                     update();
                 }
             } );
}

// ── Public setters ───────────────────────────────────────────────────────────

void HistogramWidget::setRasterLayer( QgsRasterLayer *layer )
{
    m_rasterLayer = layer;
    if ( m_rasterLayer )
    {
        computeHistogram();
    }
    else
    {
        m_histogram.clear();
        m_stats.valid = false;
    }
    update();
}

void HistogramWidget::setBand( int bandNumber )
{
    m_band = bandNumber;
    if ( m_rasterLayer )
    {
        computeHistogram();
    }
    update();
}

// ── Histogram computation via GDAL ───────────────────────────────────────────

void HistogramWidget::computeHistogram()
{
    m_histogram.clear();
    m_binCount = 0;
    m_maxFrequency = 0.0;
    m_stats.valid = false;

    if ( !m_rasterLayer )
        return;

    // Open the raster source with GDAL
    const QString source = m_rasterLayer->source();
    GDALDatasetH dataset = GDALOpen( source.toUtf8().constData(), GA_ReadOnly );
    if ( !dataset )
        return;

    int bandCount = GDALGetRasterCount( dataset );
    if ( m_band < 1 || m_band > bandCount )
    {
        GDALClose( dataset );
        return;
    }

    GDALRasterBandH band = GDALGetRasterBand( dataset, m_band );

    // ---- Statistics (min, max, mean, stddev) --------------------------------
    double dfMin = 0, dfMax = 0, dfMean = 0, dfStdDev = 0;
    CPLErr statsErr = GDALGetRasterStatistics( band, TRUE, TRUE,
                                               &dfMin, &dfMax, &dfMean, &dfStdDev );
    if ( statsErr == CE_None )
    {
        m_stats.min = dfMin;
        m_stats.max = dfMax;
        m_stats.mean = dfMean;
        m_stats.stddev = dfStdDev;
        m_stats.valid = true;
    }

    // ---- Histogram ----------------------------------------------------------
    // Use 256 bins by default.  GDALGetDefaultHistogram may return a
    // different bin count; fall back to GDALGetHistogram with 256 bins.
    int nBuckets = 256;
    double dfHistMin = 0, dfHistMax = 0;
    int *panHistogram = nullptr;

    // Try the default histogram first (fast, may be cached in .aux.xml)
    CPLErr histErr = GDALGetDefaultHistogram( band, &dfHistMin, &dfHistMax,
                                              &nBuckets, &panHistogram, TRUE,
                                              nullptr, nullptr );

    // If default histogram unavailable, compute one ourselves
    if ( histErr != CE_None || !panHistogram )
    {
        // Use statistics range if available
        if ( m_stats.valid )
        {
            dfHistMin = m_stats.min;
            dfHistMax = m_stats.max;
        }
        else
        {
            // Approximate range from data type
            GDALDataType eType = GDALGetRasterDataType( band );
            switch ( eType )
            {
                case GDT_Byte:
                    dfHistMin = 0;
                    dfHistMax = 255;
                    break;
                case GDT_UInt16:
                    dfHistMin = 0;
                    dfHistMax = 65535;
                    break;
                case GDT_Int16:
                    dfHistMin = -32768;
                    dfHistMax = 32767;
                    break;
                default:
                    dfHistMin = 0;
                    dfHistMax = 255;
                    break;
            }
        }

        if ( dfHistMin >= dfHistMax )
            dfHistMax = dfHistMin + 1.0;

        panHistogram = static_cast<int *>( CPLCalloc( nBuckets, sizeof( int ) ) );
        histErr = GDALGetRasterHistogram( band, dfHistMin, dfHistMax,
                                          nBuckets, panHistogram, TRUE, FALSE,
                                          nullptr, nullptr );
    }

    if ( histErr == CE_None && panHistogram )
    {
        m_binCount = nBuckets;
        m_minValue = dfHistMin;
        m_maxValue = dfHistMax;
        m_histogram.resize( nBuckets );
        for ( int i = 0; i < nBuckets; ++i )
        {
            m_histogram[i] = static_cast<double>( panHistogram[i] );
            if ( m_histogram[i] > m_maxFrequency )
                m_maxFrequency = m_histogram[i];
        }
    }

    if ( panHistogram )
        CPLFree( panHistogram );

    GDALClose( dataset );
}

// ── Painting ─────────────────────────────────────────────────────────────────

void HistogramWidget::paintEvent( QPaintEvent *event )
{
    Q_UNUSED( event );

    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing, true );
    painter.fillRect( rect(), QColor( 250, 250, 252 ) );

    // Layout: chart area (top) + stats area (bottom)
    const int statsHeight = 60;
    const int margin = 50;
    const int bottomMargin = 30;

    QRect chartRect( margin, 10, width() - margin - 20, height() - statsHeight - 10 - bottomMargin );
    QRect statsRect( margin, height() - statsHeight - bottomMargin, width() - margin - 20, statsHeight );

    if ( m_histogram.isEmpty() )
    {
        painter.setPen( QColor( 160, 160, 160 ) );
        painter.setFont( QFont( "sans-serif", 11 ) );
        painter.drawText( rect(), Qt::AlignCenter,
                          m_rasterLayer
                              ? tr( "No histogram data available" )
                              : tr( "No raster layer selected" ) );
        return;
    }

    // Title
    painter.setPen( QColor( 50, 50, 50 ) );
    painter.setFont( QFont( "sans-serif", 10, QFont::Bold ) );
    QString title = tr( "Band %1 Histogram" ).arg( m_band );
    if ( m_rasterLayer )
        title = m_rasterLayer->name() + " — " + title;
    painter.drawText( QRect( margin, 0, width() - margin - 20, 18 ), Qt::AlignLeft | Qt::AlignVCenter, title );

    drawChart( painter, chartRect );
    drawStats( painter, statsRect );
}

void HistogramWidget::drawChart( QPainter &painter, const QRect &chartRect )
{
    // Background
    painter.fillRect( chartRect, QColor( 255, 255, 255 ) );
    painter.setPen( QPen( QColor( 200, 200, 200 ), 1 ) );
    painter.drawRect( chartRect.adjusted( 0, 0, -1, -1 ) );

    // Horizontal grid lines
    painter.setPen( QPen( QColor( 230, 230, 230 ), 1, Qt::DashLine ) );
    const int gridLines = 4;
    for ( int i = 1; i <= gridLines; ++i )
    {
        int y = chartRect.top() + chartRect.height() * i / ( gridLines + 1 );
        painter.drawLine( chartRect.left(), y, chartRect.right(), y );
    }

    drawBars( painter, chartRect );
    drawAxes( painter, chartRect );
}

void HistogramWidget::drawBars( QPainter &painter, const QRect &chartRect )
{
    if ( m_binCount <= 0 || m_maxFrequency <= 0.0 )
        return;

    const double barWidth = static_cast<double>( chartRect.width() ) / m_binCount;

    for ( int i = 0; i < m_binCount; ++i )
    {
        double fraction = m_histogram[i] / m_maxFrequency;
        int barHeight = static_cast<int>( fraction * chartRect.height() );
        if ( barHeight < 1 && m_histogram[i] > 0 )
            barHeight = 1;

        int x = chartRect.left() + static_cast<int>( i * barWidth );
        int w = std::max( 1, static_cast<int>( ( i + 1 ) * barWidth ) - static_cast<int>( i * barWidth ) - 1 );
        int y = chartRect.bottom() - barHeight;

        // Gradient-style bar colour
        QColor barColor( 66, 133, 244 );   // Google-blue
        barColor.setAlpha( 220 );
        painter.fillRect( x, y, w, barHeight, barColor );

        // Slight border on wider bars
        if ( w > 3 )
        {
            painter.setPen( QPen( QColor( 50, 110, 210 ), 1 ) );
            painter.drawRect( x, y, w, barHeight );
        }
    }
}

void HistogramWidget::drawAxes( QPainter &painter, const QRect &chartRect )
{
    painter.setPen( QColor( 80, 80, 80 ) );
    painter.setFont( QFont( "sans-serif", 8 ) );
    const QFontMetrics fm( painter.font() );

    // X axis labels (min, mid, max pixel values)
    const int xLabelY = chartRect.bottom() + 4;
    if ( m_binCount > 0 )
    {
        auto formatValue = []( double v ) -> QString
        {
            if ( std::abs( v ) < 1.0 && v != 0.0 )
                return QString::number( v, 'g', 3 );
            if ( std::abs( v ) >= 1000.0 )
                return QString::number( v, 'f', 0 );
            return QString::number( v, 'g', 5 );
        };

        // Min
        QString minLabel = formatValue( m_minValue );
        painter.drawText( chartRect.left() - fm.horizontalAdvance( minLabel ) / 2, xLabelY + fm.height(),
                          minLabel );

        // Max
        QString maxLabel = formatValue( m_maxValue );
        painter.drawText( chartRect.right() - fm.horizontalAdvance( maxLabel ) / 2, xLabelY + fm.height(),
                          maxLabel );

        // Mid
        double midValue = ( m_minValue + m_maxValue ) / 2.0;
        QString midLabel = formatValue( midValue );
        int midX = chartRect.left() + chartRect.width() / 2;
        painter.drawText( midX - fm.horizontalAdvance( midLabel ) / 2, xLabelY + fm.height(),
                          midLabel );
    }

    // X axis title
    painter.setFont( QFont( "sans-serif", 9 ) );
    const QFontMetrics fmTitle( painter.font() );
    QString xTitle = tr( "Pixel Value" );
    int xTitleX = chartRect.left() + ( chartRect.width() - fmTitle.horizontalAdvance( xTitle ) ) / 2;
    painter.drawText( xTitleX, chartRect.bottom() + 4 + fm.height() + fmTitle.height(), xTitle );

    // Y axis labels (frequency)
    painter.setFont( QFont( "sans-serif", 8 ) );
    if ( m_maxFrequency > 0 )
    {
        auto formatFreq = []( double v ) -> QString
        {
            if ( v >= 1e6 )
                return QString::number( v / 1e6, 'f', 1 ) + "M";
            if ( v >= 1e3 )
                return QString::number( v / 1e3, 'f', 1 ) + "K";
            return QString::number( v, 'f', 0 );
        };

        // Top (max freq)
        QString topLabel = formatFreq( m_maxFrequency );
        painter.drawText( chartRect.left() - fm.horizontalAdvance( topLabel ) - 6,
                          chartRect.top() + fm.ascent(), topLabel );

        // Bottom (0)
        painter.drawText( chartRect.left() - fm.horizontalAdvance( "0" ) - 6,
                          chartRect.bottom() + fm.ascent(), "0" );

        // Mid freq
        double midFreq = m_maxFrequency / 2.0;
        int midY = chartRect.top() + chartRect.height() / 2;
        QString midFLabel = formatFreq( midFreq );
        painter.drawText( chartRect.left() - fm.horizontalAdvance( midFLabel ) - 6,
                          midY + fm.ascent() / 2, midFLabel );
    }

    // Y axis title (rotated)
    painter.save();
    painter.setFont( QFont( "sans-serif", 9 ) );
    QString yTitle = tr( "Frequency" );
    int yTitleX = 10;
    int yTitleY = chartRect.top() + ( chartRect.height() + fmTitle.horizontalAdvance( yTitle ) ) / 2;
    painter.translate( yTitleX, yTitleY );
    painter.rotate( -90 );
    painter.drawText( 0, 0, yTitle );
    painter.restore();
}

void HistogramWidget::drawStats( QPainter &painter, const QRect &statsRect )
{
    if ( !m_stats.valid )
        return;

    painter.setPen( QColor( 80, 80, 80 ) );
    painter.setFont( QFont( "sans-serif", 9 ) );
    const QFontMetrics fm( painter.font() );

    // Light background for stats area
    painter.fillRect( statsRect, QColor( 245, 245, 248 ) );
    painter.setPen( QPen( QColor( 220, 220, 220 ), 1 ) );
    painter.drawRect( statsRect.adjusted( 0, 0, -1, -1 ) );

    painter.setPen( QColor( 60, 60, 60 ) );
    painter.setFont( QFont( "sans-serif", 9, QFont::Bold ) );
    painter.drawText( statsRect.left() + 8, statsRect.top() + fm.ascent() + 4,
                      tr( "Statistics" ) );

    painter.setFont( QFont( "sans-serif", 9 ) );

    auto fmt = []( double v ) -> QString
    {
        if ( std::abs( v ) < 1.0 && v != 0.0 )
            return QString::number( v, 'g', 4 );
        if ( std::abs( v ) >= 1000.0 )
            return QString::number( v, 'f', 1 );
        return QString::number( v, 'g', 6 );
    };

    // Four stat columns
    int colWidth = statsRect.width() / 4;
    int y = statsRect.top() + fm.ascent() + 4 + fm.height() + 4;

    struct StatLabel
    {
        QString label;
        QString value;
    };

    StatLabel labels[4] =
    {
        { tr( "Min" ), fmt( m_stats.min ) },
        { tr( "Max" ), fmt( m_stats.max ) },
        { tr( "Mean" ), fmt( m_stats.mean ) },
        { tr( "StdDev" ), fmt( m_stats.stddev ) },
    };

    for ( int i = 0; i < 4; ++i )
    {
        int x = statsRect.left() + 8 + i * colWidth;

        painter.setPen( QColor( 120, 120, 120 ) );
        painter.setFont( QFont( "sans-serif", 8 ) );
        painter.drawText( x, y, labels[i].label + ":" );

        painter.setPen( QColor( 40, 40, 40 ) );
        painter.setFont( QFont( "sans-serif", 9, QFont::Bold ) );
        int labelW = painter.fontMetrics().horizontalAdvance( labels[i].label + ": " );
        painter.drawText( x + labelW, y, labels[i].value );
    }
}
