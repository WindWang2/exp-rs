// extract_band_dialog.cpp — Extract single band from multi-band raster
#include "extract_band_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "widgets/band_role_combo.h"
#include "widgets/raster_layer_combo.h"

#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
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
  setWindowTitle( dialogTitle() );
  setMinimumWidth( 520 );
  setupUi();
}

void ExtractBandDialog::setRasterLayer( QgsRasterLayer *layer )
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
  populateBandCombo();
}

void ExtractBandDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input & Band Selection Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "输入与波段选择" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "extractBandInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "工程中的多波段栅格图层。" ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ExtractBandDialog::onLayerChanged );
  form->addRow( tr( "栅格图层" ), m_layerCombo );

  m_bandCombo = new BandRoleCombo( inputGroup );
  m_bandCombo->setObjectName( QStringLiteral( "extractBandRoleCombo" ) );
  SicnuDialogHelp::tip( m_bandCombo, tr( "选择待抽取并单独导出的目标波段。" ) );
  form->addRow( tr( "目标波段" ), m_bandCombo );

  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( form );
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addWidget( SicnuUi::makeHintLabel(
    inputGroup, tr( "提示：从多波段栅格中抽取单一波段并另存为独立的单波段 GeoTIFF 影像。" ) ) );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  if ( m_rasterLayer )
    m_layerCombo->selectLayer( m_rasterLayer->id() );
  else if ( m_layerCombo->count() > 0 )
    setRasterLayer( m_layerCombo->currentRasterLayer() );
}

void ExtractBandDialog::populateBandCombo()
{
  if ( !m_bandCombo )
    return;
  auto *rl = m_layerCombo ? m_layerCombo->currentRasterLayer() : m_rasterLayer;
  if ( !rl || !rl->isValid() )
  {
    m_bandCombo->clear();
    return;
  }
  m_bandCombo->setRaster( rl->source() );
  if ( m_bandCombo->count() > 1 && m_bandCombo->currentIndex() <= 0 )
    m_bandCombo->setCurrentIndex( 1 );
}

void ExtractBandDialog::onLayerChanged( int /*index*/ )
{
  if ( m_layerCombo )
  {
    auto *layer = m_layerCombo->currentRasterLayer();
    if ( layer && layer != m_rasterLayer )
      setRasterLayer( layer );
  }
}

bool ExtractBandDialog::validateInputs()
{
  auto *rl = m_layerCombo ? m_layerCombo->currentRasterLayer() : m_rasterLayer;
  if ( !rl || !rl->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请选择有效的栅格图层。" ) );
    return false;
  }

  int bandIndex = m_bandCombo ? m_bandCombo->currentData().toInt() : 0;
  if ( bandIndex < 1 )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请选择要提取的目标波段。" ) );
    return false;
  }

  setRasterLayer( rl );

  QString outPath = outputPath();
  if ( outPath.isEmpty() )
  {
    QString inputPath = rl->source();
    outPath = QFileInfo( inputPath ).path() + QLatin1Char( '/' )
              + QFileInfo( inputPath ).completeBaseName()
              + tr( "_band%1.tif" ).arg( bandIndex );
    if ( m_outputEdit )
      m_outputEdit->setText( outPath );
  }

  return true;
}

void ExtractBandDialog::onRun()
{
  auto *rl = m_layerCombo ? m_layerCombo->currentRasterLayer() : m_rasterLayer;
  if ( !rl )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请选择栅格图层。" ) );
    return;
  }

  int bandIndex = m_bandCombo ? m_bandCombo->currentData().toInt() : 0;
  if ( bandIndex < 1 )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请选择要提取的目标波段。" ) );
    return;
  }

  QString outPath = outputPath();
  if ( outPath.isEmpty() )
  {
    QString inputPath = rl->source();
    outPath = QFileInfo( inputPath ).path() + QLatin1Char( '/' )
              + QFileInfo( inputPath ).baseName()
              + tr( "_band%1.tif" ).arg( bandIndex );
    if ( m_outputEdit )
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
