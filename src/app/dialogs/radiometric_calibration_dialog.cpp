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
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "输入数据" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "radiometricInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "选择待执行辐射定标的栅格图层。" ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &RadiometricCalibrationDialog::onLayerChanged );
  inputForm->addRow( tr( "输入栅格" ), m_layerCombo );
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Calibration Parameters Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "定标参数" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_unitCombo = new QComboBox( paramGroup );
  m_unitCombo->addItem( tr( "辐射亮度 (Radiance)" ), QStringLiteral( "radiance" ) );
  m_unitCombo->addItem( tr( "TOA 表观反射率" ), QStringLiteral( "toa_reflectance" ) );
  m_unitCombo->addItem( tr( "亮温 (K)" ), QStringLiteral( "brightness_temperature" ) );
  SicnuDialogHelp::tip( m_unitCombo, tr(
    "• 辐射亮度：L = gain×DN + bias\n"
    "• TOA 反射率：Landsat (reflMult×DN+add)/sin(sun)；S2 (DN+offset)/scale\n"
    "• 亮温：需热红外波段 K1/K2 常数" ) );
  form->addRow( tr( "输出物理量" ), m_unitCombo );

  m_allBandsCheck = new QCheckBox( tr( "处理全部有效波段" ), paramGroup );
  m_allBandsCheck->setChecked( true );
  SicnuDialogHelp::tip( m_allBandsCheck, tr( "勾选时自动对输入影像的所有有效波段执行定标；取消勾选可指定单个目标波段。" ) );
  connect( m_allBandsCheck, &QCheckBox::toggled, this, &RadiometricCalibrationDialog::onAllBandsToggled );
  form->addRow( QString(), m_allBandsCheck );

  m_bandCombo = new QComboBox( paramGroup );
  SicnuDialogHelp::tip( m_bandCombo, tr( "选择待定标的单一目标波段号。" ) );
  form->addRow( tr( "目标波段" ), m_bandCombo );
  m_bandLabel = qobject_cast<QLabel *>( form->labelForField( m_bandCombo ) );

  auto *metadataRow = new QHBoxLayout;
  metadataRow->setContentsMargins( 0, 0, 0, 0 );
  metadataRow->setSpacing( 8 );
  m_metadataEdit = new QLineEdit( paramGroup );
  m_metadataEdit->setPlaceholderText( tr( "自动探测（输入栅格旁 *_MTL.txt / MTD_MSI*.xml）" ) );
  SicnuDialogHelp::tip( m_metadataEdit, tr( "Landsat *_MTL.txt 或 Sentinel-2 MTD_MSI*.xml 路径；留空则自动探测。" ) );
  m_metadataBrowseButton = new QPushButton( tr( "浏览…" ), paramGroup );
  m_metadataBrowseButton->setFixedWidth( 76 );
  SicnuUi::markSecondary( m_metadataBrowseButton );
  SicnuDialogHelp::tip( m_metadataBrowseButton, tr( "浏览并指定传感器元数据文件" ) );
  connect( m_metadataBrowseButton, &QPushButton::clicked, this,
           &RadiometricCalibrationDialog::onBrowseMetadata );
  metadataRow->addWidget( m_metadataEdit, 1 );
  metadataRow->addWidget( m_metadataBrowseButton );
  form->addRow( tr( "元数据文件" ), metadataRow );

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
    m_bandCombo->addItem( tr( "波段 %1" ).arg( i ), i );
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
    this, tr( "选择传感器元数据文件" ), m_metadataEdit->text(),
    tr( "Landsat MTL (*_MTL.txt);;Sentinel-2 MTD (MTD_MSI*.xml);;所有文件 (*)" ) );
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
      tr( "未找到传感器元数据文件；将回退到栅格内嵌 GDAL scale/offset。" ) );
    return;
  }

  RadiometricCalibration::CalibrationMetadata meta;
  QString error;
  if ( !RadiometricCalibration::loadMetadata( m_rasterLayer->source(), metadataPath,
                                              {}, &meta, &error ) )
  {
    m_metadataStatusLabel->setText(
      tr( "已探测到 %1，但解析失败：%2" ).arg( QFileInfo( metadataPath ).fileName(), error ) );
    return;
  }

  QStringList parts;
  parts.append( tr( "%1 个波段" ).arg( meta.bands.size() ) );
  if ( !meta.spacecraft.isEmpty() )
    parts.append( tr( "平台 %1" ).arg( meta.spacecraft ) );
  if ( !meta.processingLevel.isEmpty() )
    parts.append( tr( "级别 %1" ).arg( meta.processingLevel ) );
  if ( meta.sunElevationDeg > 0.0 && meta.sunElevationDeg < 90.0 )
    parts.append( tr( "太阳高度 %1°" ).arg( meta.sunElevationDeg, 0, 'f', 1 ) );
  m_metadataStatusLabel->setText(
    tr( "使用 %1：%2。" ).arg( QFileInfo( metadataPath ).fileName(),
                              parts.join( QStringLiteral( "，" ) ) ) );
}

void RadiometricCalibrationDialog::onRun()
{
  if ( !m_rasterLayer || !m_rasterLayer->isValid() )
  {
    handleFailed( tr( "请先选择一个有效的栅格图层。" ) );
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
