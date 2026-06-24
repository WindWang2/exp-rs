// src/app/widgets/histogram_widget.h
#pragma once

#include <QWidget>
#include <QVector>
#include <QString>

class QgsRasterLayer;
typedef void *GDALDatasetH;

/**
 * \brief Raster band histogram widget.
 *
 * Displays a histogram of pixel values for a single band of a
 * QgsRasterLayer using GDAL C API to read histogram and statistics
 * data.  Rendering is done with QPainter so no QtCharts dependency
 * is required.
 */
class HistogramWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HistogramWidget( QWidget *parent = nullptr );
    ~HistogramWidget() override;

    /**
     * Set the raster layer to analyse.
     * The widget will be redrawn when setBand() is called.
     */
    void setRasterLayer( QgsRasterLayer *layer );

    /**
     * Set which band number (1-based) to display.
     * Triggers recomputation of histogram data and repaint.
     */
    void setBand( int bandNumber );

    QgsRasterLayer *rasterLayer() const { return m_rasterLayer; }
    int band() const { return m_band; }

    QSize minimumSizeHint() const override { return QSize( 320, 220 ); }
    QSize sizeHint() const override { return QSize( 500, 350 ); }

protected:
    void paintEvent( QPaintEvent *event ) override;

private:
    struct BandStats
    {
        double min = 0.0;
        double max = 0.0;
        double mean = 0.0;
        double stddev = 0.0;
        bool valid = false;
    };

    void computeHistogram();
    void drawChart( QPainter &painter, const QRect &chartRect );
    void drawAxes( QPainter &painter, const QRect &chartRect );
    void drawBars( QPainter &painter, const QRect &chartRect );
    void drawStats( QPainter &painter, const QRect &statsRect );
    void closeDataset();

    QgsRasterLayer *m_rasterLayer = nullptr;
    int m_band = 1;

    // Cached GDAL dataset handle
    GDALDatasetH m_cachedDataset = nullptr;
    QString m_cachedSource;

    // Histogram bins: index -> count
    QVector<double> m_histogram;
    int m_binCount = 0;
    double m_minValue = 0.0;
    double m_maxValue = 0.0;

    BandStats m_stats;

    // Cached max frequency for normalisation
    double m_maxFrequency = 0.0;
};
