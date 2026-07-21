// extract_band_dialog.cpp — Extract single band from multi-band raster
#include "extract_band_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "async_gdal_runner.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_conv.h>

#include "processing/gdal/gdal_safe_call.h"

ExtractBandDialog::ExtractBandDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( tr( "提取波段" ) );
  setupUi();
}

void ExtractBandDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QFrame *sec = SicnuUi::makeSection(
    this, tr( "输入" ),
    tr( "从多波段栅格中抽取一个波段保存为单波段文件。" ) );
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );

  m_layerCombo = new QComboBox( sec );
  SicnuDialogHelp::tip( m_layerCombo, tr( "工程中的多波段栅格（波段数>1）。" ) );
  form->addRow( tr( "栅格图层" ), m_layerCombo );

  m_bandCombo = new QComboBox( sec );
  SicnuDialogHelp::tip( m_bandCombo, tr( "要导出的波段。" ) );
  form->addRow( tr( "波段" ), m_bandCombo );

  qobject_cast<QVBoxLayout *>( sec->layout() )->addLayout( form );
  mainLayout->addWidget( sec );
  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ExtractBandDialog::onLayerChanged );

  populateRasterLayerCombo( m_layerCombo );
  for ( int i = m_layerCombo->count() - 1; i >= 0; --i )
  {
    auto *rl = m_layerCombo->itemData( i ).value<QgsRasterLayer *>();
    if ( !rl || rl->bandCount() <= 1 )
      m_layerCombo->removeItem( i );
  }
  if ( m_layerCombo->count() > 0 )
    populateBandCombo();
}

void ExtractBandDialog::populateBandCombo()
{
  m_bandCombo->clear();
  auto *rl = m_layerCombo->currentData().value<QgsRasterLayer *>();
  if ( !rl )
    return;
  for ( int i = 1; i <= rl->bandCount(); ++i )
  {
    QString bandName = rl->bandName( i );
    if ( bandName.isEmpty() )
      bandName = tr( "波段 %1" ).arg( i );
    m_bandCombo->addItem( bandName, i );
  }
}

void ExtractBandDialog::onLayerChanged()
{
  populateBandCombo();
}

void ExtractBandDialog::onRun()
{
  auto *rl = m_layerCombo->currentData().value<QgsRasterLayer *>();
  if ( !rl )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请选择栅格图层。" ) );
    return;
  }

  int bandIndex = m_bandCombo->currentData().toInt();
  if ( bandIndex < 1 )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请选择要提取的波段。" ) );
    return;
  }

  QString outPath = outputPath();
  if ( outPath.isEmpty() )
  {
    QString inputPath = rl->source();
    outPath = QFileInfo( inputPath ).path() + QLatin1Char( '/' )
              + QFileInfo( inputPath ).baseName()
              + tr( "_band%1.tif" ).arg( bandIndex );
    m_outputEdit->setText( outPath );
  }

  setRasterLayer( rl );
  QString sourcePath = rl->source();

  runGdalTask( [sourcePath, bandIndex, outPath]() -> QString {
    try
    {
      GdalDatasetGuard srcGuard( GDALOpen( sourcePath.toUtf8().constData(), GA_ReadOnly ) );
      if ( !srcGuard )
        return QString();

      int w = GDALGetRasterXSize( srcGuard.get() );
      int h = GDALGetRasterYSize( srcGuard.get() );

      GDALRasterBandH srcBand = GDALGetRasterBand( srcGuard.get(), bandIndex );
      if ( !srcBand )
        return QString();

      std::vector<float> buf( static_cast<size_t>( w ) * h );
      GDAL_SAFE_CALL( GDALRasterIO( srcBand, GF_Read, 0, 0, w, h, buf.data(), w, h, GDT_Float32, 0, 0 ),
                      "Failed to read band data" );

      GeoInfo geo = extractGeoInfo( srcGuard.get() );
      std::vector<std::vector<float>> bands = { buf };
      QString error;
      if ( !writeGdalOutput( outPath, w, h, bands, geo.geoTransform, geo.projection, &error ) )
        return QString();

      return outPath;
    }
    catch ( const std::runtime_error & )
    {
      return QString();
    }
  } );
}
