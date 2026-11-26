// test_r33_t04_experiment_admission.cpp — R33-T04 / WP16:
//
//   * MetricResult::fromJson used QJsonValue::toDouble(), which silently
//     yields 0.0 for a MISSING or wrong-typed value — a torn metric record
//     re-entered comparisons as a measured zero (fabricated deltas).
//   * BenchmarkResult::fromJson adopted those metrics unconditionally.
//   * ReplayReadiness::equivalentRuns folded a FAILED listRuns page into
//     "walk finished" and returned a partial list that callers treated as
//     the complete equivalent set (a false "never ran before").
#include <QJsonValue>
#include "experiment/benchmark_definition.h"
#include "experiment/benchmark_runner.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/replay_readiness.h"

#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>

using namespace sicnu::experiment;

namespace
{

QJsonObject validProtocol()
{
    QJsonObject protocol;
    protocol.insert( QStringLiteral( "dataset_version_id" ), QStringLiteral( "dv-1" ) );
    protocol.insert( QStringLiteral( "split_manifest_id" ), QStringLiteral( "sp-1" ) );
    protocol.insert( QStringLiteral( "subset" ), QStringLiteral( "test" ) );
    protocol.insert( QStringLiteral( "iou_threshold" ), 0.5 );
    protocol.insert( QStringLiteral( "confidence_threshold" ), 0.5 );
    protocol.insert( QStringLiteral( "aggregation" ), QStringLiteral( "macro" ) );
    return protocol;
}

QJsonObject resultEnvelope()
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), 1 );
    json.insert( QStringLiteral( "result_id" ), QStringLiteral( "res-r33t04" ) );
    json.insert( QStringLiteral( "benchmark_id" ), QStringLiteral( "bench-r33t04" ) );
    json.insert( QStringLiteral( "status" ), QStringLiteral( "completed" ) );
    json.insert( QStringLiteral( "protocol" ), validProtocol() );
    return json;
}

QJsonObject metric( const QString &name, const QJsonValue &value )
{
    QJsonObject m;
    m.insert( QStringLiteral( "name" ), name );
    m.insert( QStringLiteral( "definition_version" ), QStringLiteral( "exp-rs.evaluation/1" ) );
    m.insert( QStringLiteral( "value" ), value );
    m.insert( QStringLiteral( "scope" ), QStringLiteral( "overall" ) );
    return m;
}
} // namespace

TEST_CASE( "MetricResult::fromJson rejects missing/wrong-typed values instead of defaulting to 0",
           "[experiment][r33t04]" )
{
    SECTION( "valid metric round-trips exactly" )
    {
        const auto parsed = MetricResult::fromJson( metric( QStringLiteral( "iou" ), 0.875 ) );
        REQUIRE( parsed );
        CHECK( parsed.value().name == QStringLiteral( "iou" ) );
        CHECK( parsed.value().value == 0.875 );
        CHECK( parsed.value().scope == QStringLiteral( "overall" ) );
    }

    SECTION( "missing value is rejected" )
    {
        QJsonObject m = metric( QStringLiteral( "iou" ), 0.875 );
        m.remove( QStringLiteral( "value" ) );
        CHECK( !MetricResult::fromJson( m ) );
    }

    SECTION( "string and bool values are rejected" )
    {
        CHECK( !MetricResult::fromJson( metric( QStringLiteral( "iou" ),
                                                QStringLiteral( "0.875" ) ) ) );
        CHECK( !MetricResult::fromJson( metric( QStringLiteral( "iou" ), true ) ) );
    }

    SECTION( "missing or non-string name is rejected" )
    {
        QJsonObject noName = metric( QStringLiteral( "iou" ), 0.875 );
        noName.remove( QStringLiteral( "name" ) );
        CHECK( !MetricResult::fromJson( noName ) );

        QJsonObject numName;
        numName.insert( QStringLiteral( "name" ), 7 );
        numName.insert( QStringLiteral( "value" ), 0.5 );
        CHECK( !MetricResult::fromJson( numName ) );
    }
}

TEST_CASE( "BenchmarkResult::fromJson propagates a torn metric as a whole-document failure",
           "[experiment][r33t04]" )
{
    QJsonObject json = resultEnvelope();

    SECTION( "one good + one torn metric fails the document" )
    {
        QJsonArray metrics;
        metrics.append( metric( QStringLiteral( "iou" ), 0.875 ) );
        QJsonObject torn = metric( QStringLiteral( "f1" ), 0.74 );
        torn.remove( QStringLiteral( "value" ) );
        metrics.append( torn );
        json.insert( QStringLiteral( "metrics" ), metrics );
        const auto parsed = BenchmarkResult::fromJson( json );
        REQUIRE( !parsed );
        CHECK( !parsed.diagnostics().isEmpty() );
    }

    SECTION( "all-good metrics still parse" )
    {
        QJsonArray metrics;
        metrics.append( metric( QStringLiteral( "iou" ), 0.875 ) );
        metrics.append( metric( QStringLiteral( "f1" ), 0.74 ) );
        json.insert( QStringLiteral( "metrics" ), metrics );
        const auto parsed = BenchmarkResult::fromJson( json );
        REQUIRE( parsed );
        CHECK( parsed.value().metrics().size() == 2 );
        CHECK( parsed.value().metrics().at( 1 ).value == 0.74 );
    }
}

TEST_CASE( "equivalentRuns reports a query failure instead of returning a partial list",
           "[experiment][r33t04]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    ExperimentStore store;
    QString error;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "exp.db" ) ), &error ) );

    // The store enforces the experiment-existence mirror on run upserts
    // (R2 F2) — create the parent experiment first, like production does.
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-r33t04" ) );
    experiment.setName( QStringLiteral( "admission" ) );
    experiment.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    ExperimentRun run;
    run.setRunId( QStringLiteral( "11111111-2222-4333-8444-555555555555" ) );
    run.setExperimentId( QStringLiteral( "exp-r33t04" ) );
    run.setAlgorithmId( QStringLiteral( "rs:ndvi" ) );
    REQUIRE( store.upsertRun( run ).has_value() );

    const QString fingerprint = runExecutionFingerprint( run.executionIdentity() );

    // Open store: the walk completes and returns a real (possibly empty)
    // list.
    {
        const auto equivalent = ReplayReadiness::equivalentRuns( store, fingerprint,
                                                                 run.runId() );
        REQUIRE( equivalent );
        CHECK( equivalent.value().isEmpty() ); // its own id is excluded
    }

    // A failing page query (closed store) must be a typed FAILURE — the old
    // code folded !page into "walk done" and answered an empty list as if
    // the full history had been checked.
    store.close();
    const auto failed = ReplayReadiness::equivalentRuns( store, fingerprint, run.runId() );
    CHECK( !failed );
    CHECK( !failed.diagnostics().isEmpty() );
}
