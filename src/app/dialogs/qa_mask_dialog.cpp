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
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "输入数据" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "qaMaskInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "选择待提取掩膜的产品栅格图层。" ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &QaMaskDialog::onLayerChanged );
  inputForm->addRow( tr( "输入栅格" ), m_layerCombo );

  m_bandCombo = new BandRoleCombo( inputGroup );
  SicnuDialogHelp::tip( m_bandCombo, tr(
    "质量波段。默认按产品语义角色自动选择（SCL → 场景分类，QA → 质量）。" ) );
  inputForm->addRow( tr( "质量波段" ), m_bandCombo );

  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Mask Parameters Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "掩膜参数" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_sourceCombo = new QComboBox( paramGroup );
  m_sourceCombo->addItem( tr( "自动识别" ), QStringLiteral( "auto" ) );
  m_sourceCombo->addItem( tr( "Landsat QA_PIXEL 位标志" ), QStringLiteral( "landsat_qa_pixel" ) );
  m_sourceCombo->addItem( tr( "Sentinel-2 SCL 类别" ), QStringLiteral( "sentinel2_scl" ) );
  m_sourceCombo->addItem( tr( "通用位掩码" ), QStringLiteral( "generic_bitmask" ) );
  SicnuDialogHelp::tip( m_sourceCombo, tr(
    "• 自动：按波段角色/名称识别（SCL → Sentinel-2；QA → Landsat）\n"
    "• Landsat QA_PIXEL：按 Collection 2 位标志\n"
    "• Sentinel-2 SCL：按场景分类类别\n"
    "• 通用位掩码：按 bits 参数逐位判断" ) );
  connect( m_sourceCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &QaMaskDialog::onSourceChanged );
  form->addRow( tr( "质量源" ), m_sourceCombo );

  m_maskCombo = new QComboBox( paramGroup );
  m_maskCombo->addItem( tr( "云 + 云影（推荐）" ), QStringLiteral( "cloud_and_shadow" ) );
  m_maskCombo->addItem( tr( "仅云（含薄卷云）" ), QStringLiteral( "cloud" ) );
  m_maskCombo->addItem( tr( "仅云影" ), QStringLiteral( "cloud_shadow" ) );
  m_maskCombo->addItem( tr( "雪" ), QStringLiteral( "snow" ) );
  m_maskCombo->addItem( tr( "水体" ), QStringLiteral( "water" ) );
  m_maskCombo->addItem( tr( "全部无效/遮挡类别" ), QStringLiteral( "all" ) );
  SicnuDialogHelp::tip( m_maskCombo, tr(
    "选择要置为掩膜的类别。\n"
    "• Landsat：云=bit1/2/3（膨胀云/卷云/云），云影=bit4，雪=bit5，水体=bit7\n"
    "• Sentinel-2 SCL：云=类别 8/9/10，云影=3，雪=11，水体=6" ) );
  form->addRow( tr( "掩膜类别" ), m_maskCombo );

  m_bitsSpin = new QSpinBox( paramGroup );
  m_bitsSpin->setRange( 1, 65535 );
  m_bitsSpin->setValue( 1 );
  m_bitsSpin->setToolTip( tr( "通用位掩码：值为 (value & bits) != 0 的像素被掩膜。" ) );
  form->addRow( tr( "位标志 (通用)" ), m_bitsSpin );
  m_bitsLabel = qobject_cast<QLabel *>( form->labelForField( m_bitsSpin ) );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

  setupOutputRow( mainLayout );

  // Summary Group
  QGroupBox *summaryGroup = SicnuUi::makeGroup( this, tr( "掩膜统计" ) );
  auto *summaryLayout = new QVBoxLayout( summaryGroup );
  summaryLayout->setContentsMargins( 12, 10, 12, 10 );
  m_summaryLabel = SicnuUi::makeHintLabel( summaryGroup, tr( "运行后在此显示掩膜统计结果。" ) );
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
    handleFailed( tr( "请先选择一个有效的栅格图层。" ) );
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
                         tr( "掩膜像元：%1 / %2（%3%）" )
                           .arg( result["maskedPixels"].asUInt64() )
                           .arg( result["totalPixels"].asUInt64() )
                           .arg( result["maskedPercent"].asDouble(), 0, 'f', 2 ) );
                   } );
}
