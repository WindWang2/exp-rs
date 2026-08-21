// terrain_dialog.cpp — Phase 11.2
#include "terrain_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QFileInfo>

TerrainDialog::TerrainDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( tr( "地形分析" ) );
  setMinimumWidth( 480 );
  setupUi();
}

void TerrainDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QFrame *inputSec = SicnuUi::makeSection(
    this, tr( "输入" ),
    tr( "选择 DEM 与分析产品。米制投影下坡度/阴影更可靠。" ) );
  auto *inputForm = new QFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );
  inputForm->setHorizontalSpacing( 12 );
  inputForm->setVerticalSpacing( 8 );

  mLayerCombo = new QComboBox( inputSec );
  SicnuDialogHelp::tip( mLayerCombo, tr( "DEM 高程栅格。" ) );
  inputForm->addRow( tr( "DEM 图层" ), mLayerCombo );

  mAnalysisCombo = new QComboBox( inputSec );
  mAnalysisCombo->addItem( tr( "坡度 (度)" ), "slope" );
  mAnalysisCombo->addItem( tr( "坡向 (度)" ), "aspect" );
  mAnalysisCombo->addItem( tr( "山体阴影" ), "hillshade" );
  mAnalysisCombo->addItem( tr( "粗糙度 Roughness" ), "roughness" );
  mAnalysisCombo->addItem( tr( "TRI 地形起伏" ), "tri" );
  mAnalysisCombo->addItem( tr( "TPI 地形位置" ), "tpi" );
  SicnuDialogHelp::tip( mAnalysisCombo, tr(
    "坡度/坡向；Hillshade 需太阳方位/高度；Roughness/TRI/TPI。" ) );
  inputForm->addRow( tr( "分析类型" ), mAnalysisCombo );
  qobject_cast<QVBoxLayout *>( inputSec->layout() )->addLayout( inputForm );
  mainLayout->addWidget( inputSec );

  QFrame *paramSec = SicnuUi::makeSection(
    this, tr( "参数" ),
    tr( "像元大小常自动估算；太阳参数仅用于山体阴影。" ) );
  auto *paramForm = new QFormLayout();
  paramForm->setContentsMargins( 0, 0, 0, 0 );
  paramForm->setHorizontalSpacing( 12 );
  paramForm->setVerticalSpacing( 8 );

  mCellSizeSpin = new QDoubleSpinBox( paramSec );
  mCellSizeSpin->setRange( 0.001, 10000.0 );
  mCellSizeSpin->setValue( 1.0 );
  SicnuDialogHelp::tip( mCellSizeSpin, tr( "像元大小（地图单位）。" ) );
  paramForm->addRow( tr( "像元大小" ), mCellSizeSpin );

  mSunAzimuthSpin = new QDoubleSpinBox( paramSec );
  mSunAzimuthSpin->setRange( 0, 360 );
  mSunAzimuthSpin->setValue( 315.0 );
  mSunAzimuthSpin->setSuffix( QStringLiteral( "°" ) );
  SicnuDialogHelp::tip( mSunAzimuthSpin, tr( "太阳方位：自北顺时针 0–360°。" ) );
  paramForm->addRow( tr( "太阳方位" ), mSunAzimuthSpin );

  mSunElevationSpin = new QDoubleSpinBox( paramSec );
  mSunElevationSpin->setRange( 0, 90 );
  mSunElevationSpin->setValue( 45.0 );
  mSunElevationSpin->setSuffix( QStringLiteral( "°" ) );
  SicnuDialogHelp::tip( mSunElevationSpin, tr( "太阳高度：0–90°。" ) );
  paramForm->addRow( tr( "太阳高度" ), mSunElevationSpin );
  qobject_cast<QVBoxLayout *>( paramSec->layout() )->addLayout( paramForm );
  mainLayout->addWidget( paramSec );

  setupOutputRow( mainLayout );
  mStatusLabel = SicnuUi::makeHintLabel( this, tr( "就绪" ) );
  mainLayout->addWidget( mStatusLabel );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  populateRasterLayerCombo( mLayerCombo );
  connect( mLayerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
           [this]( int idx ) {
             if ( auto *rl = mLayerCombo->itemData( idx ).value<QgsRasterLayer *>() )
             {
               auto extent = rl->extent();
               if ( extent.width() > 0 && extent.height() > 0 && rl->width() > 0 )
                 mCellSizeSpin->setValue( extent.width() / rl->width() );
             }
           } );
  if ( mLayerCombo->count() > 0 )
    mLayerCombo->setCurrentIndex( 0 );
}

bool TerrainDialog::validateInputs()
{
  auto *rl = mLayerCombo ? mLayerCombo->currentData().value<QgsRasterLayer *>() : nullptr;
  if ( !rl || !rl->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请选择有效的 DEM 图层。" ) );
    return false;
  }

  setRasterLayer( rl );

  QString outPath = outputPath();
  if ( outPath.isEmpty() )
  {
    const QString inputPath = rl->source();
    const QString analysisType = mAnalysisCombo ? mAnalysisCombo->currentData().toString() : QStringLiteral( "slope" );
    outPath = QFileInfo( inputPath ).path() + QLatin1Char( '/' )
              + QFileInfo( inputPath ).completeBaseName()
              + QLatin1Char( '_' ) + analysisType + QStringLiteral( ".tif" );
    if ( m_outputEdit )
      m_outputEdit->setText( outPath );
  }

  return true;
}

void TerrainDialog::onRun()
{
  auto *rl = mLayerCombo->currentData().value<QgsRasterLayer *>();
  if ( !rl )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请选择 DEM 图层。" ) );
    return;
  }

  QString outPath = outputPath();
  if ( outPath.isEmpty() )
  {
    const QString inputPath = rl->source();
    const QString analysisType = mAnalysisCombo->currentData().toString();
    outPath = QFileInfo( inputPath ).path() + QLatin1Char( '/' )
              + QFileInfo( inputPath ).baseName()
              + QLatin1Char( '_' ) + analysisType + QStringLiteral( ".tif" );
    m_outputEdit->setText( outPath );
  }

  if ( mStatusLabel )
    mStatusLabel->setText( tr( "处理中…" ) );

  setRasterLayer( rl );
  Json::Value params( Json::objectValue );
  params["input"] = rl->source().toStdString();
  params["output"] = outPath.toStdString();
  params["product"] = mAnalysisCombo->currentData().toString().toStdString();
  params["cellSize"] = mCellSizeSpin->value();
  params["sunAzimuth"] = mSunAzimuthSpin->value();
  params["sunElevation"] = mSunElevationSpin->value();
  // No hardcoded nodata: the operator resolves the DEM's declared NoData and
  // only falls back to -9999 when neither param nor metadata provides one (#445).
  runOperatorTask( QStringLiteral( "rs:terrain_analysis" ), params );
}

void TerrainDialog::onAnalysisFinished()
{
  if ( mStatusLabel )
    mStatusLabel->setText( tr( "就绪" ) );
}
