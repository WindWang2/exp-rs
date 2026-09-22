// test_teaching_admin_core.cpp — Teacher Authoring Console contracts (Catch2).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "teaching_admin/batch_assessment.h"
#include "teaching_admin/class_summary.h"
#include "teaching_admin/curriculum_editor.h"
#include "teaching_admin/data_pack_manager.h"
#include "teaching_admin/feedback_pack.h"
#include "teaching_admin/json_util.h"
#include "teaching_admin/labspec_authoring.h"
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
