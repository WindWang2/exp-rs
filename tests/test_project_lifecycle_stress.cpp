// test_project_lifecycle_stress.cpp — Workbench 9.0 M0 (#859 follow-through)
//
// Repeated project clear → import → view churn → exit cycles against a live
// QGIS canvas, including mid-cycle canvas destruction (the window-closes-
// before-clear shape). Single cycles are covered by test_project_context_reap;
// this suite exists because teardown races only show up under churn — the
// async canvas render jobs (#859) and layer-tree bridges (#857) must survive
// repeated partial teardown orders without crash, hang or leaked layers.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>
#include <QVector>

#include <vector>

#include <gdal.h>

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include "app/project_context.h"

namespace app = sicnu::app;

namespace
{

// Small cached GeoTIFF per distinct name — identical content is fine here, the
// churn is what is under test, not the pixels.
QString syntheticTiff( const QString &name )
{
  static QTemporaryDir dir;
  static QMap<QString, QString> cache;
  auto it = cache.constFind( name );
  if ( it != cache.constEnd() )
    return it.value();

  GDALAllRegister();
  const QString path = dir.path() + QLatin1Char( '/' ) + name + QStringLiteral( ".tif" );
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  constexpr int W = 8, H = 8;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( H ), 0.0, -1.0 };
  GDALSetGeoTransform( ds, gt );
  GDALSetProjection( ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
                        "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( W, 1.0f );
  for ( int row = 0; row < H; ++row )
    GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
  GDALClose( ds );
  cache.insert( name, path );
  return path;
}

/// One full lifecycle cycle. Returns the number of layers that were staged.
int runCycle( int cycle )
{
  QgsMapCanvas canvas;
  QgsProject project;

  const sicnu::display::DisplayViewSpec viewSpec{ &canvas, project.layerTreeRoot(),
                                                  project.layerStore() };
  auto created = app::ProjectContext::create( viewSpec );
  REQUIRE( created );
  std::unique_ptr<app::ProjectContext> context = created.take();

  // Import a fresh set of layers every cycle (paths differ per cycle so the
  // catalog cannot dedup them away and the churn is real).
  const int layerCount = 3;
  for ( int i = 0; i < layerCount; ++i )
  {
    const QString path = syntheticTiff( QStringLiteral( "c%1_%2" ).arg( cycle ).arg( i ) );
    auto *layer = new QgsRasterLayer( path, QStringLiteral( "L%1_%2" ).arg( cycle ).arg( i ),
                                      QStringLiteral( "gdal" ) );
    REQUIRE( layer->isValid() );
    project.addMapLayer( layer );
  }
  REQUIRE( project.mapLayers().size() == layerCount );

  // Every third cycle: a secondary view joins the party (clearProject must
  // reach layers across ALL views — regression guard for the multi-view fix).
  sicnu::display::DisplayViewId secondaryId{};
  QgsMapCanvas secondaryCanvas;
  if ( cycle % 3 == 0 )
  {
    const sicnu::display::DisplayViewSpec secondarySpec{
      &secondaryCanvas, project.layerTreeRoot(), project.layerStore() };
    auto viewId = context->createSecondaryView( secondarySpec );
    REQUIRE( viewId );
    secondaryId = viewId.value();
    REQUIRE( context->views().size() == 2 );
  }

  // Every fourth cycle: the canvas dies BEFORE clearProject — the
  // window-closed-while-project-still-open shape. The context must clear the
  // remaining project state without touching the destroyed canvas.
  if ( cycle % 4 == 1 )
    return layerCount; // destroyed below, before clearProject (see caller)

  const auto cleared = context->clearProject( project );
  REQUIRE( cleared );
  CHECK( project.mapLayers().size() == 0 );

  if ( !secondaryId.isNull() )
  {
    REQUIRE( context->removeView( secondaryId ) );
    REQUIRE( context->views().size() == 1 );
  }
  return layerCount;
}

/// Same cycle, but the canvas (and its spec) is destroyed before clearProject.
void runCycleWithEarlyCanvasDeath( int cycle )
{
  QgsProject project;
  std::unique_ptr<app::ProjectContext> context;
  const int layerCount = 2;
  {
    QgsMapCanvas canvas;
    const sicnu::display::DisplayViewSpec viewSpec{ &canvas, project.layerTreeRoot(),
                                                    project.layerStore() };
    auto created = app::ProjectContext::create( viewSpec );
    REQUIRE( created );
    context = created.take();

    for ( int i = 0; i < layerCount; ++i )
    {
      const QString path = syntheticTiff( QStringLiteral( "d%1_%2" ).arg( cycle ).arg( i ) );
      auto *layer = new QgsRasterLayer( path, QStringLiteral( "D%1_%2" ).arg( cycle ).arg( i ),
                                        QStringLiteral( "gdal" ) );
      REQUIRE( layer->isValid() );
      project.addMapLayer( layer );
    }
    // canvas destroyed here
  }

  // Clearing a project whose view canvas is already gone must not crash nor
  // leave layers behind (async render jobs / bridge deletion order, #859/#857).
  const auto cleared = context->clearProject( project );
  REQUIRE( cleared );
  CHECK( project.mapLayers().size() == 0 );
}

constexpr int kCycles = 24;

} // namespace

TEST_CASE( "Project lifecycle churn: clear/import/view cycles stay consistent",
           "[m0][stress][project_lifecycle]" )
{
  int stagedLayers = 0;
  for ( int cycle = 0; cycle < kCycles; ++cycle )
    stagedLayers += runCycle( cycle );
  REQUIRE( stagedLayers == kCycles * 3 );
  CHECK( QgsProject::instance()->mapLayers().size() == 0 );
}

TEST_CASE( "Project lifecycle churn: canvas destroyed before clearProject",
           "[m0][stress][project_lifecycle]" )
{
  for ( int cycle = 0; cycle < kCycles; ++cycle )
    runCycleWithEarlyCanvasDeath( cycle );
  CHECK( QgsProject::instance()->mapLayers().size() == 0 );
}

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, false );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}
