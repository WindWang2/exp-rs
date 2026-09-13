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
  setWindowTitle( tr( "Terrain Analysis" ) );
  setMinimumWidth( 480 );
  setupUi();
}

void TerrainDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QGroupBox *inputGroup = setupInputGroup(
    mainLayout, tr( "Input Data and Analysis Type" ) );
  inputGroup->setToolTip(
    tr( "Select the DEM elevation raster and the target analysis product. Running in a metric projected CRS is recommended for accurate slope and shading." ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  mLayerCombo = new QComboBox( inputGroup );
  mLayerCombo->setObjectName( QStringLiteral( "terrainLayerCombo" ) );
  SicnuDialogHelp::tip( mLayerCombo, tr( "DEM elevation raster used in the computation." ) );
  inputForm->addRow( tr( "DEM layer" ), mLayerCombo );

  mAnalysisCombo = new QComboBox( inputGroup );
  mAnalysisCombo->setObjectName( QStringLiteral( "terrainAnalysisCombo" ) );
  mAnalysisCombo->addItem( tr( "Slope (degrees)" ), QStringLiteral( "slope" ) );
  mAnalysisCombo->addItem( tr( "Aspect (degrees)" ), QStringLiteral( "aspect" ) );
  mAnalysisCombo->addItem( tr( "Hillshade" ), QStringLiteral( "hillshade" ) );
  mAnalysisCombo->addItem( tr( "Surface Roughness" ), QStringLiteral( "roughness" ) );
  mAnalysisCombo->addItem( tr( "Terrain Ruggedness Index (TRI)" ), QStringLiteral( "tri" ) );
  mAnalysisCombo->addItem( tr( "Topographic Position Index (TPI)" ), QStringLiteral( "tpi" ) );
  SicnuDialogHelp::tip( mAnalysisCombo, tr(
    "Slope / aspect computation; hillshade needs solar azimuth and elevation; roughness / TRI / TPI are geomorphometric indices.")  );
  inputForm->addRow( tr( "Analysis Type" ), mAnalysisCombo );

  QGroupBox *paramGroup = setupParamGroup(
    mainLayout, tr( "Terrain Computation Parameters" ) );
  paramGroup->setToolTip(
    tr( "Pixel size is usually estimated automatically from the raster spatial resolution; sun illumination parameters only apply to hillshade analysis." ) );
  auto *paramForm = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( paramForm );

  mCellSizeSpin = new QDoubleSpinBox( paramGroup );
  mCellSizeSpin->setObjectName( QStringLiteral( "terrainCellSizeSpin" ) );
  mCellSizeSpin->setRange( 0.000001, 100000.0 );
  mCellSizeSpin->setDecimals( 4 );
  mCellSizeSpin->setValue( 1.0 );
  SicnuDialogHelp::tip( mCellSizeSpin, tr( "Horizontal and vertical grid pixel size (map coordinate units)." ) );
  paramForm->addRow( tr( "Pixel Size" ), mCellSizeSpin );

  mSunAzimuthSpin = new QDoubleSpinBox( paramGroup );
  mSunAzimuthSpin->setObjectName( QStringLiteral( "terrainSunAzimuthSpin" ) );
  mSunAzimuthSpin->setRange( 0.0, 360.0 );
  mSunAzimuthSpin->setValue( 315.0 );
  mSunAzimuthSpin->setSuffix( QStringLiteral( "°" ) );
  SicnuDialogHelp::tip( mSunAzimuthSpin, tr( "Solar azimuth: clockwise angle from true north (0°–360°)." ) );
  paramForm->addRow( tr( "Solar Azimuth" ), mSunAzimuthSpin );

  mSunElevationSpin = new QDoubleSpinBox( paramGroup );
  mSunElevationSpin->setObjectName( QStringLiteral( "terrainSunElevationSpin" ) );
  mSunElevationSpin->setRange( 0.0, 90.0 );
  mSunElevationSpin->setValue( 45.0 );
  mSunElevationSpin->setSuffix( QStringLiteral( "°" ) );
  SicnuDialogHelp::tip( mSunElevationSpin, tr( "Solar elevation: angle of the sun above the horizon (0°–90°)." ) );
  paramForm->addRow( tr( "Solar Elevation" ), mSunElevationSpin );

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
  mStatusLabel = SicnuUi::makeHintLabel( this, tr( "Ready" ) );
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
    QMessageBox::warning( this, dialogTitle(), tr( "Select a valid DEM layer." ) );
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
    QMessageBox::warning( this, dialogTitle(), tr( "Select a DEM layer." ) );
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
    mStatusLabel->setText( tr( "Processing..." ) );

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
    mStatusLabel->setText( tr( "Ready" ) );
}
