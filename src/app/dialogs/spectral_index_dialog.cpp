// src/app/dialogs/spectral_index_dialog.cpp
#include "spectral_index_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

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

/// Map of band number -> semantic role read from a raster's SICNU_BAND_ROLE
/// product metadata (written by product stacking). Empty for plain rasters.
QMap<int, sicnu::data::BandRole> readBandRoles( const QString &rasterPath, int bandCount )
{
  QMap<int, sicnu::data::BandRole> roles;
  GdalDatasetWrapper ds;
  if ( bandCount <= 0 || !ds.open( rasterPath ) )
    return roles;
  const int count = qMin( bandCount, ds.bandCount() );
  for ( int b = 1; b <= count; ++b )
  {
    const QString id = ds.bandMetadataItem( b, "SICNU_BAND_ROLE" );
    if ( !id.isEmpty() )
      roles.insert( b, sicnu::data::bandRoleFromString( id ) );
  }
  return roles;
}

/// 0-based combo index of the first band carrying @a role, or -1 when absent.
int comboIndexForRole( const QMap<int, sicnu::data::BandRole> &roles,
                       sicnu::data::BandRole role )
{
  for ( auto it = roles.constBegin(); it != roles.constEnd(); ++it )
  {
    if ( it.value() == role )
      return it.key() - 1;
  }
  return -1;
}

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
  m_nirCombo = new QComboBox( sec );
  SicnuDialogHelp::tip( m_nirCombo, tr( "近红外波段。Landsat8 常为 5，Sentinel-2 常为 8。" ) );
  form->addRow( m_nirLabel, m_nirCombo );

  m_redLabel = new QLabel( tr( "红光 Red" ), sec );
  m_redCombo = new QComboBox( sec );
  SicnuDialogHelp::tip( m_redCombo, tr( "红光波段。用于 NDVI/EVI/SAVI。" ) );
  form->addRow( m_redLabel, m_redCombo );

  m_greenLabel = new QLabel( tr( "绿光 Green" ), sec );
  m_greenCombo = new QComboBox( sec );
  SicnuDialogHelp::tip( m_greenCombo, tr( "绿光波段。用于 NDWI/MNDWI。" ) );
  form->addRow( m_greenLabel, m_greenCombo );

  m_blueLabel = new QLabel( tr( "蓝光 Blue" ), sec );
  m_blueCombo = new QComboBox( sec );
  SicnuDialogHelp::tip( m_blueCombo, tr( "蓝光波段。仅 EVI 需要。" ) );
  form->addRow( m_blueLabel, m_blueCombo );

  m_swirLabel = new QLabel( tr( "短波红外 SWIR" ), sec );
  m_swirCombo = new QComboBox( sec );
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
  const int bandCount = inputBandCount();
  if ( bandCount <= 0 )
    return;

  m_nirCombo->clear();
  m_redCombo->clear();
  m_greenCombo->clear();
  m_blueCombo->clear();
  m_swirCombo->clear();

  // Semantic roles of the input's bands (band number -> role), read from the
  // SICNU_BAND_ROLE product metadata written by product stacking. Empty for
  // plain rasters, which fall back to the legacy positional mapping.
  const QMap<int, sicnu::data::BandRole> roleByBand =
    readBandRoles( inputRasterPath(), bandCount );

  for ( int i = 1; i <= bandCount; ++i )
  {
    QString bandName = tr( "波段 %1" ).arg( i );
    const auto roleIt = roleByBand.constFind( i );
    if ( roleIt != roleByBand.constEnd() && *roleIt != sicnu::data::BandRole::Unknown )
    {
      const QString roleName = sicnu::data::bandRoleDisplayName( *roleIt );
      if ( !roleName.isEmpty() )
        bandName = tr( "波段 %1 (%2)" ).arg( i ).arg( roleName );
    }
    m_nirCombo->addItem( bandName, i );
    m_redCombo->addItem( bandName, i );
    m_greenCombo->addItem( bandName, i );
    m_blueCombo->addItem( bandName, i );
    m_swirCombo->addItem( bandName, i );
  }

  if ( roleByBand.isEmpty() )
  {
    // Default Landsat/Sentinel-style positional mapping
    if ( bandCount >= 4 )
    {
      m_nirCombo->setCurrentIndex( 3 );
      m_redCombo->setCurrentIndex( 2 );
      m_greenCombo->setCurrentIndex( 1 );
      m_blueCombo->setCurrentIndex( 0 );
    }
    if ( bandCount >= 5 )
      m_swirCombo->setCurrentIndex( 4 );
    return;
  }

  auto selectRoleIn = [&]( QComboBox *combo, sicnu::data::BandRole role, int fallbackIndex ) {
    const int index = comboIndexForRole( roleByBand, role );
    combo->setCurrentIndex( index >= 0 ? index : fallbackIndex );
  };

  selectRoleIn( m_nirCombo, sicnu::data::BandRole::NIR, bandCount >= 4 ? 3 : 0 );
  selectRoleIn( m_redCombo, sicnu::data::BandRole::Red, bandCount >= 4 ? 2 : 0 );
  selectRoleIn( m_greenCombo, sicnu::data::BandRole::Green, bandCount >= 4 ? 1 : 0 );
  selectRoleIn( m_blueCombo, sicnu::data::BandRole::Blue, bandCount >= 4 ? 0 : 0 );
  // NDBI/MNDWI conventionally use SWIR1; fall back to SWIR2.
  int swirIndex = comboIndexForRole( roleByBand, sicnu::data::BandRole::SWIR1 );
  if ( swirIndex < 0 )
    swirIndex = comboIndexForRole( roleByBand, sicnu::data::BandRole::SWIR2 );
  m_swirCombo->setCurrentIndex( swirIndex >= 0 ? swirIndex : ( bandCount >= 5 ? 4 : 0 ) );
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

  const sicnu::operators::SpectralIndexParams params = buildSpectralIndexParams(
    m_indexCombo, m_nirCombo, m_redCombo, m_greenCombo, m_blueCombo, m_swirCombo );

  // The operator writes to a temporary file; the committer publishes it to the
  // user's chosen stable path.
  const QString stablePath = outputPath();
  QTemporaryFile tempFile( QStringLiteral( "spectral_index_XXXXXX.tif" ) );
  tempFile.setAutoRemove( false );
  if ( !tempFile.open() )
  {
    handleFailed( tr( "无法创建临时输出文件。" ) );
    return;
  }
  const QString tempPath = tempFile.fileName();
  tempFile.close();
  QFile::remove( tempPath ); // the operator writes a fresh GeoTIFF

  sicnu::operators::StableOutputSpec output;
  output.tempPath = tempPath;
  output.stablePath = stablePath;
  // Display is the user's opt-in decision; when checked, the committer emits
  // displayRequested and the dialog loads the result on success.
  output.autoLoad = m_addToCanvasCheck->isChecked();

  startRun();

  sicnu::data::ProcessingAssetResolver resolver( m_dataManager );
  sicnu::OutputCommitter committer( m_dataManager );

  // The committer's displayRequested is the opt-in display seam. On fire,
  // surface the stable path so the host (openRasterDialog) loads it.
  connect( &committer, &sicnu::OutputCommitter::displayRequested, this,
           [this]( sicnu::data::AssetId ) { m_outputEdit->setText( outputPath() ); } );

  const auto result = sicnu::operators::runSpectralIndexFromAsset(
    sicnu::data::AssetRef{ *assetId, snapshot->revision() }, params, output,
    resolver, committer );

  if ( !result )
  {
    const QString message =
      result.diagnostics().isEmpty()
        ? tr( "光谱指数计算失败。" )
        : result.diagnostics().first().message;
    handleFailed( message );
    return;
  }

  handleCompleted( stablePath );
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

