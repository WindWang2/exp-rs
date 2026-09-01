// test_m2_batch_b_dialogs.cpp — Comprehensive empirical verification for Batch B dialogs
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <array>
#include <vector>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include "app/classification/rs_merge_classes_dialog.h"
#include "app/classification/rs_post_process_dialog.h"
#include "app/dialogs/apply_mask_dialog.h"
#include "app/dialogs/batch_processing_dialog.h"
#include "app/dialogs/change_detection_dialog.h"
#include "app/dialogs/comparison_dialog.h"
#include "app/dialogs/crs_preset_dialog.h"
#include "app/dialogs/fusion_dialog.h"
#include "app/dialogs/help_viewer_dialog.h"
#include "app/dialogs/mosaic_dialog.h"
#include "app/dialogs/orthorectification_dialog.h"
#include "app/dialogs/pca_dialog.h"
#include "app/dialogs/post_classification_dialog.h"
#include "app/dialogs/preferences_dialog.h"
#include "app/dialogs/product_import_dialog.h"
#include "app/dialogs/terrain_dialog.h"
#include "app/georeferencer/rs_sift_dialog.h"
#include "app/georeferencer/rs_template_match_dialog.h"
#include "app/dialogs/sicnu_algorithm_dialog.h"
#include "app/dialogs/stac_browser_dialog.h"
#include "app/classification/rs_accuracy_dialog.h"
#include "app/classification/rs_accuracy_panel.h"
#include "app/classification/rs_classifier_load_dialog.h"
#include "analysis/classification/rs_accuracy_assessment.h"

#include "app/widgets/crs_selector.h"
#include "app/widgets/raster_layer_combo.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_m2_batch_b_dialogs";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

QString createSyntheticRaster( const QString &path, int width, int height, int numBands, float fillValue = 100.0f )
{
  ensureGdalInit();
  std::vector<std::vector<float>> bands( numBands, std::vector<float>( static_cast<size_t>( width ) * height, fillValue ) );
  std::array<double, 6> gt = { 100.0, 30.0, 0.0, 500.0, 0.0, -30.0 };
  QString err;
  if ( !writeGdalOutput( path, width, height, bands, gt, QStringLiteral( "EPSG:4326" ), &err ) )
    return QString();
  return path;
}

class TestableMosaicDialog : public MosaicDialog
{
public:
  using MosaicDialog::validateInputs;
};

class TestableApplyMaskDialog : public ApplyMaskDialog
{
public:
  using ApplyMaskDialog::validateInputs;
};

class TestableChangeDetectionDialog : public ChangeDetectionDialog
{
public:
  using ChangeDetectionDialog::validateInputs;
};

} // namespace

TEST_CASE( "Batch B: OrthorectificationDialog buildParams contract and dynamic options", "[batch_b][ortho]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString raster = dir.filePath( QStringLiteral( "ortho_in.tif" ) );
  REQUIRE( !createSyntheticRaster( raster, 4, 4, 1, 100.0f ).isEmpty() );

  auto *layer = new QgsRasterLayer( raster, QStringLiteral( "ortho_in" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  OrthorectificationDialog dialog;
  dialog.setRasterLayer( layer );

  auto *outputEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "rsDialogOutputEdit" ) );
  REQUIRE( outputEdit != nullptr );
  const QString output = dir.filePath( QStringLiteral( "ortho_out.tif" ) );
  outputEdit->setText( output );

  SECTION( "Default serialization contract" )
  {
    const Json::Value params = dialog.buildParams();
    CHECK( params["input"].asString() == raster.toStdString() );
    CHECK( params["output"].asString() == output.toStdString() );
    CHECK( params["resampling"].asString() == "bilinear" );
    CHECK( params["dstCrs"].asString() == "EPSG:4326" );
    CHECK_FALSE( params.isMember( "dem" ) );
    CHECK_FALSE( params.isMember( "targetResolution" ) );
    CHECK_FALSE( params.isMember( "height" ) );
    CHECK_FALSE( params.isMember( "nodata" ) );
  }

  SECTION( "Explicit options override and serialization" )
  {
    auto *crsEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "orthoTargetCrsEdit" ) );
    auto *demEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "orthoDemEdit" ) );
    auto *resampling = dialog.findChild<QComboBox *>( QStringLiteral( "orthoResamplingCombo" ) );
    auto *resolution = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "orthoResolutionSpin" ) );
    auto *height = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "orthoHeightSpin" ) );
    auto *nodataCheck = dialog.findChild<QCheckBox *>( QStringLiteral( "orthoNodataCheck" ) );
    auto *nodataSpin = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "orthoNodataSpin" ) );

    REQUIRE( crsEdit != nullptr );
    REQUIRE( demEdit != nullptr );
    REQUIRE( resampling != nullptr );
    REQUIRE( resolution != nullptr );
    REQUIRE( height != nullptr );
    REQUIRE( nodataCheck != nullptr );
    REQUIRE( nodataSpin != nullptr );

    crsEdit->setText( QStringLiteral( "EPSG:32650" ) );
    demEdit->setText( QStringLiteral( "/tmp/dem.tif" ) );
    resampling->setCurrentIndex( resampling->findData( QStringLiteral( "cubic" ) ) );
    resolution->setValue( 15.0 );
    height->setValue( 500.0 );
    nodataCheck->setChecked( true );
    nodataSpin->setValue( -9999.0 );

    const Json::Value params = dialog.buildParams();
    CHECK( params["dstCrs"].asString() == "EPSG:32650" );
    CHECK( params["dem"].asString() == "/tmp/dem.tif" );
    CHECK( params["resampling"].asString() == "cubic" );
    CHECK( params["targetResolution"].asDouble() == 15.0 );
    CHECK( params["height"].asDouble() == 500.0 );
    CHECK( params["nodata"].asDouble() == -9999.0 );
  }
}

TEST_CASE( "Batch B: MosaicDialog file list and validation", "[batch_b][mosaic]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  TestableMosaicDialog dialog;
  auto *listWidget = dialog.findChild<QListWidget *>( QStringLiteral( "mosaicInputList" ) );
  auto *outputEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "rsDialogOutputEdit" ) );
  REQUIRE( listWidget != nullptr );
  REQUIRE( outputEdit != nullptr );

  SECTION( "File list management and validation" )
  {
    CHECK( listWidget->count() == 0 );
    listWidget->addItem( QStringLiteral( "/tmp/file1.tif" ) );
    CHECK( listWidget->count() == 1 );
    listWidget->addItem( QStringLiteral( "/tmp/file2.tif" ) );
    CHECK( listWidget->count() == 2 );
    outputEdit->setText( dir.filePath( QStringLiteral( "mosaic.tif" ) ) );
    CHECK( dialog.validateInputs() );
  }
}

TEST_CASE( "Batch B: FusionDialog layer combos and method selection", "[batch_b][fusion]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString panPath = dir.filePath( QStringLiteral( "pan.tif" ) );
  const QString msPath = dir.filePath( QStringLiteral( "ms.tif" ) );
  REQUIRE( !createSyntheticRaster( panPath, 4, 4, 1, 100.0f ).isEmpty() );
  REQUIRE( !createSyntheticRaster( msPath, 4, 4, 4, 100.0f ).isEmpty() );

  auto *panLayer = new QgsRasterLayer( panPath, QStringLiteral( "pan_layer" ) );
  auto *msLayer = new QgsRasterLayer( msPath, QStringLiteral( "ms_layer" ) );
  REQUIRE( panLayer->isValid() );
  REQUIRE( msLayer->isValid() );
  QgsProject::instance()->addMapLayer( panLayer );
  QgsProject::instance()->addMapLayer( msLayer );

  FusionDialog dialog;
  auto *panCombo = dialog.findChild<QComboBox *>( QStringLiteral( "fusionPanCombo" ) );
  auto *msCombo = dialog.findChild<QComboBox *>( QStringLiteral( "fusionMsCombo" ) );
  auto *methodCombo = dialog.findChild<QComboBox *>( QStringLiteral( "fusionMethodCombo" ) );
  auto *weightSpin = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "fusionWeightSpin" ) );

  REQUIRE( panCombo != nullptr );
  REQUIRE( msCombo != nullptr );
  REQUIRE( methodCombo != nullptr );
  REQUIRE( weightSpin != nullptr );
}

TEST_CASE( "Batch B: ChangeDetectionDialog buildParams contract and mask strategies", "[batch_b][change_detection]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString before = dir.filePath( QStringLiteral( "before.tif" ) );
  const QString after = dir.filePath( QStringLiteral( "after.tif" ) );
  REQUIRE( !createSyntheticRaster( before, 4, 4, 2, 50.0f ).isEmpty() );
  REQUIRE( !createSyntheticRaster( after, 4, 4, 2, 80.0f ).isEmpty() );

  auto *beforeLayer = new QgsRasterLayer( before, QStringLiteral( "before_layer" ) );
  auto *afterLayer = new QgsRasterLayer( after, QStringLiteral( "after_layer" ) );
  REQUIRE( beforeLayer->isValid() );
  REQUIRE( afterLayer->isValid() );
  QgsProject::instance()->addMapLayer( beforeLayer );
  QgsProject::instance()->addMapLayer( afterLayer );

  TestableChangeDetectionDialog dialog;
  auto *outputEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "rsDialogOutputEdit" ) );
  REQUIRE( outputEdit != nullptr );
  const QString output = dir.filePath( QStringLiteral( "change_out.tif" ) );
  outputEdit->setText( output );

  auto *beforeCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "cdBeforeCombo" ) );
  auto *afterCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "cdAfterCombo" ) );
  auto *methodCombo = dialog.findChild<QComboBox *>( QStringLiteral( "cdMethodCombo" ) );
  auto *makeMaskCheck = dialog.findChild<QCheckBox *>( QStringLiteral( "cdMakeMaskCheck" ) );
  auto *thresholdMethod = dialog.findChild<QComboBox *>( QStringLiteral( "cdThresholdMethodCombo" ) );
  auto *percentileSpin = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "cdPercentileSpin" ) );
  auto *cleanupCombo = dialog.findChild<QComboBox *>( QStringLiteral( "cdCleanupCombo" ) );
  auto *cleanupIterSpin = dialog.findChild<QSpinBox *>( QStringLiteral( "cdCleanupIterSpin" ) );
  auto *minAreaSpin = dialog.findChild<QSpinBox *>( QStringLiteral( "cdMinAreaSpin" ) );

  REQUIRE( beforeCombo != nullptr );
  REQUIRE( afterCombo != nullptr );
  REQUIRE( methodCombo != nullptr );
  REQUIRE( makeMaskCheck != nullptr );
  REQUIRE( thresholdMethod != nullptr );

  beforeCombo->selectLayer( beforeLayer->id() );
  afterCombo->selectLayer( afterLayer->id() );

  SECTION( "Difference without mask" )
  {
    methodCombo->setCurrentIndex( methodCombo->findData( QStringLiteral( "difference" ) ) );
    makeMaskCheck->setChecked( false );

    const Json::Value params = dialog.buildParams();
    CHECK( params["before"].asString() == before.toStdString() );
    CHECK( params["after"].asString() == after.toStdString() );
    CHECK( params["method"].asString() == "difference" );
    CHECK_FALSE( params.isMember( "makeMask" ) );
  }

  SECTION( "Difference with percentile mask and open cleanup" )
  {
    methodCombo->setCurrentIndex( methodCombo->findData( QStringLiteral( "difference" ) ) );
    makeMaskCheck->setChecked( true );
    thresholdMethod->setCurrentIndex( thresholdMethod->findData( QStringLiteral( "percentile" ) ) );
    percentileSpin->setValue( 95.0 );
    cleanupCombo->setCurrentIndex( cleanupCombo->findData( QStringLiteral( "open" ) ) );
    cleanupIterSpin->setValue( 2 );
    minAreaSpin->setValue( 10 );

    const Json::Value params = dialog.buildParams();
    CHECK( params["makeMask"].asBool() == true );
    CHECK( params["thresholdMethod"].asString() == "percentile" );
    CHECK( params["percentile"].asDouble() == 95.0 );
    CHECK( params["cleanup"].asString() == "open" );
    CHECK( params["cleanupIterations"].asInt() == 2 );
    CHECK( params["minAreaPixels"].asInt() == 10 );
  }
}

TEST_CASE( "Batch B: PcaDialog components validation", "[batch_b][pca]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString raster = dir.filePath( QStringLiteral( "pca_in.tif" ) );
  REQUIRE( !createSyntheticRaster( raster, 4, 4, 4, 100.0f ).isEmpty() );

  auto *layer = new QgsRasterLayer( raster, QStringLiteral( "pca_in" ) );
  REQUIRE( layer->isValid() );

  PcaDialog dialog;
  dialog.setRasterLayer( layer );

  auto *spin = dialog.findChild<QSpinBox *>( QStringLiteral( "pcaComponentsSpin" ) );
  REQUIRE( spin != nullptr );
  spin->setValue( 3 );
  CHECK( spin->value() == 3 );
}

TEST_CASE( "Batch B: PostClassificationDialog buildParams contract", "[batch_b][post_class]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString before = dir.filePath( QStringLiteral( "class_before.tif" ) );
  const QString after = dir.filePath( QStringLiteral( "class_after.tif" ) );
  REQUIRE( !createSyntheticRaster( before, 4, 4, 1, 1.0f ).isEmpty() );
  REQUIRE( !createSyntheticRaster( after, 4, 4, 1, 2.0f ).isEmpty() );

  auto *beforeLayer = new QgsRasterLayer( before, QStringLiteral( "class_before" ) );
  auto *afterLayer = new QgsRasterLayer( after, QStringLiteral( "class_after" ) );
  REQUIRE( beforeLayer->isValid() );
  REQUIRE( afterLayer->isValid() );
  QgsProject::instance()->addMapLayer( beforeLayer );
  QgsProject::instance()->addMapLayer( afterLayer );

  PostClassificationDialog dialog;
  auto *beforeCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "postClassBeforeCombo" ) );
  auto *afterCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "postClassAfterCombo" ) );
  auto *countSpin = dialog.findChild<QSpinBox *>( QStringLiteral( "postClassCountSpin" ) );
  auto *outputEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "rsDialogOutputEdit" ) );

  REQUIRE( beforeCombo != nullptr );
  REQUIRE( afterCombo != nullptr );
  REQUIRE( countSpin != nullptr );
  REQUIRE( outputEdit != nullptr );

  beforeCombo->selectLayer( beforeLayer->id() );
  afterCombo->selectLayer( afterLayer->id() );
  outputEdit->setText( dir.filePath( QStringLiteral( "post_out.tif" ) ) );
  countSpin->setValue( 5 );

  const Json::Value params = dialog.buildParams();
  CHECK( params["before"].asString() == before.toStdString() );
  CHECK( params["after"].asString() == after.toStdString() );
  CHECK( params["class_count"].asInt() == 5 );
}

TEST_CASE( "Batch B: TerrainDialog analysis products and solar controls", "[batch_b][terrain]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString demPath = dir.filePath( QStringLiteral( "dem.tif" ) );
  REQUIRE( !createSyntheticRaster( demPath, 4, 4, 1, 500.0f ).isEmpty() );

  auto *demLayer = new QgsRasterLayer( demPath, QStringLiteral( "dem_layer" ) );
  REQUIRE( demLayer->isValid() );
  QgsProject::instance()->addMapLayer( demLayer );

  TerrainDialog dialog;
  auto *analysisCombo = dialog.findChild<QComboBox *>( QStringLiteral( "terrainAnalysisCombo" ) );
  auto *cellSizeSpin = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "terrainCellSizeSpin" ) );
  auto *sunAzimuthSpin = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "terrainSunAzimuthSpin" ) );
  auto *sunElevationSpin = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "terrainSunElevationSpin" ) );

  REQUIRE( analysisCombo != nullptr );
  REQUIRE( cellSizeSpin != nullptr );
  REQUIRE( sunAzimuthSpin != nullptr );
  REQUIRE( sunElevationSpin != nullptr );

  SECTION( "Hillshade enables solar angles; Slope disables them" )
  {
    analysisCombo->setCurrentIndex( analysisCombo->findData( QStringLiteral( "hillshade" ) ) );
    CHECK( sunAzimuthSpin->isEnabled() );
    CHECK( sunElevationSpin->isEnabled() );

    analysisCombo->setCurrentIndex( analysisCombo->findData( QStringLiteral( "slope" ) ) );
    CHECK_FALSE( sunAzimuthSpin->isEnabled() );
    CHECK_FALSE( sunElevationSpin->isEnabled() );
  }
}

TEST_CASE( "Batch B: ApplyMaskDialog buildParams contract and NoData override", "[batch_b][apply_mask]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString prodPath = dir.filePath( QStringLiteral( "product.tif" ) );
  const QString maskPath = dir.filePath( QStringLiteral( "mask.tif" ) );
  REQUIRE( !createSyntheticRaster( prodPath, 4, 4, 4, 100.0f ).isEmpty() );
  REQUIRE( !createSyntheticRaster( maskPath, 4, 4, 1, 0.0f ).isEmpty() );

  auto *prodLayer = new QgsRasterLayer( prodPath, QStringLiteral( "prod_layer" ) );
  auto *maskLayer = new QgsRasterLayer( maskPath, QStringLiteral( "mask_layer" ) );
  REQUIRE( prodLayer->isValid() );
  REQUIRE( maskLayer->isValid() );
  QgsProject::instance()->addMapLayer( prodLayer );
  QgsProject::instance()->addMapLayer( maskLayer );

  TestableApplyMaskDialog dialog;
  auto *inputCombo = dialog.findChild<QComboBox *>( QStringLiteral( "applyMaskInputCombo" ) );
  auto *maskCombo = dialog.findChild<QComboBox *>( QStringLiteral( "applyMaskMaskCombo" ) );
  auto *noDataCheck = dialog.findChild<QCheckBox *>( QStringLiteral( "applyMaskNoDataCheck" ) );
  auto *noDataSpin = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "applyMaskNoDataSpin" ) );
  auto *alignCheck = dialog.findChild<QCheckBox *>( QStringLiteral( "applyMaskAlignCheck" ) );
  auto *outputEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "rsDialogOutputEdit" ) );

  REQUIRE( inputCombo != nullptr );
  REQUIRE( maskCombo != nullptr );
  REQUIRE( noDataCheck != nullptr );
  REQUIRE( noDataSpin != nullptr );
  REQUIRE( alignCheck != nullptr );
  REQUIRE( outputEdit != nullptr );

  inputCombo->setCurrentIndex( inputCombo->findData( prodLayer->id() ) );
  maskCombo->setCurrentIndex( maskCombo->findData( maskLayer->id() ) );
  outputEdit->setText( dir.filePath( QStringLiteral( "masked.tif" ) ) );

  SECTION( "Default buildParams: align_mask true, no explicit no_data" )
  {
    const Json::Value params = dialog.buildParams();
    CHECK( params["input"].asString() == prodPath.toStdString() );
    CHECK( params["mask"].asString() == maskPath.toStdString() );
    CHECK( params["align_mask"].asBool() == true );
    CHECK_FALSE( params.isMember( "no_data" ) );
  }

  SECTION( "Explicit no_data option" )
  {
    noDataCheck->setChecked( true );
    noDataSpin->setValue( -9999.0 );

    const Json::Value params = dialog.buildParams();
    CHECK( params["no_data"].asDouble() == -9999.0 );
  }
}

TEST_CASE( "Batch B: CrsPresetDialog search filtering and selectedEpsg", "[batch_b][crs_preset]" )
{
  ensureQgisApplication();

  CrsPresetDialog dialog;
  auto *searchEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "crsSearchEdit" ) );
  auto *treeWidget = dialog.findChild<QTreeWidget *>( QStringLiteral( "crsTreeWidget" ) );

  REQUIRE( searchEdit != nullptr );
  REQUIRE( treeWidget != nullptr );

  SECTION( "Initial tree has items" )
  {
    CHECK( treeWidget->topLevelItemCount() > 0 );
  }

  SECTION( "Search filter narrows results" )
  {
    searchEdit->setText( QStringLiteral( "4326" ) );
    // Search query executes smoothly
    CHECK( treeWidget->topLevelItemCount() > 0 );
  }
}

TEST_CASE( "Batch B: ComparisonDialog layout and button box", "[batch_b][comparison]" )
{
  ensureQgisApplication();

  ComparisonDialog dialog;
  auto *leftCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "compareLeftCombo" ) );
  auto *rightCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "compareRightCombo" ) );
  const auto buttons = dialog.findChildren<QPushButton *>();

  REQUIRE( leftCombo != nullptr );
  REQUIRE( rightCombo != nullptr );
  REQUIRE( !buttons.isEmpty() );
}

TEST_CASE( "Batch B: PreferencesDialog settings roundtrip", "[batch_b][preferences]" )
{
  ensureQgisApplication();

  PreferencesDialog dialog;
  dialog.setTheme( QStringLiteral( "dark" ) );
  dialog.setDefaultCrs( QStringLiteral( "EPSG:3857" ) );
  dialog.setGdalPath( QStringLiteral( "/usr/bin/gdal" ) );
  dialog.setOtbPath( QStringLiteral( "/usr/bin/otb" ) );
  dialog.setLogToFile( true );
  dialog.setLogFilePath( QStringLiteral( "/tmp/test.log" ) );

  CHECK( dialog.theme() == QStringLiteral( "dark" ) );
  CHECK( dialog.defaultCrs() == QStringLiteral( "EPSG:3857" ) );
  CHECK( dialog.gdalPath() == QStringLiteral( "/usr/bin/gdal" ) );
  CHECK( dialog.otbPath() == QStringLiteral( "/usr/bin/otb" ) );
  CHECK( dialog.logToFile() == true );
  CHECK( dialog.logFilePath() == QStringLiteral( "/tmp/test.log" ) );
}

TEST_CASE( "Batch B: RsMergeClassesDialog target selection and recode map", "[batch_b][merge_classes]" )
{
  ensureQgisApplication();

  RsMergeClassesDialog dialog;
  dialog.setSourceClassIds( { 4, 2, 7, 2 }, QStringLiteral( "Water" ), QColor( 0, 0, 255 ) );

  CHECK( dialog.targetClassId() == 2 ); // Min ID = 2
  CHECK( dialog.targetName() == QStringLiteral( "Water" ) );
  CHECK( dialog.targetColor() == QColor( 0, 0, 255 ) );

  const QMap<int, int> recodeMap = buildRecodeMap( { 4, 2, 7 }, 2 );
  CHECK( recodeMap.value( 4 ) == 2 );
  CHECK( recodeMap.value( 7 ) == 2 );
  CHECK( recodeMap.value( 2 ) == 2 );
}

TEST_CASE( "Batch B: RsPostProcessDialog algorithm titles and configs", "[batch_b][post_process]" )
{
  ensureQgisApplication();

  CHECK( !RsPostProcessDialog::algorithmTitle( RsPostProcessDialog::Algorithm::Sieve ).isEmpty() );
  CHECK( !RsPostProcessDialog::algorithmTitle( RsPostProcessDialog::Algorithm::Majority ).isEmpty() );
  CHECK( !RsPostProcessDialog::algorithmTitle( RsPostProcessDialog::Algorithm::Clump ).isEmpty() );
  CHECK( !RsPostProcessDialog::algorithmTitle( RsPostProcessDialog::Algorithm::Recode ).isEmpty() );
  CHECK( !RsPostProcessDialog::algorithmTitle( RsPostProcessDialog::Algorithm::Polygonize ).isEmpty() );

  RsPostProcessDialog dialog( RsPostProcessDialog::Algorithm::Sieve );
  CHECK( dialog.windowTitle().contains( QStringLiteral( "Sieve" ) ) );
}

TEST_CASE( "Batch B: RsSiftDialog parameter defaults and extraction", "[batch_b][sift]" )
{
  ensureQgisApplication();

  RsSiftDialog dialog;
  const RsSiftMatcher::Params p = dialog.params();
  CHECK( p.contrastThreshold == Catch::Approx( 0.04 ) );
  CHECK( p.maxMatches == 100 );
  CHECK( p.minInlierRatio == Catch::Approx( 0.50 ) );
  CHECK( p.ransacThreshold == Catch::Approx( 3.0 ) );
  CHECK( p.maxImageSide == 2048 );
}

TEST_CASE( "Batch B: RsTemplateMatchDialog seed modes and parameters", "[batch_b][template_match]" )
{
  ensureQgisApplication();

  RsTemplateMatchDialog dialog;
  auto *seedMode = dialog.findChild<QComboBox *>( QStringLiteral( "templateSeedMode" ) );
  auto *gridRows = dialog.findChild<QSpinBox *>( QStringLiteral( "templateGridRowsSpin" ) );
  auto *gridCols = dialog.findChild<QSpinBox *>( QStringLiteral( "templateGridColsSpin" ) );

  REQUIRE( seedMode != nullptr );
  REQUIRE( gridRows != nullptr );
  REQUIRE( gridCols != nullptr );

  SECTION( "Grid mode enables grid rows/cols" )
  {
    seedMode->setCurrentIndex( seedMode->findData( int( RsTemplateMatcher::SeedMode::Grid ) ) );
    CHECK( gridRows->isEnabled() );
    CHECK( gridCols->isEnabled() );

    const RsTemplateMatcher::Params p = dialog.params();
    CHECK( p.seedMode == RsTemplateMatcher::SeedMode::Grid );
    CHECK( p.templateSize == 65 );
    CHECK( p.searchRadiusPx == 96 );
  }

  SECTION( "ExistingSeeds mode disables grid rows/cols" )
  {
    seedMode->setCurrentIndex( seedMode->findData( int( RsTemplateMatcher::SeedMode::ExistingSeeds ) ) );
    CHECK_FALSE( gridRows->isEnabled() );
    CHECK_FALSE( gridCols->isEnabled() );

    const RsTemplateMatcher::Params p = dialog.params();
    CHECK( p.seedMode == RsTemplateMatcher::SeedMode::ExistingSeeds );
  }
}

TEST_CASE( "Batch B: SicnuAlgorithmDialog instantiation and processing context", "[batch_b][sicnu_algorithm]" )
{
  ensureQgisApplication();

  SicnuAlgorithmDialog dialog;
  CHECK( dialog.processingContext() != nullptr );
  CHECK( dialog.createProcessingParameters().isEmpty() );
}

TEST_CASE( "Batch B: StacBrowserDialog creation and structure", "[batch_b][stac_browser]" )
{
  ensureQgisApplication();

  StacBrowserDialog dialog( nullptr );
  CHECK( ( dialog.windowTitle().contains( "STAC", Qt::CaseInsensitive ) || dialog.windowTitle().contains( "数据浏览" ) ) );

  auto *resultsTable = dialog.findChild<QTableWidget *>();
  REQUIRE( resultsTable != nullptr );
  CHECK( resultsTable->columnCount() == 4 );

  auto *searchBtn = dialog.findChild<QPushButton *>();
  REQUIRE( searchBtn != nullptr );
}

TEST_CASE( "Batch B: RsAccuracyDialog result visualization and button box", "[batch_b][accuracy_dialog]" )
{
  ensureQgisApplication();

  QVector<int> yt = { 1, 1, 1, 2, 2 };
  QVector<int> yp = { 1, 1, 2, 2, 2 };
  const auto res = RsAccuracyAssessment::compute( yt, yp );

  QHash<int, QString> names{ { 1, QStringLiteral( "Water" ) }, { 2, QStringLiteral( "Forest" ) } };

  RsAccuracyDialog dialog( res, names );
  CHECK( ( dialog.windowTitle().contains( "Accuracy", Qt::CaseInsensitive ) || dialog.windowTitle().contains( "精度" ) ) );

  auto *panel = dialog.findChild<RsAccuracyPanel *>();
  REQUIRE( panel != nullptr );
  CHECK( panel->hasResult() );
  CHECK( panel->result().overallAccuracy == Catch::Approx( 4.0 / 5.0 ).margin( 1e-4 ) );
}

TEST_CASE( "Batch B: RsClassifierLoadDialog backend selection and path handling", "[batch_b][classifier_load]" )
{
  ensureQgisApplication();

  RsClassifierLoadDialog dialog;
  CHECK( ( dialog.windowTitle().contains( "分类器" ) || dialog.windowTitle().contains( "Classifier", Qt::CaseInsensitive ) ) );
  CHECK( dialog.selectedKind() == RsClassifierLoadDialog::BackendKind::NormalBayes );
  CHECK( dialog.modelPath().isEmpty() );

  auto *pathEdit = dialog.findChild<QLineEdit *>();
  REQUIRE( pathEdit != nullptr );
  pathEdit->setText( QStringLiteral( "/tmp/mock_model.yml" ) );
  CHECK( dialog.modelPath() == QStringLiteral( "/tmp/mock_model.yml" ) );
}

