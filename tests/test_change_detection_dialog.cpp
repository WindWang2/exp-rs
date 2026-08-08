// test_change_detection_dialog.cpp — change-detection dialog param build
//
// Slice 58 aligned the dialog with the rs:change_detection backend: ratio/CVA
// methods, the makeMask output toggle and the mask parameter section
// (threshold strategy, percentile/statistical K, cleanup, minimum mapping
// unit). This test drives the dialog headlessly and pins buildParams().
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTemporaryDir>

#include <array>
#include <vector>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include "app/dialogs/change_detection_dialog.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_change_detection_dialog";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

QgsRasterLayer *registerRaster( const QString &path, const QString &name )
{
  auto *layer = new QgsRasterLayer( path, name );
  if ( !layer->isValid() )
  {
    delete layer;
    return nullptr;
  }
  QgsProject::instance()->addMapLayer( layer );
  return layer;
}

} // namespace

TEST_CASE( "ChangeDetectionDialog buildParams assembles the operator JSON", "[change_detection_dialog]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString beforePath = dir.filePath( QStringLiteral( "before.tif" ) );
  const QString afterPath = dir.filePath( QStringLiteral( "after.tif" ) );
  std::vector<std::vector<float>> bands( 1, std::vector<float>( 4, 100.0f ) );
  std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
  QString err;
  REQUIRE( writeGdalOutput( beforePath, 2, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );
  REQUIRE( writeGdalOutput( afterPath, 2, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );

  QgsRasterLayer *before = registerRaster( beforePath, QStringLiteral( "before" ) );
  QgsRasterLayer *after = registerRaster( afterPath, QStringLiteral( "after" ) );
  REQUIRE( before != nullptr );
  REQUIRE( after != nullptr );

  ChangeDetectionDialog dialog;
  dialog.setRasterLayer( before );
  auto *beforePick = dialog.findChild<QComboBox *>( QStringLiteral( "cdBeforeCombo" ) );
  auto *afterPick = dialog.findChild<QComboBox *>( QStringLiteral( "cdAfterCombo" ) );
  REQUIRE( beforePick != nullptr );
  REQUIRE( afterPick != nullptr );
  beforePick->setCurrentIndex( beforePick->findData( before->id() ) );
  afterPick->setCurrentIndex( afterPick->findData( after->id() ) );

  auto *outputEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "rsDialogOutputEdit" ) );
  REQUIRE( outputEdit != nullptr );
  outputEdit->setText( dir.filePath( QStringLiteral( "change.tif" ) ) );

  auto *methodCombo = dialog.findChild<QComboBox *>( QStringLiteral( "cdMethodCombo" ) );
  REQUIRE( methodCombo != nullptr );

  auto *maskCheck = dialog.findChild<QCheckBox *>( QStringLiteral( "cdMakeMaskCheck" ) );
  auto *strategyCombo = dialog.findChild<QComboBox *>( QStringLiteral( "cdThresholdMethodCombo" ) );
  auto *percentileSpin = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "cdPercentileSpin" ) );
  auto *kSpin = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "cdStatisticalKSpin" ) );
  auto *minAreaSpin = dialog.findChild<QSpinBox *>( QStringLiteral( "cdMinAreaSpin" ) );
  auto *cleanupCombo = dialog.findChild<QComboBox *>( QStringLiteral( "cdCleanupCombo" ) );
  REQUIRE( maskCheck != nullptr );
  REQUIRE( strategyCombo != nullptr );
  REQUIRE( percentileSpin != nullptr );
  REQUIRE( kSpin != nullptr );
  REQUIRE( minAreaSpin != nullptr );
  REQUIRE( cleanupCombo != nullptr );

  SECTION( "Method list covers the backend methods" )
  {
    QStringList methods;
    for ( int i = 0; i < methodCombo->count(); ++i )
      methods << methodCombo->itemData( i ).toString();
    CHECK( methods.contains( QStringLiteral( "difference" ) ) );
    CHECK( methods.contains( QStringLiteral( "normalized_difference" ) ) );
    CHECK( methods.contains( QStringLiteral( "ratio" ) ) );
    CHECK( methods.contains( QStringLiteral( "cva" ) ) );
    CHECK( methods.contains( QStringLiteral( "change_mask" ) ) );
  }

  SECTION( "No mask by default" )
  {
    const Json::Value params = dialog.buildParams();
    CHECK_FALSE( params.isMember( "makeMask" ) );
    CHECK( params["method"].asString() == "difference" );
  }

  SECTION( "Mask checkbox surfaces the mask parameters with defaults" )
  {
    maskCheck->setChecked( true );
    const Json::Value params = dialog.buildParams();
    CHECK( params["makeMask"].asBool() == true );
    CHECK( params["threshold"].asDouble() == 10.0 );
    CHECK_FALSE( params.isMember( "thresholdMethod" ) ); // manual omitted
    CHECK_FALSE( params.isMember( "minAreaPixels" ) );   // 0 disabled
    CHECK_FALSE( params.isMember( "cleanup" ) );         // none omitted
  }

  SECTION( "Statistical strategy surfaces k" )
  {
    maskCheck->setChecked( true );
    strategyCombo->setCurrentIndex( strategyCombo->findData( QStringLiteral( "statistical" ) ) );
    kSpin->setValue( 3.0 );
    const Json::Value params = dialog.buildParams();
    CHECK( params["thresholdMethod"].asString() == "statistical" );
    CHECK( params["statisticalK"].asDouble() == 3.0 );
    CHECK_FALSE( params.isMember( "percentile" ) );
  }

  SECTION( "Percentile strategy surfaces the percentile" )
  {
    maskCheck->setChecked( true );
    strategyCombo->setCurrentIndex( strategyCombo->findData( QStringLiteral( "percentile" ) ) );
    percentileSpin->setValue( 95.0 );
    const Json::Value params = dialog.buildParams();
    CHECK( params["thresholdMethod"].asString() == "percentile" );
    CHECK( params["percentile"].asDouble() == 95.0 );
  }

  SECTION( "Cleanup and MMU surface when configured" )
  {
    maskCheck->setChecked( true );
    minAreaSpin->setValue( 8 );
    cleanupCombo->setCurrentIndex( cleanupCombo->findData( QStringLiteral( "open" ) ) );
    const Json::Value params = dialog.buildParams();
    CHECK( params["minAreaPixels"].asInt() == 8 );
    CHECK( params["cleanup"].asString() == "open" );
  }

  SECTION( "Legacy change_mask method forces the mask and manual strategy" )
  {
    methodCombo->setCurrentIndex( methodCombo->findData( QStringLiteral( "change_mask" ) ) );
    const Json::Value params = dialog.buildParams();
    CHECK( params["makeMask"].asBool() == true );
    // The strategy is forced back to manual (schema default, omitted).
    CHECK_FALSE( params.isMember( "thresholdMethod" ) );
    CHECK( params["threshold"].asDouble() == 10.0 );
  }

  QgsProject::instance()->removeMapLayer( before );
  QgsProject::instance()->removeMapLayer( after );
}
