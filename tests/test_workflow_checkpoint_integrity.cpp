// tests/test_workflow_checkpoint_integrity.cpp — #1038/#1056 checkpoint honesty
//
//   * whole-file reads are capped (no unbounded readAll on a hostile file)
//   * one corrupt/oversized/wrong-typed checkpoint quarantines itself in the
//     recovery sweep — the remaining checkpoints still recover
//   * wrong-typed external JSON in workflow definitions fails typed instead
//     of escaping Json::LogicError through WorkflowRun::fromJson
//
// Self-contained: compiles the workflow core sources directly (no
// sicnu_workflow shared lib), mirroring the D17 test style.
#include <catch2/catch_test_macros.hpp>

#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_run.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <json/json.h>

#include <memory>
#include <string>
#include <vector>

using namespace sicnu::workflow;

namespace {

WorkflowDefinition plainDefinition()
{
    WorkflowDefinition def;
    def.id = "wf_checkpoint_integrity";
    def.title = "Checkpoint Integrity";
    StepDef step;
    step.id = "s1";
    step.operatorId = "rs:test";
    def.steps.push_back( step );
    return def;
}

std::unique_ptr<WorkflowRun> runningRun( const std::string &runId )
{
    auto run = WorkflowRun::createFromDefinition( plainDefinition(), runId );
    run->transitionTo( WorkflowRunState::Planning );
    run->transitionTo( WorkflowRunState::Ready );
    run->transitionTo( WorkflowRunState::Running );
    return run;
}

Json::Value parseJson( const QByteArray &bytes )
{
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string errs;
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( bytes.constData(), bytes.constData() + bytes.size(), &parsed, &errs ) )
        return Json::Value( Json::nullValue );
    return parsed;
}

} // namespace

TEST_CASE( "loadCheckpoint refuses payloads beyond the read cap", "[workflow][checkpoint][1056]" )
{
    QTemporaryDir tmpDir;
    REQUIRE( tmpDir.isValid() );
    WorkflowCheckpointManager manager;

    // A small honest checkpoint still loads.
    const QString honestPath = manager.saveCheckpoint( *runningRun( "run-cap-honest" ), tmpDir.path() );
    REQUIRE( !honestPath.isEmpty() );
    QString err;
    REQUIRE( manager.loadCheckpoint( honestPath, &err ) != nullptr );
    REQUIRE( err.isEmpty() );

    // A file beyond the cap is rejected typed, without reading it all.
    const QString bloatedPath = tmpDir.filePath( QStringLiteral( "checkpoint_run-cap-bloated.json" ) );
    {
        QFile blob( bloatedPath );
        REQUIRE( blob.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        const QByteArray chunk( 1024 * 1024, 'x' );
        for ( int i = 0; i <= kMaxCheckpointReadBytes / ( 1024 * 1024 ); ++i )
            REQUIRE( blob.write( chunk ) == chunk.size() );
    }
    REQUIRE( QFileInfo( bloatedPath ).size() > kMaxCheckpointReadBytes );

    err.clear();
    REQUIRE( manager.loadCheckpoint( bloatedPath, &err ) == nullptr );
    REQUIRE( err.contains( QStringLiteral( "read cap" ) ) );
}

TEST_CASE( "workflowDefinitionFromJson fails typed on wrong-typed meta.ui",
           "[workflow][definition][1038]" )
{
    std::string error;

    // Baseline: valid meta.ui parses.
    Json::Value valid = parseJson( R"( {"id":"wf","steps":[{"id":"s1","meta":{"ui":{"x":12.5,"y":-3,
        "portAddToMap":{"band_math":true,"histogram":false}}}}]} )" );
    REQUIRE( valid.isObject() );
    WorkflowDefinition def;
    REQUIRE( workflowDefinitionFromJson( valid, def, error ) );
    REQUIRE( error.empty() );
    REQUIRE( def.steps.size() == 1 );
    REQUIRE( def.steps.front().uiMeta.portAddToMap.at( "band_math" ) );
    REQUIRE_FALSE( def.steps.front().uiMeta.portAddToMap.at( "histogram" ) );

    // "x":"oops" — used to throw Json::LogicError out of the parser (#1038).
    Json::Value badX = parseJson( R"( {"steps":[{"id":"s1","meta":{"ui":{"x":"oops"}}}]} )" );
    REQUIRE( badX.isObject() );
    WorkflowDefinition defX;
    REQUIRE_FALSE( workflowDefinitionFromJson( badX, defX, error ) );
    REQUIRE( error.find( "meta.ui.x" ) != std::string::npos );

    Json::Value badY = parseJson( R"( {"steps":[{"id":"s1","meta":{"ui":{"y":[]}}}]} )" );
    REQUIRE( badY.isObject() );
    WorkflowDefinition defY;
    REQUIRE_FALSE( workflowDefinitionFromJson( badY, defY, error ) );
    REQUIRE( error.find( "meta.ui.y" ) != std::string::npos );

    Json::Value badFlag = parseJson(
        R"( {"steps":[{"id":"s1","meta":{"ui":{"portAddToMap":{"a":"yes"}}}}]} )" );
    REQUIRE( badFlag.isObject() );
    WorkflowDefinition defFlag;
    REQUIRE_FALSE( workflowDefinitionFromJson( badFlag, defFlag, error ) );
    REQUIRE( error.find( "portAddToMap" ) != std::string::npos );
}

TEST_CASE( "WorkflowRun::fromJson rejects a wrong-typed checkpoint typed, never by exception",
           "[workflow][run][1038]" )
{
    const Json::Value bad = parseJson( R"(
    {
      "version": 1,
      "runId": "run-bad-meta",
      "state": "Running",
      "definition": {"id":"wf","steps":[{"id":"s1","meta":{"ui":{"x":"oops"}}}]},
      "stepPlans": []
    }
    )" );
    REQUIRE( bad.isObject() );

    std::string error;
    std::unique_ptr<WorkflowRun> run;
    try
    {
        run = WorkflowRun::fromJson( bad, error );
    }
    catch ( ... )
    {
        FAIL( "fromJson escaped with an exception instead of a typed error" );
    }
    REQUIRE( run == nullptr );
    REQUIRE( error.find( "meta.ui.x" ) != std::string::npos );
}

TEST_CASE( "recovery sweep quarantines each bad checkpoint individually",
           "[workflow][checkpoint][1038][1056]" )
{
    QTemporaryDir tmpDir;
    REQUIRE( tmpDir.isValid() );
    WorkflowCheckpointManager manager;

    // 1. A healthy interrupted run that must still recover.
    REQUIRE( !manager.saveCheckpoint( *runningRun( "run-good" ), tmpDir.path() ).isEmpty() );

    // 2. Corrupt JSON — already skipped before this track; still part of the oracle.
    {
        QFile corrupt( tmpDir.filePath( QStringLiteral( "checkpoint_run-corrupt.json" ) ) );
        REQUIRE( corrupt.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        corrupt.write( "{ this is not json" );
    }

    // 3. Wrong-typed meta.ui — the #1038 regression: used to abort the sweep.
    {
        auto tainted = runningRun( "run-wrongtype" );
        Json::Value root = tainted->toJson();
        REQUIRE( root.isObject() );
        root["definition"]["steps"][0]["meta"]["ui"]["x"] = "oops";
        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "  ";
        QFile f( tmpDir.filePath( QStringLiteral( "checkpoint_run-wrongtype.json" ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        const std::string payload = Json::writeString( writerBuilder, root );
        f.write( payload.data(), static_cast<qint64>( payload.size() ) );
    }

    // 4. Oversized — the #1056 cap in the sweep.
    {
        QFile blob( tmpDir.filePath( QStringLiteral( "checkpoint_run-bloated.json" ) ) );
        REQUIRE( blob.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        const QByteArray chunk( 1024 * 1024, 'x' );
        for ( int i = 0; i <= kMaxCheckpointReadBytes / ( 1024 * 1024 ); ++i )
            blob.write( chunk );
    }

    // Exactly the healthy run is recovered; each bad file is skipped in
    // isolation, no exception escapes the sweep.
    std::vector<std::shared_ptr<WorkflowRun>> recovered;
    REQUIRE_NOTHROW( recovered = manager.recoverInterruptedRuns( tmpDir.path() ) );
    REQUIRE( recovered.size() == 1 );
    REQUIRE( recovered.front()->runId() == "run-good" );
    REQUIRE( recovered.front()->state() == WorkflowRunState::Interrupted );
}
