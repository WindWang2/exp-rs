// src/app/dialogs/spatial_filter_dialog.cpp
#include "spatial_filter_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "widgets/raster_layer_combo.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>

SpatialFilterDialog::SpatialFilterDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setupUi();
}

void SpatialFilterDialog::setRasterLayer( QgsRasterLayer *layer )
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
}

void SpatialFilterDialog::onLayerChanged( int /*index*/ )
{
  if ( m_layerCombo )
  {
    auto *layer = m_layerCombo->currentRasterLayer();
    if ( layer && layer != m_rasterLayer )
      setRasterLayer( layer );
  }
}

void SpatialFilterDialog::onFilterTypeChanged( int /*index*/ )
{
  const QString opId = m_filterTypeCombo->currentData().toString();
  const bool isGaussian = ( opId == QStringLiteral( "opencv:gaussian_blur" ) );
  if ( m_sigmaLabel && m_sigmaSpin )
  {
    m_sigmaLabel->setVisible( isGaussian );
    m_sigmaSpin->setVisible( isGaussian );
  }
}

void SpatialFilterDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input Data Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "输入数据" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "spatialFilterInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "选择待执行空间滤波的栅格图层。" ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpatialFilterDialog::onLayerChanged );
  inputForm->addRow( tr( "输入栅格" ), m_layerCombo );
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Parameters Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "滤波参数" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_filterTypeCombo = new QComboBox( paramGroup );
  m_filterTypeCombo->addItem( tr( "均值滤波 (Mean)" ), QStringLiteral( "opencv:mean_blur" ) );
  m_filterTypeCombo->addItem( tr( "高斯滤波 (Gaussian)" ), QStringLiteral( "opencv:gaussian_blur" ) );
  m_filterTypeCombo->addItem( tr( "中值滤波 (Median)" ), QStringLiteral( "opencv:median_blur" ) );
  m_filterTypeCombo->addItem( tr( "Sobel 边缘检测" ), QStringLiteral( "opencv:sobel" ) );
  m_filterTypeCombo->addItem( tr( "Laplacian 边缘增强" ), QStringLiteral( "opencv:laplacian" ) );
  SicnuDialogHelp::tip( m_filterTypeCombo, tr(
    "• 均值 / 高斯 / 中值：平滑去噪\n"
    "• Sobel / Laplacian：边缘检测与锐化增强\n"
    "中值滤波对椒盐噪声具有极佳保边抑制效果。" ) );
  connect( m_filterTypeCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpatialFilterDialog::onFilterTypeChanged );
  form->addRow( tr( "滤波器类型" ), m_filterTypeCombo );

  m_kernelSizeCombo = new QComboBox( paramGroup );
  m_kernelSizeCombo->addItem( tr( "3×3" ), 3 );
  m_kernelSizeCombo->addItem( tr( "5×5" ), 5 );
  m_kernelSizeCombo->addItem( tr( "7×7" ), 7 );
  SicnuDialogHelp::tip( m_kernelSizeCombo, tr( "卷积滤波窗口大小。窗口越大平滑强度或响应范围越大。" ) );
  form->addRow( tr( "窗口大小" ), m_kernelSizeCombo );

  m_sigmaLabel = new QLabel( tr( "高斯标准差 Sigma" ), paramGroup );
  m_sigmaSpin = new QDoubleSpinBox( paramGroup );
  m_sigmaSpin->setRange( 0.1, 50.0 );
  m_sigmaSpin->setValue( 1.0 );
  m_sigmaSpin->setSingleStep( 0.5 );
  m_sigmaSpin->setDecimals( 2 );
  SicnuDialogHelp::tip( m_sigmaSpin, tr( "高斯滤波核的空间标准差 Sigma，默认为 1.0。" ) );
  form->addRow( m_sigmaLabel, m_sigmaSpin );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  onFilterTypeChanged( 0 );

  if ( m_rasterLayer )
    m_layerCombo->selectLayer( m_rasterLayer->id() );
  else if ( m_layerCombo->count() > 0 )
    setRasterLayer( m_layerCombo->currentRasterLayer() );
}

void SpatialFilterDialog::onRun()
{
  const QString operatorId = m_filterTypeCombo->currentData().toString();
  const int kernelSize = m_kernelSizeCombo->currentData().toInt();

  Json::Value params( Json::objectValue );
  params["input"] = m_rasterLayer->source().toStdString();
  params["output"] = outputPath().toStdString();
  params["kernelSize"] = kernelSize;
  if ( operatorId == QLatin1String( "opencv:gaussian_blur" ) )
  {
    params["sigma"] = m_sigmaSpin ? m_sigmaSpin->value() : 1.0;
  }
  if ( operatorId == QLatin1String( "opencv:sobel" ) )
  {
    params["dx"] = 1;
    params["dy"] = 1;
  }
  runOperatorTask( operatorId, params );
}
