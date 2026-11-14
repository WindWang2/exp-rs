// test_r30_t01_evaluation_limits.cpp — R30-T01 WP01/WP02/WP03: representable
// confusion-matrix totals under multi-cell overflow, strict evaluation input
// shapes with an explicit entries budget, and numerically stable regression
// metrics verified against an independent high-precision oracle.
//
// WP01: each stored cell is individually legal (< 2^53 from JSON, any
// non-negative qint64 from setCount), yet 1025 cells of 2^53-1 overflow the
// qint64 grand total (2^63 + 2^53 - 1025 > INT64_MAX). The wrapped negative
// total used to make overallAccuracy() report 0.0 for a matrix whose diagonal
// carries every sample — a misleading "perfectly wrong" score.
//
// WP03 oracle provenance: expected values below were computed exactly with
// Python fractions/decimal (see .planning/r30-t01-science/ORACLE_WP03.md) and
// transcribed as literals; tolerances are Catch::Approx relative.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "experiment/evaluation.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <utility>
#include <cmath>
#include <limits>
#include <vector>

using namespace sicnu::experiment;

namespace
{

constexpr qint64 kLargestExactCount = 9007199254740991; // 2^53 - 1

ConfusionMatrix namedMatrix( const QStringList &labels )
{
    return ConfusionMatrix( labels, labels.size() );
}

QStringList labelRange( int n )
{
    QStringList labels;
    labels.reserve( n );
    for ( int i = 0; i < n; ++i )
        labels << QStringLiteral( "c%1" ).arg( i );
    return labels;
}

QJsonObject sparseDoc( const QStringList &labels,
                       const std::vector<std::array<double, 3>> &cells )
{
    QJsonObject json;
    json.insert( QStringLiteral( "labels" ), QJsonArray::fromStringList( labels ) );
    QJsonArray nonZero;
    for ( const auto &cell : cells )
    {
        QJsonObject entry;
        entry.insert( QStringLiteral( "r" ), cell[0] );
        entry.insert( QStringLiteral( "c" ), cell[1] );
        entry.insert( QStringLiteral( "n" ), cell[2] );
        nonZero.append( entry );
    }
    json.insert( QStringLiteral( "counts_format" ), QStringLiteral( "sparse" ) );
    json.insert( QStringLiteral( "nonzero_counts" ), nonZero );
    return json;
}

/// The overflow fixture: `cells` DISTINCT nonzero cells of 2^53-1 over a
/// 33-label grid, `diagonalCells` of them on the diagonal (the rest filled
/// row-major off the diagonal). The mathematical grand total is
/// cells * (2^53-1) which exceeds INT64_MAX once cells >= 1025
/// (1025 * (2^53-1) = 2^63 + 2^53 - 1025). Every row/column carries 33
/// cells at the full fixture size (33 * (2^53-1) each — representable).
ConfusionMatrix overflowMatrix( int cells, int diagonalCells )
{
    const QStringList labels = labelRange( 33 );
    ConfusionMatrix matrix = namedMatrix( labels );
    int placed = 0;
    for ( int i = 0; i < diagonalCells; ++i, ++placed )
        matrix.setCount( i, i, kLargestExactCount );
    for ( int r = 0; r < 33 && placed < cells; ++r )
        for ( int c = 0; c < 33 && placed < cells; ++c )
            if ( r != c )
            {
                matrix.setCount( r, c, kLargestExactCount );
                ++placed;
            }
    return matrix;
}

/// The same distinct cell list the fixture fills, for callers that rebuild
/// the matrix in another order or format.
std::vector<std::pair<int, int>> overflowCells( int cells, int diagonalCells )
{
    std::vector<std::pair<int, int>> list;
    for ( int i = 0; i < diagonalCells; ++i )
        list.push_back( { i, i } );
    for ( int r = 0; r < 33 && int( list.size() ) < cells; ++r )
        for ( int c = 0; c < 33 && int( list.size() ) < cells; ++c )
            if ( r != c )
                list.push_back( { r, c } );
    return list;
}

} // namespace

// --- WP01: total representability ---------------------------------------------

TEST_CASE( "multi-cell grand-total overflow no longer scores a correct matrix as 0",
           "[experiment][metrics][r30t01]" )
{
    // 1025 cells of 2^53-1: grand = 2^63 + 2^53 - 1025 > INT64_MAX. 33 of the
    // cells sit on the diagonal, so overall accuracy is exactly 33/1025.
    const ConfusionMatrix matrix = overflowMatrix( 1025, 33 );

    // The primary RED at base: the wrapped negative grand total made
    // safeDivide return 0.0 — reading "0% accuracy".
    CHECK( matrix.overallAccuracy() == Catch::Approx( 33.0 / 1025.0 ) );

    // The qint64 projection saturates instead of wrapping negative…
    CHECK( matrix.total() == std::numeric_limits<qint64>::max() );
    // …and the saturation is observable, not silent.
    CHECK( matrix.totalsSaturated() );
    // Per-row/column totals still fit and stay exact — saturation is per
    // total, not a blanket clamp. Row 0: 1 diagonal + 32 off-diagonal cells
    // (33); column 7: diagonal + off-diagonal rows 0..30 except row 7 (the
    // row-major fill reached rows 0..30) = 31.
    CHECK( matrix.truthTotal( 0 ) == 33 * kLargestExactCount );
    CHECK( matrix.predictedTotal( 7 ) == 31 * kLargestExactCount );

    // Derived aggregates stay finite and honest (no NaN, no wrapped values).
    CHECK( std::isfinite( matrix.kappa() ) );
    CHECK( std::isfinite( matrix.mcc() ) );
    CHECK( std::isfinite( matrix.macroF1() ) );
    CHECK( std::isfinite( matrix.weightedF1() ) );
}

TEST_CASE( "totals exactly at the qint64 boundary stay exact and unsaturated",
           "[experiment][metrics][r30t01]" )
{
    // 1024 * (2^53-1) = 2^63 - 1024 <= INT64_MAX: the largest all-exact-count
    // matrix that still fits — the boundary must not saturate early.
    const ConfusionMatrix matrix = overflowMatrix( 1024, 32 );
    const qint64 expectedTotal = 1024 * kLargestExactCount;
    CHECK( matrix.total() == expectedTotal );
    CHECK_FALSE( matrix.totalsSaturated() );
    CHECK( matrix.overallAccuracy() == Catch::Approx( 32.0 / 1024.0 ) );
    CHECK( matrix.truthTotal( 0 ) == 33 * kLargestExactCount ); // 1 diag + 32 off
}

TEST_CASE( "one cell past the boundary saturates with an exact score",
           "[experiment][metrics][r30t01]" )
{
    const ConfusionMatrix matrix = overflowMatrix( 1025, 32 );
    CHECK( matrix.total() == std::numeric_limits<qint64>::max() );
    CHECK( matrix.totalsSaturated() );
    // 32 correct of 1025 classified samples — still exactly representable as
    // a ratio even though the qint64 total is not.
    CHECK( matrix.overallAccuracy() == Catch::Approx( 32.0 / 1025.0 ) );
}

TEST_CASE( "overflowed totals are insertion-order independent",
           "[experiment][metrics][r30t01]" )
{
    // Same logical matrix, inserted in a different order: the wide total and
    // every derived score must be identical (the wrapped qint64 total also
    // happened to be order independent — the ratios were not).
    const QStringList labels = labelRange( 33 );
    const std::vector<std::pair<int, int>> cells = overflowCells( 1025, 33 );
    REQUIRE( cells.size() == 1025 );
    ConfusionMatrix forward = namedMatrix( labels );
    ConfusionMatrix reverse = namedMatrix( labels );
    for ( const auto &[r, c] : cells )
        forward.setCount( r, c, kLargestExactCount );
    for ( auto it = cells.rbegin(); it != cells.rend(); ++it )
        reverse.setCount( it->first, it->second, kLargestExactCount );
    CHECK( forward.total() == reverse.total() );
    CHECK( forward.totalsSaturated() == reverse.totalsSaturated() );
    CHECK( forward.overallAccuracy() == Catch::Approx( reverse.overallAccuracy() ) );
    CHECK( forward.overallAccuracy() == Catch::Approx( 33.0 / 1025.0 ) );
}

TEST_CASE( "sparse and dense documents of the same overflowing matrix agree",
           "[experiment][metrics][r30t01]" )
{
    const QStringList labels = labelRange( 33 );
    const std::vector<std::pair<int, int>> cellList = overflowCells( 1025, 33 );
    std::vector<std::array<double, 3>> cells;
    for ( const auto &[r, c] : cellList )
        cells.push_back( { double( r ), double( c ), double( kLargestExactCount ) } );
    const auto sparse = ConfusionMatrix::fromJson( sparseDoc( labels, cells ) );
    REQUIRE( sparse.has_value() );
    QJsonObject denseJson;
    denseJson.insert( QStringLiteral( "labels" ), QJsonArray::fromStringList( labels ) );
    QJsonArray rows;
    for ( int r = 0; r < 33; ++r )
    {
        QJsonArray row;
        for ( int c = 0; c < 33; ++c )
        {
            const bool present = std::find( cellList.begin(), cellList.end(),
                                            std::make_pair( r, c ) ) != cellList.end();
            row.append( double( present ? kLargestExactCount : 0 ) );
        }
        rows.append( row );
    }
    denseJson.insert( QStringLiteral( "counts" ), rows );
    const auto dense = ConfusionMatrix::fromJson( denseJson );
    REQUIRE( dense.has_value() );

    CHECK( sparse->total() == dense->total() );
    CHECK( sparse->totalsSaturated() == dense->totalsSaturated() );
    CHECK( sparse->overallAccuracy() == Catch::Approx( dense->overallAccuracy() ) );
    CHECK( sparse->overallAccuracy() == Catch::Approx( 33.0 / 1025.0 ) );
    CHECK( sparse->kappa() == Catch::Approx( dense->kappa() ) );
    CHECK( sparse->mcc() == Catch::Approx( dense->mcc() ) );
}

TEST_CASE( "toJson marks saturated totals and saturates per-class support",
           "[experiment][metrics][r30t01]" )
{
    const ConfusionMatrix matrix = overflowMatrix( 1025, 33 );
    const QJsonObject json = matrix.toJson();

    // Additive marker: present ONLY when a qint64 projection saturated.
    CHECK( json.value( QStringLiteral( "totals_saturated" ) ).toBool() == true );
    // Scores in the document come from the wide sums (33/1025, not 0).
    CHECK( json.value( QStringLiteral( "overall_accuracy" ) ).toDouble()
           == Catch::Approx( 33.0 / 1025.0 ) );

    // Per-class supports in THIS matrix fit qint64 (32 cells/row): no
    // support_saturated marker, and zero_support still absent.
    const QJsonArray perClass = json.value( QStringLiteral( "per_class" ) ).toArray();
    REQUIRE( perClass.size() == 33 );
    const QJsonObject first = perClass.at( 0 ).toObject();
    CHECK( first.value( QStringLiteral( "support" ) ).toInteger() == 33 * kLargestExactCount );
    CHECK_FALSE( first.contains( QStringLiteral( "support_saturated" ) ) );
    CHECK_FALSE( first.contains( QStringLiteral( "zero_support" ) ) );

    // A row total CAN overflow on a wide taxonomy: 1100 labels with 1025
    // filled cells in row 0 (values 2^53-1) — row 0's support alone is past
    // INT64_MAX. Sparse emission (labels > 1024) still carries the markers.
    const QStringList wide = labelRange( 1100 );
    ConfusionMatrix rowOverflow = namedMatrix( wide );
    for ( int c = 0; c < 1025; ++c )
        rowOverflow.setCount( 0, c, kLargestExactCount );
    const QJsonObject wideJson = rowOverflow.toJson();
    CHECK( wideJson.value( QStringLiteral( "counts_format" ) ).toString()
           == QLatin1String( "sparse" ) );
    CHECK( wideJson.value( QStringLiteral( "totals_saturated" ) ).toBool() == true );
    const QJsonObject row0 = wideJson.value( QStringLiteral( "per_class" ) )
                                 .toArray()
                                 .at( 0 )
                                 .toObject();
    CHECK( row0.value( QStringLiteral( "support" ) ).toInteger()
           == std::numeric_limits<qint64>::max() );
    CHECK( row0.value( QStringLiteral( "support_saturated" ) ).toBool() == true );
    const auto metrics = rowOverflow.perClassAll();
    REQUIRE( metrics.size() == 1100 );
    CHECK( metrics[0].support == std::numeric_limits<qint64>::max() );
    CHECK( metrics[0].supportSaturated );
    CHECK_FALSE( metrics[0].zeroSupport );
    CHECK( metrics[1].zeroSupport ); // untouched rows: zero support stays exact

    // A representable matrix emits no marker at all (stable documents).
    const ConfusionMatrix small = overflowMatrix( 1024, 32 );
    CHECK_FALSE( small.toJson().contains( QStringLiteral( "totals_saturated" ) ) );
    CHECK_FALSE( small.toJson()
                     .value( QStringLiteral( "per_class" ) )
                     .toArray()
                     .at( 0 )
                     .toObject()
                     .contains( QStringLiteral( "support_saturated" ) ) );
}

TEST_CASE( "saturated documents round-trip and re-emit the marker",
           "[experiment][metrics][r30t01]" )
{
    const ConfusionMatrix matrix = overflowMatrix( 1025, 33 );
    const QJsonObject json = matrix.toJson();
    const auto reparsed = ConfusionMatrix::fromJson( json );
    REQUIRE( reparsed.has_value() );
    CHECK( *reparsed == matrix );
    CHECK( reparsed->totalsSaturated() );
    CHECK( reparsed->toJson()
               .value( QStringLiteral( "totals_saturated" ) )
               .toBool() == true );
}

// --- WP02: input shapes and the entries budget ----------------------------------

TEST_CASE( "a non-array nonzero_counts payload is refused, not read as empty",
           "[experiment][metrics][r30t01]" )
{
    // Base behavior: contains("nonzero_counts") is true, toArray() on a
    // string returns an empty array and the document parsed as a SUCCESSFUL
    // empty matrix — a torn export read as "no samples".
    const QStringList labels{ QStringLiteral( "a" ), QStringLiteral( "b" ) };
    QJsonObject json = sparseDoc( labels, { { 0, 0, 3 } } );
    json.insert( QStringLiteral( "nonzero_counts" ), QStringLiteral( "0,0,3" ) );
    const auto parsed = ConfusionMatrix::fromJson( json );
    CHECK_FALSE( parsed.has_value() );
}

TEST_CASE( "labels must be an array of strings",
           "[experiment][metrics][r30t01]" )
{
    const QStringList labels{ QStringLiteral( "a" ), QStringLiteral( "b" ) };
    SECTION( "labels as a number" )
    {
        QJsonObject json = sparseDoc( labels, { { 0, 0, 1 } } );
        json.insert( QStringLiteral( "labels" ), 5 );
        CHECK_FALSE( ConfusionMatrix::fromJson( json ).has_value() );
    }
    SECTION( "numeric label entries are refused instead of stringified" )
    {
        // Base behavior: toVariant().toStringList() silently turned 7 into
        // "7" — a hand-edited document was reinterpreted, not refused.
        QJsonObject json = sparseDoc( labels, { { 0, 0, 1 } } );
        QJsonArray numeric;
        numeric.append( 7 );
        numeric.append( 8 );
        json.insert( QStringLiteral( "labels" ), numeric );
        CHECK_FALSE( ConfusionMatrix::fromJson( json ).has_value() );
    }
    SECTION( "a counts_format=sparse declaration without sparse cells refuses" )
    {
        // The format declares sparse but the payload is a dense grid — the
        // document contradicts itself; refuse rather than guess.
        QJsonObject json = sparseDoc( labels, { { 0, 0, 1 } } );
        json.remove( QStringLiteral( "nonzero_counts" ) );
        QJsonArray dense;
        dense.append( QJsonArray{ 1, 0 } );
        dense.append( QJsonArray{ 0, 1 } );
        json.insert( QStringLiteral( "counts" ), dense );
        CHECK_FALSE( ConfusionMatrix::fromJson( json ).has_value() );
    }
}

TEST_CASE( "small zero-valued sparse entries stay legal and duplicate zeros still refuse",
           "[experiment][metrics][r30t01]" )
{
    const QStringList labels{ QStringLiteral( "a" ), QStringLiteral( "b" ) };
    // Zero entries (declared but contributing nothing) round-trip; the R29
    // duplicate-zero refusal keeps working alongside the new budget.
    const QJsonObject json = sparseDoc( labels, { { 0, 0, 0 }, { 1, 1, 4 } } );
    const auto parsed = ConfusionMatrix::fromJson( json );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->total() == 4 );

    const QJsonObject dup = sparseDoc( labels, { { 0, 0, 0 }, { 0, 0, 0 } } );
    CHECK_FALSE( ConfusionMatrix::fromJson( dup ).has_value() );
}

TEST_CASE( "the sparse entries budget is a named, documented constant",
           "[experiment][metrics][r30t01]" )
{
    // The budget bounds ALL declared entries (zeros included) — distinct from
    // the 4 Mi NONZERO-cell storage budget. Documented value asserted so a
    // silent change breaks this test.
    CHECK( ConfusionMatrix::kMaxSparseEntries == 8388608 );
    CHECK( ConfusionMatrix::kMaxStoredCells == 4194304 );
}

// --- WP03: regression stability ---------------------------------------------------

TEST_CASE( "extreme finite pairs report unrepresentable metrics, never inf-under-success",
           "[experiment][metrics][r30t01]" )
{
    // truth=+1e308, pred=-1e308: both finite, |diff| = 2e308 > DBL_MAX. The
    // true MAE/RMSE/|bias| exceed double range; MSE is 4e616. R^2: constant
    // truth → 0 by policy. MAPE: |2e308/1e308| = 2 → 200%.
    const QVector<double> truth{ 1e308 };
    const QVector<double> predicted{ -1e308 };
    const auto result = regressionMetrics( truth, predicted );
    REQUIRE( result.has_value() );
    const RegressionMetrics m = result.value();

    CHECK( m.count == 1 );
    CHECK( m.mapeDefined );
    CHECK( m.mape == Catch::Approx( 200.0 ) );
    CHECK( m.r2 == 0.0 ); // constant-truth policy, unchanged
    // The over-range metrics carry the explicit not-representable state…
    CHECK_FALSE( m.representable );
    CHECK( std::isinf( m.mae ) );
    CHECK( std::isinf( m.mse ) );
    CHECK( std::isinf( m.rmse ) );
    CHECK( std::isinf( m.bias ) );
    // …and the JSON projection marks them (nulls) with the marker key.
    const QJsonObject json = m.toJson();
    CHECK( json.value( QStringLiteral( "representable" ) ).toBool() == false );
    CHECK( json.value( QStringLiteral( "mae" ) ).isNull() );
    CHECK( json.value( QStringLiteral( "mse" ) ).isNull() );
    CHECK( json.value( QStringLiteral( "rmse" ) ).isNull() );
    CHECK( json.value( QStringLiteral( "bias" ) ).isNull() );
    CHECK( json.value( QStringLiteral( "r2" ) ).toDouble() == 0.0 );
    CHECK( json.value( QStringLiteral( "mape" ) ).toDouble() == Catch::Approx( 200.0 ) );
}

TEST_CASE( "±1e200 truth against zero prediction keeps a representable R² and RMSE",
           "[experiment][metrics][r30t01]" )
{
    // Base behavior: squaredSum and sst both overflowed double to inf and
    // r2 = 1 - inf/inf = NaN (serialized as null under success). The true
    // values: MAE = RMSE = 1e200, MSE = 1e400 (unrepresentable), bias = 0,
    // R² = 1 - SSE/SST = 0 exactly (SSE == SST here).
    const QVector<double> truth{ -1e200, 1e200, -1e200, 1e200 };
    const QVector<double> predicted{ 0.0, 0.0, 0.0, 0.0 };
    const auto result = regressionMetrics( truth, predicted );
    REQUIRE( result.has_value() );
    const RegressionMetrics m = result.value();

    CHECK( m.count == 4 );
    CHECK( m.mae == Catch::Approx( 1e200 ) );
    CHECK( m.rmse == Catch::Approx( 1e200 ) );
    CHECK( m.bias == 0.0 );
    CHECK( m.r2 == 0.0 );
    CHECK( m.mapeDefined );
    CHECK( m.mape == Catch::Approx( 100.0 ) );
    // MSE alone overflows: per-metric honesty, the set carries the marker.
    CHECK( std::isinf( m.mse ) );
    CHECK_FALSE( m.representable );
    const QJsonObject json = m.toJson();
    CHECK( json.value( QStringLiteral( "mse" ) ).isNull() );
    CHECK( json.value( QStringLiteral( "rmse" ) ).toDouble() == Catch::Approx( 1e200 ) );
}

TEST_CASE( "catastrophic cancellation in bias is compensated",
           "[experiment][metrics][r30t01]" )
{
    // Signed sum of [1e16, 1, 1, -1e16]: plain double accumulation loses the
    // small terms entirely (1e16 + 1 rounds back to 1e16) and reports bias
    // 0.0; the true mean error is -0.5 (oracle: exact rational arithmetic —
    // diffs are -1e16, -1, -1, +1e16 against zero predictions).
    const QVector<double> truth{ 1e16, 1.0, 1.0, -1e16 };
    const QVector<double> predicted{ 0.0, 0.0, 0.0, 0.0 };
    const auto result = regressionMetrics( truth, predicted );
    REQUIRE( result.has_value() );
    const RegressionMetrics m = result.value();
    CHECK( m.bias == Catch::Approx( -0.5 ) );
    CHECK( m.mae == Catch::Approx( 5e15 + 0.5 ) );
    CHECK( m.representable );

    // The compensated truth mean is 0.5, so SST = 2e32 + 1 and SSE = 2e32 + 2
    // (oracle: exact rational arithmetic) — R² is a tiny NEGATIVE number
    // (−1/(2^32+1) ≈ −5e-33). In the 64-bit long-double significand the
    // SSE/SST ratio rounds to exactly 1 (the difference sits 2^-64 below
    // the leading bit), so the computed R² is 0.0 — never a false positive
    // and never the NaN the double-only path produced. Accept [−tiny, 0].
    CHECK( m.r2 <= 0.0 );
    CHECK( std::fabs( m.r2 ) < 1e-30 );
}

TEST_CASE( "single-sample and near-constant finite regressions stay exact",
           "[experiment][metrics][r30t01]" )
{
    SECTION( "single sample" )
    {
        const auto result = regressionMetrics( { 3.0 }, { 5.0 } );
        REQUIRE( result.has_value() );
        const RegressionMetrics m = result.value();
        CHECK( m.count == 1 );
        CHECK( m.mae == 2.0 );
        CHECK( m.mse == 4.0 );
        CHECK( m.rmse == 2.0 );
        CHECK( m.bias == 2.0 );
        CHECK( m.r2 == 0.0 ); // constant truth
        CHECK( m.mape == Catch::Approx( ( 2.0 / 3.0 ) * 100.0 ) ); // |2/3|*100
        CHECK( m.representable );
    }
    SECTION( "near-constant truth" )
    {
        // truth varies by one ulp of 1.0: SST is tiny but nonzero, R² is a
        // large negative number — finite and representable.
        const QVector<double> truth{ 1.0, 1.0 + std::numeric_limits<double>::epsilon(),
                                     1.0, 1.0 };
        const QVector<double> predicted{ 1.0, 1.0, 1.0, 1.0 };
        const auto result = regressionMetrics( truth, predicted );
        REQUIRE( result.has_value() );
        const RegressionMetrics m = result.value();
        CHECK( std::isfinite( m.r2 ) );
        // One sample off by one ulp vs a 3/4-ulp population variance:
        // SSE = eps^2, SST = 3/4 eps^2 → R² = 1 - 4/3 = -1/3 (exact-rational
        // oracle). Welford's mean increment (eps/3 ≈ 2^-53.6) keeps only
        // ~10 significand bits in the 64-bit long-double mantissa, so SST
        // carries a ~2^-10-relative epsilon-scale rounding envelope — assert
        // the oracle with that documented tolerance (base code reported 0.0
        // or NaN here; measured deviation is 1.6e-4).
        CHECK( m.r2 == Catch::Approx( -1.0 / 3.0 ).epsilon( 0.001 ) );
        CHECK( m.representable );
    }
    SECTION( "non-finite skipping keeps its counts" )
    {
        const QVector<double> truth{ 1.0, std::numeric_limits<double>::quiet_NaN(), 3.0 };
        const QVector<double> predicted{ 2.0, 4.0, std::numeric_limits<double>::infinity() };
        const auto result = regressionMetrics( truth, predicted );
        REQUIRE( result.has_value() );
        const RegressionMetrics m = result.value();
        CHECK( m.nonFiniteSkipped == 2 );
        CHECK( m.count == 1 );
        CHECK( m.mae == 1.0 );
        CHECK( m.representable );
    }
}
