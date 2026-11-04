// tests/test_r27_t05_experiment_integrity.cpp — D-slice experiment oracles.
//
// #1781 (WE-22): regressionMetrics must exclude NaN/Inf pairs and report
// the exclusion count; all-non-finite input refuses instead of NaN means.
// #1782 (WE-23): confusion-matrix import refuses non-integer/string cells
// with the cell address (toInteger() used to turn them into silent 0s).
// #1785 (WE-31): duplicate pin roles refuse (a later axis must not
// silently overwrite an earlier pin); Tag axes may repeat.
// #1786 (WE-32): seed axis values must be canonical decimal integers
// below 2^53, deduped numerically.
// #1787 (WE-33): abstentions count as wrong in the headline accuracy;
// scored-only accuracy rides along as accuracy_scored_only.
// #1768 (WE-05): an all-unreadable byRef scan refuses to classify New.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_store.h"
#include "experiment/benchmark_runner.h"
#include "experiment/evaluation.h"
#include "experiment/experiment_matrix.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/lineage.h"
#include "experiment/repeat_execution.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <sqlite3.h>

#include <cmath>

using namespace sicnu::experiment;
using Catch::Approx;

namespace
{

ExperimentRun makeRun( const QString &runId, const QString &experimentId )
{
    ExperimentRun run;
    run.setRunId( runId );
    run.setExperimentId( experimentId );
    run.setAlgorithmId( QStringLiteral( "rs:ndvi" ) );
    run.setAlgorithmVersion( QStringLiteral( "1" ) );
    run.setDatasetVersionId( QStringLiteral( "dv-1" ) );
    run.setDatasetFingerprint( QStringLiteral( "fp-d" ) );
    run.setModelDigest( QStringLiteral( "md-1" ) );
    run.setSeed( 7 );
    run.setSoftwareRevision( QStringLiteral( "rev-1" ) );
    return run;
}

MatrixAxis axis( const QString &name, AxisRole role, std::initializer_list<QString> values )
{
    MatrixAxis a;
    a.name = name;
    a.role = role;
    a.values = QVector<QString>( values );
    return a;
}

} // namespace

// ---------------------------------------------------------------------------
// #1781 — regression metrics vs non-finite samples
// ---------------------------------------------------------------------------

TEST_CASE( "#1781 regression metrics exclude NaN/Inf pairs and report coverage",
           "[experiment][r27t05][issue-1781]" )
{
    const double nan_ = std::numeric_limits<double>::quiet_NaN();
    const double inf_ = std::numeric_limits<double>::infinity();

    // Mixed: 4 finite pairs + 2 poisoned (no-data) pairs. The means must
    // come from the finite pairs only.
    {
        const auto metrics = regressionMetrics(
            QVector<double>{ 1.0, 2.0, 3.0, 4.0, nan_, 9.0 },
            QVector<double>{ 1.5, 2.5, 2.5, 5.0, 8.0, inf_ } );
        REQUIRE( metrics.has_value() );
        CHECK( metrics->nonFiniteSkipped == 2 );
        CHECK( metrics->count == 4 );
        // MAE over |0.5|,|0.5|,|0.5|,|1.0| = 0.625
        CHECK( metrics->mae == Approx( 0.625 ) );
        CHECK( std::isfinite( metrics->rmse ) );
        CHECK( std::isfinite( metrics->r2 ) );
        CHECK( std::isfinite( metrics->bias ) );
        CHECK( metrics->toJson().value( QStringLiteral( "non_finite_skipped" ) ).toDouble()
               == 2.0 );
    }
    // All non-finite: refuse — a NaN mean would read as a (broken) success.
    {
        const auto refused = regressionMetrics( QVector<double>{ nan_, inf_ },
                                                QVector<double>{ 1.0, 2.0 } );
        CHECK_FALSE( refused.has_value() );
    }
}

// ---------------------------------------------------------------------------
// #1782 — confusion-matrix cell strictness
// ---------------------------------------------------------------------------

TEST_CASE( "#1782 confusion import refuses non-integer cells with the address",
           "[experiment][r27t05][issue-1782]" )
{
    auto matrixJson = []( const QJsonArray &counts ) {
        QJsonObject doc;
        doc.insert( QStringLiteral( "labels" ),
                    QJsonArray{ QStringLiteral( "water" ), QStringLiteral( "urban" ) } );
        doc.insert( QStringLiteral( "counts" ), counts );
        return doc;
    };

    // A string cell used to become a silent 0.
    {
        QJsonArray rows;
        rows.append( QJsonArray{ 3, QStringLiteral( "1" ) } );
        rows.append( QJsonArray{ 0, 2 } );
        const auto parsed = ConfusionMatrix::fromJson( matrixJson( rows ) );
        CHECK_FALSE( parsed.has_value() );
    }
    // Fractional cells refuse too.
    {
        QJsonArray rows;
        rows.append( QJsonArray{ 3.5, 1 } );
        rows.append( QJsonArray{ 0, 2 } );
        CHECK_FALSE( ConfusionMatrix::fromJson( matrixJson( rows ) ).has_value() );
    }
    // Null cell refuses.
    {
        QJsonArray rows;
        rows.append( QJsonArray{ 3, QJsonValue() } );
        rows.append( QJsonArray{ 0, 2 } );
        CHECK_FALSE( ConfusionMatrix::fromJson( matrixJson( rows ) ).has_value() );
    }
    // Genuine integers still parse.
    {
        QJsonArray rows;
        rows.append( QJsonArray{ 3, 1 } );
        rows.append( QJsonArray{ 0, 2 } );
        const auto parsed = ConfusionMatrix::fromJson( matrixJson( rows ) );
        REQUIRE( parsed.has_value() );
        CHECK( parsed->total() == 6 );
    }
}

// ---------------------------------------------------------------------------
// #1785 / #1786 — matrix axis roles and seeds
// ---------------------------------------------------------------------------

TEST_CASE( "#1785 duplicate pin roles refuse, tag roles may repeat",
           "[experiment][r27t05][issue-1785]" )
{
    MatrixDescriptor descriptor;
    descriptor.matrixId = QStringLiteral( "m1" );
    descriptor.experimentId = QStringLiteral( "e1" );
    descriptor.workflowId = QStringLiteral( "w1" );
    descriptor.axes.append( axis( QStringLiteral( "model_a" ), AxisRole::Model,
                                  { QStringLiteral( "rs:ndvi" ) } ) );
    descriptor.axes.append( axis( QStringLiteral( "model_b" ), AxisRole::Model,
                                  { QStringLiteral( "rs:savi" ) } ) );
    CHECK_FALSE( descriptor.validate().has_value() );

    // Tag axes carry no pin and may repeat.
    MatrixDescriptor tags;
    tags.matrixId = QStringLiteral( "m2" );
    tags.experimentId = QStringLiteral( "e1" );
    tags.workflowId = QStringLiteral( "w1" );
    tags.axes.append( axis( QStringLiteral( "region" ), AxisRole::Tag,
                            { QStringLiteral( "north" ) } ) );
    tags.axes.append( axis( QStringLiteral( "season" ), AxisRole::Tag,
                            { QStringLiteral( "summer" ) } ) );
    CHECK( tags.validate().has_value() );
}

TEST_CASE( "#1786 seed axis accepts only canonical distinct seeds below 2^53",
           "[experiment][r27t05][issue-1786]" )
{
    auto seedDescriptor = []( std::initializer_list<QString> values ) {
        MatrixDescriptor descriptor;
        descriptor.matrixId = QStringLiteral( "m3" );
        descriptor.experimentId = QStringLiteral( "e1" );
        descriptor.workflowId = QStringLiteral( "w1" );
        descriptor.axes.append(
            axis( QStringLiteral( "seed" ), AxisRole::Seed, values ) );
        return descriptor;
    };

    // Canonical distinct seeds pass.
    CHECK( seedDescriptor( { QStringLiteral( "0" ), QStringLiteral( "1" ),
                             QStringLiteral( "42" ) } )
               .validate()
               .has_value() );
    // Leading-zero spelling refuses (it inflates n without a new seed).
    CHECK_FALSE( seedDescriptor( { QStringLiteral( "1" ), QStringLiteral( "01" ) } )
                     .validate()
                     .has_value() );
    // Signed spelling refuses.
    CHECK_FALSE(
        seedDescriptor( { QStringLiteral( "+1" ) } ).validate().has_value() );
    // 2^53 and above refuse (aligned with bundle import).
    CHECK_FALSE( seedDescriptor( { QStringLiteral( "9007199254740992" ) } )
                     .validate()
                     .has_value() );
    CHECK( seedDescriptor( { QStringLiteral( "9007199254740991" ) } )
               .validate()
               .has_value() );
}

// ---------------------------------------------------------------------------
// #1787 — abstentions count as wrong in the headline accuracy
// ---------------------------------------------------------------------------

TEST_CASE( "#1787 headline accuracy divides by scored plus abstained",
           "[experiment][r27t05][issue-1787]" )
{
    ConfusionMatrix matrix( { QStringLiteral( "water" ), QStringLiteral( "urban" ) }, 2 );
    matrix.setCount( 0, 0, 6 ); // 6 correct
    matrix.setCount( 0, 1, 2 );
    matrix.setCount( 1, 0, 2 );
    matrix.setCount( 1, 1, 2 ); // 10 scored, 8 correct

    const auto metrics = metricResultsFromConfusion(
        matrix,
        QStringList{ QStringLiteral( "overall_accuracy" ), QStringLiteral( "oa" ),
                     QStringLiteral( "accuracy_scored_only" ) },
        QStringLiteral( "overall" ),
        /*abstentions=*/10 );

    double headline = -1.0;
    double scoredOnly = -1.0;
    qint64 support = -1;
    for ( const MetricResult &metric : metrics )
    {
        if ( metric.name == QStringLiteral( "overall_accuracy" ) )
        {
            headline = metric.value;
            support = metric.support;
        }
        if ( metric.name == QStringLiteral( "accuracy_scored_only" ) )
            scoredOnly = metric.value;
    }
    // 8 correct of (12 scored + 10 abstained) = 8/22; the scored-only view
    // stays 8/12 — visible, but no longer the headline.
    CHECK( headline == Approx( 8.0 / 22.0 ) );
    CHECK( support == 22 );
    CHECK( scoredOnly == Approx( 8.0 / 12.0 ) );
}

// ---------------------------------------------------------------------------
// #1768 — all-unreadable byRef scan refuses to classify New
// ---------------------------------------------------------------------------

TEST_CASE( "#1768 an all-corrupt byRef scan refuses instead of New",
           "[experiment][r27t05][issue-1768]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-r27t05" ) );
    experiment.setName( QStringLiteral( "byRef unreadable oracle" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    // A row whose json carries the execution_ref (so the byRef listing
    // finds it) but is torn — the record cannot parse. Write it through the
    // store first (valid row), then break the json in place (the same
    // direct-DB-surgery technique as test_experiment_consistency_r4).
    {
        ExperimentRun torn = makeRun( QStringLiteral( "torn-1" ), experiment.experimentId() );
        torn.setExecutionRef( QStringLiteral( "ref-x" ) );
        REQUIRE( store.upsertRun( torn ).has_value() );
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( qUtf8Printable( dir.filePath( QStringLiteral( "experiments.db" ) ) ),
                                  &raw, SQLITE_OPEN_READWRITE, nullptr ) == SQLITE_OK );
        char *error = nullptr;
        REQUIRE( sqlite3_exec( raw,
                               "UPDATE experiment_runs SET json='{\"execution_ref\":\"ref-x\" torn'"
                               " WHERE run_id='torn-1'",
                               nullptr, nullptr, &error ) == SQLITE_OK );
        sqlite3_free( error );
        sqlite3_close( raw );
    }

    RepeatExecutionClassifier classifier( store );
    const ExperimentRun probe = makeRun( QStringLiteral( "probe" ),
                                         experiment.experimentId() );
    const auto verdict = classifier.classify( probe.executionIdentity(), QString(),
                                              QStringLiteral( "ref-x" ) );
    // Every ref-matched row is unreadable: the duplicate/deviation question
    // cannot be answered — never a confident "New".
    CHECK_FALSE( verdict.has_value() );
}

// ---------------------------------------------------------------------------
// #1770 — depth-cap truncation is disclosed, not silent
// ---------------------------------------------------------------------------

TEST_CASE( "#1770 hitting the lineage depth cap sets budgetExhausted",
           "[experiment][r27t05][issue-1770]" )
{
    QTemporaryDir dir;
    sicnu::dataset::DatasetStore datasets;
    sicnu::experiment::ExperimentStore experiments;
    // Empty stores are fine: the chain below is declared through addEdge.
    LineageGraph graph( datasets, experiments );
    // asset-1 → dataset_version-1 → run-1: two upstream levels.
    graph.addEdge( LineageNodeId{ QStringLiteral( "asset" ), QStringLiteral( "asset-1" ) },
                   QStringLiteral( "derived" ),
                   LineageNodeId{ QStringLiteral( "dataset_version" ), QStringLiteral( "dv-1" ) } );
    graph.addEdge( LineageNodeId{ QStringLiteral( "dataset_version" ), QStringLiteral( "dv-1" ) },
                   QStringLiteral( "trained_on" ),
                   LineageNodeId{ QStringLiteral( "run" ), QStringLiteral( "run-1" ) } );

    // maxDepth 1 admits the dataset_version but cuts the asset level: the
    // remaining frontier must be disclosed (#1770) exactly like the node
    // budget does — the old loop dropped it silently.
    const auto truncated =
        graph.ancestors( LineageNodeId{ QStringLiteral( "run" ), QStringLiteral( "run-1" ) },
                         /*maxDepth=*/1 );
    CHECK( truncated.nodes.size() == 1 );
    CHECK( truncated.budgetExhausted );

    // Deep enough cap: nothing is cut, nothing is flagged.
    const auto complete =
        graph.ancestors( LineageNodeId{ QStringLiteral( "run" ), QStringLiteral( "run-1" ) },
                         /*maxDepth=*/16 );
    CHECK( complete.nodes.size() == 2 );
    CHECK_FALSE( complete.budgetExhausted );
}
