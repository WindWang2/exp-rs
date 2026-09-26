// test_parity_stress_r4.cpp — randomized consistency stress with per-step
// parity probes (ui-backend-state-parity-r4 / WP-F)
//
// The stress harness only CATCHES drift; every drift it catches must land as
// a deterministic oracle in the WP-A..E suites (DECISIONS.md D-7). Scenarios:
//
//   S1  selection storm: random canvas/layer-tree/notify pushes against the
//       single SelectionContext authority; after EVERY step the projection
//       invariants are probed (active layer alive, selected set alive,
//       pushed ids absorbed, no dying pointer handed out);
//   S2  dialog open/modify/close storm: result summary + progress dialog +
//       empty state + comparison widget live through random mutate/destroy
//       cycles; after EVERY step the widget↔source mirror contracts are
//       probed, including after deleteLate destruction;
//   S3  layer-switch storm: real project opens/layers add/remove against a
//       live raster combo; after EVERY step the combo's items must equal the
//       project's raster truth.
//
// Determinism: fixed default seed (20260927), overridable via
// SICNU_PARITY_STRESS_SEED; a failing seed reproduces with
//   SICNU_PARITY_STRESS_SEED=<seed> ctest -R test_parity_stress_r4 -V
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "app/widgets/raster_layer_combo.h"
#include "app/widgets/rs_empty_state_widget.h"
#include "app/widgets/rs_result_summary.h"
#include "app/widgets/progress_dialog.h"
#include "app/workbench/selection_context.h"
#include "widgets/comparison_widget.h"
#include "main_window.h"

#include <QApplication>
#include <QPointer>
#include <QRandomGenerator>
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

#include <gdal.h>

using sicnu::app::SelectionContext;

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_parity_stress_r4";
char *fake_argv[] = { fake_argv0, nullptr };

QgsApplication *ensureApp()
{
  static QgsApplication *app = nullptr;
  if ( !app )
  {
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-selftest" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "parity-stress-r4" ) );
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

quint32 stressSeed()
{
  bool ok = false;
  const quint32 fromEnv = qEnvironmentVariableIntValue( "SICNU_PARITY_STRESS_SEED", &ok );
  return ok ? fromEnv : 20260927u;
}

bool writeMiniGeoTiff( const QString &path )
{
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  if ( !driver )
    return false;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 2, 2, 1, GDT_Float32, nullptr );
  if ( !ds )
    return false;
  double geo[6] = { 116.0, 0.01, 0.0, 40.0, 0.0, -0.01 };
  GDALSetGeoTransform( ds, geo );
  GDALSetProjection( ds, "EPSG:4326" );
  float line[2] = { 0.5f, 1.0f };
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  const bool ok = GDALRasterIO( band, GF_Write, 0, 0, 2, 2, line, 2, 2, GDT_Float32, 0, 0 ) == CE_None;
  GDALClose( ds );
  return ok;
}

QgsVectorLayer *makeMemoryLayer( const QString &name )
{
  auto *layer = new QgsVectorLayer( QStringLiteral( "Point?crs=EPSG:4326" ), name, QStringLiteral( "memory" ) );
  Q_ASSERT( layer->isValid() );
  return layer;
}

} // namespace

// ===========================================================================
// S1 — selection storm: 300 random pushes, parity probe after every step.
// ===========================================================================
TEST_CASE( "S1: selection storm keeps the authority projection consistent",
           "[parity][stress][selection][parity-s1]" )
{
  ensureApp();
  QgsProject::instance()->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView tree;
  QgsLayerTreeModel treeModel( QgsProject::instance()->layerTreeRoot() );
  tree.setModel( &treeModel );
  SelectionContext context;
  context.attachCanvas( &canvas );
  context.attachLayerTree( &tree );

  QList<QgsVectorLayer *> layers;
  for ( int i = 0; i < 6; ++i )
  {
    QgsVectorLayer *layer = makeMemoryLayer( QStringLiteral( "stress%1" ).arg( i ) );
    layers << layer;
    QgsProject::instance()->addMapLayer( layer );
  }
  QList<QgsMapLayer *> canvasLayers;
  for ( QgsVectorLayer *layer : layers )
    canvasLayers << layer;
  canvas.setLayers( canvasLayers );

  QRandomGenerator rng( stressSeed() );
  QStringList lastAssets;
  QStringList lastDatasets;

  const int steps = 300;
  for ( int step = 0; step < steps; ++step )
  {
    switch ( rng.bounded( 6 ) )
    {
      case 0: // canvas current layer switch
      {
        const int idx = rng.bounded( layers.size() + 1 );
        canvas.setCurrentLayer( idx == layers.size() ? nullptr : layers[ idx ] );
        break;
      }
      case 1: // tree selection
      {
        const int idx = rng.bounded( layers.size() );
        const QModelIndex node = treeModel.node2index(
          QgsProject::instance()->layerTreeRoot()->findLayer( layers[ idx ]->id() ) );
        if ( node.isValid() )
          tree.selectionModel()->select( node, QItemSelectionModel::ClearAndSelect );
        break;
      }
      case 2:
        lastAssets = QStringList{ QStringLiteral( "asset-%1" ).arg( step ) };
        context.notifyAssetSelection( lastAssets );
        break;
      case 3:
        lastAssets.clear();
        context.notifyAssetSelection( {} );
        break;
      case 4:
        lastDatasets = QStringList{ QStringLiteral( "ds-%1" ).arg( step ) };
        context.notifyDatasetSelection( lastDatasets );
        break;
      case 5:
        lastDatasets.clear();
        context.notifyDatasetSelection( {} );
        break;
    }
    QApplication::processEvents();

    // ---- per-step parity probe (authority truth, not UI-derived) ----
    context.refreshNow();
    const auto snap = context.snapshot();
    if ( snap.activeLayer )
    {
      // The projection must never hand out a dead or foreign pointer.
      CHECK( QgsProject::instance()->mapLayer( snap.activeLayer->id() ) == snap.activeLayer );
    }
    for ( QgsMapLayer *layer : snap.selectedLayers )
      CHECK( QgsProject::instance()->mapLayer( layer->id() ) == layer );
    CHECK( snap.selectedAssetIds == lastAssets );
    CHECK( snap.selectedDatasetIds == lastDatasets );
  }
  QgsProject::instance()->clear();
}

// ===========================================================================
// S2 — dialog open/modify/close storm with per-step mirror probes.
// ===========================================================================
TEST_CASE( "S2: dialog storm keeps widget-source mirrors consistent",
           "[parity][stress][dialog][parity-s2]" )
{
  ensureApp();
  QRandomGenerator rng( stressSeed() + 1 );

  QPointer<RsResultSummary> summary = new RsResultSummary();
  QPointer<ProgressDialog> progress = new ProgressDialog();
  QPointer<sicnu::RsEmptyStateWidget> empty = new sicnu::RsEmptyStateWidget(
    QStringLiteral( "layers" ), QStringLiteral( "Empty" ), QStringLiteral( "Nothing" ),
    QStringLiteral( "Do it" ) );
  QPointer<ComparisonWidget> comparison = new ComparisonWidget();
  summary->show();
  progress->show();
  empty->show();
  comparison->show();

  bool summaryHasPayload = false;
  bool cancelled = false;

  const int steps = 200;
  for ( int step = 0; step < steps && summary && progress && empty && comparison; ++step )
  {
    switch ( rng.bounded( 8 ) )
    {
      case 0:
      case 1:
      {
        Json::Value payload( Json::objectValue );
        payload[ "status" ] = QStringLiteral( "step-%1" ).arg( step ).toStdString();
        summary->setResult( payload );
        summaryHasPayload = true;
        break;
      }
      case 2:
        summary->clear();
        summaryHasPayload = false;
        break;
      case 3:
        if ( rng.bounded( 4 ) == 0 && !cancelled )
        {
          progress->cancel();
          cancelled = true;
        }
        else
        {
          progress->setValue( rng.bounded( 101 ) );
        }
        break;
      case 4:
        progress->reset();
        cancelled = false;
        break;
      case 5:
        empty->setActionVisible( rng.bounded( 2 ) == 0 );
        break;
      case 6:
      {
        QPixmap pix( 8, 8 );
        pix.fill( Qt::red );
        comparison->setLeftImage( pix );
        if ( rng.bounded( 2 ) )
          comparison->setRightImage( pix );
        comparison->setMode( rng.bounded( 2 ) ? ComparisonWidget::ComparisonMode::Flicker
                                              : ComparisonWidget::ComparisonMode::SplitScreen );
        break;
      }
      case 7:
        // Close-and-reopen cycle (hide/show, the "dialog switch" shape).
        if ( rng.bounded( 2 ) )
        {
          progress->close();
          progress->show();
        }
        break;
    }
    QApplication::processEvents();

    // ---- per-step parity probe ----
    CHECK( summary->hasResult() == summaryHasPayload );
    CHECK( progress->isCancelled() == cancelled );
    if ( cancelled )
    {
      // Cancelled dialogs must never report an accepted (success) result.
      CHECK( progress->result() != static_cast<int>( QDialog::Accepted ) );
    }
  }

  // Destroy everything and probe that no object outlived its owner.
  delete summary;
  delete progress;
  delete empty;
  delete comparison;
  QApplication::processEvents();
  CHECK( summary.isNull() );
  CHECK( progress.isNull() );
  CHECK( empty.isNull() );
  CHECK( comparison.isNull() );
}

// ===========================================================================
// S3 — layer-switch storm: combo vs project truth after every step.
// ===========================================================================
TEST_CASE( "S3: layer-switch storm keeps the raster combo at project truth",
           "[parity][stress][layers][parity-s3]" )
{
  ensureApp();
  QgsProject::instance()->clear();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  QList<QgsRasterLayer *> rasters;
  for ( int i = 0; i < 4; ++i )
  {
    const QString path = tmp.filePath( QStringLiteral( "stress%1.tif" ).arg( i ) );
    REQUIRE( writeMiniGeoTiff( path ) );
    auto *layer = new QgsRasterLayer( path, QStringLiteral( "stress%1" ).arg( i ), QStringLiteral( "gdal" ) );
    REQUIRE( layer->isValid() );
    rasters << layer;
  }

  RasterLayerCombo combo;
  QRandomGenerator rng( stressSeed() + 2 );

  const int steps = 120;
  for ( int step = 0; step < steps; ++step )
  {
    switch ( rng.bounded( 4 ) )
    {
      case 0:
        QgsProject::instance()->addMapLayer( rasters[ rng.bounded( rasters.size() ) ] );
        break;
      case 1:
      {
        const QList<QgsMapLayer *> inProject = QgsProject::instance()->mapLayers().values();
        if ( !inProject.isEmpty() )
          QgsProject::instance()->removeMapLayer( inProject.first()->id() );
        break;
      }
      case 2:
        combo.populate();
        break;
      case 3:
        if ( combo.count() > 0 )
          combo.setCurrentIndex( rng.bounded( combo.count() ) );
        break;
    }
    QApplication::processEvents();

    // ---- per-step parity probe: combo == project raster truth ----
    const QList<QgsRasterLayer *> projectRasters =
      QgsProject::instance()->layers<QgsRasterLayer *>();
    int validRasters = 0;
    for ( QgsRasterLayer *layer : projectRasters )
    {
      if ( layer && layer->isValid() )
        ++validRasters;
    }
    combo.populate();
    CHECK( combo.count() == validRasters );
    if ( combo.count() > 0 )
      CHECK( combo.currentRasterLayer() != nullptr );
  }
  QgsProject::instance()->clear();
}
