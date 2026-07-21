// tests/test_workflow_runtime.cpp
#include <catch2/catch_test_macros.hpp>
#include "workflow/workflow_types.h"

TEST_CASE("workflow types compile", "[workflow]")
{
  sicnu::workflow::WorkflowDefinition d;
  d.id = "tool.test";
  REQUIRE(d.id == "tool.test");
}
