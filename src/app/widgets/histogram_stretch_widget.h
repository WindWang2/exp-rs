/***************************************************************************
 * histogram_stretch_widget.h  —  Interactive histogram stretch widget
 ***************************************************************************/
#pragma once

#include <QWidget>

class HistogramWidget;
class QgsRasterLayer;
class QComboBox;
class QDoubleSpinBox;
class QSlider;
class QLabel;
class QPushButton;

/**
 * \brief Widget for interactive contrast stretching of a raster layer.
 *
 * Displays the band histogram and provides min/max spin boxes, a percentile
 * clip slider, and a stretch-algorithm selector.  Changes are applied
 * immediately to the layer's renderer so the map canvas updates in real
 * time (no output file is written).
 */
class HistogramStretchWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit HistogramStretchWidget( QWidget *parent = nullptr );

    /**
     * Set the raster layer to analyse and stretch.
     * The widget does not take ownership of the layer.
     */
    void setRasterLayer( QgsRasterLayer *layer );

    QgsRasterLayer *rasterLayer() const { return m_rasterLayer; }

    /**
     * Current band number (1-based) used for histogram display.
     */
    int band() const { return m_band; }

    /**
     * Current stretch algorithm.
     */
    enum class StretchAlgorithm
    {
        LinearMinMax,
        PercentClip,
        StdDev,
        NoEnhancement
    };

    StretchAlgorithm algorithm() const { return m_algorithm; }

  public Q_SLOTS:
    /**
     * Set which band to display in the histogram.
     */
    void setBand( int bandNumber );

    /**
     * Apply the current parameters to the layer renderer.
     */
    void applyStretch();

    /**
     * Reset to the layer's original min/max.
     */
    void resetStretch();

  Q_SIGNALS:
    void parametersChanged();
    void stretchApplied();

  private Q_SLOTS:
    void onAlgorithmChanged( int index );
    void onPercentSliderChanged( int value );
    void onMinMaxChanged();
    void onBandChanged( int index );

  private:
    void setupUi();
    void updateMinMaxFromLayer();
    void applyContrastEnhancement( double minValue, double maxValue );

    HistogramWidget *m_histogram = nullptr;
    QgsRasterLayer *m_rasterLayer = nullptr;

    QComboBox *m_algorithmCombo = nullptr;
    QComboBox *m_bandCombo = nullptr;
    QDoubleSpinBox *m_minSpin = nullptr;
    QDoubleSpinBox *m_maxSpin = nullptr;
    QSlider *m_percentSlider = nullptr;
    QLabel *m_percentLabel = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_resetButton = nullptr;

    int m_band = 1;
    StretchAlgorithm m_algorithm = StretchAlgorithm::LinearMinMax;
    double m_dataMin = 0.0;
    double m_dataMax = 1.0;
    bool m_updating = false;
};
