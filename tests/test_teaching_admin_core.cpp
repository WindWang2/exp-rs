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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
    // fail carries the top deduction, never a fabricated pass
    {
        const auto g = gradeViaCli( cfg, bad );
        REQUIRE( g.exitCode == 1 );
        REQUIRE( g.status == QLatin1String( "fail" ) );
        REQUIRE( g.score == Catch::Approx( 40.0 ) );
        REQUIRE( g.topDeduction == QLatin1String( "a1" ) );
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
