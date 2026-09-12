// src/app/dialogs/qa_mask_dialog.cpp — QA / cloud / shadow / snow mask dialog
#include "qa_mask_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "widgets/band_role_combo.h"
#include "widgets/raster_layer_combo.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>

namespace
{

} // namespace

QaMaskDialog::QaMaskDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setShouldAutoAcceptOnSuccess( false );
  setupUi();
}

void QaMaskDialog::setRasterLayer( QgsRasterLayer *layer )
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
  if ( layer && m_bandCombo )
  {
    m_bandCombo->setRaster( layer->source() );
    // Preselect the semantic QA band (scene classification preferred).
    m_bandCombo->selectBandByRole( sicnu::data::BandRole::SceneClassification );
    if ( m_bandCombo->selectedBand() == 0 )
      m_bandCombo->selectBandByRole( sicnu::data::BandRole::QA );
  }
}

void QaMaskDialog::onLayerChanged( int /*index*/ )
{
  if ( m_layerCombo )
  {
    auto *layer = m_layerCombo->currentRasterLayer();
    if ( layer && layer != m_rasterLayer )
      setRasterLayer( layer );
  }
}

void QaMaskDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input Data Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "Input Data" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "qaMaskInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "Select the product raster layer to mask." ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &QaMaskDialog::onLayerChanged );
  inputForm->addRow( tr( "Input Raster" ), m_layerCombo );

  m_bandCombo = new BandRoleCombo( inputGroup );
  SicnuDialogHelp::tip( m_bandCombo, tr(
    tr("Quality band. Chosen automatically by product semantic role by default (SCL → scene classification, QA → quality).") ) );
  inputForm->addRow( tr( "Quality Band" ), m_bandCombo );

  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Mask Parameters Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "Mask Parameters" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_sourceCombo = new QComboBox( paramGroup );
  m_sourceCombo->addItem( tr( "Auto-detect" ), QStringLiteral( "auto" ) );
  m_sourceCombo->addItem( tr( "Landsat QA_PIXEL Bit Flags" ), QStringLiteral( "landsat_qa_pixel" ) );
  m_sourceCombo->addItem( tr( "Sentinel-2 SCL Classes" ), QStringLiteral( "sentinel2_scl" ) );
  m_sourceCombo->addItem( tr( "Generic Bit Mask" ), QStringLiteral( "generic_bitmask" ) );
  SicnuDialogHelp::tip( m_sourceCombo, tr(
    tr("• Auto: identify by band role / name (SCL → Sentinel-2; QA → Landsat)\n")
    tr("• Landsat QA_PIXEL: Collection 2 bit flags\n")
    tr("• Sentinel-2 SCL: by scene classification classes\n")
    tr("• Generic bitmask: decided bit by bit from the bits parameter") ) );
  connect( m_sourceCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &QaMaskDialog::onSourceChanged );
  form->addRow( tr( "Quality Source" ), m_sourceCombo );

  m_maskCombo = new QComboBox( paramGroup );
  m_maskCombo->addItem( tr( "Cloud + cloud shadow (recommended)" ), QStringLiteral( "cloud_and_shadow" ) );
  m_maskCombo->addItem( tr( "Cloud only (incl. thin cirrus)" ), QStringLiteral( "cloud" ) );
  m_maskCombo->addItem( tr( "Cloud shadow only" ), QStringLiteral( "cloud_shadow" ) );
  m_maskCombo->addItem( tr( "Snow" ), QStringLiteral( "snow" ) );
  m_maskCombo->addItem( tr( "Water" ), QStringLiteral( "water" ) );
  m_maskCombo->addItem( tr( "All invalid/occluded classes" ), QStringLiteral( "all" ) );
  SicnuDialogHelp::tip( m_maskCombo, tr(
    tr("Choose the classes to turn into the mask.\n")
    tr("• Landsat: cloud = bits 1/2/3 (dilated cloud / cirrus / cloud), shadow = bit 4, snow = bit 5, water = bit 7\n")
    tr("• Sentinel-2 SCL: cloud = classes 8/9/10, shadow = 3, snow = 11, water = 6") ) );
  form->addRow( tr( "Mask Classes" ), m_maskCombo );

  m_bitsSpin = new QSpinBox( paramGroup );
  m_bitsSpin->setRange( 1, 65535 );
  m_bitsSpin->setValue( 1 );
  m_bitsSpin->setToolTip( tr( "Generic bit mask: pixels where (value & bits) != 0 are masked." ) );
  form->addRow( tr( "Bit Flags (generic)" ), m_bitsSpin );
  m_bitsLabel = qobject_cast<QLabel *>( form->labelForField( m_bitsSpin ) );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

  setupOutputRow( mainLayout );

  // Summary Group
  QGroupBox *summaryGroup = SicnuUi::makeGroup( this, tr( "Mask Statistics" ) );
  auto *summaryLayout = new QVBoxLayout( summaryGroup );
  summaryLayout->setContentsMargins( 12, 10, 12, 10 );
  m_summaryLabel = SicnuUi::makeHintLabel( summaryGroup, tr( "After running, mask statistics appear here." ) );
  m_summaryLabel->setObjectName( QStringLiteral( "qaMaskSummaryLabel" ) );
  m_summaryLabel->setWordWrap( true );
  summaryLayout->addWidget( m_summaryLabel );
  mainLayout->addWidget( summaryGroup );

  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  onSourceChanged( 0 );

  if ( m_rasterLayer )
    m_layerCombo->selectLayer( m_rasterLayer->id() );
  else if ( m_layerCombo->count() > 0 )
    setRasterLayer( m_layerCombo->currentRasterLayer() );
}

void QaMaskDialog::onSourceChanged( int /*index*/ )
{
  const QString source = m_sourceCombo->currentData().toString();
  const bool generic = source == QStringLiteral( "generic_bitmask" );
  m_bitsSpin->setVisible( generic );
  if ( m_bitsLabel )
    m_bitsLabel->setVisible( generic );
}

void QaMaskDialog::onRun()
{
  if ( !m_rasterLayer || !m_rasterLayer->isValid() )
  {
    handleFailed( tr( "Select a valid raster layer first." ) );
    return;
  }

  Json::Value json( Json::objectValue );
  json["input"] = m_rasterLayer->source().toStdString();
  json["output"] = outputPath().toStdString();
  json["source"] = m_sourceCombo->currentData().toString().toStdString();
  json["mask"] = m_maskCombo->currentData().toString().toStdString();
  const int qaBand = m_bandCombo->currentData().toInt();
  if ( qaBand > 0 )
    json["qa_band"] = qaBand;
  if ( json["source"].asString() == "generic_bitmask" )
    json["bits"] = m_bitsSpin->value();

  runOperatorTask( QStringLiteral( "rs:qa_mask" ), json,
                   [this]( const Json::Value &result ) {
                     if ( m_summaryLabel && result.isMember( "maskedPercent" ) )
                       m_summaryLabel->setText(
                         tr( "Masked pixels: %1 / %2 (%3%)" )
                           .arg( result["maskedPixels"].asUInt64() )
                           .arg( result["totalPixels"].asUInt64() )
                           .arg( result["maskedPercent"].asDouble(), 0, 'f', 2 ) );
                   } );
}
