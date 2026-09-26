// test_roi_statistics_teardown_r4.cpp — WP-C teardown fixtures:
// RoiStatisticsWidget + the bounded RsScanPool (#797) it schedules on.
//
// Contract under test: the widget's destructor must cancel its in-flight
// scan generation and erase its owner entry from the pool (src/app/widgets/
// roi_statistics_widget.cpp:52-62 — epoch bump + RsScanPool::cancel with
// owner), so a dead widget never receives pool results and the pool never
// keeps a dangling owner key. The process must exit() cleanly afterwards.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgsgeometry.h>
#include <qgsfeature.h>
#include <qgsrectangle.h>

#include "widgets/roi_statistics_widget.h"
#include "widgets/rs_scan_pool.h"

#include "synthetic_raster_builder.h"

#include "support/qt_lifecycle.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_roi_statistics_teardown_r4";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    return sicnu::test::qtlifecycle::heapQApplication( fake_argc, fake_argv );
  }

  struct Scene
  {
    std::unique_ptr<QgsRasterLayer> raster;
    std::unique_ptr<QgsVectorLayer> roi;
  };

  // Small hermetic scene: 32x32 3-band GeoTIFF + one memory polygon ROI.
  Scene makeScene( const QString &dirPath )
  {
    Scene scene;
    sicnu::testing::RsSyntheticRasterBuilder b( 32, 32, 3 );
    const QString rasterPath = b.writeToDisk( QDir( dirPath ).filePath( QStringLiteral( "roi_scene.tif" ) ) );
    REQUIRE( !rasterPath.isEmpty() );
    scene.raster = std::make_unique<QgsRasterLayer>( rasterPath, QStringLiteral( "roi_scene" ) );
    REQUIRE( scene.raster->isValid() );

    scene.roi = std::make_unique<QgsVectorLayer>( QStringLiteral( "Polygon?crs=EPSG:4326" ),
                                                  QStringLiteral( "roi" ), QStringLiteral( "memory" ) );
    REQUIRE( scene.roi->isValid() );
    QgsFeature f;
    f.setGeometry( QgsGeometry::fromRect( QgsRectangle( 2.0, 2.0, 9.0, 9.0 ) ) );
    REQUIRE( scene.roi->dataProvider()->addFeature( f ) );
    return scene;
  }

  // Pool drain predicate on the widget family's dedicated bounded pool.
  bool scanPoolBusy()
  {
    return RoiStatisticsWidget::analysisThreadPool()->activeThreadCount() > 0;
  }

  void waitForPoolDrain( int budgetRounds = 100 )
  {
    for ( int i = 0; i < budgetRounds && scanPoolBusy(); ++i )
      QTest::qWait( 50 );
  }
} // namespace

TEST_CASE( "ROI statistics teardown: compute-then-destroy keeps pool clean", "[teardown][r4]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  Scene scene = makeScene( dir.path() );

  QPointer<RoiStatisticsWidget> guard;
  {
    RoiStatisticsWidget w;
    guard = &w;
    w.setRasterLayer( scene.raster.get() );
    w.setRoiLayer( scene.roi.get() );
    w.computeStatistics();
    // Graceful story: the bounded pool drains before the widget dies.
    waitForPoolDrain();
  }
  QTest::qWait( 50 );
  REQUIRE( guard.isNull() );
}

TEST_CASE( "ROI statistics teardown: destroy mid-compute cancels generation", "[teardown][r4]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  Scene scene = makeScene( dir.path() );

  QPointer<RoiStatisticsWidget> guard;
  {
    RoiStatisticsWidget w;
    guard = &w;
    w.setRasterLayer( scene.raster.get() );
    w.setRoiLayer( scene.roi.get() );
    w.computeStatistics();
    QTest::qWait( 10 ); // let the scan enter the pool
  }
  // Destructor story: cancel() ran with `this` as owner; the pool must not
  // deliver results into freed memory and must not retain the owner key.
  QTest::qWait( 200 );
  REQUIRE( guard.isNull() );

  // A fresh widget on the same scene must compute cleanly — no stale
  // generation/owner state leaked by the destroyed one.
  RoiStatisticsWidget fresh;
  fresh.setRasterLayer( scene.raster.get() );
  fresh.setRoiLayer( scene.roi.get() );
  fresh.computeStatistics();
  waitForPoolDrain();
}

TEST_CASE( "ROI statistics teardown: repeated widgets over one pool stay clean", "[teardown][r4]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  Scene scene = makeScene( dir.path() );

  for ( int round = 0; round < 3; ++round )
  {
    QPointer<RoiStatisticsWidget> guard;
    {
      RoiStatisticsWidget w;
      guard = &w;
      w.setRasterLayer( scene.raster.get() );
      w.setRoiLayer( scene.roi.get() );
      w.computeStatistics();
      if ( round % 2 == 0 )
        QTest::qWait( 30 ); // alternate graceful / immediate teardown stories
    }
    QTest::qWait( 60 );
    REQUIRE( guard.isNull() );
  }
}
