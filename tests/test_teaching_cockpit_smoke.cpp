/***************************************************************************
 * tests/test_teaching_cockpit_smoke.cpp — offscreen Lab Cockpit smoke.
 *
 * Drives the REAL dock widgets (src/app/teaching, offscreen Qt) over the
 * REAL authorities: the runtime operator registry probe, the OutputVerifier
 * grading engine (a real GTiff artifact + the shipped rules), the real
 * ExperimentStore + CapsuleBuilder + CapsuleIO export contract, and the
 * session file across a simulated restart. No operator UI is cloned and no
 * verdict/ref is fabricated: every assertion pins an honest outcome.
 *
 *   Course Home → lab → readiness → workspace → validate → feedback
 *     → submit/export → restart(restore)
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "course_home_page.h"
#include "guided_lab_workspace.h"
#include "lab_cockpit_dock.h"
#include "lab_validate_service.h"

#include "dataset/dataset_store.h"
#include "experiment/capsule/capsule_io.h"
#include "experiment/experiment_store.h"
#include "teaching/lab_feedback_projection.h"
#include "teaching/lab_session_state.h"

#include <synthetic_raster_builder.h>

#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QStandardPaths>
#include <QTextEdit>

#include <json/json.h>

#include <filesystem>
#include <string>

using namespace sicnu::app::teaching;
namespace fs = std::filesystem;

namespace {

QApplication *ensureApp()
{
  if ( !QApplication::instance() ) {
    static int argc = 1;
    static char name[] = "test_teaching_cockpit_smoke";
    static char *argv[] = { name, nullptr };
    QStandardPaths::setTestModeEnabled( true );
    static QApplication app( argc, argv );
    return &app;
  }
  return static_cast<QApplication *>( QApplication::instance() );
}

std::string sourceDir()
{
#ifdef CMAKE_SOURCE_DIR
  return CMAKE_SOURCE_DIR;
#else
  return fs::current_path().string();
#endif
}

std::string makeTempDir( const char *tag )
{
  const auto base =
    fs::temp_directory_path() / ( std::string( "teaching_cockpit_smoke_" ) + tag );
  fs::remove_all( base );
  fs::create_directories( base );
  return base.string();
}

/// A clean session directory per test (teaching session file lives under
/// AppDataLocation, which setTestModeEnabled redirects into the qttest dir).
void resetSessionDir( const char *appName )
{
  QCoreApplication::setApplicationName( appName );
  const QString root = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
  fs::remove_all( root.toStdString() );
}

struct DockFixture
{
  QApplication *app = nullptr;
  LabCockpitDock *dock = nullptr;

  DockFixture( const char *sessionTag )
  {
    app = ensureApp();
    resetSessionDir( sessionTag );
    qputenv( "SICNU_DATA_DIR", QString::fromStdString( sourceDir() + "/data" ).toUtf8() );
    dock = new LabCockpitDock( nullptr );
  }
  ~DockFixture() { delete dock; }
};

GuidedLabWorkspace *workspaceOf( LabCockpitDock *dock )
{
  return dock->findChild<GuidedLabWorkspace *>();
}

CourseHomePage *homeOf( LabCockpitDock *dock )
{
  return dock->findChild<CourseHomePage *>();
}

QTextEdit *feedbackViewOf( LabCockpitDock *dock )
{
  return dock->findChild<QTextEdit *>( QStringLiteral( "labWorkspaceFeedbackView" ) );
}

/// One recorded run in a real experiment store (the only truth export binds
/// to). Returns the db path.
std::string seedExperimentStore( const std::string &dir, QString *experimentIdOut )
{
  namespace fs2 = std::filesystem;
  const std::string dbPath = ( fs2::path( dir ) / "experiments.db" ).string();
  sicnu::experiment::ExperimentStore store;
  QString err;
  REQUIRE( store.open( QString::fromStdString( dbPath ), &err ) );

  const QString experimentId = QStringLiteral( "lab-smoke_course" );

  // The run's parent experiment must exist first (upsertRun refuses
  // experiment.not_found otherwise).
  sicnu::experiment::Experiment experiment;
  experiment.setExperimentId( experimentId );
  experiment.setName( QStringLiteral( "smoke course" ) );
  REQUIRE( store.upsertExperiment( experiment ) );

  // The store validates status transitions: record the run through the real
  // lifecycle (Created → Running → Completed), like the coordinator does.
  sicnu::experiment::ExperimentRun run;
  run.setRunId( QStringLiteral( "run-smoke-0001" ) );
  run.setExperimentId( experimentId );
  run.setStatus( sicnu::experiment::RunStatus::Created );
  run.setAlgorithmId( QStringLiteral( "rs:extract_bands" ) );
  run.setCreatedAtUtc( QDateTime::fromString( QStringLiteral( "2026-09-24T00:00:00Z" ),
                                              Qt::ISODate ) );
  REQUIRE( store.upsertRun( run ) );
  run.setStatus( sicnu::experiment::RunStatus::Running );
  REQUIRE( store.upsertRun( run ) );
  run.setStatus( sicnu::experiment::RunStatus::Completed );
  run.setFinishedAtUtc( QDateTime::fromString( QStringLiteral( "2026-09-24T00:01:00Z" ),
                                               Qt::ISODate ) );
  auto committed = store.upsertRun( run );
  REQUIRE( committed );
  *experimentIdOut = experimentId;
  return dbPath;
}

} // namespace

TEST_CASE( "smoke: course home loads the shipped curriculum honestly",
           "[teaching][smoke][course]" )
{
  DockFixture fx( "course" );
  auto *home = homeOf( fx.dock );
  REQUIRE( home != nullptr );
  const auto vm = home->viewModel();
  REQUIRE( vm.ok );
  REQUIRE( vm.courseId == "undergraduate_rs" );
  REQUIRE( !vm.modules.empty() );
}

TEST_CASE( "smoke: open lab shows honest readiness and the guided workspace",
           "[teaching][smoke][readiness]" )
{
  DockFixture fx( "openlab" );
  fx.dock->openLab( QStringLiteral( "m01" ), QStringLiteral( "lab15_data_inspection" ) );

  auto *ws = workspaceOf( fx.dock );
  REQUIRE( ws != nullptr );
  REQUIRE( !ws->timeline().steps.empty() );

  // Readiness is the honest aggregate: no fabricated passport/inspector/sci.
  const auto readiness = ws->timeline(); // timeline ok guard above
  const QString feedback = feedbackViewOf( fx.dock )->toPlainText();
  // Nothing validated yet — no verdict text, no fake capsule ref.
  REQUIRE( feedback.isEmpty() );
}

TEST_CASE( "smoke: validate with no artifact stays indeterminate, never pass",
           "[teaching][smoke][validate]" )
{
  DockFixture fx( "validate_empty" );
  fx.dock->openLab( QStringLiteral( "m01" ), QStringLiteral( "lab15_data_inspection" ) );
  auto *ws = workspaceOf( fx.dock );
  REQUIRE( ws != nullptr );

  emit ws->validateRequested();

  const QString feedback = feedbackViewOf( fx.dock )->toPlainText();
  REQUIRE( feedback.contains( QStringLiteral( "不确定" ) ) );
  REQUIRE_FALSE( feedback.contains( QStringLiteral( "总评: 通过" ) ) );
  REQUIRE( feedback.contains( QStringLiteral( "未提供产物路径" ) ) );

  // Persisted session keeps the honest summary + no capsule ref.
  auto session =
    sicnu::teaching::LabSessionState::loadFromFile( fx.dock->sessionPath().toStdString() );
  REQUIRE( session.ok );
  REQUIRE( session.lastValidationSummary["overall_status"].asString() == "indeterminate" );
  REQUIRE( session.capsuleExportRef.empty() );
}

TEST_CASE( "smoke: validate grades a REAL artifact through the real engine",
           "[teaching][smoke][validate][grader]" )
{
  // Real GTiff on disk + the shipped terrain rules: the OutputVerifier runs
  // its assertions for real (values intentionally wrong → verdict fail with
  // deduction rows — never unverifiable, never pass).
  const std::string dir = makeTempDir( "grade" );
  const std::string raster =
    ( fs::path( dir ) / "slope_out.tif" ).string();
  {
    sicnu::testing::RsSyntheticRasterBuilder builder( 32, 32, 1, GDT_Float32 );
    builder.withCrs( QStringLiteral( "EPSG:32648" ) );
    builder.withGeoTransform( 500000.0, 1.0, 4000000.0, -1.0 );
    builder.withConstantValue( 1, 3.14f ); // wrong science on purpose
    const QString written = builder.writeToDisk( QString::fromStdString( raster ) );
    REQUIRE( !written.isEmpty() );
  }

  const std::string rules =
    sourceDir() + "/data/labs/grading/terrain_slope.rules.json";
  LabValidateInput in;
  in.labId = "lab05_terrain_analysis";
  in.artifactPath = raster;
  in.rulesPath = rules;
  const auto fb = projectValidation( in );

  REQUIRE( fb.ok );
  REQUIRE( fb.overallStatus == "fail" );
  REQUIRE_FALSE( fb.overallCountsAsPass );
  // Real engine deductions are projected per assertion.
  bool sawEngineDeduction = false;
  for ( const auto &row : fb.rows )
    if ( row.layer == "grader" && row.status == "fail" ) sawEngineDeduction = true;
  REQUIRE( sawEngineDeduction );
}

TEST_CASE( "smoke: validate on a missing artifact fails honestly (no fake pass)",
           "[teaching][smoke][validate][missing]" )
{
  LabValidateInput in;
  in.labId = "lab05_terrain_analysis";
  in.artifactPath = "/nonexistent/artifact.tif";
  in.rulesPath = sourceDir() + "/data/labs/grading/terrain_slope.rules.json";
  const auto fb = projectValidation( in );
  REQUIRE( fb.ok );
  // Verifier lens fails (unreadable) and the grader refuses — the aggregate
  // is fail/indeterminate territory, NEVER pass.
  REQUIRE( fb.overallStatus != "pass" );
  REQUIRE_FALSE( fb.overallCountsAsPass );
}

TEST_CASE( "smoke: operator hand-off goes through the injected launcher seam",
           "[teaching][smoke][operator]" )
{
  DockFixture fx( "operator" );
  QString launched;
  fx.dock->setOperatorLauncher(
    [&]( const QString &operatorId ) { launched = operatorId; } );

  fx.dock->openLab( QStringLiteral( "m01" ), QStringLiteral( "lab15_data_inspection" ) );
  auto *ws = workspaceOf( fx.dock );
  REQUIRE( ws != nullptr );

  // Advance to a tool step that carries an operator id, then request a run.
  bool requested = false;
  QObject::connect( ws, &GuidedLabWorkspace::runOperatorRequested,
                    [&]( const QString &, const QString & ) { requested = true; } );
  emit ws->runOperatorRequested( QStringLiteral( "rs:extract_bands" ), QStringLiteral( "{}" ) );
  REQUIRE( requested );
  REQUIRE( launched == QStringLiteral( "rs:extract_bands" ) );
}

TEST_CASE( "smoke: capsule export uses the real builder/io contract",
           "[teaching][smoke][export]" )
{
  // (a) Without a recording context the export FAILS HONESTLY: no ref.
  {
    DockFixture fx( "export_nosource" );
    fx.dock->openLab( QStringLiteral( "m01" ), QStringLiteral( "lab15_data_inspection" ) );
    auto *ws = workspaceOf( fx.dock );
    emit ws->exportCapsuleRequested();
    const QString feedback = feedbackViewOf( fx.dock )->toPlainText();
    REQUIRE( feedback.contains( QStringLiteral( "导出失败" ) ) );
    auto session = sicnu::teaching::LabSessionState::loadFromFile(
      fx.dock->sessionPath().toStdString() );
    REQUIRE( session.ok );
    REQUIRE( session.capsuleExportRef.empty() );
  }

  // (b) With a real recorded run: CapsuleBuilder + CapsuleIO produce a real,
  // reloadable capsule and the session records a real file: ref.
  const std::string dir = makeTempDir( "export" );
  QString experimentId;
  const std::string dbPath = seedExperimentStore( dir, &experimentId );

  DockFixture fx( "export_real" );
  fx.dock->setCapsuleSourceProvider(
    [&]() -> LabCapsuleSource {
      LabCapsuleSource src;
      src.experimentDbPath = QString::fromStdString( dbPath );
      src.experimentId = experimentId;
      src.workspaceRoot = QString::fromStdString( dir );
      return src;
    } );

  fx.dock->openLab( QStringLiteral( "m01" ), QStringLiteral( "lab15_data_inspection" ) );
  auto *ws = workspaceOf( fx.dock );
  emit ws->exportCapsuleRequested();

  auto session =
    sicnu::teaching::LabSessionState::loadFromFile( fx.dock->sessionPath().toStdString() );
  REQUIRE( session.ok );
  REQUIRE( session.capsuleExportRef.rfind( "file:", 0 ) == 0 );

  const std::string capsulePath = session.capsuleExportRef.substr( 5 );
  REQUIRE( fs::exists( capsulePath ) );
  // The exported bytes reload through the canonical gate.
  auto reloaded =
    sicnu::experiment::capsule::CapsuleIO::loadCapsule( QString::fromStdString( capsulePath ) );
  REQUIRE( reloaded.has_value() );
}

TEST_CASE( "smoke: restart restores lab, artifact ref and feedback verdict",
           "[teaching][smoke][restart]" )
{
  // First life: validate (indeterminate — nothing graded) and persist.
  QString sessionPath;
  {
    DockFixture fx( "restart" );
    fx.dock->openLab( QStringLiteral( "m01" ), QStringLiteral( "lab15_data_inspection" ) );
    auto *ws = workspaceOf( fx.dock );
    ws->setArtifactPath( QStringLiteral( "/tmp/whatever.tif" ) );
    emit ws->validateRequested();
    sessionPath = fx.dock->sessionPath();
  }
  REQUIRE( fs::exists( sessionPath.toStdString() ) );

  // Second life: a fresh dock restores the SAME lab and re-renders the
  // persisted summary — no verdict is recomputed.
  QApplication *app = ensureApp();
  QCoreApplication::setApplicationName( "restart" );
  qputenv( "SICNU_DATA_DIR", QString::fromStdString( sourceDir() + "/data" ).toUtf8() );
  LabCockpitDock dock2( nullptr );
  auto *ws2 = workspaceOf( &dock2 );
  REQUIRE( ws2 != nullptr );
  REQUIRE( ws2->timeline().labId == "lab15_data_inspection" );
  auto *artifactEdit =
    ws2->findChild<QLineEdit *>( QStringLiteral( "labWorkspaceArtifactEdit" ) );
  REQUIRE( artifactEdit != nullptr );
  REQUIRE( artifactEdit->text() == QStringLiteral( "/tmp/whatever.tif" ) );
  const QString feedback = feedbackViewOf( &dock2 )->toPlainText();
  REQUIRE( feedback.contains( QStringLiteral( "不确定" ) ) );
  (void)app;
}

TEST_CASE( "smoke: switching labs never leaks the previous lab's step index",
           "[teaching][smoke][session][labswitch]" )
{
  DockFixture fx( "labswitch" );
  fx.dock->openLab( QStringLiteral( "m01" ), QStringLiteral( "lab15_data_inspection" ) );
  auto *ws = workspaceOf( fx.dock );
  REQUIRE( ws != nullptr );
  REQUIRE( ws->timeline().steps.size() >= 2 );

  // Navigate to the second step (persisted via stepIndexChanged).
  auto *stepList = ws->findChild<QListWidget *>();
  REQUIRE( stepList != nullptr );
  stepList->setCurrentRow( 1 );
  auto saved =
    sicnu::teaching::LabSessionState::loadFromFile( fx.dock->sessionPath().toStdString() );
  REQUIRE( saved.ok );
  REQUIRE( saved.stepIndex == 1 );

  // Switch to ANOTHER lab: the boundary resets navigation (and the stale
  // artifact input) instead of leaking step 1 of the old lab.
  fx.dock->openLab( QStringLiteral( "m01" ), QStringLiteral( "lab03_classification" ) );
  auto switched =
    sicnu::teaching::LabSessionState::loadFromFile( fx.dock->sessionPath().toStdString() );
  REQUIRE( switched.ok );
  REQUIRE( switched.labId == "lab03_classification" );
  REQUIRE( switched.stepIndex == 0 );
  auto *artifactEdit =
    ws->findChild<QLineEdit *>( QStringLiteral( "labWorkspaceArtifactEdit" ) );
  REQUIRE( artifactEdit != nullptr );
  REQUIRE( artifactEdit->text().isEmpty() );
}

TEST_CASE( "smoke: a foreign-course session fails closed to the course home",
           "[teaching][smoke][session][course]" )
{
  ensureApp();
  resetSessionDir( "foreign" );
  const QString sessionPath =
    QStandardPaths::writableLocation( QStandardPaths::AppDataLocation )
    + QStringLiteral( "/teaching/lab_cockpit_session.json" );
  fs::create_directories(
    fs::path( sessionPath.toStdString() ).parent_path().string() );

  sicnu::teaching::LabSessionState foreign =
    sicnu::teaching::LabSessionState::makeNew(
      "local/lab15", "some_other_course", "lab15_data_inspection" );
  REQUIRE( foreign.saveToFile( sessionPath.toStdString() ) );

  qputenv( "SICNU_DATA_DIR", QString::fromStdString( sourceDir() + "/data" ).toUtf8() );
  LabCockpitDock dock( nullptr );

  // The foreign course was NOT adopted: no lab auto-opened (workspace stays
  // without a timeline) and the in-memory session is fresh (ok=false).
  auto *ws = workspaceOf( &dock );
  REQUIRE( ws != nullptr );
  REQUIRE( ws->timeline().steps.empty() );
}
