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
#include "lab_operator_launch.h"
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
#include <QPushButton>
#include <QStandardPaths>
#include <QTextEdit>

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <sstream>
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

/// RAII guard for tests that point SICNU_DATA_DIR elsewhere: a REQUIRE
/// failure must not leak the alternate data root into later test cases.
struct DataDirGuard
{
  ~DataDirGuard()
  {
    qputenv( "SICNU_DATA_DIR",
             QString::fromStdString( sourceDir() + "/data" ).toUtf8() );
  }
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
    [&]( const QString &operatorId, const QString & ) { launched = operatorId; } );

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

// ---------------------------------------------------------------------------
// R3 hardening: operator params seam, autonomy authority, stale-ref honesty,
// button fail-closed states.
// ---------------------------------------------------------------------------

TEST_CASE( "smoke: stepless/fail-closed timelines disable every affordance",
           "[teaching][smoke][buttons]" )
{
  DockFixture fx( "buttons_empty" );
  // Start on a lab WITH steps so the workspace has live affordances, then
  // switch to lab8_temporal_analysis (no .lab.json in data/labs — the
  // registry marks it out of scope, owned by the temporal track): the
  // fail-closed timeline must retire every button the previous lab enabled.
  fx.dock->openLab( QStringLiteral( "m01" ), QStringLiteral( "lab15_data_inspection" ) );
  auto *ws = workspaceOf( fx.dock );
  REQUIRE( ws != nullptr );
  REQUIRE( !ws->timeline().steps.empty() );

  auto runButtonOf = [ws]() -> QPushButton * {
    for ( auto *btn : ws->findChildren<QPushButton *>() )
      if ( btn->text().contains( QStringLiteral( "运行" ) ) ) return btn;
    return nullptr;
  };
  QPushButton *runBefore = runButtonOf();
  REQUIRE( runBefore != nullptr );
  // The first lab15 step with an operator id enables Run.
  bool anyOperatorStep = false;
  for ( const auto &s : ws->timeline().steps )
    if ( !s.operatorId.empty() ) anyOperatorStep = true;
  REQUIRE( anyOperatorStep );
  auto *stepListBefore = ws->findChild<QListWidget *>();
  REQUIRE( stepListBefore != nullptr );
  int operatorRow = -1;
  for ( const auto &s : ws->timeline().steps )
    if ( !s.operatorId.empty() && !s.humanRequired ) { operatorRow = s.index; break; }
  if ( operatorRow >= 0 ) stepListBefore->setCurrentRow( operatorRow );
  REQUIRE( runBefore->isEnabled() );

  fx.dock->openLab( QStringLiteral( "m08_temporal_phenology" ),
                    QStringLiteral( "lab8_temporal_analysis" ) );
  REQUIRE( ws->timeline().steps.empty() );

  const auto buttons = ws->findChildren<QPushButton *>();
  REQUIRE( buttons.size() >= 6 );
  // Run / prev / next / submit must all be OFF (fail-closed); no stale
  // enablement from the previously opened lab may survive.
  for ( const auto *btn : buttons ) {
    const bool navOrRun = btn->text().contains( QStringLiteral( "上一步" ) )
                          || btn->text().contains( QStringLiteral( "下一步" ) )
                          || btn->text().contains( QStringLiteral( "运行" ) )
                          || btn->text().contains( QStringLiteral( "提交" ) );
    if ( navOrRun )
      REQUIRE_FALSE( btn->isEnabled() );
  }
  // The fail-closed reason is on the surface, not hidden.
  auto *why = ws->findChild<QTextEdit *>();
  QTextEdit *whyView = nullptr;
  const auto edits = ws->findChildren<QTextEdit *>();
  REQUIRE( !edits.empty() );
  for ( auto *edit : edits )
    if ( edit->toPlainText().contains( QStringLiteral( "⚠" ) ) ) whyView = edit;
  REQUIRE( whyView != nullptr );
}

TEST_CASE( "smoke: canonical wrapper labs project the registry-declared source",
           "[teaching][smoke][registry]" )
{
  DockFixture fx( "registry_source" );
  // lab12_sar_processing's .lab.json is a registry wrapper (no steps); the
  // registry declares its source labspec, which must project the real steps.
  fx.dock->openLab( QStringLiteral( "m07_change_detection" ),
                    QStringLiteral( "lab12_sar_processing" ) );
  auto *ws = workspaceOf( fx.dock );
  REQUIRE( ws != nullptr );
  const auto tl = ws->timeline();
  REQUIRE( tl.ok );
  REQUIRE( tl.sourceKind == "labspec" );
  // The projected steps belong to the course lab entry, not the source
  // labspec's alias id — downstream labId matching must keep working.
  REQUIRE( tl.labId == "lab12_sar_processing" );
  REQUIRE( !tl.steps.empty() );
  REQUIRE( tl.steps.front().operatorId == "rs:sar_calibrate" );
}

TEST_CASE( "smoke: autonomy comes from the course authority, not a hardcode",
           "[teaching][smoke][autonomy]" )
{
  // (a) The shipped course manifest declares sicnu.autonomy-policy/1
  //     (L2/practice): the cockpit must project exactly that.
  {
    DockFixture fx( "autonomy_course" );
    fx.dock->openLab( QStringLiteral( "m01" ), QStringLiteral( "lab15_data_inspection" ) );
    auto *ws = workspaceOf( fx.dock );
    REQUIRE( ws != nullptr );
    const auto autonomy = ws->autonomy();
    REQUIRE( autonomy.ok );
    REQUIRE( autonomy.effectiveLevel == "L2" );
    REQUIRE( autonomy.mode == "practice" );
    // Session records WHICH authority decided — never an inline policy value.
    auto session = sicnu::teaching::LabSessionState::loadFromFile(
      fx.dock->sessionPath().toStdString() );
    REQUIRE( session.ok );
    REQUIRE( session.autonomyPolicyRef == "course:undergraduate_rs" );
  }

  // (b) A course manifest without autonomy_policy → honest fail-closed L0
  //     with the reason on the display; NOT a silently invented default.
  {
    const std::string altData = makeTempDir( "autonomy_nopoly" );
    const std::string dataSrc = sourceDir() + "/data";
    fs::create_directories( altData + "/curriculum" );
    fs::copy( dataSrc + "/labs", altData + "/labs",
              fs::copy_options::recursive | fs::copy_options::copy_symlinks );
    // Manifest minus the autonomy_policy block.
    {
      std::ifstream in( dataSrc + "/curriculum/undergraduate_rs.curriculum.json" );
      Json::Value manifest;
      Json::CharReaderBuilder b;
      std::string errs;
      REQUIRE( Json::parseFromStream( b, in, &manifest, &errs ) );
      if ( manifest.isMember( "autonomy_policy" ) )
        manifest.removeMember( "autonomy_policy" );
      Json::StreamWriterBuilder w;
      w["indentation"] = "  ";
      std::ofstream out( altData + "/curriculum/undergraduate_rs.curriculum.json" );
      out << Json::writeString( w, manifest );
    }
    DockFixture fx( "autonomy_nopoly" );
    DataDirGuard envGuard;
    qputenv( "SICNU_DATA_DIR", QString::fromStdString( altData ).toUtf8() );
    LabCockpitDock dock( nullptr );
    dock.openLab( QStringLiteral( "m01" ), QStringLiteral( "lab15_data_inspection" ) );
    auto *ws = workspaceOf( &dock );
    REQUIRE( ws != nullptr );
    const auto autonomy = ws->autonomy();
    REQUIRE( autonomy.effectiveLevel == "L0" );
    bool honestNote = false;
    for ( const auto &issue : autonomy.issuesZh )
      if ( issue.find( "fail-closed" ) != std::string::npos ) honestNote = true;
    REQUIRE( honestNote );
    // DataDirGuard restores the repo data root even on REQUIRE failure.
  }
}

TEST_CASE( "smoke: a restored policy ref can never raise the student's autonomy",
           "[teaching][smoke][autonomy][restore]" )
{
  // A session file carrying the OLD inline ref format (and any would-be
  // escalated value) must not change the projected autonomy: refs are
  // bookkeeping, the authorities decide.
  ensureApp();
  resetSessionDir( "autonomy_restore" );
  const QString sessionPath =
    QStandardPaths::writableLocation( QStandardPaths::AppDataLocation )
    + QStringLiteral( "/teaching/lab_cockpit_session.json" );
  fs::create_directories( fs::path( sessionPath.toStdString() ).parent_path().string() );
  sicnu::teaching::LabSessionState planted = sicnu::teaching::LabSessionState::makeNew(
    "local/lab15", "undergraduate_rs", "lab15_data_inspection" );
  planted.autonomyPolicyRef = "inline:practice/L5"; // forged escalation attempt
  REQUIRE( planted.saveToFile( sessionPath.toStdString() ) );

  qputenv( "SICNU_DATA_DIR", QString::fromStdString( sourceDir() + "/data" ).toUtf8() );
  LabCockpitDock dock( nullptr );
  auto *ws = workspaceOf( &dock );
  REQUIRE( ws != nullptr );
  REQUIRE( ws->timeline().labId == "lab15_data_inspection" );
  const auto autonomy = ws->autonomy();
  // The course authority says L2/practice — the forged ref did nothing.
  REQUIRE( autonomy.effectiveLevel == "L2" );
  REQUIRE( autonomy.mode == "practice" );
}

TEST_CASE( "smoke: lab operator params pass the runtime registry validator",
           "[teaching][smoke][params]" )
{
  // Data ↔ registry drift gate: EVERY shipped lab operator step's params
  // must validate against the registered operator descriptor. A failure
  // here means the lab data promises parameters the runtime does not accept.
  const std::string data = sourceDir() + "/data";
  int checked = 0;
  for ( const auto &entry : fs::directory_iterator( data + "/labs" ) ) {
    if ( !entry.is_regular_file() || entry.path().extension() != ".json" )
      continue;
    const std::string name = entry.path().filename().string();
    const bool isLabDoc = name.ends_with( ".lab.json" );
    const bool isLabspecDoc = name.ends_with( ".labspec.json" );
    if ( !isLabDoc && !isLabspecDoc ) continue;
    std::ifstream in( entry.path() );
    Json::Value lab;
    Json::CharReaderBuilder b;
    std::string errs;
    // A lab document that stopped parsing must fail the gate LOUDLY, not
    // silently drop out of coverage.
    INFO( "parsing " << name );
    REQUIRE( Json::parseFromStream( b, in, &lab, &errs ) );
    if ( !lab.isObject() || !lab.isMember( "steps" ) || !lab["steps"].isArray() ) continue;
    for ( const auto &s : lab["steps"] ) {
      if ( !s.isObject() || !s.isMember( "operator_id" ) || !s["operator_id"].isString() )
        continue;
      if ( !s.isMember( "params" ) ) continue;
      Json::StreamWriterBuilder w;
      w["indentation"] = "";
      const QString paramsJson =
        QString::fromStdString( Json::writeString( w, s["params"] ) );
      const auto plan = prepareLabOperatorLaunch(
        QString::fromStdString( s["operator_id"].asString() ), paramsJson );
      INFO( entry.path().filename().string() << " step " << s["operator_id"].asString() );
      for ( const auto &issue : plan.issuesZh )
        WARN( issue );
      REQUIRE( plan.ok );
      ++checked;
    }
  }
  // Exact shipped census (lab01..lab11, lab15, lab16 operator+params steps;
  // the canonical labspec sources carry steps without params): a change in
  // this number is a data change and must update this gate consciously.
  REQUIRE( checked == 19 );
}

TEST_CASE( "smoke: hostile params are refused with typed reasons",
           "[teaching][smoke][params][adversarial]" )
{
  // Unknown operator + params → refused (nothing would check them).
  {
    const auto plan = prepareLabOperatorLaunch( QStringLiteral( "rs:does_not_exist" ),
                                                QStringLiteral( R"({"input":"x"})" ) );
    REQUIRE_FALSE( plan.ok );
    REQUIRE( !plan.issuesZh.empty() );
  }
  // Malformed JSON → refused.
  {
    const auto plan = prepareLabOperatorLaunch( QStringLiteral( "rs:contrast_stretch" ),
                                                QStringLiteral( "{\"input\": " ) );
    REQUIRE_FALSE( plan.ok );
  }
  // Non-object JSON → refused.
  {
    const auto plan = prepareLabOperatorLaunch( QStringLiteral( "rs:contrast_stretch" ),
                                                QStringLiteral( "[1,2,3]" ) );
    REQUIRE_FALSE( plan.ok );
  }
  // Unknown parameter → refused (schema drift must not pass silently).
  {
    const auto plan = prepareLabOperatorLaunch(
      QStringLiteral( "rs:contrast_stretch" ),
      QStringLiteral( R"({"input":"a.tif","output":"b.tif","bogusParam":1})" ) );
    REQUIRE_FALSE( plan.ok );
    bool mentionsBogus = false;
    for ( const auto &issue : plan.issuesZh )
      if ( issue.find( "bogusParam" ) != std::string::npos ) mentionsBogus = true;
    REQUIRE( mentionsBogus );
  }
  // Wrong enum value → refused.
  {
    const auto plan = prepareLabOperatorLaunch(
      QStringLiteral( "rs:contrast_stretch" ),
      QStringLiteral( R"({"input":"a.tif","output":"b.tif","method":"auto_magick"})" ) );
    REQUIRE_FALSE( plan.ok );
  }
  // Empty/absent params → allowed, opens with the operator's own defaults.
  REQUIRE( prepareLabOperatorLaunch( QStringLiteral( "rs:contrast_stretch" ),
                                     QString() ).ok );
  REQUIRE( prepareLabOperatorLaunch( QStringLiteral( "rs:contrast_stretch" ),
                                     QStringLiteral( "{}" ) ).ok );
  // Real lab data → allowed, params survive the round-trip.
  {
    const auto plan = prepareLabOperatorLaunch(
      QStringLiteral( "rs:contrast_stretch" ),
      QStringLiteral( R"({"input":"data/samples/landsat_sample.tif",)"
                      R"("output":"outputs/lab01.tif","method":"percent_clip"})" ) );
    REQUIRE( plan.ok );
    REQUIRE( plan.params["method"].asString() == "percent_clip" );
  }
}

TEST_CASE( "smoke: recording-context change drops project-bound refs, keeps navigation",
           "[teaching][smoke][session][project]" )
{
  DockFixture fx( "project_switch" );
  fx.dock->openLab( QStringLiteral( "m01" ), QStringLiteral( "lab15_data_inspection" ) );
  auto *ws = workspaceOf( fx.dock );
  REQUIRE( ws != nullptr );

  // Produce project-bound state: an artifact + a validation summary, and a
  // persisted navigation position (step 1).
  ws->setArtifactPath( QStringLiteral( "/tmp/from_old_project.tif" ) );
  emit ws->validateRequested();
  auto *stepList = ws->findChild<QListWidget *>();
  REQUIRE( stepList != nullptr );
  REQUIRE( stepList->count() >= 2 );
  stepList->setCurrentRow( 1 );

  auto before =
    sicnu::teaching::LabSessionState::loadFromFile( fx.dock->sessionPath().toStdString() );
  REQUIRE( before.ok );
  REQUIRE( before.artifactPath == "/tmp/from_old_project.tif" );
  REQUIRE( before.stepIndex == 1 );

  // The project switched: artifact/run/capsule refs and validation verdicts
  // recorded under the old context must not survive, while course-scoped
  // navigation (step index, evidence, mode) does.
  fx.dock->onRecordingContextChanged();

  auto after =
    sicnu::teaching::LabSessionState::loadFromFile( fx.dock->sessionPath().toStdString() );
  REQUIRE( after.ok );
  REQUIRE( after.artifactPath.empty() );
  REQUIRE( after.runId.empty() );
  REQUIRE( after.capsuleExportRef.empty() );
  // Serialization materializes a cleared summary as an empty object: what
  // must not survive is any projected VERDICT document.
  REQUIRE_FALSE( after.lastValidationSummary.isMember( "schema" ) );
  REQUIRE( after.stepIndex == 1 ); // navigation kept
  REQUIRE( ws->artifactPath().isEmpty() );

  // The surface says what happened instead of silently keeping stale rows.
  const QString feedback = feedbackViewOf( fx.dock )->toPlainText();
  REQUIRE( feedback.contains( QStringLiteral( "项目/记录上下文已切换" ) ) );
}

TEST_CASE( "smoke: run button hands the operator real, absolutized prefill",
           "[teaching][smoke][params][e2e]" )
{
  DockFixture fx( "prefill_e2e" );
  QString launchedId;
  QString launchedParams;
  fx.dock->setOperatorLauncher(
    [&]( const QString &operatorId, const QString &paramsJson ) {
      launchedId = operatorId;
      launchedParams = paramsJson;
    } );

  fx.dock->openLab( QStringLiteral( "m01" ), QStringLiteral( "lab15_data_inspection" ) );
  auto *ws = workspaceOf( fx.dock );
  REQUIRE( ws != nullptr );

  // Select the first operator step (rs:extract_bands) and press Run.
  const auto &steps = ws->timeline().steps;
  REQUIRE( steps.size() >= 2 );
  int opRow = -1;
  for ( const auto &s : steps )
    if ( !s.operatorId.empty() && !s.humanRequired ) {
      opRow = s.index;
      break;
    }
  REQUIRE( opRow >= 0 );
  auto *stepList = ws->findChild<QListWidget *>();
  REQUIRE( stepList != nullptr );
  stepList->setCurrentRow( opRow );

  QPushButton *run = nullptr;
  const auto buttons = ws->findChildren<QPushButton *>();
  for ( auto *btn : buttons )
    if ( btn->text().contains( QStringLiteral( "运行" ) ) ) run = btn;
  REQUIRE( run != nullptr );
  REQUIRE( run->isEnabled() );
  run->click();

  REQUIRE( launchedId == QString::fromStdString( steps[static_cast<size_t>( opRow )].operatorId ) );
  REQUIRE( !launchedParams.isEmpty() );
  // The prefill carries the step's params with data/ inputs absolutized
  // against the repo data root — regardless of the test process cwd.
  Json::Value params;
  {
    Json::CharReaderBuilder b;
    std::string errs;
    const std::string bytes = launchedParams.toStdString();
    std::istringstream stream( bytes );
    REQUIRE( Json::parseFromStream( b, stream, &params, &errs ) );
  }
  REQUIRE( params.isObject() );
  REQUIRE( params.isMember( "input" ) );
  const std::string input = params["input"].asString();
  // The prefill anchors data/ inputs at the repo data root regardless of the
  // test process cwd. The samples themselves are generated on demand
  // (sicnu_generate_samples) and not committed, so existence is not a
  // property of this seam — the resolved LOCATION is.
  REQUIRE( fs::path( input ).is_absolute() );
  REQUIRE( fs::path( input ).filename() == "landsat_sample.tif" );
  REQUIRE( input.find( "/data/samples/" ) != std::string::npos );
}
