/***************************************************************************
  tests/test_r22_teaching_guard_hygiene.cpp
  R22-06 round 1 — three production-safety gaps, RED→GREEN:

  #1678 lab::recordAnswer accepted NaN/±Inf numeric answers; jsoncpp then
       serializes NaN as null and the session document no longer reloads.
       Fixed at BOTH boundaries: recordAnswer refuses non-finite values and
       sessionFromJson rejects them with the same typed diagnostic.
  #1689 the cross-student leak guard could not see plain numeric student
       ids (2023110101) or Chinese names. The guard is now cohort-aware and
       gated into the production emission path (buildGuardedFeedbackPacks).
  #1688 bundleBuilderRequest passed unvalidated arguments through
       cmd.exe /c on Windows; metacharacters without spaces stay unquoted
       and become command separators ("--version 1.0&calc" runs calc).
       Parameters are now validated fail-closed before any argv exists.

  RED evidence (pre-fix, commit 1610c9b75): numeric-leak case returned ok,
  NaN answer was accepted, and the injected argv carried program cmd.exe.

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

FeedbackPackInput packFor( const QString &studentId, const QString &message )
{
    FeedbackPackInput in;
    in.row.studentId = studentId;
    in.row.labId = QStringLiteral( "lab99" );
    in.row.status = QStringLiteral( "pass" );
    in.row.verdict = QStringLiteral( "pass" );
    in.row.score = 1.0;
    in.row.message = message;
    return in;
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
        const auto res = sicnu::lab::recordAnswer( session, plan, "q_ndvi", "", value );
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

TEST_CASE( "#1678: sessionFromJson rejects non-finite numeric members",
           "[lab_runtime][session][r22][issue-1678]" )
{
    // Belt and braces: even a hand-crafted document (jsoncpp parses huge
    // exponents into infinities) is refused with the typed schema error
    // instead of loading a session that cannot round-trip.
    Json::Value doc = parseJson( [] {
        const sicnu::lab::LabRuntimePlan plan = probePlan();
        auto started = sicnu::lab::startSession( plan, probeMeta(), 1 );
        REQUIRE( started.ok );
        REQUIRE( sicnu::lab::recordAnswer( started.value, plan, "q_ndvi", "", 0.35 ).ok );
        return sicnu::lab::sessionToCanonicalBytes( started.value );
    }() );
    REQUIRE( doc[ "question_answers" ].isArray() );
    REQUIRE( doc[ "question_answers" ].size() == 1 );
    doc[ "question_answers" ][0][ "numeric" ] = std::numeric_limits<double>::infinity();

    const auto res = sicnu::lab::sessionFromJson( doc );
    REQUIRE_FALSE( res.ok );
    REQUIRE( res.diagnostics.front().code == "lab.session.schema" );

    // R1 test-gap 4: the same poison as a LITERAL exponent in the raw text
    // (jsoncpp's reader accepts 1e999 and yields a non-finite double).
    std::string text = [] {
        const sicnu::lab::LabRuntimePlan plan = probePlan();
        auto started = sicnu::lab::startSession( plan, probeMeta(), 1 );
        REQUIRE( started.ok );
        REQUIRE( sicnu::lab::recordAnswer( started.value, plan, "q_ndvi", "", 0.35 ).ok );
        return sicnu::lab::sessionToCanonicalBytes( started.value );
    }();
    const auto keyPos = text.find( "\"numeric\"" );
    REQUIRE( keyPos != std::string::npos );
    const auto colonPos = text.find( ':', keyPos );
    const auto valueEnd = text.find_first_of( ",}", colonPos );
    REQUIRE( colonPos != std::string::npos );
    REQUIRE( valueEnd != std::string::npos );
    text.replace( colonPos + 1, valueEnd - colonPos - 1, "1e999" );
    const Json::Value parsed = parseJson( text );
    const auto res2 = sicnu::lab::sessionFromJson( parsed );
    REQUIRE_FALSE( res2.ok );
}

// ── #1689: the leak guard must see real student ids ─────────────────────────

TEST_CASE( "#1689: leak guard detects numeric and Chinese-name foreign ids",
           "[teaching_admin][feedback][r22][issue-1689]" )
{
    SECTION( "plain numeric student ids" )
    {
        const QJsonObject fb =
          buildFeedbackPack( packFor( QStringLiteral( "2023001" ),
                                      QStringLiteral( "参考 2023002 的结果再检查裁剪范围" ) ) );
        const ValidationResult r = assertNoCrossStudentLeak(
          fb, QStringLiteral( "2023001" ),
          QStringList{ QStringLiteral( "2023001" ), QStringLiteral( "2023002" ) } );
        REQUIRE_FALSE( r.ok );
        REQUIRE( r.issues.first().code == QLatin1String( "cross_student_leak" ) );
    }
    SECTION( "Chinese-name ids" )
    {
        const QJsonObject fb =
          buildFeedbackPack( packFor( QStringLiteral( "王五" ),
                                      QStringLiteral( "张三 的处理范围更完整，建议对照" ) ) );
        const ValidationResult r = assertNoCrossStudentLeak(
          fb, QStringLiteral( "王五" ), QStringList{ QStringLiteral( "张三" ), QStringLiteral( "李四" ),
                                                    QStringLiteral( "王五" ) } );
        REQUIRE_FALSE( r.ok );
    }
    SECTION( "adjacent longer tokens do not trip the guard" )
    {
        // 20230021 is NOT the id 2023002 — a word-boundary match must not
        // fire inside a longer number.
        const QJsonObject fb =
          buildFeedbackPack( packFor( QStringLiteral( "2023001" ),
                                      QStringLiteral( "工单 20230021 已记录" ) ) );
        const ValidationResult r = assertNoCrossStudentLeak(
          fb, QStringLiteral( "2023001" ),
          QStringList{ QStringLiteral( "2023001" ), QStringLiteral( "2023002" ) } );
        REQUIRE( r.ok );
    }
    SECTION( "R1: Chinese-name PREFIX overlap does not trip the guard" )
    {
        // R1 finding 2: with ASCII-only boundary classes, 张三's pattern
        // matched inside 张三丰's own student_id and the production gate
        // withheld a legitimate pack. Unicode boundaries must hold.
        const QJsonObject fb =
          buildFeedbackPack( packFor( QStringLiteral( "张三丰" ),
                                      QStringLiteral( "裁剪正确，注意 NDVI 范围" ) ) );
        const ValidationResult r =
          assertNoCrossStudentLeak( fb, QStringLiteral( "张三丰" ),
                                    QStringList{ QStringLiteral( "张三" ),
                                                 QStringLiteral( "李四" ),
                                                 QStringLiteral( "张三丰" ) } );
        REQUIRE( r.ok );
        // And the reverse cohort member genuinely mentioning 张三 is caught.
        const QJsonObject leaking =
          buildFeedbackPack( packFor( QStringLiteral( "李四" ),
                                      QStringLiteral( "张三 的结果更好" ) ) );
        REQUIRE_FALSE( assertNoCrossStudentLeak(
                         leaking, QStringLiteral( "李四" ),
                         QStringList{ QStringLiteral( "张三" ), QStringLiteral( "李四" ),
                                      QStringLiteral( "张三丰" ) } )
                         .ok );
        // The production gate itself must not withhold 张三丰's pack.
        QVector<FeedbackPackInput> inputs;
        inputs.append( packFor( QStringLiteral( "张三" ), QStringLiteral( "正常反馈" ) ) );
        inputs.append( packFor( QStringLiteral( "张三丰" ), QStringLiteral( "正常反馈" ) ) );
        const FeedbackPackEmission emission =
          buildGuardedFeedbackPacks( inputs,
                                     QStringList{ QStringLiteral( "张三" ),
                                                  QStringLiteral( "张三丰" ) } );
        REQUIRE( emission.packs.size() == 2 );
        REQUIRE( emission.blockedStudents.isEmpty() );
    }
    SECTION( "own token and unrelated prose stay clean" )
    {
        const QJsonObject fb =
          buildFeedbackPack( packFor( QStringLiteral( "2023001" ),
                                      QStringLiteral( "2023001: 裁剪正确，NDVI 注意范围" ) ) );
        const ValidationResult r = assertNoCrossStudentLeak(
          fb, QStringLiteral( "2023001" ),
          QStringList{ QStringLiteral( "2023001" ), QStringLiteral( "2023002" ) } );
        REQUIRE( r.ok );
    }
    SECTION( "legacy student_ prefix form still detected via the 2-arg overload" )
    {
        const QJsonObject fb =
          buildFeedbackPack( packFor( QStringLiteral( "student_alice" ),
                                      QStringLiteral( "对比 student_bob 的结果" ) ) );
        const ValidationResult r = assertNoCrossStudentLeak( fb, QStringLiteral( "student_alice" ) );
        REQUIRE_FALSE( r.ok );
    }
}

TEST_CASE( "#1689: guarded emission withholds leaking packs on the production path",
           "[teaching_admin][feedback][r22][issue-1689]" )
{
    QVector<FeedbackPackInput> inputs;
    inputs.append( packFor( QStringLiteral( "2023001" ),
                            QStringLiteral( "裁剪正确，注意 NDVI 的取值范围" ) ) );
    inputs.append( packFor( QStringLiteral( "2023002" ),
                            QStringLiteral( "参考 2023001 的结果重做裁剪" ) ) );
    const QStringList cohort{ QStringLiteral( "2023001" ), QStringLiteral( "2023002" ) };

    const FeedbackPackEmission emission = buildGuardedFeedbackPacks( inputs, cohort );
    // The clean pack ships; the leaking pack is withheld and accounted.
    REQUIRE( emission.packs.size() == 1 );
    REQUIRE( emission.packs.first().value( QStringLiteral( "student_id" ) ).toString()
             == QLatin1String( "2023001" ) );
    REQUIRE( emission.blockedStudents == QStringList{ QStringLiteral( "2023002" ) } );
    REQUIRE( emission.refusals.size() == 1 );
    REQUIRE( emission.refusals.first().contains( QLatin1String( "2023002" ) ) );
    // The withheld pack's bytes are not in the emission at all.
    QJsonArray emittedArr;
    for ( const QJsonObject &p : emission.packs )
        emittedArr.append( p );
    const QByteArray emitted = QJsonDocument( emittedArr ).toJson( QJsonDocument::Compact );
    REQUIRE_FALSE( emitted.contains( QByteArrayLiteral( "重做裁剪" ) ) );
}

// ── #1688: cmd.exe metacharacters must never reach the command line ─────────

TEST_CASE( "#1688: bundleBuilderRequest refuses injection-shaped arguments",
           "[teaching_admin][offline][r22][issue-1688]" )
{
    struct Case
    {
        const char *version;
        const char *buildDir;
        const char *outDir;
    };
    const Case cases[] = {
        // cmd separators in a space-free argument ride through QProcess.
        { "1.0&calc", "C:/build", "C:/out" },
        { "1.0|whoami", "C:/build", "C:/out" },
        { "1.0^whoami", "C:/build", "C:/out" },
        // Environment expansion.
        { "%TEMP%", "C:/build", "C:/out" },
        { "v!PATH!", "C:/build", "C:/out" },
        // Redirect metacharacters.
        { "1.0>x", "C:/build", "C:/out" },
        { "1.0<x", "C:/build", "C:/out" },
        // Quotes break the argv join.
        { "1\"0", "C:/build", "C:/out" },
        // Legal Windows folder names that cmd would truncate at the &.
        { "v1", "C:/build", "C:\\R&D\\out" },
        { "v1", "C:\\R&D\\build", "C:/out" },
    };
    for ( const Case &c : cases )
    {
        CAPTURE( c.version, c.buildDir, c.outDir );
        // Refused on BOTH platform twins: the refusal is an EMPTY request —
        // no program, no argv, nothing a caller could execute.
        const auto win = bundleBuilderRequest( QStringLiteral( "C:/repo" ),
                                               QLatin1String( c.buildDir ),
                                               QLatin1String( c.outDir ),
                                               QLatin1String( c.version ), 250, true );
        REQUIRE( win.program.isEmpty() );
        REQUIRE( win.arguments.isEmpty() );
        const auto posix = bundleBuilderRequest( QStringLiteral( "C:/repo" ),
                                                 QLatin1String( c.buildDir ),
                                                 QLatin1String( c.outDir ),
                                                 QLatin1String( c.version ), 250, false );
        REQUIRE( posix.program.isEmpty() );
    }
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
    // A unicode path with spaces stays a single argument and is not refused.
    const auto unicode =
      bundleBuilderRequest( QStringLiteral( "C:/repo" ), QStringLiteral( "C:/build 2026" ),
                            QStringLiteral( "C:/遥感课程/离线包" ), QString(), 250, true );
    REQUIRE( unicode.program == QLatin1String( "cmd.exe" ) );
    REQUIRE( unicode.arguments.contains( QStringLiteral( "C:/遥感课程/离线包" ) ) );
}

TEST_CASE( "#1688: validateBundleBuilderParams names the offending parameter",
           "[teaching_admin][offline][r22][issue-1688]" )
{
    REQUIRE( validateBundleBuilderParams( QStringLiteral( "C:/build" ), QStringLiteral( "C:/out" ),
                                          QStringLiteral( "v1" ) )
               .isEmpty() );
    const QStringList versionReject =
      validateBundleBuilderParams( QStringLiteral( "C:/build" ), QStringLiteral( "C:/out" ),
                                   QStringLiteral( "1.0&calc" ) );
    REQUIRE( versionReject.size() == 1 );
    REQUIRE( versionReject.first().startsWith( QLatin1String( "version:" ) ) );

    const QStringList outReject =
      validateBundleBuilderParams( QStringLiteral( "C:/build" ), QStringLiteral( "C:\\R&D\\out" ),
                                   QString() );
    REQUIRE( outReject.size() == 1 );
    REQUIRE( outReject.first().startsWith( QLatin1String( "out_dir:" ) ) );

    REQUIRE_FALSE( validateBundleBuilderParams( QString(), QStringLiteral( "C:/out" ), QString() )
                     .isEmpty() );

    SECTION( "R1: Latin-1 letters are not in the ASCII whitelist" )
    {
        REQUIRE_FALSE( validateBundleBuilderParams( QStringLiteral( "C:/build" ),
                                                    QStringLiteral( "C:/out" ),
                                                    QStringLiteral( "café" ) )
                         .isEmpty() );
    }
    SECTION( "R1: cmd .cmd re-tokenization delimiters are refused in paths" )
    {
        REQUIRE_FALSE( validateBundleBuilderParams( QStringLiteral( "C:/build" ),
                                                    QStringLiteral( "C:\\a;b" ), QString() )
                         .isEmpty() );
        REQUIRE_FALSE( validateBundleBuilderParams( QStringLiteral( "C:/b,c" ),
                                                    QStringLiteral( "C:/out" ), QString() )
                         .isEmpty() );
        REQUIRE_FALSE( validateBundleBuilderParams( QStringLiteral( "C:/a=b" ),
                                                    QStringLiteral( "C:/out" ), QString() )
                         .isEmpty() );
    }
}

TEST_CASE( "#1688 R1: buildOfflineBundle refuses injection-shaped input without a process",
           "[teaching_admin][offline][r22][issue-1688]" )
{
    // R1 test-gap 3: the production refusal path — typed failure, no start.
    const ScriptRunResult refused =
      buildOfflineBundle( QStringLiteral( "C:/repo" ), QStringLiteral( "C:/build" ),
                          QStringLiteral( "C:/out" ), QStringLiteral( "1.0&calc" ), 250 );
    REQUIRE_FALSE( refused.started );
    REQUIRE( refused.error.contains( QLatin1String( "refused" ) ) );
    REQUIRE( refused.error.contains( QLatin1String( "version" ) ) );

    // R1 finding 7: repoRoot rides in the same cmd.exe argv — held to the
    // same standard by bundleBuilderRequest.
    const auto badRoot = bundleBuilderRequest( QStringLiteral( "C:\\R&D\\repo" ),
                                               QStringLiteral( "C:/build" ),
                                               QStringLiteral( "C:/out" ),
                                               QStringLiteral( "v1" ), 250, true );
    REQUIRE( badRoot.program.isEmpty() );
    const auto emptyRoot = bundleBuilderRequest( QString(), QStringLiteral( "C:/build" ),
                                                 QStringLiteral( "C:/out" ), QString(), 250, true );
    REQUIRE( emptyRoot.program.isEmpty() );
}
