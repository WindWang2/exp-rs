// src/app/dialogs/spectral_index_dialog.cpp
#include "spectral_index_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "widgets/band_role_combo.h"
#include "widgets/raster_layer_combo.h"

#include <raster/qgsrasterlayer.h>

#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QMessageBox>
#include "data/asset_types.h"
#include "data/band_role.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "operators/framework/asset_index_pipeline.h"

namespace
{

} // namespace

SpectralIndexDialog::SpectralIndexDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setupUi();
}

void SpectralIndexDialog::setRasterLayer( QgsRasterLayer *layer )
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

void SpectralIndexDialog::onLayerChanged( int /*index*/ )
{
  if ( m_layerCombo )
  {
    auto *layer = m_layerCombo->currentRasterLayer();
    if ( layer && layer != m_rasterLayer )
      setRasterLayer( layer );
  }
}

void SpectralIndexDialog::setDataManager( sicnu::data::DataManager *dataManager )
{
  m_dataManager = dataManager;
  const bool hasDm = ( dataManager != nullptr );
  m_inputAssetCombo->setVisible( hasDm );
  m_inputAssetLabel->setVisible( hasDm );
  if ( m_layerCombo )
    m_layerCombo->setVisible( !hasDm );
  if ( m_layerLabel )
    m_layerLabel->setVisible( !hasDm );
  if ( dataManager )
    populateInputAssets();
}

void SpectralIndexDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "Input Data" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerLabel = new QLabel( tr( "Input Raster" ), inputGroup );
  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "spectralIndexInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "Select the raster layer for the spectral index." ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpectralIndexDialog::onLayerChanged );
  inputForm->addRow( m_layerLabel, m_layerCombo );

  m_inputAssetLabel = new QLabel( tr( "Data Assets" ), inputGroup );
  m_inputAssetCombo = new QComboBox( inputGroup );
  m_inputAssetCombo->setObjectName( QStringLiteral( "spectralIndexAssetCombo" ) );
  m_inputAssetCombo->setVisible( false );
  m_inputAssetLabel->setVisible( false );
  SicnuDialogHelp::tip( m_inputAssetCombo, tr(
    "Chooses a registered raster data asset as input. The asset version is validated at run time; execution is refused if the version has changed.")  );
  connect( m_inputAssetCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpectralIndexDialog::onInputAssetChanged );
  inputForm->addRow( m_inputAssetLabel, m_inputAssetCombo );

  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Parameter Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "Index and Band Mapping" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_indexCombo = new QComboBox( paramGroup );
  m_indexCombo->addItem( tr( "NDVI — Normalized Difference Vegetation Index" ), QStringLiteral( "NDVI" ) );
  m_indexCombo->addItem( tr( "EVI — Enhanced Vegetation Index" ), QStringLiteral( "EVI" ) );
  m_indexCombo->addItem( tr( "SAVI — Soil-Adjusted Vegetation Index" ), QStringLiteral( "SAVI" ) );
  m_indexCombo->addItem( tr( "NDWI — Normalized Difference Water Index" ), QStringLiteral( "NDWI" ) );
  m_indexCombo->addItem( tr( "NDBI — Normalized Difference Built-up Index" ), QStringLiteral( "NDBI" ) );
  m_indexCombo->addItem( tr( "MNDWI — Modified Normalized Difference Water Index" ), QStringLiteral( "MNDWI" ) );
  SicnuDialogHelp::tip( m_indexCombo, tr(
    "Spectral index type:\n"
    "• NDVI: vegetation (NIR, Red)\n"
    "• EVI: enhanced vegetation (NIR, Red, Blue)\n"
    "• SAVI: soil-adjusted vegetation (NIR, Red)\n"
    "• NDWI: water (Green, NIR)\n"
    "• NDBI: built-up (SWIR, NIR)\n"
    "• MNDWI: modified water (Green, SWIR)")  );
  connect( m_indexCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpectralIndexDialog::onIndexChanged );
  form->addRow( tr( "Index Type" ), m_indexCombo );

  m_nirLabel = new QLabel( tr( "NIR (Near Infrared)" ), paramGroup );
  m_nirCombo = new BandRoleCombo( paramGroup );
  m_nirCombo->setObjectName( QStringLiteral( "spectralIndexNirCombo" ) );
  SicnuDialogHelp::tip( m_nirCombo, tr( "Near-infrared band; usually Band 5 on Landsat 8/9 and Band 8 on Sentinel-2." ) );
  form->addRow( m_nirLabel, m_nirCombo );

  m_redLabel = new QLabel( tr( "Red" ), paramGroup );
  m_redCombo = new BandRoleCombo( paramGroup );
  m_redCombo->setObjectName( QStringLiteral( "spectralIndexRedCombo" ) );
  SicnuDialogHelp::tip( m_redCombo, tr( "Red band; used by NDVI/EVI/SAVI." ) );
  form->addRow( m_redLabel, m_redCombo );

  m_greenLabel = new QLabel( tr( "Green" ), paramGroup );
  m_greenCombo = new BandRoleCombo( paramGroup );
  m_greenCombo->setObjectName( QStringLiteral( "spectralIndexGreenCombo" ) );
  SicnuDialogHelp::tip( m_greenCombo, tr( "Green band; used by NDWI/MNDWI." ) );
  form->addRow( m_greenLabel, m_greenCombo );

  m_blueLabel = new QLabel( tr( "Blue" ), paramGroup );
  m_blueCombo = new BandRoleCombo( paramGroup );
  m_blueCombo->setObjectName( QStringLiteral( "spectralIndexBlueCombo" ) );
  SicnuDialogHelp::tip( m_blueCombo, tr( "Blue band; used for the atmospheric background correction in EVI." ) );
  form->addRow( m_blueLabel, m_blueCombo );

  m_swirLabel = new QLabel( tr( "SWIR (Shortwave Infrared)" ), paramGroup );
  m_swirCombo = new BandRoleCombo( paramGroup );
  m_swirCombo->setObjectName( QStringLiteral( "spectralIndexSwirCombo" ) );
  SicnuDialogHelp::tip( m_swirCombo, tr( "Shortwave infrared band; used by NDBI/MNDWI." ) );
  form->addRow( m_swirLabel, m_swirCombo );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );
  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addWidget( SicnuUi::makeHintLabel(
    paramGroup, tr( "Imported products are matched by semantic band role automatically; plain rasters are pre-filled in the common band order — verify before running." ) ) );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  updateBandVisibility();

  if ( m_rasterLayer )
    m_layerCombo->selectLayer( m_rasterLayer->id() );
  else if ( m_layerCombo->count() > 0 )
    setRasterLayer( m_layerCombo->currentRasterLayer() );
}

void SpectralIndexDialog::populateInputAssets()
{
  if ( !m_dataManager )
    return;

  m_inputAssetCombo->blockSignals( true );
  m_inputAssetCombo->clear();

  sicnu::data::AssetQuery query;
  query.kind = sicnu::data::AssetKind::Raster;
  for ( const sicnu::data::AssetSnapshot &snapshot : m_dataManager->assets( query ) )
    m_inputAssetCombo->addItem( snapshot.displayName(), snapshot.id().toString() );
  m_inputAssetCombo->blockSignals( false );

  if ( m_inputAssetCombo->count() > 0 )
  {
    onInputAssetChanged( 0 );
    m_inputAssetCombo->setCurrentIndex( 0 );
  }
}

void SpectralIndexDialog::onInputAssetChanged( int comboIndex )
{
  populateBandCombos();
}

int SpectralIndexDialog::inputBandCount() const
{
  // Asset path: read band count from the selected asset's resolved source.
  if ( m_dataManager && m_inputAssetCombo->isVisible() && m_inputAssetCombo->count() > 0 )
  {
    const QString idText = m_inputAssetCombo->currentData().toString();
    const auto assetId = sicnu::data::AssetId::fromString( idText );
    if ( assetId )
    {
      const auto snapshot = m_dataManager->asset( *assetId );
      if ( snapshot )
      {
        GdalDatasetWrapper ds;
        if ( ds.open( snapshot->source().canonicalSource ) )
          return ds.bandCount();
      }
    }
    return 0;
  }

  // Layer fallback path.
  if ( m_rasterLayer && m_rasterLayer->isValid() )
    return m_rasterLayer->bandCount();
  return 0;
}

QString SpectralIndexDialog::inputRasterPath() const
{
  if ( m_dataManager && m_inputAssetCombo->isVisible() && m_inputAssetCombo->count() > 0 )
  {
    const QString idText = m_inputAssetCombo->currentData().toString();
    const auto assetId = sicnu::data::AssetId::fromString( idText );
    if ( assetId )
    {
      const auto snapshot = m_dataManager->asset( *assetId );
      if ( snapshot )
        return snapshot->source().canonicalSource;
    }
    return {};
  }
  if ( m_rasterLayer && m_rasterLayer->isValid() )
    return m_rasterLayer->source();
  return {};
}

void SpectralIndexDialog::populateBandCombos()
{
  const QString path = inputRasterPath();
  const int bandCount = inputBandCount();
  if ( bandCount <= 0 || path.isEmpty() )
    return;

  // Shared band-role selector (C5, ADR 0102): each combo lists the bands
  // labeled with their semantic role plus an "自动" item. Role-based
  // preselection below; plain rasters fall back to the positional mapping.
  m_nirCombo->setRaster( path );
  m_redCombo->setRaster( path );
  m_greenCombo->setRaster( path );
  m_blueCombo->setRaster( path );
  m_swirCombo->setRaster( path );

  // BandRoleCombo items: index 0 = auto, index b = band b.
  auto positional = []( BandRoleCombo *combo, int bandNumber, int count ) {
    combo->setCurrentIndex( bandNumber <= count ? bandNumber : 1 );
  };
  auto selectWithFallback = [&]( BandRoleCombo *combo, sicnu::data::BandRole role,
                                 int positionalBand ) {
    combo->selectBandByRole( role );
    if ( combo->selectedBand() == 0 )
      positional( combo, positionalBand, bandCount );
  };

  // Default Landsat/Sentinel-style positional mapping (band 4/3/2/1, SWIR 5).
  selectWithFallback( m_nirCombo, sicnu::data::BandRole::NIR, 4 );
  selectWithFallback( m_redCombo, sicnu::data::BandRole::Red, 3 );
  selectWithFallback( m_greenCombo, sicnu::data::BandRole::Green, 2 );
  selectWithFallback( m_blueCombo, sicnu::data::BandRole::Blue, 1 );
  // NDBI/MNDWI conventionally use SWIR1; fall back to SWIR2, then positional.
  m_swirCombo->selectBandByRole( sicnu::data::BandRole::SWIR1 );
  if ( m_swirCombo->selectedBand() == 0 )
    m_swirCombo->selectBandByRole( sicnu::data::BandRole::SWIR2 );
  if ( m_swirCombo->selectedBand() == 0 )
    positional( m_swirCombo, 5, bandCount );
}

void SpectralIndexDialog::updateBandVisibility()
{
  const int index = m_indexCombo->currentIndex();

  m_nirCombo->setVisible( true );
  m_nirLabel->setVisible( true );

  m_redCombo->setVisible( index == 0 || index == 1 || index == 2 );
  m_redLabel->setVisible( index == 0 || index == 1 || index == 2 );

  m_greenCombo->setVisible( index == 3 || index == 5 );
  m_greenLabel->setVisible( index == 3 || index == 5 );

  m_blueCombo->setVisible( index == 1 );
  m_blueLabel->setVisible( index == 1 );

  m_swirCombo->setVisible( index == 4 || index == 5 );
  m_swirLabel->setVisible( index == 4 || index == 5 );
}

void SpectralIndexDialog::onIndexChanged( int /*index*/ )
{
  updateBandVisibility();
}

void SpectralIndexDialog::onRun()
{
  // Asset path: when a Data Manager is set and an asset is selected, run
  // through the resolver + committer seams.
  if ( m_dataManager && m_inputAssetCombo->isVisible() && m_inputAssetCombo->count() > 0 )
  {
    runFromAsset();
    return;
  }
  runFromLayer();
}

/// Builds the spectral-index parameters from the shared band combo widgets.
static sicnu::operators::SpectralIndexParams buildSpectralIndexParams(
  QComboBox *indexCombo, QComboBox *nirCombo, QComboBox *redCombo,
  QComboBox *greenCombo, QComboBox *blueCombo, QComboBox *swirCombo )
{
  sicnu::operators::SpectralIndexParams params;
  params.index = indexCombo->currentData().toString();
  params.nir = nirCombo->currentData().toInt();
  params.red = redCombo->currentData().toInt();
  params.green = greenCombo->currentData().toInt();
  params.blue = blueCombo->currentData().toInt();
  params.swir = swirCombo->currentData().toInt();
  return params;
}

void SpectralIndexDialog::runFromAsset()
{
  const QString idText = m_inputAssetCombo->currentData().toString();
  const auto assetId = sicnu::data::AssetId::fromString( idText );
  if ( !assetId )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Select a valid input data asset." ) );
    return;
  }
  const auto snapshot = m_dataManager->asset( *assetId );
  if ( !snapshot )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "The selected asset no longer exists." ) );
    return;
  }

  const QString inputPath = snapshot->source().canonicalSource;
  if ( inputPath.isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "The selected asset has no valid data path." ) );
    return;
  }

  const sicnu::operators::SpectralIndexParams params = buildSpectralIndexParams(
    m_indexCombo, m_nirCombo, m_redCombo, m_greenCombo, m_blueCombo, m_swirCombo );

  Json::Value json( Json::objectValue );
  json["input"] = inputPath.toStdString();
  json["output"] = outputPath().toStdString();
  json["index"] = params.index.toStdString();
  // Omit auto (0) so operator resolves via SICNU_BAND_ROLE metadata.
  if ( params.nir > 0 )
    json["nir"] = params.nir;
  if ( params.red > 0 )
    json["red"] = params.red;
  if ( params.green > 0 )
    json["green"] = params.green;
  if ( params.blue > 0 )
    json["blue"] = params.blue;
  if ( params.swir > 0 )
    json["swir"] = params.swir;

  runOperatorTask( QStringLiteral( "rs:spectral_index" ), json );
}

void SpectralIndexDialog::runFromLayer()
{
  const sicnu::operators::SpectralIndexParams params = buildSpectralIndexParams(
    m_indexCombo, m_nirCombo, m_redCombo, m_greenCombo, m_blueCombo, m_swirCombo );

  Json::Value json( Json::objectValue );
  json["input"] = m_rasterLayer->source().toStdString();
  json["output"] = outputPath().toStdString();
  json["index"] = params.index.toStdString();
  if ( params.nir > 0 )
    json["nir"] = params.nir;
  if ( params.red > 0 )
    json["red"] = params.red;
  if ( params.green > 0 )
    json["green"] = params.green;
  if ( params.blue > 0 )
    json["blue"] = params.blue;
  if ( params.swir > 0 )
    json["swir"] = params.swir;

  runOperatorTask( QStringLiteral( "rs:spectral_index" ), json );
}

