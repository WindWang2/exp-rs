// histogram_stretch_widget.h — Photoshop-style & Piecewise Linear Contrast Stretch Widget
#pragma once

#include <QPointer>
#include <QWidget>
#include "histogram_widget.h"

#include <qgsrasterlayer.h>

class QComboBox;
class QDoubleSpinBox;
class QSlider;
class QLabel;
class QPushButton;

/**
 * \brief Photoshop-style & Piecewise Linear Interactive Contrast Stretch Widget.
 *
 * Displays an interactive RGB/grayscale histogram with Real Physical Data Ranges,
 * provides channel selection, min/max/gamma sliders, Piecewise Linear Stretch (分段点/分段线性),
 * and updates the map canvas live or exports via processing algorithms.
 */
class HistogramStretchWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HistogramStretchWidget( QWidget *parent = nullptr );

    void setRasterLayer( QgsRasterLayer *layer );
    QgsRasterLayer *rasterLayer() const { return m_rasterLayer.data(); }

    int band() const { return m_band; }

    enum class StretchAlgorithm
    {
        PhotoshopLevels,
        PiecewiseLinear,
        PercentClip,
        StdDev,
        LinearMinMax,
        HistogramEq,
        NoEnhancement
    };

    StretchAlgorithm algorithm() const { return m_algorithm; }
    QVector<QPointF> piecewisePoints() const;

public Q_SLOTS:
    void setBand( int bandNumber );
    void applyStretch();
    void resetStretch();

Q_SIGNALS:
    void parametersChanged();
    void stretchApplied();

private Q_SLOTS:
    void onChannelChanged( int index );
    void onAlgorithmChanged( int index );
    void onPercentSliderChanged( int value );
    void onMinMaxChanged();
    void onGammaChanged( double value );
    void onBandChanged( int index );
    void onHistogramCutoffsChanged( double black, double white, double gamma );
    void onPiecewisePointsChanged( const QVector<QPointF> &points );
    void onRasterLayerDestroyed();

private:
    void setupUi();
    void updateMinMaxFromLayer();
    bool hasValidRasterLayer() const;
    /** Build domain Spec from UI state and apply via rs::display deep module. */
    bool applyCurrentSpec();

    HistogramWidget *m_histogram = nullptr;
    QPointer<QgsRasterLayer> m_rasterLayer;

    QComboBox *m_channelCombo = nullptr;
    QComboBox *m_algorithmCombo = nullptr;
    QComboBox *m_bandCombo = nullptr;

    QDoubleSpinBox *m_minSpin = nullptr;
    QDoubleSpinBox *m_maxSpin = nullptr;
    QDoubleSpinBox *m_gammaSpin = nullptr;

    QSlider *m_percentSlider = nullptr;
    QLabel *m_percentLabel = nullptr;
    QLabel *m_parameterLabel = nullptr;
    QLabel *m_piecewiseHintLabel = nullptr;

    QPushButton *m_applyButton = nullptr;
    QPushButton *m_resetButton = nullptr;

    int m_band = 1;
    StretchAlgorithm m_algorithm = StretchAlgorithm::PiecewiseLinear;
    double m_dataMin = 0.0;
    double m_dataMax = 255.0;
    bool m_updating = false;
};
