// tests/test_workflow_session_controller.cpp — Catch2 unit tests for WorkflowSessionController state queries (ADR 0037)
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>

#include "app/shell/schema_form_builder.h"
#include "app/shell/workflow_session_controller.h"

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
  controller.registerBuiltins();

  const QString sessionId = controller.openTool( QStringLiteral( "wf:vegetation_indices" ) );
  if ( !sessionId.isEmpty() )
  {
    controller.runFullWorkflow();
    if ( controller.isRunInFlight() )
    {
      CHECK( ( controller.activePipelineId() >= 0 || controller.pendingTaskId() >= 0 ) );
      controller.cancelActiveRun();
      CHECK_FALSE( controller.isRunInFlight() );
      CHECK( controller.activePipelineId() == -1 );
      CHECK( controller.pendingTaskId() == -1 );

      // Safe idempotent cancel
      controller.cancelActiveRun();
      CHECK_FALSE( controller.isRunInFlight() );
    }
  }
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
