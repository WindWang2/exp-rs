/***************************************************************************
  tests/test_r23_d_lab_hardening.cpp — R23-D hardening pair (fail-closed
  contracts in src/lab):

    1. depth bomb  — jsoncpp's stackLimit contract is an EXCEPTION (json/
       reader.h: "Exceeding stackLimit ... will cause an exception", version
       1.9.6 json_reader.cpp:1040 throws Json::RuntimeError). LabSessionStore
       loads untrusted on-disk session files; a >64-deep document must be the
       typed `lab.session.corrupt` refusal, never an escaping exception.
       (Pre-R23-D: readJsonFile called CharReader::parse without the try/
       catch every sibling module has — the "typed rejection" comment in
       makeStrictReader was aspirational, not real.)

    2. finite expected_numeric bounds — jsoncpp maps an out-of-range literal
       (1e999) to ±infinity doubles, so isNumeric() alone accepts infinite
       bounds; [-inf, +inf] then accepts every numeric answer. The runtime
       parser must reject non-finite min/max with `lab.runtime.field`
       (the isfinite discipline grader_types already enforces on rules).
 ***************************************************************************/

#include "lab/session_store.h"
#include "lab/spec_runtime.h"

#include <catch2/catch_test_macros.hpp>
#include <json/json.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>

namespace
{

std::filesystem::path makeTempRoot( const char *tag )
{
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  std::filesystem::path root = std::filesystem::temp_directory_path()
    / ( std::string( "r23d-" ) + tag + "-" + std::to_string( stamp ) );
  std::filesystem::create_directories( root );
  return root;
}

/// A minimal valid v3 runtime document (the same skeleton
/// test_lab_runtime.cpp's validV3Doc proves parses).
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
          "objective_zh": "计算 ndvi",
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

Json::Value parseJson( const std::string &text )
{
  Json::CharReaderBuilder builder;
  builder[ "stackLimit" ] = 256;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  Json::Value doc;
  std::string errors;
  reader->parse( text.data(), text.data() + text.size(), &doc, &errors );
  return doc;
}

std::string firstDiagCode( const sicnu::lab::LabResult<sicnu::lab::LabRuntimePlan> &result )
{
  REQUIRE( !result.ok );
  REQUIRE( !result.diagnostics.empty() );
  return result.diagnostics.front().code;
}

} // namespace

TEST_CASE( "session store: depth-bomb session file is a typed refusal, never an escaping jsoncpp exception",
           "[lab][r23_d][hardening]" )
{
  const std::filesystem::path root = makeTempRoot( "bomb" );

  // 200 nested arrays — comfortably past the store's stackLimit of 64.
  std::string bomb( 200, '[' );
  bomb.append( 200, ']' );

  const std::filesystem::path dir = root / "labx";
  std::filesystem::create_directories( dir );
  const std::filesystem::path file = dir / "studentx-1.session.json";
  {
    std::ofstream out( file, std::ios::binary );
    out << bomb;
  }

  sicnu::lab::LabSessionStore store( root.string() );
  // RED on master: Json::RuntimeError("Exceeded stackLimit in readValue().")
  // escapes loadAny; Catch2 catches the std::exception and the test fails.
  // GREEN: the load degrades to the typed corruption refusal.
  // sessionId format is "<labId>/<studentId>/<seq>" (session_store.h).
  const auto result = store.loadAny( "labx/studentx/1" );
  REQUIRE( !result.ok );
  REQUIRE( !result.diagnostics.empty() );
  REQUIRE( result.diagnostics.front().code == "lab.session.corrupt" );
  REQUIRE( result.diagnostics.front().message.find( "stackLimit" ) != std::string::npos );

  // Control: the same store still refuses a syntactically broken file with
  // the same typed code (parse-false path, not the exception path).
  {
    const std::filesystem::path broken = dir / "studentx-2.session.json";
    std::ofstream out( broken, std::ios::binary );
    out << "{\"a\": nope}";
  }
  const auto brokenResult = store.loadAny( "labx/studentx/2" );
  REQUIRE( !brokenResult.ok );
  REQUIRE( brokenResult.diagnostics.front().code == "lab.session.corrupt" );

  std::filesystem::remove_all( root );
}

TEST_CASE( "spec runtime: expected_numeric bounds must be finite", "[lab][r23_d][spec]" )
{
  // Control: the untouched valid document still parses.
  REQUIRE( sicnu::lab::parseRuntimeBlock( parseJson( validV3Doc() ) ).ok );

  // jsoncpp's decodeDouble maps an out-of-range literal (1e999 in document
  // text) to the infinities, so the parser gate must treat stored non-finite
  // doubles as invalid bounds. Assign the infinities directly (MSVC rejects
  // 1e999 as a C++ literal) — same stored value, same acceptance path.
  {
    Json::Value doc = parseJson( validV3Doc() );
    Json::Value &bounds = doc[ "runtime" ][ "questions" ][1][ "expected_numeric" ];
    bounds[ "min" ] = -std::numeric_limits<double>::infinity();
    bounds[ "max" ] = std::numeric_limits<double>::infinity();
    // The vacuous-acceptance oracle: an everything-goes interval must not
    // be a valid plan. RED on master (accepted, result.ok == true).
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  {
    Json::Value doc = parseJson( validV3Doc() );
    doc[ "runtime" ][ "questions" ][1][ "expected_numeric" ][ "max" ] =
      std::numeric_limits<double>::infinity();
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  {
    // min alone infinite (max finite) — same typed refusal.
    Json::Value doc = parseJson( validV3Doc() );
    doc[ "runtime" ][ "questions" ][1][ "expected_numeric" ][ "min" ] =
      -std::numeric_limits<double>::infinity();
    REQUIRE( firstDiagCode( sicnu::lab::parseRuntimeBlock( doc ) ) == "lab.runtime.field" );
  }
  // Finite reversed bounds keep their existing typed rejection (no drift).
  {
    Json::Value doc = parseJson( validV3Doc() );
    Json::Value &bounds = doc[ "runtime" ][ "questions" ][1][ "expected_numeric" ];
    bounds[ "min" ] = 2.0;
    bounds[ "max" ] = 1.0;
    REQUIRE( sicnu::lab::parseRuntimeBlock( doc ).ok == false );
  }
}
