// src/app/dialogs/qa_mask_dialog.cpp — QA / cloud / shadow / snow mask dialog
#include "qa_mask_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "widgets/band_role_combo.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QFrame>
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
  setupUi();
}

void QaMaskDialog::setRasterLayer( QgsRasterLayer *layer )
{
  RasterProcessingDialogBase::setRasterLayer( layer );
  if ( layer && m_bandCombo )
  {
    m_bandCombo->setRaster( layer->source() );
    // Preselect the semantic QA band (scene classification preferred).
    m_bandCombo->selectBandByRole( sicnu::data::BandRole::SceneClassification );
    if ( m_bandCombo->selectedBand() == 0 )
      m_bandCombo->selectBandByRole( sicnu::data::BandRole::QA );
  }
}

void QaMaskDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QFrame *sec = SicnuUi::makeSection(
    this, tr( "掩膜参数" ),
    tr( "从产品质量波段（Landsat QA_PIXEL / Sentinel-2 SCL）生成二值掩膜："
        "1 = 被遮挡（云/云影/雪），0 = 有效。" ) );
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );
  form->setVerticalSpacing( 8 );

  m_sourceCombo = new QComboBox( sec );
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

  m_maskCombo = new QComboBox( sec );
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

  m_bandCombo = new BandRoleCombo( sec );
  SicnuDialogHelp::tip( m_bandCombo, tr(
    "质量波段。默认按产品语义角色自动选择（SCL → 场景分类，QA → 质量）。" ) );
  form->addRow( tr( "质量波段" ), m_bandCombo );

  m_bitsSpin = new QSpinBox( sec );
  m_bitsSpin->setRange( 1, 65535 );
  m_bitsSpin->setValue( 1 );
  m_bitsSpin->setToolTip( tr( "通用位掩码：值为 (value & bits) != 0 的像素被掩膜。" ) );
  form->addRow( tr( "位标志 (通用)" ), m_bitsSpin );
  m_bitsLabel = qobject_cast<QLabel *>( form->labelForField( m_bitsSpin ) );

  qobject_cast<QVBoxLayout *>( sec->layout() )->addLayout( form );
  mainLayout->addWidget( sec );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  onSourceChanged( 0 );
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

  runOperatorTask( QStringLiteral( "rs:qa_mask" ), json );
}
