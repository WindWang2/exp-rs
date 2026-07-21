// src/app/dialogs/contrast_stretch_dialog.cpp
#include "contrast_stretch_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "async_gdal_runner.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>

#include <gdal.h>
#include <cpl_error.h>

ContrastStretchDialog::ContrastStretchDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setupUi();
}

void ContrastStretchDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );

  setupHelpBanner( mainLayout );

  QFrame *sec = SicnuUi::makeSection(
    this, tr( "拉伸参数" ),
    tr( "选择拉伸方法；百分比裁剪与标准差法会显示额外参数。" ) );
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );
  form->setVerticalSpacing( 8 );

  m_methodCombo = new QComboBox( sec );
  m_methodCombo->addItems( { tr( "线性 (Min-Max)" ), tr( "百分比裁剪" ),
                             tr( "标准差" ), tr( "直方图均衡" ) } );
  SicnuDialogHelp::tip( m_methodCombo, tr(
    "拉伸方法：\n"
    "• 线性：最小–最大\n"
    "• 百分比裁剪：两端裁剪后再拉伸\n"
    "• 标准差：均值±K×标准差\n"
    "• 直方图均衡：增强全局对比" ) );
  connect( m_methodCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ContrastStretchDialog::onMethodChanged );
  form->addRow( tr( "方法" ), m_methodCombo );

  m_clipLabel = new QLabel( tr( "裁剪比例" ), sec );
  m_clipSpin = new QDoubleSpinBox( sec );
  m_clipSpin->setRange( 0.1, 50.0 );
  m_clipSpin->setValue( 2.0 );
  m_clipSpin->setSingleStep( 0.5 );
  m_clipSpin->setDecimals( 1 );
  m_clipSpin->setSuffix( QStringLiteral( " %" ) );
  SicnuDialogHelp::tip( m_clipSpin, tr( "两端各舍弃该比例像元后再拉伸。常用 1–2%。" ) );
  form->addRow( m_clipLabel, m_clipSpin );

  m_stddevLabel = new QLabel( tr( "标准差倍数 K" ), sec );
  m_stddevSpin = new QDoubleSpinBox( sec );
  m_stddevSpin->setRange( 0.1, 10.0 );
  m_stddevSpin->setValue( 2.0 );
  m_stddevSpin->setSingleStep( 0.5 );
  m_stddevSpin->setDecimals( 1 );
  SicnuDialogHelp::tip( m_stddevSpin, tr( "拉伸到 mean±K·σ。常用 2。" ) );
  form->addRow( m_stddevLabel, m_stddevSpin );

  qobject_cast<QVBoxLayout *>( sec->layout() )->addLayout( form );
  mainLayout->addWidget( sec );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  onMethodChanged( 0 );
}

void ContrastStretchDialog::onMethodChanged( int index )
{
  m_clipLabel->setVisible( index == 1 );
  m_clipSpin->setVisible( index == 1 );
  m_stddevLabel->setVisible( index == 2 );
  m_stddevSpin->setVisible( index == 2 );
}

void ContrastStretchDialog::onRun()
{
  QString sourcePath = m_rasterLayer->source();
  int methodIndex = m_methodCombo->currentIndex();
  double clipValue = m_clipSpin->value();
  double stddevValue = m_stddevSpin->value();

  runGdalTask( [sourcePath, outputPath = outputPath(), methodIndex, clipValue, stddevValue]() -> QString {
    try
    {
      GdalDatasetWrapper srcDataset;
      if ( !srcDataset.open( sourcePath ) )
        return QString();

      int width = srcDataset.width();
      int height = srcDataset.height();
      int bandCount = srcDataset.bandCount();
      size_t pixelCount = static_cast<size_t>( width ) * static_cast<size_t>( height );

      std::vector<std::vector<float>> allBands( bandCount, std::vector<float>( pixelCount ) );
      for ( int b = 0; b < bandCount; ++b )
      {
        if ( !srcDataset.readBandData( b + 1, allBands[b].data(), width, height ) )
          return QString();
      }

      std::vector<std::vector<float>> outputBands( bandCount, std::vector<float>( pixelCount ) );

      for ( int b = 0; b < bandCount; ++b )
      {
        switch ( methodIndex )
        {
          case 0:
          {
            float minVal = *std::min_element( allBands[b].begin(), allBands[b].end() );
            float maxVal = *std::max_element( allBands[b].begin(), allBands[b].end() );
            ImageEnhancement::linearStretch( allBands[b].data(), outputBands[b].data(),
                                             pixelCount, minVal, maxVal );
            break;
          }
          case 1:
            ImageEnhancement::percentClipStretch( allBands[b].data(), outputBands[b].data(),
                                                  pixelCount, static_cast<float>( clipValue ) );
            break;
          case 2:
            ImageEnhancement::stddevStretch( allBands[b].data(), outputBands[b].data(),
                                             pixelCount, static_cast<float>( stddevValue ) );
            break;
          case 3:
            ImageEnhancement::histogramEqualize( allBands[b].data(), outputBands[b].data(),
                                                 pixelCount );
            break;
        }
      }

      QString error;
      if ( !writeGdalOutput( outputPath, width, height, outputBands,
                             srcDataset.geoTransform(), srcDataset.projection(), &error ) )
        return QString();

      return outputPath;
    }
    catch ( const std::runtime_error & )
    {
      return QString();
    }
  } );
}
