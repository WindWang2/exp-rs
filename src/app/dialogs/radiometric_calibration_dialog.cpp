// src/app/dialogs/radiometric_calibration_dialog.cpp
#include "radiometric_calibration_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "widgets/raster_layer_combo.h"

#include <raster/qgsrasterlayer.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "processing/algorithms/radiometric_calibration.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

RadiometricCalibrationDialog::RadiometricCalibrationDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setupUi();
}

void RadiometricCalibrationDialog::setRasterLayer( QgsRasterLayer *layer )
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
  refreshMetadataStatus();
}

void RadiometricCalibrationDialog::onLayerChanged( int /*index*/ )
{
  if ( m_layerCombo )
  {
    auto *layer = m_layerCombo->currentRasterLayer();
    if ( layer && layer != m_rasterLayer )
      setRasterLayer( layer );
  }
}

void RadiometricCalibrationDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input Data Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "Input Data" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "radiometricInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "Select the raster layer for radiometric calibration." ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &RadiometricCalibrationDialog::onLayerChanged );
  inputForm->addRow( tr( "Input Raster" ), m_layerCombo );
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Calibration Parameters Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "Calibration Parameters" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_unitCombo = new QComboBox( paramGroup );
  m_unitCombo->addItem( tr( "Radiance" ), QStringLiteral( "radiance" ) );
  m_unitCombo->addItem( tr( "TOA Apparent Reflectance" ), QStringLiteral( "toa_reflectance" ) );
  m_unitCombo->addItem( tr( "Brightness Temperature (K)" ), QStringLiteral( "brightness_temperature" ) );
  SicnuDialogHelp::tip( m_unitCombo, tr(
    "• Radiance: L = gain×DN + bias\n"
    "• TOA reflectance: Landsat (reflMult×DN+add)/sin(sun); S2 (DN+offset)/scale\n"
    "• Brightness temperature: needs the thermal band K1/K2 constants")  );
  form->addRow( tr( "Output Physical Quantity" ), m_unitCombo );

  m_allBandsCheck = new QCheckBox( tr( "Process All Valid Bands" ), paramGroup );
  m_allBandsCheck->setChecked( true );
  SicnuDialogHelp::tip( m_allBandsCheck, tr( "When ticked, all valid bands of the input image are calibrated automatically; untick to calibrate a single target band." ) );
  connect( m_allBandsCheck, &QCheckBox::toggled, this, &RadiometricCalibrationDialog::onAllBandsToggled );
  form->addRow( QString(), m_allBandsCheck );

  m_bandCombo = new QComboBox( paramGroup );
  SicnuDialogHelp::tip( m_bandCombo, tr( "Chooses the single target band number to calibrate." ) );
  form->addRow( tr( "Target Band" ), m_bandCombo );
  m_bandLabel = qobject_cast<QLabel *>( form->labelForField( m_bandCombo ) );

  auto *metadataRow = new QHBoxLayout;
  metadataRow->setContentsMargins( 0, 0, 0, 0 );
  metadataRow->setSpacing( 8 );
  m_metadataEdit = new QLineEdit( paramGroup );
  m_metadataEdit->setPlaceholderText( tr( "Auto-detect (*_MTL.txt / MTD_MSI*.xml next to the input raster)" ) );
  SicnuDialogHelp::tip( m_metadataEdit, tr( "Path to a Landsat *_MTL.txt or Sentinel-2 MTD_MSI*.xml; auto-detected if left empty." ) );
  m_metadataBrowseButton = new QPushButton( tr( "Browse..." ), paramGroup );
  m_metadataBrowseButton->setFixedWidth( 76 );
  SicnuUi::markSecondary( m_metadataBrowseButton );
  SicnuDialogHelp::tip( m_metadataBrowseButton, tr( "Browse and choose the sensor metadata file" ) );
  connect( m_metadataBrowseButton, &QPushButton::clicked, this,
           &RadiometricCalibrationDialog::onBrowseMetadata );
  metadataRow->addWidget( m_metadataEdit, 1 );
  metadataRow->addWidget( m_metadataBrowseButton );
  form->addRow( tr( "Metadata File" ), metadataRow );

  m_metadataStatusLabel = SicnuUi::makeHintLabel( paramGroup, QString() );
  m_metadataStatusLabel->setWordWrap( true );
  form->addRow( QString(), m_metadataStatusLabel );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  onAllBandsToggled( true );

  if ( m_rasterLayer )
    m_layerCombo->selectLayer( m_rasterLayer->id() );
  else if ( m_layerCombo->count() > 0 )
    setRasterLayer( m_layerCombo->currentRasterLayer() );
}

void RadiometricCalibrationDialog::populateBandCombo()
{
  if ( !m_rasterLayer || !m_rasterLayer->isValid() )
    return;
  m_bandCombo->clear();
  const int bandCount = m_rasterLayer->bandCount();
  for ( int i = 1; i <= bandCount; ++i )
    m_bandCombo->addItem( tr( "Band %1" ).arg( i ), i );
}

void RadiometricCalibrationDialog::onAllBandsToggled( bool checked )
{
  const bool visible = !checked && m_rasterLayer && m_rasterLayer->bandCount() > 0;
  m_bandCombo->setVisible( visible );
  m_bandLabel->setVisible( visible );
}

void RadiometricCalibrationDialog::onBrowseMetadata()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Select Sensor Metadata File" ), m_metadataEdit->text(),
    tr( "Landsat MTL (*_MTL.txt);;Sentinel-2 MTD (MTD_MSI*.xml);;All Files (*)" ) );
  if ( path.isEmpty() )
    return;
  m_metadataEdit->setText( path );
  refreshMetadataStatus();
}

QString RadiometricCalibrationDialog::resolvedMetadataPath() const
{
  const QString explicitPath = m_metadataEdit->text().trimmed();
  if ( !explicitPath.isEmpty() )
    return explicitPath;
  if ( m_rasterLayer && m_rasterLayer->isValid() )
    return RadiometricCalibration::autoDetectMetadataFile( m_rasterLayer->source() );
  return {};
}

void RadiometricCalibrationDialog::refreshMetadataStatus()
{
  if ( !m_rasterLayer || !m_rasterLayer->isValid() )
  {
    m_metadataStatusLabel->clear();
    return;
  }

  const QString metadataPath = resolvedMetadataPath();
  if ( metadataPath.isEmpty() )
  {
    m_metadataStatusLabel->setText(
      tr( "Sensor metadata file not found; falling back to the raster's embedded GDAL scale/offset." ) );
    return;
  }

  RadiometricCalibration::CalibrationMetadata meta;
  QString error;
  if ( !RadiometricCalibration::loadMetadata( m_rasterLayer->source(), metadataPath,
                                              {}, &meta, &error ) )
  {
    m_metadataStatusLabel->setText(
      tr( "Detected %1, but parsing failed: %2" ).arg( QFileInfo( metadataPath ).fileName(), error ) );
    return;
  }

  QStringList parts;
  parts.append( tr( "%1 bands" ).arg( meta.bands.size() ) );
  if ( !meta.spacecraft.isEmpty() )
    parts.append( tr( "Platform %1" ).arg( meta.spacecraft ) );
  if ( !meta.processingLevel.isEmpty() )
    parts.append( tr( "Level %1" ).arg( meta.processingLevel ) );
  if ( meta.sunElevationDeg > 0.0 && meta.sunElevationDeg < 90.0 )
    parts.append( tr( "Sun elevation %1°" ).arg( meta.sunElevationDeg, 0, 'f', 1 ) );
  m_metadataStatusLabel->setText(
    tr( "Using %1: %2." ).arg( QFileInfo( metadataPath ).fileName(),
                              parts.join( QStringLiteral( ", " ) ) ) );
}

void RadiometricCalibrationDialog::onRun()
{
  if ( !m_rasterLayer || !m_rasterLayer->isValid() )
  {
    handleFailed( tr( "Select a valid raster layer first." ) );
    return;
  }

  Json::Value json( Json::objectValue );
  json["input"] = m_rasterLayer->source().toStdString();
  json["output"] = outputPath().toStdString();
  json["unit"] = m_unitCombo->currentData().toString().toStdString();

  const QString metadataPath = resolvedMetadataPath();
  if ( !metadataPath.isEmpty() )
    json["metadata_path"] = metadataPath.toStdString();

  if ( !m_allBandsCheck->isChecked() )
  {
    Json::Value bands( Json::arrayValue );
    bands.append( m_bandCombo->currentData().toInt() );
    json["bands"] = bands;
  }

  runOperatorTask( QStringLiteral( "rs:radiometric_calibration" ), json );
}
