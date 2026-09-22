// test_study_exemplars.cpp — the shipped study exemplars cannot silently rot
// (RS14-07 Parameter Sensitivity & Uncertainty Studio).
//
// Every examples/studies/*.sicnu-study.json must load through the PRODUCTION
// spec reader (strict version + unknown-field refusal), sample to its declared
// shape, and replay deterministically. These files are the teaching entry
// points (students load them in the studio) and agent-mode templates — a
// typo'd or rotted exemplar would teach the wrong contract.
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include "study/study_sampling.h"
#include "study/study_spec.h"

using namespace sicnu::study;

namespace
{

QJsonObject readExemplar( const QString &fileName )
{
    const QString path =
        QStringLiteral( CMAKE_SOURCE_DIR "/examples/studies/" ) + fileName;
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const QJsonObject json = QJsonDocument::fromJson( file.readAll() ).object();
    REQUIRE( !json.isEmpty() );
    return json;
}

Result<ParameterStudySpec> loadSpec( const QString &fileName )
{
    return ParameterStudySpec::fromJson( readExemplar( fileName ) );
}

} // namespace

TEST_CASE( "the exemplar directory ships exactly the three documented studies",
           "[study][exemplars]" )
{
    QDir dir( QStringLiteral( CMAKE_SOURCE_DIR "/examples/studies" ) );
    REQUIRE( dir.exists() );
    const QStringList files =
        dir.entryList( QStringList{ QStringLiteral( "*.sicnu-study.json" ) }, QDir::Files );
    REQUIRE( files.size() == 3 );
    REQUIRE( files.contains( QStringLiteral( "ndvi-threshold.sicnu-study.json" ) ) );
    REQUIRE( files.contains(
        QStringLiteral( "classification-reject-threshold.sicnu-study.json" ) ) );
    REQUIRE( files.contains( QStringLiteral( "change-threshold.sicnu-study.json" ) ) );
}

TEST_CASE( "ndvi threshold exemplar: OAT shape, baseline at 0.5, deterministic replay",
           "[study][exemplars][oat]" )
{
    const auto spec = loadSpec( QStringLiteral( "ndvi-threshold.sicnu-study.json" ) );
    REQUIRE( spec.has_value() );
    REQUIRE( spec.value().algorithmId == QStringLiteral( "rs:threshold_raster" ) );
    REQUIRE( spec.value().strategy == SamplingStrategy::OneAtATime );
    REQUIRE( spec.value().dimensions.size() == 1 );
    REQUIRE( spec.value().dimensions.first().parameterPath == QStringLiteral( "threshold" ) );
    // Teaching contract of this exemplar: the spatial story is ON, and no
    // "best parameters" verdict exists anywhere in the pipeline.
    REQUIRE( spec.value().spatialComparison );
    REQUIRE( spec.value().objectiveMetric.isEmpty() );
    REQUIRE( spec.value().objectiveMetrics.isEmpty() );
    // Exact metric set: each declared metric becomes one report curve.
    REQUIRE( spec.value().metricNames.size() == 2 );
    REQUIRE( spec.value().metricNames.contains( QStringLiteral( "maskedPercent" ) ) );
    REQUIRE( spec.value().metricNames.contains( QStringLiteral( "thresholdUsed" ) ) );

    const auto points = sampleStudyPoints( spec.value() );
    REQUIRE( points.has_value() );
    // baseline (threshold at the ladder reference 0.5) + 8 single-dimension
    // variations = 9, all inside budget.maxRuns = 32.
    REQUIRE( points.value().size() == 9 );

    bool hasBaseline = false;
    for ( const StudyPoint &point : points.value() )
    {
        // Every submission keeps the teaching-fixed base parameters and adds
        // exactly the swept value.
        REQUIRE( point.parameters.value( QStringLiteral( "thresholdMethod" ) ).toString()
                 == QStringLiteral( "manual" ) );
        REQUIRE( point.parameters.contains( QStringLiteral( "threshold" ) ) );
        if ( point.assignments.value( QStringLiteral( "threshold" ) )
             == canonicalValueText( 0.5 ) )
            hasBaseline = true;
    }
    REQUIRE( hasBaseline );

    const auto replay = sampleStudyPoints( spec.value() );
    REQUIRE( replay.has_value() );
    REQUIRE( replay.value().size() == points.value().size() );
    for ( int i = 0; i < points.value().size(); ++i )
        REQUIRE( replay.value().at( i ).pointId == points.value().at( i ).pointId );
}

TEST_CASE( "classification exemplar: grid with replicates and a declared objective",
           "[study][exemplars][grid]" )
{
    const auto spec =
        loadSpec( QStringLiteral( "classification-reject-threshold.sicnu-study.json" ) );
    REQUIRE( spec.has_value() );
    REQUIRE( spec.value().algorithmId == QStringLiteral( "rs:supervised_classification" ) );
    REQUIRE( spec.value().strategy == SamplingStrategy::Grid );
    // This exemplar declares an explicit task metric with its direction — the
    // one shipped study where the report MAY name a declaredBest. The
    // metrics are the operator's held-out evaluation pair (testSplit > 0 in
    // base_parameters is what makes the operator emit them at all).
    REQUIRE( spec.value().objectiveMetric == QStringLiteral( "overallAccuracy" ) );
    REQUIRE( spec.value().objectiveMetrics.size() == 1 );
    REQUIRE( spec.value().objectiveMetrics.first().maximize );
    REQUIRE( spec.value().metricNames.size() == 2 );
    REQUIRE( spec.value().metricNames.contains( QStringLiteral( "overallAccuracy" ) ) );
    REQUIRE( spec.value().metricNames.contains( QStringLiteral( "kappa" ) ) );
    REQUIRE( spec.value().baseParameters.value( QStringLiteral( "testSplit" ) ).toDouble()
             > 0.0 );

    const auto points = sampleStudyPoints( spec.value() );
    REQUIRE( points.has_value() );
    // 5 ladder values × 3 seed replicates = 15 <= budget.maxRuns = 16.
    REQUIRE( points.value().size() == 15 );

    // Replicates of one parameter set share the EXACT parameters document and
    // differ only in the recorded replicate seed.
    QHash<QString, QSet<quint64>> seedsByParameters;
    for ( const StudyPoint &point : points.value() )
    {
        REQUIRE( point.parameters.value( QStringLiteral( "method" ) ).toString()
                 == QStringLiteral( "svm" ) );
        const double reject =
            point.parameters.value( QStringLiteral( "rejectThreshold" ) ).toDouble();
        REQUIRE( reject >= 0.3 );
        REQUIRE( reject <= 0.9 );
        seedsByParameters[ QJsonDocument( point.parameters ).toJson() ].insert(
            point.seed );
    }
    REQUIRE( seedsByParameters.size() == 5 );
    for ( auto it = seedsByParameters.constBegin(); it != seedsByParameters.constEnd(); ++it )
        REQUIRE( it.value().size() == 3 );
}

TEST_CASE( "change threshold exemplar: LHS budget shape and seeded variability",
           "[study][exemplars][lhs]" )
{
    const auto spec = loadSpec( QStringLiteral( "change-threshold.sicnu-study.json" ) );
    REQUIRE( spec.has_value() );
    REQUIRE( spec.value().algorithmId == QStringLiteral( "rs:change_detection" ) );
    REQUIRE( spec.value().strategy == SamplingStrategy::LatinHypercube );
    REQUIRE( spec.value().spatialComparison );

    const auto points = sampleStudyPoints( spec.value() );
    REQUIRE( points.has_value() );
    // Controlled scale: N = maxRuns / replicates = 6 parameter sets × 2 seed
    // replicates = 12 points.
    REQUIRE( points.value().size() == 12 );

    QSet<QString> distinctParameterSets;
    for ( const StudyPoint &point : points.value() )
    {
        const double threshold =
            point.parameters.value( QStringLiteral( "threshold" ) ).toDouble();
        REQUIRE( threshold >= 0.05 );
        REQUIRE( threshold <= 0.8 );
        distinctParameterSets.insert(
            canonicalValueText( point.parameters.value( QStringLiteral( "threshold" ) )
                                    .toDouble() ) );
    }
    REQUIRE( distinctParameterSets.size() == 6 );

    // Deterministic replay for the shipped seed…
    const auto replay = sampleStudyPoints( spec.value() );
    REQUIRE( replay.has_value() );
    for ( int i = 0; i < points.value().size(); ++i )
        REQUIRE( replay.value().at( i ).pointId == points.value().at( i ).pointId );

    // …and a different seed samples a DIFFERENT space (the teaching point of
    // the seeded sampler: variability is a budget decision, not noise).
    // Compare the SAMPLED VALUES, not the pointIds — a pointId embeds the
    // replicate seed, so it differs even if the parameter sets repeated.
    ParameterStudySpec otherSeed = spec.value();
    otherSeed.budget.seed = spec.value().budget.seed + 1;
    const auto varied = sampleStudyPoints( otherSeed );
    REQUIRE( varied.has_value() );
    QSet<QString> sampledValues;
    for ( const StudyPoint &point : points.value() )
        sampledValues.insert( point.assignments.value( QStringLiteral( "threshold" ) ) );
    int foreignValues = 0;
    for ( const StudyPoint &point : varied.value() )
        if ( !sampledValues.contains(
                 point.assignments.value( QStringLiteral( "threshold" ) ) ) )
            ++foreignValues;
    REQUIRE( foreignValues > 0 );
}
