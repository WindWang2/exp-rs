// src/app/dialogs/spectral_index_dialog.cpp
#include "spectral_index_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "widgets/band_role_combo.h"

#include <raster/qgsrasterlayer.h>

#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QComboBox>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include "data/asset_types.h"
#include "data/band_role.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/processing_asset_resolver.h"
#include "operators/framework/asset_index_pipeline.h"
#include "processing/framework/output_committer.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

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
  populateBandCombos();
}

void SpectralIndexDialog::setDataManager( sicnu::data::DataManager *dataManager )
{
  m_dataManager = dataManager;
  m_inputAssetCombo->setVisible( dataManager != nullptr );
  m_inputAssetLabel->setVisible( dataManager != nullptr );
  if ( dataManager )
    populateInputAssets();
}

void SpectralIndexDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );

  setupHelpBanner( mainLayout );

  // ---- 输入资产 ----
  QFrame *sec = SicnuUi::makeSection(
    this, tr( "参数" ),
    tr( "选择光谱指数并映射传感器波段。波段号从 1 起。" ) );

  m_inputAssetLabel = new QLabel( tr( "输入数据资产" ), sec );
  m_inputAssetCombo = new QComboBox( sec );
  m_inputAssetCombo->setVisible( false );
  m_inputAssetLabel->setVisible( false );
  SicnuDialogHelp::tip( m_inputAssetCombo, tr(
    "选择一个已注册的栅格数据资产作为输入。运行时会校验资产版本；"
    "若版本已变更将拒绝执行。" ) );
  connect( m_inputAssetCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpectralIndexDialog::onInputAssetChanged );
  auto *assetForm = new QFormLayout();
  assetForm->setContentsMargins( 0, 0, 0, 0 );
  assetForm->addRow( m_inputAssetLabel, m_inputAssetCombo );
  qobject_cast<QVBoxLayout *>( sec->layout() )->addLayout( assetForm );

  // ---- 参数：指数类型 + 波段映射 ----
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );
  form->setVerticalSpacing( 8 );

  m_indexCombo = new QComboBox( sec );
  m_indexCombo->addItem( tr( "NDVI — 植被指数" ), QStringLiteral( "NDVI" ) );
  m_indexCombo->addItem( tr( "EVI — 增强植被指数" ), QStringLiteral( "EVI" ) );
  m_indexCombo->addItem( tr( "SAVI — 土壤调节植被指数" ), QStringLiteral( "SAVI" ) );
  m_indexCombo->addItem( tr( "NDWI — 归一化水体指数" ), QStringLiteral( "NDWI" ) );
  m_indexCombo->addItem( tr( "NDBI — 建成区指数" ), QStringLiteral( "NDBI" ) );
  m_indexCombo->addItem( tr( "MNDWI — 改进水体指数" ), QStringLiteral( "MNDWI" ) );
  SicnuDialogHelp::tip( m_indexCombo, tr(
    "光谱指数类型：\n"
    "• NDVI：植被 (NIR,Red)\n• EVI：增强植被 (NIR,Red,Blue)\n"
    "• SAVI：土壤调节植被 (NIR,Red)\n• NDWI：水体 (Green,NIR)\n"
    "• NDBI：建成区 (SWIR,NIR)\n• MNDWI：改进水体 (Green,SWIR)" ) );
  connect( m_indexCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpectralIndexDialog::onIndexChanged );
  form->addRow( tr( "指数" ), m_indexCombo );

  m_nirLabel = new QLabel( tr( "近红外 NIR" ), sec );
  m_nirCombo = new BandRoleCombo( sec );
  m_nirCombo->setObjectName( QStringLiteral( "spectralIndexNirCombo" ) );
  SicnuDialogHelp::tip( m_nirCombo, tr( "近红外波段。Landsat8 常为 5，Sentinel-2 常为 8。" ) );
  form->addRow( m_nirLabel, m_nirCombo );

  m_redLabel = new QLabel( tr( "红光 Red" ), sec );
  m_redCombo = new BandRoleCombo( sec );
  m_redCombo->setObjectName( QStringLiteral( "spectralIndexRedCombo" ) );
  SicnuDialogHelp::tip( m_redCombo, tr( "红光波段。用于 NDVI/EVI/SAVI。" ) );
  form->addRow( m_redLabel, m_redCombo );

  m_greenLabel = new QLabel( tr( "绿光 Green" ), sec );
  m_greenCombo = new BandRoleCombo( sec );
  m_greenCombo->setObjectName( QStringLiteral( "spectralIndexGreenCombo" ) );
  SicnuDialogHelp::tip( m_greenCombo, tr( "绿光波段。用于 NDWI/MNDWI。" ) );
  form->addRow( m_greenLabel, m_greenCombo );

  m_blueLabel = new QLabel( tr( "蓝光 Blue" ), sec );
  m_blueCombo = new BandRoleCombo( sec );
  m_blueCombo->setObjectName( QStringLiteral( "spectralIndexBlueCombo" ) );
  SicnuDialogHelp::tip( m_blueCombo, tr( "蓝光波段。仅 EVI 需要。" ) );
  form->addRow( m_blueLabel, m_blueCombo );

  m_swirLabel = new QLabel( tr( "短波红外 SWIR" ), sec );
  m_swirCombo = new BandRoleCombo( sec );
  m_swirCombo->setObjectName( QStringLiteral( "spectralIndexSwirCombo" ) );
  SicnuDialogHelp::tip( m_swirCombo, tr( "短波红外。用于 NDBI/MNDWI。" ) );
  form->addRow( m_swirLabel, m_swirCombo );

  qobject_cast<QVBoxLayout *>( sec->layout() )->addLayout( form );
  qobject_cast<QVBoxLayout *>( sec->layout() )->addWidget(  SicnuUi::makeHintLabel(
    sec, tr( "已导入的产品按语义波段角色自动匹配；普通栅格按 Landsat/Sentinel 常见顺序预填，"
             "请按实际数据核对波段。" ) ) );
  mainLayout->addWidget( sec );

  setupOutputRow( mainLayout );

  m_addToCanvasCheck = new QCheckBox( tr( "完成后将结果加入画布（可选）" ), this );
  mainLayout->addWidget( m_addToCanvasCheck );

  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  updateBandVisibility();
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
    QMessageBox::warning( this, dialogTitle(), tr( "请选择一个有效的输入数据资产。" ) );
    return;
  }
  const auto snapshot = m_dataManager->asset( *assetId );
  if ( !snapshot )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "所选资产已不存在。" ) );
    return;
  }

  const QString inputPath = snapshot->source().canonicalSource;
  if ( inputPath.isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "所选资产无有效数据路径。" ) );
    return;
  }

  const sicnu::operators::SpectralIndexParams params = buildSpectralIndexParams(
    m_indexCombo, m_nirCombo, m_redCombo, m_greenCombo, m_blueCombo, m_swirCombo );

  Json::Value json( Json::objectValue );
  json["input"] = inputPath.toStdString();
  json["output"] = outputPath().toStdString();
  json["index"] = params.index.toStdString();
  json["nir"] = params.nir;
  json["red"] = params.red;
  json["green"] = params.green;
  json["blue"] = params.blue;
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
  json["nir"] = params.nir;
  json["red"] = params.red;
  json["green"] = params.green;
  json["blue"] = params.blue;
  json["swir"] = params.swir;

  runOperatorTask( QStringLiteral( "rs:spectral_index" ), json );
}

