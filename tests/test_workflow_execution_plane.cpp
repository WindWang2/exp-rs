// tests/test_workflow_execution_plane.cpp — Workflow Engine v2 minimal closed-loop slice
//
// Verifies:
// - StepDef optional resourceEstimateMb / verificationPolicy parsing (forward compat)
// - WorkflowRuntime::runStep via ExecutionPlane/TaskCenter (async) with commit/verify scaffolding
// - Cancel via ExecutionPlane (Fake slow operator)
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <thread>

#include "jobs/job_engine.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_schema.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/execution_plane.h"
#include "processing/framework/task_center.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_runtime.h"
#include "workflow/workflow_types.h"

using namespace sicnu::workflow;
using namespace sicnu::operators;
using namespace sicnu::operators::schema;
using namespace sicnu::processing;

namespace {

QCoreApplication *ensureApp()
{
  static QCoreApplication *app = [] {
    int argc = 1;
    static char a0[] = "test_workflow_execution_plane";
    char *argv[] = {a0, nullptr};
    return new QCoreApplication(argc, argv);
  }();
  return app;
}

void wireFallback()
{
  sicnu::jobs::JobEngine::instance().setFallbackExecutor(
    [](const sicnu::jobs::JobRequest &req, sicnu::operators::RSOperatorContext &ctx) -> Json::Value {
      const auto adapter = AtomicAlgorithmRegistry::instance().findAdapter(req.algorithmId);
      if (!adapter)
        throw std::runtime_error("Unknown algorithm: " + req.algorithmId);
      ProgressCallback prog = [&ctx](int p, const std::string &m) { ctx.reportProgress(p / 100.0, m); };
      return adapter->execute(req.params, prog, [&ctx]() { return ctx.isCancelled(); });
    });
}

// Fast operator for ExecutionPlane success path
class FastAddOperator : public RSOperator
{
public:
  std::string name() const override { return "test:fast_add_plane"; }
  std::string displayName() const override { return "FastAddPlane"; }
  std::string group() const override { return "test"; }
  std::string description() const override { return "fast add via plane"; }
  Json::Value schema() const override
  {
    Json::Value params(Json::objectValue);
    params["a"] = makeNumberParam("a", "a", 0.0);
    params["b"] = makeNumberParam("b", "b", 0.0);
    Json::Value outputs(Json::objectValue);
    outputs["result"] = makeNumberParam("result", "sum");
    Json::Value root = makeRootSchema(displayName(), description(), params, outputs);
    root["required"] = makeRequired({"a", "b"});
    return root;
  }
  Json::Value run(const Json::Value &params, RSOperatorContext &ctx) override
  {
    (void)ctx;
    if (!params.isMember("a") || !params.isMember("b"))
      throw RSOperatorError(ErrorCode::MissingRequiredParameter, "need a b");
    Json::Value r(Json::objectValue);
    r["result"] = params["a"].asDouble() + params["b"].asDouble();
    return r;
  }
};

// Slow operator that polls cancellation
class SlowPlaneOperator : public RSOperator
{
public:
  std::string name() const override { return "test:slow_plane"; }
  std::string displayName() const override { return "SlowPlane"; }
  std::string group() const override { return "test"; }
  std::string description() const override { return "slow cancel aware"; }
  Json::Value schema() const override { return Json::Value(Json::objectValue); }
  Json::Value run(const Json::Value &, RSOperatorContext &ctx) override
  {
    for (int i = 0; i < 1000000; ++i)
    {
      ctx.throwIfCancelled();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Json::Value r(Json::objectValue);
    r["result"] = "finished";
    return r;
  }
};

void ensureOps()
{
  ensureApp();
  auto &reg = RSOperatorRegistry::instance();
  if (!reg.hasOperator("test:fast_add_plane"))
    reg.registerOperator("test:fast_add_plane", [](){ return std::make_unique<FastAddOperator>(); });
  if (!reg.hasOperator("test:slow_plane"))
    reg.registerOperator("test:slow_plane", [](){ return std::make_unique<SlowPlaneOperator>(); });
  // Refresh Atomic registry so ExecutionPlane can find them via JobEngine fallback
  AtomicAlgorithmRegistry::instance().reset();
  // Ensure fallback is wired (like production)
  wireFallback();
}

} // namespace

TEST_CASE("StepDef optional resourceEstimateMb and verificationPolicy parsing", "[workflow][execution_plane]")
{
  ensureOps();
  Json::Value defJson(Json::objectValue);
  defJson["id"] = "wf:test";
  defJson["title"] = "Test";
  Json::Value steps(Json::arrayValue);
  Json::Value s(Json::objectValue);
  s["id"] = "step1";
  s["title"] = "Step1";
  s["kind"] = static_cast<int>(StepKind::Operator);
  s["operatorId"] = "test:fast_add_plane";
  s["resourceEstimateMb"] = 123;
  s["verificationPolicy"] = "raster";
  Json::Value params(Json::objectValue);
  params["a"] = 1;
  params["b"] = 2;
  s["params"] = params;
  steps.append(s);
  // step without optional fields
  Json::Value s2(Json::objectValue);
  s2["id"] = "step2";
  s2["title"] = "Step2";
  s2["kind"] = static_cast<int>(StepKind::Operator);
  s2["operatorId"] = "test:fast_add_plane";
  steps.append(s2);
  defJson["steps"] = steps;

  WorkflowDefinition def;
  std::string err;
  REQUIRE(workflowDefinitionFromJson(defJson, def, err));
  REQUIRE(def.steps.size() == 2);
  REQUIRE(def.steps[0].resourceEstimateMb == 123);
  REQUIRE(def.steps[0].verificationPolicy == "raster");
  REQUIRE(def.steps[1].resourceEstimateMb == 0);
  REQUIRE(def.steps[1].verificationPolicy.empty());

  // Round-trip ToJson preserves optional fields
  Json::Value out = workflowDefinitionToJson(def);
  REQUIRE(out["steps"][0].isMember("resourceEstimateMb"));
  REQUIRE(out["steps"][0]["resourceEstimateMb"].asUInt() == 123);
  REQUIRE(out["steps"][0]["verificationPolicy"].asString() == "raster");
  REQUIRE_FALSE(out["steps"][1].isMember("resourceEstimateMb"));
  REQUIRE_FALSE(out["steps"][1].isMember("verificationPolicy"));

  // Forward compat: missing fields default to 0/empty
  Json::Value defJson2(Json::objectValue);
  defJson2["id"] = "wf:compat";
  Json::Value steps2(Json::arrayValue);
  Json::Value s3(Json::objectValue);
  s3["id"] = "s1";
  s3["operatorId"] = "test:fast_add_plane";
  steps2.append(s3);
  defJson2["steps"] = steps2;
  WorkflowDefinition def2;
  REQUIRE(workflowDefinitionFromJson(defJson2, def2, err));
  REQUIRE(def2.steps[0].resourceEstimateMb == 0);
  REQUIRE(def2.steps[0].verificationPolicy.empty());
}

TEST_CASE("WorkflowRuntime via ExecutionPlane submits and completes", "[workflow][execution_plane]")
{
  ensureOps();
  WorkflowRuntime rt(false);
  // Ensure fallback wired after any reset in ensureOps
  wireFallback();

  WorkflowDefinition d;
  d.id = "wf:plane_success";
  d.title = "PlaneSuccess";
  d.host = HostKind::TaskPanel;
  StepDef step;
  step.id = "run";
  step.title = "Run";
  step.kind = StepKind::Operator;
  step.operatorId = "test:fast_add_plane";
  step.resourceEstimateMb = 16; // explicit tiled-like estimate
  step.verificationPolicy = "skip"; // no file output, skip verifier
  d.steps = {step};
  rt.registerDefinition(d);
  const std::string sid = rt.open("wf:plane_success");
  REQUIRE_FALSE(sid.empty());

  Json::Value p;
  p["a"] = 2;
  p["b"] = 3;
  rt.setParams(sid, "run", p);

  // runStep should go via ExecutionPlane by default
  REQUIRE(rt.useExecutionPlane());
  Json::Value result = rt.runStep(sid, "run");
  REQUIRE(result.isMember("result"));
  REQUIRE(result["result"].asDouble() == 5.0);

  auto snap = rt.state(sid);
  REQUIRE(snap.completedStepIds.size() == 1);
  REQUIRE(snap.artifacts.count("result") == 1);
  // Artifact should be the stringified result
  REQUIRE((snap.artifacts.at("result") == "5" || snap.artifacts.at("result").find("5") != std::string::npos));
}

TEST_CASE("WorkflowRuntime via ExecutionPlane is cancellable (Fake slow operator)", "[workflow][execution_plane][cancel]")
{
  ensureOps();
  wireFallback();
  WorkflowRuntime rt(false);
  rt.setUseExecutionPlane(true);

  WorkflowDefinition d;
  d.id = "wf:plane_cancel";
  d.title = "PlaneCancel";
  d.host = HostKind::TaskPanel;
  StepDef step;
  step.id = "slow";
  step.title = "Slow";
  step.kind = StepKind::Operator;
  step.operatorId = "test:slow_plane";
  step.resourceEstimateMb = 8;
  step.verificationPolicy = "skip";
  d.steps = {step};
  rt.registerDefinition(d);
  const std::string sid = rt.open("wf:plane_cancel");
  REQUIRE_FALSE(sid.empty());

  std::atomic<bool> started{false};
  std::string runError;
  bool threw = false;

  std::thread runner([&]() {
    started.store(true);
    try {
      rt.runStep(sid, "slow");
    } catch (const std::exception &e) {
      threw = true;
      runError = e.what();
    }
  });

  while (!started.load())
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  // Give the ExecutionPlane task a moment to be admitted/running
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  rt.requestCancel(sid);
  runner.join();

  REQUIRE(threw);
  // Should contain cancel hint (TaskCenter/Operator cancelled)
  bool hasCancel = runError.find("cancel") != std::string::npos
                   || runError.find("Cancel") != std::string::npos
                   || runError.find("canceled") != std::string::npos;
  INFO("runError=" << runError);
  REQUIRE(hasCancel);

  // Session should not be marked completed after cancel
  auto snap = rt.state(sid);
  REQUIRE(snap.completedStepIds.empty());

  // Fallback path still works when plane disabled
  WorkflowRuntime rt2(false);
  rt2.setUseExecutionPlane(false);
  rt2.registerDefinition(d);
  const std::string sid2 = rt2.open("wf:plane_cancel");
  REQUIRE_FALSE(sid2.empty());
  std::atomic<bool> started2{false};
  std::string err2;
  bool threw2 = false;
  std::thread r2([&]() {
    started2.store(true);
    try { rt2.runStep(sid2, "slow"); } catch (const std::exception &e) { threw2 = true; err2 = e.what(); }
  });
  while (!started2.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  rt2.requestCancel(sid2);
  r2.join();
  REQUIRE(threw2);
  REQUIRE((err2.find("cancel") != std::string::npos || err2.find("Cancel") != std::string::npos));
}

TEST_CASE("WorkflowRuntime fallback to sync when ExecutionPlane disabled", "[workflow][execution_plane][fallback]")
{
  ensureOps();
  WorkflowRuntime rt(false);
  rt.setUseExecutionPlane(false);
  WorkflowDefinition d;
  d.id = "wf:fallback";
  d.title = "Fallback";
  d.host = HostKind::TaskPanel;
  StepDef step;
  step.id = "run";
  step.title = "Run";
  step.kind = StepKind::Operator;
  step.operatorId = "test:fast_add_plane";
  d.steps = {step};
  rt.registerDefinition(d);
  const std::string sid = rt.open("wf:fallback");
  REQUIRE_FALSE(sid.empty());
  Json::Value p;
  p["a"] = 10;
  p["b"] = 20;
  rt.setParams(sid, "run", p);
  Json::Value r = rt.runStep(sid, "run");
  REQUIRE(r["result"].asDouble() == 30.0);
}
