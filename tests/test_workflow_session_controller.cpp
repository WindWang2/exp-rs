// tests/test_workflow_session_controller.cpp — Catch2 unit tests for WorkflowSessionController state queries (ADR 0037)
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>

#include "app/shell/schema_form_builder.h"
#include "app/shell/workflow_session_controller.h"
#include "app/shell/task_panel_host.h"

TEST_CASE( "WorkflowSessionController default state queries return clean initial values", "[workflow][shell][controller]" )
{
  WorkflowSessionController controller;

  CHECK_FALSE( controller.isRunInFlight() );
  CHECK( controller.activeSessionId().isEmpty() );
  CHECK( controller.activeStepId().isEmpty() );
  CHECK( controller.activePipelineId() == -1 );
  CHECK( controller.pendingTaskId() == -1 );
}

TEST_CASE( "WorkflowSessionController cancelActiveRun resets state cleanly when idle", "[workflow][shell][controller]" )
{
  WorkflowSessionController controller;

  controller.cancelActiveRun();

  CHECK_FALSE( controller.isRunInFlight() );
  CHECK( controller.activePipelineId() == -1 );
  CHECK( controller.pendingTaskId() == -1 );

  // Idempotent second cancel
  controller.cancelActiveRun();
  CHECK_FALSE( controller.isRunInFlight() );
  CHECK( controller.activePipelineId() == -1 );
  CHECK( controller.pendingTaskId() == -1 );
}

TEST_CASE( "WorkflowSessionController cancelActiveRun cancels in-flight pipeline run", "[workflow][shell][controller]" )
{
  WorkflowSessionController controller;
  TaskPanelHost panel;
  controller.bindPanel( &panel );
  controller.registerBuiltins();

  // R10-B: the old "wf:vegetation_indices" id does not exist anywhere in
  // the source, so this case used to vacuate. openBareOperator registers
  // a real definition at runtime.
  const QString sessionId = controller.openBareOperator( QStringLiteral( "rs:band_math" ) );
  REQUIRE( !sessionId.isEmpty() );

  controller.runFullWorkflow();
  REQUIRE( controller.isRunInFlight() );
  CHECK( ( controller.activePipelineId() >= 0 || controller.pendingTaskId() >= 0 ) );
  controller.cancelActiveRun();
  CHECK_FALSE( controller.isRunInFlight() );
  CHECK( controller.activePipelineId() == -1 );
  CHECK( controller.pendingTaskId() == -1 );

  // Safe idempotent cancel
  controller.cancelActiveRun();
  CHECK_FALSE( controller.isRunInFlight() );
}

// Restored regression home (#595): SchemaFormBuilder must round-trip ARRAY
// parameters as JSON arrays (a line-edit degeneration turned them into
// comma strings) - formerly tests/test_w10_ui_misc_regression.cpp.
TEST_CASE("SchemaFormBuilder round-trips array parameters as arrays (#595)", "[shell][schema_form][w10]")
{
    // QApplication may already exist from earlier cases in this binary.
    if (!qApp)
    {
        static int argc = 1;
        static char a0[] = "test_workflow_session_controller";
        char *argv[] = {a0, nullptr};
        new QApplication(argc, argv);
    }

    SchemaFormBuilder builder;
    Json::Value schema(Json::objectValue);
    schema["type"] = "object";
    Json::Value props(Json::objectValue);
    Json::Value bandList(Json::objectValue);
    bandList["type"] = "array";
    Json::Value items(Json::objectValue);
    items["type"] = "number";
    bandList["items"] = items;
    props["bands"] = bandList;
    schema["properties"] = props;
    builder.rebuild(schema);

    Json::Value params(Json::objectValue);
    Json::Value arr(Json::arrayValue);
    arr.append(1);
    arr.append(2);
    arr.append(3);
    params["bands"] = arr;
    builder.setValues(params);

    const Json::Value out = builder.values();
    REQUIRE(out.isObject());
    REQUIRE(out.isMember("bands"));
    REQUIRE(out["bands"].isArray());
    REQUIRE(out["bands"].size() == 3);
    REQUIRE(out["bands"][0].asInt() == 1);
    REQUIRE(out["bands"][2].asInt() == 3);
}

// R10-B P1-1: runFullWorkflow/runUpToNode had no in-flight guard — a second
// click submitted a SECOND tracked pipeline, overwrote activePipelineId and
// orphaned the first run (Stop could only cancel the latest). Both entries
// now refuse while m_runInFlight, mirroring onRunClicked.
TEST_CASE( "runFullWorkflow and runUpToNode refuse duplicate submission while in flight",
           "[workflow][shell][controller][r10b]" )
{
    WorkflowSessionController controller;
    TaskPanelHost panel;
    controller.bindPanel( &panel );
    controller.registerBuiltins();

    // Real operator path (openBareOperator registers a definition at
    // runtime): "wf:vegetation_indices" exists nowhere in the source, so
    // the previous openTool-based setup vacated this whole test case.
    const QString sessionId = controller.openBareOperator( QStringLiteral( "rs:band_math" ) );
    REQUIRE( !sessionId.isEmpty() );

    QStringList messages;
    QObject::connect( &controller, &WorkflowSessionController::statusMessage,
                      [&messages]( const QString &m ) { messages << m; });

    controller.runFullWorkflow();
    // REQUIRE, not CHECK-in-if: a submission that never armed the guard
    // would silently vacuate every assertion below.
    REQUIRE( controller.isRunInFlight() );
    {
        const int pipelineBefore = controller.activePipelineId();

        // Second full submission: refused, id not overwritten, typed message.
        controller.runFullWorkflow();
        CHECK( controller.isRunInFlight() );
        CHECK( controller.activePipelineId() == pipelineBefore );
        bool sawRefusal = false;
        for ( const QString &m : messages )
        {
            if ( m.contains( QLatin1String( "already in flight" ) ) )
                sawRefusal = true;
        }
        REQUIRE( sawRefusal );

        // Partial-DAG entry shares the guard.
        messages.clear();
        controller.runUpToNode( QStringLiteral( "step_that_does_not_matter" ) );
        CHECK( controller.isRunInFlight() );
        CHECK( controller.activePipelineId() == pipelineBefore );
        sawRefusal = false;
        for ( const QString &m : messages )
        {
            if ( m.contains( QLatin1String( "already in flight" ) ) )
                sawRefusal = true;
        }
        REQUIRE( sawRefusal );

        // The guard is recoverable, not a dead end: cancel re-arms submission.
        controller.cancelActiveRun();
        REQUIRE_FALSE( controller.isRunInFlight() );
        controller.runFullWorkflow();
        REQUIRE( controller.isRunInFlight() );
        controller.cancelActiveRun();
        REQUIRE_FALSE( controller.isRunInFlight() );
    }
}
