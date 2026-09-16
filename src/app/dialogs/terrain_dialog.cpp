// terrain_dialog.cpp — Phase 11.2; extended by terrain-hydrology-11 with
// hydrology (rs:terrain_flow), visibility (rs:terrain_viewshed) and solar
// (rs:terrain_solar) products. Each product routes to its domain operator;
// parameter widgets are enabled only for the products that use them.
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

namespace
{
// Product entry → operator id. Local-kernel products stay on
// rs:terrain_analysis; the hydrology/visibility/solar products live on their
// own operators (same split as the registry).
QString operatorForProduct( const QString &product )
{
    if ( product == QLatin1String( "flow_accumulation" )
         || product == QLatin1String( "stream_network" ) )
        return QStringLiteral( "rs:terrain_flow" );
    if ( product == QLatin1String( "viewshed" ) )
        return QStringLiteral( "rs:terrain_viewshed" );
    if ( product == QLatin1String( "shadow_duration" ) )
        return QStringLiteral( "rs:terrain_solar" );
    return QStringLiteral( "rs:terrain_analysis" );
}
} // namespace

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
  mAnalysisCombo->insertSeparator( mAnalysisCombo->count() );
  mAnalysisCombo->addItem( tr( "Flow Accumulation" ), QStringLiteral( "flow_accumulation" ) );
  mAnalysisCombo->addItem( tr( "Stream Network (Strahler)" ), QStringLiteral( "stream_network" ) );
  mAnalysisCombo->addItem( tr( "Viewshed (Observer Visibility)" ), QStringLiteral( "viewshed" ) );
  mAnalysisCombo->addItem( tr( "Shadow Duration" ), QStringLiteral( "shadow_duration" ) );
  SicnuDialogHelp::tip( mAnalysisCombo, tr(
    "Slope / aspect computation; hillshade needs solar azimuth and elevation; roughness / TRI / TPI are geomorphometric indices; the hydrology, visibility and solar products run on the DEM directly.")  );
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

  mStreamThresholdSpin = new QDoubleSpinBox( paramGroup );
  mStreamThresholdSpin->setObjectName( QStringLiteral( "terrainStreamThresholdSpin" ) );
  mStreamThresholdSpin->setRange( 1.0, 1000000.0 );
  mStreamThresholdSpin->setDecimals( 0 );
  mStreamThresholdSpin->setValue( 50.0 );
  SicnuDialogHelp::tip( mStreamThresholdSpin, tr( "Minimum drainage accumulation (cells) for a stream cell." ) );
  paramForm->addRow( tr( "Stream Threshold (cells)" ), mStreamThresholdSpin );

  mObserverColSpin = new QDoubleSpinBox( paramGroup );
  mObserverColSpin->setObjectName( QStringLiteral( "terrainObserverColSpin" ) );
  mObserverColSpin->setRange( 0.0, 1000000.0 );
  mObserverColSpin->setDecimals( 0 );
  mObserverRowSpin = new QDoubleSpinBox( paramGroup );
  mObserverRowSpin->setObjectName( QStringLiteral( "terrainObserverRowSpin" ) );
  mObserverRowSpin->setRange( 0.0, 1000000.0 );
  mObserverRowSpin->setDecimals( 0 );
  auto *observerRow = SicnuUi::makeFormLayout();
  observerRow->addRow( tr( "Column" ), mObserverColSpin );
  observerRow->addRow( tr( "Row" ), mObserverRowSpin );
  paramForm->addRow( tr( "Observer (pixel)" ), observerRow );

  mObserverHeightSpin = new QDoubleSpinBox( paramGroup );
  mObserverHeightSpin->setObjectName( QStringLiteral( "terrainObserverHeightSpin" ) );
  mObserverHeightSpin->setRange( 0.0, 100.0 );
  mObserverHeightSpin->setDecimals( 1 );
  mObserverHeightSpin->setValue( 1.7 );
  mObserverHeightSpin->setSuffix( QStringLiteral( " m" ) );
  SicnuDialogHelp::tip( mObserverHeightSpin, tr( "Observer eye height above the ground." ) );
  paramForm->addRow( tr( "Observer Height" ), mObserverHeightSpin );

  mRadiusSpin = new QDoubleSpinBox( paramGroup );
  mRadiusSpin->setObjectName( QStringLiteral( "terrainRadiusSpin" ) );
  mRadiusSpin->setRange( 0.0, 1.0e9 );
  mRadiusSpin->setDecimals( 1 );
  mRadiusSpin->setValue( 0.0 );
  SicnuDialogHelp::tip( mRadiusSpin, tr( "Analysis radius in map units; 0 analyses the whole scene." ) );
  paramForm->addRow( tr( "Viewshed Radius" ), mRadiusSpin );

  mDayOfYearSpin = new QDoubleSpinBox( paramGroup );
  mDayOfYearSpin->setObjectName( QStringLiteral( "terrainDayOfYearSpin" ) );
  mDayOfYearSpin->setRange( 1.0, 365.0 );
  mDayOfYearSpin->setDecimals( 0 );
  mDayOfYearSpin->setValue( 81.0 );
  SicnuDialogHelp::tip( mDayOfYearSpin, tr( "Day of year for the generated sun track (1–365)." ) );
  paramForm->addRow( tr( "Day of Year" ), mDayOfYearSpin );

  mLatitudeSpin = new QDoubleSpinBox( paramGroup );
  mLatitudeSpin->setObjectName( QStringLiteral( "terrainLatitudeSpin" ) );
  mLatitudeSpin->setRange( -90.0, 90.0 );
  mLatitudeSpin->setDecimals( 2 );
  mLatitudeSpin->setValue( 30.0 );
  mLatitudeSpin->setSuffix( QStringLiteral( "°" ) );
  SicnuDialogHelp::tip( mLatitudeSpin, tr( "Latitude for the generated sun track (local solar time 6–18 h)." ) );
  paramForm->addRow( tr( "Latitude" ), mLatitudeSpin );

  auto updateParamsVisibility = [this]() {
    const QString product = mAnalysisCombo->currentData().toString();
    const bool isHillshade = ( product == QLatin1String( "hillshade" ) );
    const bool isStream = ( product == QLatin1String( "stream_network" ) );
    const bool isViewshed = ( product == QLatin1String( "viewshed" ) );
    const bool isShadow = ( product == QLatin1String( "shadow_duration" ) );
    const bool localKernel = !( isStream || isViewshed || isShadow
                                || product == QLatin1String( "flow_accumulation" ) );
    mSunAzimuthSpin->setEnabled( isHillshade );
    mSunElevationSpin->setEnabled( isHillshade );
    mCellSizeSpin->setEnabled( localKernel );
    mStreamThresholdSpin->setEnabled( isStream );
    mObserverColSpin->setEnabled( isViewshed );
    mObserverRowSpin->setEnabled( isViewshed );
    mObserverHeightSpin->setEnabled( isViewshed );
    mRadiusSpin->setEnabled( isViewshed );
    mDayOfYearSpin->setEnabled( isShadow );
    mLatitudeSpin->setEnabled( isShadow );
  };
  connect( mAnalysisCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, updateParamsVisibility );
  updateParamsVisibility();

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

  const QString product = mAnalysisCombo->currentData().toString();
  QString outPath = outputPath();
  if ( outPath.isEmpty() )
  {
    const QString inputPath = rl->source();
    outPath = QFileInfo( inputPath ).path() + QLatin1Char( '/' )
              + QFileInfo( inputPath ).baseName()
              + QLatin1Char( '_' ) + product + QStringLiteral( ".tif" );
    m_outputEdit->setText( outPath );
  }

  if ( mStatusLabel )
    mStatusLabel->setText( tr( "Processing..." ) );

  setRasterLayer( rl );
  Json::Value params( Json::objectValue );
  params["input"] = rl->source().toStdString();
  params["output"] = outPath.toStdString();
  params["product"] = product.toStdString();
  if ( product == QLatin1String( "viewshed" ) )
  {
    params["observer"] = QStringLiteral( "%1,%2" )
                             .arg( mObserverColSpin->value(), 0, 'f', 0 )
                             .arg( mObserverRowSpin->value(), 0, 'f', 0 )
                             .toStdString();
    params["observer_height"] = mObserverHeightSpin->value();
    params["radius"] = mRadiusSpin->value();
  }
  else if ( product == QLatin1String( "stream_network" ) )
  {
    params["threshold"] = mStreamThresholdSpin->value();
  }
  else if ( product == QLatin1String( "shadow_duration" ) )
  {
    params["day_of_year"] = mDayOfYearSpin->value();
    params["latitude"] = mLatitudeSpin->value();
  }
  else
  {
    // Local-kernel products keep their historical parameters.
    params["cellSize"] = mCellSizeSpin->value();
    params["sunAzimuth"] = mSunAzimuthSpin->value();
    params["sunElevation"] = mSunElevationSpin->value();
  }
  // No hardcoded nodata: the operator resolves the DEM's declared NoData and
  // only falls back to -9999 when neither param nor metadata provides one (#445).
  runOperatorTask( operatorForProduct( product ), params );
}

void TerrainDialog::onAnalysisFinished()
{
  if ( mStatusLabel )
    mStatusLabel->setText( tr( "Ready" ) );
}
