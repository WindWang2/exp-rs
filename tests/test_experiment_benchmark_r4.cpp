// test_experiment_benchmark_r4.cpp — Track 11 (R4) regressions for the
// benchmark runner input contract (#1333 item 2): duplicate truth/prediction
// rows used to re-weight every derived metric silently; the runner now
// refuses them by typed code. The truth source for the control case is a
// hand-computed confusion matrix (exact known-answer, no floats beyond the
// 1e-12 guard used across the experiment suites).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "experiment/benchmark_definition.h"
#include "experiment/benchmark_runner.h"

#include <QStringList>

using namespace sicnu::experiment;
using namespace sicnu::dataset;

namespace
{

BenchmarkDefinition makeDefinition()
{
    BenchmarkDefinition def;
    def.setBenchmarkId( QStringLiteral( "bench-r4-dup" ) );
    def.setBenchmarkVersion( 1 );
    def.setName( QStringLiteral( "R4 duplicate-row contract" ) );
    def.setTaskFamily( BenchmarkTaskFamily::Classification );
    def.setDatasetVersionId( QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
    def.setSplitManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    def.setLabelSchemaId( QStringLiteral( "lc" ) );
    def.setLabelSchemaVersion( 1 );
    def.metricNames() = QStringList{ QStringLiteral( "overall_accuracy" ) };
    def.protocol().setDatasetVersionId( def.datasetVersionId() );
    def.protocol().setSplitManifestId( def.splitManifestId() );
    def.protocol().setSubset( QStringLiteral( "test" ) );
    def.setRefusePseudoLabelsInTest( false );
    def.setSeedPolicy( 42 );
    return def;
}

BenchmarkTruth truthRow( const QString &id, const QString &label )
{
    BenchmarkTruth truth;
    truth.sampleId = id;
    truth.truthClass = label;
    truth.labelSource = AnnotationSourceType::Human;
    return truth;
}

BenchmarkPrediction predictionRow( const QString &id, const QString &label )
{
    BenchmarkPrediction prediction;
    prediction.sampleId = id;
    prediction.predictedClass = label;
    return prediction;
}

} // namespace

TEST_CASE( "duplicate truth rows are refused by typed code (r4)",
           "[experiment][benchmark][r4]" )
{
    BenchmarkRunRequest request;
    request.definition = makeDefinition();
    request.modelId = QStringLiteral( "model-a" );
    request.truths.append( truthRow( QStringLiteral( "s1" ), QStringLiteral( "water" ) ) );
    request.truths.append( truthRow( QStringLiteral( "s2" ), QStringLiteral( "land" ) ) );
    // The poisoned row: s1 appears twice with a DIFFERENT label — pre-fix,
    // the last row silently won and the metric was computed against it.
    request.truths.append( truthRow( QStringLiteral( "s1" ), QStringLiteral( "land" ) ) );
    request.predictions.append( predictionRow( QStringLiteral( "s1" ), QStringLiteral( "water" ) ) );
    request.predictions.append( predictionRow( QStringLiteral( "s2" ), QStringLiteral( "land" ) ) );

    const auto result = BenchmarkRunner::run( request );
    REQUIRE( result.has_value() );
    CHECK( result->status() == BenchmarkRunStatus::Failed );
    CHECK( result->failureCode() ==
           QStringLiteral( "experiment.benchmark_duplicate_sample" ) );
    CHECK( result->failureMessage().contains( QStringLiteral( "s1" ) ) );
}

TEST_CASE( "duplicate prediction rows are refused by typed code (r4)",
           "[experiment][benchmark][r4]" )
{
    BenchmarkRunRequest request;
    request.definition = makeDefinition();
    request.modelId = QStringLiteral( "model-a" );
    request.truths.append( truthRow( QStringLiteral( "s1" ), QStringLiteral( "water" ) ) );
    request.truths.append( truthRow( QStringLiteral( "s2" ), QStringLiteral( "land" ) ) );
    request.predictions.append( predictionRow( QStringLiteral( "s1" ), QStringLiteral( "water" ) ) );
    // The poisoned row: predicting s1 twice would count its cell twice in
    // the confusion matrix — a silent weight of 2 on one sample.
    request.predictions.append( predictionRow( QStringLiteral( "s1" ), QStringLiteral( "water" ) ) );

    const auto result = BenchmarkRunner::run( request );
    REQUIRE( result.has_value() );
    CHECK( result->status() == BenchmarkRunStatus::Failed );
    CHECK( result->failureCode() ==
           QStringLiteral( "experiment.benchmark_duplicate_sample" ) );
}

TEST_CASE( "distinct rows still evaluate to the exact known answer (r4 control)",
           "[experiment][benchmark][r4]" )
{
    BenchmarkRunRequest request;
    request.definition = makeDefinition();
    request.modelId = QStringLiteral( "model-a" );
    for ( const char *id : { "s1", "s2", "s3", "s4" } )
        request.truths.append(
            truthRow( QString::fromUtf8( id ),
                      QString::fromUtf8( id ) == QLatin1String( "s4" ) ? QStringLiteral( "land" )
                                                                       : QStringLiteral( "water" ) ) );
    for ( const char *id : { "s1", "s2", "s3" } )
        request.predictions.append( predictionRow( QString::fromUtf8( id ),
                                                   QStringLiteral( "water" ) ) );
    request.predictions.append( predictionRow( QStringLiteral( "s4" ), QStringLiteral( "water" ) ) );

    const auto result = BenchmarkRunner::run( request );
    REQUIRE( result.has_value() );
    CHECK( result->status() == BenchmarkRunStatus::Completed );
    bool sawOa = false;
    for ( const MetricResult &metric : result->metrics() )
    {
        if ( metric.name == QLatin1String( "overall_accuracy" ) )
        {
            sawOa = true;
            CHECK( metric.value == Catch::Approx( 0.75 ).epsilon( 1e-12 ) );
        }
    }
    CHECK( sawOa );
}
