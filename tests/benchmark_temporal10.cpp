// benchmark_temporal10.cpp — Temporal Platform 10.0 scale/performance
// evidence. Run manually:
//
//   benchmark_temporal10 --out benchmarks/temporal10.json           (full)
//   benchmark_temporal10 --bench-quick --out benchmarks/temporal10.json
//
// Tiers (the benchmark_scale8 convention):
//   * full (default) — kernel ladders at the platform target scales:
//       regularize_1000     — 1000 irregular observations -> 16-day calendar
//       harmonic_breaks_500 — joint seasonal-trend fit over 500 samples
//       region_reduce_100k  — per-date reducer over 100k regions
//       phenology_cycles    — per-year per-cycle phenology over 10 years
//   * quick — fixed small counts (smoke tier; completes well under 120 s).
//
// Evidence only: numbers are regression evidence in a JSON artifact with
// environment headers, never a gate. All benchmarks are GDAL-free (pure
// kernel cost); operator-level I/O bounds live in the test suites via the
// TemporalTileReader buffer instrumentation.

#include "processing/algorithms/temporal/temporal_calendar.h"
#include "processing/algorithms/temporal/temporal_change.h"
#include "processing/algorithms/temporal/temporal_fit.h"
#include "processing/algorithms/temporal/temporal_region_table.h"

#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>

#include <json/json.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace sicnu::temporal;

namespace
{

struct BenchResult
{
  std::string name;
  int iters = 0;
  double msTotal = 0.0;
  double opsPerSecond = 0.0;
};

double nowMs( QElapsedTimer &timer ) { return static_cast<double>( timer.nsecsElapsed() ) / 1e6; }

BenchResult runBench( const std::string &name, int iters, const std::function<void()> &fn )
{
  QElapsedTimer timer;
  timer.start();
  for ( int i = 0; i < iters; ++i )
    fn();
  BenchResult r;
  r.name = name;
  r.iters = iters;
  r.msTotal = nowMs( timer );
  r.opsPerSecond = r.msTotal > 0.0 ? static_cast<double>( iters ) / ( r.msTotal / 1000.0 ) : 0.0;
  return r;
}

// --- fixtures ----------------------------------------------------------

std::pair<std::vector<float>, std::vector<double>> irregularSeries( int count,
                                                                    unsigned seed )
{
  std::mt19937 rng( seed );
  std::uniform_real_distribution<double> jitter( -3.0, 3.0 );
  std::vector<float> y;
  std::vector<double> t;
  y.reserve( static_cast<size_t>( count ) );
  t.reserve( static_cast<size_t>( count ) );
  double day = 0.0;
  for ( int i = 0; i < count; ++i )
  {
    day += 6.0 + jitter( rng ); // ~6-day revisit with noise
    t.push_back( day );
    y.push_back( static_cast<float>( 20.0 + 0.001 * day +
                                     3.0 * std::sin( 2.0 * M_PI * day / 365.25 ) +
                                     0.5 * jitter( rng ) ) );
  }
  return { std::move( y ), std::move( t ) };
}

} // namespace

int main( int argc, char **argv )
{
  bool quick = false;
  const char *outPath = nullptr;
  for ( int i = 1; i < argc; ++i )
  {
    if ( std::strcmp( argv[i], "--bench-quick" ) == 0 )
      quick = true;
    else if ( std::strcmp( argv[i], "--out" ) == 0 && i + 1 < argc )
      outPath = argv[++i];
  }

  Json::Value artifact( Json::objectValue );
  artifact["schema"] = "exp.bench.temporal10.v1";
  artifact["tier"] = quick ? "quick" : "full";
  artifact["generatedUtc"] = QDateTime::currentDateTimeUtc()
                               .toString( Qt::ISODateWithMs )
                               .toStdString();
  Json::Value benchmarks( Json::arrayValue );

  const int regularizeCount = quick ? 100 : 1000;
  const int regularizeIters = quick ? 20 : 200;
  const int breaksCount = quick ? 100 : 500;
  const int breaksIters = quick ? 20 : 100;
  const int regionCount = quick ? 10000 : 100000;
  const int regionIters = quick ? 10 : 50;
  const int phenologyYears = quick ? 3 : 10;
  const int phenologyIters = quick ? 50 : 200;

  {
    // 1. Irregular -> 16-day calendar regularization (linear).
    const auto [y, t] = irregularSeries( regularizeCount, 42 );
    CalendarSpec spec;
    spec.cadence = CalendarCadence::Days;
    spec.cadenceDays = 16;
    const std::vector<CalendarPoint> calendar =
      buildRegularCalendar( "2020-01-01", 0.0, t.back(), spec );
    RegularizeOptions options;
    options.method = RegularizeMethod::Linear;
    const BenchResult r = runBench(
      "regularize_" + std::to_string( regularizeCount ), regularizeIters, [&] {
        const RegularizedSeries out = regularizeSeries( y, t, calendar, options );
        if ( out.points.size() != calendar.size() )
          std::fprintf( stderr, "regularize size mismatch\n" );
      } );
    Json::Value entry( Json::objectValue );
    entry["name"] = r.name;
    entry["iters"] = r.iters;
    entry["ms_total"] = r.msTotal;
    entry["ops_per_second"] = r.opsPerSecond;
    entry["calendar_points"] = static_cast<Json::Value::UInt64>( calendar.size() );
    benchmarks.append( entry );
  }

  {
    // 2. Joint harmonic+trend segmentation.
    const auto [y, t] = irregularSeries( breaksCount, 7 );
    const BenchResult r = runBench(
      "harmonic_breaks_" + std::to_string( breaksCount ), breaksIters, [&] {
        const SeasonalTrendBreaksResult fit =
          fitSeasonalTrendBreaks( y, t, 2, 3, 5, 0.10, false );
        if ( fit.segments.empty() )
          std::fprintf( stderr, "breaks fit degenerate\n" );
      } );
    Json::Value entry( Json::objectValue );
    entry["name"] = r.name;
    entry["iters"] = r.iters;
    entry["ms_total"] = r.msTotal;
    entry["ops_per_second"] = r.opsPerSecond;
    benchmarks.append( entry );
  }

  {
    // 3. Per-date reducer over the 100k-region target: O(R) accumulation.
    std::vector<size_t> counts( static_cast<size_t>( regionCount ), 1 );
    RegionDateReducer reducer( regionCount, 0, counts );
    std::vector<float> samples( static_cast<size_t>( regionCount ) );
    for ( int i = 0; i < regionCount; ++i )
      samples[static_cast<size_t>( i )] = static_cast<float>( i % 977 );
    const BenchResult r = runBench(
      "region_reduce_" + std::to_string( regionCount ), regionIters, [&] {
        reducer.beginDate();
        for ( int i = 0; i < regionCount; ++i )
          reducer.addSample( i, samples[static_cast<size_t>( i )] );
        reducer.endDate();
      } );
    Json::Value entry( Json::objectValue );
    entry["name"] = r.name;
    entry["iters"] = r.iters;
    entry["ms_total"] = r.msTotal;
    entry["ops_per_second"] = r.opsPerSecond;
    entry["rows_per_date"] = regionCount;
    benchmarks.append( entry );
  }

  {
    // 4. Per-year per-cycle phenology (weekly 2-cycle series).
    const int count = phenologyYears * 52;
    std::vector<float> y( static_cast<size_t>( count ) );
    std::vector<double> t( static_cast<size_t>( count ) );
    std::vector<int> doy( static_cast<size_t>( count ) );
    std::vector<int> year( static_cast<size_t>( count ) );
    for ( int i = 0; i < count; ++i )
    {
      const double day = 7.0 * i;
      t[static_cast<size_t>( i )] = day;
      y[static_cast<size_t>( i )] = static_cast<float>(
        20.0 + 4.0 * std::sin( 2.0 * M_PI * day / 365.25 ) );
      const QDate d = QDate( 2020, 1, 1 ).addDays( static_cast<qint64>( day ) );
      doy[static_cast<size_t>( i )] = d.dayOfYear();
      year[static_cast<size_t>( i )] = d.year();
    }
    std::vector<SeasonWindow> windows{ { 1, 180 }, { 181, 366 } };
    const BenchResult r = runBench(
      "phenology_cycles_" + std::to_string( phenologyYears ) + "y", phenologyIters, [&] {
        const std::vector<SeasonYearMetrics> metrics =
          phenologyCyclesPerYear( y, t, doy, year, windows, 0.2 );
        if ( metrics.empty() )
          std::fprintf( stderr, "phenology degenerate\n" );
      } );
    Json::Value entry( Json::objectValue );
    entry["name"] = r.name;
    entry["iters"] = r.iters;
    entry["ms_total"] = r.msTotal;
    entry["ops_per_second"] = r.opsPerSecond;
    benchmarks.append( entry );
  }

  artifact["benchmarks"] = benchmarks;

  bool ok = true;
  if ( outPath )
  {
    std::ofstream out( outPath );
    ok = static_cast<bool>( out );
    if ( ok )
      out << Json::writeString( Json::StreamWriterBuilder(), artifact ) << "\n";
  }
  std::printf( "%s\n", Json::writeString( Json::StreamWriterBuilder(), artifact ).c_str() );
  return ok ? 0 : 1;
}
