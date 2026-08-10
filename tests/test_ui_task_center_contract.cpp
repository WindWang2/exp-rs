// Regression coverage for #35: no UI path bypasses Task Center; panel is a projection.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "processing/framework/task_center.h"
#include "app/dialogs/async_algorithm_runner.h"
#include "qgstaskmanager.h"
#include <processing/qgsprocessingcontext.h>

#include <chrono>
#include <thread>

namespace {

int fake_argc = 1;
char fake_argv0[] = "test_ui_task_center_contract";
char *fake_argv[] = { fake_argv0, nullptr };

QCoreApplication *ensureApp()
{
  static QCoreApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QCoreApplication( fake_argc, fake_argv );
  return QCoreApplication::instance();
}

QString readSource( const QString &relativePath )
{
  // Prefer CMAKE_SOURCE_DIR when available via environment of ctest cwd.
  const QStringList candidates = {
    QStringLiteral( "%1/%2" ).arg( QStringLiteral( CMAKE_SOURCE_DIR ), relativePath ),
    QStringLiteral( "../%1" ).arg( relativePath ),
    QStringLiteral( "../../%1" ).arg( relativePath ),
    relativePath,
  };
  for ( const QString &path : candidates )
  {
    QFile f( path );
    if ( f.open( QIODevice::ReadOnly | QIODevice::Text ) )
      return QString::fromUtf8( f.readAll() );
  }
  return {};
}

void requireNoDirectJobEngineSubmit( const QString &relativePath )
{
  const QString text = readSource( relativePath );
  REQUIRE_FALSE( text.isEmpty() );
  REQUIRE_FALSE( text.contains( QStringLiteral( "JobEngine::instance().submit" ) ) );
  REQUIRE_FALSE( text.contains( QStringLiteral( "JobEngine::instance().cancel" ) ) );
}

void requireUsesTaskCenterSubmit( const QString &relativePath )
{
  const QString text = readSource( relativePath );
  REQUIRE_FALSE( text.isEmpty() );
  const bool usesTaskCenter = text.contains( QStringLiteral( "TaskCenter::instance().submitJob" ) )
                           || text.contains( QStringLiteral( "TaskCenter::instance().submitPipeline" ) )
                           || text.contains( QStringLiteral( "JobHandle.submitJob" ) )
                           || text.contains( QStringLiteral( "jobHandle.submitJob" ) );
  REQUIRE( usesTaskCenter );
}

} // namespace

TEST_CASE( "UI contract inventory: no direct JobEngine submit in migrated callers",
           "[task_center][contract][inventory]" )
{
  ensureApp();

  const QStringList submitCallers = {
    QStringLiteral( "src/app/panels/mosaic_panel.cpp" ),
    QStringLiteral( "src/app/dialogs/sicnu_algorithm_dialog.cpp" ),
    QStringLiteral( "src/app/shell/rs_job_runner.cpp" ),
    QStringLiteral( "src/app/dialogs/raster_processing_dialog_base.cpp" ),
    QStringLiteral( "src/app/obia/rs_obia_main_window.cpp" ),
    QStringLiteral( "src/app/classification/qgsclassificationmainwindow.cpp" ),
    QStringLiteral( "src/app/georeferencer/rs_georeferencing_session.cpp" ),
    QStringLiteral( "src/app/georeferencer/qgsgeoreferencermainwindow.cpp" ),
    QStringLiteral( "src/app/shell/workflow_session_controller.cpp" ),
  };

  for ( const QString &path : submitCallers )
  {
    INFO( path.toStdString() );
    requireNoDirectJobEngineSubmit( path );
    requireUsesTaskCenterSubmit( path );
  }
}

TEST_CASE( "UI contract inventory: RsJobPanel projects Task Center lifecycle",
           "[task_center][contract][inventory]" )
{
  ensureApp();

  const QString panel = readSource( QStringLiteral( "src/app/shell/rs_job_panel.cpp" ) );
  REQUIRE_FALSE( panel.isEmpty() );

  // Must not own JobEngine list/cancel/snapshot lifecycle.
  REQUIRE_FALSE( panel.contains( QStringLiteral( "JobEngine::instance().submit" ) ) );
  REQUIRE_FALSE( panel.contains( QStringLiteral( "JobEngine::instance().cancel" ) ) );
  REQUIRE_FALSE( panel.contains( QStringLiteral( "JobEngine::instance().list" ) ) );
  REQUIRE_FALSE( panel.contains( QStringLiteral( "JobEngine::instance().snapshot" ) ) );

  // Projection sources.
  REQUIRE( panel.contains( QStringLiteral( "TaskCenter::instance()" ) ) );
  REQUIRE( panel.contains( QStringLiteral( "cancelTask" ) ) );
  REQUIRE( panel.contains( QStringLiteral( "allTasks" ) ) );
  REQUIRE( panel.contains( QStringLiteral( "clearCompletedTasks" ) ) );
}

TEST_CASE( "UI contract: callable submit creates one Task Center-owned task",
           "[task_center][contract]" )
{
  ensureApp();
  auto &engine = sicnu::jobs::JobEngine::instance();
  engine.shutdownForTests();

  const auto before = sicnu::TaskCenter::instance().allTasks().size();

  sicnu::jobs::JobRequest req;
  req.algorithmId = "callable:contract-35";
  req.title = "Contract tracer";
  req.source = "dialog";

  const long taskId = sicnu::TaskCenter::instance().submitJob(
    req,
    []( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
      Json::Value out( Json::objectValue );
      out["output"] = "/tmp/contract-35-output.tif";
      return out;
    } );

  REQUIRE( taskId > 0 );
  REQUIRE( sicnu::TaskCenter::instance().allTasks().size() == before + 1 );

  engine.waitUntilIdleForTests();
  for ( int attempt = 0; attempt < 40
        && sicnu::TaskCenter::instance().getTaskInfo( taskId ).status == sicnu::TaskStatus::Running;
        ++attempt )
  {
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  }

  const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
  REQUIRE( info.status == sicnu::TaskStatus::Completed );
  REQUIRE( info.resultPayload["output"].asString() == "/tmp/contract-35-output.tif" );
  REQUIRE_FALSE( info.jobId.empty() );

  // JobEngine remains internal adapter; UI should observe via Task Center only.
  const auto job = engine.snapshot( info.jobId );
  REQUIRE( job.has_value() );
  REQUIRE( job->state == sicnu::jobs::JobState::Succeeded );
}

TEST_CASE( "UI contract: cancel routes through Task Center to JobEngine",
           "[task_center][contract][cancellation]" )
{
  ensureApp();
  auto &engine = sicnu::jobs::JobEngine::instance();
  engine.shutdownForTests();

  sicnu::jobs::JobRequest req;
  req.algorithmId = "callable:contract-cancel-35";
  req.source = "panel";

  const long taskId = sicnu::TaskCenter::instance().submitJob(
    req,
    []( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
      for ( int i = 0; i < 200; ++i )
      {
        ctx.throwIfCancelled();
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
      }
      return Json::Value( Json::objectValue );
    } );

  REQUIRE( taskId > 0 );
  REQUIRE( sicnu::TaskCenter::instance().cancelTask( taskId ) );
  engine.waitUntilIdleForTests();

  sicnu::AlgorithmTaskInfo info;
  for ( int attempt = 0; attempt < 100; ++attempt )
  {
    info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( info.status == sicnu::TaskStatus::Canceled )
      break;
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }
  REQUIRE( info.status == sicnu::TaskStatus::Canceled );
  REQUIRE_FALSE( info.jobId.empty() );
  const auto job = engine.snapshot( info.jobId );
  REQUIRE( job.has_value() );
  REQUIRE( job->state == sicnu::jobs::JobState::Cancelled );
}

TEST_CASE( "UI contract: AsyncAlgorithmRunner destructor marks TaskCenter task canceled",
           "[task_center][contract][async_runner]" )
{
  ensureApp();
  QVariantMap params;
  params[QStringLiteral( "INPUT" )] = QStringLiteral( "test_input.tif" );

  const long taskId = sicnu::TaskCenter::instance().enqueueTask( QStringLiteral( "test:async_teardown" ), params, true );
  REQUIRE( taskId > 0 );
  sicnu::TaskCenter::instance().markTaskRunning( taskId );

  // Lightweight task: the runner only cancels/disconnects it, so a plain
  // QgsTask subclass is sufficient — constructing a QgsProcessingAlgRunnerTask
  // with a nullptr algorithm would crash in its initializer list.
  class MinimalTask : public QgsTask
  {
  public:
    MinimalTask()
      : QgsTask( QStringLiteral( "test:async_teardown_task" ) )
    {}
  protected:
    bool run() override { return true; }
  };
  auto *task = new MinimalTask();

  class TestRunner : public AsyncAlgorithmRunner
  {
  public:
    TestRunner( QgsTask *t, long centerId )
      : AsyncAlgorithmRunner( nullptr )
    {
      setTaskForTesting( t, centerId );
    }
  };

  {
    TestRunner runner( task, taskId );
    REQUIRE( runner.isRunning() );
    // Destruction of runner while running should mark TaskCenter task canceled
  }

  // TaskCenter task is canceled when runner goes out of scope while running
  const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
  REQUIRE( info.taskId == taskId );
  REQUIRE( info.status == sicnu::TaskStatus::Canceled );
}
