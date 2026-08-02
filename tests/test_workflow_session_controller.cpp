// tests/test_workflow_session_controller.cpp — Catch2 unit tests for WorkflowSessionController state queries (ADR 0037)
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

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
}
