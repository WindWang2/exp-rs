// src/app/dialogs/spectral_index_dialog.cpp
#include "spectral_index_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>

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

void SpectralIndexDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );

  setupHelpBanner( mainLayout );

  // ---- 参数：指数类型 + 波段映射 ----
  QFrame *sec = SicnuUi::makeSection(
    this, tr( "参数" ),
    tr( "选择光谱指数并映射传感器波段。波段号从 1 起。" ) );
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

  sec->layout()->addItem( form );
  sec->layout()->addWidget( SicnuUi::makeHintLabel(
    sec, tr( "默认按 Landsat/Sentinel 常见顺序预填，请按实际数据核对波段。" ) ) );
  mainLayout->addWidget( sec );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  updateBandVisibility();
}

void SpectralIndexDialog::populateBandCombos()
{
  if ( !m_rasterLayer || !m_rasterLayer->isValid() )
    return;

  int bandCount = m_rasterLayer->bandCount();

  m_nirCombo->clear();
  m_redCombo->clear();
  m_greenCombo->clear();
  m_blueCombo->clear();
  m_swirCombo->clear();

  for ( int i = 1; i <= bandCount; ++i )
  {
    QString bandName = tr( "波段 %1" ).arg( i );
    m_nirCombo->addItem( bandName, i );
    m_redCombo->addItem( bandName, i );
    m_greenCombo->addItem( bandName, i );
    m_blueCombo->addItem( bandName, i );
    m_swirCombo->addItem( bandName, i );
  }

  // Default Landsat/Sentinel-style mapping
  if ( bandCount >= 4 )
  {
    m_nirCombo->setCurrentIndex( 3 );
    m_redCombo->setCurrentIndex( 2 );
    m_greenCombo->setCurrentIndex( 1 );
    m_blueCombo->setCurrentIndex( 0 );
  }
  if ( bandCount >= 5 )
    m_swirCombo->setCurrentIndex( 4 );
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
  Json::Value params( Json::objectValue );
  params["input"] = m_rasterLayer->source().toStdString();
  params["output"] = outputPath().toStdString();
  params["index"] = m_indexCombo->currentData().toString().toStdString();
  params["nir"] = m_nirCombo->currentData().toInt();
  params["red"] = m_redCombo->currentData().toInt();
  params["green"] = m_greenCombo->currentData().toInt();
  params["blue"] = m_blueCombo->currentData().toInt();
  params["swir"] = m_swirCombo->currentData().toInt();

  runOperatorTask( QStringLiteral( "rs:spectral_index" ), params );
}
