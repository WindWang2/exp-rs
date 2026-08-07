// test_apply_mask_dialog.cpp — apply-mask dialog param build
//
// Drives the dialog headlessly (no exec()): registers synthetic raster layers,
// selects them in the two layer pickers, then asserts buildParams() assembles
// the rs:apply_mask operator JSON (defaults omitted, explicit options surfaced).
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QTemporaryDir>

#include <array>
#include <vector>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include "app/dialogs/apply_mask_dialog.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_apply_mask_dialog";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

} // namespace

TEST_CASE( "ApplyMaskDialog buildParams assembles the operator JSON", "[apply_mask_dialog]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString input = dir.filePath( QStringLiteral( "product.tif" ) );
  const QString mask = dir.filePath( QStringLiteral( "mask.tif" ) );
  std::vector<std::vector<float>> inputBands( 2, std::vector<float>( 4, 100.0f ) );
  std::vector<std::vector<float>> maskBands( 1, std::vector<float>( 4, 0.0f ) );
  std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
  QString err;
  REQUIRE( writeGdalOutput( input, 2, 2, inputBands, gt, QStringLiteral( "EPSG:32648" ), &err ) );
  REQUIRE( writeGdalOutput( mask, 2, 2, maskBands, gt, QStringLiteral( "EPSG:32648" ), &err ) );

  QgsRasterLayer *inputLayer = new QgsRasterLayer( input, QStringLiteral( "product" ) );
  QgsRasterLayer *maskLayer = new QgsRasterLayer( mask, QStringLiteral( "mask" ) );
  REQUIRE( inputLayer->isValid() );
  REQUIRE( maskLayer->isValid() );

  // The project takes ownership; removed at teardown via clear().
  QgsProject::instance()->addMapLayer( inputLayer );
  QgsProject::instance()->addMapLayer( maskLayer );

  ApplyMaskDialog dialog;
  dialog.setRasterLayer( inputLayer ); // preselects the product as input
  dialog.populateLayers();

  auto *inputCombo = dialog.findChild<QComboBox *>( QStringLiteral( "applyMaskInputCombo" ) );
  auto *maskCombo = dialog.findChild<QComboBox *>( QStringLiteral( "applyMaskMaskCombo" ) );
  auto *outputEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "rsDialogOutputEdit" ) );
  auto *noDataCheck = dialog.findChild<QCheckBox *>( QStringLiteral( "applyMaskNoDataCheck" ) );
  auto *noDataSpin = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "applyMaskNoDataSpin" ) );
  auto *alignCheck = dialog.findChild<QCheckBox *>( QStringLiteral( "applyMaskAlignCheck" ) );
  REQUIRE( inputCombo != nullptr );
  REQUIRE( maskCombo != nullptr );
  REQUIRE( outputEdit != nullptr );
  REQUIRE( noDataCheck != nullptr );
  REQUIRE( noDataSpin != nullptr );
  REQUIRE( alignCheck != nullptr );

  // Both layers appear in both pickers; the product is preselected as input.
  REQUIRE( inputCombo->count() == 2 );
  REQUIRE( maskCombo->count() == 2 );
  CHECK( inputCombo->currentData().toString() == inputLayer->id() );
  maskCombo->setCurrentIndex( maskCombo->findData( maskLayer->id() ) );

  SECTION( "Defaults: align on, NoData override omitted, suggested output path" )
  {
    // populateLayers() suggests <dir>/product_masked.tif while output is empty.
    CHECK( outputEdit->text() == dir.filePath( QStringLiteral( "product_masked.tif" ) ) );

    const Json::Value params = dialog.buildParams();
    CHECK( params["input"].asString() == input.toStdString() );
    CHECK( params["mask"].asString() == mask.toStdString() );
    CHECK( params["output"].asString() == outputEdit->text().toStdString() );
    CHECK( params["align_mask"].asBool() == true );
    CHECK_FALSE( params.isMember( "no_data" ) );
  }

  SECTION( "NoData override surfaces when enabled" )
  {
    noDataCheck->setChecked( true );
    noDataSpin->setValue( -9999.0 );

    const Json::Value params = dialog.buildParams();
    CHECK( params["no_data"].asDouble() == -9999.0 );
    CHECK( params["align_mask"].asBool() == true );
  }

  SECTION( "Disabling auto-alignment surfaces align_mask=false" )
  {
    alignCheck->setChecked( false );
    const Json::Value params = dialog.buildParams();
    CHECK( params["align_mask"].asBool() == false );
  }

  QgsProject::instance()->clear();
}
