// src/app/dialogs/speckle_filter_dialog.cpp
#include "speckle_filter_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/algorithms/image_enhancement_streaming.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"
#include "processing/gdal/gdal_safe_call.h"
#include "widgets/raster_layer_combo.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <cpl_error.h>

SpeckleFilterDialog::SpeckleFilterDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setupUi();
}

void SpeckleFilterDialog::setRasterLayer( QgsRasterLayer *layer )
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

void SpeckleFilterDialog::onLayerChanged( int /*index*/ )
{
  if ( m_layerCombo )
  {
    auto *layer = m_layerCombo->currentRasterLayer();
    if ( layer && layer != m_rasterLayer )
      setRasterLayer( layer );
  }
}

void SpeckleFilterDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input Data Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "输入数据" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "speckleFilterInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "选择待执行斑点滤波的 SAR 栅格图层。" ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpeckleFilterDialog::onLayerChanged );
  inputForm->addRow( tr( "输入栅格" ), m_layerCombo );
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Parameters Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "滤波参数" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_filterTypeCombo = new QComboBox( paramGroup );
  m_filterTypeCombo->addItems( { tr( "Lee 滤波" ), tr( "Frost 滤波" ), tr( "Kuan 滤波" ), tr( "Gamma-MAP 滤波" ) } );
  SicnuDialogHelp::tip( m_filterTypeCombo, tr( "SAR 斑点噪声抑制算法：Lee、Frost、Kuan 或 Gamma-MAP。" ) );
  form->addRow( tr( "滤波器" ), m_filterTypeCombo );

  m_kernelSizeCombo = new QComboBox( paramGroup );
  m_kernelSizeCombo->addItems( { tr( "3×3" ), tr( "5×5" ), tr( "7×7" ) } );
  m_kernelSizeCombo->setCurrentIndex( 1 );
  SicnuDialogHelp::tip( m_kernelSizeCombo, tr( "滤波窗口。3×3 保持细节边缘，7×7 平滑去噪更强。" ) );
  form->addRow( tr( "滤波窗口" ), m_kernelSizeCombo );

  m_noiseVarLabel = new QLabel( tr( "噪声方差" ), paramGroup );
  m_noiseVarSpin = new QDoubleSpinBox( paramGroup );
  m_noiseVarSpin->setRange( 0.001, 10.0 );
  m_noiseVarSpin->setValue( 1.0 );
  m_noiseVarSpin->setSingleStep( 0.1 );
  m_noiseVarSpin->setDecimals( 3 );
  SicnuDialogHelp::tip( m_noiseVarSpin, tr( "Lee / Kuan / Gamma-MAP 模型的预估相对噪声方差。" ) );
  form->addRow( m_noiseVarLabel, m_noiseVarSpin );

  m_dampingLabel = new QLabel( tr( "阻尼因子" ), paramGroup );
  m_dampingSpin = new QDoubleSpinBox( paramGroup );
  m_dampingSpin->setRange( 0.1, 10.0 );
  m_dampingSpin->setValue( 2.0 );
  m_dampingSpin->setSingleStep( 0.5 );
  m_dampingSpin->setDecimals( 1 );
  SicnuDialogHelp::tip( m_dampingSpin, tr( "Frost 滤波器的指数阻尼衰减系数：值越大越平滑。" ) );
  form->addRow( m_dampingLabel, m_dampingSpin );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  connect( m_filterTypeCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpeckleFilterDialog::onFilterTypeChanged );
  onFilterTypeChanged( 0 );

  if ( m_rasterLayer )
    m_layerCombo->selectLayer( m_rasterLayer->id() );
  else if ( m_layerCombo->count() > 0 )
    setRasterLayer( m_layerCombo->currentRasterLayer() );
}

void SpeckleFilterDialog::onFilterTypeChanged( int index )
{
  bool isFrost = ( index == 1 );
  m_dampingLabel->setVisible( isFrost );
  m_dampingSpin->setVisible( isFrost );
  m_noiseVarSpin->setVisible( !isFrost );
  if ( m_noiseVarLabel )
    m_noiseVarLabel->setVisible( !isFrost );
}

void SpeckleFilterDialog::onRun()
{
  QString sourcePath = m_rasterLayer->source();
  int kernelSize = 3;
  switch ( m_kernelSizeCombo->currentIndex() )
  {
    case 0: kernelSize = 3; break;
    case 1: kernelSize = 5; break;
    case 2: kernelSize = 7; break;
  }
  int filterIndex = m_filterTypeCombo->currentIndex();
  float noiseVar = static_cast<float>( m_noiseVarSpin->value() );
  float damping = static_cast<float>( m_dampingSpin->value() );

  runGdalTask( [sourcePath, outputPath = outputPath(), kernelSize, filterIndex,
                noiseVar, damping]() -> QString {
    try
    {
      GdalDatasetWrapper srcDataset;
      if ( !srcDataset.open( sourcePath ) )
        return QString();
      const int width = srcDataset.width();
      const int height = srcDataset.height();
      const int bandCount = srcDataset.bandCount();

      // Streaming conversion (#691): the previous body materialized
      // outputBands[B] + bandData (B+1 full frames) plus a whole-raster
      // IntegralImage SAT (20 B/px). Now each band is streamed through halo
      // tiles (halo = kernel radius) and written tile-by-tile; the tile
      // kernels in image_enhancement_streaming accumulate the window
      // statistics directly and replicate the lee/frost/kuan/gamma-map
      // formulas of ImageEnhancement's full-frame kernels.
      GdalStreamingOutput dst( outputPath, width, height, bandCount, GDT_Float32,
                               srcDataset.geoTransform(), srcDataset.projection() );
      if ( !dst.isOpen() )
        return QString();

      const int halo = kernelSize / 2;
      for ( int b = 1; b <= bandCount; ++b )
      {
        ImageEnhancementStreaming::WindowedTileFn kernel;
        switch ( filterIndex )
        {
          case 0:
            kernel = [&]( const GdalBlockStream::Tile &tile, const float *buf, float *core ) {
              ImageEnhancementStreaming::speckleTileLee( tile, buf, core, kernelSize, noiseVar );
            };
            break;
          case 1:
            kernel = [&]( const GdalBlockStream::Tile &tile, const float *buf, float *core ) {
              ImageEnhancementStreaming::speckleTileFrost( tile, buf, core, kernelSize, damping );
            };
            break;
          case 2:
            kernel = [&]( const GdalBlockStream::Tile &tile, const float *buf, float *core ) {
              ImageEnhancementStreaming::speckleTileKuan( tile, buf, core, kernelSize, noiseVar );
            };
            break;
          case 3:
            kernel = [&]( const GdalBlockStream::Tile &tile, const float *buf, float *core ) {
              ImageEnhancementStreaming::speckleTileGammaMap( tile, buf, core, kernelSize, noiseVar );
            };
            break;
        }
        if ( !ImageEnhancementStreaming::streamBandWindowed( srcDataset, b, dst,
                                                             ImageEnhancementStreaming::kTileDim,
                                                             halo, kernel ) )
        {
          // Abandon: the destructor/close removes the partial output (#647).
          dst.abandon();
          return QString();
        }
      }
      QString error;
      if ( !dst.closeWithError( &error ) )
        return QString();
      return outputPath;
    }
    catch ( const std::runtime_error & )
    {
      return QString();
    }
  } );
}
