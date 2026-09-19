// test_map_tool_canvas_item.cpp — issue #1048.
//
// A QgsRubberBand / QgsVertexMarker created with a canvas is a scene-owned
// QGraphicsItem. QgsMapCanvas::~QgsMapCanvas deletes every child QgsMapTool
// while the scene still exists, so a tool destructor may free its item
// normally. When the canvas is gone (parent widget teardown order, deferred
// callbacks, a reparented tool) the scene teardown already reclaimed the item
// and the raw pointer dangles: the guard must drop the pointer WITHOUT
// deleting. And when the canvas is alive, the same call must delete.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QPointer>

#include "app/map_tools/map_tool_canvas_item.h"

#include <qgsmapcanvas.h>
#include <qgsmaptool.h>
#include <qgsrubberband.h>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_map_tool_canvas_item";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  static QApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QApplication( fake_argc, fake_argv );
  return app;
}

/// Mirrors the pre-fix RsRoiSpectrumTool / QgsMapToolCapture contract: the
/// destructor frees the scene item unconditionally. This is the shape that
/// double-freed when ~QgsMapCanvas deleted scene items before child tools.
class LegacyStyleTool : public QgsMapTool
{
  public:
    explicit LegacyStyleTool( QgsMapCanvas *canvas )
      : QgsMapTool( canvas )
      , mBand( new QgsRubberBand( canvas, Qgis::GeometryType::Polygon ) )
    {
    }

    ~LegacyStyleTool() override
    {
      delete mBand;
      mBand = nullptr;
    }

    QgsRubberBand *band() const { return mBand; }

  private:
    QgsRubberBand *mBand = nullptr;
};

/// New-style tool: frees through the #1048 guard, and can simulate the
/// "canvas destructor already nulled my canvas" state.
class GuardedTool : public QgsMapTool
{
  public:
    explicit GuardedTool( QgsMapCanvas *canvas )
      : QgsMapTool( canvas )
      , mBand( new QgsRubberBand( canvas, Qgis::GeometryType::Polygon ) )
    {
    }

    ~GuardedTool() override
    {
      sicnu::app::deleteToolCanvasItem( this, mBand );
    }

    QgsRubberBand *band() const { return mBand; }

    /// Deletes the OWNED member through the guard (the contract: the guard
    /// nulls the pointer it is given, so callers must pass the owner).
    bool deleteOwnedBand() { return sicnu::app::deleteToolCanvasItem( this, mBand ); }

    /// QgsMapCanvas nulls this exact member before deleting its scene items
    /// in ~QgsMapCanvas; a test can force that state without destroying the
    /// canvas itself (and then owns the cleanup of the still-live band).
    void simulateCanvasGone() { mCanvas = nullptr; }

  private:
    QgsRubberBand *mBand = nullptr;
};

} // namespace

TEST_CASE( "deleteToolCanvasItem: live canvas deletes the item and nulls the pointer", "[maptool][lifetime][1048]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  canvas.resize( 200, 200 );
  GuardedTool tool( &canvas );

  REQUIRE( tool.band() != nullptr );

  CHECK( tool.deleteOwnedBand() );
  CHECK( tool.band() == nullptr );
}

TEST_CASE( "deleteToolCanvasItem: destroyed canvas releases the pointer without deleting", "[maptool][lifetime][1048]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  canvas.resize( 200, 200 );
  GuardedTool tool( &canvas );
  QgsRubberBand *band = tool.band();
  REQUIRE( band != nullptr );

  // Canvas gone: the scene owns the item now. The guard must NOT delete.
  tool.simulateCanvasGone();
  CHECK_FALSE( tool.deleteOwnedBand() );
  CHECK( tool.band() == nullptr ); // pointer dropped, no double free

  // The band is still alive and still scene-owned: clean up deliberately.
  // (Skipping this is exactly the #1048 double-free; deleting it once here
  // proves the guard did not already free it.)
  delete band;
}

TEST_CASE( "QgsMapCanvas destruction deletes child tools before scene items (#1048)", "[maptool][lifetime][1048]" )
{
  ensureApp();
  // The tool's destructor deletes its scene-owned rubber band unconditionally.
  // Before the canvas fix, ~QgsMapCanvas deleted the scene items first and
  // this destructor then double-freed the band. A crash here is the
  // regression; a clean exit proves the tool was destroyed while the scene
  // (and therefore the band) still existed.
  auto *canvas = new QgsMapCanvas();
  canvas->resize( 200, 200 );
  QPointer<LegacyStyleTool> tool = new LegacyStyleTool( canvas );
  REQUIRE( tool->band() != nullptr );

  delete canvas; // must destroy the tool while the scene is intact

  CHECK( tool.isNull() ); // tool died with its canvas, exactly once
}
