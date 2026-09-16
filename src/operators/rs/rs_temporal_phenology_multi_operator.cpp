// src/operators/rs/rs_temporal_phenology_multi_operator.cpp
#include "rs_temporal_phenology_multi_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "operators/rs/rs_temporal_output.h"
#include "processing/algorithms/temporal/temporal_fit.h"
#include "processing/algorithms/temporal/temporal_stream.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QDate>
#include <QDateTime>
#include <QTimeZone>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace sicnu::operators::rs
{

using namespace params;
using temporal::TemporalTileReader;

namespace
{
constexpr int kDefaultTileSize = 256;
constexpr int kTypicalSceneEstimate = 12;

float nanF() { return std::numeric_limits<float>::quiet_NaN(); }

/// Median of finite values (NaN when none) — the per-cycle aggregation
/// across seasons. Hand-rolled deterministic selection over a local copy.
double medianOfFinite( std::vector<double> values )
{
  std::vector<double> finite;
  finite.reserve( values.size() );
  for ( double v : values )
    if ( std::isfinite( v ) )
      finite.push_back( v );
  if ( finite.empty() )
    return nanF();
  std::sort( finite.begin(), finite.end() );
  const size_t mid = finite.size() / 2;
  return finite.size() % 2 == 1
           ? finite[mid]
           : 0.5 * ( finite[mid - 1] + finite[mid] );
}
} // namespace

std::string RsTemporalPhenologyMultiOperator::description() const
{
  return "Phenology 2.0 with automatic cycle candidates: per pixel, cycle "
         "windows are proposed from the pixel's own seasonal component "
         "(peaks of the seasonalDecompose climatology, at most "
         "maxCyclesPerYear per year, merged by minimum span), wrapped "
         "windows count toward the HARVEST year (year of the window end), "
         "and every window is quality-gated - sample count, coverage, "
         "largest-gap fraction, amplitude share. Refused windows carry no "
         "metrics (no low-sample guessing). Outputs per cycle index the "
         "median-of-seasons SOS/POS/EOS/LOS/amplitude plus a valid flag, "
         "the cycle count (cropping-system indicator), and the refusal "
         "count.";
}

Json::Value RsTemporalPhenologyMultiOperator::schema() const
{
  using namespace schema;
  Json::Value props( Json::objectValue );
  Json::Value scenes = makeStringParam(
      "scenes", "Scenes: array of {path, time?, bands?} or bare paths" );
  scenes["type"] = "array";
  scenes["items"] = Json::Value( Json::objectValue );
  scenes["items"]["type"] = "string";
  props["scenes"] = scenes;
  props["collection"] = makeStringParam(
      "collection", "...or workspace collection id / descriptor JSON path", "" );
  props["band"] = makeIntegerParam( "band", "Explicit analysis band (1-based)", 0 );
  props["band_role"] = makeEnumParam(
      "band_role", "Analysis band role",
      { "blue", "green", "red", "red_edge", "nir", "swir1", "swir2", "vv", "vh" }, "" );
  props["duplicate_policy"] =
    makeEnumParam( "duplicate_policy", "Duplicate acquisition instants",
                   { "keep_all", "reject" }, "keep_all" );
  props["apply_qa_masking"] =
    makeBooleanParam( "apply_qa_masking", "Exclude QA/cloud-masked samples", true );
  Json::Value maxCycles = makeIntegerParam(
      "maxCyclesPerYear", "Cycle candidates kept per calendar year (1-4)", 3 );
  setRange( maxCycles, 1, 4 );
  props["maxCyclesPerYear"] = maxCycles;
  Json::Value crossing = makeNumberParam(
      "crossingFraction", "SOS/EOS threshold as a fraction of the season amplitude", 0.5 );
  setRange( crossing, 0.01, 1.0 );
  props["crossingFraction"] = crossing;
  Json::Value minValid = makeIntegerParam(
      "minValidPerSeason", "Minimum valid samples per window (below: refusal)", 6 );
  setRange( minValid, 3, 100 );
  props["minValidPerSeason"] = minValid;
  Json::Value maxGap = makeNumberParam(
      "maxGapFraction", "Refusal threshold: largest observation gap / window span", 0.5 );
  setRange( maxGap, 0.05, 1.0 );
  props["maxGapFraction"] = maxGap;
  Json::Value minCoverage = makeNumberParam(
      "minCoverage", "Refusal threshold: observed span / window span", 0.5 );
  setRange( minCoverage, 0.05, 1.0 );
  props["minCoverage"] = minCoverage;
  Json::Value minPeak = makeNumberParam(
      "minPeakFraction", "A seasonal peak must exceed this fraction of the "
                         "seasonal range to propose a cycle",
      0.25 );
  setRange( minPeak, 0.01, 1.0 );
  props["minPeakFraction"] = minPeak;
  props["tile_size"] =
    makeIntegerParam( "tile_size", "Streaming tile size (pixels)", kDefaultTileSize );
  props["output"] = makeOutputParam(
      "output",
      "Phenology GeoTIFF: cycle_count, refusal_count, cycle{k}_sos, "
      "cycle{k}_pos, cycle{k}_eos, cycle{k}_los, cycle{k}_amplitude, "
      "cycle{k}_valid for k = 1..maxCyclesPerYear (day metrics as "
      "median-of-seasons doy; NaN = refused/absent)",
      "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Phenology GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Scenes in the series", 0 );
  outputs["pixelsWithAnyCycle"] = makeIntegerParam(
      "pixelsWithAnyCycle", "Pixels with at least one valid cycle", 0 );
  outputs["meanCyclesPerYear"] = makeNumberParam(
      "meanCyclesPerYear", "Mean cycle_count over analysed pixels", 0.0 );
  Json::Value memory = makeStringParam( "memory", "Streaming working-set summary" );
  memory["type"] = "object";
  outputs["memory"] = memory;
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalPhenologyMultiOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "phenology" );
  tags.append( "multi-cropping" );
  tags.append( "quality-flags" );
  meta["tags"] = tags;
  meta["purpose"] = "Score multi-cropping systems and shift-prone seasons "
                    "without declaring season windows up front: rainfed "
                    "double cropping, orchard/grass mosaics, winter crops "
                    "that cross the calendar year";
  meta["prerequisites"] = "Common grid, acquisition times; ideally >= 2 full "
                          "years so the climatology and per-season medians "
                          "have support; >= ~12 valid samples";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes × (decomposition + per-cycle "
                      "thresholding)) — bounded by maxCyclesPerYear";
  meta["workflowHints"] =
    "Feed rs:temporal_gap_fill output for gapped series first; combine with "
    "rs:temporal_region_features for parcel-level cropping-system tables";
  meta["limitations"] =
    "Windows come from the seasonal climatology - sub-seasonal pulses "
    "within one climatology peak are not separated; day metrics are doy "
    "medians across seasons (no inter-annual CI here); refusal bands use "
    "NaN encoding, refusal reasons live in the kernel contract";
  return meta;
}

Json::Value RsTemporalPhenologyMultiOperator::executionEstimate() const
{
  return sicnu::processing::makeStreamingEstimate(
      kDefaultTileSize, kDefaultTileSize, 1, 4, 2 + kTypicalSceneEstimate, 0,
      3 * 1024 * 1024 );
}

Json::Value RsTemporalPhenologyMultiOperator::estimateExecution(
    const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  int scenes = kTypicalSceneEstimate;
  if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
    scenes = std::max<int>( 1, static_cast<int>( params["scenes"].size() ) );
  return sicnu::processing::makeStreamingEstimate(
      tileSize, tileSize, 1, 4, 2 + static_cast<std::uint64_t>( scenes ), 0,
      3 * 1024 * 1024 );
}

Json::Value RsTemporalPhenologyMultiOperator::run( const Json::Value &params,
                                                   RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );

  const int maxCycles = std::clamp( getInt( params, "maxCyclesPerYear", 3 ), 1, 4 );
  double crossingFraction = getDouble( params, "crossingFraction", 0.5 );
  if ( !( crossingFraction > 0.0 && crossingFraction <= 1.0 ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "crossingFraction must be in (0, 1]" );
  const int minValidPerSeason = std::clamp( getInt( params, "minValidPerSeason", 6 ), 3, 100 );
  double maxGapFraction = getDouble( params, "maxGapFraction", 0.5 );
  if ( !( maxGapFraction > 0.0 && maxGapFraction <= 1.0 ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "maxGapFraction must be in (0, 1]" );
  double minCoverage = getDouble( params, "minCoverage", 0.5 );
  if ( !( minCoverage > 0.0 && minCoverage <= 1.0 ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "minCoverage must be in (0, 1]" );
  double minPeakFraction = getDouble( params, "minPeakFraction", 0.25 );
  if ( !( minPeakFraction > 0.0 && minPeakFraction <= 1.0 ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "minPeakFraction must be in (0, 1]" );

  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 8 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "multi-cycle phenology needs at least 8 scenes (got " +
                               std::to_string( sceneCount ) + ")" );

  temporal::TemporalStreamOptions streamOptions;
  streamOptions.tileWidth = tileSize;
  streamOptions.tileHeight = tileSize;
  streamOptions.applyQaMasking = applyQaMasking;

  QString readerError;
  TemporalTileReader reader( prepared.collection, prepared.preflight, streamOptions, &readerError );
  if ( !readerError.isEmpty() )
    throw RSOperatorError( ErrorCode::GdalError, readerError.toStdString() );

  std::vector<int> analysisBands( sceneCount, 1 );
  bool anyFallback = false;
  for ( int s = 0; s < sceneCount; ++s )
  {
    bool fallback = false;
    const int band = reader.bandForRole( s, bandRole, bandOverride, &fallback );
    if ( band <= 0 && !bandRole.isEmpty() && bandOverride <= 0 )
      throw RSOperatorError(
          ErrorCode::InvalidParameter,
          "band_role '" + bandRole.toStdString() +
              "' cannot be resolved in scene " +
              prepared.collection.scenes().at( s ).path.toStdString() +
              "; pass an explicit band or fix the scene metadata" );
    analysisBands[s] = band > 0 ? band : 1;
    anyFallback = anyFallback || fallback;
  }
  if ( anyFallback )
    context.logWarning(
        "Analysis band resolved by positional fallback for at least one scene; "
        "pass 'band' or 'bands' to pin it." );

  // doy/year axes from the REAL UTC acquisition instants (0 when a time is
  // invalid — preflight blocks those before we get here), as the phenology
  // kernels expect.
  std::vector<double> tDays( sceneCount );
  std::vector<int> doyOf( sceneCount, 0 );
  std::vector<int> yearOf( sceneCount, 0 );
  for ( int s = 0; s < sceneCount; ++s )
  {
    tDays[s] = reader.sceneDayOffset( s );
    const auto &scene = reader.scene( s );
    const QDate date =
      scene.time.valid
        ? QDateTime::fromMSecsSinceEpoch( scene.time.epochMillis, QTimeZone::utc() ).date()
        : QDate();
    doyOf[s] = date.isValid() ? date.dayOfYear() : 0;
    yearOf[s] = date.isValid() ? date.year() : 0;
  }

  temporal::PhenologyMultiOptions phenologyOptions;
  phenologyOptions.maxCyclesPerYear = maxCycles;
  phenologyOptions.crossingFraction = crossingFraction;
  phenologyOptions.minValidPerSeason = minValidPerSeason;
  phenologyOptions.maxGapFraction = maxGapFraction;
  phenologyOptions.minCoverage = minCoverage;
  phenologyOptions.minPeakFraction = minPeakFraction;

  const int bandCount = 2 +                              // cycle_count, refusal_count
                        6 * maxCycles;                   // per-cycle bands
  const int width = reader.width();
  const int height = reader.height();

  context.reportProgress( 0.05, "Creating phenology-multi output" );
  GdalDatasetWrapper out;
  QString outErr;
  temporal_output::TemporalOutputGuard guard;
  guard.manage( &out, QString::fromStdString( outputPath ) );
  if ( !out.create( QString::fromStdString( outputPath ), width, height, bandCount,
                    static_cast<int>( GDT_Float32 ), reader.geoTransform(),
                    reader.projection(), &outErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to create output: " + outErr.toStdString() );
  int b = 1;
  auto describeBand = [&]( const QString &name ) {
    out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b ),
                        name.toUtf8().constData() );
    ++b;
  };
  describeBand( "cycle_count" );
  describeBand( "refusal_count" );
  for ( int k = 1; k <= maxCycles; ++k )
  {
    describeBand( QStringLiteral( "cycle%1_sos" ).arg( k ) );
    describeBand( QStringLiteral( "cycle%1_pos" ).arg( k ) );
    describeBand( QStringLiteral( "cycle%1_eos" ).arg( k ) );
    describeBand( QStringLiteral( "cycle%1_los" ).arg( k ) );
    describeBand( QStringLiteral( "cycle%1_amplitude" ).arg( k ) );
    describeBand( QStringLiteral( "cycle%1_valid" ).arg( k ) );
  }

  const size_t maxTilePixels =
      static_cast<size_t>( std::min( tileSize, width ) ) *
      static_cast<size_t>( std::min( tileSize, height ) );
  constexpr size_t kMaxSeriesBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
  const size_t tileFloatsPerPixel =
    2 * static_cast<size_t>( sceneCount ) + 6 * static_cast<size_t>( maxCycles ) + 2;
  if ( tileFloatsPerPixel * maxTilePixels * sizeof( float ) > kMaxSeriesBytes )
    throw RSOperatorError(
        ErrorCode::InvalidParameter,
        "tile_size " + std::to_string( tileSize ) + " x " + std::to_string( sceneCount ) +
            " scenes exceeds the 2 GiB tile working-set budget; reduce tile_size" );
  const size_t tilePixels = maxTilePixels;

  std::vector<float> tile( tilePixels );
  std::vector<float> series( static_cast<size_t>( sceneCount ) * tilePixels );
  std::vector<float> cycleCountBuf( tilePixels );
  std::vector<float> refusalCountBuf( tilePixels );
  // Per-cycle buffers: sos/pos/eos/los/amp/valid.
  std::vector<std::vector<float>> cycleBufs(
      static_cast<size_t>( maxCycles ),
      std::vector<float>( 6 * tilePixels, 0.0f ) );
  std::vector<float> pixSeries( static_cast<size_t>( sceneCount ) );

  std::uint64_t pixelsWithAnyCycle = 0;
  double cycleCountSum = 0.0;
  std::uint64_t cycleCountPixels = 0;
  int tileDone = 0;
  const int tiles = reader.totalTileCount();

  for ( int t = 0; t < tiles; ++t )
  {
    int x = 0, y = 0, w = 0, h = 0;
    reader.tileRect( t, &x, &y, &w, &h );
    const size_t pixels = static_cast<size_t>( w ) * h;

    for ( int s = 0; s < sceneCount; ++s )
    {
      if ( !reader.readSceneBandTile( s, analysisBands[s], t, tile.data() ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "failed reading scene " +
                                   prepared.collection.scenes().at( s ).path.toStdString() );
      std::copy( tile.begin(), tile.begin() + static_cast<std::ptrdiff_t>( pixels ),
                 series.begin() + static_cast<std::ptrdiff_t>( s * tilePixels ) );
      context.throwIfCancelled();
    }

    for ( size_t i = 0; i < pixels; ++i )
    {
      for ( int s = 0; s < sceneCount; ++s )
        pixSeries[static_cast<size_t>( s )] = series[s * tilePixels + i];

      const temporal::PhenologyMultiResult result = temporal::phenologyMultiCycle(
          pixSeries, tDays, doyOf, yearOf, phenologyOptions );

      // Aggregate per cycle index across seasons (median of finite values).
      std::vector<std::vector<double>> sos( static_cast<size_t>( maxCycles ) );
      std::vector<std::vector<double>> pos( static_cast<size_t>( maxCycles ) );
      std::vector<std::vector<double>> eos( static_cast<size_t>( maxCycles ) );
      std::vector<std::vector<double>> los( static_cast<size_t>( maxCycles ) );
      std::vector<std::vector<double>> amp( static_cast<size_t>( maxCycles ) );
      std::vector<int> validCount( static_cast<size_t>( maxCycles ), 0 );
      int refusals = 0;
      for ( const temporal::PhenologyCycle &cycle : result.cycles )
      {
        const size_t k = static_cast<size_t>( std::max( 0, cycle.cycleIndex ) );
        if ( k >= static_cast<size_t>( maxCycles ) )
          continue;
        if ( cycle.quality.valid )
        {
          sos[k].push_back( cycle.metrics.sos );
          pos[k].push_back( cycle.metrics.pos );
          eos[k].push_back( cycle.metrics.eos );
          los[k].push_back( cycle.metrics.los );
          amp[k].push_back( cycle.metrics.amplitude );
          ++validCount[k];
        }
        else
        {
          ++refusals;
        }
      }

      float cycleCountBand = nanF();
      if ( result.valid || !result.cycles.empty() )
      {
        int anyValid = 0;
        for ( int k = 0; k < maxCycles; ++k )
          if ( validCount[static_cast<size_t>( k )] > 0 )
            ++anyValid;
        cycleCountBand = static_cast<float>( anyValid );
        cycleCountSum += static_cast<double>( anyValid );
        ++cycleCountPixels;
        if ( anyValid > 0 )
          ++pixelsWithAnyCycle;
      }
      cycleCountBuf[i] = cycleCountBand;
      refusalCountBuf[i] =
        static_cast<float>( refusals );

      for ( int k = 0; k < maxCycles; ++k )
      {
        const size_t idx = static_cast<size_t>( k );
        auto &bufs = cycleBufs[idx];
        const bool any = validCount[idx] > 0;
        bufs[0 * static_cast<size_t>( tilePixels ) + i] =
          any ? static_cast<float>( medianOfFinite( sos[idx] ) ) : nanF();
        bufs[1 * static_cast<size_t>( tilePixels ) + i] =
          any ? static_cast<float>( medianOfFinite( pos[idx] ) ) : nanF();
        bufs[2 * static_cast<size_t>( tilePixels ) + i] =
          any ? static_cast<float>( medianOfFinite( eos[idx] ) ) : nanF();
        bufs[3 * static_cast<size_t>( tilePixels ) + i] =
          any ? static_cast<float>( medianOfFinite( los[idx] ) ) : nanF();
        bufs[4 * static_cast<size_t>( tilePixels ) + i] =
          any ? static_cast<float>( medianOfFinite( amp[idx] ) ) : nanF();
        bufs[5 * static_cast<size_t>( tilePixels ) + i] = any ? 1.0f : 0.0f;
      }
      context.throwIfCancelled();
    }

    int ob = 1;
    auto writeBandWindow = [&]( const std::vector<float> &buf ) {
      if ( !out.writeBandWindow( ob++, x, y, w, h, buf.data() ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing output band" );
    };
    writeBandWindow( cycleCountBuf );
    writeBandWindow( refusalCountBuf );
    for ( int k = 0; k < maxCycles; ++k )
    {
      for ( int metric = 0; metric < 6; ++metric )
      {
        const size_t offset =
          static_cast<size_t>( metric ) * tilePixels;
        if ( !out.writeBandWindow(
                 ob++, x, y, w, h,
                 cycleBufs[static_cast<size_t>( k )].data() +
                   static_cast<std::ptrdiff_t>( offset ) ) )
          throw RSOperatorError( ErrorCode::GdalError, "failed writing output band" );
      }
    }

    ++tileDone;
    context.reportProgress(
        0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
        "Phenology-multi tiles " + std::to_string( tileDone ) + "/" +
            std::to_string( tiles ) );
  }

  temporal_output::writeTemporalDatasetMetadata(
      out, prepared.collection, "rs:temporal_phenology_multi",
      QStringLiteral( "maxCyclesPerYear=%1 crossingFraction=%2 minValidPerSeason=%3 "
                      "maxGapFraction=%4 minCoverage=%5" )
          .arg( maxCycles )
          .arg( crossingFraction )
          .arg( minValidPerSeason )
          .arg( maxGapFraction )
          .arg( minCoverage ) );
  if ( !prepared.preflight.commonRadiometricState.isEmpty() )
    GDALSetMetadataItem( static_cast<GDALDatasetH>( out.dataset() ),
                         "SICNU_RADIOMETRIC_STATE",
                         prepared.preflight.commonRadiometricState.toUtf8().constData(),
                         nullptr );

  QString closeErr;
  if ( !out.closeWithError( &closeErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "output flush failed (disk full?): " + closeErr.toStdString() );
  guard.commit();

  Json::Value result( Json::objectValue );
  result["output"] = outputPath;
  result["sceneCount"] = sceneCount;
  result["pixelsWithAnyCycle"] = Json::Value::UInt64( pixelsWithAnyCycle );
  result["meanCyclesPerYear"] =
    cycleCountPixels > 0 ? cycleCountSum / static_cast<double>( cycleCountPixels ) : 0.0;
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
      TemporalTileReader::estimateWorkingSetBytes(
          tileSize, tileSize, 6 + 2 * static_cast<std::uint64_t>( sceneCount ), 0 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal phenology multi complete" );
  return result;
}

} // namespace sicnu::operators::rs
