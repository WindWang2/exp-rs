// tests/test_r28_t05_replay_env_identity.cpp — R28-T05 regression for
// #1772 (WE-09): environment drift is an identity deviation only when BOTH
// runs declare Strict determinism (the remediation text's contract); below
// Strict it is a deviation of record that cannot flip the verdict.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_ids.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/replay_deviation.h"
#include "experiment/run_recorder.h"

#include <QJsonObject>
#include <QTemporaryDir>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace
{

struct StoreFixture
{
    QTemporaryDir dir;
    ExperimentStore store;

    StoreFixture()
    {
        REQUIRE( store.open( dir.filePath( QStringLiteral( "experiment.sqlite" ) ) ) );
    }
};

RunStartRequest baseRequest( const QString &experimentId, const QString &executionRef )
{
    static const QString pinnedVersionId = DatasetVersionId::generate().toString();
    RunStartRequest request;
    request.experimentId = experimentId;
    request.algorithmId = QStringLiteral( "workflow:env-replay" );
    request.algorithmVersion = QStringLiteral( "1" );
    request.parameters = QJsonObject{ { QStringLiteral( "p" ), 1 } };
    request.datasetVersionId = pinnedVersionId;
    request.datasetFingerprint = QStringLiteral( "fp-ds" );
    request.splitManifestId = QStringLiteral( "split-1" );
    request.splitFingerprint = QStringLiteral( "fp-split" );
    request.modelId = QStringLiteral( "model-a" );
    request.modelDigest = QStringLiteral( "digest-1" );
    request.seed = 42;
    request.executionRef = executionRef;
    request.environment = RunEnvironment::fromFields(
        QJsonObject{ { QStringLiteral( "platform" ), QStringLiteral( "test" ) } } );
    return request;
}

QString startRun( ExperimentRunRecorder &recorder, const RunStartRequest &request,
                  const QString &artifactDigest )
{
    auto runId = recorder.startRun( request );
    REQUIRE( runId.has_value() );
    const QVector<ExperimentRun::Artifact> artifacts{
        [] ( const QString &digest ) {
            ExperimentRun::Artifact entry;
            entry.path = QStringLiteral( "out.tif" );
            entry.role = QStringLiteral( "primary" );
            entry.digest = digest;
            entry.sizeBytes = 128;
            return entry;
        }( artifactDigest ) };
    const QJsonObject metrics{ { QStringLiteral( "accuracy" ), 0.9 } };
    REQUIRE( recorder.markSucceeded( runId.value(), artifacts, metrics ).has_value() );
    return runId.value();
}

const ReplayDeviationEntry *entryFor( const ReplayDeviationReport &report,
                                      const QString &dimension )
{
    for ( const ReplayDeviationEntry &entry : report.deviationDetails )
        if ( entry.dimension == dimension )
            return &entry;
    return nullptr;
}

} // namespace

TEST_CASE( "environment drift below Strict determinism is not identity (WE-09)",
           "[r28_t05][experiment][replay_deviation]" )
{
    StoreFixture fixture;
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "e-env" ) );
    experiment.setName( QStringLiteral( "e-env" ) );
    REQUIRE( fixture.store.upsertExperiment( experiment ).has_value() );

    ExperimentRunRecorder recorder( fixture.store );

    // Both runs declare BestEffort determinism (a note is required) and
    // differ ONLY in the recorded environment platform.
    RunStartRequest originalRequest =
        baseRequest( QStringLiteral( "e-env" ), QStringLiteral( "exec-1" ) );
    originalRequest.determinism = DeterminismGrade::BestEffort;
    originalRequest.determinismNote = QStringLiteral( "gpu nondeterminism accepted" );
    const QString original = startRun( recorder, originalRequest, QStringLiteral( "digest-out" ) );

    RunStartRequest replayRequest =
        baseRequest( QStringLiteral( "e-env" ), QStringLiteral( "exec-2" ) );
    replayRequest.determinism = DeterminismGrade::BestEffort;
    replayRequest.determinismNote = QStringLiteral( "gpu nondeterminism accepted" );
    replayRequest.environment = RunEnvironment::fromFields(
        QJsonObject{ { QStringLiteral( "platform" ), QStringLiteral( "other-host" ) } } );
    const QString replay = startRun( recorder, replayRequest, QStringLiteral( "digest-out" ) );

    ReplayDeviationAnalyzer analyzer( fixture.store );
    const auto report = analyzer.analyze( original, replay );
    REQUIRE( report.has_value() );
    // Same identity pins + equal outputs → Identical; the environment drift
    // is reported as a NON-identity deviation of record.
    CHECK( report.value().verdict == ReplayDeviationReport::Verdict::Identical );
    const ReplayDeviationEntry *envEntry =
        entryFor( report.value(), QStringLiteral( "environment" ) );
    REQUIRE( envEntry != nullptr );
    CHECK_FALSE( envEntry->identityPin );
}

TEST_CASE( "environment drift under mutual Strict determinism is identity (WE-09)",
           "[r28_t05][experiment][replay_deviation]" )
{
    StoreFixture fixture;
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "e-env-strict" ) );
    experiment.setName( QStringLiteral( "e-env-strict" ) );
    REQUIRE( fixture.store.upsertExperiment( experiment ).has_value() );

    ExperimentRunRecorder recorder( fixture.store );

    // Strict on both sides (the default): environment drift now carries the
    // identity verdict.
    const RunStartRequest originalRequest =
        baseRequest( QStringLiteral( "e-env-strict" ), QStringLiteral( "exec-1" ) );
    const QString original = startRun( recorder, originalRequest, QStringLiteral( "digest-out" ) );

    RunStartRequest replayRequest =
        baseRequest( QStringLiteral( "e-env-strict" ), QStringLiteral( "exec-2" ) );
    replayRequest.environment = RunEnvironment::fromFields(
        QJsonObject{ { QStringLiteral( "platform" ), QStringLiteral( "other-host" ) } } );
    const QString replay = startRun( recorder, replayRequest, QStringLiteral( "digest-out" ) );

    ReplayDeviationAnalyzer analyzer( fixture.store );
    const auto report = analyzer.analyze( original, replay );
    REQUIRE( report.has_value() );
    CHECK( report.value().verdict == ReplayDeviationReport::Verdict::Deviated );
    const ReplayDeviationEntry *envEntry =
        entryFor( report.value(), QStringLiteral( "environment" ) );
    REQUIRE( envEntry != nullptr );
    CHECK( envEntry->identityPin );
}
