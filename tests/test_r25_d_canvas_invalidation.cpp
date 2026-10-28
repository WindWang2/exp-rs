// tests/test_r25_d_canvas_invalidation.cpp — R25-D W3 evidence pin
//
// Canvas invalidation discipline for visibility edits (Oracle 5, structural):
// the view's QgsLayerTreeMapCanvasBridge coalesces tree changes at
// event-loop granularity (deferredSetCanvasLayers, queued + pending guard)
// and the manager defers its OWN sync inside ScopedBatchUpdate. This pins
// the contract with counts, not wall time:
//   - N visibility toggles in one event-loop turn ⇒ at most 2 canvas layer
//     set changes (bridge coalesce + batch-exit sync), never N;
//   - the settled canvas layer list is exactly the visible set;
//   - the manager's batch bookkeeping still performs exactly one sync.
// A regression that reintroduces per-toggle canvas refreshes (bridge
// replaced, deferral dropped) fails here loudly instead of shipping a
// render-invalidation storm.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayerstore.h>
#include <qgsproject.h>

#include "app/display/qgis_display_manager.h"
#include "data/data_manager.h"

#include <gdal.h>
#include <cpl_conv.h>

using sicnu::data::DataManager;
using sicnu::data::RegisterRequest;
using sicnu::data::SourceDescriptor;
using sicnu::display::QgisDisplayManager;

namespace
{

void ensureQgisApplication()
{
  if ( QCoreApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_r25_d_canvas_invalidation";
  static char *argv[] = {applicationName, nullptr};
  static auto *application = new QgsApplication( argc, argv, true );
  (void)application;
  QgsApplication::initQgis();
}

QString syntheticRaster( const QTemporaryDir &dir, int index )
{
  GDALAllRegister();
  const QString path = dir.filePath( QStringLiteral( "%1.tif" ).arg( index ) );
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  constexpr int W = 16, H = 16;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = {0.0, 1.0, 0.0, static_cast<double>( H ), 0.0, -1.0};
  GDALSetGeoTransform( ds, gt );
  GDALClose( ds );
  return path;
}

} // namespace

TEST_CASE( "visibility edits coalesce into bounded canvas invalidations", "[r25d][perf][invalidation]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  DataManager dataManager;
  QgisDisplayManager displayManager( &dataManager );
  QgsMapCanvas canvas;
  QgsLayerTree tree;
  QgsMapLayerStore store;
  sicnu::display::DisplayViewSpec spec;
  spec.canvas = &canvas;
  spec.layerTree = &tree;
  spec.layerStore = &store;
  const auto created = displayManager.createView( spec );
  REQUIRE( created );
  const auto viewId = created.value();

  constexpr int N = 12;
  std::vector<sicnu::display::DisplayLayerId> layerIds;
  for ( int i = 0; i < N; ++i )
  {
    SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = syntheticRaster( dir, i );
    const auto registered = dataManager.registerSource( RegisterRequest{source} );
    REQUIRE_FALSE( registered.assetId.isNull() );
    const auto added =
      displayManager.addLayer( viewId, registered.assetId, { QStringLiteral( "L%1" ).arg( i ), true } );
    REQUIRE( added );
    layerIds.push_back( added.value() );
  }
  REQUIRE( canvas.layers().size() == N );

  QSignalSpy layersSpy( &canvas, &QgsMapCanvas::layersChanged );
  // Drain any queued bridge work from the add phase.
  QCoreApplication::processEvents();

  SECTION( "N in-batch toggles settle with bounded invalidations and one sync" )
  {
    const quint64 syncBefore = displayManager.canvasLayerSyncCount( viewId );
    {
      auto batch = displayManager.createBatchUpdate( viewId );
      for ( int i = 0; i < N; ++i )
        REQUIRE( displayManager.setLayerVisible( layerIds[i], i % 2 == 0 ) );
    } // batch exit: exactly one deferred manager sync
    QCoreApplication::processEvents();

    CHECK( displayManager.canvasLayerSyncCount( viewId ) == syncBefore + 1 );
    // Bridge coalescing: the whole batch produced at most two canvas layer
    // set changes (the queued bridge pass + the batch-exit sync) — never N.
    CHECK( layersSpy.count() <= 2 );
    // The settled canvas list is exactly the visible half.
    CHECK( canvas.layers().size() == N / 2 );
  }

  SECTION( "out-of-batch toggles still coalesce per event-loop turn" )
  {
    for ( int i = 0; i < N; ++i )
      REQUIRE( displayManager.setLayerVisible( layerIds[i], false ) );
    QCoreApplication::processEvents();
    // All N toggles landed in ONE event-loop turn: the deferred bridge pass
    // coalesces them; per-toggle refreshes would show N here.
    CHECK( layersSpy.count() <= 2 );
    CHECK( canvas.layers().isEmpty() );
  }
}
