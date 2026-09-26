// test_project_context_run_mirror.cpp — one run-mirror connection per context
//
// The workflow run-state mirror (issue #754) used to be wired inside every
// openWorkspaceStore() call. Every successful project open — and every Save
// As, which goes through reopenWorkspaceStore → openWorkspaceStore — added
// ANOTHER connection from the process-wide WorkflowRunCoordinator to this
// context's WorkspaceService. After N opens, a single run transition ran N
// queued recordRun writes and N entityChanged broadcasts for the same run.
// (recordRun is a by-id merge, so data stayed correct; the redundant store
// writes and broadcasts grew without bound per session.)
//
// Contract under test: one context, exactly ONE mirror delivery per
// coordinator emission — regardless of how many stores it opened. The test
// opens two stores back to back (open + Save As shape), then drives ONE
// real tracked pipeline through the coordinator and counts deliveries on
// both sides: coordinator emissions vs. workspaceService entityChanged
// "run" broadcasts. They must be equal.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "project_context.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include <json/json.h>

#include "jobs/job_engine.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"
#include "support/qt_lifecycle.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
int fake_argc = 1;
char fake_argv0[] = "test_project_context_run_mirror";
char *fake_argv[] = { fake_argv0, nullptr };

QCoreApplication *ensureApp()
{
  if ( !QCoreApplication::instance() )
  {
    return sicnu::test::qtlifecycle::heapQCoreApplication( fake_argc, fake_argv );
  }
  return static_cast<QCoreApplication *>( QCoreApplication::instance() );
}

/// Bounded event-loop pump: processes queued deliveries until @p predicate
/// holds, then until the observable state stops changing for @p settle
/// consecutive iterations. No sleeps masking races: the pump only turns the
/// loop so QueuedConnection mirror deliveries actually run (the test binary
/// has no event loop of its own).
void pumpUntil( const std::function<bool()> &predicate,
                int maxMs = 15000, int settleIters = 40 )
{
  QCoreApplication *app = ensureApp();
  int settle = 0;
  for ( int elapsed = 0; elapsed < maxMs; elapsed += 5 )
  {
    const bool before = predicate();
    QCoreApplication::processEvents( QEventLoop::AllEvents, 5 );
    if ( before && predicate() )
      ++settle;
    else
      settle = 0;
    if ( settle >= settleIters )
      return;
    if ( !before )
      std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    Q_UNUSED( app );
  }
}

/// A one-step definition whose operator completes immediately.
sicnu::workflow::WorkflowDefinition oneStepDefinition( const std::string &prefix )
{
  sicnu::workflow::WorkflowDefinition def;
  def.id = prefix + "_def";
  def.title = "run mirror probe";

  sicnu::workflow::StepDef step;
  step.id = "only";
  step.title = "Only";
  step.kind = sicnu::workflow::StepKind::Operator;
  step.operatorId = prefix + ":noop";
  step.params["output"] = "/tmp/" + prefix + "_only.tif";

  def.steps.push_back( step );
  return def;
}

} // namespace

TEST_CASE( "ProjectContext run mirror: one delivery per coordinator emission after repeated store opens",
           "[app][workspace][run_mirror][regression]" )
{
  ensureApp();
  auto &engine = sicnu::jobs::JobEngine::instance();
  engine.shutdownForTests();
  engine.clearExecutors();
  engine.setMaxWorkers( 2 );

  auto created = sicnu::app::ProjectContext::createHeadless();
  REQUIRE( created );
  auto context = created.take();
  REQUIRE( context->workspaceService().isStoreOpen() == false );

  QTemporaryDir dirA;
  QTemporaryDir dirB;
  REQUIRE( dirA.isValid() );
  REQUIRE( dirB.isValid() );

  // Two store binds — the open + Save-As shape that used to stack a second
  // mirror connection on the first.
  REQUIRE( context->openWorkspaceStore( dirA.filePath( "first.qgs" ) ) );
  REQUIRE( context->openWorkspaceStore( dirB.filePath( "second.qgs" ) ) );

  // Coordinator-side emission count (delivered on the worker drain thread —
  // DirectConnection so it never depends on this thread's loop).
  std::atomic<int> coordinatorEmissions{ 0 };
  QObject emissionContext;
  QObject::connect( &sicnu::workflow::WorkflowRunCoordinator::instance(),
                    &sicnu::workflow::WorkflowRunCoordinator::runStateChanged,
                    &emissionContext,
                    [&]( const QString &, const QString &, const QString &,
                         qint64, qint64 ) { coordinatorEmissions.fetch_add( 1 ); },
                    Qt::DirectConnection );

  // Service-side delivery count: every mirror write broadcasts one "run"
  // entityChanged through the context's workspace service.
  std::atomic<int> mirrorDeliveries{ 0 };
  QObject::connect( &context->workspaceService(),
                    &sicnu::workspace::WorkspaceService::entityChanged,
                    &emissionContext,
                    [&]( const QString &kind, const QString & ) {
                      if ( kind == QLatin1String( "run" ) )
                        mirrorDeliveries.fetch_add( 1 );
                    } );

  engine.registerExecutor(
      "runmirror:noop",
      []( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
        return Json::Value();
      } );

  QTemporaryDir checkpointDir;
  REQUIRE( checkpointDir.isValid() );
  sicnu::workflow::WorkflowRunCoordinator::instance().setCheckpointDirectory(
      checkpointDir.path() );

  const long pipelineId = sicnu::workflow::WorkflowRunCoordinator::instance()
                              .startTrackedPipeline( oneStepDefinition( "runmirror" ) );
  REQUIRE( pipelineId > 0 );

  // Wait for the tracked run to reach a terminal state, then settle so every
  // queued mirror delivery has actually been drained.
  auto run = sicnu::workflow::WorkflowRunCoordinator::instance().runForPipeline(
      pipelineId );
  pumpUntil( [&] {
    run = sicnu::workflow::WorkflowRunCoordinator::instance().runForPipeline(
        pipelineId );
    return run && run->state() == sicnu::workflow::WorkflowRunState::Completed;
  } );
  REQUIRE( run );
  REQUIRE( run->state() == sicnu::workflow::WorkflowRunState::Completed );

  const int emissions = coordinatorEmissions.load();
  const int deliveries = mirrorDeliveries.load();
  INFO( "coordinator emissions: " << emissions
        << ", mirror deliveries: " << deliveries );
  // A completed run emits at least Running + Completed.
  REQUIRE( emissions >= 2 );
  // THE contract: one mirror connection per context — deliveries match
  // emissions one to one (pre-fix: two store opens → two connections →
  // deliveries == 2 × emissions).
  CHECK( deliveries == emissions );

  engine.shutdownForTests();
}
