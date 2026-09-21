/***************************************************************************
  tests/test_lab_runtime.cpp
  LabSpec v3 runtime contract suite (rs14-labspec2-runtime).

  Qt-free on purpose: this target links only Catch2 + sicnu_lab_runtime
  (+ jsoncpp transitively). If the lab runtime ever grows a Qt dependency
  this executable stops compiling — the layer guard is the link graph.

  Slice A: runtime block parsing/typed validation + spec fingerprint.
***************************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <json/json.h>

#include "lab/spec_runtime.h"

#include <functional>
#include <sstream>
#include <string>

namespace
{

Json::Value parseJson( const std::string &text )
{
  Json::Value doc;
  Json::CharReaderBuilder builder;
  builder[ "stackLimit" ] = 64;
  std::string errors;
  std::istringstream stream( text );
  [[maybe_unused]] const bool ok = Json::parseFromStream( builder, stream, &doc, &errors );
  REQUIRE( ok );
  return doc;
}

/// A minimal, fully valid v3 lab document (steps + runtime block).
std::string validV3Doc()
{
  return R"JSON({
    "spec_version": 3,
    "id": "lab90_runtime_probe",
    "title": "Runtime Probe",
    "title_zh": "运行时探针",
    "objective": "probe",
    "steps": [
      { "title": "s0", "title_zh": "第0步", "description_zh": "d" },
      { "title": "s1", "title_zh": "第1步", "description_zh": "d", "operator_id": "rs:clip" },
      { "title": "s2", "title_zh": "第2步", "description_zh": "d", "operator_id": "rs:ndvi" }
    ],
    "runtime": {
      "data_packs": [ "lab90_runtime_probe" ],
      "stages": [
        {
          "id": "s1_preprocess",
          "title": "Preprocess",
          "title_zh": "预处理",
          "objective": "prepare data",
          "objective_zh": "准备数据",
          "step_indices": [ 0, 1 ],
          "allowed_tools": [ "rs:clip", "rs:pre*" ],
          "checkpoints": [
            {
              "id": "ckpt_clip",
              "title": "Clip done",
              "title_zh": "裁剪完成",
              "checks": [
                { "kind": "artifact_present", "path": "outputs/labs/lab90/clip.tif", "min_bytes": 1,
                  "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
                { "kind": "operator_invoked", "operator_id": "rs:clip", "params_subset": { "bands": 3 } },
                { "kind": "question_answered", "question_id": "q_extent" }
              ],
              "attempts_allowed": 3,
              "advance": "gate"
            }
          ]
        },
        {
          "id": "s2_index",
          "title": "Index",
          "title_zh": "指数",
          "objective": "compute ndvi",
          "objective_zh": "计算 NDVI",
          "step_indices": [ 2 ],
          "checkpoints": [
            {
              "id": "ckpt_ndvi",
              "title": "NDVI done",
              "title_zh": "NDVI 完成",
              "checks": [ { "kind": "operator_invoked", "operator_id_any": [ "rs:ndvi", "opencv:ndvi" ] } ]
            }
          ]
        }
      ],
      "questions": [
        { "id": "q_extent", "prompt": "What extent?", "prompt_zh": "范围是什么？", "kind": "free_text" },
        { "id": "q_ndvi", "prompt": "NDVI range?", "prompt_zh": "NDVI 范围？", "kind": "numeric",
          "expected_numeric": { "min": -1.0, "max": 1.0 } },
        { "id": "q_choice", "prompt": "Pick", "prompt_zh": "选择", "kind": "choice",
          "choices": [ "a", "b" ] }
      ],
      "hints": {
        "policy": { "max_reveals_per_target": 3, "escalation": [ "nudge", "hint", "worked_example" ] },
        "entries": [
          { "target_step": 1, "level": 1, "text": "look at extent", "text_zh": "看范围" },
          { "target_checkpoint": "ckpt_clip", "level": 2, "text": "bands first" }
        ]
      },
      "reproducibility": { "require_seed": true, "deterministic_operators_only": true, "notes": "seeded" }
    }
  })JSON";
}

std::string mutated( const std::function<void( Json::Value & )> &mutator )
{
  Json::Value doc = parseJson( validV3Doc() );
  mutator( doc );
  Json::StreamWriterBuilder writer;
  writer[ "indentation" ] = "";
  return Json::writeString( writer, doc );
}

std::string firstDiagCode( const sicnu::lab::LabResult<sicnu::lab::LabRuntimePlan> &result )
{
  REQUIRE( !result.ok );
  REQUIRE( !result.diagnostics.empty() );
  return result.diagnostics.front().code;
}

} // namespace

TEST_CASE( "runtime block parses on a valid v3 document", "[lab_runtime][spec]" )
{
  const Json::Value doc = parseJson( validV3Doc() );
  REQUIRE( sicnu::lab::hasRuntimeBlock( doc ) );

  const auto result = sicnu::lab::parseRuntimeBlock( doc );
  REQUIRE( result.ok );
  const sicnu::lab::LabRuntimePlan &plan = result.value;

  REQUIRE( plan.dataPacks.size() == 1 );
  REQUIRE( plan.dataPacks[0] == "lab90_runtime_probe" );

  REQUIRE( plan.stages.size() == 2 );
  REQUIRE( plan.stages[0].id == "s1_preprocess" );
  REQUIRE( plan.stages[0].stepIndices == std::vector<int>{ 0, 1 } );
  REQUIRE( plan.stages[0].allowedTools == std::vector<std::string>{ "rs:clip", "rs:pre*" } );
  REQUIRE( plan.stages[1].id == "s2_index" );
  REQUIRE( plan.stages[1].allowedTools.empty() );

  REQUIRE( plan.stages[0].checkpoints.size() == 1 );
  const sicnu::lab::Checkpoint &ckpt = plan.stages[0].checkpoints[0];
  REQUIRE( ckpt.id == "ckpt_clip" );
  REQUIRE( ckpt.checks.size() == 3 );
  REQUIRE( ckpt.checks[0].kind == sicnu::lab::CheckKind::ArtifactPresent );
  REQUIRE( ckpt.checks[0].path == "outputs/labs/lab90/clip.tif" );
  REQUIRE( ckpt.checks[0].minBytes == 1 );
  REQUIRE( ckpt.checks[0].sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
  REQUIRE( ckpt.checks[1].kind == sicnu::lab::CheckKind::OperatorInvoked );
  REQUIRE( ckpt.checks[1].operatorId == "rs:clip" );
  REQUIRE( ckpt.checks[1].paramsSubset.at( "bands" ) == "3" );
  REQUIRE( ckpt.checks[2].kind == sicnu::lab::CheckKind::QuestionAnswered );
  REQUIRE( ckpt.checks[2].questionId == "q_extent" );
  REQUIRE( ckpt.attemptsAllowed == 3 );
  REQUIRE( ckpt.advance == sicnu::lab::AdvancePolicy::Gate );

  REQUIRE( plan.stages[1].checkpoints[0].checks[0].operatorIdAny ==
           std::vector<std::string>{ "rs:ndvi", "opencv:ndvi" } );
  REQUIRE( plan.stages[1].checkpoints[0].advance == sicnu::lab::AdvancePolicy::Observe );
  REQUIRE( plan.stages[1].checkpoints[0].attemptsAllowed == 0 );

  REQUIRE( plan.questions.size() == 3 );
  REQUIRE( plan.questions[1].kind == sicnu::lab::QuestionKind::Numeric );
  REQUIRE( plan.questions[1].hasExpectedNumeric );
  REQUIRE( plan.questions[1].expectedMin == -1.0 );
  REQUIRE( plan.questions[1].expectedMax == 1.0 );
  REQUIRE( plan.questions[2].choices == std::vector<std::string>{ "a", "b" } );

  REQUIRE( plan.hints.policy.maxRevealsPerTarget == 3 );
  REQUIRE( plan.hints.policy.escalation ==
           std::vector<std::string>{ "nudge", "hint", "worked_example" } );
  REQUIRE( plan.hints.entries.size() == 2 );
  REQUIRE( plan.hints.entries[0].hasStep );
  REQUIRE( plan.hints.entries[0].targetStep == 1 );
  REQUIRE( plan.hints.entries[1].hasCheckpoint );
  REQUIRE( plan.hints.entries[1].targetCheckpoint == "ckpt_clip" );
  REQUIRE( plan.hints.entries[1].textZh.empty() );

  REQUIRE( plan.reproducibility.requireSeed );
  REQUIRE( plan.reproducibility.deterministicOperatorsOnly );
}

TEST_CASE( "runtime block typed validation rejects malformed documents", "[lab_runtime][spec][negative]" )
{
  SECTION( "absent runtime block" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) { d.removeMember( "runtime" ); } ) );
    REQUIRE( !sicnu::lab::hasRuntimeBlock( doc ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.schema" );
  }
  SECTION( "runtime not an object" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) { d[ "runtime" ] = "nope"; } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.schema" );
  }
  SECTION( "unknown top-level runtime key" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) { d[ "runtime" ][ "magic" ] = 1; } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.schema" );
  }
  SECTION( "empty stages" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) { d[ "runtime" ][ "stages" ] = Json::arrayValue; } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "bad stage id pattern" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 0 ][ "id" ] = "Bad-Stage";
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "duplicate stage id" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 1 ][ "id" ] = "s1_preprocess";
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "step index out of range" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 0 ][ "step_indices" ][ 0 ] = 7;
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "step indices descending" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 0 ][ "step_indices" ] = Json::arrayValue;
      d[ "runtime" ][ "stages" ][ 0 ][ "step_indices" ][ 0 ] = 1;
      d[ "runtime" ][ "stages" ][ 0 ][ "step_indices" ][ 1 ] = 0;
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "overlapping step indices across stages" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 1 ][ "step_indices" ] = Json::arrayValue;
      d[ "runtime" ][ "stages" ][ 1 ][ "step_indices" ][ 0 ] = 1;
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "checkpoint with empty checks" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 0 ][ "checkpoints" ][ 0 ][ "checks" ] = Json::arrayValue;
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "duplicate checkpoint id" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 1 ][ "checkpoints" ][ 0 ][ "id" ] = "ckpt_clip";
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "unknown check kind" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 0 ][ "checkpoints" ][ 0 ][ "checks" ][ 0 ][ "kind" ] = "vibes";
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "operator_invoked with both operator_id and operator_id_any" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      Json::Value &check = d[ "runtime" ][ "stages" ][ 1 ][ "checkpoints" ][ 0 ][ "checks" ][ 0 ];
      check[ "operator_id" ] = "rs:ndvi";
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "artifact path escapes the root" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 0 ][ "checkpoints" ][ 0 ][ "checks" ][ 0 ][ "path" ] =
        "outputs/../../etc/passwd";
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "artifact path absolute" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 0 ][ "checkpoints" ][ 0 ][ "checks" ][ 0 ][ "path" ] = "/etc/passwd";
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "artifact sha256 not 64 lowercase hex" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 0 ][ "checkpoints" ][ 0 ][ "checks" ][ 0 ][ "sha256" ] = "XYZ";
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "question_answered references unknown question" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 0 ][ "checkpoints" ][ 0 ][ "checks" ][ 2 ][ "question_id" ] = "q_ghost";
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.reference" );
  }
  SECTION( "hint entry references unknown checkpoint" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "hints" ][ "entries" ][ 1 ][ "target_checkpoint" ] = "ckpt_ghost";
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.reference" );
  }
  SECTION( "hint entry target_step out of range" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "hints" ][ "entries" ][ 0 ][ "target_step" ] = 9;
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.reference" );
  }
  SECTION( "hint entry without any target" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      Json::Value &entry = d[ "runtime" ][ "hints" ][ "entries" ][ 0 ];
      entry.removeMember( "target_step" );
      entry[ "level" ] = 1;
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "hint level exceeds escalation vocabulary" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "hints" ][ "entries" ][ 1 ][ "level" ] = 4;
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "choice question without choices" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "questions" ][ 2 ].removeMember( "choices" );
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "numeric question with inverted expected range" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      Json::Value &range = d[ "runtime" ][ "questions" ][ 1 ][ "expected_numeric" ];
      range[ "min" ] = 2.0;
      range[ "max" ] = 1.0;
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "invalid advance policy" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 0 ][ "checkpoints" ][ 0 ][ "advance" ] = "hard_block";
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "negative attempts_allowed" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 0 ][ "checkpoints" ][ 0 ][ "attempts_allowed" ] = -1;
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "bad data pack id shape" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "data_packs" ][ 0 ] = "../escape";
    } ) );
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  SECTION( "diagnostics carry a locator message" )
  {
    const Json::Value doc = parseJson( mutated( []( Json::Value &d ) {
      d[ "runtime" ][ "stages" ][ 0 ][ "step_indices" ][ 0 ] = 7;
    } ) );
    const auto result = sicnu::lab::parseRuntimeBlock( doc );
    REQUIRE( !result.ok );
    using Catch::Matchers::ContainsSubstring;
    REQUIRE_THAT( result.diagnostics.front().message, ContainsSubstring( "stages[0]" ) );
  }
}

TEST_CASE( "spec fingerprint is sha256 hex and deterministic", "[lab_runtime][fingerprint]" )
{
  // FIPS 180-4 known answer vector.
  REQUIRE( sicnu::lab::specFingerprint( "abc" ) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
  REQUIRE( sicnu::lab::specFingerprint( "" ) ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" );

  const std::string a = sicnu::lab::specFingerprint( "spec-bytes-A" );
  const std::string b = sicnu::lab::specFingerprint( "spec-bytes-A" );
  const std::string c = sicnu::lab::specFingerprint( "spec-bytes-B" );
  REQUIRE( a == b );
  REQUIRE( a != c );
  REQUIRE( a.size() == 64 );
  for ( const char ch : a )
    REQUIRE( ( ( ch >= '0' && ch <= '9' ) || ( ch >= 'a' && ch <= 'f' ) ) );
}
