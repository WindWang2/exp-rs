// test_mlops9_evidence.cpp — Scientific MLOps 9.0: automatic scientific
// evidence (M4), experiment matrix (M5), replay deviation (M7) and the
// promotion seam (M8), all over a real ExperimentStore.
//
// Honesty assertions throughout: missing evidence is REPORTED (typed lists),
// never silently filled; incomparable cells never win pareto by absence;
// the approval trail is append-only.
#include <catch2/catch_test_macros.hpp>

#include "experiment/evidence.h"
#include "experiment/experiment_ids.h"
#include "experiment/experiment_matrix.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/promotion.h"
#include "experiment/replay_deviation.h"
#include "experiment/run_recorder.h"

#include "dataset/dataset_ids.h"

#include <QJsonDocument>
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

ExperimentRun recordedRun( const QString &runId, const QString &experimentId,
                           const QString &datasetVersionId, const QString &modelDigest,
                           RunStatus status, const QJsonObject &metrics,
                           const QVector<ExperimentRun::Artifact> &artifacts )
{
    ExperimentRun run;
    run.setRunId( runId );
    run.setExperimentId( experimentId );
    run.setStatus( status );
    run.setAlgorithmId( QStringLiteral( "workflow:under-test" ) );
    run.setAlgorithmVersion( QStringLiteral( "1" ) );
    run.setParameters( QJsonObject{ { QStringLiteral( "p" ), 1 } } );
    run.setDatasetVersionId( datasetVersionId );
    run.setDatasetFingerprint( QStringLiteral( "fp-ds" ) );
    run.setSplitManifestId( QStringLiteral( "split-1" ) );
    run.setSplitFingerprint( QStringLiteral( "fp-split" ) );
    run.setModelId( QStringLiteral( "model-a" ) );
    run.setModelDigest( modelDigest );
    run.setSeed( 42 );
    run.setEnvironment( RunEnvironment::fromFields(
        QJsonObject{ { QStringLiteral( "platform" ), QStringLiteral( "test" ) } } ) );
    run.setSoftwareRevision( QStringLiteral( "rev-test" ) );
    run.setExecutionRef( QStringLiteral( "exec-%1" ).arg( runId ) );
    run.setStartedAtUtc( QDateTime::fromString( QStringLiteral( "2026-09-11T10:00:00.000Z" ),
                                                Qt::ISODateWithMs ) );
    run.setFinishedAtUtc( QDateTime::fromString( QStringLiteral( "2026-09-11T10:05:00.000Z" ),
                                                 Qt::ISODateWithMs ) );
    run.artifacts() = artifacts;
    run.setMetrics( metrics );
    return run;
}

ExperimentRun::Artifact artifact( const QString &path, const QString &digest )
{
    ExperimentRun::Artifact artifact;
    artifact.path = path;
    artifact.role = QStringLiteral( "primary" );
    artifact.digest = digest;
    artifact.sizeBytes = 128;
    return artifact;
}

Result<QString> startRunChecked( ExperimentRunRecorder &recorder,
                                 const RunStartRequest &request )
{
    auto runId = recorder.startRun( request );
    if ( !runId.has_value() )
    {
        INFO( runId.diagnostics().first().code.toStdString() << ": "
              << runId.diagnostics().first().message.toStdString() );
        FAIL( "startRun failed" );
    }
    return runId;
}

void ensureExperiment( ExperimentStore &store, const QString &experimentId )
{
    Experiment experiment;
    experiment.setExperimentId( experimentId );
    experiment.setName( experimentId );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );
}

QStringList missingDimensions( const QJsonObject &completeness )
{
    QStringList names;
    for ( const QJsonValue &value :
          completeness.value( QStringLiteral( "dimensions" ) ).toArray() )
    {
        const QJsonObject item = value.toObject();
        if ( !item.value( QStringLiteral( "present" ) ).toBool() )
            names.append( item.value( QStringLiteral( "name" ) ).toString() );
    }
    return names;
}

RunStartRequest baseRequest( const QString &experimentId, const QString &executionRef )
{
    // Version ids are STRICTLY parsed (UUID form) by the recorder — a pin is
    // a store identity, not a label.
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

/// A valid protocol bound to the pinned dataset version (recordMetrics
/// validates the protocol against the same contract consumers read).
EvaluationProtocol pinnedProtocol()
{
    EvaluationProtocol protocol;
    protocol.setDatasetVersionId(
        baseRequest( QStringLiteral( "probe" ), QStringLiteral( "probe" ) )
            .datasetVersionId );
    protocol.setSplitManifestId( QStringLiteral( "split-1" ) );
    return protocol;
}

} // namespace

TEST_CASE( "evidence projector reports completeness honestly (M4)",
           "[mlops9][evidence]" )
{
    StoreFixture fixture;

    // A complete run: identity + environment + digested artifacts + metrics
    // record + step evidence + timing.
    REQUIRE( fixture.store.upsertExperiment( [] {
        Experiment experiment;
        experiment.setExperimentId( QStringLiteral( "e1" ) );
        experiment.setName( QStringLiteral( "evidence" ) );
        return experiment;
    }() )
                 .has_value() );

    ExperimentRun complete = recordedRun(
        QStringLiteral( "run-complete" ), QStringLiteral( "e1" ), QStringLiteral( "ds-1" ),
        QStringLiteral( "digest-1" ), RunStatus::Completed,
        QJsonObject{ { QStringLiteral( "workflow" ),
                       QJsonObject{ { QStringLiteral( "steps" ),
                                      QJsonArray{ QJsonObject{
                                          { QStringLiteral( "status" ),
                                            QStringLiteral( "Completed" ) } } } } } } },
        { artifact( QStringLiteral( "/tmp/out.tif" ), QStringLiteral( "aa" ) ) } );
    REQUIRE( fixture.store.upsertRun( complete ).has_value() );

    MetricRecord record;
    record.runId = complete.runId();
    record.metricsHash = QStringLiteral( "hash-metrics" );
    record.metrics = QJsonObject{ { QStringLiteral( "overall_accuracy" ), 0.9 } };
    auto summary = EvidenceProjector::summarize( { complete, record } );
    REQUIRE( summary.has_value() );
    const QJsonObject document = summary.value();
    CHECK( document.value( QStringLiteral( "schema_version" ) ).toInt() ==
           kEvidenceSchemaVersion );
    CHECK( document.value( QStringLiteral( "completeness" ) )
               .toObject()
               .value( QStringLiteral( "complete" ) )
               .toBool() );
    CHECK( document.value( QStringLiteral( "metrics" ) )
               .toObject()
               .value( QStringLiteral( "schema_version" ) )
               .toInt() == kMetricsSchemaVersion );

    // The same run WITHOUT digested artifacts and without a metric record:
    // the gaps are named, never papered over.
    ExperimentRun hollow = complete;
    hollow.setRunId( QStringLiteral( "run-hollow" ) );
    auto undigested = artifact( QStringLiteral( "/tmp/out.tif" ), QString() );
    hollow.artifacts() = QVector<ExperimentRun::Artifact>{ undigested };
    auto hollowSummary = EvidenceProjector::summarize( { hollow, std::nullopt } );
    REQUIRE( hollowSummary.has_value() );
    const QJsonObject completeness =
        hollowSummary.value().value( QStringLiteral( "completeness" ) ).toObject();
    CHECK( !completeness.value( QStringLiteral( "complete" ) ).toBool() );
    const QStringList missing = missingDimensions( completeness );
    CHECK( missing.contains( QStringLiteral( "metrics" ) ) );
    CHECK( missing.contains( QStringLiteral( "artifacts" ) ) );

    // An empty run id is a typed failure, not an empty document.
    auto refused = EvidenceProjector::summarize( { ExperimentRun{}, std::nullopt } );
    REQUIRE( !refused.has_value() );
    CHECK( refused.diagnostics().first().code ==
           QStringLiteral( "experiment.evidence_invalid" ) );
}

TEST_CASE( "experiment matrix: bounded descriptor, ledger, aggregation, pareto (M5)",
           "[mlops9][matrix]" )
{
    MatrixDescriptor descriptor;
    descriptor.matrixId = QStringLiteral( "matrix-1" );
    descriptor.experimentId = QStringLiteral( "e-matrix" );
    descriptor.workflowId = QStringLiteral( "workflow:under-test" );
    descriptor.name = QStringLiteral( "region × seed sweep" );
    descriptor.objective = QStringLiteral( "cross-region stability" );

    MatrixAxis region;
    region.name = QStringLiteral( "region" );
    region.role = AxisRole::Tag;
    region.values = { QStringLiteral( "eu" ), QStringLiteral( "asia" ) };
    MatrixAxis seed;
    seed.name = QStringLiteral( "seed" );
    seed.role = AxisRole::Seed;
    seed.values = { QStringLiteral( "1" ), QStringLiteral( "2" ) };
    MatrixAxis dataset;
    dataset.name = QStringLiteral( "dataset" );
    dataset.role = AxisRole::DatasetVersion;
    dataset.values = { QStringLiteral( "ds-version-1" ) };
    descriptor.axes = { region, seed, dataset };

    REQUIRE( descriptor.validate().has_value() );
    auto cells = descriptor.enumerateCells();
    REQUIRE( cells.has_value() );
    REQUIRE( cells.value().size() == 4 ); // 2 regions × 2 seeds × 1 dataset
    // Pin mapping: datasetVersion + seed land in the pins; tag does not.
    for ( const MatrixCell &cell : cells.value() )
    {
        CHECK( cell.pins.datasetVersionId == QStringLiteral( "ds-version-1" ) );
        CHECK( cell.pins.hasSeed );
    }
    // cellIds are content identity: re-enumeration reproduces them exactly.
    auto again = descriptor.enumerateCells();
    REQUIRE( again.has_value() );
    CHECK( again.value()[0].cellId == cells.value()[0].cellId );
    // Round-trip through JSON.
    const auto restored = MatrixCell::fromJson( cells.value()[0].toJson() );
    REQUIRE( restored.has_value() );
    CHECK( restored.value() == cells.value()[0] );

    // Bounds: product over the cap is a typed refusal.
    MatrixDescriptor huge = descriptor;
    MatrixAxis many;
    many.name = QStringLiteral( "many" );
    many.role = AxisRole::Tag;
    for ( int i = 0; i < 11; ++i )
        many.values.append( QStringLiteral( "v%1" ).arg( i ) );
    huge.axes.append( many ); // 4 × 11 = 44 ... add another for > 1000
    MatrixAxis more;
    more.name = QStringLiteral( "more" );
    more.role = AxisRole::Tag;
    for ( int i = 0; i < 30; ++i )
        more.values.append( QStringLiteral( "w%1" ).arg( i ) );
    huge.axes.append( more ); // 44 × 30 = 1320 > 1000
    auto refused = huge.enumerateCells();
    REQUIRE( !refused.has_value() );
    CHECK( refused.diagnostics().first().code ==
           QStringLiteral( "experiment.matrix_too_large" ) );

    // Ledger + aggregation over a REAL store.
    StoreFixture fixture;
    REQUIRE( fixture.store
                 .upsertExperiment( [] {
                     Experiment experiment;
                     experiment.setExperimentId( QStringLiteral( "e-matrix" ) );
                     experiment.setName( QStringLiteral( "matrix" ) );
                     return experiment;
                 }() )
                 .has_value() );

    const QString recordedCell = cells.value()[0].cellId;
    const QString failedCell = cells.value()[1].cellId;
    const QString missingCell = cells.value()[2].cellId; // never linked

    ExperimentRunRecorder recorder( fixture.store );
    auto recordedRunId = startRunChecked(
        recorder, baseRequest( QStringLiteral( "e-matrix" ),
                               QStringLiteral( "exec-cell0" ) ) );
    REQUIRE( recorder
                 .markSucceeded( recordedRunId.value(), {},
                                 QJsonObject{ { QStringLiteral( "overall_accuracy" ), 0.9 },
                                              { QStringLiteral( "kappa" ), 0.8 } } )
                 .has_value() );
    REQUIRE( recorder.recordMetrics(
                 recordedRunId.value(), pinnedProtocol(),
                 QJsonObject{ { QStringLiteral( "overall_accuracy" ), 0.9 },
                              { QStringLiteral( "kappa" ), 0.8 } } )
                 .has_value() );

    auto failedRunId =
        recorder.startRun( baseRequest( QStringLiteral( "e-matrix" ),
                                        QStringLiteral( "exec-cell1" ) ) );
    REQUIRE( failedRunId.has_value() );
    REQUIRE( recorder.markFailed( failedRunId.value(), QStringLiteral( "test" ),
                                  QStringLiteral( "boom" ) )
                 .has_value() );

    MatrixLedger ledger( fixture.store );
    REQUIRE( ledger.link( recordedCell, recordedRunId.value() ).has_value() );
    REQUIRE( ledger.link( failedCell, failedRunId.value() ).has_value() );

    // Metrics schema versioning rides recordMetrics.
    const auto persisted = fixture.store.metricRecordForRun( recordedRunId.value() );
    REQUIRE( persisted.has_value() );
    CHECK( persisted->metricsSchemaVersion == kMetricsSchemaVersion );

    MatrixAggregator aggregator( fixture.store, ledger );
    auto aggregate =
        aggregator.aggregate( descriptor, { QStringLiteral( "overall_accuracy" ),
                                            QStringLiteral( "kappa" ) } );
    REQUIRE( aggregate.has_value() );
    CHECK( aggregate.value().totalCells == 4 );
    CHECK( aggregate.value().recordedCells == 1 );
    CHECK( aggregate.value().failedCells == 1 );
    CHECK( aggregate.value().missingCells == 2 );

    const CellAggregate &recorded = aggregate.value().cells.first();
    REQUIRE( recorded.status == QStringLiteral( "recorded" ) );
    CHECK( recorded.metrics.value( QStringLiteral( "overall_accuracy" ) ).mean == 0.9 );
    CHECK( recorded.metrics.value( QStringLiteral( "overall_accuracy" ) ).runCount == 1 );

    // Pareto: only cells with EVERY objective metric recorded are candidates.
    const QStringList pareto = MatrixAggregator::paretoCellIds(
        aggregate.value(), { QStringLiteral( "overall_accuracy" ) } );
    CHECK( pareto == QStringList{ recordedCell } );
    // A metric nothing recorded ⇒ no candidates at all.
    CHECK( MatrixAggregator::paretoCellIds(
               aggregate.value(), { QStringLiteral( "nonexistent" ) } )
               .isEmpty() );
}

TEST_CASE( "replay deviation: identical, deviated and incomplete verdicts (M7)",
           "[mlops9][replay]" )
{
    StoreFixture fixture;
    ensureExperiment( fixture.store, QStringLiteral( "e-replay" ) );

    auto request = baseRequest( QStringLiteral( "e-replay" ), QStringLiteral( "exec-original" ) );
    ExperimentRunRecorder recorder( fixture.store );
    auto originalId = startRunChecked( recorder, request );
    const QVector<ExperimentRun::Artifact> artifacts = {
        artifact( QStringLiteral( "/tmp/out.tif" ), QStringLiteral( "digest-a" ) ) };
    const QJsonObject metrics{ { QStringLiteral( "overall_accuracy" ), 0.9 } };
    REQUIRE( recorder.markSucceeded( originalId.value(), artifacts, metrics ).has_value() );

    // A faithful replay: same pins, same outputs.
    auto replayRequest =
        baseRequest( QStringLiteral( "e-replay" ), QStringLiteral( "exec-replay" ) );
    auto replayId = startRunChecked( recorder, replayRequest );
    REQUIRE( recorder.markSucceeded( replayId.value(), artifacts, metrics ).has_value() );

    ReplayDeviationAnalyzer analyzer( fixture.store );
    auto identical = analyzer.analyze( originalId.value(), replayId.value() );
    REQUIRE( identical.has_value() );
    CHECK( identical.value().verdict == ReplayDeviationReport::Verdict::Identical );
    CHECK( identical.value().deviations.isEmpty() );

    // A deviated replay: different dataset version pin.
    auto deviatedRequest = baseRequest( QStringLiteral( "e-replay" ), QStringLiteral( "exec-x" ) );
    deviatedRequest.datasetVersionId = DatasetVersionId::generate().toString();
    auto deviatedId = startRunChecked( recorder, deviatedRequest );
    REQUIRE( recorder.markSucceeded( deviatedId.value(), artifacts, metrics ).has_value() );
    auto deviated = analyzer.analyze( originalId.value(), deviatedId.value() );
    REQUIRE( deviated.has_value() );
    CHECK( deviated.value().verdict == ReplayDeviationReport::Verdict::Deviated );
    CHECK( !deviated.value().deviations.isEmpty() );
    // No metric deltas across different identities — no apples-to-oranges.
    CHECK( deviated.value().metricDelta.isEmpty() );

    // Incomplete: the "replay" never completed.
    auto incompleteId = startRunChecked(
        recorder, baseRequest( QStringLiteral( "e-replay" ), QStringLiteral( "exec-y" ) ) );
    auto incomplete = analyzer.analyze( originalId.value(), incompleteId.value() );
    REQUIRE( incomplete.has_value() );
    CHECK( incomplete.value().verdict == ReplayDeviationReport::Verdict::Incomplete );
    CHECK( !incomplete.value().evidenceGaps.isEmpty() );

    // Unknown ids are typed errors.
    auto unknown = analyzer.analyze( QStringLiteral( "nope" ), replayId.value() );
    REQUIRE( !unknown.has_value() );
    CHECK( unknown.diagnostics().first().code ==
           QStringLiteral( "experiment.replay_unknown_run" ) );
}

TEST_CASE( "promotion seam: criteria, benchmark gap, approval metadata (M8)",
           "[mlops9][promotion]" )
{
    StoreFixture fixture;
    ensureExperiment( fixture.store, QStringLiteral( "e-promo" ) );

    ExperimentRunRecorder recorder( fixture.store );
    auto runId = startRunChecked(
        recorder, baseRequest( QStringLiteral( "e-promo" ), QStringLiteral( "exec-promo" ) ) );
    const QJsonObject metrics{ { QStringLiteral( "overall_accuracy" ), 0.92 } };
    REQUIRE( recorder.markSucceeded( runId.value(), {}, metrics ).has_value() );
    REQUIRE( recorder.recordMetrics( runId.value(), pinnedProtocol(), metrics ).has_value() );

    PromotionEvaluator evaluator( fixture.store );

    PromotionRequest request;
    request.runId = runId.value();
    request.modelId = QStringLiteral( "model-a" );
    request.modelDigest = QStringLiteral( "digest-1" );
    PromotionCriterion accuracy;
    accuracy.metric = QStringLiteral( "overall_accuracy" );
    accuracy.minValue = 0.9;
    request.criteria = { accuracy };
    const QString pinnedVersionId =
        baseRequest( QStringLiteral( "e-promo" ), QStringLiteral( "probe" ) )
            .datasetVersionId;
    request.benchmarkDatasetVersions = { pinnedVersionId };

    auto evaluation = evaluator.evaluate( request );
    REQUIRE( evaluation.has_value() );
    CHECK( evaluation.value().eligible );
    CHECK( evaluation.value().missingEvidence.isEmpty() );

    // Record approval metadata; the store keeps the evidence readable.
    auto promotionId = evaluator.record( request, evaluation.value(),
                                         QStringLiteral( "approved" ),
                                         QStringLiteral( "release-board" ) );
    REQUIRE( promotionId.has_value() );
    const auto persisted = fixture.store.promotionById( promotionId.value() );
    REQUIRE( persisted.has_value() );
    CHECK( persisted->verdict == QStringLiteral( "eligible" ) );
    CHECK( persisted->decision == QStringLiteral( "approved" ) );
    CHECK( persisted->decidedBy == QStringLiteral( "release-board" ) );
    CHECK( persisted->decidedAtUtc.isValid() );
    const auto forModel = fixture.store.promotionsForModel( QStringLiteral( "model-a" ) );
    REQUIRE( forModel.size() == 1 );
    CHECK( forModel.first().promotionId == promotionId.value() );

    // A criterion the run did not record is a named gap, never a pass.
    PromotionCriterion boundary;
    boundary.metric = QStringLiteral( "boundary_f1" );
    boundary.minValue = 0.5;
    request.criteria = { accuracy, boundary };
    auto gapEvaluation = evaluator.evaluate( request );
    REQUIRE( gapEvaluation.has_value() );
    CHECK( !gapEvaluation.value().eligible );
    CHECK( gapEvaluation.value().missingEvidence.contains(
        QStringLiteral( "metric:boundary_f1" ) ) );

    // Benchmark membership: evidence on another dataset cannot vouch for the
    // benchmark set.
    PromotionRequest offBenchmark = request;
    offBenchmark.criteria = { accuracy };
    offBenchmark.benchmarkDatasetVersions = { QStringLiteral( "ds-version-99" ) };
    auto benchmarkGap = evaluator.evaluate( offBenchmark );
    REQUIRE( benchmarkGap.has_value() );
    CHECK( benchmarkGap.value().missingEvidence.contains(
        QStringLiteral( "benchmark_set" ) ) );

    // The approval trail is append-only: same id, different content ⇒ conflict.
    PromotionRecord tampered = persisted.value();
    tampered.decision = QStringLiteral( "rejected" );
    auto conflicted = fixture.store.savePromotionRecord( tampered );
    REQUIRE( !conflicted.has_value() );
    CHECK( conflicted.diagnostics().first().code ==
           QStringLiteral( "experiment.promotion_conflict" ) );

    // A failed run is never promotion evidence.
    auto failedId = startRunChecked(
        recorder, baseRequest( QStringLiteral( "e-promo" ), QStringLiteral( "exec-failed" ) ) );
    REQUIRE( recorder.markFailed( failedId.value(), QStringLiteral( "x" ), QStringLiteral( "y" ) )
                 .has_value() );
    PromotionRequest failedRequest;
    failedRequest.runId = failedId.value();
    failedRequest.criteria = { accuracy };
    auto failedEvaluation = evaluator.evaluate( failedRequest );
    REQUIRE( failedEvaluation.has_value() );
    CHECK( !failedEvaluation.value().eligible );
    CHECK( failedEvaluation.value().missingEvidence.contains(
        QStringLiteral( "completed_status" ) ) );
}
