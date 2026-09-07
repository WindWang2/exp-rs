// src/app/dialogs/contrast_stretch_dialog.cpp
#include "contrast_stretch_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "app/widgets/histogram_stretch_widget.h"
#include "widgets/raster_layer_combo.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMessageBox>

ContrastStretchDialog::ContrastStretchDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setupUi();
}

void ContrastStretchDialog::setRasterLayer( QgsRasterLayer *layer )
{
  RasterProcessingDialogBase::setRasterLayer( layer );
  if ( m_layerCombo && layer )
  {
    const int idx = m_layerCombo->findData( layer->id() );
    if ( idx >= 0 && m_layerCombo->currentIndex() != idx )
    {
      m_layerCombo->blockSignals( true );
      m_layerCombo->setCurrentIndex( idx );
      m_layerCombo->blockSignals( false );
    }
  }
  if ( m_stretchWidget && layer )
  {
    m_stretchWidget->setRasterLayer( layer );
  }
}

void ContrastStretchDialog::onLayerChanged( int /*index*/ )
{
  if ( m_layerCombo )
  {
    auto *layer = m_layerCombo->currentRasterLayer();
    if ( layer && layer != m_rasterLayer )
      setRasterLayer( layer );
  }
}

void ContrastStretchDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input Layer Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "输入数据" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "contrastStretchInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "选择待执行对比度拉伸的栅格图层。" ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ContrastStretchDialog::onLayerChanged );
  inputForm->addRow( tr( "输入栅格" ), m_layerCombo );
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Embedded Interactive Photoshop Levels & Histogram Panel
  m_stretchWidget = new HistogramStretchWidget( this );
  mainLayout->addWidget( m_stretchWidget, 1 );

  // Preset Parameters Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "预设算法与导出" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_methodCombo = new QComboBox( paramGroup );
  m_methodCombo->addItems( { tr( "Photoshop 自定义色阶" ), tr( "线性拉伸 (Min-Max)" ), tr( "百分比裁剪拉伸" ),
                             tr( "标准差拉伸" ), tr( "直方图均衡化" ) } );
  SicnuDialogHelp::tip( m_methodCombo, tr(
    "拉伸方法：\n"
    "• Photoshop 色阶：交互调节阴影、高光与 Gamma 中音\n"
    "• 线性：最小–最大\n"
    "• 百分比裁剪：两端裁剪后再拉伸\n"
    "• 标准差：均值±K×标准差\n"
    "• 直方图均衡化：增强全局对比" ) );
  connect( m_methodCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ContrastStretchDialog::onMethodChanged );
  form->addRow( tr( "预设方法" ), m_methodCombo );

  m_clipLabel = new QLabel( tr( "裁剪比例" ), paramGroup );
  m_clipSpin = new QDoubleSpinBox( paramGroup );
  m_clipSpin->setRange( 0.1, 50.0 );
  m_clipSpin->setValue( 2.0 );
  m_clipSpin->setSingleStep( 0.5 );
  m_clipSpin->setDecimals( 1 );
  m_clipSpin->setSuffix( QStringLiteral( " %" ) );
  SicnuDialogHelp::tip( m_clipSpin, tr( "两端各舍弃该比例像元后再拉伸。常用 1–2%。" ) );
  form->addRow( m_clipLabel, m_clipSpin );

  m_stddevLabel = new QLabel( tr( "标准差倍数 K" ), paramGroup );
  m_stddevSpin = new QDoubleSpinBox( paramGroup );
  m_stddevSpin->setRange( 0.1, 10.0 );
  m_stddevSpin->setValue( 2.0 );
  m_stddevSpin->setSingleStep( 0.5 );
  m_stddevSpin->setDecimals( 1 );
  SicnuDialogHelp::tip( m_stddevSpin, tr( "拉伸到 mean±K·σ。常用 2。" ) );
  form->addRow( m_stddevLabel, m_stddevSpin );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );

  onMethodChanged( 0 );

  if ( m_rasterLayer )
    m_layerCombo->selectLayer( m_rasterLayer->id() );
  else if ( m_layerCombo->count() > 0 )
    setRasterLayer( m_layerCombo->currentRasterLayer() );
}

void ContrastStretchDialog::onMethodChanged( int index )
{
  m_clipLabel->setVisible( index == 2 );
  m_clipSpin->setVisible( index == 2 );
  m_stddevLabel->setVisible( index == 3 );
  m_stddevSpin->setVisible( index == 3 );
}

void ContrastStretchDialog::onRun()
{
  if ( !m_rasterLayer )
    return;

  int methodIndex = m_methodCombo->currentIndex();
  double clipValue = m_clipSpin->value();
  double stddevValue = m_stddevSpin->value();

  // Thin client: the streaming stretch kernel runs as the rs:contrast_stretch
  // operator through the Task Center — same execution path as CLI/MCP.
  Json::Value params( Json::objectValue );
  params["input"] = m_rasterLayer->source().toStdString();
  params["output"] = outputPath().toStdString();
  switch ( methodIndex )
  {
    case 0:
    {
      QVector<QPointF> piecewisePoints = m_stretchWidget ? m_stretchWidget->piecewisePoints()
                                                         : QVector<QPointF>();
      if ( piecewisePoints.size() < 2 )
      {
        QMessageBox::warning( this, dialogTitle(),
                              tr( "自定义色阶至少需要两个控制点，请改用预设方法。" ) );
        return;
      }
      params["method"] = "piecewise";
      params["piecewisePoints"] = Json::Value( Json::arrayValue );
      for ( const auto &pt : piecewisePoints )
      {
        Json::Value pair( Json::arrayValue );
        pair.append( pt.x() );
        pair.append( pt.y() );
        params["piecewisePoints"].append( pair );
      }
      break;
    }
    case 2:
      params["method"] = "percent_clip";
      params["clipPercent"] = clipValue;
      break;
    case 3:
      params["method"] = "stddev";
      params["stddevK"] = stddevValue;
      break;
    case 4:
      params["method"] = "histogram_equalize";
      break;
    default:
      params["method"] = "linear";
      break;
  }
  runOperatorTask( QStringLiteral( "rs:contrast_stretch" ), params );
}
