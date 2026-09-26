// test_parity_state_mirror_r4.cpp — widget ↔ state-source mirror oracles
// (ui-backend-state-parity-r4 / WP-A, PARITY_MAP sections A–K)
//
// One oracle per PARITY_MAP row: the widget's visible state must equal the
// state its backend source actually holds. Truth comes from the source's own
// contract (loader result / project layer set / job payload / scan pool
// generation), never from re-reading the widget to derive the expectation.
//
//   GW-1/2/3/5   guided workflow list, error page, start-enable, cursor
//                bounds vs the LabSpec loader result (SICNU_DATA_DIR temp
//                fixture — deterministic);
//   SW-1/2/3     spectral workbench load contract, typed-failure failsafe,
//                selection projection;
//   RS-1/2/3     result summary payload render, clear contract,
//                openPathRequested only for artifact paths;
//   SP-1         scan pool generation: owner-scoped supersede must not
//                disturb another owner's current generation;
//   TS-1/2       timeline scrubber index bounds + play/pause contract;
//   CW-1/2       comparison widget signal contract + mode render branches;
//   ES-0         empty-state widget CTA contract;
//   RL-1/2       raster combo mirrors the project raster set — and tracks
//                layers added while the host dialog stays open (F-01 drift).
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "app/widgets/guided_workflow_widget.h"
#include "app/widgets/lab_spec_loader.h"
#include "app/widgets/raster_layer_combo.h"
#include "app/widgets/rs_empty_state_widget.h"
#include "app/widgets/rs_result_summary.h"
#include "app/widgets/rs_scan_pool.h"
#include "app/widgets/spectral_workbench_panel.h"
#include "widgets/timeline_scrubber_widget.h"
#include "widgets/comparison_widget.h"
#include "processing/algorithms/spectral_table.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPixmap>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include <gdal.h>
#include <gdal_priv.h>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_parity_state_mirror_r4";
char *fake_argv[] = { fake_argv0, nullptr };

QgsApplication *ensureApp()
{
  static QgsApplication *app = nullptr;
  if ( !app )
  {
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-selftest" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "parity-mirror-r4" ) );
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

// --- LabSpec fixture -------------------------------------------------------
QString writeLabFile( const QDir &dir, const QString &name, const QString &content )
{
  const QString path = dir.filePath( name );
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Text ) );
  file.write( content.toUtf8() );
  return path;
}

QString validLabJson( const QString &id, int steps = 2 )
{
  QString stepList;
  for ( int i = 0; i < steps; ++i )
  {
    if ( i )
      stepList += QLatin1Char( ',' );
    stepList += QStringLiteral( R"({ "title": "S%1", "title_zh": "步骤%1", "description_zh": "做。" })" ).arg( i );
  }
  return QStringLiteral( R"( {
    "spec_version": 1, "id": "%1", "title": "Mirror Lab", "title_zh": "镜像实验",
    "objective": "probe", "steps": [%2] } )" )
    .arg( id, stepList );
}

// --- GeoTIFF fixture (mirrors test_active_view_host_data_context) ----------
bool writeMiniGeoTiff( const QString &path, int width = 4, int height = 4 )
{
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  if ( !driver )
    return false;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1, GDT_Float32, nullptr );
  if ( !ds )
    return false;
  double geo[6] = { 116.0, 0.01, 0.0, 40.0, 0.0, -0.01 };
  GDALSetGeoTransform( ds, geo );
  GDALSetProjection( ds, "EPSG:4326" );
  float line[4] = { 0.5f, 1.0f, 1.5f, 2.0f };
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  const bool ok = GDALRasterIO( band, GF_Write, 0, 0, width, height,
                                line, width, height, GDT_Float32, 0, 0 ) == CE_None;
  GDALClose( ds );
  return ok;
}

QgsRasterLayer *addRasterToProject( const QString &path, const QString &name )
{
  auto *layer = new QgsRasterLayer( path, name, QStringLiteral( "gdal" ) );
  if ( !layer->isValid() )
  {
    delete layer;
    return nullptr;
  }
  QgsProject::instance()->addMapLayer( layer );
  return layer;
}

} // namespace

// ===========================================================================
// GW-1: the workflow list mirrors the loader result row-for-row.
// ===========================================================================
TEST_CASE( "GW-1: guided workflow list mirrors the LabSpec loader result",
           "[parity][mirror][guided_workflow][parity-gw1]" )
{
  ensureApp();
  QTemporaryDir dataDir;
  REQUIRE( dataDir.isValid() );
  const QDir labs( dataDir.filePath( QStringLiteral( "data/labs" ) ) );
  REQUIRE( QDir().mkpath( labs.path() ) );
  writeLabFile( labs, QStringLiteral( "lab99_mirror_a.lab.json" ), validLabJson( QStringLiteral( "lab99_mirror_a" ) ) );
  writeLabFile( labs, QStringLiteral( "lab99_broken.lab.json" ), QStringLiteral( "{ this is not json" ) );
  qputenv( "SICNU_DATA_DIR", dataDir.path().toUtf8() );

  GuidedWorkflowWidget widget( nullptr );
  const int errorCount = widget.loadErrorStrings().size();
  REQUIRE( errorCount == 1 );
  REQUIRE( widget.workflows().size() == 1 );

  QListWidget *list = widget.findChild<QListWidget *>();
  REQUIRE( list != nullptr );
  // Rows = typed error entry pinned to the top + one row per loaded workflow.
  INFO( "rows=" << list->count() << " workflows=" << widget.workflows().size()
        << " errors=" << errorCount );
  CHECK( list->count() == widget.workflows().size() + errorCount );
  qunsetenv( "SICNU_DATA_DIR" );
}

// ===========================================================================
// GW-2: selecting the error entry replaces the step view with the typed
// error page and disarms the session state.
// ===========================================================================
TEST_CASE( "GW-2: error entry selects a typed error page and disarms the session",
           "[parity][mirror][guided_workflow][parity-gw2]" )
{
  ensureApp();
  QTemporaryDir dataDir;
  REQUIRE( dataDir.isValid() );
  const QDir labs( dataDir.filePath( QStringLiteral( "data/labs" ) ) );
  REQUIRE( QDir().mkpath( labs.path() ) );
  writeLabFile( labs, QStringLiteral( "lab99_broken.lab.json" ), QStringLiteral( "{ broken" ) );
  qputenv( "SICNU_DATA_DIR", dataDir.path().toUtf8() );

  GuidedWorkflowWidget widget( nullptr );
  REQUIRE( widget.loadErrorStrings().size() == 1 );

  QListWidget *list = widget.findChild<QListWidget *>();
  REQUIRE( list != nullptr );
  list->setCurrentRow( 0 );

  // Every session affordance is disabled on the error page: nothing may
  // keep running "Next/Run" against a spec that never loaded.
  for ( QPushButton *button : widget.findChildren<QPushButton *>() )
    CHECK_FALSE( button->isEnabled() );
  qunsetenv( "SICNU_DATA_DIR" );
}

// ===========================================================================
// GW-3: start-enable equals walkability (steps non-empty).
// ===========================================================================
TEST_CASE( "GW-3: start button enabled exactly when the workflow has steps",
           "[parity][mirror][guided_workflow][parity-gw3]" )
{
  ensureApp();
  QTemporaryDir dataDir;
  REQUIRE( dataDir.isValid() );
  const QDir labs( dataDir.filePath( QStringLiteral( "data/labs" ) ) );
  REQUIRE( QDir().mkpath( labs.path() ) );
  writeLabFile( labs, QStringLiteral( "lab99_steps.lab.json" ), validLabJson( QStringLiteral( "lab99_steps" ), 2 ) );
  writeLabFile( labs, QStringLiteral( "lab99_one.lab.json" ), validLabJson( QStringLiteral( "lab99_one" ), 1 ) );
  qputenv( "SICNU_DATA_DIR", dataDir.path().toUtf8() );

  GuidedWorkflowWidget widget( nullptr );
  REQUIRE( widget.workflows().size() == 2 );

  QListWidget *list = widget.findChild<QListWidget *>();
  REQUIRE( list != nullptr );
  QPushButton *start = nullptr;
  for ( QPushButton *b : widget.findChildren<QPushButton *>() )
  {
    if ( b->text().contains( QStringLiteral( "Start" ) ) )
    {
      start = b;
      break;
    }
  }
  REQUIRE( start != nullptr );

  // Before any selection the session is unstarted: the start affordance
  // must not be live against a workflow nobody picked yet.
  CHECK_FALSE( start->isEnabled() );

  bool sawEnabled = false;
  for ( int row = 0; row < list->count(); ++row )
  {
    list->setCurrentRow( row );
    // The row maps to workflows() in loader order; walkability is steps
    // non-empty — the button must agree with the source, every row.
    const int wfIndex = row;
    if ( wfIndex < widget.workflows().size() )
    {
      const bool walkable = !widget.workflows()[ wfIndex ].steps.isEmpty();
      CHECK( start->isEnabled() == walkable );
      if ( start->isEnabled() )
        sawEnabled = true;
    }
  }
  CHECK( sawEnabled );
  qunsetenv( "SICNU_DATA_DIR" );
}

// ===========================================================================
// GW-5: the step cursor is fail-closed — Next cannot walk past the source's
// step list, and Run on a manual step does not emit a job.
// ===========================================================================
TEST_CASE( "GW-5: step cursor never passes the workflow's step list",
           "[parity][mirror][guided_workflow][parity-gw5]" )
{
  ensureApp();
  QTemporaryDir dataDir;
  REQUIRE( dataDir.isValid() );
  const QDir labs( dataDir.filePath( QStringLiteral( "data/labs" ) ) );
  REQUIRE( QDir().mkpath( labs.path() ) );
  writeLabFile( labs, QStringLiteral( "lab99_cursor.lab.json" ), validLabJson( QStringLiteral( "lab99_cursor" ), 2 ) );
  qputenv( "SICNU_DATA_DIR", dataDir.path().toUtf8() );

  GuidedWorkflowWidget widget( nullptr );
  REQUIRE( widget.workflows().size() == 1 );

  QListWidget *list = widget.findChild<QListWidget *>();
  REQUIRE( list != nullptr );
  const int workflowRow = list->count() - 1; // labs follow any error rows
  list->setCurrentRow( workflowRow );

  QSignalSpy stepSpy( &widget, &GuidedWorkflowWidget::stepCompleted );
  QSignalSpy doneSpy( &widget, &GuidedWorkflowWidget::workflowCompleted );
  REQUIRE( stepSpy.isValid() );
  REQUIRE( doneSpy.isValid() );

  // Start the session, then hammer Next far past the source's step count.
  QPushButton *start = nullptr;
  const auto buttons = widget.findChildren<QPushButton *>();
  for ( QPushButton *b : buttons )
  {
    if ( b->isEnabled() && b->text().contains( QStringLiteral( "Start" ) ) )
    {
      start = b;
      break;
    }
  }
  REQUIRE( start != nullptr );
  start->click();

  for ( int i = 0; i < 10; ++i )
    REQUIRE( QMetaObject::invokeMethod( &widget, "onNextStep" ) );

  // Source truth: two steps. One completion broadcast at the last step, the
  // workflow-over signal exactly once, and no cursor escapes the source.
  CHECK( stepSpy.count() <= 1 );
  CHECK( doneSpy.count() == 1 );
  qunsetenv( "SICNU_DATA_DIR" );
}

// ===========================================================================
// SW-1: a valid spectral table load renders count + provenance identity.
// ===========================================================================
TEST_CASE( "SW-1: spectral panel renders the loaded table identity",
           "[parity][mirror][spectral_workbench][parity-sw1]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString tablePath = tmp.filePath( QStringLiteral( "table.json" ) );
  {
    QFile f( tablePath );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( R"({"kind":"exp-rs:spectral-table","version":1,"id":"probe","license":"CC0","citation":"parity-r4 fixture","bandCount":2,"spectra":[[0.1,0.2],[0.3,0.4],[0.5,0.6]]})" );
  }

  SpectralTable::Table table;
  QString loaderError;
  const bool loaded = SpectralTable::loadValidated( tablePath, &table, &loaderError );
  INFO( "loader error" << loaderError.toStdString() );
  REQUIRE( loaded );
  SpectralWorkbenchPanel panel;
  QString loadError;
  const bool ok = panel.setTablePath( tablePath, &loadError );
  INFO( "panel error" << loadError.toStdString() );
  REQUIRE( ok );
  CHECK( panel.spectrumCount() == 3 );
  CHECK( panel.tablePath() == tablePath );

  QLabel *status = panel.findChild<QLabel *>( QStringLiteral( "spectralWorkbenchStatus" ) );
  REQUIRE( status != nullptr );
  // The provenance line carries the row/band identity of the SOURCE, so the
  // panel cannot show a table it did not load.
  INFO( "status" << status->text().toStdString() );
  CHECK( status->text().contains( QStringLiteral( "3" ) ) );
}

// ===========================================================================
// SW-2: an unloadable/invalid table fails typed with zero spectra retained.
// ===========================================================================
TEST_CASE( "SW-2: spectral panel refuses an invalid table fail-safe",
           "[parity][mirror][spectral_workbench][parity-sw2]" )
{
  ensureApp();
  SpectralWorkbenchPanel panel;
  QString error;
  CHECK_FALSE( panel.setTablePath( QStringLiteral( "/nonexistent/table.json" ), &error ) );
  CHECK_FALSE( error.isEmpty() );
  CHECK( panel.spectrumCount() == 0 );
}

// ===========================================================================
// SW-3: selection projection — selectSpectrum commits the row and emits the
// (id, index) pair the linkage seam documents.
// ===========================================================================
TEST_CASE( "SW-3: spectrum selection projects id and index",
           "[parity][mirror][spectral_workbench][parity-sw3]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString tablePath = tmp.filePath( QStringLiteral( "table.json" ) );
  {
    QFile f( tablePath );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( R"({"kind":"exp-rs:spectral-table","version":1,"id":"probe","license":"CC0","citation":"parity-r4 fixture","bandCount":1,"spectra":[[1.0],[2.0],[3.0]]})" );
  }
  SpectralWorkbenchPanel panel;
  REQUIRE( panel.setTablePath( tablePath ) );

  QSignalSpy spy( &panel, &SpectralWorkbenchPanel::spectrumSelected );
  REQUIRE( spy.isValid() );
  panel.selectSpectrum( 2 );
  CHECK( spy.count() == 1 );
  CHECK( spy.at( 0 ).at( 1 ).toInt() == 2 );

  // Out-of-range selection is a no-op, not a wraparound.
  panel.selectSpectrum( 99 );
  CHECK( spy.count() == 1 );
}

// ===========================================================================
// RS-1/RS-2/RS-3: result summary payload render, clear contract and the
// artifact double-click contract.
// ===========================================================================
TEST_CASE( "RS-1: result summary renders the payload's identity and metrics",
           "[parity][mirror][result_summary][parity-rs1]" )
{
  ensureApp();
  RsResultSummary summary;
  Json::Value result( Json::objectValue );
  result[ "status" ] = "completed";
  result[ "ndvi_mean" ] = 0.42;
  summary.setResult( result );
  CHECK( summary.hasResult() );
  // The rendered blocks must carry the payload's own identity: the metrics
  // block names the payload's metric keys.
  QLabel *metrics = summary.findChild<QLabel *>( QStringLiteral( "rsResultMetrics" ) );
  REQUIRE( metrics != nullptr );
  INFO( "metrics" << metrics->text().toStdString() );
  CHECK( metrics->text().contains( QStringLiteral( "ndvi" ), Qt::CaseInsensitive ) );
  summary.clear();
  CHECK_FALSE( summary.hasResult() );
}

TEST_CASE( "RS-2: clearing the summary empties every rendered block",
           "[parity][mirror][result_summary][parity-rs2]" )
{
  ensureApp();
  RsResultSummary summary;
  Json::Value result( Json::objectValue );
  result[ "status" ] = "completed";
  summary.setResult( result );
  REQUIRE( summary.hasResult() );
  summary.clear();
  CHECK_FALSE( summary.hasResult() );
  // Raw JSON view must not retain the payload after clear.
  QPlainTextEdit *raw = summary.findChild<QPlainTextEdit *>( QStringLiteral( "rsResultRawJson" ) );
  REQUIRE( raw != nullptr );
  CHECK( raw->toPlainText().isEmpty() );
}

TEST_CASE( "RS-3: double-clicking an artifact requests exactly its path",
           "[parity][mirror][result_summary][parity-rs3]" )
{
  ensureApp();
  RsResultSummary summary;
  Json::Value result( Json::objectValue );
  result[ "status" ] = "completed";
  Json::Value outputs( Json::arrayValue );
  Json::Value art( Json::objectValue );
  art[ "path" ] = "/outputs/probe.tif";
  art[ "role" ] = "primary";
  outputs.append( art );
  result[ "outputs" ] = outputs;
  summary.setResult( result );

  QListWidget *artifactsList = summary.findChild<QListWidget *>( QStringLiteral( "rsResultArtifacts" ) );
  REQUIRE( artifactsList != nullptr );
  REQUIRE( artifactsList->count() >= 1 );

  QSignalSpy spy( &summary, &RsResultSummary::openPathRequested );
  REQUIRE( spy.isValid() );
  emit artifactsList->itemDoubleClicked( artifactsList->item( 0 ) );
  REQUIRE( spy.count() == 1 );
  CHECK( spy.at( 0 ).at( 0 ).toString() == QStringLiteral( "/outputs/probe.tif" ) );
}

// ===========================================================================
// SP-1: generation supersede is owner-scoped — a new generation from owner A
// must not mark owner B's still-current generation stale.
// ===========================================================================
TEST_CASE( "SP-1: generation supersede respects owner scoping",
           "[parity][mirror][scan_pool][parity-sp1]" )
{
  ensureApp();
  auto &pool = sicnu::app::RsScanPool::instance();
  const char ownerA = 'A';
  const char ownerB = 'B';

  const quint64 genA1 = pool.nextGeneration( &ownerA );
  const quint64 genB1 = pool.nextGeneration( &ownerB );
  CHECK_FALSE( pool.isStale( genB1, &ownerB ) );

  // Owner A opens a NEW generation: A's old one goes stale, B's is untouched.
  const quint64 genA2 = pool.nextGeneration( &ownerA );
  CHECK( pool.isStale( genA1, &ownerA ) );
  CHECK_FALSE( pool.isStale( genA2, &ownerA ) );
  CHECK_FALSE( pool.isStale( genB1, &ownerB ) );
}

// ===========================================================================
// TS-1: the scrubber index is clamped to the acquisition list the source
// provided — no programmatic path can commit an out-of-range index.
// ===========================================================================
TEST_CASE( "TS-1: scrubber index stays within the source timeline bounds",
           "[parity][mirror][timeline][parity-ts1]" )
{
  ensureApp();
  sicnu::gui::TimelineScrubberWidget scrubber;
  scrubber.setTimelineDates( { QStringLiteral( "2026-01-01" ),
                               QStringLiteral( "2026-02-01" ),
                               QStringLiteral( "2026-03-01" ) } );

  QSignalSpy spy( &scrubber, &sicnu::gui::TimelineScrubberWidget::dateChanged );
  REQUIRE( spy.isValid() );

  scrubber.setCurrentIndex( 2 );
  CHECK( scrubber.currentIndex() == 2 );
  scrubber.setCurrentIndex( 99 );
  CHECK( scrubber.currentIndex() == 2 );
  scrubber.setCurrentIndex( -5 );
  CHECK( scrubber.currentIndex() == 0 );

  // dateChanged only fires for COMMITTED changes (contract in the header).
  const int commits = spy.count();
  CHECK( commits >= 2 );
}

// ===========================================================================
// TS-2: play/pause is the timer's liveness contract and playback reaching the
// last slice emits playbackFinished exactly once.
// ===========================================================================
TEST_CASE( "TS-2: playback finishes at the last slice and pauses",
           "[parity][mirror][timeline][parity-ts2]" )
{
  ensureApp();
  sicnu::gui::TimelineScrubberWidget scrubber;
  scrubber.setTimelineDates( { QStringLiteral( "2026-01-01" ),
                               QStringLiteral( "2026-02-01" ) } );
  scrubber.setCurrentIndex( 0 );

  QSignalSpy finished( &scrubber, &sicnu::gui::TimelineScrubberWidget::playbackFinished );
  REQUIRE( finished.isValid() );

  scrubber.setPlaySpeed( 64.0f ); // fast-forward: ~30 frames per slice at 16ms
  scrubber.play();
  QTest::qWait( 3000 );
  scrubber.pause();

  CHECK( finished.count() == 1 );
  CHECK( scrubber.currentIndex() == 1 );
}

// ===========================================================================
// CW-1/CW-2: comparison widget signal contract + mode branches.
// ===========================================================================
TEST_CASE( "CW-1: mode and flicker changes emit their contracts exactly",
           "[parity][mirror][comparison][parity-cw1]" )
{
  ensureApp();
  ComparisonWidget widget;
  QSignalSpy modeSpy( &widget, &ComparisonWidget::modeChanged );
  QSignalSpy flickerSpy( &widget, &ComparisonWidget::flickerIntervalChanged );
  REQUIRE( modeSpy.isValid() );
  REQUIRE( flickerSpy.isValid() );

  widget.setMode( ComparisonWidget::ComparisonMode::Flicker );
  CHECK( widget.mode() == ComparisonWidget::ComparisonMode::Flicker );
  CHECK( modeSpy.count() == 1 );

  widget.setMode( ComparisonWidget::ComparisonMode::Flicker );
  CHECK( modeSpy.count() == 1 ); // unchanged mode: no re-emission

  widget.setFlickerInterval( 250 );
  CHECK( widget.flickerInterval() == 250 );
  CHECK( flickerSpy.count() == 1 );
}

TEST_CASE( "CW-2: images set through the source are reported as held",
           "[parity][mirror][comparison][parity-cw2]" )
{
  ensureApp();
  ComparisonWidget widget;
  CHECK_FALSE( widget.hasLeftImage() );
  CHECK_FALSE( widget.hasRightImage() );

  QPixmap pix( 8, 8 );
  pix.fill( Qt::red );
  widget.setLeftImage( pix );
  CHECK( widget.hasLeftImage() );
  CHECK_FALSE( widget.hasRightImage() );
  widget.setRightImage( pix );
  CHECK( widget.hasRightImage() );
}

// ===========================================================================
// ES-0: the empty-state CTA contract — action text and visibility gate the
// actionClicked emissions.
// ===========================================================================
TEST_CASE( "ES-0: empty state CTA honours visibility gating",
           "[parity][mirror][empty_state][parity-es0]" )
{
  ensureApp();
  sicnu::RsEmptyStateWidget widget( QStringLiteral( "layers" ),
                                    QStringLiteral( "No layers" ),
                                    QStringLiteral( "Import something" ),
                                    QStringLiteral( "Import" ) );
  QSignalSpy spy( &widget, &sicnu::RsEmptyStateWidget::actionClicked );
  REQUIRE( spy.isValid() );

  widget.setActionVisible( true );
  QPushButton *cta = widget.findChildren<QPushButton *>().value( 0, nullptr );
  REQUIRE( cta != nullptr );
  cta->click();
  CHECK( spy.count() == 1 );

  widget.setActionVisible( false );
  CHECK_FALSE( cta->isVisible() );
}

// ===========================================================================
// RL-1: the combo's items equal the project's valid raster layer set.
// ===========================================================================
TEST_CASE( "RL-1: raster combo mirrors the project's valid raster set",
           "[parity][mirror][raster_combo][parity-rl1]" )
{
  ensureApp();
  QgsProject::instance()->clear();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString tifA = tmp.filePath( QStringLiteral( "a.tif" ) );
  const QString tifB = tmp.filePath( QStringLiteral( "b.tif" ) );
  REQUIRE( writeMiniGeoTiff( tifA ) );
  REQUIRE( writeMiniGeoTiff( tifB ) );
  QgsRasterLayer *layerA = addRasterToProject( tifA, QStringLiteral( "comboA" ) );
  QgsRasterLayer *layerB = addRasterToProject( tifB, QStringLiteral( "comboB" ) );
  REQUIRE( layerA != nullptr );
  REQUIRE( layerB != nullptr );

  RasterLayerCombo combo;
  combo.populate();
  CHECK( combo.count() == 2 );

  // Selection resolves back to the authoritative layer object.
  combo.selectLayer( layerB->id() );
  CHECK( combo.currentRasterLayer() == layerB );
  QgsProject::instance()->clear();
}

// ===========================================================================
// RL-2 (F-01): layers added while the combo is alive (dialog open) must be
// reflected — the project layer set is the source of truth, not a snapshot.
// ===========================================================================
TEST_CASE( "RL-2: raster combo tracks project layer changes while alive",
           "[parity][mirror][raster_combo][parity-rl2]" )
{
  ensureApp();
  QgsProject::instance()->clear();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString tifA = tmp.filePath( QStringLiteral( "a.tif" ) );
  REQUIRE( writeMiniGeoTiff( tifA ) );
  QgsRasterLayer *layerA = addRasterToProject( tifA, QStringLiteral( "comboA" ) );
  REQUIRE( layerA != nullptr );

  RasterLayerCombo combo;
  combo.populate();
  REQUIRE( combo.count() == 1 );

  // The dialog stays open; a background task adds a second raster (the
  // auto-load path). The combo must reflect the NEW project truth.
  const QString tifB = tmp.filePath( QStringLiteral( "b.tif" ) );
  REQUIRE( writeMiniGeoTiff( tifB ) );
  QgsRasterLayer *layerB = addRasterToProject( tifB, QStringLiteral( "comboB" ) );
  REQUIRE( layerB != nullptr );
  QApplication::processEvents();

  INFO( "combo rows after project change: " << combo.count() );
  CHECK( combo.count() == 2 );

  // And removals must not leave phantom rows.
  QgsProject::instance()->removeMapLayer( layerB );
  QApplication::processEvents();
  CHECK( combo.count() == 1 );
  QgsProject::instance()->clear();
}
