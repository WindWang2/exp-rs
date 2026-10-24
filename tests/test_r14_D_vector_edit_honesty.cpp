// test_r14_D_vector_edit_honesty.cpp — R14-D WP-A: vector editing honesty.
//
// The suite drives the REAL QgisDesktopWindow offscreen (the harness of
// test_workbench_full_shell_lifecycle.cpp: hermetic QSettings, exposed
// dialog-free entry points, modal QMessageBoxes answered by a scheduled
// dismisser whose nested exec() runs the queued callbacks). A QgsApplication
// (not a bare QApplication) is created and initQgis()'d so memory vector
// providers exist.
//
// Deterministic failure injection — no chmod/permissions games:
//   * commit failures: QgsVectorLayer::setAllowCommit(false), the same
//     genuine-commit-failure idiom as test_edit_persistence.cpp:245;
//   * delete failures: the edit buffer refuses to delete a fid that is
//     already in its deleted set (seeded via a raw deleteFeatures call),
//     while the public select() keeps it in the selection;
//   * addFeature failures: the edit buffer rejects features whose attribute
//     count differs from the layer fields ("wrong field count") — field
//     constraints are NOT enforced at addFeature time, so a schema mismatch
//     is the honest paste-failure mechanism.
//
// Contracts (survey .planning/r14-D/SURVEY_APP.md):
//   APP-1  toggleEditing: failed commit surfaced, never "Stopped editing"
//   APP-2  facade saveEdits: commit errors reach the message bar
//   APP-5  editing actions follow currentLayerChanged (removal clears them)
//   APP-6  deleteSelected: one undo step; failed delete rolled back+reported
//   APP-7  cutFeatures: failed delete half reported, never "Cut N"
//   APP-8  pasteFeatures: "Pasted X of Y", no undo step when zero land
//   APP-9A saveEdits: failed commit never falls through to "Edits saved"
//   APP-9B RsJobPanel: Load requires loadable outputs; loads counted honestly
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "support/qt_lifecycle.h"

#include "vector_test_fixtures.h"

#include "main_window.h"
#include "app/project_context.h"
#include "app/workbench/selection_context.h"
#include "app/workbench/command_registry.h"
#include "app/workbench/workbench_state.h"
#include "qgis_app_facade.h"
#include "qgsclipboard.h"
#include "shell/rs_job_panel.h"

#include "jobs/job_engine.h"
#include "processing/framework/task_center.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>

#include <json/json.h>

#include <qgsapplication.h>
#include <qgsfeature.h>
#include <qgsgeometry.h>
#include <qgsmapcanvas.h>
#include <qgsmessagebar.h>
#include <qgsmessagebaritem.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>
#include <qgslayertreeview.h>
#include <qgslayertree.h>
#include <qgsrasterlayer.h>

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_r14_D_vector_edit_honesty";
char *fake_argv[] = { fake_argv0, nullptr };

// Widgets (the shell window) AND QGIS providers (memory vector layers) are
// needed: a QgsApplication provides both.
QgsApplication *ensureApp()
{
  if ( !QCoreApplication::instance() )
  {
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-selftest" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "r14-D-vector-edit-honesty" ) );
    QSettings::setDefaultFormat( QSettings::IniFormat );
    QgsApplication *app = new QgsApplication( fake_argc, fake_argv, true );
    QSettings().clear();
    QgsApplication::initQgis();
    return app;
  }
  return qobject_cast<QgsApplication *>( QCoreApplication::instance() );
}

/// Record and dismiss up to @p budget modal QMessageBoxes as they appear,
/// clicking @p button when the box offers it (fallback: close). Mirrors the
/// armModalAnswer dismisser of test_workbench_full_shell_lifecycle.cpp, also
/// capturing each box text so honesty assertions can check WHAT was surfaced.
void armModalRecorder( QStringList *seen, QMessageBox::StandardButton button,
                       int budget = 6 )
{
  if ( budget <= 0 )
    return;
  QTimer::singleShot( 0, ensureApp(), [seen, button, budget] {
    if ( QWidget *modal = QApplication::activeModalWidget() )
    {
      if ( QMessageBox *box = qobject_cast<QMessageBox *>( modal ) )
      {
        if ( seen )
          *seen << box->text();
        if ( button != QMessageBox::NoButton )
        {
          if ( QAbstractButton *target = box->button( button ) )
            target->click();
          else
            box->close();
        }
        else
        {
          box->close();
        }
      }
      else
      {
        modal->close();
      }
    }
    armModalRecorder( seen, button, budget - 1 );
  } );
}

bool anyModalContains( const QStringList &seen, const QString &needle )
{
  for ( const QString &text : seen )
    if ( text.contains( needle, Qt::CaseInsensitive ) )
      return true;
  return false;
}

struct ShellFixture
{
  QgisDesktopWindow window;

  ShellFixture()
  {
    window.show();
    QTest::qWaitForWindowExposed( &window );
  }
};

/// Drop any layers a failed earlier case leaked into the project singleton,
/// through the same removal machinery the shell itself uses.
void purgeProjectLayers()
{
  const auto layers = QgsProject::instance()->mapLayers();
  for ( auto it = layers.cbegin(); it != layers.cend(); ++it )
    QgsProject::instance()->removeMapLayer( it.key() );
}

QgsVectorLayer *makeMemoryLayer( const QString &fieldSpec, const QString &name,
                                 int featureCount )
{
  const QString uri = fieldSpec.isEmpty()
    ? QStringLiteral( "Point?crs=EPSG:4326" )
    : QStringLiteral( "Point?crs=EPSG:4326&" ) + fieldSpec;
  auto *layer = new QgsVectorLayer( uri, name, QStringLiteral( "memory" ) );
  if ( !layer->isValid() )
    return layer;
  // addFeature() needs a live edit buffer; seed through the same
  // start/commit cycle as test_edit_persistence.cpp's makeSampleLayer.
  layer->startEditing();
  const QgsFields fields = layer->fields();
  for ( int i = 0; i < featureCount; ++i )
  {
    QgsFeature f( fields );
    f.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( i, i ) ) );
    for ( const QgsField &field : fields )
      f.setAttribute( field.name(), i );
    layer->addFeature( f );
  }
  layer->commitChanges();
  layer->updateExtents();
  return layer;
}

/// Register the layer with the live shell: project membership + canvas
/// current layer (currentVectorLayer()/activeLayer() both resolve through
/// the canvas).
void adoptInShell( QgisDesktopWindow &window, QgsVectorLayer *layer )
{
  QgsProject::instance()->addMapLayer( layer );
  window.mapCanvas()->setCurrentLayer( layer );
  QTest::qWait( 20 );
}

qlonglong countFeatures( QgsVectorLayer *layer )
{
  qlonglong n = 0;
  QgsFeatureIterator it = layer->getFeatures();
  QgsFeature f;
  while ( it.nextFeature( f ) )
    ++n;
  return n;
}

QList<QgsFeatureId> featureIdsOf( QgsVectorLayer *layer )
{
  QList<QgsFeatureId> ids;
  QgsFeatureIterator it = layer->getFeatures();
  QgsFeature f;
  while ( it.nextFeature( f ) )
    ids.append( f.id() );
  return ids;
}

} // namespace

// ── APP-1 ──────────────────────────────────────────────────────────────────

TEST_CASE( "Toggle editing: failed commit is surfaced and never reported as Stopped editing",
           "[app][vector_edit][r14_D][app1]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  QgsVectorLayer *layer =
    makeMemoryLayer( QStringLiteral( "field=kind:int" ), QStringLiteral( "toggle_source" ), 0 );
  REQUIRE( layer->isValid() );
  adoptInShell( fx.window, layer );

  REQUIRE( layer->startEditing() );
  QgsFeature f( layer->fields() );
  f.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 1, 1 ) ) );
  f.setAttribute( QStringLiteral( "kind" ), 7 );
  REQUIRE( layer->addFeature( f ) );
  REQUIRE( layer->isModified() );
  layer->setAllowCommit( false ); // genuine commit rejection

  QStringList modals;
  armModalRecorder( &modals, QMessageBox::Save );
  fx.window.toggleEditing();

  // QGIS behavior: the layer stays in edit mode with the edits buffered.
  CHECK( layer->isEditable() );
  CHECK( layer->isModified() );

  const QString status = fx.window.statusBar()->currentMessage();
  CHECK_FALSE( status.contains( QStringLiteral( "Stopped editing" ) ) );
  CHECK( status.contains( QStringLiteral( "Commit failed" ) ) );
  CHECK( status.contains( QStringLiteral( "toggle_source" ) ) );

  // The commit failure itself was surfaced (Save answered the Stop Editing
  // question; the failure warning followed and was recorded on dismissal).
  CHECK( anyModalContains( modals, QStringLiteral( "Failed to save edits" ) ) );

  layer->setAllowCommit( true );
  layer->rollBack();
  QgsProject::instance()->removeMapLayer( layer->id() );
}

// ── APP-9A ─────────────────────────────────────────────────────────────────

TEST_CASE( "Save Edits: failed commit does not fall through to the Edits saved status",
           "[app][vector_edit][r14_D][app9a]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  QgsVectorLayer *layer =
    makeMemoryLayer( QStringLiteral( "field=kind:int" ), QStringLiteral( "save_source" ), 0 );
  REQUIRE( layer->isValid() );
  adoptInShell( fx.window, layer );

  REQUIRE( layer->startEditing() );
  QgsFeature f( layer->fields() );
  f.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 2, 2 ) ) );
  f.setAttribute( QStringLiteral( "kind" ), 9 );
  REQUIRE( layer->addFeature( f ) );
  layer->setAllowCommit( false );

  QStringList modals;
  armModalRecorder( &modals, QMessageBox::Ok );
  fx.window.saveEdits(); // non-modal internal path; warning box is dismissed

  CHECK( layer->isEditable() );
  CHECK( layer->isModified() );

  const QString status = fx.window.statusBar()->currentMessage();
  CHECK_FALSE( status.contains( QStringLiteral( "Edits saved" ) ) );
  CHECK( status.contains( QStringLiteral( "Failed to save edits" ) ) );
  CHECK( anyModalContains( modals, QStringLiteral( "Failed to save edits" ) ) );

  layer->setAllowCommit( true );
  layer->rollBack();
  QgsProject::instance()->removeMapLayer( layer->id() );
}

// ── APP-2 ──────────────────────────────────────────────────────────────────

TEST_CASE( "Facade saveEdits: commit failure reaches the message bar and returns false",
           "[app][vector_edit][r14_D][app2]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  QgsVectorLayer *layer =
    makeMemoryLayer( QStringLiteral( "field=kind:int" ), QStringLiteral( "facade_source" ), 0 );
  REQUIRE( layer->isValid() );
  adoptInShell( fx.window, layer );

  REQUIRE( layer->startEditing() );
  QgsFeature f( layer->fields() );
  f.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 3, 3 ) ) );
  f.setAttribute( QStringLiteral( "kind" ), 1 );
  REQUIRE( layer->addFeature( f ) );
  layer->setAllowCommit( false );

  // Message-bar spy: the facade must route the failure through the bar the
  // attribute table shows, not return it into silence.
  QgsMessageBar *bar = QgisApp::instance()->messageBar();
  REQUIRE( bar );
  QStringList titles, texts;
  QObject::connect( bar, &QgsMessageBar::widgetAdded,
                     &fx.window, [&titles, &texts]( QgsMessageBarItem *item ) {
                       if ( item )
                       {
                         titles << item->title();
                         texts << item->text();
                       }
                     } );

  const bool ok = QgisApp::instance()->saveEdits( layer, true, true );
  CHECK_FALSE( ok ); // return value semantics preserved
  REQUIRE( texts.size() == 1 );
  CHECK( titles.first() == QStringLiteral( "Save Edits" ) );
  CHECK( texts.first().contains( QStringLiteral( "Failed to save edits" ) ) );
  CHECK( texts.first().contains( QStringLiteral( "facade_source" ) ) );

  QObject::disconnect( bar, &QgsMessageBar::widgetAdded, &fx.window, nullptr );

  layer->setAllowCommit( true );
  layer->rollBack();
  QgsProject::instance()->removeMapLayer( layer->id() );
}

// ── APP-10 ─────────────────────────────────────────────────────────────────

TEST_CASE( "Failed startEditing never reports Stopped editing and leaves no lease (R18-D)",
           "[app][vector_edit][r18d][app10]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  SECTION( "read-only layer: the refusal is surfaced, never claimed as Stopped" )
  {
    QgsVectorLayer *layer =
      makeMemoryLayer( QString(), QStringLiteral( "readonly_layer" ), 2 );
    REQUIRE( layer->isValid() );
    layer->setReadOnly( true );
    adoptInShell( fx.window, layer );

    fx.window.toggleEditing();
    QTest::qWait( 30 );

    CHECK_FALSE( layer->isEditable() );
    const QString status = fx.window.statusBar()->currentMessage();
    CHECK_FALSE( status.contains( QStringLiteral( "Stopped editing" ) ) );
    CHECK( status.contains( QStringLiteral( "Failed to start editing" ) ) );
  }

  SECTION( "asset-backed layer: a refused session must not leak the Edit Lease" )
  {
    REQUIRE( fx.window.projectContext() != nullptr );
    sicnu::data::DataManager &dataManager = fx.window.projectContext()->dataManager();

    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "ogr" );
    source.canonicalSource = vector_test_fixtures::syntheticGeoJsonPath();
    const auto registered = dataManager.registerSource(
      sicnu::data::RegisterRequest{ source } );
    REQUIRE_FALSE( registered.assetId.isNull() );
    const sicnu::data::AssetId assetId = registered.assetId;
    QTest::qWait( 100 );

    QgsVectorLayer *assetLayer = nullptr;
    for ( QgsMapLayer *l : QgsProject::instance()->mapLayers() )
    {
      auto *v = qobject_cast<QgsVectorLayer *>( l );
      if ( v && sicnu::data::AssetId::fromString(
                    v->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() ) &&
           *sicnu::data::AssetId::fromString(
              v->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() ) == assetId )
      {
        assetLayer = v;
        break;
      }
    }
    REQUIRE( assetLayer != nullptr );
    // Refuse the session at the layer level while the lease path stays live:
    // acquireEditLease() consults only the asset id, so the lease IS taken
    // and then startEditing() must fail on the read-only flag.
    assetLayer->setReadOnly( true );
    fx.window.mapCanvas()->setCurrentLayer( assetLayer );
    QTest::qWait( 20 );
    CHECK_FALSE( dataManager.hasActiveEditLease( assetId ) );

    // Snapshot identity: releasing the lease retries deferred relocations,
    // which may relocate (destroy) the layer synchronously.
    const QString assetLayerId = assetLayer->id();
    fx.window.toggleEditing();
    QTest::qWait( 30 );

    if ( QgsMapLayer *resolved = QgsProject::instance()->mapLayer( assetLayerId ) )
      if ( auto *v = qobject_cast<QgsVectorLayer *>( resolved ) )
        CHECK_FALSE( v->isEditable() );
    CHECK_FALSE( dataManager.hasActiveEditLease( assetId ) );
    CHECK_FALSE( fx.window.statusBar()->currentMessage()
                     .contains( QStringLiteral( "Stopped editing" ) ) );

    for ( QgsMapLayer *l : QgsProject::instance()->mapLayers() )
      if ( auto *v = qobject_cast<QgsVectorLayer *>( l ) )
        v->rollBack();
    purgeProjectLayers();
  }
}

// ── APP-5 ──────────────────────────────────────────────────────────────────

TEST_CASE( "Editing actions follow current-layer changes after layer removal",
           "[app][vector_edit][r14_D][app5]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  QgsVectorLayer *layer =
    makeMemoryLayer( QStringLiteral( "field=kind:int" ), QStringLiteral( "removal_target" ), 2 );
  REQUIRE( layer->isValid() );
  adoptInShell( fx.window, layer );

  QAction *capture = fx.window.editingAction( QStringLiteral( "mActionCapturePoint" ) );
  QAction *vertex = fx.window.editingAction( QStringLiteral( "mActionVertexTool" ) );
  REQUIRE( capture );
  REQUIRE( vertex );
  CHECK_FALSE( capture->isEnabled() );

  // Enter edit mode through the window's own toggle (acquires the lease,
  // starts editing, refreshes the editing actions).
  fx.window.toggleEditing();
  CHECK( layer->isEditable() );
  CHECK( capture->isEnabled() );
  CHECK( vertex->isEnabled() );

  // Removing the current editable layer clears the canvas current layer
  // (SelectionContext::handleLayerWillBeRemoved) — currentLayerChanged must
  // re-run the editing-UI refresh or the actions stay enabled as dead clicks.
  QgsProject::instance()->removeMapLayer( layer->id() );
  QTest::qWait( 50 );

  CHECK( fx.window.mapCanvas()->currentLayer() == nullptr );
  CHECK_FALSE( capture->isEnabled() );
  CHECK_FALSE( vertex->isEnabled() );
}

// ── APP-6 ──────────────────────────────────────────────────────────────────

TEST_CASE( "Delete selected: one undo step, and rejected deletes are rolled back and reported",
           "[app][vector_edit][r14_D][app6]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  SECTION( "happy path: one Delete-features undo step restores the batch" )
  {
    QgsVectorLayer *layer =
      makeMemoryLayer( QStringLiteral( "field=kind:int" ), QStringLiteral( "delete_ok" ), 3 );
    REQUIRE( layer->isValid() );
    adoptInShell( fx.window, layer );
    REQUIRE( layer->startEditing() );

    const QList<QgsFeatureId> ids = featureIdsOf( layer );
    REQUIRE( ids.size() == 3 );
    layer->selectByIds( QgsFeatureIds{ ids.at( 0 ), ids.at( 1 ) } );

    const int undoBefore = layer->undoStack()->count();
    fx.window.deleteSelectedFeatures();

    CHECK( fx.window.statusBar()->currentMessage()
             .contains( QStringLiteral( "Deleted 2 feature(s)" ) ) );
    CHECK( countFeatures( layer ) == 1 );
    // The whole batch is ONE macro command — not one undo step per feature.
    CHECK( layer->undoStack()->count() == undoBefore + 1 );

    layer->undoStack()->undo();
    CHECK( countFeatures( layer ) == 3 );

    layer->rollBack();
    QgsProject::instance()->removeMapLayer( layer->id() );
  }

  SECTION( "rejected deletes: rolled back, failure surfaced, no Deleted N" )
  {
    QgsVectorLayer *layer =
      makeMemoryLayer( QStringLiteral( "field=kind:int" ), QStringLiteral( "delete_fail" ), 3 );
    REQUIRE( layer->isValid() );
    adoptInShell( fx.window, layer );
    REQUIRE( layer->startEditing() );

    const QList<QgsFeatureId> ids = featureIdsOf( layer );
    REQUIRE( ids.size() == 3 );
    layer->selectByIds( QgsFeatureIds{ ids.at( 0 ), ids.at( 1 ) } );
    const int undoBefore = layer->undoStack()->count();

    // Seed a raw delete of ids[0] OUTSIDE any window edit command, then put
    // it back into the selection: the edit buffer now refuses to delete it a
    // second time ("already in the deleted set") — a genuine per-feature
    // provider-side rejection without a custom provider.
    REQUIRE( layer->deleteFeatures( QgsFeatureIds{ ids.at( 0 ) } ) );
    layer->select( ids.at( 0 ) );
    REQUIRE( layer->selectedFeatureIds().size() == 2 );

    QStringList modals;
    armModalRecorder( &modals, QMessageBox::Ok );
    fx.window.deleteSelectedFeatures();

    const QString status = fx.window.statusBar()->currentMessage();
    CHECK_FALSE( status.contains( QStringLiteral( "Deleted" ) ) );
    CHECK( status.contains( QStringLiteral( "Failed to delete" ) ) );
    CHECK( anyModalContains( modals, QStringLiteral( "Failed to delete the selected features" ) ) );

    // The failed batch was destroyed wholesale: ids[1] is back, only the
    // seeded raw delete remains on the undo stack.
    CHECK( countFeatures( layer ) == 2 );
    CHECK( layer->undoStack()->count() == undoBefore + 1 );

    layer->rollBack();
    QgsProject::instance()->removeMapLayer( layer->id() );
  }
}

// ── APP-7 ──────────────────────────────────────────────────────────────────

TEST_CASE( "Cut: a failed delete half is reported, never claimed as Cut N",
           "[app][vector_edit][r14_D][app7]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  SECTION( "happy path" )
  {
    QgsVectorLayer *layer =
      makeMemoryLayer( QStringLiteral( "field=kind:int" ), QStringLiteral( "cut_ok" ), 2 );
    REQUIRE( layer->isValid() );
    adoptInShell( fx.window, layer );
    REQUIRE( layer->startEditing() );

    const QList<QgsFeatureId> ids = featureIdsOf( layer );
    REQUIRE( ids.size() == 2 );
    layer->selectByIds( QgsFeatureIds{ ids.at( 0 ) } );

    const int undoBefore = layer->undoStack()->count();
    fx.window.cutFeatures();

    CHECK( fx.window.statusBar()->currentMessage()
             .contains( QStringLiteral( "Cut 1 feature(s)" ) ) );
    CHECK( countFeatures( layer ) == 1 );
    CHECK( layer->undoStack()->count() == undoBefore + 1 );
    CHECK_FALSE( QgisApp::instance()->clipboard()->isEmpty() );

    layer->rollBack();
    QgsProject::instance()->removeMapLayer( layer->id() );
  }

  SECTION( "failed delete half: source keeps the features, status says so" )
  {
    QgsVectorLayer *layer =
      makeMemoryLayer( QStringLiteral( "field=kind:int" ), QStringLiteral( "cut_fail" ), 2 );
    REQUIRE( layer->isValid() );
    adoptInShell( fx.window, layer );
    REQUIRE( layer->startEditing() );

    const QList<QgsFeatureId> ids = featureIdsOf( layer );
    REQUIRE( ids.size() == 2 );
    layer->selectByIds( QgsFeatureIds{ ids.at( 0 ), ids.at( 1 ) } );

    // Same seeded-refusal trick as APP-6: ids[0] is already in the deleted
    // set, so the cut's delete half cannot complete for the selection.
    REQUIRE( layer->deleteFeatures( QgsFeatureIds{ ids.at( 0 ) } ) );
    layer->select( ids.at( 0 ) );

    QStringList modals;
    armModalRecorder( &modals, QMessageBox::Ok );
    fx.window.cutFeatures();

    const QString status = fx.window.statusBar()->currentMessage();
    CHECK_FALSE( status.contains( QStringLiteral( "Cut 2" ) ) );
    CHECK( status.contains( QStringLiteral( "Cut failed" ) ) );
    // The clipboard holds a copy, but the source still contains the features.
    CHECK_FALSE( QgisApp::instance()->clipboard()->isEmpty() );
    CHECK( countFeatures( layer ) == 1 ); // ids[1] restored by the rollback
  }
}

// ── APP-8 ──────────────────────────────────────────────────────────────────

TEST_CASE( "Paste: only landed features are counted; nothing is pushed when zero land",
           "[app][vector_edit][r14_D][app8]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  SECTION( "schema mismatch: zero land, no undo step, honest 0 of N" )
  {
    QgsVectorLayer *source =
      makeMemoryLayer( QStringLiteral( "field=a:int&field=b:int" ), QStringLiteral( "paste_src" ), 2 );
    QgsVectorLayer *target =
      makeMemoryLayer( QStringLiteral( "field=kind:int" ), QStringLiteral( "paste_tgt" ), 0 );
    REQUIRE( source->isValid() );
    REQUIRE( target->isValid() );
    QgsProject::instance()->addMapLayer( source );
    adoptInShell( fx.window, target );

    source->selectAll();
    fx.window.mapCanvas()->setCurrentLayer( source );
    QTest::qWait( 20 );
    fx.window.copyFeatures();

    fx.window.mapCanvas()->setCurrentLayer( target );
    QTest::qWait( 20 );
    REQUIRE( target->startEditing() );
    REQUIRE_FALSE( QgisApp::instance()->clipboard()->isEmpty() );

    const int undoBefore = target->undoStack()->count();
    QStringList modals;
    armModalRecorder( &modals, QMessageBox::Ok );
    fx.window.pasteFeatures();

    // The buffer rejects every feature (attribute count 2 vs 1 field): the
    // status must say 0 of 2, not "Pasted 2".
    const QString status = fx.window.statusBar()->currentMessage();
    CHECK( status.contains( QStringLiteral( "Pasted 0 of 2 feature(s)" ) ) );
    CHECK_FALSE( status.contains( QStringLiteral( "Pasted 2 feature(s)" ) ) );
    CHECK( anyModalContains( modals, QStringLiteral( "Pasted 0 of 2" ) ) );

    // Nothing landed: no undo step, layer not modified.
    CHECK( target->undoStack()->count() == undoBefore );
    CHECK_FALSE( target->isModified() );
    CHECK( countFeatures( target ) == 0 );

    target->rollBack();
    QgsProject::instance()->removeMapLayer( target->id() );
    QgsProject::instance()->removeMapLayer( source->id() );
  }

  SECTION( "compatible schema: one paste step, undo empties the layer" )
  {
    QgsVectorLayer *source =
      makeMemoryLayer( QStringLiteral( "field=kind:int" ), QStringLiteral( "paste_src2" ), 2 );
    QgsVectorLayer *target =
      makeMemoryLayer( QStringLiteral( "field=kind:int" ), QStringLiteral( "paste_tgt2" ), 0 );
    REQUIRE( source->isValid() );
    REQUIRE( target->isValid() );
    QgsProject::instance()->addMapLayer( source );
    adoptInShell( fx.window, target );

    source->selectAll();
    fx.window.mapCanvas()->setCurrentLayer( source );
    QTest::qWait( 20 );
    fx.window.copyFeatures();

    fx.window.mapCanvas()->setCurrentLayer( target );
    QTest::qWait( 20 );
    REQUIRE( target->startEditing() );

    const int undoBefore = target->undoStack()->count();
    fx.window.pasteFeatures();

    CHECK( fx.window.statusBar()->currentMessage()
             .contains( QStringLiteral( "Pasted 2 feature(s)" ) ) );
    CHECK( countFeatures( target ) == 2 );
    CHECK( target->undoStack()->count() == undoBefore + 1 );

    target->undoStack()->undo();
    CHECK( countFeatures( target ) == 0 );

    target->rollBack();
    QgsProject::instance()->removeMapLayer( target->id() );
    QgsProject::instance()->removeMapLayer( source->id() );
  }
}

// ── APP-9B ─────────────────────────────────────────────────────────────────

TEST_CASE( "RsJobPanel: Load requires loadable outputs and counts only successful loads",
           "[app][task_center][r14_D][app9b]" )
{
  ensureApp();
  sicnu::jobs::JobEngine::instance().shutdownForTests();
  purgeProjectLayers();
  ShellFixture fx;

  RsJobPanel panel( &fx.window );
  QTreeWidget *tree = panel.findChild<QTreeWidget *>();
  QPushButton *load = panel.findChild<QPushButton *>( QStringLiteral( "rsJobLoadBtn" ) );
  REQUIRE( tree );
  REQUIRE( load );
  CHECK_FALSE( load->isEnabled() ); // nothing selected

  // A Completed task with NO outputs: Load must stay disabled (the old
  // "|| Completed" clause enabled it and the click only produced an info box).
  const long emptyTask = sicnu::TaskCenter::instance().enqueueTask(
    QStringLiteral( "test:r14d_no_output" ),
    QVariantMap{ { QStringLiteral( "INPUT" ), QVariant( QStringLiteral( "in.tif" ) ) } },
    /*autoLoad=*/false );
  REQUIRE( emptyTask > 0 );
  sicnu::TaskCenter::instance().markTaskRunning( emptyTask );
  sicnu::TaskCenter::instance().markTaskCompleted( emptyTask );

  panel.onTaskAdded( sicnu::TaskCenter::instance().getTaskInfo( emptyTask ) );
  REQUIRE( tree->topLevelItemCount() == 1 );
  tree->setCurrentItem( tree->topLevelItem( 0 ) );
  QTest::qWait( 20 );
  CHECK_FALSE( load->isEnabled() );

  // A Completed task WITH an output path: Load is enabled — and the click
  // must count only loads that actually land. The "output" is a DIRECTORY
  // with a .json name: QFileInfo::exists() passes (so loadPathsToMain
  // attempts it), but no provider can open a directory as a vector source,
  // so the load refuses and the panel must count 0 and honestly report
  // "no loadable output".
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString garbagePath = tmp.filePath( QStringLiteral( "garbage_output.json" ) );
  REQUIRE( QDir().mkpath( garbagePath ) );
  REQUIRE( QFileInfo::exists( garbagePath ) );

  const long garbageTask = sicnu::TaskCenter::instance().enqueueTask(
    QStringLiteral( "test:r14d_bad_output" ),
    QVariantMap{ { QStringLiteral( "INPUT" ), QVariant( QStringLiteral( "in.tif" ) ) } },
    /*autoLoad=*/false );
  REQUIRE( garbageTask > 0 );
  sicnu::TaskCenter::instance().markTaskRunning( garbageTask );
  Json::Value payload( Json::objectValue );
  payload["output"] = garbagePath.toStdString();
  sicnu::TaskCenter::instance().markTaskCompleted( garbageTask, QVariantMap{}, payload );

  panel.onTaskAdded( sicnu::TaskCenter::instance().getTaskInfo( garbageTask ) );
  REQUIRE( tree->topLevelItemCount() == 2 );
  tree->setCurrentItem( tree->topLevelItem( 0 ) ); // upsert inserts at index 0
  QTest::qWait( 20 );
  CHECK( load->isEnabled() );

  QStringList modals;
  armModalRecorder( &modals, QMessageBox::Ok, 8 );
  load->click();
  QTest::qWait( 50 );

  CHECK( anyModalContains( modals, QStringLiteral( "No loadable output path" ) ) );

  sicnu::TaskCenter::instance().clearCompletedTasks();
}

// ── R16-D T07 WP-1: edit-lease lifecycle ───────────────────────────────────

#include "project_context.h"
#include "data/data_manager.h"

TEST_CASE( "Edit lease is held for the session, exclusive, and committed (R16-D T07 WP-1)",
           "[app][vector_edit][r16d_t07][lease]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  REQUIRE( fx.window.projectContext() != nullptr );
  sicnu::data::DataManager &dataManager = fx.window.projectContext()->dataManager();

  sicnu::data::SourceDescriptor source;
  source.providerKey = QStringLiteral( "ogr" );
  source.canonicalSource = vector_test_fixtures::syntheticGeoJsonPath();
  const auto registered = dataManager.registerSource(
    sicnu::data::RegisterRequest{ source } );
  REQUIRE_FALSE( registered.assetId.isNull() );
  const sicnu::data::AssetId assetId = registered.assetId;
  // Auto-display policy presents the asset on the main view.
  QTest::qWait( 100 );

  QgsVectorLayer *assetLayer = nullptr;
  for ( QgsMapLayer *l : QgsProject::instance()->mapLayers() )
  {
    auto *v = qobject_cast<QgsVectorLayer *>( l );
    if ( v && sicnu::data::AssetId::fromString(
                  v->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() ) &&
         *sicnu::data::AssetId::fromString(
            v->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() ) == assetId )
    {
      assetLayer = v;
      break;
    }
  }
  REQUIRE( assetLayer != nullptr );
  fx.window.mapCanvas()->setCurrentLayer( assetLayer );
  QTest::qWait( 20 );

  const auto revisionBefore = dataManager.asset( assetId )->revision();

  // Start editing through the shell: the lease must be HELD, not acquired
  // and released inside acquireEditLease().
  fx.window.toggleEditing();
  REQUIRE( assetLayer->isEditable() );
  CHECK( dataManager.hasActiveEditLease( assetId ) );

  // Exclusivity: a second editor is refused while the session holds the
  // lease (the conflict dialog path in acquireEditLease).
  const auto second = dataManager.acquire(
    sicnu::data::AssetRef{ assetId, revisionBefore },
    sicnu::data::AssetUse{ sicnu::data::LeaseKind::Edit,
                           QStringLiteral( "other view" ) } );
  CHECK_FALSE( second );

  // saveEdits: the commit advances the asset revision (stale sibling
  // layers refresh) and the session continues under a re-acquired lease.
  QgsFeature f( assetLayer->fields() );
  f.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 2, 2 ) ) );
  if ( !assetLayer->isEditable() )
    REQUIRE( assetLayer->startEditing() );
  REQUIRE( assetLayer->addFeature( f ) );
  REQUIRE( assetLayer->isModified() );
  fx.window.saveEdits();
  const auto revisionAfterSave = dataManager.asset( assetId )->revision();
  CHECK( revisionAfterSave.value() > revisionBefore.value() );
  CHECK( dataManager.hasActiveEditLease( assetId ) );

  // Stop editing (no pending edits after the save): the lease is released.
  // The session now lives on the relocated replacement layer — re-select it
  // as the canvas current layer (the shell cleared the stale pointer when
  // the old layer was removed) before stopping, exactly as a user would.
  QgsVectorLayer *replacement = nullptr;
  for ( QgsMapLayer *l : QgsProject::instance()->mapLayers() )
  {
    auto *v = qobject_cast<QgsVectorLayer *>( l );
    if ( v && sicnu::data::AssetId::fromString(
                  v->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() ) &&
         *sicnu::data::AssetId::fromString(
            v->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() ) == assetId &&
         v->isEditable() )
    {
      replacement = v;
      break;
    }
  }
  REQUIRE( replacement != nullptr );
  fx.window.mapCanvas()->setCurrentLayer( replacement );
  QTest::qWait( 20 );
  fx.window.toggleEditing();
  CHECK_FALSE( dataManager.hasActiveEditLease( assetId ) );

  // Cleanup: end any edit state the re-resolved replacement layer holds.
  for ( QgsMapLayer *l : QgsProject::instance()->mapLayers() )
    if ( auto *v = qobject_cast<QgsVectorLayer *>( l ) )
      v->rollBack();
  purgeProjectLayers();
}

// ── R16-D T07 WP-2/WP-3: action gating + unavailability reasons ────────────

TEST_CASE( "Digitizing tools gate on geometry type and expose reasons (R16-D T07 WP-2/WP-3)",
           "[app][vector_edit][r16d_t07][gating]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  const auto reasonOf = []( QAction *a ) {
    return a->property( "sicnu/unavailableReason" ).toString();
  };

  SECTION( "no editable layer: disabled with a reason" )
  {
    // currentLayerChanged drives updateEditingUI (main_window_connections);
    // a null current layer does not always emit, so drive it the way a user
    // does: select a non-vector layer.
    auto *raster = new QgsRasterLayer(
      QStringLiteral( "wms:?crs=EPSG:4326" ), QStringLiteral( "no_vector_sel" ),
      QStringLiteral( "wms" ) );
    QgsProject::instance()->addMapLayer( raster );
    fx.window.mapCanvas()->setCurrentLayer( raster );
    QTest::qWait( 20 );
    QAction *node = fx.window.editingAction( QStringLiteral( "mActionVertexTool" ) );
    REQUIRE( node != nullptr );
    CHECK_FALSE( node->isEnabled() );
    CHECK_FALSE( reasonOf( node ).isEmpty() );
    CHECK( node->toolTip().contains( reasonOf( node ) ) );
  }

  const auto adoptEditing = [ &fx ]( const QString &geomType, const QString &name ) {
    auto *layer = new QgsVectorLayer(
      geomType + QStringLiteral( "?crs=EPSG:4326" ), name, QStringLiteral( "memory" ) );
    REQUIRE( layer->isValid() );
    QgsProject::instance()->addMapLayer( layer );
    fx.window.mapCanvas()->setCurrentLayer( layer );
    QTest::qWait( 20 );
    fx.window.toggleEditing();
    REQUIRE( layer->isEditable() );
    return layer;
  };

  SECTION( "point layer: line/polygon-only tools disabled with reasons" )
  {
    QgsVectorLayer *layer =
      adoptEditing( QStringLiteral( "Point" ), QStringLiteral( "pts" ) );
    QAction *node = fx.window.editingAction( QStringLiteral( "mActionVertexTool" ) );
    CHECK( node->isEnabled() );
    CHECK( reasonOf( node ).isEmpty() );
    for ( const char *name :
          { "mActionOffsetCurve", "mActionReverseLine", "mActionAddRing",
            "mActionFillRing", "mActionSplitFeatures" } )
    {
      QAction *a = fx.window.editingAction( QLatin1String( name ) );
      INFO( name );
      REQUIRE( a != nullptr );
      CHECK_FALSE( a->isEnabled() );
      CHECK_FALSE( reasonOf( a ).isEmpty() );
    }
    layer->rollBack();
  }

  SECTION( "line layer: ring tools stay disabled, line tools enabled" )
  {
    QgsVectorLayer *layer =
      adoptEditing( QStringLiteral( "LineString" ), QStringLiteral( "lins" ) );
    CHECK( fx.window.editingAction( QStringLiteral( "mActionOffsetCurve" ) )->isEnabled() );
    QAction *ring = fx.window.editingAction( QStringLiteral( "mActionAddRing" ) );
    CHECK_FALSE( ring->isEnabled() );
    CHECK_FALSE( reasonOf( ring ).isEmpty() );
    layer->rollBack();
  }

  SECTION( "polygon layer: ring tools enabled, reverse-line disabled" )
  {
    QgsVectorLayer *layer =
      adoptEditing( QStringLiteral( "Polygon" ), QStringLiteral( "polys" ) );
    CHECK( fx.window.editingAction( QStringLiteral( "mActionAddRing" ) )->isEnabled() );
    // Offset curve carries a polygon mode in this QGIS build — enabled here.
    CHECK( fx.window.editingAction( QStringLiteral( "mActionOffsetCurve" ) )->isEnabled() );
    QAction *rev = fx.window.editingAction( QStringLiteral( "mActionReverseLine" ) );
    CHECK_FALSE( rev->isEnabled() );
    CHECK_FALSE( reasonOf( rev ).isEmpty() );
    layer->rollBack();
  }

  purgeProjectLayers();
}

// ── R18-D: menu-registered digitizing actions follow the same gate ─────────

TEST_CASE( "Digitizing menu actions follow the editing availability gate (R18-D)",
           "[app][vector_edit][r18d][menu_gate]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  const auto reasonOf = []( QAction *a ) {
    return a->property( "sicnu/unavailableReason" ).toString();
  };

  // The digitizing MENU actions used to bypass the gate entirely
  // (enabled-but-will-fail). Review R18-D R1 P2-1 extended the gate from the
  // first 7 to every menu command whose toolbar twin is gated (Add Feature,
  // Node, Move, Rotate, Scale, Offset, Reverse, Reshape, Split Features,
  // Split Part, Simplify, Add/Fill/Delete Ring, Add/Delete Part, Trim/Extend,
  // Chamfer/Fillet, Feature Array).
  const std::vector<const char *> menuGated = {
    "mActionCapturePoint",   "mActionVertexTool",    "mActionMoveFeature",
    "mActionRotateFeature",  "mActionScaleFeature",  "mActionOffsetCurve",
    "mActionReverseLine",    "mActionReshape",       "mActionSplitFeatures",
    "mActionSplitParts",     "mActionSimplify",      "mActionAddRing",
    "mActionAddPart",        "mActionFillRing",      "mActionDeletePart",
    "mActionDeleteRing",     "mActionTrimExtendFeature",
    "mActionChamferFillet",  "mActionFeatureArray",
  };

  SECTION( "no editable layer: menu digitizing actions are disabled with a reason" )
  {
    // Drive updateEditingUI the way a user does: current layer is a raster.
    auto *raster = new QgsRasterLayer(
      QStringLiteral( "wms:?crs=EPSG:4326" ), QStringLiteral( "menu_gate_none" ),
      QStringLiteral( "wms" ) );
    QgsProject::instance()->addMapLayer( raster );
    fx.window.mapCanvas()->setCurrentLayer( raster );
    QTest::qWait( 20 );

    for ( const char *name : menuGated )
    {
      INFO( name );
      QAction *a = fx.window.editingAction( QLatin1String( name ) );
      REQUIRE( a != nullptr );
      CHECK_FALSE( a->isEnabled() );
      CHECK_FALSE( reasonOf( a ).isEmpty() );
      CHECK( a->toolTip().contains( reasonOf( a ) ) );
    }

    // Pin the MENU twins themselves (R2 P3-2): editingAction() prefers the
    // toolbar action for the 12 duplicated objectNames, so a menu-side
    // regression would otherwise stay invisible to the loop above.
    const QList<QAction *> menuTwins = fx.window.menuDigitizingActions();
    REQUIRE( menuTwins.size() == static_cast<int>( menuGated.size() ) );
    for ( QAction *a : menuTwins )
    {
      INFO( a->objectName().toStdString() );
      CHECK_FALSE( a->isEnabled() );
      CHECK_FALSE( reasonOf( a ).isEmpty() );
    }
  }

  SECTION( "polygon edit session unlocks matching tools, geometry still gates" )
  {
    auto *poly = new QgsVectorLayer( QStringLiteral( "Polygon?crs=EPSG:4326" ),
                                     QStringLiteral( "menu_gate_poly" ),
                                     QStringLiteral( "memory" ) );
    REQUIRE( poly->isValid() );
    QgsProject::instance()->addMapLayer( poly );
    fx.window.mapCanvas()->setCurrentLayer( poly );
    QTest::qWait( 20 );
    fx.window.toggleEditing();
    REQUIRE( poly->isEditable() );

    // Polygon-capable and geometry-agnostic menu tools unlock…
    for ( const char *name :
          { "mActionCapturePoint", "mActionVertexTool", "mActionMoveFeature",
            "mActionScaleFeature", "mActionDeleteRing", "mActionFeatureArray",
            "mActionSplitParts", "mActionAddPart", "mActionChamferFillet",
            "mActionTrimExtendFeature", "mActionOffsetCurve",
            "mActionSplitFeatures", "mActionReshape", "mActionSimplify",
            "mActionAddRing", "mActionFillRing", "mActionDeletePart" } )
    {
      INFO( name );
      QAction *a = fx.window.editingAction( QLatin1String( name ) );
      REQUIRE( a != nullptr );
      CHECK( a->isEnabled() );
    }
    // …and trim/extend squares polygon boundaries too (FeatureFilter accepts
    // any edge match — review R18-D R1 P1: the tool is NOT line-only).
    QAction *trim =
      fx.window.editingAction( QStringLiteral( "mActionTrimExtendFeature" ) );
    REQUIRE( trim != nullptr );
    CHECK( trim->isEnabled() );
    CHECK( reasonOf( trim ).isEmpty() );

    // The line-only reverse stays honestly disabled on a polygon layer.
    QAction *reverse =
      fx.window.editingAction( QStringLiteral( "mActionReverseLine" ) );
    REQUIRE( reverse != nullptr );
    CHECK_FALSE( reverse->isEnabled() );
    CHECK( reasonOf( reverse ).contains( QStringLiteral( "line" ) ) );

    poly->rollBack();
  }

  purgeProjectLayers();
}

// ── R18-D: the editing projection tracks the canvas, not the tree ──────────

TEST_CASE( "Editing projection tracks the canvas current layer, not the tree selection (R18-D)",
           "[app][vector_edit][r18d][active_editing]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  QgsVectorLayer *layer =
    makeMemoryLayer( QString(), QStringLiteral( "active_ed" ), 1 );
  REQUIRE( layer->isValid() );
  // adoptInShell sets the canvas current layer WITHOUT selecting it in the
  // layer tree — the exact shape of programmatic current-layer switches
  // (classification windows, session workspaces). The selection context
  // used to derive activeEditable only from the tree∩canvas intersection,
  // reporting "no edit session" while one was running: the shell's toggle
  // action read unchecked and a re-click offered to discard the live
  // session.
  adoptInShell( fx.window, layer );
  fx.window.toggleEditing();
  REQUIRE( layer->isEditable() );

  fx.window.selectionContext()->refreshNow();
  const sicnu::app::SelectionContextSnapshot snap =
    fx.window.selectionContext()->snapshot();
  CHECK( snap.activeEditable );
  CHECK_FALSE( snap.activeModified );

  QTest::qWait( 30 ); // let the changed() projection reach the registry
  CHECK( fx.window.commandRegistry()
             ->unavailabilityReason( QStringLiteral( "layer.saveEdits" ) )
             .isEmpty() );

  layer->rollBack();
  purgeProjectLayers();
}

// ── R18-D: task lifecycle events refresh the in-flight facts ───────────────

TEST_CASE( "Task lifecycle events refresh the in-flight facts (R18-D)",
           "[app][task_center][r18d][inflight]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  fx.window.selectionContext()->refreshNow();
  CHECK_FALSE( fx.window.selectionContext()->snapshot().hasInFlightTask );
  CHECK_FALSE( fx.window.workbenchState()->facts().taskInFlight );

  // A task enters the running state WITHOUT any canvas/selection event: the
  // predicate was injected once at assembly and nothing re-triggered the
  // projection, so the snapshot used to stay idle-stale until the user
  // happened to touch a layer.
  const long task = sicnu::TaskCenter::instance().enqueueTask(
    QStringLiteral( "test:r18d_inflight" ), QVariantMap{}, /*autoLoad=*/false );
  REQUIRE( task > 0 );
  sicnu::TaskCenter::instance().markTaskRunning( task );
  QTest::qWait( 250 ); // debounce; no canvas/selection event is touched

  CHECK( fx.window.selectionContext()->snapshot().hasInFlightTask );
  CHECK( fx.window.workbenchState()->facts().taskInFlight );

  sicnu::TaskCenter::instance().markTaskCompleted( task );
  QTest::qWait( 250 );
  CHECK_FALSE( fx.window.selectionContext()->snapshot().hasInFlightTask );
  CHECK_FALSE( fx.window.workbenchState()->facts().taskInFlight );
}

// ── R18-D: editing started outside the shell still refreshes the UI ────────

TEST_CASE( "Editing started outside the shell still refreshes the editing UI (R18-D)",
           "[app][vector_edit][r18d][external_edit]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  QgsVectorLayer *layer =
    makeMemoryLayer( QString(), QStringLiteral( "ext_edit" ), 1 );
  REQUIRE( layer->isValid() );
  adoptInShell( fx.window, layer );

  // The attribute table / facade path starts editing directly on the layer
  // (QgisApp::toggleEditing): no currentLayerChanged fires, and the shell
  // used to leave the toolbar gating on the stale no-session state —
  // dead clicks on digitizing tools that cannot work, and a toggle action
  // that reads unchecked while a session runs.
  REQUIRE( layer->startEditing() );
  QTest::qWait( 200 );

  QAction *node = fx.window.editingAction( QStringLiteral( "mActionVertexTool" ) );
  REQUIRE( node != nullptr );
  CHECK( node->isEnabled() );
  // Command registry actions carry the cmd_<id> objectName.
  QAction *toggle =
    fx.window.findChild<QAction *>( QStringLiteral( "cmd_layer.toggleEditing" ) );
  REQUIRE( toggle != nullptr );
  CHECK( toggle->isChecked() );

  fx.window.selectionContext()->refreshNow();
  CHECK( fx.window.selectionContext()->snapshot().activeEditable );

  // And the stop side re-locks the tools when the session ends the same way.
  layer->rollBack(); // emits editingStopped
  QTest::qWait( 200 );
  CHECK_FALSE( node->isEnabled() );

  purgeProjectLayers();
}

// ── R16-D T07 review R2 P1: discard path survives a pending relocation ─────

TEST_CASE( "Layer switch during a deferred relocation does not crash (R16-D T07 R2)",
           "[app][vector_edit][r16d_t07][deferred_switch]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  REQUIRE( fx.window.projectContext() != nullptr );
  sicnu::data::DataManager &dataManager = fx.window.projectContext()->dataManager();
  sicnu::data::SourceDescriptor source;
  source.providerKey = QStringLiteral( "ogr" );
  source.canonicalSource = vector_test_fixtures::syntheticGeoJsonPath();
  const auto registered = dataManager.registerSource(
    sicnu::data::RegisterRequest{ source } );
  REQUIRE_FALSE( registered.assetId.isNull() );
  const sicnu::data::AssetId assetId = registered.assetId;
  QTest::qWait( 100 );

  QgsVectorLayer *assetLayer = nullptr;
  for ( QgsMapLayer *l : QgsProject::instance()->mapLayers() )
  {
    auto *v = qobject_cast<QgsVectorLayer *>( l );
    if ( v && sicnu::data::AssetId::fromString(
                  v->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() ) &&
         *sicnu::data::AssetId::fromString(
            v->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() ) == assetId )
    {
      assetLayer = v;
      break;
    }
  }
  REQUIRE( assetLayer != nullptr );
  fx.window.mapCanvas()->setCurrentLayer( assetLayer );
  QTest::qWait( 20 );
  fx.window.toggleEditing();
  REQUIRE( assetLayer->isEditable() );

  // An asset change mid-session defers the relocation (buffered edits kept).
  dataManager.notifyExternalContentChange( assetId );

  // Switching the current layer drives confirmSaveEdits' not-modified path:
  // rollBack + rollbackEditLease -> the deferred relocation proceeds and
  // destroys the editing layer synchronously. The switch (and this test)
  // must survive it (review R16-D R2 P1).
  QPointer<QgsVectorLayer> doomed( assetLayer );
  auto *other = new QgsVectorLayer( QStringLiteral( "Point?crs=EPSG:4326" ),
                                    QStringLiteral( "switch_target" ),
                                    QStringLiteral( "memory" ) );
  QgsProject::instance()->addMapLayer( other );
  QTest::qWait( 20 );
  // Drive the REAL switch path: onLayerTreeClicked (the only caller of
  // confirmSaveEdits). The tree view carries no model in this fixture
  // (ActiveViewHost wires it per view), so a null index walks the same
  // branch a real click on a non-layer row takes: index2node -> null ->
  // newLayer(null) != currentVl -> confirmSaveEdits.
  QVERIFY( QMetaObject::invokeMethod( &fx.window, "onLayerTreeClicked",
                                      Q_ARG( QModelIndex, QModelIndex() ) ) );
  QTest::qWait( 50 );

  CHECK( doomed.isNull() ); // relocated under the session end
  // The Display Layer identity survives with a fresh, non-editing layer.
  bool replacementPresent = false;
  for ( QgsMapLayer *l : QgsProject::instance()->mapLayers() )
  {
    auto *v = qobject_cast<QgsVectorLayer *>( l );
    if ( v && sicnu::data::AssetId::fromString(
                  v->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() ) &&
         *sicnu::data::AssetId::fromString(
            v->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() ) == assetId )
    {
      replacementPresent = true;
      CHECK_FALSE( v->isEditable() );
    }
  }
  CHECK( replacementPresent );

  for ( QgsMapLayer *l : QgsProject::instance()->mapLayers() )
    if ( auto *v = qobject_cast<QgsVectorLayer *>( l ) )
      v->rollBack();
  purgeProjectLayers();
}

// ── R19-03: Edit-menu session commands ride the editing availability gate ──
// r18-D gated the 19 Digitizing twins but left the Edit menu's own
// Cut/Paste/Delete Selected as plain addAction entries: enabled with no
// edit session, honest only at use time (status-bar refusal). They join
// the same gate (no geometry mask — any editable geometry qualifies).

TEST_CASE( "Edit menu session commands gate on the editing session (R19-03)",
           "[app][vector_edit][r19d][menu_session_gate]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  const auto reasonOf = []( QAction *a ) {
    return a->property( "sicnu/unavailableReason" ).toString();
  };

  const std::vector<const char *> sessionGated = {
    "mActionCutFeatures",
    "mActionPasteFeatures",
    "mActionDeleteSelectedFeatures",
  };

  SECTION( "no editable layer: session commands are disabled with a reason" )
  {
    auto *raster = new QgsRasterLayer(
      QStringLiteral( "wms:?crs=EPSG:4326" ), QStringLiteral( "session_gate_none" ),
      QStringLiteral( "wms" ) );
    QgsProject::instance()->addMapLayer( raster );
    fx.window.mapCanvas()->setCurrentLayer( raster );
    QTest::qWait( 20 );

    // RED phase note: the lookup itself is the failing surface today — the
    // three Edit-menu entries carry no objectName and no gate registration,
    // so editingAction() finds nothing. The findChildren scan below then
    // pins the MENU entries by objectName (they have no toolbar twins, so
    // unlike the digitizing case no dedicated accessor is needed).
    const QList<QAction *> registered = fx.window.findChildren<QAction *>();
    for ( const char *name : sessionGated )
    {
      INFO( name );
      QAction *a = fx.window.editingAction( QLatin1String( name ) );
      REQUIRE( a != nullptr );
      // The registered action must be the menu entry (no toolbar twin).
      QAction *menuEntry = nullptr;
      for ( QAction *c : registered )
        if ( c->objectName() == QLatin1String( name ) )
          menuEntry = c;
      REQUIRE( menuEntry != nullptr );
      CHECK_FALSE( menuEntry->isEnabled() );
      CHECK_FALSE( reasonOf( menuEntry ).isEmpty() );
      CHECK( menuEntry->toolTip().contains( reasonOf( menuEntry ) ) );
    }
    purgeProjectLayers();
  }

  SECTION( "an edit session unlocks them; rollback re-locks" )
  {
    QgsVectorLayer *layer =
      makeMemoryLayer( QString(), QStringLiteral( "session_gate_vec" ), 1 );
    REQUIRE( layer->isValid() );
    adoptInShell( fx.window, layer );
    fx.window.toggleEditing();
    REQUIRE( layer->isEditable() );

    for ( const char *name : sessionGated )
    {
      INFO( name );
      QAction *a = fx.window.editingAction( QLatin1String( name ) );
      REQUIRE( a != nullptr );
      CHECK( a->isEnabled() );
      CHECK( reasonOf( a ).isEmpty() );
    }

    layer->rollBack();
    CHECK_FALSE( layer->isEditable() );
    for ( const char *name : sessionGated )
    {
      INFO( name );
      CHECK_FALSE( fx.window.editingAction( QLatin1String( name ) )->isEnabled() );
    }
    purgeProjectLayers();
  }
}

// ── R21-D W1b: every editing entry point keeps the DataManager Edit Lease
// honest. The attribute table, the facade and the map-tool vector layer
// tools used to commit/roll back directly on the provider, leaving the
// lease acquired by the shell's toggleEditing() held forever (asset locked
// against every other view) and the asset revision stale (siblings never
// refresh). These cases drive those exact UI paths.

#include <qgsvectorlayertools.h>

namespace
{
/// Register the writable GeoJSON fixture as a Data Asset, surface its
/// Display Layer (auto-display policy) and make it the canvas current
/// layer — the same setup as the R16-D T07 WP-1 lease case.
struct W1bAssetLayer
{
  sicnu::data::AssetId assetId;
  QgsVectorLayer *layer = nullptr;
  sicnu::data::DataManager *dataManager = nullptr;

  bool setup( QgisDesktopWindow &window )
  {
    dataManager = &window.projectContext()->dataManager();
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "ogr" );
    source.canonicalSource = vector_test_fixtures::syntheticGeoJsonPath();
    const auto registered = dataManager->registerSource(
      sicnu::data::RegisterRequest{ source } );
    if ( registered.assetId.isNull() )
      return false;
    assetId = registered.assetId;
    QTest::qWait( 100 ); // auto-display presents the asset on the main view
    for ( QgsMapLayer *l : QgsProject::instance()->mapLayers() )
    {
      auto *v = qobject_cast<QgsVectorLayer *>( l );
      const auto id = sicnu::data::AssetId::fromString(
        v ? v->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString()
          : QString() );
      if ( v && id && *id == assetId )
      {
        layer = v;
        break;
      }
    }
    if ( !layer )
      return false;
    window.mapCanvas()->setCurrentLayer( layer );
    QTest::qWait( 20 );
    return true;
  }

  /// Re-resolve the (possibly relocated — commitEditLease swaps Display
  /// Layers synchronously) layer for the asset.
  QgsVectorLayer *resolve()
  {
    for ( QgsMapLayer *l : QgsProject::instance()->mapLayers() )
    {
      auto *v = qobject_cast<QgsVectorLayer *>( l );
      const auto id = sicnu::data::AssetId::fromString(
        v ? v->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString()
          : QString() );
      if ( v && id && *id == assetId )
        return v;
    }
    return nullptr;
  }

  static bool makeModified( QgsVectorLayer *layer )
  {
    if ( !layer->isEditable() && !layer->startEditing() )
      return false;
    QgsFeature f( layer->fields() );
    f.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 3, 3 ) ) );
    return layer->addFeature( f ) && layer->isModified();
  }
};

void rollbackAllVectorLayers()
{
  for ( QgsMapLayer *l : QgsProject::instance()->mapLayers() )
    if ( auto *v = qobject_cast<QgsVectorLayer *>( l ) )
      v->rollBack();
}
} // namespace

TEST_CASE( "Facade toggleEditing stop releases the DataManager Edit Lease (W1b)",
           "[app][vector_edit][w1b][lease]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  W1bAssetLayer fxAsset;
  REQUIRE( fxAsset.setup( fx.window ) );
  const auto revisionBefore =
    fxAsset.dataManager->asset( fxAsset.assetId )->revision();

  // Session starts through the shell menu (the lease owner).
  fx.window.toggleEditing();
  REQUIRE( fxAsset.layer->isEditable() );
  REQUIRE( fxAsset.dataManager->hasActiveEditLease( fxAsset.assetId ) );
  REQUIRE( W1bAssetLayer::makeModified( fxAsset.layer ) );

  // The attribute table's "Toggle Editing" routes through the facade
  // (qgsattributetabledialog.cpp:930): commit + leave edit mode.
  REQUIRE( QgisApp::instance()->toggleEditing( fxAsset.layer ) );

  // The coordinated commit may relocate the Display Layer: re-resolve.
  QgsVectorLayer *resolved = fxAsset.resolve();
  REQUIRE( resolved != nullptr );
  CHECK_FALSE( resolved->isEditable() );

  // The session ended through a non-shell entry: the asset must not stay
  // locked and the commit must have advanced the data-plane revision.
  CHECK_FALSE( fxAsset.dataManager->hasActiveEditLease( fxAsset.assetId ) );
  CHECK( fxAsset.dataManager->asset( fxAsset.assetId )->revision().value() >
         revisionBefore.value() );

  rollbackAllVectorLayers();
  purgeProjectLayers();
}

TEST_CASE( "Facade saveEdits with leaveEditable=false ends the lease (W1b)",
           "[app][vector_edit][w1b][lease]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  W1bAssetLayer fxAsset;
  REQUIRE( fxAsset.setup( fx.window ) );
  const auto revisionBefore =
    fxAsset.dataManager->asset( fxAsset.assetId )->revision();

  fx.window.toggleEditing();
  REQUIRE( fxAsset.layer->isEditable() );
  REQUIRE( fxAsset.dataManager->hasActiveEditLease( fxAsset.assetId ) );
  REQUIRE( W1bAssetLayer::makeModified( fxAsset.layer ) );

  // The attribute table's save cascade saves referencing layers with
  // leaveEditable semantics; the explicit close
  // (qgsattributetabledialog.cpp:706) ends the session.
  REQUIRE( QgisApp::instance()->saveEdits( fxAsset.layer,
                                           /*leaveEditable=*/false,
                                           /*triggerRepaint=*/false ) );

  QgsVectorLayer *resolved = fxAsset.resolve();
  REQUIRE( resolved != nullptr );
  CHECK_FALSE( resolved->isEditable() );
  CHECK_FALSE( fxAsset.dataManager->hasActiveEditLease( fxAsset.assetId ) );
  CHECK( fxAsset.dataManager->asset( fxAsset.assetId )->revision().value() >
         revisionBefore.value() );

  rollbackAllVectorLayers();
  purgeProjectLayers();
}

TEST_CASE( "Facade batch saveEdits keeps the session under a fresh lease (W1b)",
           "[app][vector_edit][w1b][lease]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  W1bAssetLayer fxAsset;
  REQUIRE( fxAsset.setup( fx.window ) );
  const auto revisionBefore =
    fxAsset.dataManager->asset( fxAsset.assetId )->revision();

  fx.window.toggleEditing();
  REQUIRE( fxAsset.layer->isEditable() );
  REQUIRE( fxAsset.dataManager->hasActiveEditLease( fxAsset.assetId ) );
  REQUIRE( W1bAssetLayer::makeModified( fxAsset.layer ) );

  // The attribute table's Save button fires the no-arg batch overload
  // (qgsattributetabledialog.cpp:299): commit everything editable, then
  // re-enter edit mode — which must happen under a re-acquired lease.
  QgisApp::instance()->saveEdits();

  QgsVectorLayer *resolved = fxAsset.resolve();
  REQUIRE( resolved != nullptr );
  CHECK( resolved->isEditable() );
  CHECK( fxAsset.dataManager->hasActiveEditLease( fxAsset.assetId ) );
  CHECK( fxAsset.dataManager->asset( fxAsset.assetId )->revision().value() >
         revisionBefore.value() );

  rollbackAllVectorLayers();
  purgeProjectLayers();
}

TEST_CASE( "Map-tool vector layer tools bracket the Edit Lease (W1b)",
           "[app][vector_edit][w1b][lease][tools]" )
{
  ensureApp();
  purgeProjectLayers();
  ShellFixture fx;

  W1bAssetLayer fxAsset;
  REQUIRE( fxAsset.setup( fx.window ) );
  const auto revisionBefore =
    fxAsset.dataManager->asset( fxAsset.assetId )->revision();

  // Ported map tools start sessions through QgsGuiVectorLayerTools (the
  // QgsVectorLayerTools installed at main_window.cpp:534) — that path must
  // acquire the lease exactly like the shell's toggleEditing().
  QgsVectorLayerTools *tools = QgisApp::instance()->vectorLayerTools();
  REQUIRE( tools != nullptr );
  REQUIRE( tools->startEditing( fxAsset.layer ) );
  CHECK( fxAsset.layer->isEditable() );
  CHECK( fxAsset.dataManager->hasActiveEditLease( fxAsset.assetId ) );

  REQUIRE( W1bAssetLayer::makeModified( fxAsset.layer ) );

  // Stop-editing asks the user (modal): answer Save. The recorder runs the
  // nested loop the same way the other modal cases in this file do.
  QStringList modals;
  armModalRecorder( &modals, QMessageBox::Save, 4 );
  CHECK( tools->stopEditing( fxAsset.layer, /*allowCancel=*/false ) );
  QTest::qWait( 50 );
  REQUIRE( anyModalContains( modals, QStringLiteral( "save" ) ) );

  QgsVectorLayer *resolved = fxAsset.resolve();
  REQUIRE( resolved != nullptr );
  CHECK_FALSE( resolved->isEditable() );
  CHECK_FALSE( fxAsset.dataManager->hasActiveEditLease( fxAsset.assetId ) );
  CHECK( fxAsset.dataManager->asset( fxAsset.assetId )->revision().value() >
         revisionBefore.value() );

  rollbackAllVectorLayers();
  purgeProjectLayers();
}
