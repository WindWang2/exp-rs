// tests/test_workflow_runtime.cpp
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_schema.h"
#include "workflow/builtin_definitions.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_gate.h"
#include "workflow/workflow_runtime.h"
#include "workflow/workflow_session.h"
#include "workflow/workflow_types.h"

#include <QCoreApplication>

using namespace sicnu::workflow;
using namespace sicnu::operators;
using namespace sicnu::operators::schema;

namespace {

int &wfAppArgc()
{
  static int argc = 1;
  return argc;
}
char wfAppArgv0[] = "test_workflow_runtime";
char *wfAppArgv[] = {wfAppArgv0, nullptr};

void ensureQtApp()
{
  if ( !QCoreApplication::instance() )
    new QCoreApplication( wfAppArgc(), wfAppArgv );
}

/// Minimal mock operator (same pattern as tests/test_rs_operator.cpp).
class TestAddOperator : public RSOperator
{
  public:
    std::string name() const override { return "test:add"; }
    std::string displayName() const override { return "Add Two Numbers"; }
    std::string group() const override { return "math"; }
    std::string description() const override { return "Adds two numbers."; }

    Json::Value schema() const override
    {
      Json::Value params( Json::objectValue );
      params["a"] = makeNumberParam( "a", "First summand", 0.0 );
      params["b"] = makeNumberParam( "b", "Second summand", 0.0 );

      Json::Value outputs( Json::objectValue );
      outputs["result"] = makeNumberParam( "result", "Sum" );

      Json::Value root = makeRootSchema( displayName(), description(), params, outputs );
      root["required"] = makeRequired( {"a", "b"} );
      return root;
    }

    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override
    {
      if ( !params.isMember( "a" ) || !params.isMember( "b" ) )
      {
        throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                               "Parameters 'a' and 'b' are required" );
      }
      if ( !params["a"].isNumeric() || !params["b"].isNumeric() )
      {
        throw RSOperatorError( ErrorCode::TypeMismatch,
                               "Parameters 'a' and 'b' must be numeric" );
      }

      context.throwIfCancelled();

      Json::Value result( Json::objectValue );
      result["result"] = params["a"].asDouble() + params["b"].asDouble();
      return result;
    }
};

void ensureTestAddRegistered()
{
  ensureQtApp();
  auto &reg = RSOperatorRegistry::instance();
  if ( !reg.hasOperator( "test:add" ) )
  {
    reg.registerOperator( "test:add", []() -> std::unique_ptr<RSOperator> {
      return std::make_unique<TestAddOperator>();
    } );
  }
}

WorkflowDefinition makeTwoStep()
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

} // namespace

TEST_CASE( "workflow types compile", "[workflow]" )
{
  WorkflowDefinition d;
  d.id = "tool.test";
  REQUIRE( d.id == "tool.test" );
}

TEST_CASE( "Runtime stores definitions", "[workflow]" )
{
  WorkflowRuntime rt( false );
  rt.registerDefinition( makeTwoStep() );
  REQUIRE( rt.hasDefinition( "tool.demo" ) );
  REQUIRE_FALSE( rt.hasDefinition( "missing" ) );
  const auto *d = rt.findDefinition( "tool.demo" );
  REQUIRE( d != nullptr );
  REQUIRE( d->steps.size() == 2 );
}

TEST_CASE( "Session open starts at first step", "[workflow]" )
{
  auto d = makeTwoStep();
  WorkflowSession s( d, "sess-1" );
  auto snap = s.snapshot();
  REQUIRE( snap.sessionId == "sess-1" );
  REQUIRE( snap.definitionId == "tool.demo" );
  REQUIRE( snap.currentStepId == "configure" );
  REQUIRE( snap.completedStepIds.empty() );
}

TEST_CASE( "Session goto and setParams mark dirty", "[workflow]" )
{
  WorkflowSession s( makeTwoStep(), "sess-2" );
  REQUIRE( s.gotoStep( "review" ) );
  REQUIRE( s.snapshot().currentStepId == "review" );
  REQUIRE_FALSE( s.gotoStep( "nope" ) );
  Json::Value p;
  p["a"] = 1;
  p["b"] = 2;
  s.setParams( "configure", p );
  REQUIRE( s.snapshot().dirty );
  REQUIRE( s.paramsFor( "configure" )["a"].asInt() == 1 );
}

TEST_CASE( "Gate hasArtifact", "[workflow][gate]" )
{
  WorkflowSession s( makeTwoStep(), "g1" );
  GateDef g;
  g.require = "hasArtifact:output";
  g.hint = "需要先生成输出";
  auto r = evaluateGates( s, {g} );
  REQUIRE_FALSE( r.ok );
  REQUIRE( r.hints.at( 0 ).find( "输出" ) != std::string::npos );
  s.setArtifact( "output", "/tmp/out.tif" );
  r = evaluateGates( s, {g} );
  REQUIRE( r.ok );
}

TEST_CASE( "Gate paramNonEmpty", "[workflow][gate]" )
{
  WorkflowSession s( makeTwoStep(), "g2" );
  GateDef g;
  g.require = "paramNonEmpty:configure.input";
  g.hint = "请选择输入";
  REQUIRE_FALSE( evaluateGates( s, {g} ).ok );
  Json::Value p;
  p["input"] = "/data/a.tif";
  s.setParams( "configure", p );
  REQUIRE( evaluateGates( s, {g} ).ok );
}

TEST_CASE( "Gate paramNonEmpty current step", "[workflow][gate]" )
{
  WorkflowSession s( makeTwoStep(), "g3" );
  // current step is "configure"
  GateDef g;
  g.require = "paramNonEmpty:input";
  g.hint = "请填写 input";
  REQUIRE_FALSE( evaluateGates( s, {g} ).ok );
  Json::Value p;
  p["input"] = "/data/b.tif";
  s.setParams( "configure", p );
  REQUIRE( evaluateGates( s, {g} ).ok );
}

TEST_CASE( "Gate unknown require fails", "[workflow][gate]" )
{
  WorkflowSession s( makeTwoStep(), "g4" );
  GateDef g;
  g.require = "minCount:3";
  auto r = evaluateGates( s, {g} );
  REQUIRE_FALSE( r.ok );
  REQUIRE_FALSE( r.hints.empty() );
  REQUIRE( r.hints.at( 0 ).find( "未知门禁条件" ) != std::string::npos );
}

TEST_CASE( "Gate empty list passes", "[workflow][gate]" )
{
  WorkflowSession s( makeTwoStep(), "g5" );
  auto r = evaluateGates( s, {} );
  REQUIRE( r.ok );
  REQUIRE( r.hints.empty() );
}

TEST_CASE( "Builtin workflows registered", "[workflow]" )
{
  WorkflowRuntime rt;
  // Atomic TaskPanel tools
  REQUIRE( rt.hasDefinition( "tool.rs.spectral_index" ) );
  REQUIRE( rt.hasDefinition( "tool.rs.band_math" ) );
  REQUIRE( rt.hasDefinition( "tool.rs.change_detection" ) );
  REQUIRE( rt.hasDefinition( "tool.rs.image_fusion" ) );
  REQUIRE( rt.hasDefinition( "tool.rs.mosaic" ) );
  REQUIRE( rt.hasDefinition( "tool.rs.terrain_analysis" ) );
  REQUIRE( rt.hasDefinition( "tool.rs.pca" ) );
  REQUIRE( rt.hasDefinition( "tool.rs.atmospheric_correction" ) );
  REQUIRE( rt.hasDefinition( "tool.rs.qa_mask" ) );
  // Workspace labs
  REQUIRE( rt.hasDefinition( "lab.classify.supervised" ) );
  REQUIRE( rt.hasDefinition( "lab.georef.image_to_map" ) );
  REQUIRE( rt.hasDefinition( "lab.obia" ) );
  REQUIRE( rt.hasDefinition( "classification_postprocess_merge" ) );
  // Reusable preprocessing DAG (B8/E1).
  REQUIRE( rt.hasDefinition( "lab.preprocess.optical" ) );
  // registerBuiltinWorkflows (builtin_definitions.cpp) registers 14 atomic
  // tools + 5 compound workflows (preprocess_optical, classify_supervised,
  // georef_image_to_map, obia, classification_postprocess_merge) = 19. The
  // individual hasDefinition assertions above cover the actual set; keep this
  // count in sync with registerBuiltinWorkflows.
  REQUIRE( rt.registeredDefinitionIds().size() == 19 );

  const auto *d = rt.findDefinition( "tool.rs.spectral_index" );
  REQUIRE( d );
  REQUIRE( d->host == HostKind::TaskPanel );
  REQUIRE( d->steps.size() == 1 );
  REQUIRE( d->steps[0].operatorId == "rs:spectral_index" );
  REQUIRE( d->steps[0].id == "run" );
  REQUIRE_FALSE( d->steps[0].gates.empty() );
  REQUIRE( d->steps[0].gates[0].require == "paramNonEmpty:run.input" );
}

TEST_CASE( "Builtin lab.classify.supervised has 7 steps in order", "[workflow]" )
{
  WorkflowRuntime rt;
  const auto *d = rt.findDefinition( "lab.classify.supervised" );
  REQUIRE( d );
  REQUIRE( d->host == HostKind::Workspace );
  REQUIRE( d->workspaceKind == "classify" );
  REQUIRE( d->title == "监督分类" );
  REQUIRE( d->steps.size() == 7 );

  const std::vector<std::string> expectedIds = {
    "classes", "samples", "evaluate", "train", "accuracy", "post", "export"
  };
  const std::vector<std::string> expectedTitles = {
    "分类体系", "样本", "样本评价", "训练-分类", "精度评定", "后处理", "输出"
  };
  const std::vector<StepKind> expectedKinds = {
    StepKind::Interactive,
    StepKind::Interactive,
    StepKind::Review,
    StepKind::Operator,
    StepKind::Review,
    StepKind::Interactive,
    StepKind::Review,
  };

  for ( size_t i = 0; i < expectedIds.size(); ++i )
  {
    REQUIRE( d->steps[i].id == expectedIds[i] );
    REQUIRE( d->steps[i].title == expectedTitles[i] );
    REQUIRE( d->steps[i].kind == expectedKinds[i] );
  }

  REQUIRE( d->steps[3].operatorId == "rs:supervised_classification" );
  REQUIRE( d->steps[3].artifactOnSuccess == "classified_output" );

  // Soft gates expressible via pure session artifacts.
  REQUIRE_FALSE( d->steps[1].gates.empty() );
  REQUIRE( d->steps[1].gates[0].require == "hasArtifact:source_raster" );
  REQUIRE_FALSE( d->steps[3].gates.empty() );
  REQUIRE( d->steps[3].gates[0].require == "hasArtifact:source_raster" );
  REQUIRE_FALSE( d->steps[4].gates.empty() );
  REQUIRE( d->steps[4].gates[0].require == "hasArtifact:classified_output" );

  const auto sid = rt.open( "lab.classify.supervised" );
  REQUIRE_FALSE( sid.empty() );
  REQUIRE( rt.state( sid ).currentStepId == "classes" );
  REQUIRE( rt.gotoStep( sid, "train" ) );
  REQUIRE( rt.state( sid ).currentStepId == "train" );
}

TEST_CASE( "Builtin lab.georef.image_to_map has 6 steps in order", "[workflow]" )
{
  WorkflowRuntime rt;
  const auto *d = rt.findDefinition( "lab.georef.image_to_map" );
  REQUIRE( d );
  REQUIRE( d->host == HostKind::Workspace );
  REQUIRE( d->workspaceKind == "georef" );
  REQUIRE( d->title == "几何校正（影像到地图）" );
  REQUIRE( d->steps.size() == 6 );

  const std::vector<std::string> expectedIds = {
    "open_image", "gcp", "transform", "residual", "warp", "load_result"
  };
  const std::vector<std::string> expectedTitles = {
    "打开影像", "控制点", "变换模型", "残差检查", "重采样写出", "加载结果"
  };
  const std::vector<StepKind> expectedKinds = {
    StepKind::Interactive,
    StepKind::Interactive,
    StepKind::Interactive,
    StepKind::Review,
    StepKind::Interactive,
    StepKind::Review,
  };

  for ( size_t i = 0; i < expectedIds.size(); ++i )
  {
    REQUIRE( d->steps[i].id == expectedIds[i] );
    REQUIRE( d->steps[i].title == expectedTitles[i] );
    REQUIRE( d->steps[i].kind == expectedKinds[i] );
  }

  // Soft gates on pure session artifacts.
  REQUIRE_FALSE( d->steps[1].gates.empty() );
  REQUIRE( d->steps[1].gates[0].require == "hasArtifact:source_raster" );
  REQUIRE_FALSE( d->steps[2].gates.empty() );
  REQUIRE( d->steps[2].gates[0].require == "hasArtifact:gcp_count" );
  REQUIRE_FALSE( d->steps[4].gates.empty() );
  REQUIRE( d->steps[4].gates[0].require == "hasArtifact:gcp_count" );
  REQUIRE_FALSE( d->steps[5].gates.empty() );
  REQUIRE( d->steps[5].gates[0].require == "hasArtifact:output" );

  const auto sid = rt.open( "lab.georef.image_to_map" );
  REQUIRE_FALSE( sid.empty() );
  REQUIRE( rt.state( sid ).currentStepId == "open_image" );

  // Soft gate: warp blocked without gcp_count; passes after mock artifact.
  REQUIRE( rt.gotoStep( sid, "warp" ) );
  auto can = rt.canRun( sid, "warp" );
  REQUIRE_FALSE( can.ok );
  rt.setArtifact( sid, "gcp_count", "4" );
  can = rt.canRun( sid, "warp" );
  REQUIRE( can.ok );
  REQUIRE( rt.gotoStep( sid, "residual" ) );
  REQUIRE( rt.state( sid ).currentStepId == "residual" );
}

TEST_CASE( "Builtin lab.obia has 5 steps in order", "[workflow]" )
{
  WorkflowRuntime rt;
  const auto *d = rt.findDefinition( "lab.obia" );
  REQUIRE( d );
  REQUIRE( d->host == HostKind::Workspace );
  REQUIRE( d->workspaceKind == "obia" );
  REQUIRE( d->title == "面向对象分类 (OBIA)" );
  REQUIRE( d->steps.size() == 5 );

  const std::vector<std::string> expectedIds = {
    "open_image", "segment", "label", "classify", "export"
  };
  const std::vector<std::string> expectedTitles = {
    "打开影像", "分割", "对象标注", "对象分类", "导出 / 精度 / 上主图"
  };
  const std::vector<StepKind> expectedKinds = {
    StepKind::Interactive,
    StepKind::Operator,
    StepKind::Interactive,
    StepKind::Operator,
    StepKind::Review,
  };

  for ( size_t i = 0; i < expectedIds.size(); ++i )
  {
    REQUIRE( d->steps[i].id == expectedIds[i] );
    REQUIRE( d->steps[i].title == expectedTitles[i] );
    REQUIRE( d->steps[i].kind == expectedKinds[i] );
  }

  REQUIRE( d->steps[1].operatorId == "rs:obia_segment" );
  REQUIRE( d->steps[1].artifactOnSuccess == "segment_map" );
  REQUIRE( d->steps[0].artifactOnSuccess == "source_raster" );
  REQUIRE( d->steps[3].operatorId == "rs:obia_classify" );
  REQUIRE( d->steps[3].artifactOnSuccess == "classified_output" );
  REQUIRE( d->steps[4].title == "导出 / 精度 / 上主图" );

  REQUIRE_FALSE( d->steps[1].gates.empty() );
  REQUIRE( d->steps[1].gates[0].require == "hasArtifact:source_raster" );
  REQUIRE_FALSE( d->steps[2].gates.empty() );
  REQUIRE( d->steps[2].gates[0].require == "hasArtifact:segment_map" );
  REQUIRE_FALSE( d->steps[4].gates.empty() );
  REQUIRE( d->steps[4].gates[0].require == "hasArtifact:classified_output" );

  const auto sid = rt.open( "lab.obia" );
  REQUIRE_FALSE( sid.empty() );
  REQUIRE( rt.state( sid ).currentStepId == "open_image" );

  REQUIRE( rt.gotoStep( sid, "segment" ) );
  auto can = rt.canRun( sid, "segment" );
  REQUIRE_FALSE( can.ok );
  rt.setArtifact( sid, "source_raster", "/tmp/demo.tif" );
  can = rt.canRun( sid, "segment" );
  REQUIRE( can.ok );
}

TEST_CASE( "Runtime open returns empty for missing definition", "[workflow]" )
{
  WorkflowRuntime rt( false );
  auto id = rt.open( "does.not.exist" );
  REQUIRE( id.empty() );
}

TEST_CASE( "Runtime open and canRun respects step gates", "[workflow]" )
{
  ensureTestAddRegistered();

  WorkflowRuntime rt( false );
  WorkflowDefinition d = makeTwoStep();
  d.steps[0].gates.push_back( {"paramNonEmpty:configure.a", "需要参数 a"} );
  rt.registerDefinition( d );

  auto id = rt.open( "tool.demo" );
  REQUIRE_FALSE( id.empty() );

  auto can = rt.canRun( id, "configure" );
  REQUIRE_FALSE( can.ok );

  Json::Value p;
  p["a"] = 1;
  p["b"] = 2;
  rt.setParams( id, "configure", p );
  can = rt.canRun( id, "configure" );
  REQUIRE( can.ok );
}

TEST_CASE( "Runtime run operator writes artifact", "[workflow]" )
{
  ensureTestAddRegistered();

  WorkflowRuntime rt( false );
  WorkflowDefinition d;
  d.id = "tool.add";
  d.title = "Add";
  d.host = HostKind::TaskPanel;
  StepDef step;
  step.id = "run";
  step.title = "Run";
  step.kind = StepKind::Operator;
  step.operatorId = "test:add";
  // no gates; TestAddOperator returns {"result": sum}
  d.steps = {step};
  rt.registerDefinition( d );

  auto id = rt.open( "tool.add" );
  REQUIRE_FALSE( id.empty() );

  Json::Value p;
  p["a"] = 2;
  p["b"] = 3;
  rt.setParams( id, "run", p );

  auto can = rt.canRun( id, "run" );
  REQUIRE( can.ok );

  Json::Value result = rt.runStep( id, "run" );
  REQUIRE( result.isMember( "result" ) );
  REQUIRE( result["result"].asDouble() == 5.0 );

  auto snap = rt.state( id );
  REQUIRE( snap.artifacts.count( "result" ) == 1 );
  REQUIRE_FALSE( snap.artifacts.at( "result" ).empty() );
  REQUIRE( snap.completedStepIds.size() == 1 );
  REQUIRE( snap.completedStepIds.front() == "run" );
}

TEST_CASE( "WorkflowSession resolveParams resolves placeholders using artifacts", "[workflow][placeholder]" )
{
  WorkflowDefinition d;
  d.id = "tool.pipeline";
  d.title = "Pipeline";
  d.host = HostKind::TaskPanel;

  StepDef step1;
  step1.id = "step1";
  step1.title = "Step 1";
  step1.kind = StepKind::Operator;
  step1.artifactOnSuccess = "raster_output";

  StepDef step2;
  step2.id = "step2";
  step2.title = "Step 2";
  step2.kind = StepKind::Operator;

  d.steps = {step1, step2};

  WorkflowSession sess( d, "sess-test" );
  sess.setArtifact( "raster_output", "/path/to/extracted.tif" );

  Json::Value rawParams;
  rawParams["input"] = "$step1.output";
  rawParams["mode"] = "fast";
  sess.setParams( "step2", rawParams );

  REQUIRE( sess.paramsFor( "step2" )["input"].asString() == "$step1.output" );

  Json::Value resolved = sess.resolveParams( "step2" );
  REQUIRE( resolved["input"].asString() == "/path/to/extracted.tif" );
  REQUIRE( resolved["mode"].asString() == "fast" );
}

TEST_CASE( "WorkflowSession pipelineId binding stores pipeline reference in snapshot", "[workflow][pipeline_id]" )
{
  WorkflowDefinition d = makeTwoStep();
  WorkflowSession session( d, "sess-pipe" );
  REQUIRE( session.pipelineId() == -1 );
  REQUIRE( session.snapshot().pipelineId == -1 );

  session.setPipelineId( 42 );
  REQUIRE( session.pipelineId() == 42 );
  REQUIRE( session.snapshot().pipelineId == 42 );

  SECTION( "snapshot enriches completedStepIds from pipeline resolver" )
  {
    // Without a resolver, no enrichment — snapshot only has local state
    REQUIRE( session.snapshot().completedStepIds.empty() );

    // Inject a mock resolver that reports "configure" as Completed
    session.setPipelineStatusResolver( []( long pipelineId ) {
      REQUIRE( pipelineId == 42 );
      return std::unordered_map<std::string, PipelineStepStatus>{
          { "configure", PipelineStepStatus::Completed },
          { "review", PipelineStepStatus::Running },
      };
    } );

    const auto snap = session.snapshot();
    // "configure" should be enriched into completedStepIds by the resolver
    REQUIRE( snap.completedStepIds.size() == 1 );
    REQUIRE( snap.completedStepIds[0] == "configure" );
    // "review" is still Running — should NOT appear in completedStepIds
  }

  SECTION( "resolver enrichment does not duplicate locally completed steps" )
  {
    session.markStepComplete( "configure" );
    REQUIRE( session.snapshot().completedStepIds.size() == 1 );

    // Resolver also reports "configure" as Completed — should NOT duplicate
    session.setPipelineStatusResolver( []( long ) {
      return std::unordered_map<std::string, PipelineStepStatus>{
          { "configure", PipelineStepStatus::Completed },
      };
    } );

    const auto snap = session.snapshot();
    REQUIRE( snap.completedStepIds.size() == 1 );
    REQUIRE( snap.completedStepIds[0] == "configure" );
  }
}


TEST_CASE( "Preprocessing DAG chains operators with artifact flow", "[workflow][preprocess]" )
{
  WorkflowRuntime rt;
  const auto *d = rt.findDefinition( "lab.preprocess.optical" );
  REQUIRE( d != nullptr );
  REQUIRE( d->steps.size() == 4 );

  CHECK( d->steps[0].operatorId == "rs:radiometric_calibration" );
  CHECK( d->steps[1].operatorId == "rs:qa_mask" );
  CHECK( d->steps[2].operatorId == "rs:atmospheric_correction" );
  CHECK( d->steps[3].operatorId == "rs:spectral_index" );

  // Downstream steps consume upstream artifacts via placeholders (ADR 0016).
  CHECK( d->steps[1].params["input"].asString() == "$calibration.calibrated" );
  CHECK( d->steps[2].params["input"].asString() == "$calibration.calibrated" );
  CHECK( d->steps[3].params["input"].asString() == "$atmospheric.corrected" );

  // The NDVI step omits band numbers: resolved from product semantic band
  // roles (ADR 0065) when the raster carries SICNU_BAND_ROLE metadata.
  CHECK_FALSE( d->steps[3].params.isMember( "nir" ) );
  CHECK( d->steps[3].params["index"].asString() == "NDVI" );
}
