#include "histogram_stretch_widget.h"
#include "histogram_widget.h"
#include "display/qgs_display_stretch.h"

#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgis.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QSignalBlocker>

#include <cmath>
#include <vector>

namespace {

rs::display::ChannelScope scopeFromChannelMode( HistogramWidget::ChannelMode mode )
{
  using Mode = HistogramWidget::ChannelMode;
  using Scope = rs::display::ChannelScope;
  switch ( mode )
  {
    case Mode::Red: return Scope::Red;
    case Mode::Green: return Scope::Green;
    case Mode::Blue: return Scope::Blue;
    case Mode::SingleBand: return Scope::ActiveGrayBand;
    case Mode::MasterRGB: return Scope::MasterRgb;
  }
  return Scope::MasterRgb;
}

} // namespace

HistogramStretchWidget::HistogramStretchWidget( QWidget *parent )
    : QWidget( parent )
{
    setupUi();
}

void HistogramStretchWidget::setupUi()
{
    setObjectName( QStringLiteral( "rsHistogramStretchRoot" ) );
    auto *mainLayout = new QVBoxLayout( this );
    mainLayout->setContentsMargins( 6, 6, 6, 6 );
    mainLayout->setSpacing( 6 );

    // Histogram display (instrument chart chrome via QSS #rsHistogramChart)
    m_histogram = new HistogramWidget( this );
    m_histogram->setObjectName( QStringLiteral( "rsHistogramChart" ) );
    m_histogram->setToolTip( tr( "直方图：拖动控制点做分段线性拉伸；双击添加、右键删除控制点。" ) );
    connect( m_histogram, &HistogramWidget::cutoffsChanged,
             this, &HistogramStretchWidget::onHistogramCutoffsChanged );
    connect( m_histogram, &HistogramWidget::piecewisePointsChanged,
             this, &HistogramStretchWidget::onPiecewisePointsChanged );
    mainLayout->addWidget( m_histogram, 1 );

    // Controls
    auto *formLayout = new QFormLayout();
    formLayout->setSpacing( 6 );

    // Channel Selector (Photoshop Style)
    m_channelCombo = new QComboBox( this );
    m_channelCombo->setToolTip( tr( "选择直方图通道模式：RGB 综合、单通道或单波段灰度。" ) );
    m_channelCombo->addItem( tr( "RGB 综合通道 (Master RGB)" ), static_cast<int>( HistogramWidget::ChannelMode::MasterRGB ) );
    m_channelCombo->addItem( tr( "红通道 (Red)" ), static_cast<int>( HistogramWidget::ChannelMode::Red ) );
    m_channelCombo->addItem( tr( "绿通道 (Green)" ), static_cast<int>( HistogramWidget::ChannelMode::Green ) );
    m_channelCombo->addItem( tr( "蓝通道 (Blue)" ), static_cast<int>( HistogramWidget::ChannelMode::Blue ) );
    m_channelCombo->addItem( tr( "单波段 / 灰度 (Single Band)" ), static_cast<int>( HistogramWidget::ChannelMode::SingleBand ) );
    connect( m_channelCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, &HistogramStretchWidget::onChannelChanged );
    formLayout->addRow( tr( "通道模式 (Channel):" ), m_channelCombo );

    // Band Selector
    m_bandCombo = new QComboBox( this );
    m_bandCombo->setToolTip( tr( "选择单波段模式下的波段号。" ) );
    connect( m_bandCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, &HistogramStretchWidget::onBandChanged );
    formLayout->addRow( tr( "单波段选择 (Band):" ), m_bandCombo );

    // Algorithm Selector
    m_algorithmCombo = new QComboBox( this );
    m_algorithmCombo->setToolTip( tr( "拉伸算法：分段线性/PS 色阶/2% 剪裁/2σ/全阶线性/均衡化/无增强。" ) );
    m_algorithmCombo->addItem( tr( "分段线性拉伸 (Piecewise Linear)" ), static_cast<int>( StretchAlgorithm::PiecewiseLinear ) );
    m_algorithmCombo->addItem( tr( "Photoshop 色阶调整 (PS Levels)" ), static_cast<int>( StretchAlgorithm::PhotoshopLevels ) );
    m_algorithmCombo->addItem( tr( "2% 累计剪裁线性拉伸 (2% Percent Clip)" ), static_cast<int>( StretchAlgorithm::PercentClip ) );
    m_algorithmCombo->addItem( tr( "2σ 标准差拉伸 (StdDev)" ), static_cast<int>( StretchAlgorithm::StdDev ) );
    m_algorithmCombo->addItem( tr( "全阶线性拉伸 (Linear Min-Max)" ), static_cast<int>( StretchAlgorithm::LinearMinMax ) );
    m_algorithmCombo->addItem( tr( "直方图均衡化 (Histogram Eq)" ), static_cast<int>( StretchAlgorithm::HistogramEq ) );
    m_algorithmCombo->addItem( tr( "无增强 (No Enhancement)" ), static_cast<int>( StretchAlgorithm::NoEnhancement ) );
    connect( m_algorithmCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, &HistogramStretchWidget::onAlgorithmChanged );
    formLayout->addRow( tr( "拉伸算法 (Algorithm):" ), m_algorithmCombo );

    // Piecewise Interactive Hint Label
    m_piecewiseHintLabel = new QLabel( tr( "分段交互: 双击添加控制点，拖拽移动，右键删除。" ), this );
    m_piecewiseHintLabel->setObjectName( QStringLiteral( "rsDialogHint" ) );
    formLayout->addRow( m_piecewiseHintLabel );

    // Percent Slider
    auto *percentLayout = new QHBoxLayout();
    m_percentSlider = new QSlider( Qt::Horizontal, this );
    m_percentSlider->setToolTip( tr( "剪裁比例：保留中间 N% 像素做线性拉伸（2% 剪裁算法用）。" ) );
    m_percentSlider->setRange( 1, 100 );
    m_percentSlider->setValue( 98 );
    connect( m_percentSlider, &QSlider::valueChanged,
             this, &HistogramStretchWidget::onPercentSliderChanged );
    m_percentLabel = new QLabel( tr( "98%" ), this );
    m_percentLabel->setMinimumWidth( 40 );
    percentLayout->addWidget( m_percentSlider );
    percentLayout->addWidget( m_percentLabel );
    m_parameterLabel = new QLabel( tr( "剪裁比例 (Clip %):" ), this );
    formLayout->addRow( m_parameterLabel, percentLayout );

    // Photoshop Levels Controls
    auto *levelsLayout = new QHBoxLayout();

    m_minSpin = new QDoubleSpinBox( this );
    m_minSpin->setToolTip( tr( "阴影（最小值）：低于此值的像素映射为黑。" ) );
    m_minSpin->setDecimals( 2 );
    m_minSpin->setRange( -1e9, 1e9 );
    connect( m_minSpin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
             this, &HistogramStretchWidget::onMinMaxChanged );

    m_gammaSpin = new QDoubleSpinBox( this );
    m_gammaSpin->setToolTip( tr( "Gamma（中音）：1.0 为线性；<1 提亮，>1 压暗。" ) );
    m_gammaSpin->setDecimals( 2 );
    m_gammaSpin->setRange( 0.1, 10.0 );
    m_gammaSpin->setSingleStep( 0.05 );
    m_gammaSpin->setValue( 1.0 );
    connect( m_gammaSpin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
             this, &HistogramStretchWidget::onGammaChanged );

    m_maxSpin = new QDoubleSpinBox( this );
    m_maxSpin->setToolTip( tr( "高光（最大值）：高于此值的像素映射为白。" ) );
    m_maxSpin->setDecimals( 2 );
    m_maxSpin->setRange( -1e9, 1e9 );
    connect( m_maxSpin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
             this, &HistogramStretchWidget::onMinMaxChanged );

    levelsLayout->addWidget( new QLabel( tr( "阴影 (Min):" ), this ) );
    levelsLayout->addWidget( m_minSpin );
    levelsLayout->addWidget( new QLabel( tr( "Gamma (中音):" ), this ) );
    levelsLayout->addWidget( m_gammaSpin );
    levelsLayout->addWidget( new QLabel( tr( "高光 (Max):" ), this ) );
    levelsLayout->addWidget( m_maxSpin );

    formLayout->addRow( levelsLayout );
    mainLayout->addLayout( formLayout );

    // Buttons
    auto *buttonLayout = new QHBoxLayout();
    m_applyButton = new QPushButton( tr( "应用到显示" ), this );
    m_applyButton->setToolTip( tr( "把当前拉伸参数应用到地图显示。" ) );
    m_applyButton->setProperty( "primary", true );
    m_applyButton->setObjectName( QStringLiteral( "rsPrimaryButton" ) );
    connect( m_applyButton, &QPushButton::clicked, this, &HistogramStretchWidget::applyStretch );

    m_resetButton = new QPushButton( tr( "重置 (Reset)" ), this );
    m_resetButton->setToolTip( tr( "重置拉伸参数为默认值。" ) );
    connect( m_resetButton, &QPushButton::clicked, this, &HistogramStretchWidget::resetStretch );

    buttonLayout->addWidget( m_applyButton );
    buttonLayout->addWidget( m_resetButton );
    buttonLayout->addStretch();
    mainLayout->addLayout( buttonLayout );

    onAlgorithmChanged( 0 );
}

QVector<QPointF> HistogramStretchWidget::piecewisePoints() const
{
    return m_histogram ? m_histogram->piecewisePoints() : QVector<QPointF>();
}

void HistogramStretchWidget::setRasterLayer( QgsRasterLayer *layer )
{
    if ( m_rasterLayer )
        disconnect( m_rasterLayer, &QObject::destroyed, this, &HistogramStretchWidget::onRasterLayerDestroyed );

    m_rasterLayer = layer;
    m_histogram->setRasterLayer( layer );

    if ( m_rasterLayer )
        connect( m_rasterLayer, &QObject::destroyed, this, &HistogramStretchWidget::onRasterLayerDestroyed );

    m_bandCombo->clear();
    if ( layer && layer->isValid() )
    {
        const int bandCount = layer->bandCount();
        for ( int i = 1; i <= bandCount; ++i )
            m_bandCombo->addItem( tr( "Band %1" ).arg( i ), i );

        m_updating = true;
        if ( bandCount >= 3 )
        {
            m_channelCombo->setCurrentIndex( 0 );
            m_histogram->setChannelMode( HistogramWidget::ChannelMode::MasterRGB );
            m_bandCombo->setEnabled( false );
        }
        else
        {
            m_channelCombo->setCurrentIndex( 4 );
            m_histogram->setChannelMode( HistogramWidget::ChannelMode::SingleBand );
            m_bandCombo->setEnabled( true );
        }

        m_bandCombo->setCurrentIndex( 0 );
        m_updating = false;
        setBand( 1 );
        updateMinMaxFromLayer();
    }
}

void HistogramStretchWidget::onRasterLayerDestroyed()
{
    m_rasterLayer.clear();
    if ( m_histogram )
        m_histogram->setRasterLayer( nullptr );
}

bool HistogramStretchWidget::hasValidRasterLayer() const
{
    return m_rasterLayer && m_rasterLayer->isValid();
}

void HistogramStretchWidget::setBand( int bandNumber )
{
    if ( bandNumber < 1 || !hasValidRasterLayer() || bandNumber > m_rasterLayer->bandCount() )
        return;

    m_band = bandNumber;
    m_histogram->setBand( m_band );
    updateMinMaxFromLayer();
}

void HistogramStretchWidget::onChannelChanged( int index )
{
    if ( index < 0 || m_updating )
        return;
    auto mode = static_cast<HistogramWidget::ChannelMode>( m_channelCombo->itemData( index ).toInt() );
    m_histogram->setChannelMode( mode );
    m_bandCombo->setEnabled( mode == HistogramWidget::ChannelMode::SingleBand );
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
    const bool isPiecewise = ( m_algorithm == StretchAlgorithm::PiecewiseLinear );
    const bool needsPercent = ( m_algorithm == StretchAlgorithm::PercentClip ||
                                m_algorithm == StretchAlgorithm::StdDev );

    m_histogram->setEnablePiecewise( isPiecewise );
    m_piecewiseHintLabel->setVisible( isPiecewise );

    m_percentSlider->setVisible( needsPercent );
    m_percentLabel->setVisible( needsPercent );
    m_parameterLabel->setVisible( needsPercent );

    if ( needsPercent )
    {
        const QSignalBlocker blocker( m_percentSlider );
        if ( m_algorithm == StretchAlgorithm::StdDev )
        {
            m_parameterLabel->setText( tr( "标准差系数:" ) );
            m_percentSlider->setRange( 5, 50 );
            m_percentSlider->setValue( 20 );
            m_percentLabel->setText( tr( "2.0σ" ) );
        }
        else
        {
            m_parameterLabel->setText( tr( "保留比例 (Keep %):" ) );
            m_percentSlider->setRange( 1, 100 );
            m_percentSlider->setValue( 98 );
            m_percentLabel->setText( tr( "98%" ) );
        }
    }

    if ( !m_updating )
        applyStretch();
}

void HistogramStretchWidget::onPercentSliderChanged( int value )
{
    if ( m_algorithm == StretchAlgorithm::StdDev )
        m_percentLabel->setText( tr( "%1σ" ).arg( value / 10.0, 0, 'f', 1 ) );
    else
        m_percentLabel->setText( tr( "%1%" ).arg( value ) );
    if ( !m_updating )
        applyStretch();
}

void HistogramStretchWidget::onMinMaxChanged()
{
    if ( !m_updating )
    {
        m_histogram->setCutoffs( m_minSpin->value(), m_maxSpin->value(), m_gammaSpin->value() );
        applyStretch();
    }
}

void HistogramStretchWidget::onGammaChanged( double value )
{
    if ( !m_updating )
    {
        m_histogram->setCutoffs( m_minSpin->value(), m_maxSpin->value(), value );
        applyStretch();
    }
}

void HistogramStretchWidget::onHistogramCutoffsChanged( double black, double white, double gamma )
{
    m_updating = true;
    m_minSpin->setValue( black );
    m_maxSpin->setValue( white );
    m_gammaSpin->setValue( gamma );
    m_updating = false;

    applyStretch();
}

void HistogramStretchWidget::onPiecewisePointsChanged( const QVector<QPointF> &points )
{
    Q_UNUSED( points )
    if ( !m_updating && m_algorithm == StretchAlgorithm::PiecewiseLinear )
        applyStretch();
}

void HistogramStretchWidget::updateMinMaxFromLayer()
{
    if ( !hasValidRasterLayer() )
        return;

    QgsRasterDataProvider *provider = m_rasterLayer->dataProvider();
    if ( !provider )
        return;

    m_updating = true;

    m_dataMin = m_histogram->realDataMin();
    m_dataMax = m_histogram->realDataMax();

    if ( !( m_dataMax > m_dataMin ) )
    {
        const QgsRasterBandStats stats = provider->bandStatistics(
            m_band, Qgis::RasterBandStatistic::Min | Qgis::RasterBandStatistic::Max );
        m_dataMin = stats.minimumValue;
        m_dataMax = stats.maximumValue;
    }

    if ( !( m_dataMax > m_dataMin ) )
    {
        m_dataMin = 0.0;
        m_dataMax = 255.0;
    }

    const double pad = std::max( 1.0, qAbs( m_dataMax - m_dataMin ) );
    m_minSpin->setRange( m_dataMin - pad, m_dataMax + pad );
    m_maxSpin->setRange( m_dataMin - pad, m_dataMax + pad );

    m_minSpin->setValue( m_dataMin );
    m_maxSpin->setValue( m_dataMax );
    m_gammaSpin->setValue( 1.0 );

    m_histogram->setCutoffs( m_dataMin, m_dataMax, 1.0 );
    m_histogram->resetPiecewisePoints();

    m_updating = false;
}

bool HistogramStretchWidget::applyCurrentSpec()
{
    if ( !hasValidRasterLayer() )
        return false;

    using namespace rs::display;

    const auto mode = m_histogram
                        ? m_histogram->channelMode()
                        : HistogramWidget::ChannelMode::SingleBand;
    const ChannelScope scope = scopeFromChannelMode( mode );

    StretchSpec spec = StretchSpec::realDataRange( scope );

    switch ( m_algorithm )
    {
      case StretchAlgorithm::PiecewiseLinear:
      {
        std::vector<ControlPoint> pts;
        const QVector<QPointF> uiPts = m_histogram->piecewisePoints();
        pts.reserve( static_cast<size_t>( uiPts.size() ) );
        for ( const QPointF &p : uiPts )
          pts.push_back( ControlPoint{ p.x(), p.y() } );
        spec = StretchSpec::piecewise( std::move( pts ), scope );
        break;
      }
      case StretchAlgorithm::PhotoshopLevels:
        spec = StretchSpec::levels( m_minSpin->value(), m_maxSpin->value(),
                                    m_gammaSpin->value(), scope );
        break;
      case StretchAlgorithm::PercentClip:
      {
        // Slider is "keep %" (98 → keep 98%, clip 2% total)
        const double keep = m_percentSlider->value();
        const double clipTotal = std::max( 0.0, 100.0 - keep );
        spec = StretchSpec::percentClip( clipTotal, scope );
        break;
      }
      case StretchAlgorithm::StdDev:
      {
        const double k = m_percentSlider->value() / 10.0;
        spec = StretchSpec::stdDev( k, scope );
        break;
      }
      case StretchAlgorithm::LinearMinMax:
        spec = StretchSpec::linearMinMax( m_minSpin->value(), m_maxSpin->value(), scope );
        break;
      case StretchAlgorithm::HistogramEq:
        spec = StretchSpec::histogramEqualize( scope );
        break;
      case StretchAlgorithm::NoEnhancement:
        spec = StretchSpec::noEnhancement( scope );
        break;
    }

    spec = spec.withStatsBand( m_band );

    const ApplyStretchResult result = applyToLayer( m_rasterLayer.data(), spec, m_band );
    if ( !result )
      return false;

    const ResolvedStretch &applied = result.value().applied;
    m_updating = true;
    m_minSpin->setValue( applied.displayMin );
    m_maxSpin->setValue( applied.displayMax );
    if ( applied.gamma > 0.0 )
      m_gammaSpin->setValue( applied.gamma );
    m_histogram->setCutoffs( applied.displayMin, applied.displayMax, m_gammaSpin->value() );
    m_updating = false;
    return true;
}

void HistogramStretchWidget::applyStretch()
{
    if ( applyCurrentSpec() )
        emit stretchApplied();
}

void HistogramStretchWidget::resetStretch()
{
    if ( !hasValidRasterLayer() )
        return;

    updateMinMaxFromLayer();

    using namespace rs::display;
    const auto mode = m_histogram
                        ? m_histogram->channelMode()
                        : HistogramWidget::ChannelMode::SingleBand;
    const StretchSpec spec =
      StretchSpec::realDataRange( scopeFromChannelMode( mode ) ).withStatsBand( m_band );

    if ( applyToLayer( m_rasterLayer.data(), spec, m_band ) )
        emit stretchApplied();
}
