// test_experiment_evaluation.cpp — Foundation 5.0 experiment/evaluation
// tests (goal §23/§25/§26/§27/§29/§51, ADR 0137/0138): canonical config
// identity, truthful run status transitions, known-answer metric values
// (fixed confusion matrix → exact numbers), regression metrics with an
// explicit zero-denominator policy, boundary F-score, detection AP,
// protocol binding, comparability-first comparison, environment secret
// filtering, and the reproduction bundle/validator.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_store.h"
#include "dataset/split.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/lineage.h"
#include "experiment/reproduction_bundle.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>
#include <cmath>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace
{

ExperimentRun makeRun( const QString &runId, const QString &experimentId )
{
    ExperimentRun run;
    run.setRunId( runId );
    run.setExperimentId( experimentId );
    run.setAlgorithmId( QStringLiteral( "rs:classify" ) );
    run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
    QJsonObject parameters;
    parameters.insert( QStringLiteral( "bands" ), QJsonArray{ 1, 2, 3 } );
    parameters.insert( QStringLiteral( "model" ), QStringLiteral( "rf" ) );
    run.setParameters( parameters );
    run.setDatasetVersionId( QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
    run.setDatasetFingerprint( QStringLiteral( "df1" ) );
    run.setSplitManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    run.setSplitFingerprint( QStringLiteral( "sf1" ) );
    run.setSeed( 42 );
    return run;
}

} // namespace

TEST_CASE( "canonical config hash ignores key order and formatting",
           "[experiment][identity]" )
{
    QJsonObject a;
    a.insert( QStringLiteral( "a" ), 1 );
    a.insert( QStringLiteral( "b" ), 2 );
    QJsonObject b;
    b.insert( QStringLiteral( "b" ), 2 );
    b.insert( QStringLiteral( "a" ), 1 );
    CHECK( runConfigHash( a ) == runConfigHash( b ) );
    CHECK( runConfigHash( a ).size() == 64 );

    // Semantically different configs never collide.
    QJsonObject c = a;
    c.insert( QStringLiteral( "a" ), 2 );
    CHECK( runConfigHash( c ) != runConfigHash( a ) );
}

TEST_CASE( "three run hashes have three distinct meanings", "[experiment][identity]" )
{
    ExperimentRun run = makeRun( QStringLiteral( "r1" ), QStringLiteral( "e1" ) );
    RunExecutionIdentity identity = run.executionIdentity();

    // Same execution identity → same fingerprint; any pin change → new one.
    CHECK( runExecutionFingerprint( identity ) == runExecutionFingerprint( identity ) );
    RunExecutionIdentity changedSeed = identity;
    changedSeed.seed = 43;
    CHECK( runExecutionFingerprint( changedSeed ) != runExecutionFingerprint( identity ) );
    RunExecutionIdentity changedDataset = identity;
    changedDataset.datasetFingerprint = QStringLiteral( "df2" );
    CHECK( runExecutionFingerprint( changedDataset ) != runExecutionFingerprint( identity ) );

    // Result fingerprint covers outputs+metrics only.
    const QString result1 = runResultFingerprint( { QStringLiteral( "d1" ) },
                                                  QJsonObject{ { QStringLiteral( "acc" ), 0.9 } } );
    const QString result2 = runResultFingerprint( { QStringLiteral( "d1" ) },
                                                  QJsonObject{ { QStringLiteral( "acc" ), 0.9 } } );
    const QString result3 = runResultFingerprint( { QStringLiteral( "d2" ) },
                                                  QJsonObject{ { QStringLiteral( "acc" ), 0.9 } } );
    CHECK( result1 == result2 );
    CHECK( result1 != result3 );
    // Config hash is NOT the execution fingerprint even for identical params.
    CHECK( runConfigHash( run.parameters() ) != runExecutionFingerprint( identity ) );
}

TEST_CASE( "run status transitions are truthful and terminal states hold",
           "[experiment][lifecycle]" )
{
    CHECK( isValidRunTransition( RunStatus::Created, RunStatus::Running ) );
    CHECK( isValidRunTransition( RunStatus::Created, RunStatus::Cancelled ) );
    CHECK( isValidRunTransition( RunStatus::Running, RunStatus::Completed ) );
    CHECK( isValidRunTransition( RunStatus::Running, RunStatus::Interrupted ) );
    CHECK( isValidRunTransition( RunStatus::Interrupted, RunStatus::Running ) );
    CHECK( isValidRunTransition( RunStatus::Cancelling, RunStatus::Cancelled ) );
    // Fabrications are refused.
    CHECK( !isValidRunTransition( RunStatus::Created, RunStatus::Completed ) );
    CHECK( !isValidRunTransition( RunStatus::Failed, RunStatus::Running ) );
    CHECK( !isValidRunTransition( RunStatus::Completed, RunStatus::Running ) );
    CHECK( isTerminalRunStatus( RunStatus::Failed ) );
    CHECK( isTerminalRunStatus( RunStatus::Completed ) );
    CHECK( !isTerminalRunStatus( RunStatus::Interrupted ) );
}

TEST_CASE( "known confusion matrix yields exact metric values", "[experiment][metrics]" )
{
    // 3-class matrix (rows = truth, cols = predicted):
    //            water  urban  veg
    // water       10      5      0
    // urban        2     60      3
    // veg          0      5     15
    ConfusionMatrix matrix( { QStringLiteral( "water" ), QStringLiteral( "urban" ),
                              QStringLiteral( "veg" ) },
                            3 );
    const qint64 counts[3][3] = { { 10, 5, 0 }, { 2, 60, 3 }, { 0, 5, 15 } };
    for ( int r = 0; r < 3; ++r )
        for ( int c = 0; c < 3; ++c )
            matrix.setCount( r, c, counts[r][c] );

    CHECK( matrix.total() == 100 );
    // Water (row 0): TP=10, predicted total = col0 = 10+2+0 = 12, truth
    // total = row0 = 15 → precision 10/12, recall 10/15,
    // F1 = 2PR/(P+R), IoU = 10/(12+15-10) = 10/17.
    const auto water = matrix.perClass( 0 );
    CHECK( std::abs( water.precision - 10.0 / 12.0 ) < 1e-12 );
    CHECK( std::abs( water.recall - 10.0 / 15.0 ) < 1e-12 );
    const double waterF1 = 2.0 * ( 10.0 / 12.0 ) * ( 10.0 / 15.0 ) /
                           ( ( 10.0 / 12.0 ) + ( 10.0 / 15.0 ) );
    CHECK( std::abs( water.f1 - waterF1 ) < 1e-12 );
    CHECK( std::abs( water.iou - 10.0 / 17.0 ) < 1e-12 );
    CHECK( water.support == 15 );
    // Urban (row 1): precision 60/70, recall 60/65.
    const auto urban = matrix.perClass( 1 );
    CHECK( std::abs( urban.precision - 60.0 / 70.0 ) < 1e-12 );
    CHECK( std::abs( urban.recall - 60.0 / 65.0 ) < 1e-12 );
    // Overall accuracy = 85/100.
    CHECK( std::abs( matrix.overallAccuracy() - 0.85 ) < 1e-12 );
    // Balanced accuracy = mean recall = (10/15 + 60/65 + 15/20) / 3.
    const double balanced = ( 10.0 / 15.0 + 60.0 / 65.0 + 15.0 / 20.0 ) / 3.0;
    CHECK( std::abs( matrix.balancedAccuracy() - balanced ) < 1e-12 );
    // Kappa: pe = (T0*P0 + T1*P1 + T2*P2)/100^2 with truth totals
    // (15, 65, 20) and predicted totals (12, 70, 18); observed 0.85.
    const double pe = ( 15.0 * 12.0 + 65.0 * 70.0 + 20.0 * 18.0 ) / 10000.0;
    CHECK( std::abs( matrix.kappa() - ( 0.85 - pe ) / ( 1.0 - pe ) ) < 1e-12 );
    // Macro IoU: water 10/17, urban 60/75, veg 15/23.
    const double macroIou = ( 10.0 / 17.0 + 60.0 / 75.0 + 15.0 / 23.0 ) / 3.0;
    CHECK( std::abs( matrix.macroIoU() - macroIou ) < 1e-12 );

    // MCC pinned on the exact 2x2 perfect-diagonal case (MCC = 1).
    ConfusionMatrix diagonal( { QStringLiteral( "a" ), QStringLiteral( "b" ) }, 2 );
    diagonal.setCount( 0, 0, 50 );
    diagonal.setCount( 1, 1, 50 );
    CHECK( std::abs( diagonal.mcc() - 1.0 ) < 1e-12 );
    // 3x3 MCC is finite and within [-1, 1].
    CHECK( std::isfinite( matrix.mcc() ) );
    CHECK( matrix.mcc() >= -1.0 );
    CHECK( matrix.mcc() <= 1.0 );

    // Round-trip preserves the matrix exactly.
    const auto parsed = ConfusionMatrix::fromJson( matrix.toJson() );
    REQUIRE( parsed.has_value() );
    CHECK( parsed.value() == matrix );
}

TEST_CASE( "regression metrics follow the explicit zero policy",
           "[experiment][metrics]" )
{
    const QVector<double> truth{ 1.0, 2.0, 0.0, 4.0 };
    const QVector<double> predicted{ 1.5, 2.0, 3.0, 3.0 };
    const auto metrics = regressionMetrics( truth, predicted );
    REQUIRE( metrics.has_value() );
    // MAE = (0.5+0+3+1)/4 = 1.125; MSE = (0.25+0+9+1)/4 = 2.5625; RMSE = 1.6…
    CHECK( std::abs( metrics->mae - 1.125 ) < 1e-12 );
    CHECK( std::abs( metrics->mse - 2.5625 ) < 1e-12 );
    CHECK( std::abs( metrics->rmse - std::sqrt( 2.5625 ) ) < 1e-12 );
    CHECK( std::abs( metrics->bias - 0.625 ) < 1e-12 );
    // MAPE skips the zero-truth row: mean(|d/t|) over 3 rows ×100.
    const double mape = 100.0 * ( ( 0.5 / 1.0 ) + 0.0 + ( 1.0 / 4.0 ) ) / 3.0;
    CHECK( std::abs( metrics->mape - mape ) < 1e-12 );
    CHECK( metrics->mapeSkippedZeros == 1 );
    // R² against a constant truth series is 0 by policy.
    const auto degenerate = regressionMetrics( { 2.0, 2.0 }, { 1.0, 5.0 } );
    REQUIRE( degenerate.has_value() );
    CHECK( degenerate->r2 == 0.0 );
    // Length mismatch is a loud failure.
    CHECK( !regressionMetrics( { 1.0 }, {} ).has_value() );
}

TEST_CASE( "boundary F-score and detection AP behave on known inputs",
           "[experiment][metrics]" )
{
    // Width 10 image; vertical boundary at column 4 (indices 4,14,24…).
    QVector<qint64> truth;
    for ( qint64 y = 0; y < 5; ++y )
        truth.append( y * 10 + 4 );
    // Identical prediction → perfect score.
    const auto perfect = boundaryFScore( truth, truth, 10, 1.0 );
    REQUIRE( perfect.has_value() );
    CHECK( perfect.value() > 0.999 );
    // Prediction shifted by one column with tolerance 1.5 still matches.
    QVector<qint64> shifted;
    for ( qint64 y = 0; y < 5; ++y )
        shifted.append( y * 10 + 5 );
    const auto within = boundaryFScore( truth, shifted, 10, 1.5 );
    REQUIRE( within.has_value() );
    CHECK( within.value() > 0.999 );
    // Far prediction does not match.
    QVector<qint64> far;
    for ( qint64 y = 0; y < 5; ++y )
        far.append( y * 10 + 9 );
    const auto missed = boundaryFScore( truth, far, 10, 1.5 );
    REQUIRE( missed.has_value() );
    CHECK( missed.value() < 1e-9 );
    // Both empty = identical boundary-less masks.
    CHECK( boundaryFScore( {}, {}, 10, 1.0 ).value() == 1.0 );

    // AP: 3 ground truths, 4 detections ranked by confidence where ranks
    // 1,2,4 are TPs → precision sum = 1/1 + 2/2 + 3/4 = 2.75 → AP = 2.75/3.
    DetectionBox d1;
    d1.confidence = 0.9;
    d1.isTruePositive = true;
    DetectionBox d2;
    d2.confidence = 0.8;
    d2.isTruePositive = true;
    DetectionBox d3;
    d3.confidence = 0.7;
    d3.isTruePositive = false;
    DetectionBox d4;
    d4.confidence = 0.6;
    d4.isTruePositive = true;
    const double ap = averagePrecision( { d1, d2, d3, d4 }, 3 );
    CHECK( std::abs( ap - 2.75 / 3.0 ) < 1e-12 );
    // IoU sanity: identical boxes = 1; disjoint = 0.
    DetectionBox a{ 0, 0, 10, 10, 1.0, false };
    DetectionBox b{ 0, 0, 10, 10, 1.0, false };
    CHECK( std::abs( a.iouWith( b ) - 1.0 ) < 1e-12 );
    DetectionBox c{ 100, 100, 10, 10, 1.0, false };
    CHECK( a.iouWith( c ) == 0.0 );
}

TEST_CASE( "evaluation protocol is validated and binds metric identity",
           "[experiment][protocol]" )
{
    EvaluationProtocol protocol;
    protocol.setDatasetVersionId( QStringLiteral( "v1" ) );
    protocol.setSplitManifestId( QStringLiteral( "sp1" ) );
    protocol.setSubset( QStringLiteral( "test" ) );
    protocol.ignoreLabels().append( QStringLiteral( "background" ) );
    REQUIRE( protocol.validate().has_value() );

    const auto parsed = EvaluationProtocol::fromJson( protocol.toJson() );
    REQUIRE( parsed.has_value() );
    CHECK( parsed.value() == protocol );

    // Invalid thresholds refused.
    EvaluationProtocol bad = protocol;
    bad.setIouThreshold( 0.0 );
    CHECK( !bad.validate().has_value() );
    EvaluationProtocol badAggregation = protocol;
    badAggregation.setAggregation( QStringLiteral( "average-ish" ) );
    CHECK( !badAggregation.validate().has_value() );

    // The protocol is part of a MetricRecord and survives round-trip.
    MetricRecord record;
    record.runId = QStringLiteral( "r1" );
    record.protocol = protocol;
    record.metrics = QJsonObject{ { QStringLiteral( "overall_accuracy" ), 0.85 } };
    record.metricsHash = QStringLiteral( "mh" );
    const auto parsedRecord = MetricRecord::fromJson( record.toJson() );
    REQUIRE( parsedRecord.has_value() );
    CHECK( parsedRecord->protocol == protocol );
}

TEST_CASE( "comparison answers comparability before metrics",
           "[experiment][comparison]" )
{
    ExperimentRun a = makeRun( QStringLiteral( "r1" ), QStringLiteral( "e1" ) );
    ExperimentRun b = makeRun( QStringLiteral( "r2" ), QStringLiteral( "e1" ) );

    // Identical pins → Comparable.
    auto comparison = RunComparison::compare( a, b );
    CHECK( comparison.verdict == RunComparison::Verdict::Comparable );
    // 9.0 added the artifacts + runtime diagnostic dimensions to the
    // identity/config/seed/environment seven.
    CHECK( comparison.dimensions.size() == 9 );

    // Config-only difference → ComparableWithDifferences (the interesting
    // scientific case: same data/split/model, different parameters).
    QJsonObject parameters = b.parameters();
    parameters.insert( QStringLiteral( "model" ), QStringLiteral( "svm" ) );
    b.setParameters( parameters );
    comparison = RunComparison::compare( a, b );
    CHECK( comparison.verdict == RunComparison::Verdict::ComparableWithDifferences );

    // Dataset pin difference → NotComparable, no metric averaging excuse.
    b.setDatasetFingerprint( QStringLiteral( "df-other" ) );
    comparison = RunComparison::compare( a, b );
    CHECK( comparison.verdict == RunComparison::Verdict::NotComparable );
    CHECK( std::any_of( comparison.reasons.cbegin(), comparison.reasons.cend(),
                        []( const QString &reason ) {
                            return reason.contains( QLatin1String( "dataset" ) );
                        } ) );
    // The verdict + diffs round-trip.
    const auto parsed = RunComparison::fromJson( comparison.toJson() );
    CHECK( parsed.verdict == comparison.verdict );
    CHECK( parsed.dimensions.size() == comparison.dimensions.size() );
}

TEST_CASE( "environment capture filters secrets by name and value shape",
           "[experiment][environment]" )
{
    QHash<QString, QString> variables;
    variables.insert( QStringLiteral( "SICNU_EXECUTION_CACHE" ), QStringLiteral( "1" ) );
    variables.insert( QStringLiteral( "SICNU_API_KEY" ), QStringLiteral( "sk-abcdefghij0123456789" ) );
    variables.insert( QStringLiteral( "MY_TOKEN" ), QStringLiteral( "ghp_abcdefghij0123456789abcdefghij0123456789" ) );
    variables.insert( QStringLiteral( "PASSWORD" ), QStringLiteral( "hunter2" ) );
    variables.insert( QStringLiteral( "AUTH_HEADER" ), QStringLiteral( "Bearer abc" ) );
    variables.insert( QStringLiteral( "AWS_KEY" ), QStringLiteral( "AKIAABCDEFGHIJKLMNOP" ) );
    variables.insert( QStringLiteral( "PRIVATE_PEM" ), QStringLiteral( "-----BEGIN PRIVATE KEY-----" ) );
    variables.insert( QStringLiteral( "SESSION_COOKIE" ), QStringLiteral( "sid=1" ) );
    variables.insert( QStringLiteral( "INNOCENT" ), QStringLiteral( "plain value" ) );
    // A secret smuggled under an innocent NAME is caught by value shape.
    variables.insert( QStringLiteral( "CONFIG" ), QStringLiteral( "token=xoxb-123456" ) );

    const auto filtered = RunEnvironment::filterSecrets( variables );
    CHECK( filtered.contains( QStringLiteral( "SICNU_EXECUTION_CACHE" ) ) );
    CHECK( filtered.contains( QStringLiteral( "INNOCENT" ) ) );
    for ( const QString &suspicious :
          { QStringLiteral( "SICNU_API_KEY" ), QStringLiteral( "MY_TOKEN" ),
            QStringLiteral( "PASSWORD" ), QStringLiteral( "AUTH_HEADER" ),
            QStringLiteral( "AWS_KEY" ), QStringLiteral( "PRIVATE_PEM" ),
            QStringLiteral( "SESSION_COOKIE" ), QStringLiteral( "CONFIG" ) } )
    {
        INFO( suspicious.toStdString() );
        CHECK( !filtered.contains( suspicious ) );
    }

    // The reconstruction path applies the same filter (defense-in-depth).
    auto environment = RunEnvironment::fromFields( QJsonObject(), variables );
    CHECK( !environment.envVariables().contains( QStringLiteral( "SICNU_API_KEY" ) ) );
    CHECK( environment.envVariables().contains( QStringLiteral( "INNOCENT" ) ) );
    const auto roundTripped = RunEnvironment::fromJson( environment.toJson() );
    REQUIRE( roundTripped.has_value() );
    CHECK( roundTripped.value() == environment );
}

TEST_CASE( "experiment store enforces transitions, identity and conflicts",
           "[experiment][store]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    Experiment experiment;
    experiment.setExperimentId( ExperimentId::generate().toString() );
    experiment.setName( QStringLiteral( "RF vs SVM on landcover" ) );
    experiment.setObjective( QStringLiteral( "Which classifier generalizes better?" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );
    // Differing re-upsert is a conflict.
    Experiment changed = experiment;
    changed.setObjective( QStringLiteral( "Rewritten" ) );
    auto conflict = store.upsertExperiment( changed );
    CHECK( !conflict.has_value() );

    // Run lifecycle through the store.
    ExperimentRun run = makeRun( QStringLiteral( "run-1" ), experiment.experimentId() );
    run.setStatus( RunStatus::Created );
    REQUIRE( store.upsertRun( run ).has_value() );
    run.setStatus( RunStatus::Running );
    REQUIRE( store.upsertRun( run ).has_value() );

    // Fabricated completion is refused.
    ExperimentRun fabricated = run;
    fabricated.setStatus( RunStatus::Created );
    fabricated.setRunId( QStringLiteral( "run-2" ) );
    REQUIRE( store.upsertRun( fabricated ).has_value() );
    ExperimentRun fakeComplete = fabricated;
    fakeComplete.setStatus( RunStatus::Completed );
    CHECK( !store.upsertRun( fakeComplete ).has_value() );

    // Identity pins immutable after start.
    ExperimentRun changedPin = run;
    changedPin.setSeed( 43 );
    CHECK( !store.upsertRun( changedPin ).has_value() );

    // Legitimate completion persists; terminal state is final.
    run.setStatus( RunStatus::Completed );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( store.upsertRun( run ).has_value() );
    ExperimentRun reopen = run;
    reopen.setStatus( RunStatus::Running );
    CHECK( !store.upsertRun( reopen ).has_value() );

    // Queries.
    const auto loaded = store.runById( QStringLiteral( "run-1" ) );
    REQUIRE( loaded.has_value() );
    CHECK( loaded->status() == RunStatus::Completed );
    CHECK( loaded->executionIdentity() == run.executionIdentity() );
    const auto page = store.listRuns( experiment.experimentId() );
    REQUIRE( page.has_value() );
    CHECK( page.value().first == 2 );
    const auto byStatus = store.listRuns( QString(), QString(),
                                          runStatusToString( RunStatus::Completed ) );
    REQUIRE( byStatus.has_value() );
    CHECK( byStatus.value().first == 1 );

    // Metrics: save once, conflicting content refused.
    MetricRecord record;
    record.runId = QStringLiteral( "run-1" );
    record.protocol.setDatasetVersionId( run.datasetVersionId() );
    record.protocol.setSplitManifestId( run.splitManifestId() );
    record.metrics = QJsonObject{ { QStringLiteral( "overall_accuracy" ), 0.85 } };
    REQUIRE( store.saveMetricRecord( record ).has_value() );
    MetricRecord conflicting = record;
    conflicting.metrics = QJsonObject{ { QStringLiteral( "overall_accuracy" ), 0.99 } };
    CHECK( !store.saveMetricRecord( conflicting ).has_value() );
    CHECK( store.metricRecordForRun( QStringLiteral( "run-1" ) ).has_value() );

    // Experiments with runs are not deletable.
    CHECK( !store.deleteExperiment( experiment.experimentId() ).has_value() );
}

TEST_CASE( "lineage traverses ancestors/descendants, cuts cycles, flags tombs",
           "[experiment][lineage]" )
{
    QTemporaryDir dir;
    DatasetStore datasets;
    ExperimentStore experiments;
    REQUIRE( datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
    REQUIRE( experiments.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    // asset-1 → dataset_version-1 → run-1 → metric-1 (via experiment edges).
    REQUIRE( datasets.addLineageEdge( QStringLiteral( "asset" ), QStringLiteral( "asset-1" ),
                                      QStringLiteral( "derived_from" ),
                                      QStringLiteral( "dataset_version" ),
                                      QStringLiteral( "version-1" ) )
                 .has_value() );
    REQUIRE( experiments.addLineageEdge( QStringLiteral( "dataset_version" ),
                                         QStringLiteral( "version-1" ),
                                         QStringLiteral( "evaluated_on" ),
                                         QStringLiteral( "run" ), QStringLiteral( "run-1" ) )
                 .has_value() );
    REQUIRE( experiments.addLineageEdge( QStringLiteral( "run" ), QStringLiteral( "run-1" ),
                                         QStringLiteral( "produced" ),
                                         QStringLiteral( "metric" ), QStringLiteral( "metric-1" ) )
                 .has_value() );

    LineageGraph graph( datasets, experiments );
    graph.setExistenceResolver( []( const LineageNodeId &node ) {
        // Everything exists except the dangling metric.
        return node.id != QLatin1String( "metric-1" );
    } );

    // Upstream from the run: dataset_version + asset.
    const auto ancestors = graph.ancestors( LineageNodeId{ QStringLiteral( "run" ), QStringLiteral( "run-1" ) } );
    CHECK( ancestors.nodes.size() == 2 );
    CHECK( std::any_of( ancestors.nodes.cbegin(), ancestors.nodes.cend(),
                        []( const LineageNode &node ) {
                            return node.id.id == QLatin1String( "asset-1" ) && !node.dangling;
                        } ) );

    // Downstream from the run: metric marked dangling by the resolver.
    const auto descendants = graph.descendants( LineageNodeId{ QStringLiteral( "run" ), QStringLiteral( "run-1" ) } );
    REQUIRE( descendants.nodes.size() == 1 );
    CHECK( descendants.nodes.first().dangling );

    // A cycle (run-1 → run-1 via a weird edge) must not hang.
    LineageGraph cyclic( datasets, experiments );
    cyclic.addEdge( LineageNodeId{ QStringLiteral( "run" ), QStringLiteral( "x" ) },
                    QStringLiteral( "loops" ),
                    LineageNodeId{ QStringLiteral( "run" ), QStringLiteral( "x" ) } );
    const auto looped = cyclic.descendants( LineageNodeId{ QStringLiteral( "run" ), QStringLiteral( "x" ) } );
    CHECK( looped.nodes.isEmpty() ); // self-edge visits nothing new

    // Node budget enforced.
    const auto bounded =
        graph.ancestors( LineageNodeId{ QStringLiteral( "run" ), QStringLiteral( "run-1" ) }, 16, 1 );
    CHECK( bounded.budgetExhausted );
}

TEST_CASE( "reproduction bundle exports complete files and validates",
           "[experiment][repro]" )
{
    QTemporaryDir dir;
    DatasetStore datasets;
    ExperimentStore experiments;
    REQUIRE( datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
    REQUIRE( experiments.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    // A real dataset version so the pin resolves.
    const DatasetId datasetId = DatasetId::generate();
    REQUIRE( datasets.createDataset( datasetId, QStringLiteral( "lc" ) ).has_value() );
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId.toString() );
    manifest.setVersionId( DatasetVersionId::generate().toString() );
    const auto draft = datasets.createDraftVersion( manifest );
    REQUIRE( draft.has_value() );
    REQUIRE( datasets.stageVersion(
                 DatasetVersionId::fromString( draft->versionId() ).value_or( DatasetVersionId{} ) )
                 .has_value() );
    const auto committed = datasets.commitVersion(
        DatasetVersionId::fromString( draft->versionId() ).value_or( DatasetVersionId{} ) );
    REQUIRE( committed.has_value() );

    // The owning experiment must exist before its runs.
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-9" ) );
    experiment.setName( QStringLiteral( "repro" ) );
    REQUIRE( experiments.upsertExperiment( experiment ).has_value() );

    // A completed strict run bound to that version.
    ExperimentRun run = makeRun( QStringLiteral( "run-9" ), QStringLiteral( "exp-9" ) );
    run.setDatasetVersionId( committed->versionId() );
    run.setDatasetFingerprint( committed->fingerprint() );
    run.setStatus( RunStatus::Created );
    REQUIRE( experiments.upsertRun( run ).has_value() );
    run.setStatus( RunStatus::Running );
    REQUIRE( experiments.upsertRun( run ).has_value() );
    run.setStatus( RunStatus::Completed );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( experiments.upsertRun( run ).has_value() );

    ReproductionBundleExporter exporter( experiments, datasets );
    ReproductionBundleOptions options;
    options.outputDir = dir.filePath( QStringLiteral( "bundle" ) );
    options.currentSoftwareRevision = QStringLiteral( "test" );
    const auto report = exporter.exportRun( QStringLiteral( "run-9" ), options );
    if ( !report.ok )
    {
        for ( const QString &warning : report.warnings )
            WARN( warning.toStdString() );
    }
    REQUIRE( report.ok );
    for ( const QString &name :
          { QStringLiteral( "manifest.json" ), QStringLiteral( "dataset_refs.json" ),
            QStringLiteral( "run_config.json" ), QStringLiteral( "environment.json" ),
            QStringLiteral( "software.json" ), QStringLiteral( "model_refs.json" ),
            QStringLiteral( "metrics.json" ), QStringLiteral( "provenance.json" ),
            QStringLiteral( "split.json" ), QStringLiteral( "README.md" ),
            QStringLiteral( "checksums.txt" ) } )
    {
        INFO( name.toStdString() );
        CHECK( QFile::exists( QDir( report.bundlePath ).filePath( name ) ) );
    }

    // Validation: dataset pin resolves + fingerprint matches + no model hook.
    ReproductionHooks hooks;
    const auto validation = exporter.validateBundle( report.bundlePath, hooks );
    for ( const QString &reason : validation.reasons )
        std::cerr << "REASON: " << reason.toStdString() << std::endl;
    CHECK( validation.level != ReproductionLevel::Impossible );
    CHECK( validation.reasons.size() >= 2 );

    // A missing bundle is Impossible, loudly.
    const auto missing = exporter.validateBundle( dir.filePath( QStringLiteral( "nope" ) ), hooks );
    CHECK( missing.level == ReproductionLevel::Impossible );
}

TEST_CASE( "reproduction bundle filters secrets in environment.json at export boundary",
           "[experiment][bundle][issue789]" )
{
    QTemporaryDir dir;
    DatasetStore datasets;
    ExperimentStore experiments;
    REQUIRE( datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
    REQUIRE( experiments.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-sec" ) );
    experiment.setName( QStringLiteral( "Secret test" ) );
    REQUIRE( experiments.upsertExperiment( experiment ).has_value() );

    QHash<QString, QString> rawEnv;
    rawEnv.insert( QStringLiteral( "PATH" ), QStringLiteral( "/usr/bin:/bin" ) );
    rawEnv.insert( QStringLiteral( "SICNU_API_KEY" ), QStringLiteral( "super_secret_123" ) );
    rawEnv.insert( QStringLiteral( "AWS_SECRET_ACCESS_KEY" ), QStringLiteral( "secret_aws_key" ) );

    ExperimentRun run;
    run.setRunId( QStringLiteral( "run-sec" ) );
    run.setExperimentId( experiment.experimentId() );
    run.setStatus( RunStatus::Completed );
    run.setEnvironment( RunEnvironment::fromFields( QJsonObject{}, rawEnv ) );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( experiments.upsertRun( run ).has_value() );

    ReproductionBundleExporter exporter( experiments, datasets );
    ReproductionBundleOptions options;
    options.outputDir = dir.filePath( QStringLiteral( "bundle_sec" ) );
    options.currentSoftwareRevision = QStringLiteral( "rev1" );
    const auto report = exporter.exportRun( QStringLiteral( "run-sec" ), options );
    REQUIRE( report.ok );

    const QString envJsonPath = QDir( report.bundlePath ).filePath( QStringLiteral( "environment.json" ) );
    REQUIRE( QFile::exists( envJsonPath ) );

    QFile envFile( envJsonPath );
    REQUIRE( envFile.open( QIODevice::ReadOnly ) );
    const QJsonDocument doc = QJsonDocument::fromJson( envFile.readAll() );
    REQUIRE( doc.isObject() );
    const QJsonObject envVars = doc.object().value( QStringLiteral( "env_variables" ) ).toObject();

    CHECK( envVars.contains( QStringLiteral( "PATH" ) ) );
    CHECK_FALSE( envVars.contains( QStringLiteral( "SICNU_API_KEY" ) ) );
    CHECK_FALSE( envVars.contains( QStringLiteral( "AWS_SECRET_ACCESS_KEY" ) ) );
}

TEST_CASE( "ExperimentStore upsertRun validation runs inside transaction",
           "[experiment][store][issue811]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "exp.db" ) ) ) );

    Experiment exp;
    exp.setExperimentId( QStringLiteral( "exp-tx" ) );
    exp.setName( QStringLiteral( "Transaction test" ) );
    REQUIRE( store.upsertExperiment( exp ).has_value() );

    ExperimentRun run;
    run.setRunId( QStringLiteral( "run-tx" ) );
    run.setExperimentId( exp.experimentId() );
    run.setStatus( RunStatus::Created );
    REQUIRE( store.upsertRun( run ).has_value() );

    // Illegal status transition (Created -> Completed directly without Running)
    ExperimentRun illegal = run;
    illegal.setStatus( RunStatus::Completed );
    const auto res = store.upsertRun( illegal );
    CHECK( !res.has_value() );

    // Check that store is clean and valid transition still works
    run.setStatus( RunStatus::Running );
    REQUIRE( store.upsertRun( run ).has_value() );
    const auto loaded = store.runById( QStringLiteral( "run-tx" ) );
    REQUIRE( loaded.has_value() );
    CHECK( loaded->status() == RunStatus::Running );
}
