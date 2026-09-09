// Workbench 5.0 — SelectionContext rules + aggregation (Milestone B)
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/selection_context.h"
#include "app/workbench/workbench_host.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QItemSelectionModel>
#include <QTest>
#include <qgsapplication.h>
#include <qgsmaplayer.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgslayertree.h>
#include <qgslayertreemodel.h>
#include <qgslayertreeview.h>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_selection_context";
char *fake_argv[] = { fake_argv0, nullptr };

QCoreApplication *ensureApp()
{
  static QgsApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
  {
    // QgsApplication (QApplication subclass, GUI off): the lifecycle cases
    // below need real providers (memory layers) and widget-based tree views.
    app = new QgsApplication( fake_argc, fake_argv, false );
    QgsApplication::initQgis();
  }
  return QCoreApplication::instance();
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

// ── Workbench 6.0 Milestone A: selection lifetime hazards (#778) ───────────

TEST_CASE( "SelectionContext: layer removal purges the projection before the object dies",
           "[selection_context][lifecycle][ux6]" )
{
  ensureApp();
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  // Declaration order matters: the model must outlive the view (reverse
  // destruction), or ~QgsLayerTreeView touches the half-destructed model.
  QgsLayerTreeModel model( project->layerTreeRoot() );
  QgsLayerTreeView tree;
  tree.setLayerTreeModel( &model );
  // The shell's initLayerTree() sets BOTH the layer-tree model and the
  // QTreeView model — without the latter selectionModel() is null.
  tree.setModel( &model );

  sicnu::app::SelectionContext ctx;
  ctx.attachCanvas( &canvas );
  ctx.attachLayerTree( &tree );

  QgsVectorLayer *victim = new QgsVectorLayer( QStringLiteral( "Point?crs=EPSG:4326" ),
                                               QStringLiteral( "victim" ),
                                               QStringLiteral( "memory" ) );
  REQUIRE( victim->isValid() );
  project->addMapLayer( victim, false );
  project->layerTreeRoot()->addLayer( victim );
  canvas.setCurrentLayer( victim );
  if ( QgsLayerTreeLayer *node = project->layerTreeRoot()->findLayer( victim->id() ) )
  {
    const QModelIndex idx = model.node2index( node );
    tree.selectionModel()->select( idx, QItemSelectionModel::Select | QItemSelectionModel::Rows );
  }
  { FILE* t = fopen( "C:/Users/wangj.KEVIN/projects/ux6_trace.log", "a" ); if ( t ) { fputs( "C\n", t ); fclose( t ); } }
  ctx.refreshNow();
  { FILE* t = fopen( "C:/Users/wangj.KEVIN/projects/ux6_trace.log", "a" ); if ( t ) { fputs( "D\n", t ); fclose( t ); } }
  REQUIRE( ctx.snapshot().activeLayer == victim );
  REQUIRE( ctx.snapshot().selectedLayers.contains( victim ) );

  // QgsMapLayerStore fires layerWillBeRemoved BEFORE destroying the object —
  // the context must purge the doomed pointer from every projection and
  // re-broadcast so consumers never hold it across the deletion (#778).
  { FILE* t = fopen( "C:/Users/wangj.KEVIN/projects/ux6_trace.log", "a" ); if ( t ) { fputs( "A\n", t ); fclose( t ); } }
  project->removeMapLayer( victim->id() );
  { FILE* t = fopen( "C:/Users/wangj.KEVIN/projects/ux6_trace.log", "a" ); if ( t ) { fputs( "B\n", t ); fclose( t ); } }

  const auto after = ctx.snapshot();
  REQUIRE( after.activeLayer == nullptr );
  REQUIRE( after.selectedLayers.isEmpty() );
  // The purged selection must also be observable through the rules.
  REQUIRE_FALSE( sicnu::app::ContextRules::layerSelected( after ) );

  project->clear();
}

TEST_CASE( "SelectionContext: canvas destruction leaves the context safe to query",
           "[selection_context][lifecycle][ux6]" )
{
  ensureApp();
  QgsProject *project = QgsProject::instance();
  project->clear();

  auto *canvas = new QgsMapCanvas();
  sicnu::app::SelectionContext ctx;
  ctx.attachCanvas( canvas );
  ctx.refreshNow();
  REQUIRE( ctx.snapshot().activeLayer == nullptr );

  // The context tracks its sources through QPointer guards — destroying a
  // source must not leave a dangling raw pointer behind (#778).
  delete canvas;
  const auto snap = ctx.snapshot();
  REQUIRE( snap.activeLayer == nullptr );
  REQUIRE( snap.layerCount == 0 );

  project->clear();
}

// ── Workbench 6.0 Milestone E: prerequisite facts + deterministic reasons ──

TEST_CASE( "ContextRules: prerequisite facts project the snapshot deterministically",
           "[selection_context][ux6]" )
{
  ensureApp();
  QgsRasterLayer raster( QStringLiteral( "/tmp/dem.tif" ), QStringLiteral( "dem" ) );
  const auto snap = snapshotWith( &raster );

  const auto facts = ContextRules::prerequisiteFacts( snap );
  CHECK( facts.hasLayerSelection );
  CHECK( facts.hasRaster );
  CHECK_FALSE( facts.hasVector );
  CHECK_FALSE( facts.hasSar );
  CHECK_FALSE( facts.editing );
  CHECK_FALSE( facts.hasGovernanceAsset );

  const auto none = ContextRules::prerequisiteFacts( snapshotWith( nullptr ) );
  CHECK_FALSE( none.hasLayerSelection );
  CHECK( none.workbenchId.isEmpty() );
}

TEST_CASE( "ContextRules: rs.* commands carry a deterministic raster reason",
           "[selection_context][ux6]" )
{
  ensureApp();
  // Milestone E: every prefix family with an availability predicate has a
  // reason case — rs.* used to fall through to an empty explanation.
  const auto none = snapshotWith( nullptr );
  REQUIRE( ContextRules::unavailabilityReason( none, QStringLiteral( "rs.bandMath" ) )
               == QObject::tr( "需要选中栅格图层" ) );
  REQUIRE( ContextRules::unavailabilityReason( none, QStringLiteral( "rs.pca" ) )
               == QObject::tr( "需要选中栅格图层" ) );

  QgsRasterLayer raster( QStringLiteral( "/tmp/dem.tif" ), QStringLiteral( "dem" ) );
  const auto rasterSnap = snapshotWith( &raster );
  REQUIRE( ContextRules::unavailabilityReason( rasterSnap, QStringLiteral( "rs.bandMath" ) ).isEmpty() );

  // Unrelated families keep their reasons.
  REQUIRE( ContextRules::unavailabilityReason( none, QStringLiteral( "layer.properties" ) )
               == QObject::tr( "需要选中图层" ) );
  REQUIRE( ContextRules::unavailabilityReason( none, QStringLiteral( "sar.calibrate" ) )
               == QObject::tr( "需要选中 SAR 数据" ) );
}


// ── Review L #1: edit commands carry reasons for every disabled state ──────

TEST_CASE( "ContextRules: layer edit commands explain every disabled state",
           "[selection_context][ux6][review-l]" )
{
  ensureApp();
  const auto none = snapshotWith( nullptr );

  REQUIRE( ContextRules::unavailabilityReason( none, QStringLiteral( "layer.toggleEditing" ) )
               == QObject::tr( "需要选中矢量图层" ) );
  REQUIRE( ContextRules::unavailabilityReason( none, QStringLiteral( "layer.saveEdits" ) )
               == QObject::tr( "需要选中矢量图层" ) );
  REQUIRE( ContextRules::unavailabilityReason( none, QStringLiteral( "layer.attributeTable" ) )
               == QObject::tr( "需要选中矢量图层" ) );

  QgsRasterLayer raster( QStringLiteral( "/tmp/dem.tif" ), QStringLiteral( "dem" ) );
  const auto rasterSnap = snapshotWith( &raster );
  REQUIRE( ContextRules::unavailabilityReason( rasterSnap, QStringLiteral( "layer.toggleEditing" ) )
               == QObject::tr( "需要选中矢量图层" ) );
  REQUIRE( ContextRules::unavailabilityReason( rasterSnap, QStringLiteral( "layer.attributeTable" ) )
               == QObject::tr( "需要选中矢量图层" ) );

  QgsVectorLayer readOnly( QStringLiteral( "Point?crs=EPSG:4326" ),
                           QStringLiteral( "ro" ), QStringLiteral( "memory" ) );
  readOnly.setReadOnly( true );
  const auto roSnap = snapshotWith( &readOnly );
  REQUIRE( ContextRules::unavailabilityReason( roSnap, QStringLiteral( "layer.toggleEditing" ) )
               == QObject::tr( "当前图层不可编辑" ) );

  QgsVectorLayer editable( QStringLiteral( "Point?crs=EPSG:4326" ),
                           QStringLiteral( "rw" ), QStringLiteral( "memory" ) );
  const auto rwSnap = snapshotWith( &editable );
  REQUIRE( ContextRules::unavailabilityReason( rwSnap, QStringLiteral( "layer.saveEdits" ) )
               == QObject::tr( "请先开启编辑会话" ) );
}

// ── Workbench 6.0 Milestone A: selection lifetime hazards (#778) ───────────
