// Workbench 7.0 — temporal scene model (goal §D + §H): date filtering,
// pagination bounds for large collections, and truthful unknown-date/QA cells.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/temporal_scene_model.h"

#include <QCoreApplication>

namespace
{

QCoreApplication &testApp()
{
  static int argc = 0;
  static QCoreApplication app( argc, nullptr );
  return app;
}

sicnu::temporal::TemporalSceneRef makeScene( const QString &path, int year, int month, int day,
                                             double cloud = -1.0 )
{
  sicnu::temporal::TemporalSceneRef scene;
  scene.path = path;
  const QString iso =
    QStringLiteral( "%1-%2-%3" ).arg( year, 4, 10, QLatin1Char( '0' ) )
      .arg( month, 2, 10, QLatin1Char( '0' ) )
      .arg( day, 2, 10, QLatin1Char( '0' ) );
  scene.time = sicnu::temporal::parseAcquisitionTime( iso );
  scene.cloudCoverPercent = cloud;
  return scene;
}

} // namespace

TEST_CASE( "TemporalSceneModel filters by date window", "[temporal][scene-model]" )
{
  testApp();
  sicnu::app::TemporalSceneModel model;
  QVector<sicnu::temporal::TemporalSceneRef> scenes;
  scenes.append( makeScene( QStringLiteral( "s1.tif" ), 2024, 1, 10 ) );
  scenes.append( makeScene( QStringLiteral( "s2.tif" ), 2024, 3, 15 ) );
  scenes.append( makeScene( QStringLiteral( "s3.tif" ), 2024, 6, 20 ) );
  // A scene without a resolvable date stays in the unfiltered view but can
  // never claim a place inside a date-filtered window.
  sicnu::temporal::TemporalSceneRef undated;
  undated.path = QStringLiteral( "undated.tif" );
  scenes.append( undated );
  model.setScenes( scenes );

  CHECK( model.totalScenes() == 4 );

  model.setDateFilter( QDate( 2024, 2, 1 ), QDate( 2024, 12, 31 ) );
  CHECK( model.totalScenes() == 2 );
  CHECK( model.sceneAtRow( 0 )->path == QStringLiteral( "s2.tif" ) );
  CHECK( model.sceneAtRow( 1 )->path == QStringLiteral( "s3.tif" ) );

  model.setDateFilter( QDate(), QDate() );
  CHECK( model.totalScenes() == 4 );
}

TEST_CASE( "TemporalSceneModel pages large collections", "[temporal][scene-model][scale]" )
{
  testApp();
  sicnu::app::TemporalSceneModel model;
  QVector<sicnu::temporal::TemporalSceneRef> scenes;
  scenes.reserve( 100000 );
  for ( int i = 0; i < 100000; ++i )
  {
    // Deterministic synthetic schedule: 2 scenes/day for ~137 years of
    // calendar coverage — logical scale without 100k files on disk.
    const int day = i / 2;
    scenes.append( makeScene( QStringLiteral( "s%1.tif" ).arg( i ), 2000 + day / 365,
                              1 + ( day % 365 ) / 31, 1 + ( day % 30 ) ) );
  }
  model.setScenes( scenes );

  CHECK( model.totalScenes() == 100000 );
  CHECK( model.pageCount() == ( 100000 + sicnu::app::TemporalSceneModel::kPageSize - 1 ) /
                                sicnu::app::TemporalSceneModel::kPageSize );
  // Visible rows are bounded by the page size (goal §D/§H).
  CHECK( model.rowCount() == sicnu::app::TemporalSceneModel::kPageSize );

  model.setPage( 499 );
  CHECK( model.page() == 499 );
  CHECK( model.rowCount() == 100000 - 499 * sicnu::app::TemporalSceneModel::kPageSize );
  CHECK( model.sceneAtRow( 0 )->path == QStringLiteral( "s99800.tif" ) );

  // Out-of-range pages clamp instead of corrupting state.
  model.setPage( 99999 );
  CHECK( model.page() == model.pageCount() - 1 );
  model.setPage( -3 );
  CHECK( model.page() == 0 );

  // Last page row count stays exact after returning from a far page.
  model.setPage( 1 );
  CHECK( model.rowCount() == sicnu::app::TemporalSceneModel::kPageSize );
}

TEST_CASE( "TemporalSceneModel renders QA and unknown values truthfully",
           "[temporal][scene-model]" )
{
  testApp();
  sicnu::app::TemporalSceneModel model;
  QVector<sicnu::temporal::TemporalSceneRef> scenes;
  scenes.append( makeScene( QStringLiteral( "clear.tif" ), 2024, 5, 1, 12.4 ) );
  scenes.append( makeScene( QStringLiteral( "cloudy.tif" ), 2024, 5, 2, 85.0 ) );
  sicnu::temporal::TemporalSceneRef unreported;
  unreported.path = QStringLiteral( "noreport.tif" );
  unreported.time = sicnu::temporal::parseAcquisitionTime( QStringLiteral( "2024-05-03" ) );
  scenes.append( unreported );
  model.setScenes( scenes );

  const QVariant cloud0 = model.data( model.index( 0, sicnu::app::TemporalSceneModel::Cloud ),
                                      Qt::DisplayRole );
  CHECK( cloud0.toString() == QStringLiteral( "12%" ) );
  const QVariant cloudNone =
    model.data( model.index( 2, sicnu::app::TemporalSceneModel::Cloud ), Qt::DisplayRole );
  CHECK( cloudNone.toString() == QStringLiteral( "未报告" ) );
}
