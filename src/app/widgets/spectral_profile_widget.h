// src/app/widgets/spectral_profile_widget.h
#pragma once

#include <QWidget>
#include <QVector>
#include <QString>

#include <qgspointxy.h>

class QgsRasterLayer;

// Opaque GDAL handle
typedef void *GDALDatasetH;

/**
 * \brief Spectral profile (pixel value across all bands) widget.
 *
 * Displays a line chart of pixel values at a given map coordinate
 * across all bands of a QgsRasterLayer.  Band descriptions are used
 * as X-axis labels when available.
 *
 * Rendering is done entirely with QPainter so no QtCharts dependency
 * is required.
 */
class SpectralProfileWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SpectralProfileWidget( QWidget *parent = nullptr );
    ~SpectralProfileWidget() override;

    /**
     * Extract the pixel values across all bands at the given map
     * coordinate from \a layer and update the display.
     *
     * The point must be in the same CRS as the raster layer.
     */
    void setProfile( const QgsPointXY &point, QgsRasterLayer *layer );

    /**
     * Clear the profile data and reset the widget to its empty state.
     */
    void clear();

    QSize minimumSizeHint() const override { return QSize( 320, 220 ); }
    QSize sizeHint() const override { return QSize( 500, 350 ); }

protected:
    void paintEvent( QPaintEvent *event ) override;

private:
    void extractProfile( const QgsPointXY &point, QgsRasterLayer *layer );
    void drawChart( QPainter &painter, const QRect &chartRect );
    void drawAxes( QPainter &painter, const QRect &chartRect );
    void drawLine( QPainter &painter, const QRect &chartRect );
    void closeDataset();

    QgsPointXY m_point;
    QgsRasterLayer *m_rasterLayer = nullptr;

    // Cached GDAL dataset handle — avoids reopening on every click
    GDALDatasetH m_cachedDataset = nullptr;
    QString m_cachedSource;

    // Per-band data
    QVector<double> m_values;          // pixel value per band
    QVector<QString> m_bandLabels;     // label per band (description or "Band N")
    QString m_layerName;
    bool m_hasData = false;

    // Computed ranges
    double m_minValue = 0.0;
    double m_maxValue = 0.0;
};
