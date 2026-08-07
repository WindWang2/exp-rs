// test_orthorectification_dialog.cpp - orthorectification dialog param build
//
// Drives the dialog headlessly (no exec()): sets a synthetic raster layer and
// widget state, then asserts buildParams() assembles the gdal:orthorectification
// operator JSON (defaults omitted, explicit options surfaced).
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
#include <qgsrasterlayer.h>

#include "app/dialogs/orthorectification_dialog.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_orthorectification_dialog";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

} // namespace

TEST_CASE( "OrthorectificationDialog buildParams assembles the operator JSON",
           "[ortho_dialog]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString raster = dir.filePath( QStringLiteral( "input.tif" ) );
  std::vector<std::vector<float>> bands( 1, std::vector<float>( 4, 100.0f ) );
  std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
  QString err;
  REQUIRE( writeGdalOutput( raster, 2, 2, bands, gt, QStringLiteral( "EPSG:32650" ), &err ) );

  QgsRasterLayer layer( raster, QStringLiteral( "input" ) );
  REQUIRE( layer.isValid() );

  OrthorectificationDialog dialog;
  dialog.setRasterLayer( &layer );

  auto *outputEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "rsDialogOutputEdit" ) );
  REQUIRE( outputEdit != nullptr );
  const QString output = dir.filePath( QStringLiteral( "ortho.tif" ) );
  outputEdit->setText( output );

  SECTION( "Defaults: bilinear, EPSG:4326, optional params omitted" )
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

  SECTION( "Explicit options surface in the JSON" )
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
    demEdit->setText( QStringLiteral( "/data/dem.tif" ) );
    resampling->setCurrentIndex( resampling->findData( QStringLiteral( "cubic" ) ) );
    resolution->setValue( 15.0 );
    height->setValue( 500.0 );
    nodataCheck->setChecked( true );
    nodataSpin->setValue( -9999.0 );

    const Json::Value params = dialog.buildParams();
    CHECK( params["dstCrs"].asString() == "EPSG:32650" );
    CHECK( params["dem"].asString() == "/data/dem.tif" );
    CHECK( params["resampling"].asString() == "cubic" );
    CHECK( params["targetResolution"].asDouble() == 15.0 );
    CHECK( params["height"].asDouble() == 500.0 );
    CHECK( params["nodata"].asDouble() == -9999.0 );
  }
}
