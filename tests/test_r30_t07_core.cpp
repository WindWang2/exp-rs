// test_r30_t07_core.cpp — R30-T07 core leaf regressions (Qt Core only).
//
// Real behaviour, not source-string assertions:
//   G1 session persistence: atomic publish, bounded read, typed numeric
//      refusal, legal legacy recovery, save failure preserves the old doc.
//   G2/G3 submission identity + metric cells + bounded paging.
//   G5 CompletionLedger lab/session/course object verification.
//   G6 durable teaching drafts + guarded local feedback delivery.
#include <catch2/catch_test_macros.hpp>

#include "experiment_studio/run_matrix_metric.h"
#include "experiment_studio/studio_export.h"
#include "experiment_studio/studio_session.h"
#include "experiment_studio/studio_session_io.h"
#include "experiment_studio/studio_submission.h"
#include "experiment_studio/studio_types.h"
#include "platform/durable_sidecar.h"
#include "study/study_export.h"
#include "teaching/lab_completion_ledger.h"
#include "teaching/lab_draft_store.h"
#include "teaching/lab_feedback_projection.h"
#include "teaching/lab_session_state.h"
#include "teaching/verdict_stamp.h"
#include "teaching_admin/batch_assessment.h"
#include "teaching_admin/class_summary.h"
#include "teaching_admin/feedback_delivery.h"
#include "teaching_admin/feedback_pack.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTemporaryDir>

#include <json/json.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace sicnu::experiment_studio;

namespace
{

void writeBytes( const QString &path, const QByteArray &bytes )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
}

QByteArray readBytes( const QString &path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
}

StudioSessionState sampleSession()
{
    StudioSessionState s;
    s.ok = true;
    s.studyId = QStringLiteral( "study-r30" );
    s.experimentId = QStringLiteral( "exp-r30" );
    s.algorithmId = QStringLiteral( "rs:threshold_raster" );
    s.activeTab = QStringLiteral( "matrix" );
    s.selectedPointIds = QStringList{ QStringLiteral( "p0001" ), QStringLiteral( "p0002" ) };
    s.reportStorePath = QStringLiteral( "/tmp/store-a.db" );
    s.submittedSpecDigest = QStringLiteral( "abc123" );
    return s;
}

Json::Value parseJson( const std::string &text )
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream in( text );
    REQUIRE( Json::parseFromStream( builder, in, &root, &errors ) );
    return root;
}

Json::Value passingVerifierReport()
{
    return parseJson( R"({"schema":"sicnu.verification.report/1","overall":"pass","specId":"lab.spec"})" );
}

Json::Value passingGradeTranscript()
{
    return parseJson( R"({"verdict":"pass","score":90.0,"passing_score":60.0,"deductions":[],"evidence":[{"assertion_id":"band_count","kind":"identity","passed":true}]})" );
}

/// A REAL stamped pass session saved through the production sidecar writer.
/// @p courseId / @p labId / @p sessionId are the identity under test.
std::string stampedPassSession( const std::filesystem::path &dir,
                                const std::string &courseId,
                                const std::string &labId,
                                const std::string &sessionId,
                                const std::string &artifactTag )
{
    const auto artifact = dir / ( artifactTag + ".tif" );
    const auto sessionPath = dir / ( artifactTag + ".session.json" );
    {
        std::ofstream out( artifact, std::ios::binary );
        out << "graded bytes for " << artifactTag;
    }
    auto fb = sicnu::teaching::LabFeedbackProjection::fromReports(
        labId, passingVerifierReport(), passingGradeTranscript() );
    REQUIRE( fb.overallStatus == "pass" );
    auto s = sicnu::teaching::LabSessionState::makeNew( sessionId, courseId, labId );
    s.artifactPath = artifact.string();
    s.lastValidationSummary = fb.toJson();
    s.labStatus = sicnu::teaching::LabUiStatus::Completed;
    REQUIRE( s.saveToFile( sessionPath.string() ) );
    const auto reloaded = sicnu::teaching::LabSessionState::loadFromFile( sessionPath.string() );
    REQUIRE( reloaded.ok );
    REQUIRE_FALSE( reloaded.verdictArtifactStamp.empty() );
    return sessionPath.string();
}

std::filesystem::path tempPath( const char *tag )
{
    auto dir = std::filesystem::temp_directory_path()
               / ( std::string( "r30_t07_" ) + tag + "_" + std::to_string( ::getpid() ) );
    std::filesystem::remove_all( dir );
    std::filesystem::create_directories( dir );
    return dir;
}

RunMatrixViewModel matrixWithRows( int count )
{
    sicnu::study::StudyReport report;
    report.studyId = QStringLiteral( "study-paging" );
    for ( int i = 0; i < count; ++i )
    {
        sicnu::study::StudyRunRow row;
        row.pointId = QStringLiteral( "p%1" ).arg( i, 4, 10, QLatin1Char( '0' ) );
        row.runId = QStringLiteral( "run-%1" ).arg( i );
        row.status = QStringLiteral( "recorded" );
        row.metrics.insert( QStringLiteral( "maskedPercent" ), double( i ) );
        report.runTable.append( row );
    }
    report.recordedCount = count;
    return projectRunMatrix( report );
}

} // namespace

// ───────────────────────── G1: session persistence ─────────────────────────

TEST_CASE( "R30-T07 G1: session round-trips through the atomic authority",
           "[r30_t07][session][atomic]" )
{
    QTemporaryDir dir;
    const QString path = dir.filePath( "studio_session.json" );
    const StudioSessionState original = sampleSession();

    const auto saved = saveStudioSessionFile( path, original );
    INFO( saved.message.toStdString() );
    REQUIRE( saved.ok );
    REQUIRE( QFileInfo::exists( path ) );
    REQUIRE( QFileInfo::exists( path + QStringLiteral( ".last-good" ) ) );

    const auto loaded = loadStudioSessionFile( path );
    INFO( loaded.code.toStdString() );
    REQUIRE( loaded.ok() );
    REQUIRE( loaded.status == StudioSessionLoadStatus::Ok );
    CHECK( loaded.state.studyId == original.studyId );
    CHECK( loaded.state.experimentId == original.experimentId );
    CHECK( loaded.state.selectedPointIds == original.selectedPointIds );
    CHECK( loaded.state.reportStorePath == original.reportStorePath );
    CHECK( loaded.state.submittedSpecDigest == original.submittedSpecDigest );
    CHECK( loaded.state.policy.maxPoints == original.policy.maxPoints );
}

TEST_CASE( "R30-T07 G1: a failed save leaves the previous document byte-intact",
           "[r30_t07][session][save_failure]" )
{
    QTemporaryDir dir;
    const QString path = dir.filePath( "studio_session.json" );
    REQUIRE( saveStudioSessionFile( path, sampleSession() ).ok );
    const QByteArray before = readBytes( path );

    // Oversize refusal happens BEFORE any filesystem effect (typed TooLarge).
    StudioSessionState huge = sampleSession();
    QJsonObject gigantic;
    for ( int i = 0; i < 400; ++i )
        gigantic.insert( QStringLiteral( "k%1" ).arg( i ), QString( 4096, QLatin1Char( 'x' ) ) );
    huge.lastStudyReport = gigantic;
    const auto refused = saveStudioSessionFile( path, huge, 1024 );
    CHECK_FALSE( refused.ok );
    CHECK( refused.code == QStringLiteral( "experiment_studio.session_oversize" ) );
    CHECK( readBytes( path ) == before );

    // A target that is a DIRECTORY must also fail closed, not truncate.
    const QString dirPath = dir.filePath( "session_as_dir" );
    REQUIRE( QDir().mkdir( dirPath ) );
    const auto dirRefused = saveStudioSessionFile( dirPath, sampleSession() );
    CHECK_FALSE( dirRefused.ok );
    CHECK( dirRefused.code == QStringLiteral( "experiment_studio.session_not_a_file" ) );
    CHECK( readBytes( path ) == before );
    const auto stillReadable = loadStudioSessionFile( path );
    REQUIRE( stillReadable.ok() );
    CHECK( stillReadable.state.studyId == QStringLiteral( "study-r30" ) );

    // R2 P1-C evidence: a VERIFY-phase failure happens AFTER the atomic
    // rename, so the main target already holds the NEW bytes. This is exactly
    // why the dock must not claim "previous document left intact" for the
    // IO-layer `session_publish_verify_failed` code.
    const QByteArray newPayload = QStringLiteral( "{\"replaced\":true}" ).toUtf8();
    sicnu::platform::sidecar::WriteRequest verifyRequest;
    verifyRequest.targetPath = path.toUtf8().constData();
    verifyRequest.bytes = std::string( newPayload.constData(),
                                       static_cast<std::size_t>( newPayload.size() ) );
    verifyRequest.hooks.failAt = []( sicnu::platform::sidecar::WritePhase phase ) {
        return phase == sicnu::platform::sidecar::WritePhase::Verify;
    };
    const auto verifyFailed = sicnu::platform::sidecar::write( verifyRequest );
    CHECK_FALSE( static_cast<bool>( verifyFailed ) );
    CHECK( verifyFailed.status
           == sicnu::platform::sidecar::WriteStatus::VerifyFailed );
    CHECK( QString::fromUtf8(
               sicnu::platform::sidecar::writeStatusName( verifyFailed.status ) )
           == QStringLiteral( "verify_failed" ) );
    CHECK( readBytes( path ) == newPayload ); // published despite the failure
}

TEST_CASE( "R30-T07 G1: bounded reads refuse oversize, damaged and future docs",
           "[r30_t07][session][bounded]" )
{
    QTemporaryDir dir;
    const QString path = dir.filePath( "session.json" );

    // Oversize: a valid but huge document must not be slurped.
    writeBytes( path, QByteArray( 8192, 'x' ) );
    const auto oversize = loadStudioSessionFile( path, 1024 );
    CHECK( oversize.status == StudioSessionLoadStatus::Oversize );
    CHECK_FALSE( oversize.ok() );

    // Damaged JSON is an explicit typed refusal with a user-facing reason.
    writeBytes( path, QByteArray( "{ not json" ) );
    const auto damaged = loadStudioSessionFile( path );
    CHECK( damaged.status == StudioSessionLoadStatus::DamagedJson );
    CHECK( damaged.message.contains( QStringLiteral( "damaged" ) ) );
    CHECK( damaged.state.designerDraft.isEmpty() );

    // A FUTURE schema is named as such, and never partially adopted.
    QJsonObject future = sampleSession().toJson();
    future.insert( QStringLiteral( "schema" ),
                   QStringLiteral( "sicnu.experiment_studio.session/2" ) );
    writeBytes( path, QJsonDocument( future ).toJson() );
    const auto newer = loadStudioSessionFile( path );
    CHECK( newer.status == StudioSessionLoadStatus::ForeignSchema );
    CHECK( newer.message.contains( QStringLiteral( "newer version" ) ) );
    CHECK_FALSE( newer.ok() );
    CHECK( newer.state.studyId.isEmpty() );

    // A genuinely foreign document gets the "another version" reason.
    QJsonObject foreign = sampleSession().toJson();
    foreign.insert( QStringLiteral( "schema" ), QStringLiteral( "some.other.doc/1" ) );
    writeBytes( path, QJsonDocument( foreign ).toJson() );
    CHECK( loadStudioSessionFile( path ).message.contains(
        QStringLiteral( "another version" ) ) );
}

TEST_CASE( "R30-T07 G1: a semantically corrupted main session recovers from last-good",
           "[r30_t07][session][last_good]" )
{
    QTemporaryDir dir;
    const QString path = dir.filePath( "session.json" );
    REQUIRE( saveStudioSessionFile( path, sampleSession() ).ok );
    const QString lastGood = path + QString::fromUtf8( kStudioSessionLastGoodSuffix );
    REQUIRE( QFileInfo::exists( lastGood ) );

    // Future schema in the main doc: the last-good generation is valid.
    QJsonObject future = sampleSession().toJson();
    future.insert( QStringLiteral( "schema" ),
                   QStringLiteral( "sicnu.experiment_studio.session/2" ) );
    writeBytes( path, QJsonDocument( future ).toJson() );
    const auto recoveredSchema = loadStudioSessionFile( path );
    CHECK( recoveredSchema.status == StudioSessionLoadStatus::RecoveredLastGood );
    REQUIRE( recoveredSchema.ok() );
    CHECK( recoveredSchema.state.studyId == QStringLiteral( "study-r30" ) );
    CHECK( recoveredSchema.issues.join( QLatin1Char( ' ' ) )
               .contains( QStringLiteral( "session_foreign_schema" ) ) );

    // Invalid numeric field in the main doc: same recovery contract.
    QJsonObject badFields = sampleSession().toJson();
    QJsonObject policy = badFields.value( QStringLiteral( "policy" ) ).toObject();
    policy.insert( QStringLiteral( "max_points" ), 2.5 ); // fractional
    badFields.insert( QStringLiteral( "policy" ), policy );
    writeBytes( path, QJsonDocument( badFields ).toJson() );
    const auto recoveredFields = loadStudioSessionFile( path );
    CHECK( recoveredFields.status == StudioSessionLoadStatus::RecoveredLastGood );
    REQUIRE( recoveredFields.ok() );
    CHECK( recoveredFields.state.studyId == QStringLiteral( "study-r30" ) );
    CHECK( recoveredFields.issues.join( QLatin1Char( ' ' ) )
               .contains( QStringLiteral( "session_invalid_fields" ) ) );

    // With NO last-good generation, the explicit typed refusal must survive
    // and the exact bad field must be named in the diagnostics.
    REQUIRE( QFile::remove( lastGood ) );
    const auto unrecoverable = loadStudioSessionFile( path );
    CHECK( unrecoverable.status == StudioSessionLoadStatus::InvalidFields );
    CHECK_FALSE( unrecoverable.ok() );
    CHECK( unrecoverable.issues.join( QLatin1Char( ' ' ) )
               .contains( QStringLiteral( "max_points" ) ) );
}

TEST_CASE( "R30-T07 G1: legal legacy single-file sessions still restore",
           "[r30_t07][session][legacy]" )
{
    QJsonObject legacy;
    legacy.insert( QStringLiteral( "schema" ),
                   QString::fromUtf8( kStudioSessionSchema ) );
    legacy.insert( QStringLiteral( "study_id" ), QStringLiteral( "legacy-study" ) );
    legacy.insert( QStringLiteral( "active_tab" ), QStringLiteral( "designer" ) );
    QJsonObject draft;
    draft.insert( QStringLiteral( "draft_valid" ), true );
    legacy.insert( QStringLiteral( "designer_draft" ), draft );
    // No policy, no store refs, no submitted identity — all optional.
    const StudioSessionState restored = StudioSessionState::fromJson( legacy );
    REQUIRE( restored.ok );
    CHECK( restored.studyId == QStringLiteral( "legacy-study" ) );
    CHECK( restored.reportStorePath.isEmpty() );
    CHECK( restored.submittedDesignerVm.isEmpty() );
    CHECK( restored.policy.maxPoints == 1000 );
    CHECK( restored.designerDraft.value( QStringLiteral( "draft_valid" ) ).toBool() );

    // A torn MAIN document is recovered from the last-good generation.
    QTemporaryDir dir;
    const QString path = dir.filePath( "session.json" );
    REQUIRE( saveStudioSessionFile( path, sampleSession() ).ok );
    writeBytes( path, QByteArray( "{ torn" ) );
    const auto recovered = loadStudioSessionFile( path );
    INFO( recovered.code.toStdString() );
    REQUIRE( recovered.ok() );
    CHECK( recovered.status == StudioSessionLoadStatus::RecoveredLastGood );
    CHECK( recovered.state.studyId == QStringLiteral( "study-r30" ) );
}

TEST_CASE( "R30-T07 G1: policy extremes/fractional values are refused, not cast",
           "[r30_t07][session][typed_numbers]" )
{
    const auto withPolicy = []( const QJsonValue &maxPoints ) {
        QJsonObject doc = sampleSession().toJson();
        QJsonObject pol;
        pol.insert( QStringLiteral( "max_points" ), maxPoints );
        doc.insert( QStringLiteral( "policy" ), pol );
        return StudioSessionState::fromJson( doc );
    };
    CHECK_FALSE( withPolicy( QJsonValue( 1e300 ) ).ok );          // overflow
    CHECK_FALSE( withPolicy( QJsonValue( 2.5 ) ).ok );            // fractional
    CHECK_FALSE( withPolicy( QJsonValue( -1.0 ) ).ok );           // negative
    CHECK_FALSE( withPolicy( QJsonValue( QStringLiteral( "1000" ) ) ).ok ); // wrong type
    CHECK( withPolicy( QJsonValue( 1000 ) ).ok );                 // legal
    // A null optional policy value in an old document means "use the
    // declared default", not "refuse the whole session".
    CHECK( withPolicy( QJsonValue::Null ).ok );

    QJsonObject badString = sampleSession().toJson();
    badString.insert( QStringLiteral( "study_id" ), QJsonValue( 42 ) );
    CHECK_FALSE( StudioSessionState::fromJson( badString ).ok );

    // Typed conversions used by GUI spinbox restore.
    QString why;
    int out = 0;
    CHECK( studioSessionIntValue( QJsonValue( 500 ), 1, 1000, &out, &why ) );
    CHECK( out == 500 );
    CHECK_FALSE( studioSessionIntValue( QJsonValue( 500.5 ), 1, 1000, &out, &why ) );
    CHECK( why.contains( QStringLiteral( "fractional" ) ) );
    CHECK_FALSE( studioSessionIntValue( QJsonValue( 1e300 ), 1, 1000, &out, &why ) );
    qint64 wide = 0;
    CHECK_FALSE( studioSessionInt64Value( QJsonValue( 1e300 ), 1, 1000000, &wide, &why ) );
}

// ─────────────────── G3: metric identity + bounded paging ───────────────────

TEST_CASE( "R30-T07 G3: a missing metric never becomes another metric or zero",
           "[r30_t07][metric][identity]" )
{
    QJsonObject row;
    row.insert( QStringLiteral( "otherMetric" ), 42.0 );
    row.insert( QStringLiteral( "nullMetric" ), QJsonValue::Null );
    row.insert( QStringLiteral( "objectMetric" ), QJsonObject{ { QStringLiteral( "a" ), 1.0 } } );
    row.insert( QStringLiteral( "arrayMetric" ), QJsonArray{ 1.0, 2.0 } );
    row.insert( QStringLiteral( "stringMetric" ), QStringLiteral( "n/a" ) );
    row.insert( QStringLiteral( "boolMetric" ), true );
    row.insert( QStringLiteral( "zeroMetric" ), 0.0 );
    row.insert( QStringLiteral( "integerMetric" ), 42 );

    // R1 P0-1 regression: a real INTEGER metric is a value, not undefined.
    const QJsonObject textRow = QJsonDocument::fromJson(
                                    QJsonDocument( row ).toJson( QJsonDocument::Compact ) )
                                    .object();
    for ( const QJsonObject &source : { row, textRow } )
    {
        const auto integerCell = projectRunMetricCell(
            source, QStringLiteral( "integerMetric" ), QStringLiteral( "run-7" ) );
        CHECK( integerCell.kind == RunMetricCellKind::Value );
        CHECK( integerCell.value == 42.0 );
        CHECK( integerCell.display.contains( QStringLiteral( "42" ) ) );
    }

    const auto missing = projectRunMetricCell( row, QStringLiteral( "maskedPercent" ),
                                               QStringLiteral( "run-7" ) );
    CHECK( missing.kind == RunMetricCellKind::MissingKey );
    CHECK_FALSE( missing.hasValue() );
    CHECK_FALSE( missing.display.contains( QStringLiteral( "42" ) ) );
    CHECK( missing.reason.contains( QStringLiteral( "metric_missing" ) ) );
    CHECK( missing.toJson().value( QStringLiteral( "run_id" ) ).toString()
           == QStringLiteral( "run-7" ) );

    CHECK( projectRunMetricCell( row, QStringLiteral( "nullMetric" ) ).kind
           == RunMetricCellKind::Null );
    CHECK( projectRunMetricCell( row, QStringLiteral( "objectMetric" ) ).kind
           == RunMetricCellKind::Object );
    CHECK( projectRunMetricCell( row, QStringLiteral( "arrayMetric" ) ).kind
           == RunMetricCellKind::Array );
    CHECK( projectRunMetricCell( row, QStringLiteral( "stringMetric" ) ).kind
           == RunMetricCellKind::String );
    CHECK( projectRunMetricCell( row, QStringLiteral( "boolMetric" ) ).kind
           == RunMetricCellKind::Bool );
    const auto zero = projectRunMetricCell( row, QStringLiteral( "zeroMetric" ) );
    CHECK( zero.kind == RunMetricCellKind::Value );
    CHECK( zero.value == 0.0 );
    CHECK( zero.display.contains( QStringLiteral( "0" ) ) );
    // A real zero is the ONLY non-value-shaped case with a numeric value.
    CHECK( projectRunMetricCell( row, QStringLiteral( "maskedPercent" ) ).value == 0.0 );
}

TEST_CASE( "R30-T07 G3: paging is bounded and keeps off-page selection memory",
           "[r30_t07][matrix][paging]" )
{
    const auto vm = matrixWithRows( 800 );
    const auto page0 = projectRunMatrixPage( vm, 0, 500 );
    CHECK( page0.totalRows == 800 );
    CHECK( page0.pageCount == 2 );
    CHECK( page0.page == 0 );
    CHECK( page0.firstRow == 0 );
    CHECK( page0.rows.size() == 500 );
    CHECK( page0.rows.first().pointId == QStringLiteral( "p0000" ) );

    const QStringList selection{ QStringLiteral( "p0799#0" ) };
    const auto page1 = projectRunMatrixPage( vm, 1, 500, selection );
    CHECK( page1.page == 1 );
    CHECK( page1.firstRow == 500 );
    CHECK( page1.rows.size() == 300 );
    CHECK( page1.rows.last().pointId == QStringLiteral( "p0799" ) );
    CHECK( page1.selectedKeys == selection ); // not trimmed to visible rows
    CHECK( page1.toJson().value( QStringLiteral( "total_rows" ) ).toInt() == 800 );

    // A hostile page index clamps instead of wrapping silently.
    CHECK( projectRunMatrixPage( vm, 999, 500 ).page == 1 );
    CHECK( projectRunMatrixPage( vm, -4, 500 ).page == 0 );
    CHECK( projectRunMatrixPage( vm, 0, 0 ).pageSize == 1 );
    const auto emptyPage = projectRunMatrixPage( RunMatrixViewModel{}, 0, 500 );
    CHECK( emptyPage.rows.isEmpty() );
    CHECK( emptyPage.issues.contains( QStringLiteral( "experiment_studio.matrix_no_rows" ) ) );
}

// ─────────────────────── G2: submitted design identity ───────────────────────

TEST_CASE( "R30-T07 G2: spec digest, report binding and stale-callback refusal",
           "[r30_t07][submission][identity]" )
{
    QJsonObject specA;
    specA.insert( QStringLiteral( "algorithm_id" ), QStringLiteral( "rs:threshold_raster" ) );
    specA.insert( QStringLiteral( "max_runs" ), 20 );
    QJsonObject reordered;
    reordered.insert( QStringLiteral( "max_runs" ), 20 );
    reordered.insert( QStringLiteral( "algorithm_id" ), QStringLiteral( "rs:threshold_raster" ) );
    CHECK( studioSpecDigest( specA ) == studioSpecDigest( reordered ) );
    CHECK( studioSpecDigest( specA ).size() == 64 );
    // R1 P0-1 regression: INTEGER fields must change the digest. On Qt6 all
    // JSON numbers are one numeric family; a typed-reader regression that
    // skipped them would silently collapse 20 and 21 into one identity.
    QJsonObject specB = specA;
    specB.insert( QStringLiteral( "max_runs" ), 21 );
    CHECK( studioSpecDigest( specA ) != studioSpecDigest( specB ) );
    QJsonObject seedB = specA;
    seedB.insert( QStringLiteral( "seed" ), 7 );
    CHECK( studioSpecDigest( specA ) != studioSpecDigest( seedB ) );
    // A spec that went through JSON TEXT must digest identically to the
    // in-memory object (the report echo is parsed from text).
    const QJsonObject parsed = QJsonDocument::fromJson(
                                  QJsonDocument( specA ).toJson( QJsonDocument::Compact ) )
                                  .object();
    CHECK( studioSpecDigest( parsed ) == studioSpecDigest( specA ) );

    StudioSubmission submission;
    submission.submissionId = 7;
    submission.storePath = QStringLiteral( "/tmp/store-a.db" );
    submission.studyId = QStringLiteral( "study-A" );
    submission.experimentId = QStringLiteral( "exp-A" );
    submission.algorithmId = QStringLiteral( "rs:threshold_raster" );
    submission.specJson = specA;
    submission.specDigest = studioSpecDigest( specA );
    submission.designerVm = QJsonObject{ { QStringLiteral( "draft_valid" ), true } };

    const auto roundTrip = StudioSubmission::fromJson( submission.toJson() );
    REQUIRE( roundTrip );
    CHECK( roundTrip.value().submissionId == 7 );
    CHECK( roundTrip.value().specDigest == submission.specDigest );
    // The persisted submission (run → close → reopen → export) travels
    // through JSON TEXT; its integer id must survive that round trip and
    // still bind to the report.
    const QJsonObject textRoundTrip = QJsonDocument::fromJson(
                                         QJsonDocument( submission.toJson() )
                                             .toJson( QJsonDocument::Compact ) )
                                         .object();
    const auto rehydrated = StudioSubmission::fromJson( textRoundTrip );
    REQUIRE( rehydrated );
    CHECK( rehydrated.value().submissionId == 7 );
    CHECK( rehydrated.value().specDigest == submission.specDigest );
    QString restoreWhy;
    QJsonObject matchingReport;
    matchingReport.insert( QStringLiteral( "spec" ), rehydrated.value().specJson );
    CHECK( studioSubmissionMatchesReport( rehydrated.value(), matchingReport, &restoreWhy ) );

    QJsonObject report;
    report.insert( QStringLiteral( "spec" ), specA );
    report.insert( QStringLiteral( "study_id" ), QStringLiteral( "study-A" ) );
    report.insert( QStringLiteral( "experiment_id" ), QStringLiteral( "exp-A" ) );
    QString why;
    CHECK( studioSubmissionMatchesReport( submission, report, &why ) );

    QJsonObject otherSpec = specA;
    otherSpec.insert( QStringLiteral( "max_runs" ), 21 );
    report.insert( QStringLiteral( "spec" ), otherSpec );
    CHECK_FALSE( studioSubmissionMatchesReport( submission, report, &why ) );
    CHECK( why == QStringLiteral( "experiment_studio.submission_spec_mismatch" ) );

    std::optional<StudioSubmission> active = submission;
    CHECK_FALSE( studioSubmissionIsStale( 7, active, submission.storePath ) );
    CHECK( studioSubmissionIsStale( 6, active, submission.storePath ) ); // expired
    CHECK( studioSubmissionIsStale( 7, active, QStringLiteral( "/tmp/store-b.db" ) ) );
    CHECK( studioSubmissionIsStale( 7, std::nullopt, submission.storePath ) );
}

TEST_CASE( "R30-T07 G2: export bundle carries submission identity and stays legacy-readable",
           "[r30_t07][export][identity]" )
{
    QTemporaryDir dir;
    const QString path = dir.filePath( "bundle.json" );
    StudioExportBundle bundle;
    bundle.studyId = QStringLiteral( "study-A" );
    bundle.studyReport = QJsonObject{ { QStringLiteral( "document_type" ),
                                       QStringLiteral( "sicnu.studyreport.v1" ) } };
    bundle.submission = QJsonObject{ { QStringLiteral( "submission_id" ), 7 } };
    bundle.metricCells = QJsonObject{ { QStringLiteral( "metric" ),
                                       QStringLiteral( "maskedPercent" ) } };
    bundle.issues.append( QStringLiteral( "none" ) );
    REQUIRE( writeStudioExportJson( bundle, path ) );
    const auto reloaded = readStudioExportJson( path );
    REQUIRE( reloaded );
    CHECK( reloaded.value().submission.value( QStringLiteral( "submission_id" ) ).toInt() == 7 );
    CHECK( reloaded.value().metricCells.value( QStringLiteral( "metric" ) ).toString()
           == QStringLiteral( "maskedPercent" ) );

    // A pre-R30 bundle has neither field; the reader must not refuse it.
    QJsonObject legacy = bundle.toJson();
    legacy.remove( QStringLiteral( "submission" ) );
    legacy.remove( QStringLiteral( "metric_cells" ) );
    const auto legacyResult = StudioExportBundle::fromJson( legacy );
    REQUIRE( legacyResult );
    CHECK( legacyResult.value().submission.isEmpty() );
    CHECK( legacyResult.value().metricCells.isEmpty() );
    CHECK( legacyResult.value().studyId == QStringLiteral( "study-A" ) );
}

// ───────────────────────── G5: CompletionLedger ─────────────────────────

TEST_CASE( "R30-T07 G5: a valid A session spliced into a B entry is refused",
           "[r30_t07][ledger][object]" )
{
    const auto dir = tempPath( "ledger_wrong_object" );
    const std::string sessionA = stampedPassSession( dir, "course-1", "lab_A", "sess-A", "artA" );
    const auto session = sicnu::teaching::LabSessionState::loadFromFile( sessionA );
    REQUIRE( session.ok );
    const auto stamp = sicnu::teaching::parseVerdictStamp( session.verdictArtifactStamp );
    REQUIRE( stamp.parsed );

    Json::Value doc( Json::objectValue );
    doc["schema"] = sicnu::teaching::kLabCompletionLedgerSchema;
    Json::Value entry( Json::objectValue );
    entry["session_ref"] = sessionA;
    entry["verdict_digest"] = stamp.summaryDigest;
    entry["artifact_stamp"] = session.verdictArtifactStamp;
    entry["course_id"] = "course-1";
    doc["completed"]["lab_B"] = entry; // claims B, evidence is A
    const auto ledgerPath = dir / "ledger.json";
    {
        Json::StreamWriterBuilder writer;
        std::ofstream out( ledgerPath );
        out << Json::writeString( writer, doc );
    }
    const auto loaded = sicnu::teaching::LabCompletionLedger::loadVerified(
        ledgerPath.string() );
    CHECK( loaded.ok );
    CHECK( loaded.completed.empty() );
    bool sawMismatch = false;
    for ( const auto &issue : loaded.issuesZh )
        if ( issue.find( "lab" ) != std::string::npos )
            sawMismatch = true;
    CHECK( sawMismatch );
    CHECK( loaded.toProgressDoc()["completed"].empty() );
}

TEST_CASE( "R30-T07 G5: two courses sharing a lab id never cross progress",
           "[r30_t07][ledger][course]" )
{
    const auto dir = tempPath( "ledger_two_courses" );
    const std::string sessionC1 = stampedPassSession( dir, "course-1", "shared_lab", "s1", "c1art" );
    const std::string sessionC2 = stampedPassSession( dir, "course-2", "shared_lab", "s2", "c2art" );

    sicnu::teaching::LabCompletionLedger ledger;
    ledger.ok = true;
    REQUIRE( ledger.recordVerdict(
        sicnu::teaching::LabSessionState::loadFromFile( sessionC1 ), sessionC1 ) );
    REQUIRE( ledger.recordVerdict(
        sicnu::teaching::LabSessionState::loadFromFile( sessionC2 ), sessionC2 ) );
    REQUIRE( ledger.completed.size() == 2 );
    const auto ledgerPath = dir / "ledger.json";
    REQUIRE( ledger.saveToFile( ledgerPath.string() ) );

    const auto all = sicnu::teaching::LabCompletionLedger::loadVerified( ledgerPath.string() );
    REQUIRE( all.ok );
    CHECK( all.completed.size() == 2 );

    const auto c1 = sicnu::teaching::LabCompletionLedger::loadVerified(
        ledgerPath.string(), "course-1" );
    REQUIRE( c1.completed.size() == 1 );
    CHECK( c1.completed[0].courseId == "course-1" );
    CHECK( c1.toProgressDoc( "course-1" )["completed"].isMember( "shared_lab" ) );

    const auto c2 = sicnu::teaching::LabCompletionLedger::loadVerified(
        ledgerPath.string(), "course-2" );
    REQUIRE( c2.completed.size() == 1 );
    CHECK( c2.completed[0].courseId == "course-2" );
    CHECK_FALSE( c2.toProgressDoc( "course-1" )["completed"].isMember( "shared_lab" ) );
}

TEST_CASE( "R30-T07 G5: a legacy entry cannot be stolen by another course",
           "[r30_t07][ledger][legacy_course]" )
{
    // R1 P1-7: a legacy entry has no course_id. A course-2 session for the
    // same lab must NOT rewrite course-1's evidence chain; it creates its own
    // namespaced entry instead. The namespaced key must also round-trip for
    // an entry whose course identity is still empty.
    const auto dir = tempPath( "ledger_legacy_course" );
    const std::string sessionC1 =
        stampedPassSession( dir, "course-1", "shared_lab", "legacy-s1", "legacy-art1" );
    const std::string sessionC2 =
        stampedPassSession( dir, "course-2", "shared_lab", "legacy-s2", "legacy-art2" );

    sicnu::teaching::LabCompletionLedger ledger;
    ledger.ok = true;
    const auto c1 = sicnu::teaching::LabSessionState::loadFromFile( sessionC1 );
    REQUIRE( c1.ok );
    // Strip the course from the ENTRY (not the session) to emulate pre-R30.
    REQUIRE( ledger.recordVerdict( c1, sessionC1 ) );
    REQUIRE( ledger.completed.size() == 1 );
    ledger.completed[0].courseId.clear();

    const auto c2 = sicnu::teaching::LabSessionState::loadFromFile( sessionC2 );
    REQUIRE( c2.ok );
    REQUIRE( ledger.recordVerdict( c2, sessionC2 ) );
    REQUIRE( ledger.completed.size() == 2 );
    CHECK( ledger.completed[0].sessionRef == sessionC1 ); // course-1 evidence intact
    CHECK( ledger.completed[1].courseId == "course-2" );

    const auto ledgerPath = dir / "ledger.json";
    REQUIRE( ledger.saveToFile( ledgerPath.string() ) );
    const auto reloaded =
        sicnu::teaching::LabCompletionLedger::loadVerified( ledgerPath.string() );
    REQUIRE( reloaded.ok ); // empty-course namespaced key must parse
    CHECK( reloaded.completed.size() == 2 );
}

TEST_CASE( "R30-T07 G5: legacy entries migrate to their verified session course",
           "[r30_t07][ledger][migration]" )
{
    const auto dir = tempPath( "ledger_legacy" );
    const std::string sessionRef = stampedPassSession( dir, "course-legacy", "lab_old", "s-old", "oldart" );
    const auto session = sicnu::teaching::LabSessionState::loadFromFile( sessionRef );
    const auto stamp = sicnu::teaching::parseVerdictStamp( session.verdictArtifactStamp );

    Json::Value doc( Json::objectValue );
    doc["schema"] = sicnu::teaching::kLabCompletionLedgerSchema;
    Json::Value entry( Json::objectValue );
    entry["session_ref"] = sessionRef;
    entry["verdict_digest"] = stamp.summaryDigest;
    entry["artifact_stamp"] = session.verdictArtifactStamp;
    doc["completed"]["lab_old"] = entry; // NO course_id (pre-R30)
    const auto ledgerPath = dir / "ledger.json";
    {
        Json::StreamWriterBuilder writer;
        std::ofstream out( ledgerPath );
        out << Json::writeString( writer, doc );
    }
    // Upgrade must NOT silently clear real completion evidence.
    const auto migrated = sicnu::teaching::LabCompletionLedger::loadVerified(
        ledgerPath.string(), "course-legacy" );
    REQUIRE( migrated.ok );
    REQUIRE( migrated.completed.size() == 1 );
    CHECK( migrated.completed[0].courseId == "course-legacy" );
    CHECK( migrated.toProgressDoc( "course-legacy" )["completed"].isMember( "lab_old" ) );

    const auto otherCourse = sicnu::teaching::LabCompletionLedger::loadVerified(
        ledgerPath.string(), "course-X" );
    CHECK( otherCourse.completed.empty() );

    // Artifact drift still demotes (R29 regression preserved).
    {
        std::ofstream out( dir / "oldart.tif", std::ios::binary | std::ios::trunc );
        out << "drifted bytes";
    }
    const auto drifted = sicnu::teaching::LabCompletionLedger::loadVerified(
        ledgerPath.string(), "course-legacy" );
    CHECK( drifted.completed.empty() );
    CHECK_FALSE( drifted.issuesZh.empty() );
}

// ───────────────────────── G6: drafts + feedback ─────────────────────────

TEST_CASE( "R30-T07 G6: local drafts survive a restart and stay lab/course scoped",
           "[r30_t07][drafts][persistence]" )
{
    const auto dir = tempPath( "drafts" );
    const std::string path = ( dir / "lab_drafts.json" ).string();
    std::vector<sicnu::teaching::LabDraftRecord> records;
    sicnu::teaching::LabDraftRecord a;
    a.courseId = "course-1"; a.labId = "lab-A"; a.stepId = "step/one";
    a.text = "my unsubmitted answer"; a.rulesVersion = "rules-v1";
    a.artifactPath = "/tmp/a.tif";
    records.push_back( a );
    sicnu::teaching::LabDraftRecord b = a;
    b.courseId = "course-2"; b.labId = "lab-B"; b.stepId = "step-two";
    b.text = "other course";
    records.push_back( b );
    std::string error;
    REQUIRE( sicnu::teaching::saveLabDrafts( path, records, &error ) );

    const auto loaded = sicnu::teaching::loadLabDrafts( path );
    REQUIRE( loaded.ok );
    REQUIRE( loaded.drafts.size() == 2 );
    CHECK( loaded.drafts[0].text == "my unsubmitted answer" );
    CHECK( loaded.drafts[0].submitted == false ); // a draft is NOT a pass
    CHECK( sicnu::teaching::labDraftKey( "course-1", "lab-A", "step/one" )
           != sicnu::teaching::labDraftKey( "course-1", "lab-A", "step-one" ) );

    // Rule/artifact change marks a restored draft stale with a reason.
    const auto stale = sicnu::teaching::checkLabDraftValidity(
        loaded.drafts[0], "rules-v2", "/tmp/a.tif" );
    CHECK_FALSE( stale.valid );
    CHECK( stale.reasons.front() == "lab_draft_store.rules_changed" );
    CHECK( sicnu::teaching::checkLabDraftValidity(
               loaded.drafts[0], "rules-v1", "/tmp/a.tif" ).valid );

    // Legacy length-only refs are readable but NEVER claim to restore text.
    const auto legacy = sicnu::teaching::parseLegacyHumanRef( "human:step/one:21" );
    REQUIRE( legacy.parsed );
    CHECK( legacy.stepId == "step/one" );
    CHECK( legacy.length == 21 );
    CHECK_FALSE( legacy.textRecoverable );
    CHECK_FALSE( sicnu::teaching::parseLegacyHumanRef( "human:step:abc" ).parsed );

    // Corrupt and oversize documents fail closed.
    {
        std::ofstream out( path, std::ios::binary | std::ios::trunc );
        out << "{ broken";
    }
    CHECK_FALSE( sicnu::teaching::loadLabDrafts( path ).ok );
    {
        std::ofstream out( path, std::ios::binary | std::ios::trunc );
        out << std::string( 4096, 'x' );
    }
    CHECK_FALSE( sicnu::teaching::loadLabDrafts( path, 128 ).ok );
    CHECK_FALSE( sicnu::teaching::saveLabDrafts( path, records, &error, 128 ) );
}

TEST_CASE( "R30-T07 G6: guarded feedback packs are delivered as real local files",
           "[r30_t07][feedback][delivery]" )
{
    const auto dir = tempPath( "feedback" );
    const QString destination = QString::fromStdString( ( dir / "out" ).string() );

    // A REAL published batch report, produced by the production publisher.
    sicnu::teaching_admin::BatchAssessmentReport report;
    report.labId = QStringLiteral( "lab_feedback" );
    report.total = 2;
    report.graded = 2;
    sicnu::teaching_admin::BatchRowResult rowA;
    rowA.studentId = QStringLiteral( "2023110101" );
    rowA.labId = report.labId;
    rowA.status = QStringLiteral( "pass" );
    rowA.score = 88.0;
    rowA.verdict = QStringLiteral( "pass" );
    rowA.message = QStringLiteral( "graded against rubric v3" );
    rowA.artifactPath = QString::fromStdString( ( dir / "a.tif" ).string() );
    rowA.rubricVersion = QStringLiteral( "rubric-v3" );
    rowA.graderDigest = QStringLiteral( "grader-digest-a" );
    sicnu::teaching_admin::BatchRowResult rowB = rowA;
    rowB.studentId = QStringLiteral( "2023110102" );
    rowB.status = QStringLiteral( "unavailable" );
    rowB.score = -1.0;
    rowB.missingEvidence = true;
    rowB.message = QStringLiteral( "artifact missing" );
    rowB.topDeduction.clear();
    rowB.graderDigest.clear();
    report.rows = { rowA, rowB };
    const QString reportPrefix = QString::fromStdString( ( dir / "batch" ).string() );
    REQUIRE( sicnu::teaching_admin::publishBatchOutputsAtomic( report, reportPrefix ) );
    const QString reportPath = reportPrefix + QStringLiteral( ".json" );
    REQUIRE( QFileInfo::exists( reportPath ) );

    sicnu::teaching_admin::BatchAssessmentReport loadedReport;
    QString loadError;
    REQUIRE( sicnu::teaching_admin::loadBatchReportFromFile(
        reportPath, loadedReport, loadError ) );
    REQUIRE( loadedReport.rows.size() == 2 );

    // Build the guarded packs EXACTLY as the console does: real rows, real
    // cohort roster, real guard. No invented score/finding.
    QVector<sicnu::teaching_admin::FeedbackPackInput> inputs;
    QStringList cohort;
    for ( const auto &row : loadedReport.rows )
    {
        sicnu::teaching_admin::FeedbackPackInput in;
        in.row = row;
        inputs.append( in );
        cohort.append( row.studentId );
    }
    const auto emission = sicnu::teaching_admin::buildGuardedFeedbackPacks( inputs, cohort );
    REQUIRE( emission.packs.size() == 2 );

    sicnu::teaching_admin::FeedbackDeliveryRequest request;
    request.destinationDir = destination;
    request.courseId = QStringLiteral( "undergraduate_rs" );
    request.labId = report.labId;
    request.batchReportPath = reportPath;
    request.overwriteExisting = false;
    const auto delivered = sicnu::teaching_admin::deliverGuardedFeedback(
        emission.packs, sicnu::teaching_admin::buildClassSummary( loadedReport ),
        emission.blockedStudents, emission.refusals, request );
    INFO( delivered.message.toStdString() );
    REQUIRE( delivered.ok );
    CHECK_FALSE( delivered.partial );
    REQUIRE( delivered.writtenPacks.size() == 2 );
    CHECK( delivered.batchReportDigest.size() == 64 );
    REQUIRE( QFileInfo::exists( delivered.manifestPath ) );
    REQUIRE( QFileInfo::exists( delivered.summaryPath ) );

    // Each written file carries the REAL grading result, not a placeholder.
    bool sawRealScore = false;
    bool sawNoFabricatedFinding = false;
    for ( const QString &packPath : delivered.writtenPacks )
    {
        const QByteArray bytes = readBytes( packPath );
        const QJsonObject pack = QJsonDocument::fromJson( bytes ).object();
        CHECK( pack.value( QStringLiteral( "schema" ) ).toString()
               == QString::fromLatin1( sicnu::teaching_admin::kFeedbackPackSchema ) );
        if ( pack.value( QStringLiteral( "overall" ) )
                 .toObject().value( QStringLiteral( "score" ) ).toDouble() == 88.0 )
            sawRealScore = true;
        if ( pack.value( QStringLiteral( "verifier_findings" ) ).toArray().isEmpty() )
            sawNoFabricatedFinding = true;
    }
    CHECK( sawRealScore );
    CHECK( sawNoFabricatedFinding );

    const QJsonObject manifest =
        QJsonDocument::fromJson( readBytes( delivered.manifestPath ) ).object();
    CHECK( manifest.value( QStringLiteral( "no_upload" ) ).toBool() );
    CHECK( manifest.value( QStringLiteral( "batch_report_digest" ) ).toString().size() == 64 );
    CHECK( manifest.value( QStringLiteral( "written_packs" ) ).toArray().size() == 2 );
    // R2 P1-B: the ON-DISK truthful manifest must carry the SAME final
    // status as the returned FeedbackDeliveryResult, not write-time defaults.
    CHECK( manifest.value( QStringLiteral( "ok" ) ).toBool() );
    CHECK_FALSE( manifest.value( QStringLiteral( "partial" ) ).toBool() );
    CHECK( manifest.value( QStringLiteral( "code" ) ).toString()
           == delivered.code );
    CHECK( manifest.value( QStringLiteral( "message" ) ).toString()
           == delivered.message );
    CHECK( manifest.value( QStringLiteral( "guarded_in" ) ).toInt()
           == delivered.guardedIn );

    // Re-delivery with the default conflict policy must NOT overwrite.
    const auto second = sicnu::teaching_admin::deliverGuardedFeedback(
        emission.packs, sicnu::teaching_admin::buildClassSummary( loadedReport ),
        emission.blockedStudents, emission.refusals, request );
    // Conflicts now cover the two packs AND the two delivery-owned artifacts
    // (class summary + manifest) that must not be erased by a partial re-run.
    CHECK( second.conflicts.size() == 4 );
    CHECK( second.partial );
    CHECK( second.writtenPacks.isEmpty() );
    CHECK_FALSE( second.ok ); // a zero-write re-run is NEVER a success
    CHECK( second.manifestPath.isEmpty() );

    // A named-but-unreadable real batch report breaks the evidence chain:
    // refuse before writing anything instead of shipping an empty digest.
    sicnu::teaching_admin::FeedbackDeliveryRequest badSource = request;
    badSource.batchReportPath =
        QString::fromStdString( ( dir / "no-such-report.json" ).string() );
    badSource.destinationDir =
        QString::fromStdString( ( dir / "bad-source" ).string() );
    const auto badSourceResult = sicnu::teaching_admin::deliverGuardedFeedback(
        emission.packs, sicnu::teaching_admin::buildClassSummary( loadedReport ),
        emission.blockedStudents, emission.refusals, badSource );
    CHECK_FALSE( badSourceResult.ok );
    CHECK( badSourceResult.code == QStringLiteral( "feedback.delivery_bad_source" ) );
    CHECK( badSourceResult.writtenPacks.isEmpty() );
    CHECK_FALSE( QFileInfo::exists( badSource.destinationDir ) );

    // No destination = no write, ever.
    sicnu::teaching_admin::FeedbackDeliveryRequest noDest = request;
    noDest.destinationDir.clear();
    const auto refused = sicnu::teaching_admin::deliverGuardedFeedback(
        emission.packs, {}, {}, {}, noDest );
    CHECK_FALSE( refused.ok );
    CHECK( refused.code == QStringLiteral( "feedback.delivery_no_destination" ) );
    CHECK( refused.manifestPath.isEmpty() );
}
