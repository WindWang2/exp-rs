/***************************************************************************
  tests/test_r22_teaching_guard_hygiene.cpp
  R22-06 round 1 — three production-safety gaps as runtime RED cases:

  #1678 lab::recordAnswer accepts NaN/±Inf numeric answers; jsoncpp then
       serializes NaN as null and the session document no longer reloads.
  #1689 the cross-student leak guard cannot see plain numeric student ids
       (2023110101) or Chinese names — only "student_" prefixed tokens.
  #1688 bundleBuilderRequest passes unvalidated arguments through
       cmd.exe /c on Windows; metacharacters without spaces stay unquoted
       and become command separators ("--version 1.0&calc" runs calc).

  The lab runtime fixture below is the canonical valid v3 document from
  tests/test_lab_runtime.cpp (kept verbatim so this suite stays hermetic).
***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"
#include "teaching_admin/feedback_pack.h"
#include "teaching_admin/script_adapters.h"

#include <QJsonDocument>
#include <QTemporaryDir>

#include <json/json.h>

#include "lab/session_state.h"
#include "lab/spec_runtime.h"

#include <limits>
#include <sstream>
#include <string>

using namespace sicnu::teaching_admin;

namespace
{

// ── lab runtime fixture (verbatim from tests/test_lab_runtime.cpp) ──────────

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

sicnu::lab::LabRuntimePlan probePlan()
{
    const auto result = sicnu::lab::parseRuntimeBlock( parseJson( validV3Doc() ) );
    REQUIRE( result.ok );
    return result.value;
}

sicnu::lab::SessionMeta probeMeta()
{
    sicnu::lab::SessionMeta meta;
    meta.labId = "lab90_runtime_probe";
    meta.studentId = "student1";
    meta.labSpecVersion = 3;
    meta.planSource = "authored_v3";
    meta.hasSeed = true;
    meta.seed = 42;
    return meta;
}

} // namespace

// ── #1678: non-finite numeric answers poison the session round-trip ─────────

TEST_CASE( "#1678: recordAnswer refuses NaN and ±Inf numeric answers",
           "[lab_runtime][session][r22][issue-1678]" )
{
    const sicnu::lab::LabRuntimePlan plan = probePlan();
    auto started = sicnu::lab::startSession( plan, probeMeta(), 1 );
    REQUIRE( started.ok );
    sicnu::lab::LabSession session = started.value;

    const double poison[] = { std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity() };
    for ( const double value : poison )
    {
        const auto res =
          sicnu::lab::recordAnswer( session, plan, "q_ndvi", "", value );
        REQUIRE_FALSE( res.ok );
        REQUIRE( res.diagnostics.front().code == "lab.session.field" );
    }

    // Nothing was recorded: the session document round-trips with no null
    // numeric member anywhere (the reload-poisoning byte pattern).
    const std::string bytes = sicnu::lab::sessionToCanonicalBytes( session );
    REQUIRE( bytes.find( "\"numeric\":null" ) == std::string::npos );
    const auto reloaded = sicnu::lab::sessionFromJson( parseJson( bytes ) );
    REQUIRE( reloaded.ok );

    // A finite answer still works and round-trips.
    REQUIRE( sicnu::lab::recordAnswer( session, plan, "q_ndvi", "", 0.35 ).ok );
    const std::string bytes2 = sicnu::lab::sessionToCanonicalBytes( session );
    REQUIRE( sicnu::lab::sessionFromJson( parseJson( bytes2 ) ).ok );
}

// ── #1689: the leak guard must see real student ids ─────────────────────────

TEST_CASE( "#1689: leak guard detects numeric and Chinese-name foreign ids",
           "[teaching_admin][feedback][r22][issue-1689]" )
{
    FeedbackPackInput in;
    in.row.studentId = QStringLiteral( "2023001" );
    in.row.labId = QStringLiteral( "lab99" );
    in.row.status = QStringLiteral( "pass" );
    in.row.verdict = QStringLiteral( "pass" );
    in.row.score = 1.0;
    in.row.message = QStringLiteral( "参考 2023002 的结果再检查裁剪范围" );
    const QJsonObject fb = buildFeedbackPack( in );

    // Master: the blob only contains plain numbers, the student_-prefix scan
    // sees nothing, the guard returns ok — the leak ships.
    const ValidationResult r = assertNoCrossStudentLeak( fb, QStringLiteral( "2023001" ) );
    REQUIRE_FALSE( r.ok );
}

TEST_CASE( "#1689: leak guard keeps passing own-token and unrelated content",
           "[teaching_admin][feedback][r22][issue-1689]" )
{
    FeedbackPackInput in;
    in.row.studentId = QStringLiteral( "2023001" );
    in.row.labId = QStringLiteral( "lab99" );
    in.row.status = QStringLiteral( "pass" );
    in.row.verdict = QStringLiteral( "pass" );
    in.row.score = 0.85;
    in.row.message = QStringLiteral( "2023001: 裁剪正确，下一步注意 NDVI 的取值范围" );
    const QJsonObject fb = buildFeedbackPack( in );

    // The student's own id and ordinary prose/numbers must not trip the guard.
    const ValidationResult r = assertNoCrossStudentLeak( fb, QStringLiteral( "2023001" ) );
    REQUIRE( r.ok );
}

// ── #1688: cmd.exe metacharacters must never reach the command line ─────────

TEST_CASE( "#1688: bundleBuilderRequest refuses injection-shaped arguments",
           "[teaching_admin][offline][r22][issue-1688]" )
{
    // "1.0&calc" contains no space: QProcess leaves it unquoted, cmd.exe
    // splits at & and runs calc after the script. Master builds the request
    // happily — this RED case demands a refusal (empty program).
    const auto injected = bundleBuilderRequest( QStringLiteral( "C:/repo" ),
                                                QStringLiteral( "C:/build" ),
                                                QStringLiteral( "C:/out" ),
                                                QStringLiteral( "1.0&calc" ), 250, true );
    REQUIRE( injected.program.isEmpty() );

    // A legal Windows path with & (C:\R&D\out) is equally refused — the
    // argument cannot ride through cmd.exe /c safely.
    const auto andedOut = bundleBuilderRequest( QStringLiteral( "C:/repo" ),
                                                QStringLiteral( "C:/build" ),
                                                QStringLiteral( "C:\\R&D\\out" ),
                                                QStringLiteral( "v1" ), 250, true );
    REQUIRE( andedOut.program.isEmpty() );
}

TEST_CASE( "#1688: bundleBuilderRequest still builds legal requests",
           "[teaching_admin][offline][r22][issue-1688]" )
{
    const auto legal = bundleBuilderRequest( QStringLiteral( "C:/repo" ),
                                             QStringLiteral( "C:/build" ),
                                             QStringLiteral( "C:/out" ),
                                             QStringLiteral( "v2026.09.1-beta_2" ), 250, true );
    REQUIRE( legal.program == QLatin1String( "cmd.exe" ) );
    REQUIRE( legal.arguments.contains( QStringLiteral( "v2026.09.1-beta_2" ) ) );
}
