// test_study_sampling.cpp — deterministic sampler contracts (RS14-07 Slice B).
//
// Oracles:
//   - grid points are INDISTINGUISHABLE from matrix authority cells (same
//     identity space, same cellId algorithm, same enumeration);
//   - LHS stratification covers every stratum exactly once per dimension and
//     is bit-reproducible for a fixed seed (cross-platform determinism);
//   - every over-budget sample is a typed refusal, never a truncation.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "study/study_sampling.h"

#include "experiment/experiment_matrix.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace sicnu::study;

namespace
{

ParameterDimension dim( const QString &path, double minV, double maxV, int steps )
{
    ParameterDimension d;
    d.parameterPath = path;
    d.minValue = minV;
    d.maxValue = maxV;
    d.stepCount = steps;
    return d;
}

ParameterStudySpec spec( SamplingStrategy strategy, const QVector<ParameterDimension> &dimensions,
                         qint64 maxRuns = 1000, int replicates = 1, quint64 seed = 7 )
{
    ParameterStudySpec s;
    s.studyId = QStringLiteral( "sampler-contract" );
    s.experimentId = QStringLiteral( "exp-sampler" );
    s.algorithmId = QStringLiteral( "rs:threshold_raster" );
    s.strategy = strategy;
    s.dimensions = dimensions;
    s.budget.maxRuns = maxRuns;
    s.budget.maxInFlight = 2;
    s.budget.perRunTimeoutMs = 1000;
    s.budget.seedReplicates = replicates;
    s.budget.seed = seed;
    s.metricNames.append( QStringLiteral( "maskedPercent" ) );
    return s;
}

bool hasCode( const sicnu::data::Result<QVector<StudyPoint>> &result, const QString &code )
{
    if ( result )
        return false;
    for ( const auto &d : result.diagnostics() )
        if ( d.code == code )
            return true;
    return false;
}

} // namespace

TEST_CASE( "dimension ladder is evenly spaced with exact endpoints", "[study][sampling]" )
{
    const auto ladder = dimensionLadder( dim( QStringLiteral( "threshold" ), 0.1, 0.9, 5 ) );
    REQUIRE( ladder.size() == 5 );
    REQUIRE( ladder.front() == Catch::Approx( 0.1 ) );
    REQUIRE( ladder.back() == 0.9 ); // exact by contract
    REQUIRE( ladder.at( 1 ) == Catch::Approx( 0.3 ) );
    REQUIRE( ladder.at( 2 ) == Catch::Approx( 0.5 ) );
    REQUIRE( ladder.at( 3 ) == Catch::Approx( 0.7 ) );
}

TEST_CASE( "canonical value text roundtrips exactly", "[study][sampling]" )
{
    for ( const double value : { 0.1, 0.25, 0.33333333333333331, 5.0, -12.75, 1e-9 } )
        REQUIRE( canonicalValueText( value ).toDouble() == value );
}

TEST_CASE( "grid sampling reproduces matrix authority cells", "[study][sampling][oracle]" )
{
    const auto s = spec( SamplingStrategy::Grid,
                         { dim( QStringLiteral( "threshold" ), 0.2, 0.8, 4 ),
                           dim( QStringLiteral( "cleanupIterations" ), 1.0, 2.0, 2 ) } );
    const auto sampled = sampleStudyPoints( s );
    REQUIRE( sampled.has_value() );
    const auto &points = sampled.value();
    REQUIRE( points.size() == 8 );
    REQUIRE( s.budget.maxRuns >= points.size() );

    // Cross-authority oracle: an independent MatrixDescriptor with the same
    // axes and the SAME identity space must enumerate exactly the same
    // cellIds (with the replicate seed key present in the identity).
    sicnu::experiment::MatrixDescriptor authority;
    authority.matrixId = studyMatrixId( s );
    authority.experimentId = s.experimentId;
    authority.workflowId = QStringLiteral( "study-sampler" );
    for ( const ParameterDimension &d : s.dimensions )
    {
        sicnu::experiment::MatrixAxis axis;
        axis.name = d.parameterPath;
        axis.role = sicnu::experiment::AxisRole::Tag;
        for ( const double value : dimensionLadder( d ) )
            axis.values.append( canonicalValueText( value ) );
        authority.axes.append( axis );
    }
    const auto cells = authority.enumerateCells();
    REQUIRE( cells.has_value() );

    QSet<QString> authorityIds;
    for ( const auto &cell : cells.value() )
    {
        auto assignments = cell.assignments;
        assignments.insert( QStringLiteral( "seed" ), QString::number( s.budget.seed ) );
        authorityIds.insert( sicnu::experiment::matrixCellId( studyMatrixId( s ), assignments ) );
    }
    QSet<QString> pointIds;
    for ( const auto &point : points )
    {
        pointIds.insert( point.pointId );
        REQUIRE( point.assignments.contains( QStringLiteral( "seed" ) ) );
        // Swept parameters carry DOUBLE values in the submitted parameters.
        for ( const ParameterDimension &d : s.dimensions )
        {
            REQUIRE( point.parameters.value( d.parameterPath ).isDouble() );
            REQUIRE( point.assignments.value( d.parameterPath )
                     == canonicalValueText( point.parameters.value( d.parameterPath ).toDouble() ) );
        }
    }
    REQUIRE( pointIds.size() == points.size() ); // unique
    REQUIRE( pointIds == authorityIds );         // identical identity space
}

TEST_CASE( "grid sampling is deterministic across calls", "[study][sampling][replay]" )
{
    const auto s = spec( SamplingStrategy::Grid,
                         { dim( QStringLiteral( "threshold" ), 0.1, 0.9, 5 ) } );
    const auto first = sampleStudyPoints( s );
    const auto second = sampleStudyPoints( s );
    REQUIRE( first.has_value() && second.has_value() );
    REQUIRE( first.value() == second.value() );
}

TEST_CASE( "over-budget grid sampling is refused, never truncated", "[study][sampling][budget]" )
{
    const auto s = spec( SamplingStrategy::Grid,
                         { dim( QStringLiteral( "a" ), 0.0, 1.0, 10 ),
                           dim( QStringLiteral( "b" ), 0.0, 1.0, 10 ) },
                         /*maxRuns=*/50 );
    const auto sampled = sampleStudyPoints( s );
    REQUIRE( !sampled.has_value() ); // 100 points > 50
    REQUIRE( hasCode( sampled, QStringLiteral( "study.budget_exceeded" ) ) );
}

TEST_CASE( "one-at-a-time sampling varies exactly one dimension per point", "[study][sampling][oat]" )
{
    const auto s = spec( SamplingStrategy::OneAtATime,
                         { dim( QStringLiteral( "threshold" ), 0.0, 1.0, 5 ),
                           dim( QStringLiteral( "percentile" ), 80.0, 99.0, 3 ) } );
    const auto sampled = sampleStudyPoints( s );
    REQUIRE( sampled.has_value() );
    const auto &points = sampled.value();
    REQUIRE( points.size() == 1 + 4 + 2 ); // baseline + per-dimension non-reference values

    const auto &baseline = points.at( 0 );
    REQUIRE( baseline.assignments.value( QStringLiteral( "threshold" ) )
             == canonicalValueText( 0.5 ) ); // middle of a 5-ladder
    REQUIRE( baseline.assignments.value( QStringLiteral( "percentile" ) )
             == canonicalValueText( 89.5 ) ); // middle of an odd 3-ladder over [80, 99]

    // Every non-baseline point differs from the baseline in EXACTLY ONE
    // dimension, and every (dimension, non-reference value) pair appears once.
    QHash<QString, QSet<QString>> variedValues;
    for ( const auto &point : points )
    {
        if ( point.pointId == baseline.pointId )
            continue;
        QStringList differing;
        for ( const auto &it : point.assignments.toStdMap() )
        {
            if ( it.first == QStringLiteral( "seed" ) )
                continue;
            if ( point.assignments.value( it.first ) != baseline.assignments.value( it.first ) )
                differing.append( it.first );
        }
        REQUIRE( differing.size() == 1 );
        variedValues[differing.first()].insert(
            point.assignments.value( differing.first() ) );
    }
    // threshold: 5 ladder values minus the 0.5 reference → 4 distinct texts
    REQUIRE( variedValues.value( QStringLiteral( "threshold" ) ).size() == 4 );
    REQUIRE( variedValues.value( QStringLiteral( "percentile" ) ).size() == 2 );
    // 4-ladder reference rule: floor((stepCount-1)/2) = index 1
    const auto even = spec( SamplingStrategy::OneAtATime,
                            { dim( QStringLiteral( "k" ), 2.0, 8.0, 4 ) } );
    const auto evenPoints = sampleStudyPoints( even );
    REQUIRE( evenPoints.has_value() );
    const double refLadderValue = dimensionLadder( even.dimensions.at( 0 ) ).at( 1 );
    REQUIRE( evenPoints.value().at( 0 ).assignments.value( QStringLiteral( "k" ) )
             == canonicalValueText( refLadderValue ) );
    REQUIRE( evenPoints.value().size() == 1 + 3 );
}

TEST_CASE( "latin hypercube covers every stratum exactly once and is seed-reproducible",
           "[study][sampling][lhs]" )
{
    const int n = 12;
    const auto s = spec( SamplingStrategy::LatinHypercube,
                         { dim( QStringLiteral( "threshold" ), 0.0, 1.0, 3 ),
                           dim( QStringLiteral( "statisticalK" ), 1.0, 4.0, 3 ) },
                         /*maxRuns=*/n );
    const auto sampled = sampleStudyPoints( s );
    REQUIRE( sampled.has_value() );
    const auto &points = sampled.value();
    REQUIRE( points.size() == n );

    // Stratum coverage: each dimension hits every stratum exactly once.
    for ( const ParameterDimension &d : s.dimensions )
    {
        QSet<int> strata;
        for ( const auto &point : points )
        {
            const double v = point.parameters.value( d.parameterPath ).toDouble();
            REQUIRE( v >= d.minValue );
            REQUIRE( v <= d.maxValue );
            int stratum = static_cast<int>( std::floor( ( v - d.minValue )
                                                        / ( d.maxValue - d.minValue ) * n ) );
            stratum = std::clamp( stratum, 0, n - 1 );
            strata.insert( stratum );
        }
        REQUIRE( strata.size() == n );
    }

    // Same seed ⇒ identical points (bit-reproducible, cross-platform stream).
    const auto replay = sampleStudyPoints( s );
    REQUIRE( replay.has_value() );
    REQUIRE( replay.value() == points );

    // Different seed ⇒ different sample (the sampler is seeded, not fixed).
    const auto other = sampleStudyPoints(
        spec( SamplingStrategy::LatinHypercube, s.dimensions, n, 1, s.budget.seed + 1 ) );
    REQUIRE( other.has_value() );
    bool differs = false;
    for ( int i = 0; i < n; ++i )
        differs = differs
                  || other.value().at( i ).pointId != points.at( i ).pointId;
    REQUIRE( differs );
}

TEST_CASE( "seed replicates share parameters and differ only in seed identity",
           "[study][sampling][replicates]" )
{
    const auto s = spec( SamplingStrategy::Grid,
                         { dim( QStringLiteral( "threshold" ), 0.1, 0.9, 3 ) },
                         /*maxRuns=*/100, /*replicates=*/3, /*seed=*/11 );
    const auto sampled = sampleStudyPoints( s );
    REQUIRE( sampled.has_value() );
    const auto &points = sampled.value();
    REQUIRE( points.size() == 9 ); // 3 parameter sets × 3 replicates
    for ( int i = 0; i < 3; ++i )
    {
        const auto &base = points.at( i * 3 );
        for ( int r = 1; r < 3; ++r )
        {
            const auto &replicate = points.at( i * 3 + r );
            // Same parameter set…
            REQUIRE( replicate.parameters == base.parameters );
            REQUIRE( replicate.replicateIndex == r );
            // …different identity (the seed is part of it).
            REQUIRE( replicate.pointId != base.pointId );
            REQUIRE( replicate.seed == base.seed + static_cast<quint64>( r ) );
        }
    }
}

TEST_CASE( "study matrix identity is namespaced per study", "[study][sampling]" )
{
    auto s = spec( SamplingStrategy::Grid,
                   { dim( QStringLiteral( "threshold" ), 0.1, 0.9, 3 ) } );
    REQUIRE( studyMatrixId( s ) == QStringLiteral( "study:sampler-contract" ) );
    s.studyId = QStringLiteral( "other" );
    REQUIRE( studyMatrixId( s ) == QStringLiteral( "study:other" ) );
}
