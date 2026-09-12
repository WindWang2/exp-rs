// src/app/dialogs/band_ratio_dialog.cpp
#include "band_ratio_dialog.h"
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
#include <QMessageBox>

BandRatioDialog::BandRatioDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setupUi();
}

void BandRatioDialog::setRasterLayer( QgsRasterLayer *layer )
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
  populateBandCombos();
}

void BandRatioDialog::onLayerChanged( int /*index*/ )
{
  if ( m_layerCombo )
  {
    auto *layer = m_layerCombo->currentRasterLayer();
    if ( layer && layer != m_rasterLayer )
      setRasterLayer( layer );
  }
}

void BandRatioDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input Data Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "Input Data" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "bandRatioInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "Select the raster layer for band math." ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &BandRatioDialog::onLayerChanged );
  inputForm->addRow( tr( "Input Raster" ), m_layerCombo );
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Parameters Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "Operation Parameters" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_modeCombo = new QComboBox( paramGroup );
  m_modeCombo->addItems( { tr( "Band Ratio" ), tr( "IHS Color Transform" ) } );
  SicnuDialogHelp::tip( m_modeCombo, tr(
    tr("• Band ratio: numerator band ÷ denominator band\n• IHS transform: converts the three RGB bands into intensity, hue and saturation") ) );
  connect( m_modeCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &BandRatioDialog::onModeChanged );
  form->addRow( tr( "Operation Mode" ), m_modeCombo );

  m_band1Label = new QLabel( tr( "Numerator Band" ), paramGroup );
  m_band1Combo = new BandRoleCombo( paramGroup );
  SicnuDialogHelp::tip( m_band1Combo, tr( "Numerator band of the ratio." ) );
  form->addRow( m_band1Label, m_band1Combo );

  m_band2Label = new QLabel( tr( "Denominator Band" ), paramGroup );
  m_band2Combo = new BandRoleCombo( paramGroup );
  SicnuDialogHelp::tip( m_band2Combo, tr( "Denominator band of the ratio (must not be all zeros)." ) );
  form->addRow( m_band2Label, m_band2Combo );

  m_redLabel = new QLabel( tr( "Red Band (R)" ), paramGroup );
  m_redCombo = new BandRoleCombo( paramGroup );
  SicnuDialogHelp::tip( m_redCombo, tr( "Red component band of the IHS transform." ) );
  form->addRow( m_redLabel, m_redCombo );

  m_greenLabel = new QLabel( tr( "Green Band (G)" ), paramGroup );
  m_greenCombo = new BandRoleCombo( paramGroup );
  SicnuDialogHelp::tip( m_greenCombo, tr( "Green component band of the IHS transform." ) );
  form->addRow( m_greenLabel, m_greenCombo );

  m_blueLabel = new QLabel( tr( "Blue Band (B)" ), paramGroup );
  m_blueCombo = new BandRoleCombo( paramGroup );
  SicnuDialogHelp::tip( m_blueCombo, tr( "Blue component band of the IHS transform." ) );
  form->addRow( m_blueLabel, m_blueCombo );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  onModeChanged( 0 );

  if ( m_rasterLayer )
    m_layerCombo->selectLayer( m_rasterLayer->id() );
  else if ( m_layerCombo->count() > 0 )
    setRasterLayer( m_layerCombo->currentRasterLayer() );
}

void BandRatioDialog::populateBandCombos()
{
  if ( !m_rasterLayer || !m_rasterLayer->isValid() )
    return;

  const QString sourcePath = m_rasterLayer->source();
  const int bandCount = m_rasterLayer->bandCount();

  m_band1Combo->setRaster( sourcePath );
  m_band2Combo->setRaster( sourcePath );
  m_redCombo->setRaster( sourcePath );
  m_greenCombo->setRaster( sourcePath );
  m_blueCombo->setRaster( sourcePath );

  m_band1Combo->selectBandByRole( sicnu::data::BandRole::NIR );
  if ( m_band1Combo->selectedBand() == 0 && bandCount >= 1 )
    m_band1Combo->setCurrentIndex( 1 );

  m_band2Combo->selectBandByRole( sicnu::data::BandRole::Red );
  if ( m_band2Combo->selectedBand() == 0 && bandCount >= 2 )
    m_band2Combo->setCurrentIndex( 2 );

  m_redCombo->selectBandByRole( sicnu::data::BandRole::Red );
  if ( m_redCombo->selectedBand() == 0 && bandCount >= 1 )
    m_redCombo->setCurrentIndex( 1 );

  m_greenCombo->selectBandByRole( sicnu::data::BandRole::Green );
  if ( m_greenCombo->selectedBand() == 0 && bandCount >= 2 )
    m_greenCombo->setCurrentIndex( 2 );

  m_blueCombo->selectBandByRole( sicnu::data::BandRole::Blue );
  if ( m_blueCombo->selectedBand() == 0 && bandCount >= 3 )
    m_blueCombo->setCurrentIndex( 3 );
}

void BandRatioDialog::onModeChanged( int index )
{
  bool isRatio = ( index == 0 );
  m_band1Label->setVisible( isRatio );
  m_band1Combo->setVisible( isRatio );
  m_band2Label->setVisible( isRatio );
  m_band2Combo->setVisible( isRatio );
  m_redLabel->setVisible( !isRatio );
  m_redCombo->setVisible( !isRatio );
  m_greenLabel->setVisible( !isRatio );
  m_greenCombo->setVisible( !isRatio );
  m_blueLabel->setVisible( !isRatio );
  m_blueCombo->setVisible( !isRatio );
}

void BandRatioDialog::onRun()
{
  if ( !m_rasterLayer )
    return;

  QString sourcePath = m_rasterLayer->source();
  int modeIndex = m_modeCombo->currentIndex();
  int band1Num = m_band1Combo->currentData().toInt();
  int band2Num = m_band2Combo->currentData().toInt();
  int redNum = m_redCombo->currentData().toInt();
  int greenNum = m_greenCombo->currentData().toInt();
  int blueNum = m_blueCombo->currentData().toInt();

  if ( modeIndex == 0 )
  {
    if ( band1Num < 1 || band2Num < 1 || band1Num == band2Num )
    {
      QMessageBox::warning( this, dialogTitle(), tr( "Select two different valid bands for the band ratio." ) );
      return;
    }
  }
  else
  {
    if ( redNum < 1 || greenNum < 1 || blueNum < 1 )
    {
      QMessageBox::warning( this, dialogTitle(), tr( "Select valid RGB bands for the IHS transform." ) );
      return;
    }
  }

  // Thin client: the kernel runs as the rs:band_ratio operator through the
  // Task Center — same execution path as CLI/MCP.
  Json::Value params( Json::objectValue );
  params["input"] = sourcePath.toStdString();
  params["output"] = outputPath().toStdString();
  if ( modeIndex == 0 )
  {
    params["mode"] = "ratio";
    params["numeratorBand"] = band1Num;
    params["denominatorBand"] = band2Num;
  }
  else
  {
    params["mode"] = "ihs";
    params["redBand"] = redNum;
    params["greenBand"] = greenNum;
    params["blueBand"] = blueNum;
  }
  runOperatorTask( QStringLiteral( "rs:band_ratio" ), params );
}
