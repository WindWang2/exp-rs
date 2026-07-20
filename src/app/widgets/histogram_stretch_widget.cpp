/***************************************************************************
 * histogram_stretch_widget.cpp  —  Interactive histogram stretch widget
 ***************************************************************************/
#include "histogram_stretch_widget.h"
#include "histogram_widget.h"

#include <qgsrasterlayer.h>
#include <qgsrasterrenderer.h>
#include <qgscontrastenhancement.h>
#include <qgssinglebandgrayrenderer.h>
#include <qgsmultibandcolorrenderer.h>
#include <qgis.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QMessageBox>

HistogramStretchWidget::HistogramStretchWidget( QWidget *parent )
    : QWidget( parent )
{
    setupUi();
}

void HistogramStretchWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout( this );
    mainLayout->setContentsMargins( 4, 4, 4, 4 );
    mainLayout->setSpacing( 4 );

    // Histogram display
    m_histogram = new HistogramWidget( this );
    mainLayout->addWidget( m_histogram, 1 );

    // Controls
    auto *formLayout = new QFormLayout();
    formLayout->setSpacing( 4 );

    // Band selector
    m_bandCombo = new QComboBox( this );
    connect( m_bandCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, &HistogramStretchWidget::onBandChanged );
    formLayout->addRow( tr( "Band:" ), m_bandCombo );

    // Algorithm selector
    m_algorithmCombo = new QComboBox( this );
    m_algorithmCombo->addItem( tr( "Linear Min-Max" ), static_cast<int>( StretchAlgorithm::LinearMinMax ) );
    m_algorithmCombo->addItem( tr( "Percent Clip" ), static_cast<int>( StretchAlgorithm::PercentClip ) );
    m_algorithmCombo->addItem( tr( "Std Dev" ), static_cast<int>( StretchAlgorithm::StdDev ) );
    m_algorithmCombo->addItem( tr( "No Enhancement" ), static_cast<int>( StretchAlgorithm::NoEnhancement ) );
    connect( m_algorithmCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, &HistogramStretchWidget::onAlgorithmChanged );
    formLayout->addRow( tr( "Algorithm:" ), m_algorithmCombo );

    // Percent slider (for percent clip / std dev quick adjustment)
    auto *percentLayout = new QHBoxLayout();
    m_percentSlider = new QSlider( Qt::Horizontal, this );
    m_percentSlider->setRange( 1, 100 );
    m_percentSlider->setValue( 98 ); // 2% clip
    connect( m_percentSlider, &QSlider::valueChanged,
             this, &HistogramStretchWidget::onPercentSliderChanged );
    m_percentLabel = new QLabel( tr( "98%" ), this );
    m_percentLabel->setMinimumWidth( 40 );
    percentLayout->addWidget( m_percentSlider );
    percentLayout->addWidget( m_percentLabel );
    formLayout->addRow( tr( "Range %:" ), percentLayout );

    // Min/max spin boxes
    auto *minMaxLayout = new QHBoxLayout();
    m_minSpin = new QDoubleSpinBox( this );
    m_minSpin->setDecimals( 3 );
    m_minSpin->setRange( -1e9, 1e9 );
    connect( m_minSpin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
             this, &HistogramStretchWidget::onMinMaxChanged );
    m_maxSpin = new QDoubleSpinBox( this );
    m_maxSpin->setDecimals( 3 );
    m_maxSpin->setRange( -1e9, 1e9 );
    connect( m_maxSpin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
             this, &HistogramStretchWidget::onMinMaxChanged );
    minMaxLayout->addWidget( new QLabel( tr( "Min:" ), this ) );
    minMaxLayout->addWidget( m_minSpin );
    minMaxLayout->addWidget( new QLabel( tr( "Max:" ), this ) );
    minMaxLayout->addWidget( m_maxSpin );
    formLayout->addRow( minMaxLayout );

    mainLayout->addLayout( formLayout );

    // Buttons
    auto *buttonLayout = new QHBoxLayout();
    m_applyButton = new QPushButton( tr( "Apply" ), this );
    connect( m_applyButton, &QPushButton::clicked, this, &HistogramStretchWidget::applyStretch );
    m_resetButton = new QPushButton( tr( "Reset" ), this );
    connect( m_resetButton, &QPushButton::clicked, this, &HistogramStretchWidget::resetStretch );
    buttonLayout->addWidget( m_applyButton );
    buttonLayout->addWidget( m_resetButton );
    buttonLayout->addStretch();
    mainLayout->addLayout( buttonLayout );

    onAlgorithmChanged( 0 );
}

void HistogramStretchWidget::setRasterLayer( QgsRasterLayer *layer )
{
    m_rasterLayer = layer;
    m_histogram->setRasterLayer( layer );

    m_bandCombo->clear();
    if ( layer && layer->isValid() )
    {
        const int bandCount = layer->bandCount();
        for ( int i = 1; i <= bandCount; ++i )
            m_bandCombo->addItem( tr( "Band %1" ).arg( i ), i );
        m_bandCombo->setCurrentIndex( 0 );
        setBand( 1 );
        updateMinMaxFromLayer();
    }
}

void HistogramStretchWidget::setBand( int bandNumber )
{
    if ( bandNumber < 1 || !m_rasterLayer || bandNumber > m_rasterLayer->bandCount() )
        return;

    m_band = bandNumber;
    m_histogram->setBand( m_band );
    updateMinMaxFromLayer();
}

void HistogramStretchWidget::onBandChanged( int index )
{
    if ( index < 0 )
        return;
    setBand( m_bandCombo->itemData( index ).toInt() );
}

void HistogramStretchWidget::onAlgorithmChanged( int index )
{
    m_algorithm = static_cast<StretchAlgorithm>( m_algorithmCombo->itemData( index ).toInt() );
    const bool needsPercent = ( m_algorithm == StretchAlgorithm::PercentClip ||
                                m_algorithm == StretchAlgorithm::StdDev );
    m_percentSlider->setVisible( needsPercent );
    m_percentLabel->setVisible( needsPercent );

    if ( !m_updating )
        applyStretch();
}

void HistogramStretchWidget::onPercentSliderChanged( int value )
{
    m_percentLabel->setText( tr( "%1%" ).arg( value ) );
    if ( !m_updating )
        applyStretch();
}

void HistogramStretchWidget::onMinMaxChanged()
{
    if ( !m_updating )
        applyStretch();
}

void HistogramStretchWidget::updateMinMaxFromLayer()
{
    if ( !m_rasterLayer || !m_rasterLayer->isValid() )
        return;

    QgsRasterDataProvider *provider = m_rasterLayer->dataProvider();
    if ( !provider )
        return;

    m_updating = true;

    const QgsRasterBandStats stats = provider->bandStatistics(
        m_band, Qgis::RasterBandStatistic::Min | Qgis::RasterBandStatistic::Max );
    m_dataMin = stats.minimumValue;
    m_dataMax = stats.maximumValue;

    m_minSpin->setRange( m_dataMin - qAbs( m_dataMin ), m_dataMax + qAbs( m_dataMax ) );
    m_maxSpin->setRange( m_dataMin - qAbs( m_dataMin ), m_dataMax + qAbs( m_dataMax ) );
    m_minSpin->setValue( m_dataMin );
    m_maxSpin->setValue( m_dataMax );

    m_updating = false;
}

void HistogramStretchWidget::applyStretch()
{
    if ( !m_rasterLayer || !m_rasterLayer->isValid() )
        return;

    QgsRasterRenderer *renderer = m_rasterLayer->renderer();
    if ( !renderer )
        return;

    double minValue = m_minSpin->value();
    double maxValue = m_maxSpin->value();

    if ( m_algorithm == StretchAlgorithm::NoEnhancement )
    {
        applyContrastEnhancement( m_dataMin, m_dataMax );
        emit stretchApplied();
        return;
    }

    // For percent clip / std dev, adjust min/max based on slider
    const double percent = m_percentSlider->value() / 100.0;
    if ( m_algorithm == StretchAlgorithm::PercentClip )
    {
        const double range = m_dataMax - m_dataMin;
        const double clip = range * ( 1.0 - percent ) / 2.0;
        minValue = m_dataMin + clip;
        maxValue = m_dataMax - clip;
    }
    else if ( m_algorithm == StretchAlgorithm::StdDev )
    {
        QgsRasterDataProvider *provider = m_rasterLayer->dataProvider();
        const QgsRasterBandStats stats = provider->bandStatistics(
            m_band, Qgis::RasterBandStatistic::Mean | Qgis::RasterBandStatistic::StdDev );
        const double k = ( 100 - m_percentSlider->value() ) / 20.0; // rough mapping
        minValue = stats.mean - k * stats.stdDev;
        maxValue = stats.mean + k * stats.stdDev;
    }

    // Update spin boxes without re-triggering apply
    m_updating = true;
    m_minSpin->setValue( minValue );
    m_maxSpin->setValue( maxValue );
    m_updating = false;

    applyContrastEnhancement( minValue, maxValue );
    emit stretchApplied();
}

void HistogramStretchWidget::applyContrastEnhancement( double minValue, double maxValue )
{
    if ( !m_rasterLayer || !m_rasterLayer->renderer() )
        return;

    QgsRasterRenderer *renderer = m_rasterLayer->renderer();

    auto createEnhancement = [&]() -> QgsContrastEnhancement * {
        auto *ce = new QgsContrastEnhancement( Qgis::DataType::Float32 );
        ce->setMinimumValue( minValue );
        ce->setMaximumValue( maxValue );
        ce->setContrastEnhancementAlgorithm(
            m_algorithm == StretchAlgorithm::NoEnhancement
                ? QgsContrastEnhancement::NoEnhancement
                : QgsContrastEnhancement::StretchToMinimumMaximum );
        return ce;
    };

    // Determine band(s) to enhance
    if ( auto *grayRenderer = dynamic_cast<QgsSingleBandGrayRenderer *>( renderer ) )
    {
        grayRenderer->setContrastEnhancement( createEnhancement() );
    }
    else if ( auto *rgbRenderer = dynamic_cast<QgsMultiBandColorRenderer *>( renderer ) )
    {
        rgbRenderer->setRedContrastEnhancement( createEnhancement() );
        rgbRenderer->setGreenContrastEnhancement( createEnhancement() );
        rgbRenderer->setBlueContrastEnhancement( createEnhancement() );
    }

    m_rasterLayer->triggerRepaint();
}

void HistogramStretchWidget::resetStretch()
{
    if ( !m_rasterLayer )
        return;

    updateMinMaxFromLayer();
    applyStretch();
}
