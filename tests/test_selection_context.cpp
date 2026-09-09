// Workbench 5.0 — SelectionContext rules + aggregation (Milestone B)
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/selection_context.h"
#include "app/workbench/workbench_host.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <qgsmaplayer.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_selection_context";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  static QApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QApplication( fake_argc, fake_argv );
  return app;
}

namespace ContextRules = sicnu::app::ContextRules;
using sicnu::app::SelectionContextSnapshot;

SelectionContextSnapshot snapshotWith( QgsMapLayer *active )
{
  SelectionContextSnapshot s;
  s.activeLayer = active;
  if ( active )
    s.selectedLayers = { active };
  if ( qobject_cast<QgsRasterLayer *>( active ) )
    s.hasRaster = true;
  if ( qobject_cast<QgsVectorLayer *>( active ) )
    s.hasVector = true;
  return s;
}

} // namespace

TEST_CASE( "ContextRules: raster/vector/SAR selection drive their tool groups",
           "[selection_context][contract]" )
{
  ensureApp();

  QgsRasterLayer raster( QStringLiteral( "/tmp/dem.tif" ), QStringLiteral( "dem" ) );
  QgsVectorLayer roads( QStringLiteral( "Point?crs=EPSG:4326" ), QStringLiteral( "roads" ),
                        QStringLiteral( "memory" ) );
  QgsRasterLayer sar( QStringLiteral( "/data/s1a_IW_grd.tif" ), QStringLiteral( "S1A_IW" ) );

  const auto rasterSnap = snapshotWith( &raster );
  const auto vectorSnap = snapshotWith( &roads );
  const auto sarSnap = snapshotWith( &sar );
  const auto none = snapshotWith( nullptr );

  REQUIRE( ContextRules::rasterSelected( rasterSnap ) );
  REQUIRE_FALSE( ContextRules::rasterSelected( vectorSnap ) );
  REQUIRE( ContextRules::vectorSelected( vectorSnap ) );
  REQUIRE_FALSE( ContextRules::vectorSelected( rasterSnap ) );
  REQUIRE( ContextRules::layerSelected( rasterSnap ) );
  REQUIRE_FALSE( ContextRules::layerSelected( none ) );

  // SAR heuristic: product tokens in name/source.
  REQUIRE( sarSnap.hasRaster );
  REQUIRE( ContextRules::sarSelected( [&] {
    SelectionContextSnapshot s = sarSnap;
    s.hasSar = true;
    return s;
  }() ) );
  REQUIRE_FALSE( ContextRules::sarSelected( rasterSnap ) );
}

TEST_CASE( "ContextRules: editing availability vs active edit session", "[selection_context]" )
{
  ensureApp();
  QgsVectorLayer layer( QStringLiteral( "Point?crs=EPSG:4326" ), QStringLiteral( "pts" ),
                        QStringLiteral( "memory" ) );
  const auto snap = snapshotWith( &layer );

  REQUIRE( ContextRules::editingAvailable( snap ) ); // editable-capable layer
  REQUIRE_FALSE( ContextRules::editingActive( snap ) ); // no edit session yet

  layer.startEditing();
  SelectionContextSnapshot editing = snap;
  editing.activeEditable = true;
  REQUIRE( ContextRules::editingActive( editing ) );
  layer.rollBack();
}

TEST_CASE( "ContextRules: governance selection rules", "[selection_context]" )
{
  ensureApp();
  SelectionContextSnapshot s;
  REQUIRE_FALSE( ContextRules::resultSelected( s ) );
  REQUIRE_FALSE( ContextRules::assetSelected( s ) );
  s.selectedResultIds = QStringList{ "res-1" };
  REQUIRE( ContextRules::resultSelected( s ) );
  s.selectedAssetIds = QStringList{ "asset-1" };
  REQUIRE( ContextRules::assetSelected( s ) );
}

TEST_CASE( "SelectionContext: notify coalesces into one debounced broadcast",
           "[selection_context][behavior]" )
{
  ensureApp();
  sicnu::app::SelectionContext ctx;
  QSignalSpy spy( &ctx, &sicnu::app::SelectionContext::changed );

  ctx.notifyAssetSelection( QStringList{ "a", "b" } );
  ctx.notifyGovernanceSelection( QStringList{ "r1" } );
  // Debounce timer still running → no synchronous emissions yet.
  REQUIRE( spy.isEmpty() );
  REQUIRE( ctx.snapshot().selectedAssetIds == QStringList{ "a", "b" } );

  QTest::qWait( 400 );
  REQUIRE( spy.count() == 1 );
  const SelectionContextSnapshot snap =
      spy.first().first().value<SelectionContextSnapshot>();
  REQUIRE( snap.selectedAssetIds.size() == 2 );
  REQUIRE( snap.selectedResultIds == QStringList{ "r1" } );
}

TEST_CASE( "SelectionContext: identical notifications do not reschedule",
           "[selection_context][behavior]" )
{
  ensureApp();
  sicnu::app::SelectionContext ctx;
  QSignalSpy spy( &ctx, &sicnu::app::SelectionContext::changed );

  ctx.notifyAssetSelection( QStringList{ "a" } );
  ctx.notifyAssetSelection( QStringList{ "a" } ); // duplicate — no-op
  QTest::qWait( 400 );
  REQUIRE( spy.count() == 1 );
  ctx.notifyAssetSelection( QStringList{ "a" } );
  // No further emission scheduled for identical value: wait past the window.
  QTest::qWait( 400 );
  REQUIRE( spy.count() == 1 );
}

TEST_CASE( "SelectionContext: workbench id flows into the snapshot",
           "[selection_context][behavior]" )
{
  ensureApp();
  sicnu::app::WorkbenchHost host;
  class Bench : public sicnu::app::IWorkbench
  {
    public:
      QString id() const override { return QStringLiteral( "classify" ); }
      QString title() const override { return id(); }
      QIcon icon() const override { return {}; }
      QWidget *primaryWidget() override { return nullptr; }
      void activate() override { m_active = true; }
      bool isActive() const override { return m_active; }
      sicnu::app::WorkbenchFeatures features() const override
      {
        return sicnu::app::WorkbenchFeature::ExternalWindow;
      }
      bool m_active = false;
  } bench;
  REQUIRE( host.registerWorkbench( &bench ) );
  REQUIRE( host.activate( "classify" ) );

  sicnu::app::SelectionContext ctx;
  ctx.attachWorkbenchHost( &host );
  REQUIRE( ctx.snapshot().workbenchId == "classify" );
}

TEST_CASE( "SelectionContext: layer removal immediately evicts layer from cached snapshot (#778)",
           "[selection_context][behavior]" )
{
  ensureApp();
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsVectorLayer *layer = new QgsVectorLayer( QStringLiteral( "Point?crs=EPSG:4326" ),
                                              QStringLiteral( "test_layer" ), QStringLiteral( "memory" ) );
  project->addMapLayer( layer );

  sicnu::app::SelectionContext ctx;
  QgsMapCanvas canvas;
  ctx.attachCanvas( &canvas );
  canvas.setCurrentLayer( layer );

  const auto snap1 = ctx.snapshot();
  REQUIRE( snap1.activeLayer == layer );

  // Now remove layer from project. layersWillBeRemoved signal fires!
  project->removeMapLayer( layer->id() );

  // Snapshot must NOT return the deleted layer, even without waiting 150ms!
  const auto snap2 = ctx.snapshot();
  REQUIRE( snap2.activeLayer == nullptr );
  project->clear();
}

