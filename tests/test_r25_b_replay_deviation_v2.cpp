// tests/test_r25_b_replay_deviation_v2.cpp — R25-B feature 3: typed replay
// deviation entries (schema v2): domain attribution (data / model /
// experiment / environment), identity-pin flag and actionable remediation
// for every deviation, with byte-identical v1 fields and unchanged verdict
// semantics.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_ids.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/replay_deviation.h"
#include "experiment/run_recorder.h"

#include <QJsonArray>
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
    request.algorithmId = QStringLiteral( "workflow:under-test" );
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

ExperimentRun::Artifact artifact( const QString &path, const QString &digest )
{
    ExperimentRun::Artifact entry;
    entry.path = path;
    entry.role = QStringLiteral( "primary" );
    entry.digest = digest;
    entry.sizeBytes = 128;
    return entry;
}

QString startRun( ExperimentRunRecorder &recorder, const RunStartRequest &request,
                  const QString &artifactDigest )
{
    auto runId = recorder.startRun( request );
    REQUIRE( runId.has_value() );
    const QVector<ExperimentRun::Artifact> artifacts{ artifact( QStringLiteral( "out.tif" ), artifactDigest ) };
    const QJsonObject metrics{ { QStringLiteral( "accuracy" ), 0.9 } };
    REQUIRE( recorder.markSucceeded( runId.value(), artifacts, metrics ).has_value() );
    return runId.value();
}

const ReplayDeviationEntry *entryFor( const ReplayDeviationReport &report,
                                      const QString &dimension )
{
    for ( const ReplayDeviationEntry &entry : report.deviationDetails )
    {
        if ( entry.dimension == dimension )
            return &entry;
    }
    return nullptr;
}

} // namespace

TEST_CASE( "deviation entries attribute domains and prescribe remediation",
           "[r25_b][replay_deviation]" )
{
    StoreFixture fixture;
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "e-v2" ) );
    experiment.setName( QStringLiteral( "e-v2" ) );
    REQUIRE( fixture.store.upsertExperiment( experiment ).has_value() );

    ExperimentRunRecorder recorder( fixture.store );
    const RunStartRequest base = baseRequest( QStringLiteral( "e-v2" ), QStringLiteral( "exec-1" ) );
    const QString original =
      startRun( recorder, base, QStringLiteral( "digest-out" ) );

    // Deviated replay: a different dataset version pin.
    RunStartRequest dataRequest = baseRequest( QStringLiteral( "e-v2" ), QStringLiteral( "exec-2" ) );
    dataRequest.datasetVersionId = DatasetVersionId::generate().toString();
    const QString dataReplay =
      startRun( recorder, dataRequest, QStringLiteral( "digest-out" ) );

    ReplayDeviationAnalyzer analyzer( fixture.store );
    const auto dataDeviated = analyzer.analyze( original, dataReplay );
    REQUIRE( dataDeviated.has_value() );
    REQUIRE( dataDeviated.value().verdict == ReplayDeviationReport::Verdict::Deviated );

    // v2: every deviation string has a typed entry, with a domain + a
    // non-empty remediation; the dataset pin belongs to the DATA domain and
    // is an identity pin.
    REQUIRE( dataDeviated.value().deviationDetails.size()
             == dataDeviated.value().deviations.size() );
    const ReplayDeviationEntry *datasetEntry =
      entryFor( dataDeviated.value(), QStringLiteral( "dataset" ) );
    REQUIRE( datasetEntry != nullptr );
    CHECK( datasetEntry->domain == ReplayDeviationDomain::Data );
    CHECK( datasetEntry->identityPin );
    CHECK_FALSE( datasetEntry->remediation.isEmpty() );

    // Deviated replay: a different model digest — MODEL domain.
    RunStartRequest modelRequest = baseRequest( QStringLiteral( "e-v2" ), QStringLiteral( "exec-3" ) );
    modelRequest.modelDigest = QStringLiteral( "digest-other" );
    const QString modelReplay =
      startRun( recorder, modelRequest, QStringLiteral( "digest-out" ) );
    const auto modelDeviated = analyzer.analyze( original, modelReplay );
    REQUIRE( modelDeviated.has_value() );
    REQUIRE( modelDeviated.value().verdict == ReplayDeviationReport::Verdict::Deviated );
    const ReplayDeviationEntry *modelEntry =
      entryFor( modelDeviated.value(), QStringLiteral( "model" ) );
    REQUIRE( modelEntry != nullptr );
    CHECK( modelEntry->domain == ReplayDeviationDomain::Model );
    CHECK( modelEntry->identityPin );
    CHECK( modelEntry->remediation.contains( QStringLiteral( "digest" ) ) );

    // Deviated replay: a different config pin — EXPERIMENT domain.
    RunStartRequest configRequest = baseRequest( QStringLiteral( "e-v2" ), QStringLiteral( "exec-4" ) );
    configRequest.parameters = QJsonObject{ { QStringLiteral( "p" ), 2 } };
    const QString configReplay =
      startRun( recorder, configRequest, QStringLiteral( "digest-out" ) );
    const auto configDeviated = analyzer.analyze( original, configReplay );
    REQUIRE( configDeviated.has_value() );
    REQUIRE( configDeviated.value().verdict == ReplayDeviationReport::Verdict::Deviated );
    const ReplayDeviationEntry *configEntry =
      entryFor( configDeviated.value(), QStringLiteral( "config" ) );
    REQUIRE( configEntry != nullptr );
    CHECK( configEntry->domain == ReplayDeviationDomain::Experiment );

    // Environment field drift is its own domain (still an identity pin per
    // the comparison vocabulary).
    RunStartRequest envRequest = baseRequest( QStringLiteral( "e-v2" ), QStringLiteral( "exec-5" ) );
    envRequest.environment =
      RunEnvironment::fromFields( QJsonObject{ { QStringLiteral( "platform" ),
                                                 QStringLiteral( "other-host" ) } } );
    const QString envReplay =
      startRun( recorder, envRequest, QStringLiteral( "digest-out" ) );
    const auto envDeviated = analyzer.analyze( original, envReplay );
    REQUIRE( envDeviated.has_value() );
    const ReplayDeviationEntry *envEntry =
      entryFor( envDeviated.value(), QStringLiteral( "environment" ) );
    REQUIRE( envEntry != nullptr );
    CHECK( envEntry->domain == ReplayDeviationDomain::Environment );
}

TEST_CASE( "deviation v2 keeps v1 fields and verdict semantics intact",
           "[r25_b][replay_deviation]" )
{
    StoreFixture fixture;
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "e-compat" ) );
    experiment.setName( QStringLiteral( "e-compat" ) );
    REQUIRE( fixture.store.upsertExperiment( experiment ).has_value() );

    ExperimentRunRecorder recorder( fixture.store );
    const RunStartRequest base =
      baseRequest( QStringLiteral( "e-compat" ), QStringLiteral( "exec-1" ) );
    const QString original =
      startRun( recorder, base, QStringLiteral( "digest-out" ) );

    // Identical replay: no deviations in EITHER form.
    const RunStartRequest same =
      baseRequest( QStringLiteral( "e-compat" ), QStringLiteral( "exec-2" ) );
    const QString replay =
      startRun( recorder, same, QStringLiteral( "digest-out" ) );

    ReplayDeviationAnalyzer analyzer( fixture.store );
    const auto identical = analyzer.analyze( original, replay );
    REQUIRE( identical.has_value() );
    CHECK( identical.value().verdict == ReplayDeviationReport::Verdict::Identical );
    CHECK( identical.value().deviations.isEmpty() );
    CHECK( identical.value().deviationDetails.isEmpty() );

    // Deviated: v1 strings keep their exact historical spellings.
    RunStartRequest deviatedRequest =
      baseRequest( QStringLiteral( "e-compat" ), QStringLiteral( "exec-3" ) );
    deviatedRequest.modelDigest = QStringLiteral( "digest-other" );
    const QString deviatedRun =
      startRun( recorder, deviatedRequest, QStringLiteral( "digest-out" ) );
    const auto deviated = analyzer.analyze( original, deviatedRun );
    REQUIRE( deviated.has_value() );
    REQUIRE( deviated.value().verdict == ReplayDeviationReport::Verdict::Deviated );
    bool hasLegacySpelling = false;
    for ( const QString &line : deviated.value().deviations )
        hasLegacySpelling = hasLegacySpelling || line.startsWith( QStringLiteral( "pin 'model' differs:" ) );
    CHECK( hasLegacySpelling );
    // No metric deltas across different identities (v1 contract).
    CHECK( deviated.value().metricDelta.isEmpty() );

    // Schema v2 serialization: stamp + typed array, one JSON object per entry.
    const QJsonObject json = deviated.value().toJson();
    CHECK( json.value( QStringLiteral( "schema_version" ) ).toInt() == 2 );
    const QJsonArray details = json.value( QStringLiteral( "deviation_details" ) ).toArray();
    REQUIRE( details.size() == deviated.value().deviationDetails.size() );
    bool hasModelEntry = false;
    for ( const auto &value : details )
    {
        const QJsonObject entry = value.toObject();
        CHECK( entry.contains( QStringLiteral( "domain" ) ) );
        CHECK( entry.contains( QStringLiteral( "remediation" ) ) );
        hasModelEntry = hasModelEntry
                        || ( entry.value( QStringLiteral( "dimension" ) ).toString()
                             == QStringLiteral( "model" )
                             && entry.value( QStringLiteral( "domain" ) ).toString()
                                  == QStringLiteral( "model" ) );
    }
    CHECK( hasModelEntry );
    CHECK( json.contains( QStringLiteral( "deviations" ) ) ); // v1 field kept

    // Incomplete: unknown-run refusal unchanged (typed error).
    const auto unknown = analyzer.analyze( QStringLiteral( "nope" ), replay );
    REQUIRE( !unknown.has_value() );
    CHECK( unknown.diagnostics().first().code
           == QStringLiteral( "experiment.replay_unknown_run" ) );
}
