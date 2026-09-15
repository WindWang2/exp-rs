// test_d19_benchmark.cpp — D19 Benchmark Definition / Runner / Compare /
// Service + ExperimentRun benchmark pins + pseudo-label safety.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_types.h"
#include "experiment/benchmark_compare.h"
#include "experiment/benchmark_definition.h"
#include "experiment/benchmark_runner.h"
#include "experiment/benchmark_service.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"

#include <cmath>

#include <QTemporaryDir>

using namespace sicnu::experiment;
using namespace sicnu::dataset;

namespace
{

BenchmarkDefinition makeDefinition()
{
    BenchmarkDefinition def;
    def.setBenchmarkId( QStringLiteral( "bench-class-v1" ) );
    def.setBenchmarkVersion( 1 );
    def.setName( QStringLiteral( "Classification smoke" ) );
    def.setTaskFamily( BenchmarkTaskFamily::Classification );
    def.setDatasetVersionId( QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
    def.setSplitManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    def.setLabelSchemaId( QStringLiteral( "lc" ) );
    def.setLabelSchemaVersion( 1 );
    def.metricNames() = QStringList{ QStringLiteral( "overall_accuracy" ),
                                     QStringLiteral( "kappa" ),
                                     QStringLiteral( "macro_f1" ),
                                     QStringLiteral( "per_class_f1" ) };
    def.protocol().setDatasetVersionId( def.datasetVersionId() );
    def.protocol().setSplitManifestId( def.splitManifestId() );
    def.protocol().setSubset( QStringLiteral( "test" ) );
    def.setRefusePseudoLabelsInTest( true );
    def.setSeedPolicy( 42 );
    return def;
}

} // namespace

TEST_CASE( "D19 benchmark definition round-trip and digest stability",
           "[d19][benchmark][definition]" )
{
    const BenchmarkDefinition def = makeDefinition();
    REQUIRE( def.validate().has_value() );
    const QString digest = def.contentDigest();
    const auto parsed = BenchmarkDefinition::fromJson( def.toJson() );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->contentDigest() == digest );
    CHECK( parsed->refusePseudoLabelsInTest() );
    CHECK( parsed->taskFamily() == BenchmarkTaskFamily::Classification );
}

TEST_CASE( "D19 runner computes metrics and records reproducibility gaps",
           "[d19][benchmark][runner]" )
{
    BenchmarkRunRequest request;
    request.definition = makeDefinition();
    request.modelId = QStringLiteral( "model-a" );
    request.seed = 42;
    // Intentionally omit modelDigest + softwareRevision → gaps.

    for ( const char *id : { "s1", "s2", "s3", "s4" } )
    {
        BenchmarkTruth truth;
        truth.sampleId = QString::fromUtf8( id );
        truth.truthClass = ( QString::fromUtf8( id ) == QLatin1String( "s4" ) )
                               ? QStringLiteral( "land" )
                               : QStringLiteral( "water" );
        truth.labelSource = AnnotationSourceType::Human;
        request.truths.append( truth );
    }
    // Perfect predictions for first three, wrong on s4.
    for ( const char *id : { "s1", "s2", "s3" } )
    {
        BenchmarkPrediction pred;
        pred.sampleId = QString::fromUtf8( id );
        pred.predictedClass = QStringLiteral( "water" );
        request.predictions.append( pred );
    }
    BenchmarkPrediction pred4;
    pred4.sampleId = QStringLiteral( "s4" );
    pred4.predictedClass = QStringLiteral( "water" ); // wrong
    request.predictions.append( pred4 );

    const auto result = BenchmarkRunner::run( request );
    REQUIRE( result.has_value() );
    CHECK( result->status() == BenchmarkRunStatus::Completed );
    CHECK( !result->reproducibilityComplete() );
    CHECK( result->reproducibilityGaps().contains( QStringLiteral( "model_digest" ) ) );
    CHECK( result->reproducibilityGaps().contains( QStringLiteral( "software_revision" ) ) );

    bool sawOa = false;
    for ( const MetricResult &metric : result->metrics() )
    {
        if ( metric.name == QLatin1String( "overall_accuracy" ) )
        {
            sawOa = true;
            CHECK( std::abs( metric.value - 0.75 ) < 1e-12 );
            CHECK( metric.definitionVersion == QLatin1String( "exp-rs.evaluation/1" ) );
        }
    }
    CHECK( sawOa );
}

TEST_CASE( "D19 runner refuses pseudo labels in protected test",
           "[d19][benchmark][pseudo]" )
{
    BenchmarkRunRequest request;
    request.definition = makeDefinition();
    BenchmarkTruth truth;
    truth.sampleId = QStringLiteral( "s1" );
    truth.truthClass = QStringLiteral( "water" );
    truth.labelSource = AnnotationSourceType::Pseudo;
    request.truths.append( truth );
    BenchmarkPrediction pred;
    pred.sampleId = QStringLiteral( "s1" );
    pred.predictedClass = QStringLiteral( "water" );
    request.predictions.append( pred );

    const auto result = BenchmarkRunner::run( request );
    REQUIRE( result.has_value() );
    CHECK( result->status() == BenchmarkRunStatus::Failed );
    CHECK( result->failureCode() ==
           QStringLiteral( "experiment.benchmark_pseudo_in_test" ) );
}

TEST_CASE( "D19 benchmark compare and seed summary", "[d19][benchmark][compare]" )
{
    BenchmarkRunRequest request;
    request.definition = makeDefinition();
    request.modelDigest = QStringLiteral( "abc" );
    request.softwareRevision = QStringLiteral( "deadbeef" );

    auto fill = [&]( const QString &predClass ) {
        request.truths.clear();
        request.predictions.clear();
        BenchmarkTruth truth;
        truth.sampleId = QStringLiteral( "s1" );
        truth.truthClass = QStringLiteral( "water" );
        request.truths.append( truth );
        BenchmarkPrediction pred;
        pred.sampleId = QStringLiteral( "s1" );
        pred.predictedClass = predClass;
        request.predictions.append( pred );
    };

    fill( QStringLiteral( "water" ) );
    const auto a = BenchmarkRunner::run( request );
    REQUIRE( a.has_value() );
    fill( QStringLiteral( "land" ) );
    const auto b = BenchmarkRunner::run( request );
    REQUIRE( b.has_value() );

    const BenchmarkComparison comparison = compareBenchmarkResults( *a, *b );
    CHECK( comparison.comparable );
    CHECK( !comparison.deltas.isEmpty() );

    BenchmarkService service;
    REQUIRE( service.publishDefinition( makeDefinition() ).has_value() );
    // Conflict on different content same id@version
    BenchmarkDefinition other = makeDefinition();
    other.setName( QStringLiteral( "changed" ) );
    CHECK( !service.publishDefinition( other ).has_value() );

    REQUIRE( service.recordResult( *a ).has_value() );
    REQUIRE( service.recordResult( *b ).has_value() );
    const auto summary =
        service.seedSummary( QStringLiteral( "bench-class-v1" ),
                             QStringList{ QStringLiteral( "overall_accuracy" ) } );
    REQUIRE( !summary.isEmpty() );
    CHECK( summary.first().count == 2 );
}

TEST_CASE( "D19 ExperimentRun carries optional benchmark pins",
           "[d19][benchmark][experiment]" )
{
    ExperimentRun run;
    run.setRunId( QStringLiteral( "33333333-3333-4333-8333-333333333333" ) );
    run.setExperimentId( QStringLiteral( "44444444-4444-4444-8444-444444444444" ) );
    run.setAlgorithmId( QStringLiteral( "op" ) );
    run.setDatasetVersionId( QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
    run.setSplitManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    run.setBenchmarkDefinitionId( QStringLiteral( "bench-class-v1" ) );
    run.setBenchmarkDefinitionVersion( 1 );
    const QJsonObject json = run.toJson();
    CHECK( json.value( QStringLiteral( "benchmark_definition_id" ) ).toString() ==
           QStringLiteral( "bench-class-v1" ) );
    const auto parsed = ExperimentRun::fromJson( json );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->benchmarkDefinitionId() == QStringLiteral( "bench-class-v1" ) );
    CHECK( parsed->benchmarkDefinitionVersion() == 1 );
}

TEST_CASE( "D19 BenchmarkService persists via ExperimentStore",
           "[d19][benchmark][persist]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "exp.sqlite" ) ) ) );

    BenchmarkService service( &store );
    REQUIRE( service.publishDefinition( makeDefinition() ).has_value() );

    BenchmarkRunRequest request;
    request.definition = makeDefinition();
    request.modelId = QStringLiteral( "model-a" );
    request.modelDigest = QStringLiteral( "digest" );
    request.softwareRevision = QStringLiteral( "rev" );
    BenchmarkTruth truth;
    truth.sampleId = QStringLiteral( "s1" );
    truth.truthClass = QStringLiteral( "water" );
    request.truths.append( truth );
    BenchmarkPrediction pred;
    pred.sampleId = QStringLiteral( "s1" );
    pred.predictedClass = QStringLiteral( "water" );
    request.predictions.append( pred );
    const auto run = service.run( request );
    REQUIRE( run.has_value() );
    CHECK( run->status() == BenchmarkRunStatus::Completed );

    // Cold service hydrates from the same store.
    BenchmarkService cold( &store );
    REQUIRE( cold.hydrateFromStore().has_value() );
    const auto def = cold.definition( QStringLiteral( "bench-class-v1" ), 1 );
    REQUIRE( def.has_value() );
    CHECK( def->contentDigest() == makeDefinition().contentDigest() );
    const auto results = cold.resultsFor( QStringLiteral( "bench-class-v1" ) );
    REQUIRE( !results.isEmpty() );
    CHECK( results.first().resultId() == run->resultId() );
}
