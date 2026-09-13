// extract_band_dialog.cpp — Extract single band from multi-band raster
#include "extract_band_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
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
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "Inputs and Band Selection" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "extractBandInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "A multiband raster layer from the project." ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ExtractBandDialog::onLayerChanged );
  form->addRow( tr( "Raster Layer" ), m_layerCombo );

  m_bandCombo = new BandRoleCombo( inputGroup );
  m_bandCombo->setObjectName( QStringLiteral( "extractBandRoleCombo" ) );
  SicnuDialogHelp::tip( m_bandCombo, tr( "Chooses the target band to extract and export separately." ) );
  form->addRow( tr( "Target Band" ), m_bandCombo );

  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( form );
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addWidget( SicnuUi::makeHintLabel(
    inputGroup, tr( "Tip: extracts a single band from a multiband raster and saves it as a standalone single-band GeoTIFF." ) ) );

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
    QMessageBox::warning( this, dialogTitle(), tr( "Select a valid raster layer." ) );
    return false;
  }

  int bandIndex = m_bandCombo ? m_bandCombo->currentData().toInt() : 0;
  if ( bandIndex < 1 )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Select the target band to extract." ) );
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
    QMessageBox::warning( this, dialogTitle(), tr( "Select a raster layer." ) );
    return;
  }

  int bandIndex = m_bandCombo ? m_bandCombo->currentData().toInt() : 0;
  if ( bandIndex < 1 )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Select the target band to extract." ) );
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

  // Thin client: extraction runs as the rs:extract_bands operator through the
  // Task Center — same execution path as CLI/MCP.
  Json::Value params( Json::objectValue );
  params["input"] = rl->source().toStdString();
  params["output"] = outPath.toStdString();
  params["bands"] = Json::Value( Json::arrayValue );
  params["bands"].append( bandIndex );
  runOperatorTask( QStringLiteral( "rs:extract_bands" ), params );
}
