// src/app/dialogs/speckle_filter_dialog.cpp
#include "speckle_filter_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QFrame>
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

void SpeckleFilterDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QFrame *sec = SicnuUi::makeSection(
    this, tr( "滤波参数" ),
    tr( "SAR 斑点抑制。窗口越大越平滑；Frost 用阻尼，其余用噪声方差。" ) );
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );
  form->setVerticalSpacing( 8 );

  m_filterTypeCombo = new QComboBox( sec );
  m_filterTypeCombo->addItems( { tr( "Lee" ), tr( "Frost" ), tr( "Kuan" ), tr( "Gamma-MAP" ) } );
  SicnuDialogHelp::tip( m_filterTypeCombo, tr( "Lee / Frost / Kuan / Gamma-MAP。" ) );
  form->addRow( tr( "滤波器" ), m_filterTypeCombo );

  m_kernelSizeCombo = new QComboBox( sec );
  m_kernelSizeCombo->addItems( { tr( "3×3" ), tr( "5×5" ), tr( "7×7" ) } );
  m_kernelSizeCombo->setCurrentIndex( 1 );
  SicnuDialogHelp::tip( m_kernelSizeCombo, tr( "滤波窗口。3×3 保细节，7×7 更平滑。" ) );
  form->addRow( tr( "窗口" ), m_kernelSizeCombo );

  m_noiseVarLabel = new QLabel( tr( "噪声方差" ), sec );
  m_noiseVarSpin = new QDoubleSpinBox( sec );
  m_noiseVarSpin->setRange( 0.001, 10.0 );
  m_noiseVarSpin->setValue( 1.0 );
  m_noiseVarSpin->setSingleStep( 0.1 );
  m_noiseVarSpin->setDecimals( 3 );
  SicnuDialogHelp::tip( m_noiseVarSpin, tr( "Lee/Kuan/Gamma-MAP 噪声方差。" ) );
  form->addRow( m_noiseVarLabel, m_noiseVarSpin );

  m_dampingLabel = new QLabel( tr( "阻尼因子" ), sec );
  m_dampingSpin = new QDoubleSpinBox( sec );
  m_dampingSpin->setRange( 0.1, 10.0 );
  m_dampingSpin->setValue( 2.0 );
  m_dampingSpin->setSingleStep( 0.5 );
  m_dampingSpin->setDecimals( 1 );
  SicnuDialogHelp::tip( m_dampingSpin, tr( "Frost 阻尼：越大越平滑。" ) );
  form->addRow( m_dampingLabel, m_dampingSpin );

  qobject_cast<QVBoxLayout *>( sec->layout() )->addLayout( form );
  mainLayout->addWidget( sec );
  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  connect( m_filterTypeCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpeckleFilterDialog::onFilterTypeChanged );
  onFilterTypeChanged( 0 );
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
      int width = srcDataset.width();
      int height = srcDataset.height();
      int bandCount = srcDataset.bandCount();
      size_t pixelCount = static_cast<size_t>( width ) * static_cast<size_t>( height );
      std::vector<std::vector<float>> outputBands( bandCount, std::vector<float>( pixelCount ) );
      for ( int b = 0; b < bandCount; ++b )
      {
        std::vector<float> bandData( pixelCount );
        if ( !srcDataset.readBandData( b + 1, bandData.data(), width, height ) )
          return QString();
        switch ( filterIndex )
        {
          case 0:
            ImageEnhancement::leeFilter( bandData.data(), outputBands[b].data(),
                                         width, height, kernelSize, noiseVar );
            break;
          case 1:
            ImageEnhancement::frostFilter( bandData.data(), outputBands[b].data(),
                                           width, height, kernelSize, damping );
            break;
          case 2:
            ImageEnhancement::kuanFilter( bandData.data(), outputBands[b].data(),
                                          width, height, kernelSize, noiseVar );
            break;
          case 3:
            ImageEnhancement::gammaMapFilter( bandData.data(), outputBands[b].data(),
                                              width, height, kernelSize, noiseVar );
            break;
        }
      }
      QString error;
      GdalDatasetGuard dstGuard( createOutputTiff( outputPath, width, height, bandCount,
                                                   GDT_Float32, srcDataset.geoTransform(),
                                                   srcDataset.projection(), &error ) );
      if ( !dstGuard )
        return QString();
      for ( int b = 0; b < bandCount; ++b )
      {
        GDALRasterBandH dstBand = GDALGetRasterBand( dstGuard.get(), b + 1 );
        if ( !dstBand )
          return QString();
        GDAL_SAFE_CALL( GDALRasterIO( dstBand, GF_Write, 0, 0, width, height,
                                      outputBands[b].data(), width, height, GDT_Float32, 0, 0 ),
                        "Failed to write output band" );
      }
      return outputPath;
    }
    catch ( const std::runtime_error & )
    {
      return QString();
    }
  } );
}
