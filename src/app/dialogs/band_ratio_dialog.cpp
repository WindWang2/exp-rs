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
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "输入数据" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "bandRatioInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "选择待执行波段运算的栅格图层。" ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &BandRatioDialog::onLayerChanged );
  inputForm->addRow( tr( "输入栅格" ), m_layerCombo );
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Parameters Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "运算参数" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_modeCombo = new QComboBox( paramGroup );
  m_modeCombo->addItems( { tr( "波段比值 (Band Ratio)" ), tr( "IHS 颜色变换" ) } );
  SicnuDialogHelp::tip( m_modeCombo, tr(
    "• 波段比值：分子波段 ÷ 分母波段\n• IHS 变换：RGB 三波段转换为强度 (Intensity)、色调 (Hue)、饱和度 (Saturation)" ) );
  connect( m_modeCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &BandRatioDialog::onModeChanged );
  form->addRow( tr( "运算模式" ), m_modeCombo );

  m_band1Label = new QLabel( tr( "分子波段" ), paramGroup );
  m_band1Combo = new BandRoleCombo( paramGroup );
  SicnuDialogHelp::tip( m_band1Combo, tr( "比值运算分子波段。" ) );
  form->addRow( m_band1Label, m_band1Combo );

  m_band2Label = new QLabel( tr( "分母波段" ), paramGroup );
  m_band2Combo = new BandRoleCombo( paramGroup );
  SicnuDialogHelp::tip( m_band2Combo, tr( "比值运算分母波段（请勿全为 0）。" ) );
  form->addRow( m_band2Label, m_band2Combo );

  m_redLabel = new QLabel( tr( "红光波段 R" ), paramGroup );
  m_redCombo = new BandRoleCombo( paramGroup );
  SicnuDialogHelp::tip( m_redCombo, tr( "IHS 变换红色分量波段。" ) );
  form->addRow( m_redLabel, m_redCombo );

  m_greenLabel = new QLabel( tr( "绿光波段 G" ), paramGroup );
  m_greenCombo = new BandRoleCombo( paramGroup );
  SicnuDialogHelp::tip( m_greenCombo, tr( "IHS 变换绿色分量波段。" ) );
  form->addRow( m_greenLabel, m_greenCombo );

  m_blueLabel = new QLabel( tr( "蓝光波段 B" ), paramGroup );
  m_blueCombo = new BandRoleCombo( paramGroup );
  SicnuDialogHelp::tip( m_blueCombo, tr( "IHS 变换蓝色分量波段。" ) );
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
      QMessageBox::warning( this, dialogTitle(), tr( "请为波段比值选择两个不同的有效波段。" ) );
      return;
    }
  }
  else
  {
    if ( redNum < 1 || greenNum < 1 || blueNum < 1 )
    {
      QMessageBox::warning( this, dialogTitle(), tr( "请为 IHS 变换选择有效的 RGB 波段。" ) );
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
