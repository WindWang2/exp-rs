// test_lab_document.cpp — LabDocument normalization over D2 + D3 lab formats
#include <catch2/catch_test_macros.hpp>

#include "recipes/lab_document.h"
#include "recipes/lab_source.h"

#include <filesystem>
#include <fstream>

using namespace sicnu::recipes;
namespace fs = std::filesystem;

namespace {

const char *kD2 = R"({
  "spec_version": 2,
  "id": "lab02_spectral_analysis",
  "title": "Spectral Analysis",
  "title_zh": "光谱指数与波段运算",
  "objective": "掌握光谱特征分析方法。",
  "objective_zh": "中文目标",
  "prerequisite_knowledge": ["lab01_image_enhancement"],
  "glossary": [{"term": "NDVI", "term_zh": "归一化植被指数", "definition_zh": "…"}],
  "param_ranges": {"rs:spectral_index": {"red": {"min": 1, "max": 7}}},
  "expected_artifacts": [{"path": "outputs/lab02_ndvi.tif", "kind": "raster", "note_zh": "…"}],
  "prerequisites": [{"path": "data/samples/landsat_sample.tif", "note": "样本影像"}],
  "steps": [
    {"title": "Load", "title_zh": "加载", "description_zh": "…", "action": "addRasterLayer",
     "completion_hint": "影像已加载。"},
    {"title": "Calc NDVI", "title_zh": "计算", "description_zh": "…",
     "operator_id": "rs:spectral_index",
     "params": {"input": "data/samples/landsat_sample.tif", "output": "outputs/lab02_ndvi.tif",
                "index": "NDVI", "red": 4, "nir": 5},
     "teaching_note": "NDVI ∈ [-1,1]。", "completion_hint": "植被高值。"}
  ],
  "grading_ref": {"pipeline": "data/pipelines/landsat_ndvi.json"},
  "grading_rules": "data/labs/grading/ndvi_basics.rules.json",
  "thinking_questions": ["为什么植被 NIR 高？"]
})";

const char *kD3 = R"({
  "schema": "sicnu.labspec.v1",
  "id": "lab9_sar_processing",
  "version": 1,
  "title": "SAR Processing",
  "theme": "sar",
  "audience": "本科生",
  "duration_minutes": 120,
  "prerequisites": ["lab4_change_detection"],
  "objectives": ["理解相干斑", "掌握 Lee 滤波"],
  "principles": [{"heading": "相干斑统计", "body": "…"}],
  "data": {"spec_ref": "data/labs/data-specs/lab9.json", "offline": true, "description": "VV 影像对"},
  "pipeline": {"ref": "data/labs/pipelines/lab9.pipeline.json", "runner": "cli --pipeline …"},
  "operators": [{"operator_id": "rs:sar_calibrate", "role": "DN→σ0"}],
  "steps": [
    {"id": "s1", "title": "辐射定标", "detail": "A=100", "operator_id": "rs:sar_calibrate",
     "headless_note": "pipeline 步骤 cal_before"}
  ],
  "grading_ref": {"intent_ref": "data/labs/grading/lab9.intent.json", "grader_owner": "D4"},
  "expected_results": [{"claim": "水体 σ0≈0.006", "artifact": "out/sigma0.tif", "tolerance_note": "S1"}],
  "questions": [{"prompt": "为什么乘性噪声？", "hint": "相干叠加"}],
  "glossary": [{"term": "speckle", "term_zh": "相干斑", "definition_zh": "…"}]
})";

} // namespace

TEST_CASE( "D2 lab parses into normalized document", "[labdoc]" )
{
  LabDocument doc;
  LabDocumentError err;
  REQUIRE( parseLabDocument( kD2, "lab02_spectral_analysis.lab.json", doc, &err ) );

  REQUIRE( doc.format == LabFormat::D2 );
  REQUIRE( doc.specVersion == 2 );
  REQUIRE( doc.id == "lab02_spectral_analysis" );
  REQUIRE( doc.titleZh == "光谱指数与波段运算" );
  REQUIRE( doc.prerequisites.size() == 1 );
  REQUIRE( doc.prerequisites[0].path == "data/samples/landsat_sample.tif" );
  REQUIRE( doc.steps.size() == 2 );
  REQUIRE( doc.steps[0].isUiAction() );
  REQUIRE( doc.steps[1].hasOperator() );
  REQUIRE( doc.steps[1].params["index"].asString() == "NDVI" );
  REQUIRE( doc.steps[1].params["red"].asInt() == 4 );
  REQUIRE( doc.expectedArtifacts.size() == 1 );
  REQUIRE( doc.expectedArtifacts[0].kind == "raster" );
  REQUIRE( doc.gradingRules == "data/labs/grading/ndvi_basics.rules.json" );
  REQUIRE( doc.gradingPipeline == "data/pipelines/landsat_ndvi.json" );
  REQUIRE( doc.questions.size() == 1 );
  REQUIRE( doc.questions[0].prompt == "为什么植被 NIR 高？" );
  REQUIRE( doc.glossaryTerms == std::vector<std::string>{ "NDVI" } );
  REQUIRE( doc.paramRanges.isMember( "rs:spectral_index" ) );
  REQUIRE( doc.prerequisiteKnowledge.size() == 1 );
  REQUIRE( !doc.sourceFingerprint.empty() );
}

TEST_CASE( "D3 labspec parses into normalized document", "[labdoc]" )
{
  LabDocument doc;
  LabDocumentError err;
  REQUIRE( parseLabDocument( kD3, "lab9_sar_processing.labspec.json", doc, &err ) );

  REQUIRE( doc.format == LabFormat::D3 );
  REQUIRE( doc.schemaTag == "sicnu.labspec.v1" );
  REQUIRE( doc.id == "lab9_sar_processing" );
  REQUIRE( doc.durationMinutes == 120 );
  REQUIRE( doc.dataSpecRef == "data/labs/data-specs/lab9.json" );
  REQUIRE( doc.prerequisites.size() == 1 ); // data.spec_ref folds into one asset
  REQUIRE( doc.steps.size() == 1 );
  REQUIRE( doc.steps[0].id == "s1" );
  REQUIRE( doc.steps[0].operatorId == "rs:sar_calibrate" );
  REQUIRE( doc.steps[0].headlessNote.find( "cal_before" ) != std::string::npos );
  REQUIRE( doc.gradingPipeline == "data/labs/pipelines/lab9.pipeline.json" );
  REQUIRE( doc.gradingIntentRef == "data/labs/grading/lab9.intent.json" );
  REQUIRE( doc.expectedResults.size() == 1 );
  REQUIRE( doc.expectedResults[0].claim.find( "σ0" ) != std::string::npos );
  REQUIRE( doc.questions.size() == 1 );
  REQUIRE( doc.questions[0].hint == "相干叠加" );
  REQUIRE( doc.operatorRoles.size() == 1 );
  REQUIRE( doc.operatorRoles[0].find( "rs:sar_calibrate" ) == 0 );
}

TEST_CASE( "Unrecognized/malformed input fails typed, never crashes", "[labdoc]" )
{
  LabDocument doc;
  LabDocumentError err;

  REQUIRE( !parseLabDocument( "{broken", "x.json", doc, &err ) );
  REQUIRE( err.reason.find( "invalid JSON" ) != std::string::npos );

  REQUIRE( !parseLabDocument( "[1,2]", "x.json", doc, &err ) );
  REQUIRE( err.reason.find( "object" ) != std::string::npos );

  REQUIRE( !parseLabDocument( R"({"foo": 1})", "x.json", doc, &err ) );
  REQUIRE( err.reason.find( "unrecognized" ) != std::string::npos );

  REQUIRE( !parseLabDocument( R"({"schema": "other/9"})", "x.json", doc, &err ) );
  REQUIRE( err.reason.find( "unrecognized schema" ) != std::string::npos );

  REQUIRE( !parseLabDocument( R"({"spec_version": 9, "id": "x"})", "x.json", doc, &err ) );
  REQUIRE( err.reason.find( "spec_version" ) != std::string::npos );

  // D2 without id → typed failure.
  REQUIRE( !parseLabDocument( R"({"spec_version": 2, "title": "t"})", "x.json", doc, &err ) );
  REQUIRE( err.reason.find( "id" ) != std::string::npos );
}

TEST_CASE( "Step-less D2 wrapper parses (steps empty, no error)", "[labdoc]" )
{
  LabDocument doc;
  LabDocumentError err;
  REQUIRE( parseLabDocument( R"({"spec_version":2,"id":"lab12_sar_processing",
    "title":"SAR","title_zh":"SAR","objective":"o"})",
                             "lab12_sar_processing.lab.json", doc, &err ) );
  REQUIRE( doc.steps.empty() );
  REQUIRE( doc.id == "lab12_sar_processing" );
}

TEST_CASE( "fnv1a64Hex is deterministic and 16-char", "[labdoc]" )
{
  const std::string a = fnv1a64Hex( "hello" );
  REQUIRE( a.size() == 16 );
  REQUIRE( a == fnv1a64Hex( "hello" ) );
  REQUIRE( a != fnv1a64Hex( "hellp" ) );
}
