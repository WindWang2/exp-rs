// test_recipe_compiler.cpp — LabToRecipeCompiler contract tests
#include <catch2/catch_test_macros.hpp>

#include "recipes/lab_document.h"
#include "recipes/provider_interfaces.h"
#include "recipes/recipe_compiler.h"
#include "recipes/scientific_recipe.h"

#include <set>

using namespace sicnu::recipes;

namespace {

LabDocument parseOrFail( const std::string &text )
{
  LabDocument doc;
  LabDocumentError err;
  REQUIRE( parseLabDocument( text, "mem.lab.json", doc, &err ) );
  return doc;
}

FakeOperatorCatalog makeOps()
{
  FakeOperatorCatalog ops;
  ops.add( "rs:spectral_index", { "input", "output", "index", "red", "nir" } );
  ops.add( "rs:band_math", { "input", "output", "expression" } );
  ops.add( "rs:sar_calibrate" );
  ops.add( "rs:contrast_stretch" );
  return ops;
}

const char *kLab02 = R"({
  "spec_version": 2, "id": "lab02_spectral_analysis", "title": "Spectral Analysis",
  "title_zh": "光谱指数与波段运算",
  "objective": "掌握光谱特征分析方法。",
  "prerequisite_knowledge": ["lab01_image_enhancement"],
  "glossary": [{"term":"NDVI","term_zh":"归一化植被指数","definition_zh":"…"}],
  "param_ranges": {
    "rs:spectral_index": {"red": {"min":1,"max":7}, "index": {"values":["NDVI"]}},
    "rs:band_math": {"expression": {"values": ["b5 / b4", "b4 / b5"]}}
  },
  "expected_artifacts": [{"path":"outputs/lab02_ndvi.tif","kind":"raster"}],
  "prerequisites": [{"path": "data/samples/landsat_sample.tif", "note": "样本"}],
  "steps": [
    {"title":"Load Sample Data","title_zh":"加载","description_zh":"…",
     "action":"addRasterLayer","completion_hint":"影像已加载。"},
    {"title":"Examine Spectral Profiles","title_zh":"观察光谱","description_zh":"…",
     "action":"identifyFeatures","teaching_note":"光谱签名。","completion_hint":"能看到曲线。"},
    {"title":"Calculate NDVI","title_zh":"计算 NDVI","description_zh":"…",
     "operator_id":"rs:spectral_index",
     "params":{"input":"data/samples/landsat_sample.tif","output":"outputs/lab02_ndvi.tif",
               "index":"NDVI","red":4,"nir":5},
     "teaching_note":"NDVI ∈ [-1,1]。","completion_hint":"植被高值。"},
    {"title":"Custom Band Ratio","title_zh":"比值","description_zh":"…",
     "operator_id":"rs:band_math",
     "params":{"input":"data/samples/landsat_sample.tif","output":"outputs/ratio.tif",
               "expression":"b5 / b4"},
     "completion_hint":"比值高值。"}
  ],
  "grading_ref": {"pipeline":"data/pipelines/landsat_ndvi.json"},
  "grading_rules": "data/labs/grading/ndvi_basics.rules.json",
  "thinking_questions": ["为什么植被 NIR 高？"]
})";

} // namespace

TEST_CASE( "Happy path: lab02 compiles to a full ScientificRecipe", "[compiler]" )
{
  const FakeOperatorCatalog ops = makeOps();
  const CompileResult result = compileLabToRecipe( parseOrFail( kLab02 ), ops );
  const Json::Value &r = result.recipe;

  REQUIRE( r["schema"].asString() == "sicnu.scientific_recipe/1" );
  REQUIRE( r["recipe_id"].asString() == "lab.lab02_spectral_analysis" );
  REQUIRE( r["goal_pattern"]["intent"].asString() == "spectral_analysis" );
  REQUIRE( r["goal_pattern"]["modality"].asString() == "optical" );

  // teaching origin
  const Json::Value &origin = r["teaching_origin"];
  REQUIRE( origin["kind"].asString() == "labspec_d2" );
  REQUIRE( origin["lab_id"].asString() == "lab02_spectral_analysis" );
  REQUIRE( origin["spec_version"].asInt() == 2 );
  REQUIRE( origin["compiled_by"].asString() == "sicnu.lab2recipe/1" );
  REQUIRE( !origin["source_fingerprint"].asString().empty() );
  REQUIRE( origin["glossary_terms"][0].asString() == "NDVI" );
  REQUIRE( origin["prerequisite_knowledge"][0].asString() == "lab01_image_enhancement" );

  // assets
  REQUIRE( r["required_assets"].size() == 1 );
  REQUIRE( r["required_assets"][0]["predicate"]["path"].asString() ==
           "data/samples/landsat_sample.tif" );

  // stages: 4 steps + 1 reflection
  REQUIRE( r["stages"].size() == 5 );
  const Json::Value &s1 = r["stages"][0];
  REQUIRE( s1["kind"].asString() == "human_only" );
  REQUIRE( s1["boundary"].asString() == "ui_action" );
  REQUIRE( s1["action"].asString() == "addRasterLayer" );
  REQUIRE( s1["human_only"].asBool() );

  const Json::Value &s3 = r["stages"][2];
  REQUIRE( s3["kind"].asString() == "operator" );
  REQUIRE( s3["operator_id"].asString() == "rs:spectral_index" );
  REQUIRE( s3["params"]["red"].asInt() == 4 );
  REQUIRE( s3["depends_on"][0].asString() == r["stages"][1]["id"].asString() );

  // artifact_exists hook wired to the declared expected artifact
  bool foundArtifactHook = false;
  for ( const auto &h : s3["verifier_hooks"] )
    if ( h["kind"].asString() == "artifact_exists" &&
         h["target"].asString() == "outputs/lab02_ndvi.tif" )
    {
      foundArtifactHook = true;
      REQUIRE( h["artifact_kind"].asString() == "raster" );
    }
  REQUIRE( foundArtifactHook );

  // completion_hint hook present on operator stage
  bool foundHint = false;
  for ( const auto &h : s3["verifier_hooks"] )
    if ( h["kind"].asString() == "completion_hint" )
      foundHint = true;
  REQUIRE( foundHint );

  // reflection stage last
  const Json::Value &last = r["stages"][4];
  REQUIRE( last["kind"].asString() == "reflection" );
  REQUIRE( last["human_only"].asBool() );
  REQUIRE( last["prompt"].asString() == "为什么植被 NIR 高？" );

  // preflight
  const Json::Value &pf = r["preflight"];
  std::set<std::string> requiredOps;
  for ( const auto &o : pf["required_operators"] )
    requiredOps.insert( o.asString() );
  REQUIRE( requiredOps == std::set<std::string>{ "rs:band_math", "rs:spectral_index" } );
  REQUIRE( pf["required_assets"][0].asString() == "data/samples/landsat_sample.tif" );
  REQUIRE( pf["param_bounds"]["rs:spectral_index"]["red"]["max"].asInt() == 7 );

  // alternatives: only multi-value param_ranges entries
  bool foundExprAlt = false;
  for ( const auto &a : r["alternatives"] )
    if ( a["param"].asString() == "expression" )
    {
      foundExprAlt = true;
      REQUIRE( a["choices"].size() == 2 );
      REQUIRE( a["operator_id"].asString() == "rs:band_math" );
    }
  REQUIRE( foundExprAlt );

  // evidence
  REQUIRE( r["evidence"]["grading_rules"].asString() ==
           "data/labs/grading/ndvi_basics.rules.json" );
  REQUIRE( r["evidence"]["grading_pipeline"].asString() ==
           "data/pipelines/landsat_ndvi.json" );
  REQUIRE( r["evidence"]["artifacts"].size() == 1 );

  // compilation stats + diagnostics: two ui_action info markers, nothing else
  REQUIRE( r["compilation"]["stages_operator"].asInt() == 2 );
  REQUIRE( r["compilation"]["stages_human_only"].asInt() == 2 );
  REQUIRE( r["compilation"]["stages_reflection"].asInt() == 1 );
  REQUIRE( r["compilation"]["coverage"].asDouble() == 0.5 );
  REQUIRE( result.diagnostics.size() == 2 );
  for ( const auto &d : result.diagnostics )
  {
    REQUIRE( d.code == diag_codes::kUiActionStep );
    REQUIRE( d.severity == DiagnosticSeverity::Info );
  }
}

TEST_CASE( "Unknown operator is a typed warning, stage flagged", "[compiler]" )
{
  FakeOperatorCatalog ops; // empty catalog — nothing known
  const CompileResult result = compileLabToRecipe( parseOrFail( kLab02 ), ops );

  int unknownCount = 0;
  for ( const auto &d : result.diagnostics )
    if ( d.code == diag_codes::kUnknownOperator )
      ++unknownCount;
  REQUIRE( unknownCount == 2 );

  for ( const auto &s : result.recipe["stages"] )
    if ( s["kind"].asString() == "operator" )
      REQUIRE( s["operator_known"].asBool() == false );
  // Recipe still emitted — typed degradation, not silent loss.
  REQUIRE( result.recipe["stages"].size() == 5 );
}

TEST_CASE( "param_ranges violation produces param_out_of_range warning", "[compiler]" )
{
  std::string doc = kLab02;
  const std::string needle = "\"red\":4";
  doc.replace( doc.find( needle ), needle.size(), "\"red\":99" );
  const CompileResult result = compileLabToRecipe( parseOrFail( doc ), makeOps() );

  bool found = false;
  for ( const auto &d : result.diagnostics )
    if ( d.code == diag_codes::kParamOutOfRange && d.field == "params.red" )
      found = true;
  REQUIRE( found );
}

TEST_CASE( "All-manual lab compiles with zero operator stages + diagnostics", "[compiler]" )
{
  const LabDocument lab = parseOrFail( R"({
    "spec_version": 1, "id": "lab06_georeferencing", "title": "Georef",
    "title_zh": "配准", "objective": "o",
    "steps": [
      {"title":"Pick GCPs","title_zh":"选点","description_zh":"…"},
      {"title":"Review","title_zh":"检查","description_zh":"…"}
    ]
  })" );
  const CompileResult result = compileLabToRecipe( lab, makeOps() );

  REQUIRE( result.recipe["compilation"]["stages_operator"].asInt() == 0 );
  REQUIRE( result.recipe["compilation"]["stages_human_only"].asInt() == 2 );
  REQUIRE( result.recipe["compilation"]["coverage"].asDouble() == 0.0 );
  int manual = 0;
  for ( const auto &d : result.diagnostics )
    if ( d.code == diag_codes::kManualStep )
      ++manual;
  REQUIRE( manual == 2 );
}

TEST_CASE( "Step-less document emits no_steps error diagnostic", "[compiler]" )
{
  const LabDocument lab = parseOrFail( R"({
    "spec_version": 2, "id": "lab12_sar_processing", "title": "SAR",
    "title_zh": "SAR", "objective": "o"
  })" );
  const CompileResult result = compileLabToRecipe( lab, makeOps() );
  REQUIRE( !result.ok() );
  bool found = false;
  for ( const auto &d : result.diagnostics )
    if ( d.code == diag_codes::kNoSteps && d.severity == DiagnosticSeverity::Error )
      found = true;
  REQUIRE( found );
  // Recipe still emitted as an explicit stub with teaching origin intact.
  REQUIRE( result.recipe["teaching_origin"]["lab_id"].asString() == "lab12_sar_processing" );
  REQUIRE( result.recipe["stages"].empty() );
}

TEST_CASE( "D3 lab compiles with expected_results → evidence claims", "[compiler]" )
{
  const LabDocument lab = parseOrFail( R"({
    "schema": "sicnu.labspec.v1", "id": "lab9_sar_processing", "version": 1,
    "title": "SAR 实验", "theme": "sar", "duration_minutes": 120,
    "objectives": ["理解相干斑"], "principles": [{"heading": "相干斑"}],
    "data": {"spec_ref": "data/spec.json"},
    "pipeline": {"ref": "data/pipelines/lab9.pipeline.json"},
    "operators": [{"operator_id": "rs:sar_calibrate", "role": "DN→σ0"}],
    "steps": [{"id": "s1", "title": "定标", "operator_id": "rs:sar_calibrate",
               "headless_note": "cal_before"}],
    "expected_results": [{"claim": "σ0≈0.006", "artifact": "out.tif", "tolerance_note": "S1"}],
    "questions": [{"prompt": "为什么乘性？", "hint": "相干叠加"}],
    "glossary": [{"term": "speckle"}]
  })" );
  const CompileResult result = compileLabToRecipe( lab, makeOps() );
  const Json::Value &r = result.recipe;

  REQUIRE( r["teaching_origin"]["kind"].asString() == "labspec_d3" );
  REQUIRE( r["teaching_origin"]["source_schema"].asString() == "sicnu.labspec.v1" );
  REQUIRE( r["goal_pattern"]["modality"].asString() == "sar" );
  REQUIRE( r["stages"][0]["kind"].asString() == "operator" );
  REQUIRE( r["stages"][0]["headless_note"].asString() == "cal_before" );
  REQUIRE( r["stages"][1]["kind"].asString() == "reflection" );
  REQUIRE( r["stages"][1]["hint"].asString() == "相干叠加" );
  REQUIRE( r["evidence"]["claims"][0]["claim"].asString() == "σ0≈0.006" );
  REQUIRE( r["evidence"]["grading_pipeline"].asString() ==
           "data/pipelines/lab9.pipeline.json" );
  REQUIRE( r["evidence"]["data_spec"].asString() == "data/spec.json" );
  // operator_roles feed preflight even without inline steps using the id
  bool calibrateListed = false;
  for ( const auto &o : r["preflight"]["required_operators"] )
    if ( o.asString() == "rs:sar_calibrate" )
      calibrateListed = true;
  REQUIRE( calibrateListed );
}

TEST_CASE( "Compilation is deterministic: identical input → identical bytes", "[compiler]" )
{
  const LabDocument lab = parseOrFail( kLab02 );
  const FakeOperatorCatalog ops = makeOps();
  const CompileResult a = compileLabToRecipe( lab, ops );
  const CompileResult b = compileLabToRecipe( lab, ops );
  REQUIRE( serializeRecipe( a.recipe ) == serializeRecipe( b.recipe ) );
}

TEST_CASE( "params stay verbatim — data/ and outputs/ paths unresolved", "[compiler]" )
{
  const CompileResult result = compileLabToRecipe( parseOrFail( kLab02 ), makeOps() );
  const Json::Value &params = result.recipe["stages"][2]["params"];
  REQUIRE( params["input"].asString() == "data/samples/landsat_sample.tif" );
  REQUIRE( params["output"].asString() == "outputs/lab02_ndvi.tif" );
}
