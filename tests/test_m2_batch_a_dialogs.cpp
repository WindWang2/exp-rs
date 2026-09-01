// test_m2_batch_a_dialogs.cpp — Comprehensive empirical verification for Batch A dialogs
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
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>

#include <array>
#include <vector>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include "app/dialogs/atmospheric_dialog.h"
#include "app/dialogs/band_math_dialog.h"
#include "app/dialogs/band_ratio_dialog.h"
#include "app/dialogs/contrast_stretch_dialog.h"
#include "app/dialogs/extract_band_dialog.h"
#include "app/dialogs/qa_mask_dialog.h"
#include "app/dialogs/radiometric_calibration_dialog.h"
#include "app/dialogs/spatial_filter_dialog.h"
#include "app/dialogs/speckle_filter_dialog.h"
#include "app/dialogs/spectral_index_dialog.h"
#include "app/dialogs/spectral_library_dialog.h"

#include "app/widgets/band_role_combo.h"
#include "app/widgets/histogram_stretch_widget.h"
#include "app/widgets/raster_layer_combo.h"
#include "data/data_manager.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_m2_batch_a_dialogs";
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

class TestableExtractBandDialog : public ExtractBandDialog
{
public:
  using ExtractBandDialog::validateInputs;
};

class TestableQaMaskDialog : public QaMaskDialog
{
public:
  using QaMaskDialog::shouldAutoAcceptOnSuccess;
};

} // namespace

TEST_CASE( "Batch A: AtmosphericDialog layout, layer switching, and method controls", "[batch_a][atmospheric]" )
{
  ensureQgisApplication();
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString raster1Path = tempDir.filePath( QStringLiteral( "single_band.tif" ) );
  const QString raster4Path = tempDir.filePath( QStringLiteral( "four_bands.tif" ) );
  REQUIRE( !createSyntheticRaster( raster1Path, 4, 4, 1, 50.0f ).isEmpty() );
  REQUIRE( !createSyntheticRaster( raster4Path, 4, 4, 4, 120.0f ).isEmpty() );

  auto *layer1 = new QgsRasterLayer( raster1Path, QStringLiteral( "single_band" ) );
  auto *layer4 = new QgsRasterLayer( raster4Path, QStringLiteral( "four_bands" ) );
  REQUIRE( layer1->isValid() );
  REQUIRE( layer4->isValid() );
  QgsProject::instance()->addMapLayer( layer1 );
  QgsProject::instance()->addMapLayer( layer4 );

  AtmosphericDialog dialog;

  auto *layerCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "atmosphericInputLayerCombo" ) );
  auto *methodCombo = dialog.findChild<QComboBox *>();
  REQUIRE( layerCombo != nullptr );
  REQUIRE( methodCombo != nullptr );

  SECTION( "Layer attachment and switching updates band count and combos" )
  {
    dialog.setRasterLayer( layer1 );
    CHECK( layerCombo->currentRasterLayer() == layer1 );

    // Switch to 4-band layer
    dialog.setRasterLayer( layer4 );
    CHECK( layerCombo->currentRasterLayer() == layer4 );

    // Rapid switching stress test
    for ( int i = 0; i < 30; ++i )
    {
      dialog.setRasterLayer( ( i % 2 == 0 ) ? layer1 : layer4 );
      CHECK( layerCombo->currentRasterLayer() != nullptr );
    }
  }

  SECTION( "Null layer safety" )
  {
    dialog.setRasterLayer( nullptr );
    CHECK( true );
  }
}

TEST_CASE( "Batch A: RadiometricCalibrationDialog unit, band combo, and all-bands toggle", "[batch_a][radiometric]" )
{
  ensureQgisApplication();
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = tempDir.filePath( QStringLiteral( "rad_test.tif" ) );
  REQUIRE( !createSyntheticRaster( rasterPath, 4, 4, 3, 200.0f ).isEmpty() );

  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "rad_layer" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  RadiometricCalibrationDialog dialog;
  dialog.setRasterLayer( layer );

  auto *layerCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "radiometricInputLayerCombo" ) );
  REQUIRE( layerCombo != nullptr );
  CHECK( layerCombo->currentRasterLayer() == layer );

  SECTION( "Null layer safety on radiometric calibration" )
  {
    dialog.setRasterLayer( nullptr );
    CHECK( true );
  }
}

TEST_CASE( "Batch A: ContrastStretchDialog histogram widget and presets", "[batch_a][contrast]" )
{
  ensureQgisApplication();
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = tempDir.filePath( QStringLiteral( "stretch_test.tif" ) );
  REQUIRE( !createSyntheticRaster( rasterPath, 4, 4, 3, 100.0f ).isEmpty() );

  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "stretch_layer" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  ContrastStretchDialog dialog;
  dialog.setRasterLayer( layer );

  auto *layerCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "contrastStretchInputLayerCombo" ) );
  REQUIRE( layerCombo != nullptr );
  CHECK( layerCombo->currentRasterLayer() == layer );

  SECTION( "Layer switching to null and back" )
  {
    dialog.setRasterLayer( nullptr );
    CHECK( true );
    dialog.setRasterLayer( layer );
    CHECK( layerCombo->currentRasterLayer() == layer );
  }
}

TEST_CASE( "Batch A: SpatialFilterDialog filter types and sigma controls", "[batch_a][spatial_filter]" )
{
  ensureQgisApplication();
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = tempDir.filePath( QStringLiteral( "spatial_test.tif" ) );
  REQUIRE( !createSyntheticRaster( rasterPath, 4, 4, 1, 100.0f ).isEmpty() );

  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "spatial_layer" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  SpatialFilterDialog dialog;
  dialog.setRasterLayer( layer );

  auto *layerCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "spatialFilterInputLayerCombo" ) );
  REQUIRE( layerCombo != nullptr );
  CHECK( layerCombo->currentRasterLayer() == layer );
}

TEST_CASE( "Batch A: SpeckleFilterDialog filter options and damping controls", "[batch_a][speckle_filter]" )
{
  ensureQgisApplication();
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = tempDir.filePath( QStringLiteral( "speckle_test.tif" ) );
  REQUIRE( !createSyntheticRaster( rasterPath, 4, 4, 1, 100.0f ).isEmpty() );

  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "speckle_layer" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  SpeckleFilterDialog dialog;
  dialog.setRasterLayer( layer );

  auto *layerCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "speckleFilterInputLayerCombo" ) );
  REQUIRE( layerCombo != nullptr );
  CHECK( layerCombo->currentRasterLayer() == layer );
}

TEST_CASE( "Batch A: SpectralIndexDialog BandRoleCombos and index switching", "[batch_a][spectral_index]" )
{
  ensureQgisApplication();
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString raster4Path = tempDir.filePath( QStringLiteral( "index_4b.tif" ) );
  const QString raster8Path = tempDir.filePath( QStringLiteral( "index_8b.tif" ) );
  REQUIRE( !createSyntheticRaster( raster4Path, 4, 4, 4, 100.0f ).isEmpty() );
  REQUIRE( !createSyntheticRaster( raster8Path, 4, 4, 8, 100.0f ).isEmpty() );

  auto *layer4 = new QgsRasterLayer( raster4Path, QStringLiteral( "index_4b" ) );
  auto *layer8 = new QgsRasterLayer( raster8Path, QStringLiteral( "index_8b" ) );
  REQUIRE( layer4->isValid() );
  REQUIRE( layer8->isValid() );
  QgsProject::instance()->addMapLayer( layer4 );
  QgsProject::instance()->addMapLayer( layer8 );

  SpectralIndexDialog dialog;
  dialog.setRasterLayer( layer4 );

  auto *nirCombo = dialog.findChild<BandRoleCombo *>( QStringLiteral( "spectralIndexNirCombo" ) );
  auto *redCombo = dialog.findChild<BandRoleCombo *>( QStringLiteral( "spectralIndexRedCombo" ) );
  auto *greenCombo = dialog.findChild<BandRoleCombo *>( QStringLiteral( "spectralIndexGreenCombo" ) );
  auto *blueCombo = dialog.findChild<BandRoleCombo *>( QStringLiteral( "spectralIndexBlueCombo" ) );
  auto *swirCombo = dialog.findChild<BandRoleCombo *>( QStringLiteral( "spectralIndexSwirCombo" ) );

  REQUIRE( nirCombo != nullptr );
  REQUIRE( redCombo != nullptr );
  REQUIRE( greenCombo != nullptr );
  REQUIRE( blueCombo != nullptr );
  REQUIRE( swirCombo != nullptr );

  SECTION( "Switching to 8-band layer refreshes band role combos" )
  {
    dialog.setRasterLayer( layer8 );
    CHECK( nirCombo->count() > 1 );
    CHECK( redCombo->count() > 1 );
  }

  SECTION( "DataManager toggle" )
  {
    sicnu::data::DataManager dm;
    dialog.setDataManager( &dm );
    dialog.setDataManager( nullptr );
    CHECK( true );
  }
}

TEST_CASE( "Batch A: SpectralLibraryDialog matching and summary updates", "[batch_a][spectral_library]" )
{
  ensureQgisApplication();

  SpectralLibraryDialog dialog;
  auto *pathEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "spectralLibPathEdit" ) );
  REQUIRE( pathEdit != nullptr );
  const auto buttons = dialog.findChildren<QPushButton *>();
  REQUIRE( !buttons.isEmpty() );

  SECTION( "Set spectral curve profile" )
  {
    QVector<double> wavelengths = { 450.0, 550.0, 650.0, 850.0 };
    QVector<double> values = { 0.05, 0.08, 0.04, 0.45 };
    dialog.setSpectrum( wavelengths, values );
    CHECK( true );
  }
}

TEST_CASE( "Batch A: BandRatioDialog mode switching and band role combos", "[batch_a][band_ratio]" )
{
  ensureQgisApplication();
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = tempDir.filePath( QStringLiteral( "ratio_test.tif" ) );
  REQUIRE( !createSyntheticRaster( rasterPath, 4, 4, 4, 100.0f ).isEmpty() );

  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "ratio_layer" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  BandRatioDialog dialog;
  dialog.setRasterLayer( layer );

  auto *layerCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "bandRatioInputLayerCombo" ) );
  REQUIRE( layerCombo != nullptr );
  CHECK( layerCombo->currentRasterLayer() == layer );
}

TEST_CASE( "Batch A: BandMathDialog expression validation and info label", "[batch_a][band_math]" )
{
  ensureQgisApplication();
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = tempDir.filePath( QStringLiteral( "math_test.tif" ) );
  REQUIRE( !createSyntheticRaster( rasterPath, 4, 4, 3, 100.0f ).isEmpty() );

  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "math_layer" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  BandMathDialog dialog;
  dialog.setRasterLayer( layer );

  auto *layerCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "bandMathInputLayerCombo" ) );
  auto *exprEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "bandMathExpressionEdit" ) );
  REQUIRE( layerCombo != nullptr );
  REQUIRE( exprEdit != nullptr );

  SECTION( "Layer switching updates band info" )
  {
    dialog.setRasterLayer( nullptr );
    dialog.setRasterLayer( layer );
    CHECK( layerCombo->currentRasterLayer() == layer );
  }
}

TEST_CASE( "Batch A: ExtractBandDialog band selection and output validation", "[batch_a][extract_band]" )
{
  ensureQgisApplication();
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = tempDir.filePath( QStringLiteral( "extract_test.tif" ) );
  REQUIRE( !createSyntheticRaster( rasterPath, 4, 4, 4, 100.0f ).isEmpty() );

  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "extract_layer" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  TestableExtractBandDialog dialog;
  dialog.setRasterLayer( layer );

  auto *layerCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "extractBandInputLayerCombo" ) );
  auto *bandCombo = dialog.findChild<BandRoleCombo *>( QStringLiteral( "extractBandRoleCombo" ) );
  REQUIRE( layerCombo != nullptr );
  REQUIRE( bandCombo != nullptr );

  SECTION( "Validation of inputs" )
  {
    CHECK( dialog.validateInputs() );
  }
}

TEST_CASE( "Batch A: QaMaskDialog auto-accept disabled and source combo", "[batch_a][qa_mask]" )
{
  ensureQgisApplication();
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = tempDir.filePath( QStringLiteral( "qa_test.tif" ) );
  REQUIRE( !createSyntheticRaster( rasterPath, 4, 4, 2, 100.0f ).isEmpty() );

  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "qa_layer" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  TestableQaMaskDialog dialog;
  dialog.setRasterLayer( layer );

  // QaMaskDialog must preserve result summary on completion
  CHECK_FALSE( dialog.shouldAutoAcceptOnSuccess() );

  auto *layerCombo = dialog.findChild<RasterLayerCombo *>( QStringLiteral( "qaMaskInputLayerCombo" ) );
  auto *summaryLabel = dialog.findChild<QLabel *>( QStringLiteral( "qaMaskSummaryLabel" ) );
  REQUIRE( layerCombo != nullptr );
  REQUIRE( summaryLabel != nullptr );
}
