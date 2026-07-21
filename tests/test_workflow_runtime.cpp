// tests/test_workflow_runtime.cpp
#include <catch2/catch_test_macros.hpp>

#include "workflow/workflow_definition.h"
#include "workflow/workflow_registry.h"
#include "workflow/workflow_session.h"
#include "workflow/workflow_types.h"

using namespace sicnu::workflow;

static WorkflowDefinition makeTwoStep()
{
  WorkflowDefinition d;
  d.id = "tool.demo";
  d.title = "Demo";
  d.host = HostKind::TaskPanel;
  StepDef a;
  a.id = "configure";
  a.title = "Configure";
  a.kind = StepKind::Operator;
  a.operatorId = "test:add";
  StepDef b;
  b.id = "review";
  b.title = "Review";
  b.kind = StepKind::Review;
  d.steps = {a, b};
  return d;
}

TEST_CASE("workflow types compile", "[workflow]")
{
  WorkflowDefinition d;
  d.id = "tool.test";
  REQUIRE(d.id == "tool.test");
}

TEST_CASE("Registry stores definitions", "[workflow]")
{
  WorkflowRegistry reg;
  reg.registerDefinition(makeTwoStep());
  REQUIRE(reg.has("tool.demo"));
  REQUIRE_FALSE(reg.has("missing"));
  const auto *d = reg.find("tool.demo");
  REQUIRE(d != nullptr);
  REQUIRE(d->steps.size() == 2);
}

TEST_CASE("Session open starts at first step", "[workflow]")
{
  auto d = makeTwoStep();
  WorkflowSession s(d, "sess-1");
  auto snap = s.snapshot();
  REQUIRE(snap.sessionId == "sess-1");
  REQUIRE(snap.definitionId == "tool.demo");
  REQUIRE(snap.currentStepId == "configure");
  REQUIRE(snap.completedStepIds.empty());
}

TEST_CASE("Session goto and setParams mark dirty", "[workflow]")
{
  WorkflowSession s(makeTwoStep(), "sess-2");
  REQUIRE(s.gotoStep("review"));
  REQUIRE(s.snapshot().currentStepId == "review");
  REQUIRE_FALSE(s.gotoStep("nope"));
  Json::Value p;
  p["a"] = 1;
  p["b"] = 2;
  s.setParams("configure", p);
  REQUIRE(s.snapshot().dirty);
  REQUIRE(s.paramsFor("configure")["a"].asInt() == 1);
}
