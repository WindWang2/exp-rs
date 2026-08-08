// test_post_classification_dialog.cpp — post-classification dialog param build
//
// Drives the dialog headlessly: registers two thematic raster layers, selects
// them, and asserts buildParams() assembles the rs:post_classification_change
// operator JSON (band defaults, class_count omission/override).
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTemporaryDir>

#include <array>
#include <vector>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include "app/dialogs/post_classification_dialog.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_post_classification_dialog";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

} // namespace

TEST_CASE( "PostClassificationDialog buildParams assembles the operator JSON",
           "[post_classification_dialog]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString before = dir.filePath( QStringLiteral( "before.tif" ) );
  const QString after = dir.filePath( QStringLiteral( "after.tif" ) );
  std::vector<std::vector<float>> bands( 2, std::vector<float>( 4, 1.0f ) );
  std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
  QString err;
  REQUIRE( writeGdalOutput( before, 2, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );
  REQUIRE( writeGdalOutput( after, 2, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );

  QgsRasterLayer *beforeLayer = new QgsRasterLayer( before, QStringLiteral( "before" ) );
  QgsRasterLayer *afterLayer = new QgsRasterLayer( after, QStringLiteral( "after" ) );
  REQUIRE( beforeLayer->isValid() );
  REQUIRE( afterLayer->isValid() );
  QgsProject::instance()->addMapLayer( beforeLayer );
  QgsProject::instance()->addMapLayer( afterLayer );

  PostClassificationDialog dialog;
  dialog.populateLayers();

  auto *beforeCombo = dialog.findChild<QComboBox *>( QStringLiteral( "postClassBeforeCombo" ) );
  auto *afterCombo = dialog.findChild<QComboBox *>( QStringLiteral( "postClassAfterCombo" ) );
  auto *beforeBand = dialog.findChild<QComboBox *>( QStringLiteral( "postClassBeforeBandCombo" ) );
  auto *afterBand = dialog.findChild<QComboBox *>( QStringLiteral( "postClassAfterBandCombo" ) );
  auto *classCount = dialog.findChild<QSpinBox *>( QStringLiteral( "postClassCountSpin" ) );
  auto *outputEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "rsDialogOutputEdit" ) );
  REQUIRE( beforeCombo != nullptr );
  REQUIRE( afterCombo != nullptr );
  REQUIRE( beforeBand != nullptr );
  REQUIRE( afterBand != nullptr );
  REQUIRE( classCount != nullptr );
  REQUIRE( outputEdit != nullptr );

  beforeCombo->setCurrentIndex( beforeCombo->findData( beforeLayer->id() ) );
  afterCombo->setCurrentIndex( afterCombo->findData( afterLayer->id() ) );
  const QString output = dir.filePath( QStringLiteral( "change_map.tif" ) );
  outputEdit->setText( output );

  SECTION( "defaults: band 1 on both sides, class_count omitted (auto)" )
  {
    const Json::Value params = dialog.buildParams();
    CHECK( params["before"].asString() == before.toStdString() );
    CHECK( params["after"].asString() == after.toStdString() );
    CHECK( params["output"].asString() == output.toStdString() );
    CHECK( params["band"].asInt() == 1 );
    CHECK( params["afterBand"].asInt() == 1 );
    CHECK_FALSE( params.isMember( "class_count" ) );
  }

  SECTION( "explicit class_count surfaces in the JSON" )
  {
    classCount->setValue( 5 );
    const Json::Value params = dialog.buildParams();
    CHECK( params["class_count"].asInt() == 5 );
  }

  SECTION( "band selection flows through" )
  {
    beforeBand->setCurrentIndex( 1 ); // band 2
    afterBand->setCurrentIndex( 0 );  // band 1
    const Json::Value params = dialog.buildParams();
    CHECK( params["band"].asInt() == 2 );
    CHECK( params["afterBand"].asInt() == 1 );
  }

  QgsProject::instance()->clear();
}
