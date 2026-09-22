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
#include "experiment/repeat_execution.h"
#include "experiment/reproduction_bundle.h"
#include "experiment/reproduction_bundle_import.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

#include <sqlite3.h>

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

TEST_CASE( "corrupt stored run is preserved, not overwritten (#1056)",
           "[experiment][store]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    Experiment experiment;
    experiment.setExperimentId( ExperimentId::generate().toString() );
    experiment.setName( QStringLiteral( "corrupt-row forensics" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    ExperimentRun run = makeRun( QStringLiteral( "run-corrupt" ), experiment.experimentId() );
    run.setStatus( RunStatus::Created );
    REQUIRE( store.upsertRun( run ).has_value() );

    // Corrupt the stored record out-of-band: the row now exists but its
    // JSON cannot be parsed.
    const QString corruptPayload = QStringLiteral( "{not-json-evidence" );
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( dir.filePath( QStringLiteral( "experiments.db" ) )
                                      .toUtf8()
                                      .constData(),
                                  &raw, SQLITE_OPEN_READWRITE, nullptr ) == SQLITE_OK );
        sqlite3_stmt *update = nullptr;
        REQUIRE( sqlite3_prepare_v2( raw, "UPDATE experiment_runs SET json=? WHERE run_id=?",
                                     -1, &update, nullptr ) == SQLITE_OK );
        sqlite3_bind_text( update, 1, corruptPayload.toUtf8().constData(), -1,
                           SQLITE_TRANSIENT );
        sqlite3_bind_text( update, 2, run.runId().toUtf8().constData(), -1,
                           SQLITE_TRANSIENT );
        REQUIRE( sqlite3_step( update ) == SQLITE_DONE );
        sqlite3_finalize( update );
        sqlite3_close( raw );
    }

    // The read path stays fail-conservative (row reads as absent).
    CHECK( !store.runById( run.runId() ).has_value() );

    // The write path must NOT treat the corrupt row as "no existing run":
    // fail closed with a typed diagnostic, transition/identity checks
    // cannot run on evidence that cannot be parsed.
    const auto overwritten = store.upsertRun( run );
    REQUIRE( !overwritten.has_value() );
    REQUIRE( overwritten.diagnostics().first().code ==
             QStringLiteral( "experiment.corrupt_record" ) );

    // Evidence survives: the corrupt payload is still exactly in place.
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( dir.filePath( QStringLiteral( "experiments.db" ) )
                                      .toUtf8()
                                      .constData(),
                                  &raw, SQLITE_OPEN_READONLY, nullptr ) == SQLITE_OK );
        sqlite3_stmt *probe = nullptr;
        REQUIRE( sqlite3_prepare_v2( raw, "SELECT json FROM experiment_runs WHERE run_id=?",
                                     -1, &probe, nullptr ) == SQLITE_OK );
        sqlite3_bind_text( probe, 1, run.runId().toUtf8().constData(), -1,
                           SQLITE_TRANSIENT );
        REQUIRE( sqlite3_step( probe ) == SQLITE_ROW );
        const QString stored = QString::fromUtf8(
            reinterpret_cast<const char *>( sqlite3_column_text( probe, 0 ) ) );
        CHECK( stored == corruptPayload );
        sqlite3_finalize( probe );
        sqlite3_close( raw );
    }
}

TEST_CASE( "execution ref lookup matches JSON-escaped refs (#1056)",
           "[experiment][store]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    Experiment experiment;
    experiment.setExperimentId( ExperimentId::generate().toString() );
    experiment.setName( QStringLiteral( "escaped refs" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    // The ref carries characters the JSON serializer escapes (quote,
    // backslash, control char): the old raw-substring needle could never
    // match the stored escaped form, so restart reconciliation saw no run
    // and re-recorded a duplicate.
    const QString escapedRef = QStringLiteral( "job\\42\"batch\u0001x" );
    ExperimentRun matched = makeRun( QStringLiteral( "run-ref" ), experiment.experimentId() );
    matched.setExecutionRef( escapedRef );
    matched.setStatus( RunStatus::Created );
    REQUIRE( store.upsertRun( matched ).has_value() );

    ExperimentRun other = makeRun( QStringLiteral( "run-other" ), experiment.experimentId() );
    other.setExecutionRef( QStringLiteral( "job\\42\"batch\u0002x" ) );
    other.setStatus( RunStatus::Created );
    REQUIRE( store.upsertRun( other ).has_value() );

    ExperimentRun plain = makeRun( QStringLiteral( "run-plain" ), experiment.experimentId() );
    plain.setStatus( RunStatus::Created );
    REQUIRE( store.upsertRun( plain ).has_value() );

    const auto ids = store.runIdsByExecutionRef( escapedRef );
    REQUIRE( ids.size() == 1 );
    CHECK( ids.first() == QStringLiteral( "run-ref" ) );

    const auto otherIds = store.runIdsByExecutionRef( QStringLiteral( "job\\42\"batch\u0002x" ) );
    REQUIRE( otherIds.size() == 1 );
    CHECK( otherIds.first() == QStringLiteral( "run-other" ) );

    CHECK( store.runIdsByExecutionRef( QStringLiteral( "absent-ref" ) ).isEmpty() );
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

TEST_CASE( "repeat-execution classifier separates duplicate, rerun, deviation and new",
           "[experiment][repeat][identity]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );
    Experiment experiment;
    experiment.setExperimentId( ExperimentId::generate().toString() );
    experiment.setName( QStringLiteral( "repeat classification" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    ExperimentRun run = makeRun( QStringLiteral( "run-1" ), experiment.experimentId() );
    run.setStatus( RunStatus::Created );
    REQUIRE( store.upsertRun( run ).has_value() );
    run.setStatus( RunStatus::Running );
    REQUIRE( store.upsertRun( run ).has_value() );
    run.setStatus( RunStatus::Completed );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    run.artifacts().append( ExperimentRun::Artifact{
        QStringLiteral( "out.tif" ), QStringLiteral( "primary" ),
        QStringLiteral( "digest-a" ), 100 } );
    run.setMetrics( QJsonObject{ { QStringLiteral( "overall_accuracy" ), 0.85 } } );
    REQUIRE( store.upsertRun( run ).has_value() );
    const QString resultFp = run.resultFingerprint();

    RepeatExecutionClassifier classifier( store );

    // Malformed asks are typed failures, never verdicts.
    RunExecutionIdentity blank;
    CHECK( !classifier.classify( blank ).has_value() );

    // Same identity + same result fingerprint → duplicate execution.
    const auto duplicate = classifier.classify( run.executionIdentity(), resultFp );
    REQUIRE( duplicate.has_value() );
    CHECK( duplicate->classification == RepeatExecutionClassifier::Classification::SameExecution );
    CHECK( duplicate->matchedRunIds.contains( QStringLiteral( "run-1" ) ) );

    // Same identity, results differ → rerun; the verdict reports the
    // matched run's declared determinism honesty.
    const QString otherResult = runResultFingerprint(
        { QStringLiteral( "digest-b" ) }, QJsonObject{ { QStringLiteral( "overall_accuracy" ), 0.5 } } );
    const auto rerun = classifier.classify( run.executionIdentity(), otherResult );
    REQUIRE( rerun.has_value() );
    CHECK( rerun->classification ==
           RepeatExecutionClassifier::Classification::EquivalentRerun );
    CHECK( !rerun->reasons.isEmpty() );

    // Same identity, no result evidence yet → duplicate CANDIDATE (the
    // classifier refuses to guess duplicate-vs-rerun).
    const auto candidate = classifier.classify( run.executionIdentity() );
    REQUIRE( candidate.has_value() );
    CHECK( candidate->classification ==
           RepeatExecutionClassifier::Classification::SameIdentity );

    // Unknown identity → new.
    RunExecutionIdentity fresh = run.executionIdentity();
    fresh.seed = 77;
    const auto freshVerdict = classifier.classify( fresh );
    REQUIRE( freshVerdict.has_value() );
    CHECK( freshVerdict->classification == RepeatExecutionClassifier::Classification::New );

    // Same platform executionRef under DIFFERENT pins → deviated, with the
    // pin-level comparison attached.
    ExperimentRun changed = makeRun( QStringLiteral( "run-2" ), experiment.experimentId() );
    changed.setSeed( 43 ); // a real pin change under the SAME execution ref
    changed.setExecutionRef( QStringLiteral( "task-9" ) );
    changed.setStatus( RunStatus::Created );
    REQUIRE( store.upsertRun( changed ).has_value() );
    RunExecutionIdentity refIdentity = run.executionIdentity(); // original pins
    refIdentity.seed = 999; // matches NO recorded run (unique twin-less pins)
    const auto deviated =
        classifier.classify( refIdentity, QString(), QStringLiteral( "task-9" ) );
    REQUIRE( deviated.has_value() );
    CHECK( deviated->classification == RepeatExecutionClassifier::Classification::Deviated );
    CHECK( deviated->matchedRunIds.contains( QStringLiteral( "run-2" ) ) );
    CHECK( !deviated->pinComparison.isEmpty() );

    // Environment drift is evidence and never a verdict downgrade.
    QJsonObject fields;
    fields.insert( QStringLiteral( "platform" ), QStringLiteral( "linux" ) );
    RunEnvironment repeatEnv = RunEnvironment::fromFields( fields );
    const auto drifted = classifier.classify( run.executionIdentity(), resultFp, QString(),
                                              repeatEnv );
    REQUIRE( drifted.has_value() );
    CHECK( drifted->classification ==
           RepeatExecutionClassifier::Classification::SameExecution );
    CHECK( !drifted->environmentDrift.isEmpty() );

    // Verdict JSON round-trip carries the classification + evidence.
    const QJsonObject json = drifted->toJson();
    CHECK( json.value( QStringLiteral( "classification" ) ).toString() ==
           QStringLiteral( "same_execution" ) );
    CHECK( json.value( QStringLiteral( "schema_version" ) ) ==
           kRepeatExecutionSchemaVersion );
}

TEST_CASE( "batch run upsert is all-or-nothing; batch metric save follows the "
           "conflict rule",
           "[experiment][store][batch]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );
    Experiment experiment;
    experiment.setExperimentId( ExperimentId::generate().toString() );
    experiment.setName( QStringLiteral( "batch" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    auto makeBatchRun = [ & ]( const QString &id, quint64 seed )
    {
        ExperimentRun run = makeRun( id, experiment.experimentId() );
        run.setSeed( seed );
        run.setStatus( RunStatus::Created );
        return run;
    };

    // Advance the whole batch once: four Created runs land whole.
    QVector<ExperimentRun> valid;
    valid.append( makeBatchRun( QStringLiteral( "b-1" ), 1 ) );
    valid.append( makeBatchRun( QStringLiteral( "b-2" ), 2 ) );
    valid.append( makeBatchRun( QStringLiteral( "b-3" ), 3 ) );
    valid.append( makeBatchRun( QStringLiteral( "b-4" ), 4 ) );
    QVector<ExperimentRun> fresh = valid;
    REQUIRE( store.upsertRunsBatch( fresh ).has_value() );
    CHECK( store.runCount() == 4 );

    // A batch whose middle roll is invalid leaves NOTHING behind: b-1 may
    // advance, but b-2 claims a Completed state without ever running — the
    // refusal rolls back b-1's advance too.
    QVector<ExperimentRun> mixed;
    ExperimentRun advance = makeBatchRun( QStringLiteral( "b-1" ), 1 );
    advance.setStatus( RunStatus::Running );
    mixed.append( advance );
    ExperimentRun illegal = makeBatchRun( QStringLiteral( "b-2" ), 2 );
    illegal.setStatus( RunStatus::Completed ); // Created -> Completed is refused
    mixed.append( illegal );
    ExperimentRun advance3 = makeBatchRun( QStringLiteral( "b-3" ), 3 );
    advance3.setStatus( RunStatus::Running );
    mixed.append( advance3 );
    const auto rejected = store.upsertRunsBatch( mixed );
    CHECK_FALSE( rejected.has_value() );
    CHECK( rejected.diagnostics().first().code == QStringLiteral( "experiment.bad_transition" ) );
    CHECK( store.runCount() == 4 );
    CHECK( store.runById( QStringLiteral( "b-1" ) )->status() == RunStatus::Created );
    CHECK( store.runById( QStringLiteral( "b-3" ) )->status() == RunStatus::Created );
    CHECK_FALSE( store.runById( QStringLiteral( "b-2" ) )->status() == RunStatus::Completed );

    // Same-status re-submission is a refusal for the batch, exactly like the
    // single-run path ("correction = new run", never a silent rewrite).
    const auto idempotent = store.upsertRunsBatch( fresh );
    CHECK_FALSE( idempotent.has_value() );
    CHECK( idempotent.diagnostics().first().code == QStringLiteral( "experiment.bad_transition" ) );
    CHECK( store.runCount() == 4 );

    // Metric batches: two records land together; a conflicting re-save rolls
    // the whole batch back (the new record in it is NOT persisted).
    MetricRecord first;
    first.runId = QStringLiteral( "b-1" );
    first.protocol.setDatasetVersionId( QStringLiteral( "dv" ) );
    first.protocol.setSplitManifestId( QStringLiteral( "sm" ) );
    first.protocol.setSubset( QStringLiteral( "test" ) );
    first.metrics = QJsonObject{ { QStringLiteral( "acc" ), 0.9 } };
    MetricRecord second = first;
    second.runId = QStringLiteral( "b-2" );
    REQUIRE( store.saveMetricRecordsBatch( { first, second } ).has_value() );
    MetricRecord conflicting = first;
    conflicting.metrics = QJsonObject{ { QStringLiteral( "acc" ), 0.1 } };
    MetricRecord third = first;
    third.runId = QStringLiteral( "b-3" );
    const auto batchConflict = store.saveMetricRecordsBatch( { conflicting, third } );
    CHECK( !batchConflict.has_value() );
    CHECK( batchConflict.diagnostics().first().code == QStringLiteral( "experiment.conflict" ) );
    CHECK( !store.metricRecordForRun( QStringLiteral( "b-3" ) ).has_value() );
}

TEST_CASE( "keyset cursor paging covers deep run ranges exactly once and fails "
           "typed on misuse",
           "[experiment][store][cursor]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );
    Experiment experiment;
    experiment.setExperimentId( ExperimentId::generate().toString() );
    experiment.setName( QStringLiteral( "cursor" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    constexpr int kRuns = 250;
    QVector<ExperimentRun> batch;
    batch.reserve( kRuns );
    for ( int i = 0; i < kRuns; ++i )
    {
        ExperimentRun run = makeRun( QStringLiteral( "run-%1" ).arg( i, 3, 10, QLatin1Char( '0' ) ),
                                     experiment.experimentId() );
        run.setStatus( RunStatus::Created );
        batch.append( run );
    }
    REQUIRE( store.upsertRunsBatch( batch ).has_value() );

    // Full walk: 250 runs in pages of 40, order identical to listRuns.
    QStringList viaCursor;
    QString cursor;
    while ( true )
    {
        const auto page = store.listRunsByCursor( experiment.experimentId(), QString(),
                                                  QString(), cursor, 40 );
        REQUIRE( page.has_value() );
        CHECK( page->total == kRuns );
        for ( const ExperimentRun &run : page->runs )
            viaCursor.append( run.runId() );
        if ( page->nextCursor.isEmpty() )
            break;
        cursor = page->nextCursor;
    }
    REQUIRE( viaCursor.size() == kRuns );
    CHECK( viaCursor.size() == QSet<QString>( viaCursor.cbegin(), viaCursor.cend() ).size() );
    const auto fullList = store.listRuns( experiment.experimentId(), QString(), QString(), 0,
                                          ExperimentStore::kMaxPageSize );
    REQUIRE( fullList.has_value() );
    QStringList viaOffset;
    for ( const ExperimentRun &run : fullList->second )
        viaOffset.append( run.runId() );
    CHECK( viaCursor == viaOffset );

    // A cursor replayed under a DIFFERENT filter is a typed mismatch, never
    // a silent rescan.
    const auto firstPage = store.listRunsByCursor( experiment.experimentId(), QString(),
                                                   QString(), QString(), 40 );
    REQUIRE( firstPage.has_value() );
    CHECK( firstPage->nextCursor.isEmpty() == false );
    const auto mismatched = store.listRunsByCursor( QStringLiteral( "other-experiment" ),
                                                    QString(), QString(),
                                                    firstPage->nextCursor, 40 );
    CHECK( !mismatched.has_value() );
    CHECK( mismatched.diagnostics().first().code ==
           QStringLiteral( "experiment.cursor_mismatch" ) );

    // A tampered cursor is cursor_invalid, not a crash or a fabricated page.
    const auto tampered = store.listRunsByCursor(
        experiment.experimentId(), QString(), QString(),
        firstPage->nextCursor.left( firstPage->nextCursor.size() / 2 ), 40 );
    CHECK( !tampered.has_value() );
    CHECK( tampered.diagnostics().first().code == QStringLiteral( "data.cursor_invalid" ) );

    // Status filter + cursor compose.
    QString runningCursor;
    qint64 runningSeen = 0;
    while ( true )
    {
        const auto page = store.listRunsByCursor(
            QString(), QString(), runStatusToString( RunStatus::Created ), runningCursor, 100 );
        REQUIRE( page.has_value() );
        runningSeen += page->runs.size();
        if ( page->nextCursor.isEmpty() )
            break;
        runningCursor = page->nextCursor;
    }
    CHECK( runningSeen == kRuns );
}

TEST_CASE( "reproduction bundle imports offline: integrity gates, identity "
           "preservation and idempotent re-import",
           "[experiment][repro][import]" )
{
    // --- export from store A -------------------------------------------------
    QTemporaryDir dir;
    DatasetStore datasets;
    ExperimentStore experiments;
    REQUIRE( datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
    REQUIRE( experiments.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    const DatasetId datasetId = DatasetId::generate();
    REQUIRE( datasets.createDataset( datasetId, QStringLiteral( "lc" ) ).has_value() );
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId.toString() );
    manifest.setVersionId( DatasetVersionId::generate().toString() );
    const auto draft = datasets.createDraftVersion( manifest );
    REQUIRE( draft.has_value() );
    const DatasetVersionId versionId =
        DatasetVersionId::fromString( draft->versionId() ).value_or( DatasetVersionId{} );
    REQUIRE( datasets.stageVersion( versionId ).has_value() );
    const auto committed = datasets.commitVersion( versionId );
    REQUIRE( committed.has_value() );

    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-src" ) );
    experiment.setName( QStringLiteral( "source" ) );
    REQUIRE( experiments.upsertExperiment( experiment ).has_value() );
    ExperimentRun run = makeRun( QStringLiteral( "run-src" ), QStringLiteral( "exp-src" ) );
    run.setDatasetVersionId( committed->versionId() );
    run.setDatasetFingerprint( committed->fingerprint() );
    run.setStatus( RunStatus::Created );
    REQUIRE( experiments.upsertRun( run ).has_value() );
    run.setStatus( RunStatus::Running );
    REQUIRE( experiments.upsertRun( run ).has_value() );
    run.setStatus( RunStatus::Completed );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( experiments.upsertRun( run ).has_value() );
    MetricRecord record;
    record.runId = QStringLiteral( "run-src" );
    record.protocol.setDatasetVersionId( committed->versionId() );
    record.protocol.setSplitManifestId( run.splitManifestId() );
    record.protocol.setSubset( QStringLiteral( "test" ) );
    record.metrics = QJsonObject{ { QStringLiteral( "overall_accuracy" ), 0.9 } };
    REQUIRE( experiments.saveMetricRecord( record ).has_value() );

    ReproductionBundleExporter exporter( experiments, datasets );
    ReproductionBundleOptions exportOptions;
    exportOptions.outputDir = dir.filePath( QStringLiteral( "bundle" ) );
    exportOptions.currentSoftwareRevision = QStringLiteral( "test" );
    const auto exportReport = exporter.exportRun( QStringLiteral( "run-src" ), exportOptions );
    REQUIRE( exportReport.ok );

    // --- import into a FRESH store B -----------------------------------------
    ExperimentStore target;
    REQUIRE( target.open( dir.filePath( QStringLiteral( "target.db" ) ) ) );
    Experiment home;
    home.setExperimentId( QStringLiteral( "exp-dst" ) );
    home.setName( QStringLiteral( "destination" ) );
    REQUIRE( target.upsertExperiment( home ).has_value() );

    ReproductionBundleImporter importer( target );
    ReproductionBundleImportOptions importOptions;
    importOptions.bundleDir = exportReport.bundlePath;
    importOptions.targetExperimentId = QStringLiteral( "exp-dst" );

    // A missing target experiment is a typed refusal.
    ReproductionBundleImportOptions orphaned = importOptions;
    orphaned.targetExperimentId = QStringLiteral( "exp-nowhere" );
    CHECK_FALSE( importer.importRun( orphaned ).ok );

    // Integrity gate: a tampered bundle never reaches the store.
    const QString tamperedBundle = dir.filePath( QStringLiteral( "tampered" ) );
    QDir().mkpath( tamperedBundle );
    for ( const QString &name :
          { QStringLiteral( "manifest.json" ), QStringLiteral( "run_config.json" ),
            QStringLiteral( "environment.json" ), QStringLiteral( "checksums.txt" ) } )
        QFile::copy( QDir( exportReport.bundlePath ).filePath( name ),
                     QDir( tamperedBundle ).filePath( name ) );
    {
        QFile tampered( QDir( tamperedBundle ).filePath( QStringLiteral( "run_config.json" ) ) );
        REQUIRE( tampered.open( QIODevice::WriteOnly | QIODevice::Append ) );
        tampered.write( " " );
    }
    ReproductionBundleImportOptions tamperedOptions = importOptions;
    tamperedOptions.bundleDir = tamperedBundle;
    const auto tamperedReport = importer.importRun( tamperedOptions );
    CHECK_FALSE( tamperedReport.ok );
    CHECK( tamperedReport.warnings.join( QLatin1Char( ';' ) )
               .contains( QLatin1String( "checksum" ) ) );
    CHECK( target.runCount() == 0 );

    // keepOriginalRunId onto an occupied id with DIFFERENT identity refuses
    // (the Created-status run could otherwise be silently overwritten).
    Experiment occupier;
    occupier.setExperimentId( QStringLiteral( "exp-occ" ) );
    occupier.setName( QStringLiteral( "occupier" ) );
    REQUIRE( target.upsertExperiment( occupier ).has_value() );
    ExperimentRun existing = makeRun( QStringLiteral( "run-src" ), QStringLiteral( "exp-occ" ) );
    existing.setSeed( 123 ); // different identity from the bundle
    existing.setStatus( RunStatus::Created );
    REQUIRE( target.upsertRun( existing ).has_value() );
    ReproductionBundleImportOptions keepOptions = importOptions;
    keepOptions.keepOriginalRunId = true;
    keepOptions.targetExperimentId = QStringLiteral( "exp-occ" );
    const auto refused = importer.importRun( keepOptions );
    CHECK_FALSE( refused.ok );
    CHECK( refused.warnings.join( QLatin1Char( ';' ) )
               .contains( QLatin1String( "already exists" ) ) );
    CHECK( target.runById( QStringLiteral( "run-src" ) )->seed() == 123 );

    // The clean bundle imports: identity preserved, lifecycle honest.
    const auto importReport = importer.importRun( importOptions );
    REQUIRE( importReport.ok );
    CHECK( importReport.originalRunId == QStringLiteral( "run-src" ) );
    CHECK( importReport.runId != QStringLiteral( "run-src" ) ); // fresh id by default
    CHECK_FALSE( importReport.alreadyPresent );
    const auto installed = target.runById( importReport.runId );
    REQUIRE( installed.has_value() );
    CHECK( installed->status() == RunStatus::Created ); // never a fake lifecycle
    CHECK( runExecutionFingerprint( installed->executionIdentity() ) ==
           runExecutionFingerprint( run.executionIdentity() ) );
    CHECK( installed->executionRef() == QStringLiteral( "bundle:run-src" ) );
    // Metrics evidence installed beside the run.
    CHECK( target.metricRecordForRun( importReport.runId ).has_value() );
    // The classifier recognizes the imported evidence as the same execution.
    RepeatExecutionClassifier classifier( target );
    const auto duplicate = classifier.classify( run.executionIdentity(),
                                                run.resultFingerprint() );
    REQUIRE( duplicate.has_value() );
    CHECK( duplicate->classification ==
           RepeatExecutionClassifier::Classification::SameExecution );

    // Re-importing the same bundle is an idempotent no-op.
    const auto again = importer.importRun( importOptions );
    REQUIRE( again.ok );
    CHECK( again.alreadyPresent );
    CHECK( again.runId == importReport.runId );
    CHECK( target.runCount() == 2 ); // the imported run + the occupier

    CHECK( target.runById( QStringLiteral( "run-src" ) )->seed() == 123 );
}

TEST_CASE( "secret redaction reaches arrays nested inside arrays", "[experiment][identity]" )
{
    // The redaction pass once stopped at objects behind ONE array level;
    // the lab report side already needed a deep pass of its own. Every
    // export boundary shares RunEnvironment::redactSecretKeys, so it must
    // recurse the same shapes the capsule validator's scanTree recurses.
    QJsonObject inner;
    inner.insert( QStringLiteral( "api_key" ), QStringLiteral( "sk-live-123" ) );
    QJsonArray outer;
    outer.append( QJsonValue( QJsonArray{ inner } ) );
    QJsonObject payload;
    payload.insert( QStringLiteral( "layers" ), outer );

    const QJsonObject redacted = RunEnvironment::redactSecretKeys( payload );
    const QJsonObject reached = redacted.value( QStringLiteral( "layers" ) )
                                    .toArray()
                                    .at( 0 )
                                    .toArray()
                                    .at( 0 )
                                    .toObject();
    REQUIRE( reached.value( QStringLiteral( "api_key" ) ).toString()
             == QStringLiteral( "***" ) );
}

TEST_CASE( "metric records refuse foreign layout versions", "[experiment][metrics]" )
{
    // evaluation.h promises: readers refuse foreign versions rather than
    // silently reinterpreting documents. A missing key stays readable as
    // v1 (records written before the field existed).
    MetricRecord record;
    record.runId = QStringLiteral( "run-metrics-version" );
    record.metrics = QJsonObject{ { QStringLiteral( "overallAccuracy" ), 0.75 } };

    QJsonObject foreign = record.toJson();
    foreign.insert( QStringLiteral( "metrics_schema_version" ), 99 );
    const auto refused = MetricRecord::fromJson( foreign );
    REQUIRE( !refused.has_value() );
    bool typedVersion = false;
    for ( const auto &diagnostic : refused.diagnostics() )
        if ( diagnostic.code == QStringLiteral( "evaluation.version" ) )
            typedVersion = true;
    REQUIRE( typedVersion );

    QJsonObject legacy = record.toJson();
    legacy.remove( QStringLiteral( "metrics_schema_version" ) );
    const auto readable = MetricRecord::fromJson( legacy );
    REQUIRE( readable.has_value() );
    CHECK( readable.value().metricsSchemaVersion == 1 );
}

TEST_CASE( "artifact sizes above the int range round-trip through the store",
           "[experiment][identity]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-big" ) );
    experiment.setName( QStringLiteral( "big artifacts" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    // toJson writes qint64; reading back through toInt() silently folded
    // 3 GiB down to the -1 default (and replay_readiness then skipped the
    // size-verified check for it).
    ExperimentRun run = makeRun( QStringLiteral( "run-big" ), QStringLiteral( "exp-big" ) );
    ExperimentRun::Artifact artifact;
    artifact.path = QStringLiteral( "out/dem.tif" );
    artifact.role = QStringLiteral( "primary" );
    artifact.digest = QStringLiteral( "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" );
    artifact.sizeBytes = 3221225472LL; // 3 GiB
    run.artifacts().append( artifact );
    REQUIRE( store.upsertRun( run ).has_value() );

    const auto loaded = store.runById( QStringLiteral( "run-big" ) );
    REQUIRE( loaded.has_value() );
    REQUIRE( loaded->artifacts().size() == 1 );
    CHECK( loaded->artifacts().first().sizeBytes == 3221225472LL );
}

TEST_CASE( "a run that moves experiments keeps listing columns and"
           " run-id mirrors consistent",
           "[experiment][store]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );
    Experiment first;
    first.setExperimentId( QStringLiteral( "exp-a" ) );
    first.setName( QStringLiteral( "A" ) );
    REQUIRE( store.upsertExperiment( first ).has_value() );
    Experiment second;
    second.setExperimentId( QStringLiteral( "exp-b" ) );
    second.setName( QStringLiteral( "B" ) );
    REQUIRE( store.upsertExperiment( second ).has_value() );

    ExperimentRun run = makeRun( QStringLiteral( "run-move" ), QStringLiteral( "exp-a" ) );
    run.setStatus( RunStatus::Created );
    REQUIRE( store.upsertRun( run ).has_value() );

    // experiment_id is not part of the frozen execution identity, so a
    // start-time upsert (Created → Running) that also re-homes the run is
    // legal — and every index the store keeps must follow the body, not
    // trail behind it (listRuns filters on columns; run_count reads mirrors).
    run.setExperimentId( QStringLiteral( "exp-b" ) );
    run.setStatus( RunStatus::Running );
    REQUIRE( store.upsertRun( run ).has_value() );

    const auto inOld = store.listRuns( QStringLiteral( "exp-a" ), {}, {} );
    REQUIRE( inOld.has_value() );
    CHECK( inOld.value().second.isEmpty() );

    const auto inNew = store.listRuns( QStringLiteral( "exp-b" ), {}, {} );
    REQUIRE( inNew.has_value() );
    REQUIRE( inNew.value().second.size() == 1 );
    CHECK( inNew.value().second.first().runId() == QStringLiteral( "run-move" ) );
    CHECK( inNew.value().second.first().status() == RunStatus::Running );

    const auto oldExperiment = store.experimentById( QStringLiteral( "exp-a" ) );
    REQUIRE( oldExperiment.has_value() );
    CHECK( !oldExperiment->runIds().contains( QStringLiteral( "run-move" ) ) );
    const auto newExperiment = store.experimentById( QStringLiteral( "exp-b" ) );
    REQUIRE( newExperiment.has_value() );
    CHECK( newExperiment->runIds().contains( QStringLiteral( "run-move" ) ) );
}

TEST_CASE( "bundle integrity gate: an emptied checksums.txt never passes vacuously",
           "[experiment][repro]" )
{
    QTemporaryDir dir;
    DatasetStore datasets;
    ExperimentStore experiments;
    REQUIRE( datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
    REQUIRE( experiments.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );
    ReproductionBundleExporter exporter( experiments, datasets );

    // The bundle is otherwise JUDGEABLE: its dataset pin resolves with a
    // matching fingerprint, so the pre-#hardening validator — whose
    // checksum loop silently never ran on an emptied table — walked all the
    // document checks and returned BestEffort with a "bundle checksums
    // verified" reason in the list. Integrity must gate FIRST and FAIL on a
    // zero-entry table.
    const DatasetId datasetId = DatasetId::generate();
    REQUIRE( datasets.createDataset( datasetId, QStringLiteral( "lc" ) ).has_value() );
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId.toString() );
    manifest.setVersionId( DatasetVersionId::generate().toString() );
    const auto draft = datasets.createDraftVersion( manifest );
    REQUIRE( draft.has_value() );
    const auto versionId =
        DatasetVersionId::fromString( draft->versionId() ).value_or( DatasetVersionId{} );
    REQUIRE( datasets.stageVersion( versionId ).has_value() );
    const auto committed = datasets.commitVersion( versionId );
    REQUIRE( committed.has_value() );

    const QString bundle = dir.filePath( QStringLiteral( "bundle" ) );
    REQUIRE( QDir( bundle ).mkpath( QStringLiteral( "." ) ) );
    const auto writeFile = [&bundle]( const QString &name, const QByteArray &content ) {
        QFile file( QDir( bundle ).filePath( name ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( content ) == content.size() );
    };
    writeFile( QStringLiteral( "manifest.json" ),
               QJsonDocument( QJsonObject{ { QStringLiteral( "schema_version" ), 1 } } ).toJson() );
    writeFile( QStringLiteral( "run_config.json" ),
               QJsonDocument( QJsonObject{
                   { QStringLiteral( "run_id" ), QStringLiteral( "run-x" ) },
                   { QStringLiteral( "determinism" ), QStringLiteral( "strict" ) },
                   { QStringLiteral( "dataset_version_id" ), committed->versionId() },
                   { QStringLiteral( "dataset_fingerprint" ), committed->fingerprint() } } )
                   .toJson() );
    writeFile( QStringLiteral( "environment.json" ), QByteArray( "{}" ) );
    // The post-tamper shape: zero verifiable entries.
    writeFile( QStringLiteral( "checksums.txt" ), QByteArray() );

    ReproductionHooks hooks;
    const auto validation = exporter.validateBundle( bundle, hooks );
    CHECK( validation.level == ReproductionLevel::Impossible );
    bool missingCoverage = false;
    for ( const QString &reason : validation.reasons )
        if ( reason.contains( QLatin1String( "does not cover" ) ) )
            missingCoverage = true;
    REQUIRE( missingCoverage );

    // A table naming files it does not verify, or files the directory does
    // not contain, is an integrity signal too — not noise to skip.
    writeFile( QStringLiteral( "checksums.txt" ),
               QByteArray( "deadbeef  run_config.json\n" ) );
    const auto truncated = exporter.validateBundle( bundle, hooks );
    CHECK( truncated.level == ReproductionLevel::Impossible );
}
