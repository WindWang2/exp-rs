// tests/test_r32_t04_core.cpp — R32-T04 regression suite (execution backend).
//
// Covers: WP01 (deadline ordering), WP02 (retry identity in-transaction),
// WP03 (sidecar naming authority), WP06 (workflow schema typing),
// WP07 (canonical evidence round-trip), WP11 (cache content identity),
// WP12 (readiness typing + percent clamp), WP13 (bounded stamp + stream
// digest), WP14 (feedback reason row), WP16 (metric admission +
// equivalent-runs fail-closed), WP17 (cache key injectivity),
// WP18 (bundle recovery admission).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <json/json.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "workflow/workflow_definition.h"

#include "processing/framework/task_center.h"
#include "workflow/artifact_gc.h"

#include "exprs/workflow_schema.h"
#include "workflow/workflow_definition.h"

#include "verify/verify_types.h"

#include "data/execution_fingerprint.h"

#include "teaching/lab_readiness.h"
#include "teaching/lab_feedback_projection.h"
#include "teaching/course_home_view_model.h"
#include "teaching/verdict_stamp.h"
#include "lab/sha256.h"

#include "experiment/benchmark_runner.h"
#include "experiment/replay_readiness.h"
#include "experiment/experiment_store.h"

#include "teaching_admin/batch_assessment.h"

#include "science_context/context_cache.h"
#include "science_context/bundle.h"

using namespace exprs;
using sicnu::jobs::JobEngine;
using sicnu::jobs::JobRequest;
using sicnu::jobs::JobState;

namespace
{
bool pollUntil( const std::function<bool()> &predicate, int timeoutMs )
{
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds( timeoutMs );
    while ( std::chrono::steady_clock::now() < deadline )
    {
        if ( predicate() )
            return true;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    return predicate();
}

sicnu::operators::RSOperatorError cancelledError( const std::string &msg )
{
    return sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::Cancelled,
                                              msg );
}

Json::Value parseJson( const std::string &json )
{
    Json::Value root;
    Json::Reader reader;
    reader.parse( json, root, false );
    return root;
}
} // namespace

// ===========================================================================
// WP01 — the watchdog must fire the EARLIEST deadline, not the
// lexicographically-first jobId's deadline (#2122).
// ===========================================================================
TEST_CASE( "WP01 short-budget job is not starved behind a long-budget job with a smaller jobId",
           "[r32t04][wp01]" )
{
    JobEngine &eng = JobEngine::instance();
    eng.shutdownForTests();

    // jobId order and deadline order are opposite: the first job gets a LONG
    // budget, the second job a SHORT one.
    std::atomic<bool> longUnwound{ false };
    std::atomic<bool> shortUnwound{ false };

    JobRequest longReq;
    longReq.algorithmId = "r32t04:long";
    longReq.title = "long";
    longReq.timeoutMs = 20'000; // would starve job-b for 20 s under the bug

    JobRequest shortReq;
    shortReq.algorithmId = "r32t04:short";
    shortReq.title = "short";
    shortReq.timeoutMs = 250;

    const auto cooperative = []( std::atomic<bool> &unwound ) {
        return [ &unwound ]( const JobRequest &,
                             sicnu::operators::RSOperatorContext &ctx ) -> Json::Value {
            while ( !ctx.cancelFlag() || !ctx.cancelFlag()->load() )
                std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
            unwound.store( true );
            throw cancelledError( "budget elapsed" );
        };
    };

    const std::string longId = eng.submit( longReq, cooperative( longUnwound ) );
    const std::string shortId = eng.submit( shortReq, cooperative( shortUnwound ) );
    REQUIRE( !longId.empty() );
    REQUIRE( !shortId.empty() );

    // The SHORT budget must fire well within its 250 ms + slack — under the
    // bug the watchdog parked on the long job's 20 s deadline instead.
    REQUIRE( pollUntil( [&]
                        {
                            const auto snap = eng.snapshot( shortId );
                            return snap && snap->state == JobState::Cancelled;
                        },
                        3'000 ) );
    REQUIRE( shortUnwound.load() );
    // The long job was never cancelled by the watchdog.
    REQUIRE( eng.snapshot( longId )->state == JobState::Running );
    REQUIRE_FALSE( longUnwound.load() );

    // #2122 cleanup: a manual cancel drops the armed deadline — fire cancel
    // and confirm the long job unwinds via the cancel path.
    REQUIRE( eng.cancel( longId ) );
    REQUIRE( pollUntil( [&]
                        {
                            const auto snap = eng.snapshot( longId );
                            return snap && snap->state == JobState::Cancelled;
                        },
                        5'000 ) );
    eng.shutdownForTests();
}

// ===========================================================================
// WP02 — a retried pipeline task's FIRST taskAdded/taskUpdated signals
// already carry pipelineId/stepId (#2123): the coordinator-side drop of
// identity-less events can no longer swallow a retry.
// ===========================================================================
TEST_CASE( "WP02 retried task binds pipeline identity inside the submission transaction",
           "[r32t04][wp02]" )
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();
    engine.setMaxWorkers( 2 );
    auto &center = sicnu::TaskCenter::instance();

    std::atomic<int> runs{ 0 };
    engine.registerExecutor(
        "r32t04:retry_step",
        [&]( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
            if ( runs.fetch_add( 1 ) == 0 )
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::ProcessingFailure,
                    "intentional first-run failure" );
            Json::Value out( Json::objectValue );
            out["ok"] = true;
            return out;
        } );

    sicnu::workflow::WorkflowDefinition def;
    def.id = "r32t04_retry_def";
    def.title = "Retry Pipeline";
    sicnu::workflow::StepDef step;
    step.id = "s1";
    step.operatorId = "r32t04:retry_step";
    def.steps.push_back( step );

    // Capture every identity-bearing signal emission.
    std::mutex signalMutex;
    std::vector<std::pair<long, QString>> addedIdentities;
    const QMetaObject::Connection addedConn =
        QObject::connect( &center, &sicnu::TaskCenter::taskAdded,
                          [&]( const AlgorithmTaskInfo &info ) {
                              std::lock_guard<std::mutex> lock( signalMutex );
                              addedIdentities.emplace_back( info.pipelineId, info.stepId );
                          } );

    const long pipelineId = center.submitPipeline( def );
    REQUIRE( pipelineId >= 0 );

    REQUIRE( pollUntil( [&]
                        {
                            const auto info = center.getPipelineInfo( pipelineId );
                            return info.isFailed;
                        },
                        10'000 ) );
    const auto failedInfo = center.getPipelineInfo( pipelineId );
    REQUIRE( failedInfo.stepToTaskId.contains( "s1" ) );
    const long firstTaskId = failedInfo.stepToTaskId.value( "s1" );

    addedIdentities.clear();
    const long retried = center.retryTask( firstTaskId );
    REQUIRE( retried > 0 );

    REQUIRE( pollUntil( [&]
                        {
                            const auto info = center.getPipelineInfo( pipelineId );
                            return info.isCompleted && !info.isFailed;
                        },
                        10'000 ) );

    {
        std::lock_guard<std::mutex> lock( signalMutex );
        // Every taskAdded emitted for the retry carries pipeline identity —
        // the FIRST flush, not a post-submit patch (#2123).
        for ( const auto &identity : addedIdentities )
        {
            if ( identity.first == -1 || identity.second.isEmpty() )
                FAIL( "taskAdded emitted without pipeline identity (pipelineId="
                      << identity.first << " stepId=\"" << identity.second.toStdString()
                      << "\")" );
        }
        REQUIRE( addedIdentities.size() >= 1 );
    }
    QObject::disconnect( addedConn );
    center.shutdown();
    engine.shutdownForTests();
}

// ===========================================================================
// WP03 — one sidecar naming authority: stem-based with the PAM full-name
// exception (#2124).
// ===========================================================================
TEST_CASE( "WP03 sidecarPath follows the stem rule with the PAM exception",
           "[r32t04][wp03]" )
{
    using sicnu::workflow::ArtifactGC;
    // Shapefile family + rasters: stem rule.
    REQUIRE( ArtifactGC::sidecarPath( "/w/out.shp", ".shx" ).endsWith( "/out.shx" ) );
    REQUIRE( ArtifactGC::sidecarPath( "/w/out.shp", ".dbf" ).endsWith( "/out.dbf" ) );
    REQUIRE( ArtifactGC::sidecarPath( "/w/out.shp", ".shp.xml" ).endsWith( "/out.shp.xml" ) );
    REQUIRE( ArtifactGC::sidecarPath( "/w/out.tif", ".tfw" ).endsWith( "/out.tfw" ) );
    REQUIRE_FALSE( ArtifactGC::sidecarPath( "/w/out.tif", ".tfw" ).endsWith( "out.tif.tfw" ) );
    // PAM exception: full filename + .aux.xml.
    REQUIRE( ArtifactGC::sidecarPath( "/w/out.tif", ".aux.xml" ).endsWith( "out.tif.aux.xml" ) );
    // Multi-dot stems keep everything but the last suffix.
    REQUIRE( ArtifactGC::sidecarPath( "/w/a.b.c.tif", ".tfw" ).endsWith( "/a.b.c.tfw" ) );
}

TEST_CASE( "WP03 GC removes stem sidecars and the PAM file together with the main artifact",
           "[r32t04][wp03]" )
{
    QTemporaryDir dir;
    const QString main = dir.filePath( "layer.shp" );
    const QString shx = dir.filePath( "layer.shx" );
    const QString dbf = dir.filePath( "layer.dbf" );
    const QString pam = dir.filePath( "raster.tif.aux.xml" );
    const QString raster = dir.filePath( "raster.tif" );
    for ( const QString &path : { main, shx, dbf, raster, pam } )
        QFile( path ).open( QIODevice::WriteOnly );

    const QStringList removed =
        sicnu::workflow::ArtifactGC::removeFilesWithSidecars( { main, raster } );
    REQUIRE_FALSE( QFile::exists( main ) );
    REQUIRE_FALSE( QFile::exists( shx ) ); // stem sidecar went with the .shp
    REQUIRE_FALSE( QFile::exists( dbf ) );
    REQUIRE_FALSE( QFile::exists( raster ) );
    REQUIRE_FALSE( QFile::exists( pam ) ); // PAM full-name exception honored
    REQUIRE( removed.size() >= 5 );
}

// ===========================================================================
// WP06 — the schema/engine type-check id/title/name and step inputs (#2110).
// ===========================================================================
TEST_CASE( "WP06 schema rejects wrong-typed top-level id/title and inputs members",
           "[r32t04][wp06]" )
{
    {
        PluginDiagnosticLog log;
        REQUIRE_FALSE( validateWorkflowDocument(
            parseJson( R"({ "id": {"bad":1}, "steps": [ {"id":"s1","operator":"rs:x"} ] })" ),
            log ) );
        REQUIRE( log.hasErrors() );
    }
    {
        PluginDiagnosticLog log;
        REQUIRE_FALSE( validateWorkflowDocument(
            parseJson( R"({ "title": [1], "steps": [ {"id":"s1","operator":"rs:x"} ] })" ),
            log ) );
        REQUIRE( log.hasErrors() );
    }
    {
        // inputs[].port wrong type — used to throw Json::LogicError at CLI
        // conversion time (exit 3) instead of a typed ValidationFailure.
        PluginDiagnosticLog log;
        REQUIRE_FALSE( validateWorkflowDocument(
            parseJson( R"({ "steps": [ {"id":"s1","operator":"rs:x",
                             "inputs": [ {"port": 7, "source": "q"} ] } ] })" ),
            log ) );
        REQUIRE( log.hasErrors() );
    }
    {
        // Both accepted spellings with correct types still validate.
        PluginDiagnosticLog log;
        REQUIRE( validateWorkflowDocument(
            parseJson( R"({ "id": "w1", "title": "W",
                          "steps": [ {"id":"s1","operator":"rs:x",
                                      "inputs": [ {"port":"p","source":"s"},
                                                  {"fromStepId":"a","toPort":"b"} ] } ] })" ),
            log ) );
    }
}

TEST_CASE( "WP06 engine refuses wrong-typed id/title instead of defaulting to agent_plan",
           "[r32t04][wp06]" )
{
    sicnu::workflow::WorkflowDefinition def;
    std::string error;
    REQUIRE_FALSE( sicnu::workflow::workflowDefinitionFromJson(
        parseJson( R"({ "id": 42, "steps": [ {"id":"s1","operatorId":"rs:x"} ] })" ), def,
        error ) );
    REQUIRE( error.find( "'id' must be a string" ) != std::string::npos );

    error.clear();
    REQUIRE_FALSE( sicnu::workflow::workflowDefinitionFromJson(
        parseJson( R"({ "title": {"x":1}, "steps": [ {"id":"s1","operatorId":"rs:x"} ] })" ),
        def, error ) );
    REQUIRE( error.find( "'title' must be a string" ) != std::string::npos );

    // Absent members keep the documented defaults.
    error.clear();
    REQUIRE( sicnu::workflow::workflowDefinitionFromJson(
        parseJson( R"({ "steps": [ {"id":"s1","operatorId":"rs:x"} ] })" ), def, error ) );
    REQUIRE( def.id == "agent_plan" );
}

// ===========================================================================
// WP07 — canonical evidence with ARRAY payloads round-trips (#2115).
// ===========================================================================
TEST_CASE( "WP07 array evidence round-trips through canonical JSON",
           "[r32t04][wp07]" )
{
    sicnu::verify::VerificationEvidence evidence;
    evidence.source = "relational.consistency";
    // The production engine's relational evaluator emits one entry per
    // relation — an ARRAY of objects on BOTH sides.
    evidence.observed.append( parseJson( R"({"left":"a.tif","right":"b.tif","holds":true})" ) );
    evidence.observed.append( parseJson( R"({"left":"a.tif","right":"c.tif","holds":false})" ) );
    evidence.expected.append( parseJson( R"({"relations":1})" ) );
    evidence.hasSampling = true;
    evidence.sampledPoints = 512;

    const Json::Value canonical = evidence.toCanonicalJson();
    sicnu::verify::VerificationEvidence parsed;
    std::string error;
    REQUIRE( parsed.fromCanonicalJson( canonical, error ) );
    REQUIRE( parsed.observed == evidence.observed );
    REQUIRE( parsed.expected == evidence.expected );
    REQUIRE( parsed.source == evidence.source );
    REQUIRE( parsed.hasSampling );
    REQUIRE( parsed.sampledPoints == 512 );

    // Hostile shapes stay refused: non-object evidence, unknown fields,
    // missing observed/expected, bad sampling coupling.
    REQUIRE_FALSE( parsed.fromCanonicalJson( Json::Value( "string" ), error ) );
    REQUIRE_FALSE( parsed.fromCanonicalJson( parseJson( R"({"source":"x"})" ), error ) );
    REQUIRE_FALSE( parsed.fromCanonicalJson(
        parseJson( R"({"source":"x","observed":[1],"expected":{},"extra":1})" ), error ) );
    REQUIRE_FALSE( parsed.fromCanonicalJson(
        parseJson( R"({"source":"x","observed":[],"expected":{},"hasSampling":true})" ),
        error ) );
}

// ===========================================================================
// WP11 — the in-memory execution cache validates by CONTENT within the
// digest budget: a same-size/same-mtime rewrite invalidates.
// ===========================================================================
TEST_CASE( "WP11 cached execution invalidated by same-size same-mtime rewrite",
           "[r32t04][wp11]" )
{
    auto &cache = sicnu::data::ExecutionResultCache::instance();
    cache.clear();
    cache.setEnabled( true );

    QTemporaryDir dir;
    const QString artifact = dir.filePath( "out.txt" );
    {
        QFile f( artifact );
        f.open( QIODevice::WriteOnly );
        f.write( "version-one" );
    }
    const QDateTime originalMtime = QFileInfo( artifact ).lastModified();

    sicnu::data::ExecutionFingerprint fp =
        sicnu::data::makeExecutionFingerprintV2( "r32t04:cache", "1.0", QJsonObject{}, {} );
    sicnu::data::ExecutionResultCache::CachedExecution entry;
    entry.declaredOutputPath = artifact;
    entry.producedArtifacts = QStringList{ artifact };
    entry.artifactSizes.insert( artifact, QFileInfo( artifact ).size() );
    entry.artifactMsecs.insert( artifact,
                                static_cast<qint64>( originalMtime.toMSecsSinceEpoch() ) );
    cache.storeExecution( fp, entry );
    REQUIRE( cache.lookupExecution( fp ).has_value() );

    // Same bytes size, restored mtime, DIFFERENT content — the stat proxy is
    // blind; the recorded content digest must invalidate.
    {
        QFile f( artifact );
        f.open( QIODevice::WriteOnly | QIODevice::Truncate );
        f.write( "version-two" ); // same length
    }
    QFile::setFileTime( originalMtime, QFile::FileModificationTime, artifact );
    REQUIRE( QFileInfo( artifact ).lastModified() == originalMtime );

    REQUIRE_FALSE( cache.lookupExecution( fp ).has_value() );

    cache.setEnabled( false );
    cache.clear();
}

// ===========================================================================
// WP12 — passport verdict typing + bounded percent projection (#1953).
// ===========================================================================
TEST_CASE( "WP12 non-bool passport verdict blocks; missing verdict stays unknown",
           "[r32t04][wp12]" )
{
    const auto aggregate = []( const Json::Value &passport ) {
        return sicnu::teaching::LabReadiness::aggregate(
            "lab.x", Json::Value(), passport, Json::Value(), Json::Value(), false );
    };
    {
        // String verdict: used to fall through to the PASS branch.
        const auto r = aggregate( parseJson( R"({"ok": "false"})" ) );
        bool sawPassportPass = false;
        for ( const auto &item : r.items )
            if ( item.category == "passport" && item.ok )
                sawPassportPass = true;
        REQUIRE_FALSE( sawPassportPass );
        REQUIRE( r.level != sicnu::teaching::ReadinessLevel::Ready );
    }
    {
        const auto r = aggregate( parseJson( R"({"ok": false})" ) );
        REQUIRE( r.level == sicnu::teaching::ReadinessLevel::Blocked );
    }
    {
        const auto r = aggregate( parseJson( R"({"ok": true})" ) );
        REQUIRE( r.level != sicnu::teaching::ReadinessLevel::Blocked );
    }
    {
        // No verdict member: unknown (with an item), never silent.
        const auto r = aggregate( parseJson( R"({"crs": "EPSG:4326"})" ) );
        bool hasPassportUnknown = false;
        for ( const auto &item : r.items )
            if ( item.category == "passport" && item.severity == "unknown" )
                hasPassportUnknown = true;
        REQUIRE( hasPassportUnknown );
    }
}

TEST_CASE( "WP12 out-of-int-range percent clamps on the double before the cast",
           "[r32t04][wp12]" )
{
    // A minimal curriculum manifest with zero labs makes the reported hint
    // the only progress source (the r15-E fallback path).
    const Json::Value manifest = parseJson( R"({
        "schema": "sicnu.curriculum/1", "course": {"id": "c1"},
        "modules": []
    })" );
    Json::Value progress( Json::objectValue );
    progress["schema"] = "sicnu.curriculum.progress.summary/1";
    progress["overall_percent"] = 1e300;
    const auto vm = sicnu::teaching::CourseHomeViewModel::fromDocuments(
        manifest, progress, Json::Value() );
    REQUIRE( vm.overallPercent >= 0 );
    REQUIRE( vm.overallPercent <= 100 );
    REQUIRE( vm.overallPercent == 100 );
}

// ===========================================================================
// WP13 — streamed digest equals one-shot; stamp size/digest from one pass.
// ===========================================================================
TEST_CASE( "WP13 Sha256Stream chunked digest equals the one-shot digest",
           "[r32t04][wp13]" )
{
    // Deterministic pseudo-random payload across sizes crossing the 64-byte
    // block boundary and the two-block tail case.
    for ( std::size_t size : { std::size_t( 0 ), std::size_t( 1 ), std::size_t( 55 ),
                               std::size_t( 56 ), std::size_t( 63 ), std::size_t( 64 ),
                               std::size_t( 65 ), std::size_t( 119 ), std::size_t( 120 ),
                               std::size_t( 128 ), std::size_t( 4096 ) } )
    {
        std::string bytes( size, '\0' );
        for ( std::size_t i = 0; i < size; ++i )
            bytes[i] = static_cast<char>( ( i * 31 + 7 ) % 256 );
        const std::string oneShot = sicnu::lab::sha256Hex( bytes );

        sicnu::lab::Sha256Stream stream;
        for ( std::size_t offset = 0; offset < size; offset += 7 ) // odd chunking
        {
            const std::size_t chunk = std::min<std::size_t>( 7, size - offset );
            stream.update( bytes.data() + offset, chunk );
        }
        REQUIRE( stream.finalizeHex() == oneShot );
    }
}

TEST_CASE( "WP13 verdict stamp binds digest and size from a single observation",
           "[r32t04][wp13]" )
{
    QTemporaryDir dir;
    const QString artifact = dir.filePath( "verdict.json" );
    {
        QFile f( artifact );
        f.open( QIODevice::WriteOnly );
        f.write( QByteArray( 10'000, 'x' ) );
    }
    const std::string summaryDigest( 64, 'a' );
    const std::string stamp = sicnu::teaching::makeVerdictStamp( summaryDigest,
                                                                 artifact.toStdString() );
    const auto parsed = sicnu::teaching::parseVerdictStamp( stamp );
    REQUIRE( parsed.parsed );
    REQUIRE( parsed.size == 10'000ULL ); // from the digest pass, no second open
    REQUIRE( parsed.contentDigest
             == sicnu::teaching::verdictArtifactContentDigest( artifact.toStdString() ) );

    // Missing artifact → no stamp (fail-closed, not a wrapped tellg size).
    REQUIRE( sicnu::teaching::makeVerdictStamp( summaryDigest,
                                                dir.filePath( "gone.bin" ).toStdString() )
                 .empty() );
}

// ===========================================================================
// WP14 — the grader.overall row itself carries the unavailability reason.
// ===========================================================================
TEST_CASE( "WP14 grader unavailability reason lands on the grader.overall row",
           "[r32t04][wp14]" )
{
    const auto projection = sicnu::teaching::LabFeedbackProjection::fromReports(
        "lab.x", Json::Value(),
        parseJson( R"({ "schema": "sicnu.lab.grade/1", "overall": "indeterminate",
                        "error": "grader offline" })" ) );
    bool foundRow = false;
    for ( const auto &row : projection.rows )
    {
        if ( row.id == "grader.overall" )
        {
            foundRow = true;
            REQUIRE( row.reasonZh == "grader offline" );
            REQUIRE( row.status == "indeterminate" );
            REQUIRE_FALSE( row.countsAsPass );
        }
    }
    REQUIRE( foundRow );
    bool sawIssue = false;
    for ( const auto &issue : projection.issuesZh )
        if ( issue.find( "评分不可用" ) != std::string::npos )
            sawIssue = true;
    REQUIRE( sawIssue );
}

// ===========================================================================
// WP16 — typed metric admission; equivalent-runs walks fail closed.
// ===========================================================================
TEST_CASE( "WP16 benchmark result rejects metrics without a numeric value",
           "[r32t04][wp16]" )
{
    const auto base = []( const char *metrics ) {
        return parseJson( std::string( R"({ "schema_version": 1, "result_id": "r1",
            "benchmark_id": "b1", "metrics": )" )
                          + metrics + " }" );
    };
    sicnu::experiment::BenchmarkResult result;
    // Missing value / wrong-typed value / non-object entry all refuse.
    for ( const char *metrics :
          { R"([ {"name": "iou"} ])", R"([ {"name": "iou", "value": "0.5"} ])",
            R"([ "broken" ])" } )
    {
        const auto parsed = sicnu::experiment::BenchmarkResult::fromJson(
            QJsonObject::fromJson( QByteArray( base( metrics ).toStyledString().c_str() ) ) );
        REQUIRE_FALSE( parsed.has_value() );
    }
    // A well-formed metric still parses.
    const auto ok = sicnu::experiment::BenchmarkResult::fromJson( QJsonObject::fromJson(
        QByteArray( base( R"([ {"name": "iou", "value": 0.5} ])" ).toStyledString().c_str() ) ) );
    REQUIRE( ok.has_value() );
    REQUIRE( ok->metrics().size() == 1 );
}

TEST_CASE( "WP16 equivalentRuns reports failure instead of a partial list",
           "[r32t04][wp16]" )
{
    QTemporaryDir dir;
    sicnu::experiment::ExperimentStore store;
    QString openError;
    REQUIRE( store.open( dir.filePath( "exp.sqlite3" ), &openError ) );
    // Healthy store, no runs: success with an empty list (end-of-pagination).
    QStringList runs;
    QString error;
    REQUIRE( sicnu::experiment::ReplayReadiness::equivalentRuns(
        store, QStringLiteral( "no-such-fingerprint" ), QString(), runs, &error ) );
    REQUIRE( runs.isEmpty() );
    // Closed store → listRuns errors → the walk must fail CLOSED instead of
    // returning the partial (here: empty) list as if it were complete.
    store.close();
    REQUIRE_FALSE( sicnu::experiment::ReplayReadiness::equivalentRuns(
        store, QStringLiteral( "no-such-fingerprint" ), QString(), runs, &error ) );
    REQUIRE( !error.isEmpty() );
}

// ===========================================================================
// WP17 — the context-cache key is injective over field content (#2124-class).
// ===========================================================================
TEST_CASE( "WP17 delimiter-carrying goal/intent pairs never share a cache key",
           "[r32t04][wp17]" )
{
    sicnu::science_context::CacheKeyMaterial a;
    a.assetDigest = "d";
    a.goal = "a|b";
    a.intent = "c";

    sicnu::science_context::CacheKeyMaterial b;
    b.assetDigest = "d";
    b.goal = "a";
    b.intent = "b|c";

    REQUIRE( sicnu::science_context::makeCacheKey( a )
             != sicnu::science_context::makeCacheKey( b ) );

    // Same request stays stable; empty fields and unicode stay injective.
    sicnu::science_context::CacheKeyMaterial c1 = a;
    REQUIRE( sicnu::science_context::makeCacheKey( c1 )
             == sicnu::science_context::makeCacheKey( a ) );
    sicnu::science_context::CacheKeyMaterial u1;
    u1.goal = u8"目标|一";
    u1.intent = u8"意图";
    sicnu::science_context::CacheKeyMaterial u2;
    u2.goal = u8"目标";
    u2.intent = u8"一|意图";
    REQUIRE( sicnu::science_context::makeCacheKey( u1 )
             != sicnu::science_context::makeCacheKey( u2 ) );
}

// ===========================================================================
// WP18 — bundle recovery admits by type and budget, fails closed.
// ===========================================================================
TEST_CASE( "WP18 bundleFromJson rejects corrupt sections fail-closed",
           "[r32t04][wp18]" )
{
    const auto goodBody = []( const std::string &extra ) {
        return parseJson( std::string( R"({ "schema": "sicnu.science_context.bundle/1",
            "bundle_id": "b", "goal": "g", "intent": "i" )" )
                          + extra + " }" );
    };
    sicnu::science_context::ScientificContextBundle bundle;
    std::string error;
    // Non-object capability entry: silently skipped before.
    REQUIRE_FALSE( sicnu::science_context::bundleFromJson(
        goodBody( R"(, "capabilities": [ "not-an-object" ] )" ), bundle, &error ) );
    // Non-object recipe entry.
    REQUIRE_FALSE( sicnu::science_context::bundleFromJson(
        goodBody( R"(, "recipes": [ 42 ] )" ), bundle, &error ) );
    // Negative budget.
    REQUIRE_FALSE( sicnu::science_context::bundleFromJson(
        goodBody( R"(, "constraints": { "max_bytes": -1 } )" ), bundle, &error ) );
    // Wrong-typed execution_blocked: used to default FALSE (fail-open).
    REQUIRE_FALSE( sicnu::science_context::bundleFromJson(
        goodBody( R"(, "planner_projection": { "execution_blocked": "yes" } )" ), bundle,
        &error ) );
    // Absent execution_blocked keeps the documented default (not blocked).
    REQUIRE( sicnu::science_context::bundleFromJson(
        goodBody( R"(, "planner_projection": { "goal": "g" } )" ), bundle, &error ) );
    REQUIRE_FALSE( bundle.planner.executionBlocked );
}

// ===========================================================================
// WP15 — batch progress counts ADOPTED rows; all-adopted emits completion;
// cancellation reports its honest partial count (#1953).
// ===========================================================================
TEST_CASE( "WP15 batch progress is monotonic over adopted + newly graded rows",
           "[r32t04][wp15][batch]" )
{
    QTemporaryDir dir;
    const QString submissions = dir.path() + QStringLiteral( "/submissions" );
    QDir submissionsDir( submissions );
    submissionsDir.mkpath( QStringLiteral( "." ) );
    const QString students[] = { QStringLiteral( "s01" ), QStringLiteral( "s02" ) };
    int fileNo = 0;
    for ( const QString &student : students )
    {
        QDir sd( submissions + QStringLiteral( "/" ) + student );
        sd.mkpath( QStringLiteral( "." ) );
        QFile f( sd.filePath( QStringLiteral( "artifact_%1.tif" ).arg( ++fileNo ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "original-bytes" );
    }

    sicnu::teaching_admin::BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab_r32t04" );
    cfg.submissionsDir = submissions;
    cfg.outDir = submissions;
    cfg.rubricVersion = QStringLiteral( "r1" );
    cfg.labVersion = QStringLiteral( "v1" );
    cfg.softwareVersion = QStringLiteral( "sv1" );
    cfg.checkpointPath = dir.path() + QStringLiteral( "/ckpt.json" );

    const auto passingRow = []( const sicnu::teaching_admin::SubmissionItem &item ) {
        sicnu::teaching_admin::BatchRowResult row;
        row.studentId = item.studentId;
        row.artifactPath = item.path;
        row.status = QStringLiteral( "pass" );
        row.verdict = QStringLiteral( "pass" );
        row.score = 88.0;
        return row;
    };

    // Run 1: two fresh grades; progress must reach (2, 2).
    {
        std::mutex mutex;
        std::vector<std::pair<int, int>> events;
        const auto report = sicnu::teaching_admin::runBatchAssessment(
            cfg, passingRow, nullptr,
            [&]( int done, int total ) {
                std::lock_guard<std::mutex> lock( mutex );
                events.emplace_back( done, total );
            } );
        REQUIRE( report.rows.size() == 2 );
        std::lock_guard<std::mutex> lock( mutex );
        REQUIRE( events.size() >= 2 );
        REQUIRE( events.back() == std::make_pair( 2, 2 ) );
    }

    // Run 2 (restart): both rows ADOPTED — the callback must still emit the
    // terminal (2, 2) state instead of never firing (the old from-zero bug).
    {
        std::mutex mutex;
        std::vector<std::pair<int, int>> events;
        const auto report = sicnu::teaching_admin::runBatchAssessment(
            cfg, passingRow, nullptr,
            [&]( int done, int total ) {
                std::lock_guard<std::mutex> lock( mutex );
                events.emplace_back( done, total );
            } );
        REQUIRE( report.resumed == 2 );
        std::lock_guard<std::mutex> lock( mutex );
        REQUIRE( events.size() >= 1 );
        REQUIRE( events.back() == std::make_pair( 2, 2 ) );
    }
}
