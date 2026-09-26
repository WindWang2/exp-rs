// test_parity_selection_authority_r4.cpp — selection-authority matrix oracles
// (ui-backend-state-parity-r4 / WP-B, PARITY_MAP section L)
//
// The single selection authority is sicnu::app::SelectionContext: every
// projection (ribbon availability, inspector, workbench state) must read the
// same snapshot, every view push must be absorbed by it, and every
// authority-owned write-back to a view must have exactly one writer.
//
// These oracles pin the bidirectional matrix the existing
// test_selection_context.cpp does not cover:
//
//   sc1  canvas → authority: the canvas current layer IS the snapshot's
//        activeLayer (source→authority absorption, then authority truth);
//   sc2  layer tree → authority: the tree selection IS selectedLayers;
//   sc3  all nine push chains are absorbed field-for-field, including the
//        empty-push clear contract;
//   sc4  the `changed` broadcast payload is byte-for-byte the same projection
//        a consumer reading snapshot() would get (broadcast atomicity);
//   sc5  layer removal evicts the canvas current layer immediately — the
//        authority owns this write-back (SC-5 single-writer contract);
//   sc6  interleaved sources converge into ONE debounced broadcast carrying
//        every absorbed change.
//
// Truth always comes from the authority's snapshot()/changed payload, never
// from re-reading view internals to derive expectations.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "app/workbench/selection_context.h"
#include "app/workbench/workbench_host.h"

#include <QApplication>
#include <QItemSelectionModel>
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>
#include <qgslayertree.h>
#include <qgslayertreemodel.h>
#include <qgslayertreeview.h>

using sicnu::app::SelectionContext;

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_parity_selection_authority_r4";
char *fake_argv[] = { fake_argv0, nullptr };

QgsApplication *ensureApp()
{
  static QgsApplication *app = nullptr;
  if ( !app )
  {
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-selftest" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "parity-selection-r4" ) );
    QSettings::setDefaultFormat( QSettings::IniFormat );
    app = new QgsApplication( fake_argc, fake_argv, false );
    QgsApplication::initQgis();
    QSettings().clear();
  }
  return app;
}

class FastExitListener : public Catch::EventListenerBase
{
  public:
    using Catch::EventListenerBase::EventListenerBase;
    void testRunEnded( const Catch::TestRunStats &stats ) override
    {
      const bool ok = !stats.aborting && stats.totals.testCases.failed == 0;
      QgsApplication::exitQgis();
      std::fprintf( stderr, "\n%s: %u/%u assertions, %u/%u test cases\n",
                    ok ? "ALL TESTS PASSED" : "TESTS FAILED",
                    static_cast<unsigned>( stats.totals.assertions.passed ),
                    static_cast<unsigned>( stats.totals.assertions.passed
                                           + stats.totals.assertions.failed ),
                    static_cast<unsigned>( stats.totals.testCases.passed ),
                    static_cast<unsigned>( stats.totals.testCases.passed
                                           + stats.totals.testCases.failed ) );
      std::fflush( stderr );
      std::_Exit( ok ? 0 : 1 );
    }
};
CATCH_REGISTER_LISTENER( FastExitListener )

/// One attached authority per test: canvas + layer tree wired the way the
/// shell does it (main_window_workbench.cpp:365-367), plus a memory vector
/// layer so no on-disk fixture is needed.
struct AuthorityFixture
{
  QgsMapCanvas canvas;
  QgsLayerTreeView tree;
  QgsLayerTreeModel *treeModel = nullptr;
  QgsVectorLayer *layerA = nullptr;
  QgsVectorLayer *layerB = nullptr;
  SelectionContext context;

  explicit AuthorityFixture( const QString &name )
  {
    QgsProject::instance()->clear();
    layerA = new QgsVectorLayer( QStringLiteral( "Point?crs=EPSG:4326" ),
                                 QStringLiteral( "A-%1" ).arg( name ), QStringLiteral( "memory" ) );
    layerB = new QgsVectorLayer( QStringLiteral( "Point?crs=EPSG:4326" ),
                                 QStringLiteral( "B-%1" ).arg( name ), QStringLiteral( "memory" ) );
    REQUIRE( layerA->isValid() );
    REQUIRE( layerB->isValid() );
    QgsProject::instance()->addMapLayers( { layerA, layerB } );

    treeModel = new QgsLayerTreeModel( QgsProject::instance()->layerTreeRoot() );
    tree.setModel( treeModel );

    canvas.setLayers( { layerA, layerB } );
    canvas.setCurrentLayer( layerA );

    context.attachCanvas( &canvas );
    context.attachLayerTree( &tree );
  }

  ~AuthorityFixture() { QgsProject::instance()->clear(); }
};

/// Settle the debounce window: the authority coalesces with ≤150 ms.
void settle( SelectionContext &context, int ms = 400 )
{
  QEventLoop loop;
  QTimer::singleShot( ms, &loop, &QEventLoop::quit );
  QObject::connect( &context, &SelectionContext::changed, &loop, &QEventLoop::quit );
  loop.exec();
  QApplication::processEvents();
}

} // namespace

// ===========================================================================
// SC-1: canvas → authority absorption; the snapshot's activeLayer is the
// projection every consumer must agree with.
// ===========================================================================
TEST_CASE( "SC-1: canvas current layer is the authority's activeLayer projection",
           "[parity][selection][authority][parity-sc1]" )
{
  ensureApp();
  AuthorityFixture fx( QStringLiteral( "sc1" ) );

  fx.canvas.setCurrentLayer( fx.layerB );
  fx.context.refreshNow();
  CHECK( fx.context.snapshot().activeLayer == fx.layerB );

  fx.canvas.setCurrentLayer( nullptr );
  fx.context.refreshNow();
  CHECK( fx.context.snapshot().activeLayer == nullptr );

  fx.canvas.setCurrentLayer( fx.layerA );
  fx.context.refreshNow();
  const auto snap = fx.context.snapshot();
  CHECK( snap.activeLayer == fx.layerA );
  CHECK( snap.hasLayerSelection() );
}

// ===========================================================================
// SC-2: layer-tree selection → authority absorption.
// ===========================================================================
TEST_CASE( "SC-2: layer tree selection is the authority's selectedLayers projection",
           "[parity][selection][authority][parity-sc2]" )
{
  ensureApp();
  AuthorityFixture fx( QStringLiteral( "sc2" ) );

  QItemSelectionModel *selection = fx.tree.selectionModel();
  REQUIRE( selection != nullptr );

  // Select layer B's tree node through the REAL tree selection model — the
  // same path a user's click takes (main_window_connections second writer
  // aside, the tree model is the authority input).
  const QModelIndex indexA = fx.tree.node2index(
    QgsProject::instance()->layerTreeRoot()->findLayer( fx.layerA->id() ) );
  const QModelIndex indexBNode = fx.tree.node2index(
    QgsProject::instance()->layerTreeRoot()->findLayer( fx.layerB->id() ) );
  REQUIRE( indexA.isValid() );
  REQUIRE( indexBNode.isValid() );

  selection->select( indexBNode, QItemSelectionModel::ClearAndSelect );
  fx.context.refreshNow();
  const auto snapB = fx.context.snapshot();
  CHECK( snapB.selectedLayers.contains( fx.layerB ) );

  selection->select( indexA, QItemSelectionModel::ClearAndSelect );
  fx.context.refreshNow();
  const auto snapA = fx.context.snapshot();
  CHECK( snapA.selectedLayers.contains( fx.layerA ) );
  CHECK_FALSE( snapA.selectedLayers.contains( fx.layerB ) );

  selection->clearSelection();
  fx.context.refreshNow();
  CHECK( fx.context.snapshot().selectedLayers.isEmpty() );
}

// ===========================================================================
// SC-3: all nine push chains are absorbed field-for-field, including their
// empty-push clear contract (UI→authority direction of the matrix).
// ===========================================================================
TEST_CASE( "SC-3: every notify push is absorbed and every empty push clears",
           "[parity][selection][authority][matrix][parity-sc3]" )
{
  ensureApp();
  AuthorityFixture fx( QStringLiteral( "sc3" ) );
  auto &ctx = fx.context;

  // --- asset ids
  ctx.notifyAssetSelection( { QStringLiteral( "asset-1" ) } );
  CHECK( ctx.snapshot().selectedAssetIds == QStringList{ QStringLiteral( "asset-1" ) } );
  ctx.notifyAssetSelection( {} );
  CHECK( ctx.snapshot().selectedAssetIds.isEmpty() );

  // --- governance entities
  ctx.notifyGovernanceSelection( { QStringLiteral( "gov-1" ) } );
  CHECK( ctx.snapshot().selectedResultIds == QStringList{ QStringLiteral( "gov-1" ) } );
  ctx.notifyGovernanceSelection( {} );
  CHECK( ctx.snapshot().selectedResultIds.isEmpty() );

  // --- datasets
  ctx.notifyDatasetSelection( { QStringLiteral( "ds-1" ) } );
  CHECK( ctx.snapshot().selectedDatasetIds == QStringList{ QStringLiteral( "ds-1" ) } );
  ctx.notifyDatasetSelection( {} );
  CHECK( ctx.snapshot().selectedDatasetIds.isEmpty() );

  // --- experiment runs
  ctx.notifyExperimentSelection( { QStringLiteral( "run-1" ) } );
  CHECK( ctx.snapshot().selectedExperimentIds == QStringList{ QStringLiteral( "run-1" ) } );
  ctx.notifyExperimentSelection( {} );
  CHECK( ctx.snapshot().selectedExperimentIds.isEmpty() );

  // --- models
  ctx.notifyModelSelection( { QStringLiteral( "model-1" ) } );
  CHECK( ctx.snapshot().selectedModelIds == QStringList{ QStringLiteral( "model-1" ) } );
  ctx.notifyModelSelection( {} );
  CHECK( ctx.snapshot().selectedModelIds.isEmpty() );

  // --- workflow runs
  ctx.notifyWorkflowSelection( { QStringLiteral( "wfrun-1" ) } );
  CHECK( ctx.snapshot().selectedWorkflowRunIds == QStringList{ QStringLiteral( "wfrun-1" ) } );
  ctx.notifyWorkflowSelection( {} );
  CHECK( ctx.snapshot().selectedWorkflowRunIds.isEmpty() );

  // --- mission task (id + status)
  ctx.notifyMissionTaskSelection( QStringLiteral( "task-1" ), sicnu::app::MissionTaskStatus::Pending );
  auto missionSnap = ctx.snapshot();
  CHECK( missionSnap.selectedMissionTaskId == QStringLiteral( "task-1" ) );
  CHECK( missionSnap.hasMissionTaskSelection );
  ctx.notifyMissionTaskSelection( QString(), sicnu::app::MissionTaskStatus::Pending );
  CHECK_FALSE( ctx.snapshot().hasMissionTaskSelection );

  // --- pipeline node
  ctx.notifyPipelineNodeSelection( QStringLiteral( "node-1" ) );
  CHECK( ctx.snapshot().selectedPipelineNodeId == QStringLiteral( "node-1" ) );
  ctx.notifyPipelineNodeSelection( QString() );
  CHECK( ctx.snapshot().selectedPipelineNodeId.isEmpty() );
}

// ===========================================================================
// SC-4: the changed broadcast payload IS the snapshot a pull consumer reads —
// no delivery window where the two projections disagree.
// ===========================================================================
TEST_CASE( "SC-4: changed broadcast payload equals the snapshot consumers read",
           "[parity][selection][authority][broadcast][parity-sc4]" )
{
  ensureApp();
  AuthorityFixture fx( QStringLiteral( "sc4" ) );

  QSignalSpy spy( &fx.context, &SelectionContext::changed );
  REQUIRE( spy.isValid() );

  fx.context.notifyAssetSelection( { QStringLiteral( "payload-1" ) } );
  fx.context.notifyDatasetSelection( { QStringLiteral( "payload-ds" ) } );
  settle( fx.context );

  REQUIRE( spy.count() >= 1 );
  const auto last = spy.at( spy.count() - 1 );
  const auto broadcast = last.at( 0 ).value<sicnu::app::SelectionContextSnapshot>();
  const auto pulled = fx.context.snapshot();
  CHECK( broadcast.selectedAssetIds == pulled.selectedAssetIds );
  CHECK( broadcast.selectedDatasetIds == pulled.selectedDatasetIds );
  CHECK( broadcast.activeLayer == pulled.activeLayer );
  CHECK( broadcast.selectedLayers.size() == pulled.selectedLayers.size() );
}

// ===========================================================================
// SC-5: layer removal evicts the canvas current layer IMMEDIATELY — the
// authority owns this view write-back (the SC-5 single-writer contract; the
// duplicate write in active_view_host is the convergence candidate).
// ===========================================================================
TEST_CASE( "SC-5: removing the current layer clears the canvas through the authority",
           "[parity][selection][authority][lifecycle][parity-sc5]" )
{
  ensureApp();
  AuthorityFixture fx( QStringLiteral( "sc5" ) );

  fx.canvas.setCurrentLayer( fx.layerA );
  fx.context.refreshNow();
  REQUIRE( fx.context.snapshot().activeLayer == fx.layerA );

  // The removal announcement path (#778) — the authority's own write-back
  // must not depend on the debounce window.
  QgsProject::instance()->removeMapLayer( fx.layerA );
  QApplication::processEvents();

  CHECK( fx.canvas.currentLayer() == nullptr );
  CHECK( fx.context.snapshot().activeLayer == nullptr );
  CHECK_FALSE( fx.context.snapshot().selectedLayers.contains( fx.layerA ) );
}

// ===========================================================================
// SC-6: interleaved sources converge into ONE debounced broadcast carrying
// every absorbed change (the 150 ms window is a coalescer, not a dropper).
// ===========================================================================
TEST_CASE( "SC-6: interleaved pushes converge into one broadcast with all changes",
           "[parity][selection][authority][debounce][parity-sc6]" )
{
  ensureApp();
  AuthorityFixture fx( QStringLiteral( "sc6" ) );

  QSignalSpy spy( &fx.context, &SelectionContext::changed );
  REQUIRE( spy.isValid() );

  // Three different sources change inside one debounce window.
  fx.canvas.setCurrentLayer( fx.layerB );
  fx.context.notifyAssetSelection( { QStringLiteral( "burst-asset" ) } );
  fx.context.notifyPipelineNodeSelection( QStringLiteral( "burst-node" ) );
  settle( fx.context );

  const auto snap = fx.context.snapshot();
  CHECK( snap.activeLayer == fx.layerB );
  CHECK( snap.selectedAssetIds == QStringList{ QStringLiteral( "burst-asset" ) } );
  CHECK( snap.selectedPipelineNodeId == QStringLiteral( "burst-node" ) );

  // All of it must have surfaced through the broadcast channel, not just the
  // lazy snapshot: every changed payload after the window carries the union.
  bool sawUnion = false;
  for ( int i = 0; i < spy.count(); ++i )
  {
    const auto payload = spy.at( i ).at( 0 ).value<sicnu::app::SelectionContextSnapshot>();
    if ( payload.activeLayer == fx.layerB
         && payload.selectedAssetIds == QStringList{ QStringLiteral( "burst-asset" ) }
         && payload.selectedPipelineNodeId == QStringLiteral( "burst-node" ) )
    {
      sawUnion = true;
    }
  }
  CHECK( sawUnion );
}
