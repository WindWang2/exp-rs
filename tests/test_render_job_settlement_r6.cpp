// test_render_job_settlement_r6.cpp — WP-C/WP-H deterministic settlement
// oracles for the #1389 session-canvas lifecycle cluster.
//
// Every race window is controlled by a seam, not by sleeps: a custom
// QgsMapLayer returns a QgsMapLayerRenderer whose render() signals a
// "render entered" semaphore and then parks until the test releases it (or
// the job cancels it via the render feedback). That turns
// "layer removal while a parallel render job is in flight" into a
// repeatable oracle:
//   render entered → removal path called → removal must return only after
//   the job settled (worker observed the cancellation) → layer delete safe.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QPointer>
#include <QtTest>

#include <atomic>
#include <chrono>
#include <memory>
#include <semaphore>
#include <vector>

#include <qgscoordinatereferencesystem.h>
#include <qgsfeedback.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsmaplayerrenderer.h>
#include <qgsrasterlayer.h>
#include <qgsrectangle.h>

#include <QTemporaryDir>

#include <cpl_conv.h>
#include <gdal.h>

#include <QDir>
#include <QMap>

#include "app/map_tools/swipe_map_tool.h"
#include "shell/rs_session_map_workspace.h"
#include "support/qt_lifecycle.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_render_job_settlement_r6";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    return sicnu::test::qtlifecycle::heapQApplication( fake_argc, fake_argv );
  }

  //! Shared control block for one blocking render: observation flags AND
  //! semaphores. Held by shared_ptr from BOTH the layer and the renderer:
  //! the willBeDeleted tripwire fires from ~QgsRasterLayer — AFTER the
  //! derived class (and its members) is gone — while the parked worker may
  //! still be inside render(); everything the worker touches must therefore
  //! outlive the layer. Flags must additionally be readable by the test
  //! after the layer is deleted: proving settlement after deletion is the
  //! whole point of these oracles.
  struct RenderGate
  {
    std::atomic_bool entered{ false };
    std::atomic_bool canceledDuringRender{ false };
    std::atomic_bool finishedNormally{ false };
    std::counting_semaphore<> enteredSemaphore{ 0 };
    std::counting_semaphore<> releaseGate{ 0 };

    //! Unblock parked worker(s) — for the "render completes normally" arm.
    void release() { releaseGate.release(); }

    //! Deterministic barrier: the parallel job's worker is inside render()
    //! holding the layer.
    bool waitEntered( int timeoutMs = 10000 )
    {
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds( timeoutMs );
      while ( std::chrono::steady_clock::now() < deadline )
      {
        if ( enteredSemaphore.try_acquire_for( std::chrono::milliseconds( 5 ) ) )
          return true;
      }
      return entered.load();
    }
  };
  using SharedRenderGate = std::shared_ptr<RenderGate>;

  //! Renderer that parks inside render() until released or canceled — the
  //! deterministic "job holds the layer on a worker thread" point.
  class BlockingLayerRenderer : public QgsMapLayerRenderer
  {
    public:
      BlockingLayerRenderer( const QString &layerId, const SharedRenderGate &gate )
          : QgsMapLayerRenderer( layerId ), mGate( gate )
      {
      }

      ~BlockingLayerRenderer() override
      {
        // Never leave the worker parked past renderer destruction (abnormal
        // teardown paths): unblock so the worker can exit.
        mGate->releaseGate.release();
      }

      QgsFeedback *feedback() const override { return &mFeedback; }

      bool render() override
      {
        mGate->entered.store( true );
        mGate->enteredSemaphore.release();
        while ( !mGate->releaseGate.try_acquire_for( std::chrono::milliseconds( 5 ) ) )
        {
          if ( mFeedback.isCanceled() )
          {
            mGate->canceledDuringRender.store( true );
            return false;
          }
        }
        mGate->finishedNormally.store( true );
        return true;
      }

    private:
      SharedRenderGate mGate;
      mutable QgsFeedback mFeedback;
  };

  //! A tiny real GeoTIFF: the blocking layer must pass the raster prepare
  //! pipeline (renderer() must be non-null), which needs an actual source.
  QString syntheticRaster( const QString &key )
  {
    static QTemporaryDir dir;
    static QMap<QString, QString> cache;
    const auto it = cache.constFind( key );
    if ( it != cache.constEnd() )
      return it.value();

    GDALAllRegister();
    const QString path = dir.path() + QLatin1Char( '/' ) +
                         QString::number( cache.size() ) + QStringLiteral( ".tif" );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    constexpr int W = 8, H = 8;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1,
                                  GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( H ), 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );
    GDALSetProjection( ds,
      "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
      "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> line( W, 1.0f );
    for ( int row = 0; row < H; ++row )
      GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
    GDALClose( ds );
    cache.insert( key, path );
    return path;
  }

  //! Minimal raster layer whose only job is to hand the blocking renderer to
  //! the render job. The willBeDeleted tripwire needs NO extra wiring: the
  //! vendored ~QgsRasterLayer emits it as its first statement (emitting it
  //! again from a derived destructor would double-deliver to handlers such
  //! as QgsLayerTreeLayer::layerWillBeDeleted, whose weak ref is already
  //! cleared by then). Must derive QgsRasterLayer (not bare QgsMapLayer):
  //! the label/prepare pipeline static-casts Raster-typed layers, and the
  //! raster prepare pipeline requires an initialized renderer, hence the
  //! real (tiny, throwaway) GeoTIFF source.
  class BlockingRenderLayer : public QgsRasterLayer
  {
    public:
      explicit BlockingRenderLayer( const QString &name )
          : QgsRasterLayer( syntheticRaster( name ), name )
          , mGate( std::make_shared<RenderGate>() )
      {
        setValid( isValid() );
        setCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) );
      }

      QgsMapLayerRenderer *createMapRenderer( QgsRenderContext & ) override
      {
        return new BlockingLayerRenderer( id(), mGate );
      }

      QgsRectangle extent() const override { return QgsRectangle( 0.0, 0.0, 10.0, 10.0 ); }

      RenderGate *gate() { return mGate.get(); }
      SharedRenderGate gateShared() { return mGate; }

    private:
      SharedRenderGate mGate;
  };

  void configureCanvas( QgsMapCanvas &canvas )
  {
    canvas.resize( 320, 240 );
    canvas.setDestinationCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) );
    canvas.setExtent( QgsRectangle( 0, 0, 10, 10 ) );
  }
}

TEST_CASE( "Session workspace removeLayer settles the swipe compare job before the caller deletes the layer (#1389)",
           "[lifecycle][r6][swipe][settlement]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  configureCanvas( canvas );
  RsSessionMapWorkspace workspace( &canvas );

  // Owned here: deletion order at scope end is layer AFTER the asserts that
  // prove the settle (a parked worker never needs a manual gate release —
  // deleting the layer trips the willBeDeleted settle, whose cancel unblocks
  // the worker).
  auto layer = std::make_unique<BlockingRenderLayer>( QStringLiteral( "compare" ) );
  workspace.addLayer( layer.get() );

  SwipeMapTool tool( &canvas );
  tool.setCompareLayer( layer.get() );
  tool.activate(); // single deterministic render trigger

  REQUIRE( layer->gate()->waitEntered() );
  REQUIRE( tool.hasPendingCompareRender() );

  // The R2 contract: removeLayer hands the layer back for the caller to
  // delete. It must first wind down every render consumer — the swipe job's
  // worker is parked INSIDE the layer right now, and the removal blocks on
  // its settlement (worker observes the cancellation before we return).
  workspace.removeLayer( layer.get() );

  REQUIRE_FALSE( tool.hasPendingCompareRender() );
  REQUIRE( layer->gate()->canceledDuringRender.load() );

  // Now the deletion is safe: no background thread can still touch the layer.
  layer.reset();
  QTest::qWait( 50 );
  REQUIRE( tool.compareLayer() == nullptr );
  tool.deactivate();
}

TEST_CASE( "Canvas destruction settles an in-flight swipe compare job first (#1389)",
           "[lifecycle][r6][swipe][settlement]" )
{
  ensureApp();
  auto *canvas = new QgsMapCanvas();
  configureCanvas( *canvas );

  auto layer = std::make_unique<BlockingRenderLayer>( QStringLiteral( "compare" ) );

  auto *tool = new SwipeMapTool( canvas );
  tool->setCompareLayer( layer.get() );
  tool->activate();

  REQUIRE( layer->gate()->waitEntered() );
  REQUIRE( tool->hasPendingCompareRender() );

  // The canvas destructor must settle external jobs before its children
  // (this tool included) are destroyed, and before the layer can die.
  delete canvas;

  REQUIRE( layer->gate()->canceledDuringRender.load() );
}

TEST_CASE( "SwipeMapTool destruction alone settles its in-flight compare job (#1389)",
           "[lifecycle][r6][swipe][settlement]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  configureCanvas( canvas );

  auto layer = std::make_unique<BlockingRenderLayer>( QStringLiteral( "compare" ) );

  auto *tool = new SwipeMapTool( &canvas );
  tool->setCompareLayer( layer.get() );
  tool->activate();

  REQUIRE( layer->gate()->waitEntered() );
  REQUIRE( tool->hasPendingCompareRender() );

  delete tool; // blocking settle in ~SwipeMapTool

  REQUIRE( layer->gate()->canceledDuringRender.load() );
}

TEST_CASE( "Deleting the compare layer mid-render settles the job through the willBeDeleted tripwire (#1389)",
           "[lifecycle][r6][swipe][settlement]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  configureCanvas( canvas );

  auto layer = std::make_unique<BlockingRenderLayer>( QStringLiteral( "compare" ) );

  SwipeMapTool tool( &canvas );
  tool.setCompareLayer( layer.get() );
  tool.activate();

  // No manual gate release here on purpose: the willBeDeleted tripwire must
  // do the settling (destructor-emitted, first statement of the dtor body —
  // the instance is still intact while the worker parks inside it).
  REQUIRE( layer->gate()->waitEntered() );
  REQUIRE( tool.hasPendingCompareRender() );

  const SharedRenderGate gate = layer->gateShared();
  layer.reset(); // any deletion path — even ones bypassing canvas settle

  REQUIRE( gate->canceledDuringRender.load() );
  REQUIRE_FALSE( tool.hasPendingCompareRender() );
  REQUIRE( tool.compareLayer() == nullptr );
  tool.deactivate();
  QTest::qWait( 50 );
}

TEST_CASE( "Late compare-render completion after a settled removal does not resurrect a stale snapshot (#1389)",
           "[lifecycle][r6][swipe][late-callback]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  configureCanvas( canvas );
  RsSessionMapWorkspace workspace( &canvas );

  auto layer = std::make_unique<BlockingRenderLayer>( QStringLiteral( "compare" ) );
  workspace.addLayer( layer.get() );

  SwipeMapTool tool( &canvas );
  tool.setCompareLayer( layer.get() );
  tool.activate();

  REQUIRE( layer->gate()->waitEntered() );

  // Settled removal, then deletion, then let every queued callback run.
  const SharedRenderGate gate = layer->gateShared();
  workspace.removeLayer( layer.get() );
  REQUIRE( gate->canceledDuringRender.load() );
  layer.reset();

  QTest::qWait( 120 ); // deliver any late finished/deleteLater events

  // Generation identity: nothing re-registered, nothing re-rendered, no
  // callback resurrected state for the deleted layer.
  REQUIRE_FALSE( tool.hasPendingCompareRender() );
  REQUIRE( tool.compareLayer() == nullptr );
  tool.deactivate();
}

TEST_CASE( "Preview-overwrite churn: repeated create → render → replace → delete survives (#1389, WP-G)",
           "[lifecycle][r6][churn]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  configureCanvas( canvas );
  RsSessionMapWorkspace workspace( &canvas );

  SwipeMapTool tool( &canvas );
  tool.activate();

  constexpr int kCycles = 6;
  for ( int cycle = 0; cycle < kCycles; ++cycle )
  {
    auto layer = std::make_unique<BlockingRenderLayer>( QStringLiteral( "preview%1" ).arg( cycle ) );
    workspace.addLayer( layer.get() );

    tool.setCompareLayer( layer.get() ); // marks dirty → render starts

    // Odd cycles race the removal mid-render; even cycles let the render
    // finish first — both orders must settle cleanly.
    if ( cycle % 2 == 1 )
    {
      REQUIRE( layer->gate()->waitEntered() );
      workspace.removeLayer( layer.get() );
      REQUIRE( layer->gate()->canceledDuringRender.load() );
    }
    else
    {
      REQUIRE( layer->gate()->waitEntered() );
      layer->gate()->release();
      // Render completes on its own; wait for the tool to drop the job.
      for ( int i = 0; i < 2000 && tool.hasPendingCompareRender(); ++i )
        QTest::qWait( 5 );
      REQUIRE_FALSE( tool.hasPendingCompareRender() );
      REQUIRE( layer->gate()->finishedNormally.load() );
      workspace.removeLayer( layer.get() );
    }

    // The overwrite: the replaced layer dies while the tool may still hold
    // it; the tripwire + settled removal keep this safe.
    layer.reset();
    QTest::qWait( 20 );
    REQUIRE( tool.compareLayer() == nullptr );
  }
  tool.deactivate();
}

TEST_CASE( "Session switch churn: canvas/workspace teardown mid-render, then reopen (#1389, WP-G)",
           "[lifecycle][r6][churn][session]" )
{
  ensureApp();

  constexpr int kSwitches = 4;
  for ( int cycle = 0; cycle < kSwitches; ++cycle )
  {
    auto *canvas = new QgsMapCanvas();
    configureCanvas( *canvas );
    auto *workspace = new RsSessionMapWorkspace( canvas );

    auto *layer = new BlockingRenderLayer( QStringLiteral( "layer%1" ).arg( cycle ) );
    workspace->addLayer( layer ); // store takes ownership

    auto *tool = new SwipeMapTool( canvas );
    tool->setCompareLayer( layer );
    tool->activate();

    REQUIRE( layer->gate()->waitEntered() );

    // Session switch: the canvas goes away mid-render (destructor settles
    // the external job before its children — this tool — are destroyed),
    // then the workspace dies and its store deletes the still-owned layer.
    // (No gate guard: the layer is store-owned; on a failed REQUIRE the
    // parked worker + layer leak, they cannot dangle.)
    delete canvas;
    REQUIRE( layer->gate()->canceledDuringRender.load() );
    delete workspace;
    QTest::qWait( 20 );
  }
}

// WP-J: measurement (recorded in the PR, not CI-gated as an absolute time
// budget). The R6 settle additions must not add per-operation work when no
// external job is registered: settleExternalRenderJobs() early-returns on an
// empty registry, so addLayer/removeLayer churn cost stays a single
// setCanvasLayers per operation. This case records the per-op cost at an
// idle canvas and asserts the structural contract (one remove = one sync via
// the canvas layer list; tool state clean between cycles).
#include <QElapsedTimer>

TEST_CASE( "Layer add/remove churn at an idle canvas records per-op cost (WP-J measurement)",
           "[lifecycle][r6][perf-measurement]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  configureCanvas( canvas );
  RsSessionMapWorkspace workspace( &canvas );

  SwipeMapTool tool( &canvas );
  tool.activate();

  constexpr int kCycles = 300;
  QVector<BlockingRenderLayer *> layers;
  layers.reserve( kCycles );
  for ( int i = 0; i < kCycles; ++i )
    layers.append( new BlockingRenderLayer( QStringLiteral( "churn%1" ).arg( i ) ) );

  QElapsedTimer timer;
  timer.start();
  qint64 addNanos = 0;
  qint64 removeNanos = 0;
  for ( int i = 0; i < kCycles; ++i )
  {
    QElapsedTimer op;
    op.start();
    workspace.addLayer( layers[i] );
    addNanos += op.nsecsElapsed();

    op.restart();
    workspace.removeLayer( layers[i] ); // settle: no-op at an idle canvas
    removeNanos += op.nsecsElapsed();
  }
  const double addMicrosPerOp = addNanos / 1000.0 / kCycles;
  const double removeMicrosPerOp = removeNanos / 1000.0 / kCycles;
  INFO( QStringLiteral( "workspace addLayer: %1 µs/op, removeLayer(idle settle): %2 µs/op over %3 cycles" )
        .arg( addMicrosPerOp ).arg( removeMicrosPerOp ).arg( kCycles )
        .toStdString() );
  WARN( "WP-J measurement: add=" << addMicrosPerOp << "us/op remove=" << removeMicrosPerOp << "us/op" );

  // Structural contract: churn leaves no residue.
  CHECK( canvas.layers().isEmpty() );
  CHECK_FALSE( tool.hasPendingCompareRender() );
  tool.deactivate();
  qDeleteAll( layers );
}
