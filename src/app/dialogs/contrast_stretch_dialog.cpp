// src/app/dialogs/contrast_stretch_dialog.cpp
#include "contrast_stretch_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "async_gdal_runner.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"
#include "processing/framework/task_center.h"
#include "app/widgets/histogram_stretch_widget.h"

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

void ContrastStretchDialog::setRasterLayer( QgsRasterLayer *layer )
{
  RasterProcessingDialogBase::setRasterLayer( layer );
  if ( m_stretchWidget && layer )
  {
    m_stretchWidget->setRasterLayer( layer );
  }
}

void ContrastStretchDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );

  setupHelpBanner( mainLayout );

  // Embedded Interactive Photoshop Levels & Histogram Panel
  m_stretchWidget = new HistogramStretchWidget( this );
  mainLayout->addWidget( m_stretchWidget, 1 );

  QFrame *sec = SicnuUi::makeSection(
    this, tr( "预设算法与导出一览" ),
    tr( "选择特定预设方法或直接导出拉伸后的 GeoTIFF 图像。" ) );
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );
  form->setVerticalSpacing( 8 );

  m_methodCombo = new QComboBox( sec );
  m_methodCombo->addItems( { tr( "Photoshop 自定义色阶" ), tr( "线性 (Min-Max)" ), tr( "百分比裁剪" ),
                             tr( "标准差" ), tr( "直方图均衡" ) } );
  SicnuDialogHelp::tip( m_methodCombo, tr(
    "拉伸方法：\n"
    "• Photoshop 色阶：交互调节阴影、高光与 Gamma 中音\n"
    "• 线性：最小–最大\n"
    "• 百分比裁剪：两端裁剪后再拉伸\n"
    "• 标准差：均值±K×标准差\n"
    "• 直方图均衡：增强全局对比" ) );
  connect( m_methodCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ContrastStretchDialog::onMethodChanged );
  form->addRow( tr( "预设方法" ), m_methodCombo );

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

  onMethodChanged( 0 );
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

  QString sourcePath = m_rasterLayer->source();
  int methodIndex = m_methodCombo->currentIndex();
  double clipValue = m_clipSpin->value();
  double stddevValue = m_stddevSpin->value();

  // Enqueue async TaskCenter task
  QVariantMap params;
  params.insert( QStringLiteral( "INPUT" ), sourcePath );
  params.insert( QStringLiteral( "OUTPUT" ), outputPath() );
  params.insert( QStringLiteral( "METHOD" ), methodIndex );
  params.insert( QStringLiteral( "CLIP_PERCENT" ), clipValue );
  params.insert( QStringLiteral( "STDDEV_FACTOR" ), stddevValue );

  long centerTaskId = sicnu::TaskCenter::instance().enqueueTask( QStringLiteral( "gdal:contrast_stretch" ), params, true );

  QVector<QPointF> piecewisePoints = m_stretchWidget ? m_stretchWidget->piecewisePoints() : QVector<QPointF>();
  std::vector<std::pair<float, float>> stdPoints;
  for ( const auto &pt : piecewisePoints )
  {
    stdPoints.emplace_back( static_cast<float>( pt.x() ), static_cast<float>( pt.y() ) );
  }

  runGdalTask( [sourcePath, outputPath = outputPath(), methodIndex, clipValue, stddevValue, stdPoints, centerTaskId]() -> QString {
    try
    {
      sicnu::TaskCenter::instance().markTaskRunning( centerTaskId );
      GdalDatasetWrapper srcDataset;
      if ( !srcDataset.open( sourcePath ) )
      {
        sicnu::TaskCenter::instance().markTaskFailed( centerTaskId, QStringLiteral( "Failed to open GDAL dataset" ) );
        return QString();
      }

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
            ImageEnhancement::piecewiseLinearStretch( allBands[b].data(), outputBands[b].data(),
                                                      pixelCount, stdPoints );
            break;
          case 1:
          case 2:
          {
            float minVal = *std::min_element( allBands[b].begin(), allBands[b].end() );
            float maxVal = *std::max_element( allBands[b].begin(), allBands[b].end() );
            ImageEnhancement::linearStretch( allBands[b].data(), outputBands[b].data(),
                                             pixelCount, minVal, maxVal );
            break;
          }
          case 3:
            ImageEnhancement::percentClipStretch( allBands[b].data(), outputBands[b].data(),
                                                  pixelCount, static_cast<float>( clipValue ) );
            break;
          case 4:
            ImageEnhancement::stddevStretch( allBands[b].data(), outputBands[b].data(),
                                             pixelCount, static_cast<float>( stddevValue ) );
            break;
          case 5:
            ImageEnhancement::histogramEqualize( allBands[b].data(), outputBands[b].data(),
                                                 pixelCount );
            break;
        }
      }

      QString error;
      if ( !writeGdalOutput( outputPath, width, height, outputBands,
                             srcDataset.geoTransform(), srcDataset.projection(), &error ) )
      {
        sicnu::TaskCenter::instance().markTaskFailed( centerTaskId, error );
        return QString();
      }

      QVariantMap res;
      res.insert( QStringLiteral( "OUTPUT" ), outputPath );
      sicnu::TaskCenter::instance().markTaskCompleted( centerTaskId, res );
      return outputPath;
    }
    catch ( const std::runtime_error &e )
    {
      sicnu::TaskCenter::instance().markTaskFailed( centerTaskId, QString::fromUtf8( e.what() ) );
      return QString();
    }
  } );
}
