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

  QGroupBox *inputGroup = setupInputGroup(
    mainLayout, tr( "输入数据与分析类型" ) );
  inputGroup->setToolTip(
    tr( "选择 DEM 高程栅格与目标分析产品。建议在米制投影坐标系下运行以保证坡度与阴影计算精度。" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  mLayerCombo = new QComboBox( inputGroup );
  mLayerCombo->setObjectName( QStringLiteral( "terrainLayerCombo" ) );
  SicnuDialogHelp::tip( mLayerCombo, tr( "参与计算的 DEM 高程栅格图层。" ) );
  inputForm->addRow( tr( "DEM 图层" ), mLayerCombo );

  mAnalysisCombo = new QComboBox( inputGroup );
  mAnalysisCombo->setObjectName( QStringLiteral( "terrainAnalysisCombo" ) );
  mAnalysisCombo->addItem( tr( "坡度 (Slope, 度)" ), QStringLiteral( "slope" ) );
  mAnalysisCombo->addItem( tr( "坡向 (Aspect, 度)" ), QStringLiteral( "aspect" ) );
  mAnalysisCombo->addItem( tr( "山体阴影 (Hillshade)" ), QStringLiteral( "hillshade" ) );
  mAnalysisCombo->addItem( tr( "地表粗糙度 (Roughness)" ), QStringLiteral( "roughness" ) );
  mAnalysisCombo->addItem( tr( "地形起伏度 (TRI)" ), QStringLiteral( "tri" ) );
  mAnalysisCombo->addItem( tr( "地形位置指数 (TPI)" ), QStringLiteral( "tpi" ) );
  SicnuDialogHelp::tip( mAnalysisCombo, tr(
    "坡度/坡向计算；山体阴影需指定太阳方位角与高度角；粗糙度/TRI/TPI 为地貌特征指数。" ) );
  inputForm->addRow( tr( "分析类型" ), mAnalysisCombo );

  QGroupBox *paramGroup = setupParamGroup(
    mainLayout, tr( "地形计算参数" ) );
  paramGroup->setToolTip(
    tr( "像元尺寸通常根据栅格空间分辨率自动估算；太阳光照参数仅在山体阴影分析时生效。" ) );
  auto *paramForm = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( paramForm );

  mCellSizeSpin = new QDoubleSpinBox( paramGroup );
  mCellSizeSpin->setObjectName( QStringLiteral( "terrainCellSizeSpin" ) );
  mCellSizeSpin->setRange( 0.000001, 100000.0 );
  mCellSizeSpin->setDecimals( 4 );
  mCellSizeSpin->setValue( 1.0 );
  SicnuDialogHelp::tip( mCellSizeSpin, tr( "水平与垂直网格像元大小（地图坐标单位）。" ) );
  paramForm->addRow( tr( "像元大小" ), mCellSizeSpin );

  mSunAzimuthSpin = new QDoubleSpinBox( paramGroup );
  mSunAzimuthSpin->setObjectName( QStringLiteral( "terrainSunAzimuthSpin" ) );
  mSunAzimuthSpin->setRange( 0.0, 360.0 );
  mSunAzimuthSpin->setValue( 315.0 );
  mSunAzimuthSpin->setSuffix( QStringLiteral( "°" ) );
  SicnuDialogHelp::tip( mSunAzimuthSpin, tr( "太阳方位角：自正北顺时针旋转角度 (0°~360°)。" ) );
  paramForm->addRow( tr( "太阳方位角" ), mSunAzimuthSpin );

  mSunElevationSpin = new QDoubleSpinBox( paramGroup );
  mSunElevationSpin->setObjectName( QStringLiteral( "terrainSunElevationSpin" ) );
  mSunElevationSpin->setRange( 0.0, 90.0 );
  mSunElevationSpin->setValue( 45.0 );
  mSunElevationSpin->setSuffix( QStringLiteral( "°" ) );
  SicnuDialogHelp::tip( mSunElevationSpin, tr( "太阳高度角：太阳光线与地平面的夹角 (0°~90°)。" ) );
  paramForm->addRow( tr( "太阳高度角" ), mSunElevationSpin );

  auto updateSunParamsVisibility = [this]() {
    const QString product = mAnalysisCombo->currentData().toString();
    const bool isHillshade = ( product == QStringLiteral( "hillshade" ) );
    mSunAzimuthSpin->setEnabled( isHillshade );
    mSunElevationSpin->setEnabled( isHillshade );
  };
  connect( mAnalysisCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, updateSunParamsVisibility );
  updateSunParamsVisibility();

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
