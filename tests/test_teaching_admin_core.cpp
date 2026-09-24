// test_teaching_admin_core.cpp — Teacher Authoring Console contracts (Catch2).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "teaching_admin/batch_assessment.h"
#include "teaching_admin/class_summary.h"
#include "teaching_admin/curriculum_editor.h"
#include "teaching_admin/data_pack_manager.h"
#include "teaching_admin/feedback_pack.h"
#include "teaching_admin/grader_cli_adapter.h"
#include "teaching_admin/json_util.h"
#include "teaching_admin/labspec_authoring.h"
#include "teaching_admin/operator_catalog.h"
#include "teaching_admin/release_preflight.h"
#include "teaching_admin/rubric_builder.h"
#include "teaching_admin/script_adapters.h"
#include "teaching_admin/student_projection.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <atomic>

using namespace sicnu::teaching_admin;

namespace {

QJsonObject minimalCurriculum()
{
    return QJsonObject{
        { QStringLiteral( "schema" ), QStringLiteral( "sicnu.curriculum/1" ) },
        { QStringLiteral( "id" ), QStringLiteral( "demo_course" ) },
        { QStringLiteral( "title" ), QStringLiteral( "Demo" ) },
        { QStringLiteral( "title_zh" ), QStringLiteral( "演示课" ) },
        { QStringLiteral( "modules" ),
          QJsonArray{ QJsonObject{
              { QStringLiteral( "id" ), QStringLiteral( "m01_basics" ) },
              { QStringLiteral( "index" ), 1 },
              { QStringLiteral( "title" ), QStringLiteral( "Basics" ) },
              { QStringLiteral( "title_zh" ), QStringLiteral( "基础" ) },
              { QStringLiteral( "summary_zh" ), QStringLiteral( "摘要" ) },
              { QStringLiteral( "learning_outcomes" ), QJsonArray{ QStringLiteral( "能读元数据" ) } },
              { QStringLiteral( "prerequisite_modules" ), QJsonArray{} },
              { QStringLiteral( "labs" ),
                QJsonArray{ QJsonObject{
                    { QStringLiteral( "lab_id" ), QStringLiteral( "lab15_data_inspection" ) },
                    { QStringLiteral( "role" ), QStringLiteral( "core" ) },
                    { QStringLiteral( "estimated_effort_minutes" ), 120 },
                    { QStringLiteral( "required_data_packs" ),
                      QJsonArray{ QStringLiteral( "lab15_data_inspection" ) } },
                } } },
          } } },
    };
}

} // namespace

TEST_CASE( "curriculum valid", "[teaching_admin][curriculum]" )
{
    QSet<QString> labs{ QStringLiteral( "lab15_data_inspection" ) };
    QSet<QString> packs{ QStringLiteral( "lab15_data_inspection" ) };
    const auto r = validateCurriculum( minimalCurriculum(), labs, packs );
    REQUIRE( r.ok );
}

TEST_CASE( "curriculum cycle detected", "[teaching_admin][curriculum]" )
{
    auto m = minimalCurriculum();
    QJsonArray modules = m.value( QStringLiteral( "modules" ) ).toArray();
    modules.append( QJsonObject{
        { QStringLiteral( "id" ), QStringLiteral( "m02_next" ) },
        { QStringLiteral( "index" ), 2 },
        { QStringLiteral( "title" ), QStringLiteral( "Next" ) },
        { QStringLiteral( "title_zh" ), QStringLiteral( "下一" ) },
        { QStringLiteral( "summary_zh" ), QStringLiteral( "摘要" ) },
        { QStringLiteral( "learning_outcomes" ), QJsonArray{ QStringLiteral( "x" ) } },
        { QStringLiteral( "prerequisite_modules" ), QJsonArray{ QStringLiteral( "m01_basics" ) } },
        { QStringLiteral( "labs" ),
          QJsonArray{ QJsonObject{
              { QStringLiteral( "lab_id" ), QStringLiteral( "lab15_data_inspection" ) },
              { QStringLiteral( "role" ), QStringLiteral( "core" ) },
              { QStringLiteral( "estimated_effort_minutes" ), 60 },
          } } },
    } );
    // create cycle: m01 requires m02
    QJsonObject m01 = modules.at( 0 ).toObject();
    m01.insert( QStringLiteral( "prerequisite_modules" ), QJsonArray{ QStringLiteral( "m02_next" ) } );
    modules.replace( 0, m01 );
    m.insert( QStringLiteral( "modules" ), modules );

    QSet<QString> labs{ QStringLiteral( "lab15_data_inspection" ) };
    const auto r = validateCurriculum( m, labs );
    REQUIRE_FALSE( r.ok );
    bool found = false;
    for ( const auto &i : r.issues )
        if ( i.code == QLatin1String( "cyclic_prerequisites" ) )
            found = true;
    REQUIRE( found );
}

TEST_CASE( "curriculum missing lab", "[teaching_admin][curriculum]" )
{
    QSet<QString> labs{ QStringLiteral( "other_lab" ) };
    const auto r = validateCurriculum( minimalCurriculum(), labs );
    REQUIRE_FALSE( r.ok );
    bool found = false;
    for ( const auto &i : r.issues )
        if ( i.code == QLatin1String( "unknown_lab_reference" ) )
            found = true;
    REQUIRE( found );
}

TEST_CASE( "labspec unknown operator and invalid param", "[teaching_admin][labspec]" )
{
    QJsonObject spec{
        { QStringLiteral( "schema" ), QStringLiteral( "sicnu.labspec.v1" ) },
        { QStringLiteral( "id" ), QStringLiteral( "demo_lab" ) },
        { QStringLiteral( "operators" ), QJsonArray{ QStringLiteral( "rs:ndvi" ), QStringLiteral( "rs:nope" ) } },
        { QStringLiteral( "steps" ),
          QJsonArray{ QJsonObject{
              { QStringLiteral( "operator_id" ), QStringLiteral( "rs:ndvi" ) },
              { QStringLiteral( "params" ), QJsonObject{ { QStringLiteral( "bogus" ), 1 } } },
          } } },
        { QStringLiteral( "alien_field" ), 1 },
    };
    QSet<QString> ops{ QStringLiteral( "rs:ndvi" ) };
    QJsonObject schemas{
        { QStringLiteral( "rs:ndvi" ),
          QJsonObject{ { QStringLiteral( "properties" ),
                         QJsonObject{ { QStringLiteral( "red" ), QJsonObject{} } } } } },
    };
    const auto r = validateLabSpec( spec, ops, schemas );
    REQUIRE_FALSE( r.ok );
    QSet<QString> codes;
    for ( const auto &i : r.issues )
        codes.insert( i.code );
    REQUIRE( codes.contains( QStringLiteral( "unknown_operator" ) ) );
    REQUIRE( codes.contains( QStringLiteral( "invalid_param" ) ) );
    REQUIRE( codes.contains( QStringLiteral( "unknown_field" ) ) );
}

TEST_CASE( "malformed grading rule and weight mismatch", "[teaching_admin][rubric]" )
{
    QJsonObject badRules{
        { QStringLiteral( "schema_version" ), QStringLiteral( "sicnu.lab.rules/1" ) },
        { QStringLiteral( "lab_id" ), QStringLiteral( "x" ) },
        { QStringLiteral( "title" ), QStringLiteral( "t" ) },
        { QStringLiteral( "artifact" ), QJsonObject{ { QStringLiteral( "kind" ), QStringLiteral( "nope" ) } } },
        { QStringLiteral( "assertions" ),
          QJsonArray{ QJsonObject{
              { QStringLiteral( "id" ), QStringLiteral( "a1" ) },
              { QStringLiteral( "kind" ), QStringLiteral( "not_a_kind" ) },
              { QStringLiteral( "weight" ), 0 },
              { QStringLiteral( "params" ), QJsonObject{} },
          } } },
    };
    auto r = validateLabRules( badRules );
    REQUIRE_FALSE( r.ok );

    QJsonObject process{
        { QStringLiteral( "schema" ), QStringLiteral( "sicnu.grader.rubric/1" ) },
        { QStringLiteral( "criteria" ),
          QJsonArray{
              QJsonObject{ { QStringLiteral( "id" ), QStringLiteral( "c1" ) },
                           { QStringLiteral( "kind" ), QStringLiteral( "stage" ) },
                           { QStringLiteral( "weight" ), 0.4 } },
              QJsonObject{ { QStringLiteral( "id" ), QStringLiteral( "c2" ) },
                           { QStringLiteral( "kind" ), QStringLiteral( "metric" ) },
                           { QStringLiteral( "weight" ), 0.4 },
                           { QStringLiteral( "metricKey" ), QStringLiteral( "oa" ) } },
          } },
    };
    r = validateProcessRubric( process );
    REQUIRE_FALSE( r.ok );
    bool weight = false;
    for ( const auto &i : r.issues )
        if ( i.code == QLatin1String( "weight_mismatch" ) )
            weight = true;
    REQUIRE( weight );
}

TEST_CASE( "data pack path traversal", "[teaching_admin][packs]" )
{
    QJsonObject pack{
        { QStringLiteral( "schema_version" ), QStringLiteral( "sicnu.lab-pack/1" ) },
        { QStringLiteral( "lab_id" ), QStringLiteral( "x" ) },
        { QStringLiteral( "inputs" ),
          QJsonArray{ QJsonObject{ { QStringLiteral( "path" ), QStringLiteral( "../etc/passwd" ) } } } },
    };
    const auto r = validatePackDocument( pack, QStringLiteral( "/tmp" ) );
    REQUIRE_FALSE( r.ok );
    bool trav = false;
    for ( const auto &i : r.issues )
        if ( i.code == QLatin1String( "path_traversal" ) )
            trav = true;
    REQUIRE( trav );
}

TEST_CASE( "batch corrupted + 1-of-N + cancel + missing evidence + atomic publish + regrade",
           "[teaching_admin][batch]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QDir root( dir.path() );
    REQUIRE( root.mkdir( QStringLiteral( "s01" ) ) );
    REQUIRE( root.mkdir( QStringLiteral( "s02" ) ) );
    // non-ASCII student dir
    REQUIRE( root.mkdir( QStringLiteral( "学生03" ) ) );

    auto write = [&]( const QString &rel, const QByteArray &bytes ) {
        QFile f( root.filePath( rel ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( bytes );
    };
    write( QStringLiteral( "s01/ok.tif" ), QByteArray( "GOOD" ) );
    write( QStringLiteral( "s02/bad.tif" ), QByteArray( "CORRUPT" ) );
    write( QStringLiteral( "学生03/ok.tif" ), QByteArray( "GOOD" ) );

    // fabricate 97 more pass files as flat ids for 100 total intent (use 10 for speed)
    for ( int i = 4; i <= 10; ++i )
    {
        const QString sid = QStringLiteral( "s%1" ).arg( i, 2, 10, QLatin1Char( '0' ) );
        REQUIRE( root.mkdir( sid ) );
        write( sid + QStringLiteral( "/ok.tif" ), QByteArray( "GOOD" ) );
    }
    write( QStringLiteral( "s01/missing.txt" ), QByteArray( "MISSING_EVIDENCE" ) );

    BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab15_data_inspection" );
    cfg.submissionsDir = dir.path();
    cfg.rubricVersion = QStringLiteral( "r1" );
    cfg.labVersion = QStringLiteral( "l1" );
    cfg.softwareVersion = QStringLiteral( "sw1" );

    std::atomic<bool> cancel{ false };
    int calls = 0;
    auto report = runBatchAssessment(
        cfg,
        [&]( const SubmissionItem &item ) {
            ++calls;
            BatchRowResult row;
            row.studentId = item.studentId;
            row.labId = cfg.labId;
            row.artifactPath = item.path;
            QFile f( item.path );
            REQUIRE( f.open( QIODevice::ReadOnly ) );
            const QByteArray b = f.readAll();
            if ( b.startsWith( "CORRUPT" ) )
            {
                row.status = QStringLiteral( "corrupted" );
                row.verdict = QStringLiteral( "error" );
                row.message = QStringLiteral( "corrupt" );
                return row;
            }
            if ( b.startsWith( "MISSING_EVIDENCE" ) )
            {
                row.missingEvidence = true;
                row.score = 0.0; // would-be silent zero — orchestrator rewrites
                row.status = QStringLiteral( "fail" );
                row.verdict = QStringLiteral( "fail" );
                row.message = QStringLiteral( "no evidence" );
                return row;
            }
            row.status = QStringLiteral( "pass" );
            row.verdict = QStringLiteral( "pass" );
            row.score = 90.0;
            return row;
        },
        &cancel );

    REQUIRE( report.total >= 10 );
    REQUIRE( report.corrupted >= 1 );
    bool sawUnavailable = false;
    for ( const auto &row : report.rows )
    {
        if ( row.missingEvidence )
        {
            REQUIRE( row.verdict == QLatin1String( "unavailable" ) );
            REQUIRE( ( row.score != 0.0 || row.status == QLatin1String( "unavailable" ) ) );
            sawUnavailable = true;
        }
    }
    REQUIRE( sawUnavailable );

    // cancellation mid-flight
    cancel.store( true );
    auto cancelled = runBatchAssessment(
        cfg,
        [&]( const SubmissionItem &item ) {
            BatchRowResult row;
            row.studentId = item.studentId;
            row.status = QStringLiteral( "pass" );
            row.verdict = QStringLiteral( "pass" );
            row.score = 1;
            row.artifactPath = item.path;
            return row;
        },
        &cancel );
    REQUIRE( cancelled.cancelledEarly );
    REQUIRE( cancelled.cancelled == cancelled.total );

    QTemporaryDir outDir;
    REQUIRE( outDir.isValid() );
    const QString prefix = QDir( outDir.path() ).filePath( QStringLiteral( "batch" ) );
    REQUIRE( publishBatchOutputsAtomic( report, prefix ) );
    REQUIRE( QFileInfo::exists( prefix + QStringLiteral( ".json" ) ) );
    REQUIRE( QFileInfo::exists( prefix + QStringLiteral( ".csv" ) ) );

    // deterministic regrade
    cancel.store( false );
    // Reuse the same grading logic for deterministic regrade identity.
    GradeCallable gradeAgain = [&]( const SubmissionItem &item ) {
            BatchRowResult row;
            row.studentId = item.studentId;
            row.labId = cfg.labId;
            row.artifactPath = item.path;
            QFile f( item.path );
            REQUIRE( f.open( QIODevice::ReadOnly ) );
            const QByteArray b = f.readAll();
            if ( b.startsWith( "CORRUPT" ) )
            {
                row.status = QStringLiteral( "corrupted" );
                row.verdict = QStringLiteral( "error" );
                row.message = QStringLiteral( "corrupt" );
                return row;
            }
            if ( b.startsWith( "MISSING_EVIDENCE" ) )
            {
                row.missingEvidence = true;
                row.score = 0.0;
                row.status = QStringLiteral( "fail" );
                row.verdict = QStringLiteral( "fail" );
                row.message = QStringLiteral( "no evidence" );
                return row;
            }
            row.status = QStringLiteral( "pass" );
            row.verdict = QStringLiteral( "pass" );
            row.score = 90.0;
            return row;
        };
    auto again = runBatchAssessment( cfg, gradeAgain, &cancel );
    {
        const QJsonObject ja = report.toJson();
        const QJsonObject jb = again.toJson();
        // Counters + row statuses must match for deterministic regrade.
        REQUIRE( ja.value( QStringLiteral( "total" ) ) == jb.value( QStringLiteral( "total" ) ) );
        REQUIRE( ja.value( QStringLiteral( "graded" ) ) == jb.value( QStringLiteral( "graded" ) ) );
        REQUIRE( ja.value( QStringLiteral( "corrupted" ) ) == jb.value( QStringLiteral( "corrupted" ) ) );
        REQUIRE( ja.value( QStringLiteral( "missing_evidence" ) ) == jb.value( QStringLiteral( "missing_evidence" ) ) );
        REQUIRE( ja.value( QStringLiteral( "rows" ) ).toArray().size()
                 == jb.value( QStringLiteral( "rows" ) ).toArray().size() );
        REQUIRE( regradeTraceability( report ).value( QStringLiteral( "report_digest" ) ).toString()
                 == regradeTraceability( again ).value( QStringLiteral( "report_digest" ) ).toString() );
    }

    const auto summary = buildClassSummary( report );
    REQUIRE( summary.value( QStringLiteral( "schema" ) ).toString()
             == QLatin1String( "sicnu.teaching.class_summary/1" ) );

    FeedbackPackInput fin;
    fin.row = report.rows.first();
    const auto fb = buildFeedbackPack( fin );
    const auto leak = assertNoCrossStudentLeak( fb, fin.row.studentId );
    REQUIRE( leak.ok );
}

TEST_CASE( "release report canonical serialization", "[teaching_admin][preflight]" )
{
    PreflightInput in;
    in.curriculum = minimalCurriculum();
    in.knownLabIds = { QStringLiteral( "lab15_data_inspection" ) };
    in.softwareVersion = QStringLiteral( "test-1" );
    in.labSpec = QJsonObject{
        { QStringLiteral( "schema" ), QStringLiteral( "sicnu.labspec.v1" ) },
        { QStringLiteral( "id" ), QStringLiteral( "lab15_data_inspection" ) },
        { QStringLiteral( "operators" ), QJsonArray{ QStringLiteral( "rs:extract_bands" ) } },
        { QStringLiteral( "steps" ), QJsonArray{} },
    };
    in.knownOperators = { QStringLiteral( "rs:extract_bands" ) };
    const auto a = runPreflight( in );
    const auto b = runPreflight( in );
    REQUIRE( a.canonicalDigest() == b.canonicalDigest() );
    REQUIRE( a.toJson().value( QStringLiteral( "schema" ) ).toString()
             == QLatin1String( "sicnu.teaching_release_report/1" ) );
}

TEST_CASE( "offline bundle verify adapter typed exit", "[teaching_admin][offline]" )
{
    // Missing bundle → exit 2 / unverifiable (structured, not log scrape).
    QTemporaryDir dir;
    const QString repo = QString::fromUtf8( qgetenv( "SICNU_SOURCE_DIR" ) );
    QString root = repo;
    if ( root.isEmpty() )
    {
        // walk up from cwd
        QDir d = QDir::current();
        for ( int i = 0; i < 6; ++i )
        {
            if ( QFileInfo::exists( d.filePath( QStringLiteral( "scripts/verify_bundle_manifest.py" ) ) ) )
            {
                root = d.absolutePath();
                break;
            }
            if ( !d.cdUp() )
                break;
        }
    }
    if ( root.isEmpty() || !QFileInfo::exists( QDir( root ).filePath( QStringLiteral( "scripts/verify_bundle_manifest.py" ) ) ) )
    {
        WARN( "verify_bundle_manifest.py not found; skipping adapter live call" );
        return;
    }
    const auto r = verifyOfflineBundle( root, dir.path() );
    REQUIRE_FALSE( r.ok );
    // either unverifiable or verified-failed is fine; must be structured
    REQUIRE( r.toJson().contains( QStringLiteral( "exit_code" ) ) );
}

TEST_CASE( "unsafe relative path helper", "[teaching_admin][packs]" )
{
    REQUIRE( isUnsafeRelativePath( QStringLiteral( "../x" ) ) );
    REQUIRE( isUnsafeRelativePath( QStringLiteral( "/abs" ) ) );
    REQUIRE( isUnsafeRelativePath( QStringLiteral( "C:/windows" ) ) );
    REQUIRE_FALSE( isUnsafeRelativePath( QStringLiteral( "data/samples/a.tif" ) ) );
}

namespace {

/// Directory of the fake grader CLI helper (same runtime dir as this test),
/// injected by CMake as FAKE_GRADER_CLI_DIR.
QString fakeGraderCliPath()
{
    static QString cached;
    if ( !cached.isEmpty() )
        return cached;
    QString dir = QStringLiteral( FAKE_GRADER_CLI_DIR );
#ifdef Q_OS_WIN
    cached = QDir( dir ).filePath( QStringLiteral( "test_teaching_fake_grader_cli.exe" ) );
#else
    cached = QDir( dir ).filePath( QStringLiteral( "test_teaching_fake_grader_cli" ) );
#endif
    return cached;
}

} // namespace

TEST_CASE( "grader cli resolution: explicit path wins, env honored", "[teaching_admin][grader]" )
{
    REQUIRE( resolveGraderCli( QStringLiteral( "/explicit/grader" ) )
             == QLatin1String( "/explicit/grader" ) );

    qputenv( "SICNU_GEO_RS_CLI", fakeGraderCliPath().toLocal8Bit() );
    const QString resolved = resolveGraderCli();
    REQUIRE( resolved == fakeGraderCliPath() );
    qunsetenv( "SICNU_GEO_RS_CLI" );
}

TEST_CASE( "gradeViaCli: pass/fail/unverifiable/usage over the real CLI contract",
           "[teaching_admin][grader]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QDir root( dir.path() );
    auto write = [&]( const QString &name, const QByteArray &bytes ) {
        QFile f( root.filePath( name ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( bytes );
        return f.fileName();
    };
    const QString good = write( QStringLiteral( "good.tif" ), QByteArray( "GOOD-scene" ) );
    const QString bad = write( QStringLiteral( "bad.tif" ), QByteArray( "BAD-scene" ) );
    const QString unverifiable = write( QStringLiteral( "weird.bin" ), QByteArray( "UNVERIFIABLE" ) );

    GraderCliConfig cfg;
    cfg.cliPath = fakeGraderCliPath();
    cfg.labIdOrRulesPath = QStringLiteral( "lab15_data_inspection" );
    cfg.timeoutMs = 20000;

    // pass
    {
        const auto g = gradeViaCli( cfg, good );
        REQUIRE( g.started );
        REQUIRE( g.exitCode == 0 );
        REQUIRE( g.status == QLatin1String( "pass" ) );
        REQUIRE( g.verdict == QLatin1String( "pass" ) );
        REQUIRE( g.score == Catch::Approx( 88.5 ) );
        REQUIRE( g.reportDigest.size() == 64 );
        REQUIRE( g.unavailableReason.isEmpty() );
    }
    // fail carries the top deduction by MAX WEIGHT (a2 w60 beats a1 w10),
    // never a fabricated pass
    {
        const auto g = gradeViaCli( cfg, bad );
        REQUIRE( g.exitCode == 1 );
        REQUIRE( g.status == QLatin1String( "fail" ) );
        REQUIRE( g.score == Catch::Approx( 40.0 ) );
        REQUIRE( g.topDeduction == QLatin1String( "a2" ) );
    }
    // a grader that crashes after emitting a valid transcript must yield
    // unavailable/grader_crashed — the transcript is never trusted
    {
        const QString crasher = write( QStringLiteral( "boom.tif" ), QByteArray( "CRASH" ) );
        const auto g = gradeViaCli( cfg, crasher );
        REQUIRE( g.started );
        REQUIRE_FALSE( g.timedOut );
        REQUIRE( g.status == QLatin1String( "unavailable" ) );
        REQUIRE( g.unavailableReason == QLatin1String( "grader_crashed" ) );
        REQUIRE( g.score < 0.0 );
    }
    // unverifiable artifact: typed unavailable with reason, score stays < 0
    {
        const auto g = gradeViaCli( cfg, unverifiable );
        REQUIRE( g.exitCode == 3 );
        REQUIRE( g.status == QLatin1String( "unavailable" ) );
        REQUIRE( g.verdict == QLatin1String( "unavailable" ) );
        REQUIRE( g.score < 0.0 );
        REQUIRE( g.unavailableReason == QLatin1String( "artifact_unverifiable" ) );
    }
    // usage: unknown lab → typed error row
    {
        GraderCliConfig usageCfg = cfg;
        usageCfg.labIdOrRulesPath = QStringLiteral( "usage_lab" );
        const auto g = gradeViaCli( usageCfg, good );
        REQUIRE( g.exitCode == 2 );
        REQUIRE( g.status == QLatin1String( "error" ) );
        REQUIRE_FALSE( g.message.isEmpty() );
    }
    // usage: missing artifact
    {
        const auto g = gradeViaCli( cfg, root.filePath( QStringLiteral( "nope.tif" ) ) );
        REQUIRE( g.exitCode == 2 );
        REQUIRE( g.status == QLatin1String( "error" ) );
    }
    // grader binary cannot start → unavailable, never an implicit pass
    {
        GraderCliConfig missing = cfg;
        missing.cliPath = QStringLiteral( "/no/such/grader_binary_xyz" );
        const auto g = gradeViaCli( missing, good );
        REQUIRE_FALSE( g.started );
        REQUIRE( g.status == QLatin1String( "unavailable" ) );
        REQUIRE_FALSE( g.unavailableReason.isEmpty() );
        REQUIRE( g.score < 0.0 );
    }
    // adversarial oracle: a broken authority that exits 0 while the
    // transcript says fail must be refused, not trusted.
    {
        GraderCliConfig mismatch = cfg;
        mismatch.labIdOrRulesPath = QStringLiteral( "mismatch_lab" );
        const auto g = gradeViaCli( mismatch, good );
        REQUIRE( g.status == QLatin1String( "unavailable" ) );
        REQUIRE( g.unavailableReason == QLatin1String( "grader_exit_verdict_mismatch" ) );
        REQUIRE( g.score < 0.0 );
    }
}

TEST_CASE( "batch over grader cli callable: graded/unavailable counters and deterministic regrade",
           "[teaching_admin][grader][batch]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QDir root( dir.path() );
    REQUIRE( root.mkdir( QStringLiteral( "s01" ) ) );
    REQUIRE( root.mkdir( QStringLiteral( "s02" ) ) );
    REQUIRE( root.mkdir( QStringLiteral( "s03" ) ) );
    auto write = [&]( const QString &rel, const QByteArray &bytes ) {
        QFile f( root.filePath( rel ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( bytes );
    };
    write( QStringLiteral( "s01/a.tif" ), QByteArray( "GOOD" ) );
    write( QStringLiteral( "s02/b.tif" ), QByteArray( "BAD" ) );
    write( QStringLiteral( "s03/c.bin" ), QByteArray( "UNVERIFIABLE" ) );

    GraderCliConfig cfg;
    cfg.cliPath = fakeGraderCliPath();
    cfg.labIdOrRulesPath = QStringLiteral( "lab15_data_inspection" );
    cfg.timeoutMs = 20000;

    BatchAssessmentConfig batch;
    batch.labId = cfg.labIdOrRulesPath;
    batch.submissionsDir = dir.path();
    batch.rubricVersion = QStringLiteral( "r1" );
    batch.labVersion = QStringLiteral( "l1" );
    batch.softwareVersion = QStringLiteral( "sw1" );

    const auto run = [&]( BatchAssessmentReport *out ) {
        *out = runBatchAssessment( batch, cliGradeCallable( cfg ) );
    };
    BatchAssessmentReport report;
    run( &report );

    REQUIRE( report.total == 3 );
    REQUIRE( report.graded == 2 ); // pass + fail
    REQUIRE( report.unavailable == 1 ); // unverifiable artifact
    REQUIRE( report.failed == 0 );
    bool sawUnavailableRow = false;
    for ( const auto &row : report.rows )
    {
        if ( row.studentId == QLatin1String( "s03" ) )
        {
            sawUnavailableRow = true;
            REQUIRE( row.status == QLatin1String( "unavailable" ) );
            REQUIRE( row.verdict == QLatin1String( "unavailable" ) );
            REQUIRE( row.score < 0.0 );
            REQUIRE( row.unavailableReason == QLatin1String( "artifact_unverifiable" ) );
        }
        else
        {
            REQUIRE( row.unavailableReason.isEmpty() );
            REQUIRE( row.graderDigest.size() == 64 );
        }
    }
    REQUIRE( sawUnavailableRow );
    REQUIRE( report.toJson().value( QStringLiteral( "unavailable" ) ).toInt() == 1 );

    // deterministic regrade: identical inputs → identical report digest
    BatchAssessmentReport again;
    run( &again );
    REQUIRE( regradeTraceability( report ).value( QStringLiteral( "report_digest" ) ).toString()
             == regradeTraceability( again ).value( QStringLiteral( "report_digest" ) ).toString() );
}

TEST_CASE( "batch with missing grader: every row typed unavailable, none graded",
           "[teaching_admin][grader][batch]" )
{    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QDir root( dir.path() );
    REQUIRE( root.mkdir( QStringLiteral( "s01" ) ) );
    QFile f( root.filePath( QStringLiteral( "s01/a.tif" ) ) );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QByteArray( "GOOD" ) );

    GraderCliConfig cfg;
    cfg.cliPath = QStringLiteral( "/no/such/grader_binary_xyz" );
    cfg.labIdOrRulesPath = QStringLiteral( "lab15_data_inspection" );

    BatchAssessmentConfig batch;
    batch.labId = cfg.labIdOrRulesPath;
    batch.submissionsDir = dir.path();

    const auto report = runBatchAssessment( batch, cliGradeCallable( cfg ) );
    REQUIRE( report.total == 1 );
    REQUIRE( report.graded == 0 );
    REQUIRE( report.unavailable == 1 );
    for ( const auto &row : report.rows )
    {
        REQUIRE( row.status == QLatin1String( "unavailable" ) );
        REQUIRE( row.score < 0.0 );
        REQUIRE_FALSE( row.unavailableReason.isEmpty() );
        REQUIRE( row.toJson().value( QStringLiteral( "unavailable_reason" ) ).toString()
                 == row.unavailableReason );
    }
}

TEST_CASE( "operator catalog: sidecars feed ids and param schemas, broken files fail closed",
           "[teaching_admin][operators]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QDir cap( dir.path() );

    auto write = [&]( const QString &name, const QByteArray &bytes ) {
        QFile f( cap.filePath( name ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( bytes );
    };
    write( QStringLiteral( "rs-ndvi.json" ),
           QByteArray( R"({"id":"rs:ndvi","schema_version":2,"capability":{"io":{"parameters":[)"
                       R"({"name":"red","type":"integer","required":false},)"
                       R"({"name":"nir","type":"integer","required":false},)"
                       R"({"name":"output","type":"string","required":true}]}}})" ) );
    write( QStringLiteral( "rs-extract_bands.json" ),
           QByteArray( R"({"id":"rs:extract_bands","capability":{"io":{"parameters":[]}}})" ) );
    write( QStringLiteral( "capability_relations.json" ), QByteArray( "{}" ) );
    write( QStringLiteral( "rs-broken.json" ), QByteArray( "not json" ) );
    write( QStringLiteral( "rs-noid.json" ), QByteArray( R"({"capability":{}})" ) );

    const auto cat = loadOperatorCatalog( dir.path() );
    REQUIRE( cat.operatorIds.contains( QStringLiteral( "rs:ndvi" ) ) );
    REQUIRE( cat.operatorIds.contains( QStringLiteral( "rs:extract_bands" ) ) );
    REQUIRE_FALSE( cat.operatorIds.contains( QStringLiteral( "unknown:op" ) ) );
    REQUIRE( cat.issues.size() == 2 ); // broken + id-less, fail-closed

    // The schema shape matches validateLabSpec's operatorParamSchemas contract.
    const auto props = cat.paramSchemas.value( QStringLiteral( "rs:ndvi" ) )
                         .toObject()
                         .value( QStringLiteral( "properties" ) )
                         .toObject();
    REQUIRE( props.contains( QStringLiteral( "red" ) ) );
    REQUIRE( props.contains( QStringLiteral( "nir" ) ) );
    REQUIRE( props.contains( QStringLiteral( "output" ) ) );

    // Unknown-operator fail-closed still holds with the real-shaped catalog.
    QJsonObject spec{
        { QStringLiteral( "schema" ), QStringLiteral( "sicnu.labspec.v1" ) },
        { QStringLiteral( "id" ), QStringLiteral( "demo" ) },
        { QStringLiteral( "operators" ), QJsonArray{ QStringLiteral( "rs:ndvi" ), QStringLiteral( "rs:nonexistent" ) } },
        { QStringLiteral( "steps" ),
          QJsonArray{ QJsonObject{
              { QStringLiteral( "operator_id" ), QStringLiteral( "rs:ndvi" ) },
              { QStringLiteral( "params" ), QJsonObject{ { QStringLiteral( "bogus_param" ), 1 } } } } } },
    };
    const auto vr = validateLabSpec( spec, cat.operatorIds, cat.paramSchemas );
    REQUIRE_FALSE( vr.ok );
    QSet<QString> codes;
    for ( const auto &i : vr.issues )
        codes.insert( i.code );
    REQUIRE( codes.contains( QStringLiteral( "unknown_operator" ) ) );
    REQUIRE( codes.contains( QStringLiteral( "invalid_param" ) ) );
}

TEST_CASE( "operator catalog: missing dir is a typed issue, empty allow-set stays fail-closed",
           "[teaching_admin][operators]" )
{
    const auto cat = loadOperatorCatalog( QStringLiteral( "/no/such/capability_dir" ) );
    REQUIRE( cat.operatorIds.isEmpty() );
    REQUIRE( cat.issues.size() == 1 );
    REQUIRE( cat.issues.first().code == QLatin1String( "operator_registry_missing" ) );

    // With no registry, any operator reference must be flagged (empty
    // knownOperators must not silently pass — never a demo allow-list).
    QJsonObject spec{
        { QStringLiteral( "schema" ), QStringLiteral( "sicnu.labspec.v1" ) },
        { QStringLiteral( "id" ), QStringLiteral( "demo" ) },
        { QStringLiteral( "steps" ),
          QJsonArray{ QJsonObject{ { QStringLiteral( "operator_id" ), QStringLiteral( "rs:ndvi" ) } } } },
    };
    const auto vr = validateLabSpec( spec, cat.operatorIds, cat.paramSchemas );
    REQUIRE_FALSE( vr.ok );
    bool unknown = false;
    for ( const auto &i : vr.issues )
        if ( i.code == QLatin1String( "unknown_operator" ) )
            unknown = true;
    REQUIRE( unknown );
}

TEST_CASE( "operator catalog: repo capability sidecars are the real truth",
           "[teaching_admin][operators][repo]" )
{
    QString repo = QString::fromUtf8( qgetenv( "SICNU_SOURCE_DIR" ) );
    if ( repo.isEmpty() )
    {
        QDir d = QDir::current();
        for ( int i = 0; i < 6; ++i )
        {
            if ( QFileInfo::exists( d.filePath(
                   QStringLiteral( "data/processing/algorithm_meta/capability/rs-ndvi.json" ) ) ) )
            {
                repo = d.absolutePath();
                break;
            }
            if ( !d.cdUp() )
                break;
        }
    }
    const QString capDir = QDir( repo ).filePath(
      QStringLiteral( "data/processing/algorithm_meta/capability" ) );
    if ( !QFileInfo::exists( QDir( capDir ).filePath( QStringLiteral( "rs-ndvi.json" ) ) ) )
    {
        WARN( "repo capability sidecars not found; skipping registry truth check" );
        return;
    }
    const auto cat = loadOperatorCatalog( capDir );
    REQUIRE( cat.operatorIds.size() > 50 ); // the registry is large, not a demo trio
    REQUIRE( cat.operatorIds.contains( QStringLiteral( "rs:ndvi" ) ) );
    REQUIRE( cat.operatorIds.contains( QStringLiteral( "rs:extract_bands" ) ) );
    REQUIRE( cat.issues.isEmpty() );
    // A hardcoded allow-list ("rs:extract_bands,rs:resample,rs:ndvi") would
    // have rejected the real breadth; the sidecar set must cover e.g. rs:pca.
    REQUIRE( cat.operatorIds.contains( QStringLiteral( "rs:pca" ) ) );
}

TEST_CASE( "labspec version contract is fail-closed", "[teaching_admin][labspec]" )
{
    QSet<QString> ops{ QStringLiteral( "rs:ndvi" ) };
    // D3 schema string is pinned
    {
        QJsonObject spec{
            { QStringLiteral( "schema" ), QStringLiteral( "sicnu.labspec.v99" ) },
            { QStringLiteral( "id" ), QStringLiteral( "demo" ) },
        };
        const auto r = validateLabSpec( spec, ops );
        REQUIRE_FALSE( r.ok );
        bool mismatch = false;
        for ( const auto &i : r.issues )
            if ( i.code == QLatin1String( "schema_mismatch" ) && i.path == QLatin1String( "schema" ) )
                mismatch = true;
        REQUIRE( mismatch );
    }
    // D2 spec_version must be 1|2|3
    {
        QJsonObject spec{
            { QStringLiteral( "spec_version" ), 7 },
            { QStringLiteral( "id" ), QStringLiteral( "demo" ) },
        };
        const auto r = validateLabSpec( spec, ops );
        REQUIRE_FALSE( r.ok );
        bool mismatch = false;
        for ( const auto &i : r.issues )
            if ( i.code == QLatin1String( "schema_mismatch" )
                 && i.path == QLatin1String( "spec_version" ) )
                mismatch = true;
        REQUIRE( mismatch );
    }
    // legal versions still pass (no schema_mismatch among issues)
    for ( const int v : { 1, 2, 3 } )
    {
        QJsonObject spec{
            { QStringLiteral( "schema" ), QStringLiteral( "sicnu.labspec.v1" ) },
            { QStringLiteral( "spec_version" ), v },
            { QStringLiteral( "id" ), QStringLiteral( "demo" ) },
        };
        const auto r = validateLabSpec( spec, ops );
        for ( const auto &i : r.issues )
            REQUIRE( i.code != QLatin1String( "schema_mismatch" ) );
    }
    // offline must hold when data.offline is declared false
    {
        QJsonObject spec{
            { QStringLiteral( "schema" ), QStringLiteral( "sicnu.labspec.v1" ) },
            { QStringLiteral( "id" ), QStringLiteral( "demo" ) },
            { QStringLiteral( "data" ),
              QJsonObject{ { QStringLiteral( "spec_ref" ), QStringLiteral( "data/labs/data-specs/demo.json" ) },
                           { QStringLiteral( "offline" ), false } } },
        };
        const auto r = validateLabSpec( spec, ops );
        REQUIRE_FALSE( r.ok );
        bool offline = false;
        for ( const auto &i : r.issues )
            if ( i.code == QLatin1String( "offline_required" ) )
                offline = true;
        REQUIRE( offline );
    }
}

TEST_CASE( "labspec dangling refs are typed when a root is provided", "[teaching_admin][labspec]" )
{
    QTemporaryDir repo;
    REQUIRE( repo.isValid() );
    REQUIRE( QDir( repo.path() ).mkpath( QStringLiteral( "data/labs/grading" ) ) );
    {
        QFile intent( QDir( repo.path() ).filePath( QStringLiteral( "data/labs/grading/demo.intent.json" ) ) );
        REQUIRE( intent.open( QIODevice::WriteOnly ) );
        intent.write( QByteArray( "{}" ) );
    }

    QJsonObject spec{
        { QStringLiteral( "schema" ), QStringLiteral( "sicnu.labspec.v1" ) },
        { QStringLiteral( "id" ), QStringLiteral( "demo" ) },
        { QStringLiteral( "data" ),
          QJsonObject{ { QStringLiteral( "spec_ref" ), QStringLiteral( "data/labs/data-specs/demo.json" ) },
                       { QStringLiteral( "offline" ), true } } },
        { QStringLiteral( "grading_ref" ),
          QJsonObject{ { QStringLiteral( "intent_ref" ),
                         QStringLiteral( "data/labs/grading/demo.intent.json" ) },
                       { QStringLiteral( "grader_owner" ), QStringLiteral( "D4" ) } } },
    };
    QSet<QString> ops{ QStringLiteral( "rs:ndvi" ) };

    // intent_ref resolves, spec_ref dangles
    const auto r = validateLabSpec( spec, ops, {}, repo.path() );
    REQUIRE_FALSE( r.ok );
    bool dangling = false;
    bool wrongPath = true;
    for ( const auto &i : r.issues )
    {
        if ( i.code == QLatin1String( "dangling_ref" ) && i.path == QLatin1String( "data.spec_ref" ) )
            dangling = true;
        if ( i.code == QLatin1String( "dangling_ref" ) && i.path == QLatin1String( "grading_ref.intent_ref" ) )
            wrongPath = false;
    }
    REQUIRE( dangling );
    REQUIRE( wrongPath );

    // without a root: no existence checks at all (pure structural lint)
    const auto structural = validateLabSpec( spec, ops );
    for ( const auto &i : structural.issues )
        REQUIRE( i.code != QLatin1String( "dangling_ref" ) );

    // escape attempt is unsafe even without a root
    QJsonObject escaping = spec;
    escaping.insert( QStringLiteral( "grading_ref" ),
                     QJsonObject{ { QStringLiteral( "intent_ref" ), QStringLiteral( "../secrets.json" ) } } );
    const auto unsafe = validateLabSpec( escaping, ops, {}, repo.path() );
    bool unsafeSeen = false;
    for ( const auto &i : unsafe.issues )
        if ( i.code == QLatin1String( "unsafe_ref" ) )
            unsafeSeen = true;
    REQUIRE( unsafeSeen );
}

TEST_CASE( "pack validation enforces containment and provenance contract",
           "[teaching_admin][packs]" )
{
    QTemporaryDir repo;
    REQUIRE( repo.isValid() );
    REQUIRE( QDir( repo.path() ).mkpath( QStringLiteral( "data" ) ) );
    {
        QFile f( QDir( repo.path() ).filePath( QStringLiteral( "data/ok.tif" ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "GRID" ) );
    }
    const QString root = QDir( repo.path() ).canonicalPath();

    auto makePack = []( const QJsonArray &inputs ) {
        return QJsonObject{ { QStringLiteral( "schema_version" ), QStringLiteral( "sicnu.lab-pack/1" ) },
                            { QStringLiteral( "inputs" ), inputs } };
    };

    // in-root input is fine (role required by the authority)
    {
        const auto r = validatePackDocument(
          makePack( QJsonArray{ QJsonObject{ { QStringLiteral( "path" ), QStringLiteral( "data/ok.tif" ) },
                                             { QStringLiteral( "role" ), QStringLiteral( "sample" ) } } } ),
          root );
        REQUIRE( r.ok );
    }
    // missing role is now a typed error, as in the pack authority loader
    {
        const auto r = validatePackDocument(
          makePack( QJsonArray{ QJsonObject{ { QStringLiteral( "path" ), QStringLiteral( "data/ok.tif" ) } } } ),
          root );
        REQUIRE_FALSE( r.ok );
        bool missingRole = false;
        for ( const auto &i : r.issues )
            if ( i.code == QLatin1String( "missing_role" ) )
                missingRole = true;
        REQUIRE( missingRole );
    }
    // backslash paths are rejected, not normalized
    {
        const auto r = validatePackDocument(
          makePack( QJsonArray{ QJsonObject{ { QStringLiteral( "path" ), QStringLiteral( "data\\ok.tif" ) },
                                             { QStringLiteral( "role" ), QStringLiteral( "sample" ) } } } ),
          root );
        REQUIRE_FALSE( r.ok );
        bool backslash = false;
        for ( const auto &i : r.issues )
            if ( i.code == QLatin1String( "backslash_path" ) )
                backslash = true;
        REQUIRE( backslash );
    }
    // lexical escape
    {
        const auto r = validatePackDocument(
          makePack( QJsonArray{ QJsonObject{
            { QStringLiteral( "path" ), QStringLiteral( "data/../../outside.tif" ) },
            { QStringLiteral( "role" ), QStringLiteral( "sample" ) } } } ),
          root );
        REQUIRE_FALSE( r.ok );
        bool traversal = false;
        for ( const auto &i : r.issues )
            if ( i.code == QLatin1String( "path_traversal" ) )
                traversal = true;
        REQUIRE( traversal );
    }
#if !defined(Q_OS_WIN)
    // symlink escape (canonical target outside the root)
    {
        const QString outside = QDir::temp().filePath( QStringLiteral( "pack_escape_probe.tif" ) );
        {
            QFile f( outside );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( QByteArray( "outside" ) );
        }
        const QString link = QDir( repo.path() ).filePath( QStringLiteral( "data/link.tif" ) );
        QFile::remove( link );
        REQUIRE( QFile::link( outside, link ) );
        const auto r = validatePackDocument(
          makePack( QJsonArray{ QJsonObject{ { QStringLiteral( "path" ), QStringLiteral( "data/link.tif" ) },
                                             { QStringLiteral( "role" ), QStringLiteral( "sample" ) } } } ),
          root );
        REQUIRE_FALSE( r.ok );
        bool escape = false;
        for ( const auto &i : r.issues )
            if ( i.code == QLatin1String( "path_escape" ) )
                escape = true;
        REQUIRE( escape );
        QFile::remove( link );
        QFile::remove( outside );
    }
#endif
    // committed fixture requires a sha256 pin; bad provenance is rejected
    {
        const auto r = validatePackDocument(
          makePack( QJsonArray{
            QJsonObject{ { QStringLiteral( "path" ), QStringLiteral( "data/ok.tif" ) },
                         { QStringLiteral( "role" ), QStringLiteral( "fixture" ) },
                         { QStringLiteral( "bytes" ), 4096 },
                         { QStringLiteral( "provenance" ), QStringLiteral( "committed-fixture" ) } },
            QJsonObject{ { QStringLiteral( "path" ), QStringLiteral( "data/ok.tif" ) },
                         { QStringLiteral( "role" ), QStringLiteral( "fixture" ) },
                         { QStringLiteral( "provenance" ), QStringLiteral( "random-internet" ) } } } ),
          root );
        REQUIRE_FALSE( r.ok );
        bool missingSha = false;
        bool badProv = false;
        for ( const auto &i : r.issues )
        {
            if ( i.code == QLatin1String( "missing_sha256" ) )
                missingSha = true;
            if ( i.code == QLatin1String( "invalid_provenance" ) )
                badProv = true;
        }
        REQUIRE( missingSha );
        REQUIRE( badProv );
    }
}

TEST_CASE( "pack inventory verifies digests, byte pins and presence by tier",
           "[teaching_admin][packs]" )
{
    QTemporaryDir repo;
    REQUIRE( repo.isValid() );
    QDir root( repo.path() );
    REQUIRE( root.mkpath( QStringLiteral( "data/labs/packs" ) ) );

    const QByteArray goodBytes = QByteArray( "deterministic scene bytes" );
    const QString goodSha = sha256Hex( goodBytes );
    {
        QFile f( root.filePath( QStringLiteral( "data/scene.tif" ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( goodBytes );
    }

    auto writePack = [&]( const QString &name, const QJsonObject &pack ) {
        QFile f( root.filePath( QStringLiteral( "data/labs/packs/%1" ).arg( name ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QJsonDocument( pack ).toJson( QJsonDocument::Indented ) );
    };

    QJsonObject goodPack{
        { QStringLiteral( "schema_version" ), QStringLiteral( "sicnu.lab-pack/1" ) },
        { QStringLiteral( "lab_id" ), QStringLiteral( "demo" ) },
        { QStringLiteral( "pack_version" ), QStringLiteral( "1" ) },
        { QStringLiteral( "declared_offline_bytes" ), static_cast<double>( goodBytes.size() ) },
        { QStringLiteral( "inputs" ),
          QJsonArray{ QJsonObject{
            { QStringLiteral( "path" ), QStringLiteral( "data/scene.tif" ) },
            { QStringLiteral( "role" ), QStringLiteral( "fixture" ) },
            { QStringLiteral( "provenance" ), QStringLiteral( "committed-fixture" ) },
            { QStringLiteral( "sha256" ), goodSha },
            { QStringLiteral( "bytes" ), static_cast<double>( goodBytes.size() ) } } } },
    };
    writePack( QStringLiteral( "demo.pack.json" ), goodPack );

    // committed fixture with correct digest → available, no issues
    {
        const auto inv = inventoryPacks( root.filePath( QStringLiteral( "data/labs/packs" ) ),
                                         root.path(), 1024 * 1024 * 1024 );
        REQUIRE( inv.packs.size() == 1 );
        REQUIRE( inv.packs.first().offlineAvailable );
        REQUIRE( inv.packs.first().issues.isEmpty() );
        // P1 regression: the declared byte sum used to be dropped to 0,
        // silently making withinBudget trivially true.
        REQUIRE( inv.packs.first().declaredBytes == static_cast<qint64>( goodBytes.size() ) );
        REQUIRE( inv.packs.first().computedBytes == static_cast<qint64>( goodBytes.size() ) );
        REQUIRE( inv.withinBudget );
    }

    // corrupt the fixture → digest_mismatch, offlineAvailable false
    {
        QFile f( root.filePath( QStringLiteral( "data/scene.tif" ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "tampered scene bytes" ) );
        const auto inv = inventoryPacks( root.filePath( QStringLiteral( "data/labs/packs" ) ),
                                         root.path(), 1024 * 1024 * 1024 );
        REQUIRE( inv.packs.size() == 1 );
        REQUIRE_FALSE( inv.packs.first().offlineAvailable );
        bool digest = false;
        for ( const auto &i : inv.packs.first().issues )
            if ( i.code == QLatin1String( "digest_mismatch" ) )
                digest = true;
        REQUIRE( digest );
    }

    // restore bytes; make the pin lie about the size (committed → error)
    {
        QFile f( root.filePath( QStringLiteral( "data/scene.tif" ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( goodBytes );
        goodPack[QStringLiteral( "inputs" )] = QJsonArray{ QJsonObject{
          { QStringLiteral( "path" ), QStringLiteral( "data/scene.tif" ) },
          { QStringLiteral( "role" ), QStringLiteral( "fixture" ) },
          { QStringLiteral( "provenance" ), QStringLiteral( "committed-fixture" ) },
          { QStringLiteral( "sha256" ), goodSha },
          { QStringLiteral( "bytes" ), static_cast<double>( goodBytes.size() + 999 ) } } };
        writePack( QStringLiteral( "demo.pack.json" ), goodPack );
        const auto inv = inventoryPacks( root.filePath( QStringLiteral( "data/labs/packs" ) ),
                                         root.path(), 1024 * 1024 * 1024 );
        REQUIRE_FALSE( inv.packs.first().offlineAvailable );
        bool bytePin = false;
        for ( const auto &i : inv.packs.first().issues )
            if ( i.code == QLatin1String( "byte_mismatch" ) && i.severity == QLatin1String( "error" ) )
                bytePin = true;
        REQUIRE( bytePin );
    }

    // generated input missing → typed warning, not available but not silent
    {
        QJsonObject generatedPack{
            { QStringLiteral( "schema_version" ), QStringLiteral( "sicnu.lab-pack/1" ) },
            { QStringLiteral( "lab_id" ), QStringLiteral( "gen" ) },
            { QStringLiteral( "inputs" ),
              QJsonArray{ QJsonObject{
                { QStringLiteral( "path" ), QStringLiteral( "data/labs/_tmp/absent.tif" ) },
                { QStringLiteral( "role" ), QStringLiteral( "sample" ) },
                { QStringLiteral( "provenance" ), QStringLiteral( "generated-samples" ) } } } },
        };
        writePack( QStringLiteral( "gen.pack.json" ), generatedPack );
        const auto inv = inventoryPacks( root.filePath( QStringLiteral( "data/labs/packs" ) ),
                                         root.path(), 1024 * 1024 * 1024 );
        REQUIRE( inv.packs.size() == 2 );
        const auto &gen = inv.packs.last();
        REQUIRE_FALSE( gen.offlineAvailable );
        bool warned = false;
        for ( const auto &i : gen.issues )
            if ( i.code == QLatin1String( "input_missing" ) && i.severity == QLatin1String( "warning" ) )
                warned = true;
        REQUIRE( warned );
    }
}

namespace {

QJsonObject labspecWithAnswers()
{
    return QJsonObject{
        { QStringLiteral( "schema" ), QStringLiteral( "sicnu.labspec.v1" ) },
        { QStringLiteral( "id" ), QStringLiteral( "lab99_leak_probe" ) },
        { QStringLiteral( "version" ), QStringLiteral( "3" ) },
        { QStringLiteral( "title" ), QStringLiteral( "Leak Probe" ) },
        { QStringLiteral( "data" ),
          QJsonObject{ { QStringLiteral( "spec_ref" ), QStringLiteral( "data/labs/data-specs/x.json" ) },
                       { QStringLiteral( "offline" ), true } } },
        { QStringLiteral( "grading_ref" ),
          QJsonObject{ { QStringLiteral( "intent_ref" ),
                         QStringLiteral( "data/labs/grading/lab99.intent.json" ) },
                       { QStringLiteral( "grader_owner" ), QStringLiteral( "D4" ) } } },
        { QStringLiteral( "expected_results" ),
          QJsonArray{ QJsonObject{
            { QStringLiteral( "claim" ),
              QStringLiteral( "输出栅格的 NDVI 均值应为 0.42 ± 0.01" ) },
            { QStringLiteral( "artifact" ), QStringLiteral( "data/labs/_tmp/out/ndvi.tif" ) } } } },
        { QStringLiteral( "questions" ),
          QJsonArray{ QJsonObject{ { QStringLiteral( "prompt" ), QStringLiteral( "为什么需要检查日期完整性？" ) },
                                   { QStringLiteral( "hint" ), QStringLiteral( "rs:temporal_* 前置检查" ) } } } },
        { QStringLiteral( "operators" ), QJsonArray{ QStringLiteral( "rs:ndvi" ) } },
        { QStringLiteral( "steps" ),
          QJsonArray{ QJsonObject{
            { QStringLiteral( "id" ), QStringLiteral( "s1" ) },
            { QStringLiteral( "title" ), QStringLiteral( "计算 NDVI" ) },
            { QStringLiteral( "operator_id" ), QStringLiteral( "rs:ndvi" ) },
            { QStringLiteral( "params" ),
              QJsonObject{ { QStringLiteral( "red" ), 3 },
                           { QStringLiteral( "nir" ), 4 },
                           { QStringLiteral( "secret_formula" ), QStringLiteral( "band0*1.7-band1" ) } } } },
          QJsonObject{
            { QStringLiteral( "id" ), QStringLiteral( "s2" ) },
            { QStringLiteral( "title" ), QStringLiteral( "记录结果" ) },
            { QStringLiteral( "human_only" ), true } } } },
    };
}

} // namespace

TEST_CASE( "student projection masks answers and keeps the teacher view as truth",
           "[teaching_admin][projection]" )
{
    const auto spec = labspecWithAnswers();
    const auto student = projectStudentLabView( spec );
    const auto teacher = projectRecipeCompileView( spec );

    // shared truth: identical lab id and step order in both projections
    REQUIRE( student.value( QStringLiteral( "lab_id" ) ) == teacher.value( QStringLiteral( "lab_id" ) ) );
    const QJsonArray studentSteps = student.value( QStringLiteral( "steps" ) ).toArray();
    const QJsonArray teacherSteps = teacher.value( QStringLiteral( "steps" ) ).toArray();
    REQUIRE( studentSteps.size() == teacherSteps.size() );
    for ( int i = 0; i < studentSteps.size(); ++i )
        REQUIRE( studentSteps.at( i ).toObject().value( QStringLiteral( "id" ) )
                 == teacherSteps.at( i ).toObject().value( QStringLiteral( "id" ) ) );

    // teacher-only fields are absent
    REQUIRE_FALSE( student.contains( QStringLiteral( "grading_ref" ) ) );
    REQUIRE_FALSE( student.contains( QStringLiteral( "expected_results" ) ) );

    // param names stay, values are masked
    const QJsonObject params =
      studentSteps.at( 0 ).toObject().value( QStringLiteral( "params" ) ).toObject();
    REQUIRE( params.contains( QStringLiteral( "red" ) ) );
    REQUIRE( params.value( QStringLiteral( "red" ) ) == QLatin1String( "***" ) );
    REQUIRE( params.value( QStringLiteral( "secret_formula" ) ) == QLatin1String( "***" ) );
    REQUIRE( params.value( QStringLiteral( "secret_formula" ) )
             != spec.value( QStringLiteral( "steps" ) )
                  .toArray()
                  .at( 0 )
                  .toObject()
                  .value( QStringLiteral( "params" ) )
                  .toObject()
                  .value( QStringLiteral( "secret_formula" ) ) );

    // student-visible pedagogy survives
    REQUIRE( student.contains( QStringLiteral( "questions" ) ) );
    REQUIRE( studentSteps.at( 1 ).toObject().value( QStringLiteral( "human_only" ) ) == true );

    // deterministic serialization
    REQUIRE( canonicalJsonBytes( projectStudentLabView( spec ) )
             == canonicalJsonBytes( projectStudentLabView( spec ) ) );
}

TEST_CASE( "leak oracle passes the real projection and kills mutations",
           "[teaching_admin][projection][adversarial]" )
{
    const auto spec = labspecWithAnswers();
    const auto student = projectStudentLabView( spec );

    // the real projection must pass its own oracle
    const auto clean = assertNoAnswerLeak( student, spec );
    REQUIRE( clean.ok );

    // MUTATION ORACLE — each tampered view must be caught:
    auto mustFail = [&]( const QJsonObject &mutant, const QString &expectedCode ) {
        const auto r = assertNoAnswerLeak( mutant, spec );
        REQUIRE_FALSE( r.ok );
        bool found = false;
        for ( const auto &i : r.issues )
            if ( i.code == expectedCode )
                found = true;
        REQUIRE( found );
    };

    // (1) grading_ref re-added
    QJsonObject m1 = student;
    m1.insert( QStringLiteral( "grading_ref" ), spec.value( QStringLiteral( "grading_ref" ) ) );
    mustFail( m1, QStringLiteral( "teacher_only_field" ) );

    // (2) an expected_results claim smuggled into a step title
    QJsonObject m2 = student;
    {
        QJsonArray steps = m2.value( QStringLiteral( "steps" ) ).toArray();
        QJsonObject s2 = steps.at( 1 ).toObject();
        s2.insert( QStringLiteral( "title" ),
                   QStringLiteral( "结果应说明 NDVI 均值应为 0.42 ± 0.01" ) );
        steps.replace( 1, s2 );
        m2.insert( QStringLiteral( "steps" ), steps );
    }
    mustFail( m2, QStringLiteral( "answer_leak" ) );

    // (3) a param value left unmasked at its position
    QJsonObject m3 = student;
    {
        QJsonArray steps = m3.value( QStringLiteral( "steps" ) ).toArray();
        QJsonObject s1 = steps.at( 0 ).toObject();
        QJsonObject params = s1.value( QStringLiteral( "params" ) ).toObject();
        params.insert( QStringLiteral( "secret_formula" ),
                       QStringLiteral( "band0*1.7-band1" ) );
        s1.insert( QStringLiteral( "params" ), params );
        steps.replace( 0, s1 );
        m3.insert( QStringLiteral( "steps" ), steps );
    }
    mustFail( m3, QStringLiteral( "answer_leak" ) );

    // (4) the grading intent path quoted in the note
    QJsonObject m4 = student;
    m4.insert( QStringLiteral( "note" ),
               QStringLiteral( "see data/labs/grading/lab99.intent.json for answers" ) );
    mustFail( m4, QStringLiteral( "answer_leak" ) );
}

TEST_CASE( "bundle manifest inspection + version pin", "[teaching_admin][offline]" )
{
    QTemporaryDir bundle;
    REQUIRE( bundle.isValid() );
    // no manifest → typed issue, not ok
    {
        const auto info = inspectBundleManifest( bundle.path() );
        REQUIRE_FALSE( info.ok );
        REQUIRE( info.issues.size() == 1 );
        REQUIRE( info.issues.first().code == QLatin1String( "manifest_unreadable" ) );
    }
    // manifest with version + files
    {
        {
            QFile f( QDir( bundle.path() ).filePath( QStringLiteral( "manifest.json" ) ) );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( QByteArray( R"({"schema":"sicnu.offline_bundle/2","bundle_version":"v2026.09","files":[{},{}]})" ) );
        }
        const auto info = inspectBundleManifest( bundle.path() );
        REQUIRE( info.ok );
        REQUIRE( info.schema == QLatin1String( "sicnu.offline_bundle/2" ) );
        REQUIRE( info.bundleVersion == QLatin1String( "v2026.09" ) );
        REQUIRE( info.fileCount == 2 );
        REQUIRE( info.issues.isEmpty() );
    }
}

TEST_CASE( "bundle builder argv selects the platform twin script",
           "[teaching_admin][offline]" )
{
    const auto posix = bundleBuilderRequest( QStringLiteral( "/repo" ), QStringLiteral( "/build" ),
                                             QStringLiteral( "/out" ), QStringLiteral( "v1" ), 250, false );
    REQUIRE( posix.program == QLatin1String( "bash" ) );
    REQUIRE( posix.arguments.first().endsWith( QLatin1String( "build_offline_bundle.sh" ) ) );

    const auto win = bundleBuilderRequest( QStringLiteral( "/repo" ), QStringLiteral( "/build" ),
                                           QStringLiteral( "/out" ), QStringLiteral( "v1" ), 250, true );
    REQUIRE( win.program == QLatin1String( "cmd.exe" ) );
    REQUIRE( win.arguments.contains( QLatin1String( "/c" ) ) );
    REQUIRE( win.arguments.at( 1 ).endsWith( QLatin1String( "build_offline_bundle.cmd" ) ) );

    // both carry the same option contract; version appended only when set
    for ( const auto *req : { &posix, &win } )
    {
        REQUIRE( req->arguments.contains( QLatin1String( "--build-dir" ) ) );
        REQUIRE( req->arguments.contains( QLatin1String( "--out" ) ) );
        REQUIRE( req->arguments.contains( QLatin1String( "--max-mb" ) ) );
        REQUIRE( req->arguments.contains( QLatin1String( "--version" ) ) );
    }
    const auto noVersion =
      bundleBuilderRequest( QStringLiteral( "/repo" ), QStringLiteral( "/b" ), QStringLiteral( "/o" ),
                            QString(), 250, false );
    REQUIRE_FALSE( noVersion.arguments.contains( QLatin1String( "--version" ) ) );
}

TEST_CASE( "verifyOfflineBundle flags a broken version pin", "[teaching_admin][offline]" )
{
    // Hermetic: a fake canonical verifier (same exit contract) + a manifest.
    QTemporaryDir repo;
    REQUIRE( repo.isValid() );
    REQUIRE( QDir( repo.path() ).mkpath( QStringLiteral( "scripts" ) ) );
    {
        QFile f( QDir( repo.path() ).filePath( QStringLiteral( "scripts/verify_bundle_manifest.py" ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "#!/usr/bin/env python3\n"
                             "import sys\n"
                             "print('VERDICT OK')\n"
                             "sys.exit(0)\n" ) );
    }
    QTemporaryDir bundle;
    REQUIRE( bundle.isValid() );
    {
        QFile f( QDir( bundle.path() ).filePath( QStringLiteral( "manifest.json" ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( R"({"schema":"sicnu.offline_bundle/2","bundle_version":"v2026.09","files":[]})" ) );
    }

    // expected matches declared → ok
    const auto okRun = verifyOfflineBundle( repo.path(), bundle.path(), QStringLiteral( "python3" ),
                                            QStringLiteral( "v2026.09" ) );
    REQUIRE( okRun.verifiable );
    REQUIRE( okRun.ok );
    REQUIRE( okRun.bundleVersion == QLatin1String( "v2026.09" ) );
    REQUIRE( okRun.manifestSchema == QLatin1String( "sicnu.offline_bundle/2" ) );

    // expected differs → typed failed verification, never a silent pass
    const auto mismatch = verifyOfflineBundle( repo.path(), bundle.path(), QStringLiteral( "python3" ),
                                               QStringLiteral( "vOLD" ) );
    REQUIRE( mismatch.verifiable );
    REQUIRE_FALSE( mismatch.ok );
    REQUIRE( mismatch.summary == QLatin1String( "version_mismatch" ) );
    bool pinned = false;
    for ( const auto &f : mismatch.findings )
        if ( f.startsWith( QLatin1String( "version_mismatch:" ) ) )
            pinned = true;
    REQUIRE( pinned );
}

TEST_CASE( "foundry drift check reuses gen_lab_packs.py --check contract",
           "[teaching_admin][offline]" )
{
    QTemporaryDir repo;
    REQUIRE( repo.isValid() );
    REQUIRE( QDir( repo.path() ).mkpath( QStringLiteral( "scripts" ) ) );
    const QString python = QStandardPaths::findExecutable( QStringLiteral( "python3" ) );
    if ( python.isEmpty() )
    {
        WARN( "python3 not available; skipping foundry drift adapter test" );
        return;
    }
    // in sync: exit 0
    {
        {
            QFile f( QDir( repo.path() ).filePath( QStringLiteral( "scripts/gen_lab_packs.py" ) ) );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( QByteArray( "import sys\nprint('packs in sync')\nsys.exit(0)\n" ) );
        }
        const auto check = checkLabPackDrift( repo.path(), python );
        REQUIRE( check.ran );
        REQUIRE( check.inSync );
        REQUIRE( check.driftFiles.isEmpty() );
    }
    // drift: exit 1 + DRIFT lines parsed into typed results
    {
        {
            QFile f( QDir( repo.path() ).filePath( QStringLiteral( "scripts/gen_lab_packs.py" ) ) );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( QByteArray( "import sys\n"
                                 "print('DRIFT data/labs/packs/lab01.pack.json')\n"
                                 "print('DRIFT data/labs/packs/lab15.pack.json')\n"
                                 "sys.exit(1)\n" ) );
        }
        const auto check = checkLabPackDrift( repo.path(), python );
        REQUIRE( check.ran );
        REQUIRE_FALSE( check.inSync );
        REQUIRE( check.exitCode == 1 );
        REQUIRE( check.driftFiles.size() == 2 );
        REQUIRE( check.driftFiles.first()
                 == QLatin1String( "data/labs/packs/lab01.pack.json" ) );
        REQUIRE( check.summary.contains( QLatin1String( "drift" ) ) );
    }
    // missing script is typed, not a crash
    {
        const auto missing = checkLabPackDrift( QStringLiteral( "/no/such/repo" ), python );
        REQUIRE_FALSE( missing.ran );
        REQUIRE( missing.summary == QLatin1String( "foundry_script_missing" ) );
    }
}

TEST_CASE( "gradeViaCli against a real grader CLI when one is provided",
           "[teaching_admin][grader][real-cli]" )
{
    // Opt-in lane: set SICNU_GEO_RS_CLI to a real sicnu_geo_rs_cli binary
    // (the fake helper is not a real grader and is rejected here). Skipped
    // elsewhere — hermetic coverage lives in the [grader] cases.
    const QString cli = QString::fromUtf8( qgetenv( "SICNU_GEO_RS_CLI" ) );
    const QString repo = QString::fromUtf8( qgetenv( "SICNU_SOURCE_DIR" ) );
    if ( cli.isEmpty() || !QFileInfo::exists( cli ) || repo.isEmpty()
         || !QFileInfo::exists( QDir( repo ).filePath( QStringLiteral( "data/labs/grading" ) ) ) )
    {
        WARN( "real grader CLI / repo not provided; skipping real-CLI integration" );
        return;
    }
    // pick any committed rules file
    const QDir grading( QDir( repo ).filePath( QStringLiteral( "data/labs/grading" ) ) );
    const auto rules = grading.entryList( { QStringLiteral( "*.rules.json" ) }, QDir::Files, QDir::Name );
    if ( rules.isEmpty() )
    {
        WARN( "no committed rules files found; skipping real-CLI integration" );
        return;
    }
    // pick any committed fixture raster as a real artifact
    QString artifact;
    const QDir fixtures( QDir( repo ).filePath( QStringLiteral( "tests/fixtures/lab" ) ) );
    for ( const QFileInfo &fi :
          fixtures.entryInfoList( { QStringLiteral( "*.tif" ), QStringLiteral( "*.png" ) },
                                  QDir::Files, QDir::Name ) )
    {
        artifact = fi.absoluteFilePath();
        break;
    }
    if ( artifact.isEmpty() )
    {
        WARN( "no committed fixture raster found; skipping real-CLI integration" );
        return;
    }

    GraderCliConfig cfg;
    cfg.cliPath = cli;
    cfg.labIdOrRulesPath = grading.filePath( rules.first() );
    const auto g = gradeViaCli( cfg, artifact );
    // Structure contract only: the verdict belongs to the grader.
    REQUIRE( g.started );
    REQUIRE( ( g.exitCode == 0 || g.exitCode == 1 || g.exitCode == 3 ) );
    REQUIRE( ( g.status == QLatin1String( "pass" ) || g.status == QLatin1String( "fail" )
               || g.status == QLatin1String( "unavailable" ) ) );
    if ( g.status != QLatin1String( "unavailable" ) )
        REQUIRE( g.reportDigest.size() == 64 );
    else
        REQUIRE_FALSE( g.unavailableReason.isEmpty() );
}
