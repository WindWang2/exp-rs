// src/app/widgets/spectral_profile_widget.h
#pragma once

#include <QWidget>
#include <QVector>
#include <QString>
#include <QPointer>

#include <qgspointxy.h>

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>

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
     * The point is expected in the project/map CRS and is transformed to
     * the layer CRS when they differ.
     *
     * Local sources are sampled inline. A network-backed source (/vsicurl/,
     * /vsis3/, … or a bare URL) is sampled on a worker thread instead: a
     * GDAL read on such a source can cost a network round trip, and the
     * identify slot that calls this must not block on it (#1561). For those
     * sources the chart clears immediately and repaints when the sample
     * lands; hasData() stays false until then.
     */
    void setProfile( const QgsPointXY &point, QgsRasterLayer *layer );

    /**
     * Worker-thread payload landed back through m_sampler for network-backed
     * sources. Plain values only — no QGIS object crosses the thread
     * boundary (the worker opens its own GDAL handle, since a dataset must
     * never be shared across threads).
     */
    struct ProfileSample
    {
        QVector<double> values;
        QVector<QString> bandLabels;
        QVector<double> wavelengths;
        QVector<double> fwhm;
        quint64 generation = 0;
        bool ok = false;
    };

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

    /// Per-band full-width-at-half-maximum (nm) from the raster's FWHM band
    /// metadata (0.0 for bands without one; empty when the raster has none).
    /// Useful for spectral-library matching and resampling (D1 surface).
    QVector<double> fwhm() const { return m_fwhm; }

    /**
     * Toggle continuum-removal display: when enabled, the chart shows the
     * continuum-removed spectrum (reflectance / convex-hull continuum, in
     * (0, 1]) computed by the SpectralClassification kernel, so absorption
     * features become comparable across brightnesses. Raw values are kept
     * untouched in values(); displayValues() applies the transform.
     */
    void setContinuumRemovalEnabled( bool enabled );
    bool continuumRemovalEnabled() const { return m_continuumRemoval; }

    /// Values the chart currently draws: raw band values, or the
    /// continuum-removed spectrum when the continuum-removal view is enabled.
    QVector<double> displayValues() const;

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

    /// Clears the per-band chart state before an extraction starts.
    void resetProfileState();
    /// Finalizes an extraction: hasData, the plot range sanity fix, the
    /// display-cache invalidation and the debug trace. Both the inline and
    /// the worker-thread paths end here.
    void finalizeProfile();
    /// Project/map CRS → layer CRS (identity when the two agree). Pure
    /// computation: it stays on the GUI thread even when the band reads do
    /// not (#1561).
    QgsPointXY toLayerCrs( const QgsPointXY &point, QgsRasterLayer *layer ) const;
    /// Starts the off-GUI-thread sampling for a network-backed source.
    void startRemoteSampling( const QString &source, const QgsPointXY &point,
                              QgsRasterLayer *layer );
    /// Lands a worker sample on the GUI thread (dropped when superseded).
    void landSample( const ProfileSample &sample );

    /// Fractional X position of band @p i across the chart width: scaled by
    /// the wavelength grid when all bands carry WAVELENGTH metadata, else by
    /// band index.
    double xFractionForBand( int i, int bandCount ) const;

    QgsPointXY m_point;
    QPointer<QgsRasterLayer> m_rasterLayer;
    QString m_rasterLayerId;

    // Cached GDAL dataset handle — avoids reopening on every click
    GDALDatasetH m_cachedDataset = nullptr;
    QString m_cachedSource;

    // Off-GUI-thread sampling for network-backed sources (#1561). Local
    // sources never touch these: they keep their inline extraction.
    QFutureWatcher<ProfileSample> m_sampler;
    quint64 m_samplingGeneration = 0;

    // Per-band data
    QVector<double> m_values;          // pixel value per band
    QVector<QString> m_bandLabels;     // label per band (description or "Band N")
    QVector<double> m_wavelengths;     // center wavelength (nm) per band, 0 when absent
    QVector<double> m_fwhm;            // FWHM (nm) per band, 0 when absent
    QString m_layerName;
    bool m_hasData = false;
    bool m_continuumRemoval = false;   // chart shows continuum-removed spectrum

    // Computed ranges
    double m_minValue = 0.0;
    double m_maxValue = 0.0;

    // Continuum-removed display cache: recomputed only when the spectrum,
    // wavelengths, or the CR toggle changes (not on every paint). Mutable so
    // the const displayValues() accessor can memoize.
    mutable QVector<double> m_displayCache;
    mutable bool m_displayDirty = true;

    /// Effective plot Y range: [0, 1] in continuum-removal mode, else the
    /// raw value range. Both drawLine() and drawAxes() must agree on it.
    void plotRange( double *vMin, double *vMax ) const;
};

// ─── D13 · exp_gui spectral workbench seam ─────────────────────────────────
// Namespaced D13 widgets (the legacy global SpectralProfileWidget above keeps
// its exact behavior for existing call sites):
//  - async, non-blocking profile sampling on a worker thread (queued result);
//  - QPointer lifecycle guard — destroying the layer clears the widget;
//  - 60 FPS QPainter rendering with crosshair tracking (no QtCharts);
//  - featureSelected(wavelength, depth) where depth is the relative depth
//    inside the current plot range.

namespace exp_gui
{

    class SpectralProfileWidget : public QWidget
    {
        Q_OBJECT

      public:
        /// Worker-thread payload landed back through the QFutureWatcher.
        struct ProfileSample
        {
            QVector<double> values;
            QVector<double> wavelengths;
            QString layerName;
            bool ok = false;
        };

      public:
        explicit SpectralProfileWidget( QWidget *parent = nullptr );
        ~SpectralProfileWidget() override;

        /// Starts an ASYNC band sampling for @p point (layer CRS coordinates)
        /// on @p layer. The widget clears immediately and re-populates via a
        /// queued GUI-thread callback when the worker finishes. Destroying
        /// @p layer mid-flight (QPointer guard) yields an empty profile.
        void setProfile( const QgsPointXY &point, QgsRasterLayer *layer );

        /// Synchronously display a precomputed spectrum (also the worker
        /// landing seam). Empty @p values clears the widget.
        void setSpectrum( const QVector<double> &values,
                          const QVector<double> &wavelengths = {},
                          const QVector<QString> &labels = {},
                          const QString &layerName = QString() );

        void clear();
        bool hasData() const noexcept { return m_hasData; }
        bool isSampling() const noexcept { return m_sampling; }
        QVector<double> currentValues() const { return m_values; }
        QVector<double> currentWavelengths() const { return m_wavelengths; }

      signals:
        /// Crosshair tracking: @p wavelength at the cursor, @p depth is the
        /// sample's relative depth inside the current plot range [0, 1].
        void featureSelected( double wavelength, double depth );
        void inspectionPointChanged( const QgsPointXY &point );
        /// Emitted (queued) after an async profile lands.
        void profileReady();

      protected:
        void paintEvent( QPaintEvent *event ) override;
        void mouseMoveEvent( QMouseEvent *event ) override;

      private:
        QVector<double> sampleBands( const QString &sourcePath, const QgsPointXY &point,
                                     int bandCount, QVector<double> *wavelengths );

        QPointer<QgsRasterLayer> m_observedLayer;
        QgsPointXY m_point;
        QVector<double> m_values;
        QVector<double> m_wavelengths;
        QVector<QString> m_labels;
        QString m_layerName;
        bool m_hasData = false;
        std::atomic<bool> m_sampling{ false };
        double m_minValue = 0.0;
        double m_maxValue = 0.0;
        int m_hoverIndex = -1;
        QFutureWatcher<ProfileSample> m_sampler;
        QMetaObject::Connection m_layerDestroyedConnection;
    };

} // namespace exp_gui
