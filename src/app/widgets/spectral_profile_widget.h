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
     * Display a precomputed spectrum (e.g. an ROI mean spectrum from the
     * SpectralRoiProfile kernel) in the same chart as a point profile.
     * Wavelengths/labels shorter than values are dropped; an empty values
     * vector clears the widget.
     */
    void setSpectrum( const QVector<double> &values,
                      const QVector<double> &wavelengths = {},
                      const QVector<QString> &labels = {},
                      const QString &layerName = QString() );

    /**
     * Clear the profile data and reset the widget to its empty state.
     */
    void clear();

    /// Whether a valid profile (>= 1 readable band) is currently displayed.
    bool hasData() const { return m_hasData; }

    /// Pixel value per band of the current profile (NaN for failed band reads).
    QVector<double> values() const { return m_values; }

    /// Per-band label ("B2", "Band 1", ...) used as the chart X axis.
    QVector<QString> bandLabels() const { return m_bandLabels; }

    /// Per-band center wavelength (nm) from the raster's WAVELENGTH band
    /// metadata (0.0 for bands without one; empty when the raster has none).
    /// When populated, the chart X axis is wavelength-scaled.
    QVector<double> wavelengths() const { return m_wavelengths; }

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

    /// Fractional X position of band @p i across the chart width: scaled by
    /// the wavelength grid when all bands carry WAVELENGTH metadata, else by
    /// band index.
    double xFractionForBand( int i, int bandCount ) const;

    QgsPointXY m_point;
    QgsRasterLayer *m_rasterLayer = nullptr;

    // Cached GDAL dataset handle — avoids reopening on every click
    GDALDatasetH m_cachedDataset = nullptr;
    QString m_cachedSource;

    // Per-band data
    QVector<double> m_values;          // pixel value per band
    QVector<QString> m_bandLabels;     // label per band (description or "Band N")
    QVector<double> m_wavelengths;     // center wavelength (nm) per band, 0 when absent
    QString m_layerName;
    bool m_hasData = false;

    // Computed ranges
    double m_minValue = 0.0;
    double m_maxValue = 0.0;
};
