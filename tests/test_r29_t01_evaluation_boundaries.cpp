// test_r29_t01_evaluation_boundaries.cpp — R29-T01 WP02/WP03: consistent
// read/mutation contracts for the sparse ConfusionMatrix and single-pass
// aggregation. Key alias: with n=2, count(0,2) hashed onto the same flat
// key as the legal count(1,0); mutations already refused out-of-grid
// indices, reads must agree. Serialization contract: mixed dense/sparse
// documents, unknown counts_format tokens, duplicate sparse cells, duplicate
// label codes and counts above the JSON double-exactness boundary (2^53)
// are refused typed instead of silently reinterpreted. Aggregation: every
// metric in one toJson document matches an independently computed oracle.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "experiment/evaluation.h"

#include <QJsonArray>
#include <QJsonObject>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace sicnu::experiment;

namespace
{

ConfusionMatrix namedMatrix( const QStringList &labels )
{
    return ConfusionMatrix( labels, labels.size() );
}

/// Sparse document builder (mirrors toJson's sparse emission shape). The
/// cell payload is double-typed: r/c/n are JSON numbers, and the boundary
/// cases carry exact 2^53-scale doubles that must not narrow.
QJsonObject sparseDoc( const QStringList &labels, const std::vector<std::array<double, 3>> &cells )
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

} // namespace

TEST_CASE( "out-of-grid reads return zero instead of aliasing legal cells",
           "[experiment][metrics][r29t01]" )
{
    const QStringList labels{ QStringLiteral( "water" ), QStringLiteral( "forest" ) };
    ConfusionMatrix matrix = namedMatrix( labels );
    // (truth=1, pred=0) = 5 — the cell count(0,2) used to alias (n=2).
    matrix.setCount( 1, 0, 5 );
    REQUIRE( matrix.count( 1, 0 ) == 5 );
    // The aliased reads: 0*2+2 == 1*2+0 == 2 in the old flat-key math.
    CHECK( matrix.count( 0, 2 ) == 0 );
    CHECK( matrix.count( 2, 0 ) == 0 );
    // Negative indices, too.
    CHECK( matrix.count( -1, 0 ) == 0 );
    CHECK( matrix.count( 0, -1 ) == 0 );
    // The out-of-grid reads never polluted the totals.
    CHECK( matrix.total() == 5 );
    CHECK( matrix.truthTotal( 1 ) == 5 );
    CHECK( matrix.truthTotal( 7 ) == 0 );
    CHECK( matrix.predictedTotal( 0 ) == 5 );
    CHECK( matrix.predictedTotal( 9 ) == 0 );
}

TEST_CASE( "mutations refuse negative setCount and saturate increment at qint64 max",
           "[experiment][metrics][r29t01]" )
{
    const QStringList labels{ QStringLiteral( "a" ) };
    ConfusionMatrix matrix = namedMatrix( labels );
    matrix.setCount( 0, 0, -3 );
    CHECK( matrix.count( 0, 0 ) == 0 );
    CHECK( matrix.total() == 0 );

    matrix.setCount( 0, 0, std::numeric_limits<qint64>::max() );
    REQUIRE( matrix.count( 0, 0 ) == std::numeric_limits<qint64>::max() );
    // ++ at the maximum must not wrap negative (signed overflow).
    matrix.increment( 0, 0 );
    CHECK( matrix.count( 0, 0 ) == std::numeric_limits<qint64>::max() );
    CHECK( matrix.total() == std::numeric_limits<qint64>::max() );
    // A fresh cell still increments normally.
    const QStringList pair{ QStringLiteral( "a" ), QStringLiteral( "b" ) };
    ConfusionMatrix fresh = namedMatrix( pair );
    fresh.increment( 1, 1 );
    CHECK( fresh.count( 1, 1 ) == 1 );
}

TEST_CASE( "an empty default-constructed matrix reports zeros, never NaN",
           "[experiment][metrics][r29t01]" )
{
    // Reachability check for the suspected zero-label division: with no
    // labels and no counts, matrixSums never enters its loop, so every
    // aggregate is defined (safeDivide's zero-denominator policy → 0).
    const ConfusionMatrix matrix;
    CHECK( matrix.size() == 0 );
    CHECK( matrix.total() == 0 );
    CHECK( matrix.count( 0, 0 ) == 0 );
    CHECK( matrix.overallAccuracy() == 0.0 );
    CHECK( matrix.balancedAccuracy() == 0.0 );
    CHECK( matrix.kappa() == 0.0 );
    CHECK( matrix.mcc() == 0.0 );
    CHECK( matrix.macroF1() == 0.0 );
    CHECK( matrix.macroIoU() == 0.0 );
    CHECK( matrix.weightedF1() == 0.0 );
    CHECK( matrix.perClassAll().isEmpty() );
    // The empty document still serializes deterministically and re-reads.
    const QJsonObject json = matrix.toJson();
    const auto reparsed = ConfusionMatrix::fromJson( json );
    // An all-zero matrix emits no labels → the reader's "needs labels"
    // refusal applies; the point of this check is that toJson itself is
    // total (no NaN, no throw) on the empty matrix.
    CHECK( json.contains( QStringLiteral( "counts" ) ) );
    CHECK( std::isfinite( json.value( QStringLiteral( "macro_f1" ) ).toDouble() ) );
    CHECK( std::isfinite( json.value( QStringLiteral( "mcc" ) ).toDouble() ) );
    CHECK( std::isfinite( json.value( QStringLiteral( "weighted_f1" ) ).toDouble() ) );
    (void)reparsed;
}

TEST_CASE( "serialization refuses mixed formats, foreign tokens, duplicate cells "
           "and duplicate label codes",
           "[experiment][metrics][r29t01]" )
{
    const QStringList labels{ QStringLiteral( "a" ), QStringLiteral( "b" ) };

    SECTION( "mixed dense + sparse payload" )
    {
        QJsonObject json = sparseDoc( labels, { { 0, 0, 1 } } );
        QJsonArray dense;
        dense.append( QJsonArray{ 1, 0 } );
        dense.append( QJsonArray{ 0, 1 } );
        json.insert( QStringLiteral( "counts" ), dense );
        CHECK_FALSE( ConfusionMatrix::fromJson( json ).has_value() );
    }
    SECTION( "unknown counts_format token" )
    {
        QJsonObject json = sparseDoc( labels, { { 0, 0, 1 } } );
        json.insert( QStringLiteral( "counts_format" ), QStringLiteral( "sparse-v3" ) );
        CHECK_FALSE( ConfusionMatrix::fromJson( json ).has_value() );
    }
    SECTION( "duplicate sparse cell" )
    {
        const QJsonObject json = sparseDoc( labels, { { 0, 0, 1 }, { 0, 0, 2 } } );
        CHECK_FALSE( ConfusionMatrix::fromJson( json ).has_value() );
    }
    SECTION( "duplicate label codes" )
    {
        QStringList dup{ QStringLiteral( "a" ), QStringLiteral( "a" ) };
        const QJsonObject json = sparseDoc( dup, { { 0, 0, 1 } } );
        CHECK_FALSE( ConfusionMatrix::fromJson( json ).has_value() );
    }
    SECTION( "dense count above the double exactness boundary" )
    {
        QJsonObject json;
        json.insert( QStringLiteral( "labels" ), QJsonArray::fromStringList( labels ) );
        QJsonArray dense;
        dense.append( QJsonArray{ 9007199254740992.0, 0.0 } ); // 2^53 exactly
        dense.append( QJsonArray{ 0.0, 1.0 } );
        json.insert( QStringLiteral( "counts" ), dense );
        CHECK_FALSE( ConfusionMatrix::fromJson( json ).has_value() );
    }
    SECTION( "sparse count above the double exactness boundary" )
    {
        const QJsonObject json = sparseDoc( labels, { { 0, 0, 9007199254740992.0 } } );
        CHECK_FALSE( ConfusionMatrix::fromJson( json ).has_value() );
    }
    SECTION( "the largest exact count still round-trips" )
    {
        const qint64 largestExact = 9007199254740991; // 2^53 - 1
        const QJsonObject json = sparseDoc( labels, { { 0, 1, double( largestExact ) } } );
        const auto parsed = ConfusionMatrix::fromJson( json );
        REQUIRE( parsed.has_value() );
        CHECK( parsed->count( 0, 1 ) == largestExact );
        CHECK( parsed->total() == largestExact );
    }
}

TEST_CASE( "one-pass toJson matches the public metric methods and an independent oracle",
           "[experiment][metrics][r29t01]" )
{
    // A 3-class matrix with asymmetric confusions, built to exercise
    // support-skewed macro means and the weighted aggregate.
    const QStringList labels{ QStringLiteral( "water" ), QStringLiteral( "forest" ),
                              QStringLiteral( "urban" ) };
    ConfusionMatrix matrix = namedMatrix( labels );
    matrix.setCount( 0, 0, 40 );
    matrix.setCount( 0, 1, 10 );
    matrix.setCount( 1, 0, 5 );
    matrix.setCount( 1, 1, 45 );
    matrix.setCount( 1, 2, 5 );
    matrix.setCount( 2, 2, 20 );
    matrix.setCount( 2, 0, 2 );

    const QJsonObject json = matrix.toJson();

    // The document agrees with the public methods (single-sourced formulas).
    CHECK( json.value( QStringLiteral( "overall_accuracy" ) ).toDouble()
           == Catch::Approx( matrix.overallAccuracy() ) );
    CHECK( json.value( QStringLiteral( "balanced_accuracy" ) ).toDouble()
           == Catch::Approx( matrix.balancedAccuracy() ) );
    CHECK( json.value( QStringLiteral( "kappa" ) ).toDouble() == Catch::Approx( matrix.kappa() ) );
    CHECK( json.value( QStringLiteral( "mcc" ) ).toDouble() == Catch::Approx( matrix.mcc() ) );
    CHECK( json.value( QStringLiteral( "macro_f1" ) ).toDouble()
           == Catch::Approx( matrix.macroF1() ) );
    CHECK( json.value( QStringLiteral( "macro_iou" ) ).toDouble()
           == Catch::Approx( matrix.macroIoU() ) );
    CHECK( json.value( QStringLiteral( "weighted_f1" ) ).toDouble()
           == Catch::Approx( matrix.weightedF1() ) );

    // Independent oracle: totals straight from the cells, per-class metrics
    // by hand. grand = 40+10+5+45+5+20+2 = 127.
    const qint64 grand = 127;
    REQUIRE( matrix.total() == grand );
    // class water: tp=40, truth=50, pred=47 → P=40/47, R=0.8,
    // IoU=40/(47+50-40)=40/57.
    const QJsonArray perClass = json.value( QStringLiteral( "per_class" ) ).toArray();
    REQUIRE( perClass.size() == 3 );
    CHECK( perClass.at( 0 ).toObject().value( QStringLiteral( "support" ) ).toInteger() == 50 );
    CHECK( perClass.at( 0 ).toObject().value( QStringLiteral( "precision" ) ).toDouble()
           == Catch::Approx( 40.0 / 47.0 ) );
    CHECK( perClass.at( 0 ).toObject().value( QStringLiteral( "recall" ) ).toDouble()
           == Catch::Approx( 0.8 ) );
    CHECK( perClass.at( 0 ).toObject().value( QStringLiteral( "iou" ) ).toDouble()
           == Catch::Approx( 40.0 / 57.0 ) );
    // class urban: tp=20, truth=22, pred=25.
    CHECK( perClass.at( 2 ).toObject().value( QStringLiteral( "precision" ) ).toDouble()
           == Catch::Approx( 20.0 / 25.0 ) );
    CHECK( perClass.at( 2 ).toObject().value( QStringLiteral( "recall" ) ).toDouble()
           == Catch::Approx( 20.0 / 22.0 ) );
    // overall = (40+45+20)/127.
    CHECK( json.value( QStringLiteral( "overall_accuracy" ) ).toDouble()
           == Catch::Approx( 105.0 / 127.0 ) );

    // Round-trip: the document re-reads to the same matrix (== covers labels
    // and the sparse store) and re-emits identical aggregates.
    const auto reparsed = ConfusionMatrix::fromJson( json );
    REQUIRE( reparsed.has_value() );
    CHECK( *reparsed == matrix );
    const QJsonObject json2 = reparsed->toJson();
    CHECK( json2.value( QStringLiteral( "kappa" ) ).toDouble()
           == Catch::Approx( json.value( QStringLiteral( "kappa" ) ).toDouble() ) );
    CHECK( json2.value( QStringLiteral( "mcc" ) ).toDouble()
           == Catch::Approx( json.value( QStringLiteral( "mcc" ) ).toDouble() ) );
    CHECK( json2.value( QStringLiteral( "weighted_f1" ) ).toDouble()
           == Catch::Approx( json.value( QStringLiteral( "weighted_f1" ) ).toDouble() ) );
}

TEST_CASE( "sparse documents without a counts_format field still read (legacy shape)",
           "[experiment][metrics][r29t01]" )
{
  // The sparse emission always stamps counts_format, but a legacy/hand
  // document without the token keeps reading: absence means "no
  // declaration", only a WRONG declaration refuses.
  const QStringList labels{ QStringLiteral( "a" ), QStringLiteral( "b" ) };
  QJsonObject json = sparseDoc( labels, { { 0, 0, 3 }, { 1, 1, 4 } } );
  json.remove( QStringLiteral( "counts_format" ) );
  const auto parsed = ConfusionMatrix::fromJson( json );
  REQUIRE( parsed.has_value() );
  CHECK( parsed->count( 0, 0 ) == 3 );
  CHECK( parsed->count( 1, 1 ) == 4 );
  CHECK( parsed->total() == 7 );
}

TEST_CASE( "zero-support classes are marked, not silently scored (WP09)",
           "[experiment][metrics][r29t01]" )
{
  // Class "b" never appears as truth: its 0.0 metrics are UNDEFINED (zero
  // denominator), which used to read exactly like "classified badly". The
  // optional zero_support marker separates the two.
  const QStringList labels{ QStringLiteral( "a" ), QStringLiteral( "b" ) };
  ConfusionMatrix matrix = namedMatrix( labels );
  matrix.setCount( 0, 0, 7 );
  matrix.setCount( 0, 1, 2 );

  const auto metrics = matrix.perClassAll();
  CHECK_FALSE( metrics[0].zeroSupport );
  REQUIRE( metrics[1].zeroSupport );
  CHECK( metrics[1].support == 0 );

  const QJsonObject json = matrix.toJson();
  const QJsonArray perClass = json.value( QStringLiteral( "per_class" ) ).toArray();
  REQUIRE( perClass.size() == 2 );
  CHECK_FALSE( perClass.at( 0 ).toObject().contains( QStringLiteral( "zero_support" ) ) );
  CHECK( perClass.at( 1 ).toObject().value( QStringLiteral( "zero_support" ) ).toBool()
         == true );
  // A fully supported matrix emits no marker at all (stable documents).
  ConfusionMatrix supported = namedMatrix( labels );
  supported.setCount( 0, 0, 1 );
  supported.setCount( 1, 1, 1 );
  const QJsonObject clean = supported.toJson();
  const QJsonArray cleanPerClass =
    clean.value( QStringLiteral( "per_class" ) ).toArray();
  CHECK_FALSE( cleanPerClass.at( 0 ).toObject().contains( QStringLiteral( "zero_support" ) ) );
  CHECK_FALSE( cleanPerClass.at( 1 ).toObject().contains( QStringLiteral( "zero_support" ) ) );
}
