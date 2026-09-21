// test_study_spec.cpp — ParameterStudySpec contract (RS14-07 Slice A).
//
// RED→GREEN contract tests for the study spec value object: versioned JSON
// roundtrip, typed validation refusals, budget caps. Pure core: no QGIS,
// no network, no GUI.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "study/study_spec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace sicnu::study;

namespace
{

ParameterStudySpec validSpec()
{
    ParameterStudySpec spec;
    spec.studyId = QStringLiteral( "ndvi-threshold-sweep" );
    spec.experimentId = QStringLiteral( "exp-ndvi-sweep" );
    spec.algorithmId = QStringLiteral( "rs:threshold_raster" );
    spec.baseParameters = QJsonObject{
        { QStringLiteral( "input" ), QStringLiteral( "/data/ndvi.tif" ) } };
    spec.strategy = SamplingStrategy::Grid;
    ParameterDimension dim;
    dim.parameterPath = QStringLiteral( "threshold" );
    dim.minValue = 0.1;
    dim.maxValue = 0.9;
    dim.stepCount = 5;
    spec.dimensions.append( dim );
    spec.budget.maxRuns = 100;
    spec.budget.maxInFlight = 2;
    spec.budget.perRunTimeoutMs = 600000;
    spec.budget.seedReplicates = 1;
    spec.budget.seed = 42;
    spec.metricNames.append( QStringLiteral( "maskedPercent" ) );
    return spec;
}

bool hasCode( const sicnu::data::Result<void> &result, const QString &code )
{
    if ( result )
        return false;
    for ( const auto &d : result.diagnostics() )
        if ( d.code == code )
            return true;
    return false;
}

} // namespace

TEST_CASE( "ParameterStudySpec roundtrips through versioned JSON", "[study][spec]" )
{
    const auto spec = validSpec();

    const auto json = spec.toJson();
    REQUIRE( json.value( QStringLiteral( "schema_version" ) ).toInt() == kStudySpecSchemaVersion );

    const auto parsed = ParameterStudySpec::fromJson( json );
    REQUIRE( parsed.has_value() );
    const auto &restored = parsed.value();
    REQUIRE( restored.studyId == spec.studyId );
    REQUIRE( restored.experimentId == spec.experimentId );
    REQUIRE( restored.algorithmId == spec.algorithmId );
    REQUIRE( restored.baseParameters == spec.baseParameters );
    REQUIRE( restored.strategy == spec.strategy );
    REQUIRE( restored.dimensions.size() == 1 );
    REQUIRE( restored.dimensions.at( 0 ).parameterPath == QStringLiteral( "threshold" ) );
    REQUIRE( restored.dimensions.at( 0 ).minValue == Catch::Approx( 0.1 ) );
    REQUIRE( restored.dimensions.at( 0 ).maxValue == Catch::Approx( 0.9 ) );
    REQUIRE( restored.dimensions.at( 0 ).stepCount == 5 );
    REQUIRE( restored.budget.maxRuns == 100 );
    REQUIRE( restored.budget.maxInFlight == 2 );
    REQUIRE( restored.budget.perRunTimeoutMs == 600000 );
    REQUIRE( restored.budget.seedReplicates == 1 );
    REQUIRE( restored.budget.seed == 42 );
    REQUIRE( restored.metricNames == spec.metricNames );
    REQUIRE( restored.validate().has_value() );
}

TEST_CASE( "ParameterStudySpec refuses foreign schema versions", "[study][spec]" )
{
    auto json = validSpec().toJson();
    json.insert( QStringLiteral( "schema_version" ), 999 );
    const auto parsed = ParameterStudySpec::fromJson( json );
    REQUIRE( !parsed.has_value() );
    bool found = false;
    for ( const auto &d : parsed.diagnostics() )
        found = found || d.code == QStringLiteral( "study.spec_unsupported_version" );
    REQUIRE( found );
}

TEST_CASE( "ParameterStudySpec refuses unknown fields (a mistyped spec must not silently change semantics)", "[study][spec]" )
{
    auto json = validSpec().toJson();
    json.insert( QStringLiteral( "tolerence" ), 0.5 ); // typo'd key
    const auto parsed = ParameterStudySpec::fromJson( json );
    REQUIRE( !parsed.has_value() );
    bool found = false;
    for ( const auto &d : parsed.diagnostics() )
        found = found || d.code == QStringLiteral( "study.spec_unknown_field" );
    REQUIRE( found );
}

TEST_CASE( "ParameterStudySpec validation refusals are typed", "[study][spec]" )
{
    SECTION( "missing study id" )
    {
        auto spec = validSpec();
        spec.studyId.clear();
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_invalid_study_id" ) ) );
    }
    SECTION( "missing experiment id" )
    {
        auto spec = validSpec();
        spec.experimentId.clear();
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_invalid_experiment_id" ) ) );
    }
    SECTION( "missing algorithm id" )
    {
        auto spec = validSpec();
        spec.algorithmId.clear();
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_invalid_algorithm_id" ) ) );
    }
    SECTION( "no dimensions" )
    {
        auto spec = validSpec();
        spec.dimensions.clear();
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_no_dimensions" ) ) );
    }
    SECTION( "dimension range inverted" )
    {
        auto spec = validSpec();
        spec.dimensions[0].minValue = 0.9;
        spec.dimensions[0].maxValue = 0.1;
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_invalid_dimension_range" ) ) );
    }
    SECTION( "dimension needs at least two ladder values" )
    {
        auto spec = validSpec();
        spec.dimensions[0].stepCount = 1;
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_invalid_dimension_steps" ) ) );
    }
    SECTION( "empty dimension path" )
    {
        auto spec = validSpec();
        spec.dimensions[0].parameterPath.clear();
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_invalid_dimension_path" ) ) );
    }
    SECTION( "duplicate dimension paths" )
    {
        auto spec = validSpec();
        spec.dimensions.append( spec.dimensions.at( 0 ) );
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_duplicate_dimension" ) ) );
    }
    SECTION( "no metrics" )
    {
        auto spec = validSpec();
        spec.metricNames.clear();
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_no_metrics" ) ) );
    }
    SECTION( "objective metric must be an aggregated metric" )
    {
        auto spec = validSpec();
        spec.objectiveMetric = QStringLiteral( "notCollected" );
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_unknown_metric" ) ) );
    }
    SECTION( "objective metric direction must reference a collected metric" )
    {
        auto spec = validSpec();
        spec.objectiveMetrics.append( StudyMetricSpec{ QStringLiteral( "notCollected" ), true } );
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_unknown_metric" ) ) );
    }
    SECTION( "a declared task metric requires a declared direction" )
    {
        auto spec = validSpec();
        spec.objectiveMetric = QStringLiteral( "maskedPercent" ); // no objective_metrics entry
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_missing_direction" ) ) );
    }
    SECTION( "negative spatial epsilon" )
    {
        auto spec = validSpec();
        spec.spatialEpsilon = -1.0;
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_invalid_epsilon" ) ) );
    }
}

TEST_CASE( "ParameterStudySpec budget refusals are typed and capped by the matrix authority", "[study][spec][budget]" )
{
    SECTION( "maxRuns below one" )
    {
        auto spec = validSpec();
        spec.budget.maxRuns = 0;
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_invalid_budget" ) ) );
    }
    SECTION( "maxRuns beyond the matrix cell cap is refused, never truncated" )
    {
        auto spec = validSpec();
        spec.budget.maxRuns = sicnu::experiment::kMaxMatrixCells + 1;
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_budget_over_cap" ) ) );
    }
    SECTION( "in-flight window below one" )
    {
        auto spec = validSpec();
        spec.budget.maxInFlight = 0;
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_invalid_budget" ) ) );
    }
    SECTION( "in-flight window above the hard bound" )
    {
        auto spec = validSpec();
        spec.budget.maxInFlight = kStudyMaxInFlightBound + 1;
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_invalid_budget" ) ) );
    }
    SECTION( "seed replicates below one" )
    {
        auto spec = validSpec();
        spec.budget.seedReplicates = 0;
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_invalid_budget" ) ) );
    }
    SECTION( "per-run timeout below one millisecond" )
    {
        auto spec = validSpec();
        spec.budget.perRunTimeoutMs = 0;
        REQUIRE( hasCode( spec.validate(), QStringLiteral( "study.spec_invalid_budget" ) ) );
    }
}

TEST_CASE( "Sampling strategy names roundtrip and unknown names are typed", "[study][spec]" )
{
    REQUIRE( samplingStrategyToString( SamplingStrategy::Grid ) == QStringLiteral( "grid" ) );
    REQUIRE( samplingStrategyToString( SamplingStrategy::OneAtATime ) == QStringLiteral( "oat" ) );
    REQUIRE( samplingStrategyToString( SamplingStrategy::LatinHypercube ) == QStringLiteral( "lhs" ) );

    REQUIRE( samplingStrategyFromString( QStringLiteral( "grid" ) ) == SamplingStrategy::Grid );
    REQUIRE( samplingStrategyFromString( QStringLiteral( "oat" ) ) == SamplingStrategy::OneAtATime );
    REQUIRE( samplingStrategyFromString( QStringLiteral( "lhs" ) ) == SamplingStrategy::LatinHypercube );
    REQUIRE( !samplingStrategyFromString( QStringLiteral( "bayes" ) ).has_value() );

    auto json = validSpec().toJson();
    json.insert( QStringLiteral( "sampling" ), QStringLiteral( "autoML" ) );
    const auto parsed = ParameterStudySpec::fromJson( json );
    REQUIRE( !parsed.has_value() );
    bool found = false;
    for ( const auto &d : parsed.diagnostics() )
        found = found || d.code == QStringLiteral( "study.spec_invalid_strategy" );
    REQUIRE( found );
}

TEST_CASE( "StudyMetricSpec direction roundtrips", "[study][spec]" )
{
    auto spec = validSpec();
    spec.metricNames.append( QStringLiteral( "maskedPixels" ) );
    spec.objectiveMetrics.append( StudyMetricSpec{ QStringLiteral( "maskedPercent" ), false } );
    const auto parsed = ParameterStudySpec::fromJson( spec.toJson() );
    REQUIRE( parsed.has_value() );
    REQUIRE( parsed.value().objectiveMetrics.size() == 1 );
    REQUIRE( parsed.value().objectiveMetrics.at( 0 ).name == QStringLiteral( "maskedPercent" ) );
    REQUIRE_FALSE( parsed.value().objectiveMetrics.at( 0 ).maximize );
    REQUIRE( parsed.value().validate().has_value() );
}
