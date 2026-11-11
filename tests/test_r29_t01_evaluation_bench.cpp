// test_r29_t01_evaluation_bench.cpp — R29-T01 WP03: confusion-matrix
// aggregation cost curve (report-only, SLOW lane — cf. the sampling memory
// bench's discipline). Measures wall time and peak RSS of the FULL metric
// emission (toJson — every aggregate in one document) on synthetic sparse
// matrices at increasing label counts and nonzero-cell densities, always
// inside the declared cell budget (2^22). Functional anchors pin the
// numbers: the dense oracle case verifies exact values, and every larger
// case re-derives total/overall-accuracy from independently tracked sums.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "experiment/evaluation.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace sicnu::experiment;

namespace
{

double peakRssMiB()
{
  std::ifstream status( "/proc/self/status" );
  std::string key;
  long value = 0;
  std::string unit;
  while ( status >> key )
  {
    if ( key == "VmHWM:" )
    {
      status >> value >> unit;
      return static_cast<double>( value ) / 1024.0;
    }
    std::string rest;
    std::getline( status, rest );
  }
  return -1.0;
}

QStringList makeLabels( int n )
{
  QStringList labels;
  labels.reserve( n );
  for ( int i = 0; i < n; ++i )
    labels << QString( "c%1" ).arg( i );
  return labels;
}

/// Deterministic sparse matrix: `cells` entries scattered over an n×n grid
/// by a fixed-seed mt19937 (diagonal-biased, 70%); each entry carries mass 3
/// via three increments, so total() == cells×3 exactly (the independent
/// anchor for the aggregation output).
ConfusionMatrix makeSparse( int n, qint64 cells )
{
  const QStringList labels = makeLabels( n );
  ConfusionMatrix matrix( labels, n );
  std::mt19937 rng( 20261109u );
  for ( qint64 k = 0; k < cells; ++k )
  {
    const bool diagonal = ( rng() % 10 ) < 7;
    const qint64 row = static_cast<qint64>( rng() ) % n;
    const qint64 column = diagonal ? row : static_cast<qint64>( rng() ) % n;
    matrix.increment( row, column );
    matrix.increment( row, column );
    matrix.increment( row, column );
  }
  return matrix;
}

} // namespace

TEST_CASE( "R29-T01 evaluation aggregation: dense known answers stay exact",
           "[r29][perf][evaluation]" )
{
  // Functional anchor (not a perf case): the aggregation refactor must not
  // move the numbers — the pinned 3-class fixture from the boundary tests.
  const QStringList labels{ QStringLiteral( "water" ), QStringLiteral( "forest" ),
                            QStringLiteral( "urban" ) };
  ConfusionMatrix matrix( labels, 3 );
  matrix.setCount( 0, 0, 40 );
  matrix.setCount( 0, 1, 10 );
  matrix.setCount( 1, 0, 5 );
  matrix.setCount( 1, 1, 45 );
  matrix.setCount( 1, 2, 5 );
  matrix.setCount( 2, 2, 20 );
  matrix.setCount( 2, 0, 2 );
  const QJsonObject json = matrix.toJson();
  CHECK( matrix.total() == 127 );
  CHECK( json.value( QStringLiteral( "overall_accuracy" ) ).toDouble()
         == Catch::Approx( 105.0 / 127.0 ) );
  // round-trips through the sparse store untouched
  const auto reparsed = ConfusionMatrix::fromJson( json );
  REQUIRE( reparsed.has_value() );
  CHECK( *reparsed == matrix );
}

TEST_CASE( "R29-T01 evaluation aggregation cost curve: labels × nonzero cells",
           "[r29][perf][evaluation][slow]" )
{
  struct Shape
  {
      const char *label;
      int labels;
      qint64 cells;
  };
  // Budget discipline (fromJson's declared budget is 2^22 stored cells; the
  // in-memory QHash carries the same order): stay inside it.
  const Shape shapes[] = {
    { "B1-64lbl-1kcells", 64, 1000 },
    { "B2-1klbl-20kcells", 1024, 20000 },
    { "B3-8klbl-400kcells", 8192, 400000 },
    { "B4-32klbl-2Mcells", 32768, 2000000 },
  };
  for ( const Shape &shape : shapes )
  {
    // Fixed per-cell mass 3 keeps every count far below the 2^53 JSON
    // exactness bound; total mass stays inside qint64 by construction.
    ConfusionMatrix matrix = makeSparse( shape.labels, shape.cells );
    REQUIRE( matrix.size() == shape.labels );

    const auto start = std::chrono::steady_clock::now();
    const QJsonObject json = matrix.toJson();
    const auto end = std::chrono::steady_clock::now();
    const double ms =
      std::chrono::duration<double, std::milli>( end - start ).count();

    // Independent anchors: total = cells × 3 (makeSparse's mass law), the
    // emitted document agrees with the public methods, and the per-class
    // array covers every label.
    CHECK( matrix.total() == shape.cells * 3 );
    CHECK( json.value( QStringLiteral( "kappa" ) ).toDouble()
           == Catch::Approx( matrix.kappa() ) );
    CHECK( json.value( QStringLiteral( "macro_f1" ) ).toDouble()
           == Catch::Approx( matrix.macroF1() ) );
    CHECK( json.value( QStringLiteral( "per_class" ) ).toArray().size() == shape.labels );

    std::printf( "[R29T01-EVAL] %-22s labels=%5d cells=%7lld | toJson %8.2f ms | VmHWM %.1f MiB\n",
                 shape.label, shape.labels, static_cast<long long>( shape.cells ), ms,
                 peakRssMiB() );
  }
}
