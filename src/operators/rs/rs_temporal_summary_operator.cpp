// src/operators/rs/rs_temporal_summary_operator.cpp
#include "rs_temporal_summary_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "operators/rs/rs_temporal_output.h"
#include "processing/algorithms/temporal/temporal_stream.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"


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
constexpr std::uint64_t kMedianMemoryBudgetBytes = 256ull * 1024 * 1024;
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

const char *const kStatBandNames[] = { "count",       "valid_count", "mean",
                                       "min",         "max",         "stddev",
                                       "median" };

int medianTileSide( int requestedTileSize, int sceneCount )
{
  if ( sceneCount < 1 )
    sceneCount = 1;
  const std::uint64_t maxPixels = kMedianMemoryBudgetBytes / ( 4 * static_cast<std::uint64_t>( sceneCount ) );
  std::uint64_t side = static_cast<std::uint64_t>( std::sqrt( static_cast<double>( maxPixels ) ) );
  side = std::min<std::uint64_t>( side, static_cast<std::uint64_t>( requestedTileSize ) );
  side = std::max<std::uint64_t>( side, 16 );
  return static_cast<int>( side );
}

/// True when even the 16x16 floor would exceed the median memory budget
/// (>262k scenes — refuse rather than silently exceed the budget).
bool medianBudgetInfeasible( int sceneCount )
{
  return static_cast<std::uint64_t>( sceneCount ) * 16 * 16 * 4 > kMedianMemoryBudgetBytes;
}
} // namespace

std::string RsTemporalSummaryOperator::description() const
{
  return "Per-pixel temporal statistics over a multi-date collection: count, "
         "valid_count, mean, min, max, stddev (Welford) and optional exact "
         "median. Values follow the shared temporal validity contract "
         "(NoData/NaN/QA-cloud masked samples are excluded).";
}

Json::Value RsTemporalSummaryOperator::schema() const
{
  using namespace schema;
  Json::Value props( Json::objectValue );
  Json::Value scenes = makeStringParam( "scenes",
                                        "Scenes: array of {path, time?, bands?, mask_band?} "
                                        "or bare path strings (chronological order is "
                                        "re-derived from acquisition times)" );
  scenes["type"] = "array";
  scenes["items"] = Json::Value( Json::objectValue );
  scenes["items"]["type"] = "string";
  props["scenes"] = scenes;
  props["collection"] = makeStringParam( "collection",
                                         "...or path to a temporal collection descriptor JSON",
                                         "" );
  props["band"] = makeIntegerParam( "band", "Explicit analysis band (1-based; wins over band_role)", 0 );
  props["band_role"] = makeEnumParam( "band_role", "Analysis band role",
                                      { "blue", "green", "red", "red_edge", "nir", "swir1", "swir2" },
                                      "" );
  props["duplicate_policy"] = makeEnumParam( "duplicate_policy",
                                             "Duplicate acquisition instants: keep all (deterministic "
                                             "input-order ties) or reject",
                                             { "keep_all", "reject" }, "keep_all" );
  props["include_median"] = makeBooleanParam( "include_median",
                                              "Add an exact per-pixel median band (memory-bounded "
                                              "tile shrink; never an approximation)",
                                              false );
  props["apply_qa_masking"] = makeBooleanParam( "apply_qa_masking",
                                                "Exclude QA/cloud-masked samples (Landsat QA_PIXEL / "
                                                "Sentinel-2 SCL when present)",
                                                true );
  props["tile_size"] = makeIntegerParam( "tile_size", "Streaming tile size (pixels)", kDefaultTileSize );
  props["output"] = makeOutputParam( "output", "Summary GeoTIFF (one band per statistic)", "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Summary GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Number of scenes analysed", 0 );
  outputs["validFraction"] = makeNumberParam( "validFraction", "Fraction of valid samples", 0.0 );
  return makeRootSchema( displayName(), description(), props, outputs );
}

Json::Value RsTemporalSummaryOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "time-series" );
  tags.append( "statistics" );
  meta["tags"] = tags;
  meta["purpose"] = "Per-pixel statistics (mean/stddev/min/max/counts, optional median) "
                    "across a chronological multi-date collection";
  meta["prerequisites"] = "Common grid across scenes (temporal preflight), acquisition times, "
                          "consistent radiometric state";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes)";
  meta["workflowHints"] = "Run after alignment; results feed temporal_composite/trend/anomaly";
  meta["limitations"] = "Median is exact but shrinks the streaming tile to respect the memory "
                        "budget; stddev uses the population (N) denominator";
  return meta;
}

Json::Value RsTemporalSummaryOperator::executionEstimate() const
{
  return makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4, 8, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalSummaryOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const bool median = getBool( params, "include_median", false );
  int scenes = 1;
  if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
    scenes = std::max<int>( 1, static_cast<int>( params["scenes"].size() ) );
  int side = median ? medianTileSide( tileSize, scenes ) : tileSize;
  // per pixel: 1 value buffer + 6 accumulator floats (Welford + min/max)
  Json::Value est = makeStreamingEstimate(
    side, side, 1, 4, median ? static_cast<std::uint64_t>( scenes ) + 6 : 7, 0,
    2 * 1024 * 1024 );
  return est;
}

Json::Value RsTemporalSummaryOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const bool includeMedian = getBool( params, "include_median", false );
  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int requestedTile = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );

  const int sceneCount = prepared.collection.sceneCount();
  if ( includeMedian && medianBudgetInfeasible( sceneCount ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "include_median needs more than the median memory budget allows "
                           "for this scene count; reduce the collection" );
  const int tileSide = includeMedian ? medianTileSide( requestedTile, sceneCount ) : requestedTile;

  temporal::TemporalStreamOptions streamOptions;
  streamOptions.tileWidth = tileSide;
  streamOptions.tileHeight = tileSide;
  streamOptions.applyQaMasking = applyQaMasking;

  QString readerError;
  TemporalTileReader reader( prepared.collection, prepared.preflight, streamOptions, &readerError );
  if ( !readerError.isEmpty() )
    throw RSOperatorError( ErrorCode::GdalError, readerError.toStdString() );

  // Per-scene analysis band (explicit override > role > band 1).
  std::vector<int> analysisBands( sceneCount, 1 );
  bool anyFallback = false;
  for ( int s = 0; s < sceneCount; ++s )
  {
    bool fallback = false;
    const int band = reader.bandForRole( s, bandRole, bandOverride, &fallback );
    // documented default: explicit band > role > positional fallback > band 1
    analysisBands[s] = band > 0 ? band : 1;
    anyFallback = anyFallback || fallback;
  }
  if ( anyFallback )
    context.logWarning( "Analysis band resolved by positional fallback for at least one scene "
                        "(no SICNU_BAND_ROLE metadata); pass 'band' or 'bands' to pin it." );

  const int bandCount = includeMedian ? 7 : 6;
  const int width = reader.width();
  const int height = reader.height();

  context.reportProgress( 0.05, "Creating output" );
  GdalDatasetWrapper out;
  QString outErr;
  temporal_output::TemporalOutputGuard guard;
  guard.manage( &out, QString::fromStdString( outputPath ) );
  if ( !out.create( QString::fromStdString( outputPath ), width, height, bandCount,
                    static_cast<int>( GDT_Float32 ), reader.geoTransform(), reader.projection(),
                    &outErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to create output: " + outErr.toStdString() );
  for ( int b = 1; b <= bandCount; ++b )
  {
    out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b ),
                        kStatBandNames[b - 1] );
  }

  const int tiles = reader.totalTileCount();
  const size_t tilePixels = static_cast<size_t>( tileSide ) * tileSide;

  std::vector<float> tile( tilePixels );
  std::vector<std::uint32_t> n( tilePixels );
  std::vector<double> mean( tilePixels ), m2( tilePixels );
  std::vector<float> mn( tilePixels ), mx( tilePixels );
  std::vector<float> medianValues;
  if ( includeMedian )
    medianValues.resize( tilePixels * static_cast<size_t>( sceneCount ) );

  std::uint64_t totalValid = 0;
  const double totalSamples = static_cast<double>( width ) * height * sceneCount;
  int tileDone = 0;
  std::vector<float> bandBuf( tilePixels );

  for ( int t = 0; t < tiles; ++t )
  {
    int x = 0, y = 0, w = 0, h = 0;
    reader.tileRect( t, &x, &y, &w, &h );
    const size_t pixels = static_cast<size_t>( w ) * h;
    std::fill( n.begin(), n.begin() + pixels, 0 );
    std::fill( mean.begin(), mean.begin() + pixels, 0.0 );
    std::fill( m2.begin(), m2.begin() + pixels, 0.0 );
    std::fill( mn.begin(), mn.begin() + pixels, std::numeric_limits<float>::infinity() );
    std::fill( mx.begin(), mx.begin() + pixels, -std::numeric_limits<float>::infinity() );

    for ( int s = 0; s < sceneCount; ++s )
    {
      if ( !reader.readSceneBandTile( s, analysisBands[s], t, tile.data() ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "failed reading scene " + std::to_string( s ) );
      for ( size_t i = 0; i < pixels; ++i )
      {
        const float v = tile[i];
        if ( !std::isfinite( v ) )
          continue;
        // Welford update in structure-of-arrays form (same recurrence as
        // temporal::stats::WelfordAccumulator, kept SoA for cache locality).
        const std::uint32_t cnt = n[i] + 1;
        const double delta = v - mean[i];
        mean[i] += delta / cnt;
        m2[i] += delta * ( v - mean[i] );
        n[i] = cnt;
        mn[i] = std::min( mn[i], v );
        mx[i] = std::max( mx[i], v );
        if ( includeMedian )
          medianValues[i * static_cast<size_t>( sceneCount ) + ( cnt - 1 )] = v;
      }
      context.throwIfCancelled();
    }

    // finalize + write (n[i] is also the per-pixel median fill count)
    auto writeStat = [&]( int band, auto fill ) {
      for ( size_t i = 0; i < pixels; ++i )
        bandBuf[i] = fill( i );
      if ( !out.writeBandWindow( band, x, y, w, h, bandBuf.data() ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing summary band" );
    };

    const float sceneCountF = static_cast<float>( sceneCount );
    writeStat( 1, [&]( size_t ) { return sceneCountF; } );
    writeStat( 2, [&]( size_t i ) { return static_cast<float>( n[i] ); } );
    writeStat( 3, [&]( size_t i ) { return n[i] > 0 ? static_cast<float>( mean[i] ) : kNan; } );
    writeStat( 4, [&]( size_t i ) { return n[i] > 0 ? mn[i] : kNan; } );
    writeStat( 5, [&]( size_t i ) { return n[i] > 0 ? mx[i] : kNan; } );
    writeStat( 6, [&]( size_t i ) {
      return n[i] > 0 ? static_cast<float>( std::sqrt( m2[i] / n[i] ) ) : kNan;
    } );
    if ( includeMedian )
    {
      writeStat( 7, [&]( size_t i ) {
        const std::uint32_t cnt = n[i];
        if ( cnt == 0 )
          return kNan;
        float *begin = &medianValues[i * static_cast<size_t>( sceneCount )];
        std::nth_element( begin, begin + ( cnt - 1 ) / 2, begin + cnt );
        if ( cnt % 2 == 1 )
          return begin[( cnt - 1 ) / 2];
        // even count: average the two central order statistics
        const float lower = *std::max_element( begin, begin + cnt / 2 );
        const float upper = *std::min_element( begin + cnt / 2, begin + cnt );
        return 0.5f * ( lower + upper );
      } );
    }
    for ( size_t i = 0; i < pixels; ++i )
      totalValid += n[i];

    ++tileDone;
    context.throwIfCancelled();
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                            "Summary tiles " + std::to_string( tileDone ) + "/" +
                                std::to_string( tiles ) );
  }

  temporal_output::writeTemporalDatasetMetadata(
    out, prepared.collection, "rs:temporal_summary",
    QStringLiteral( "band_role=%1 include_median=%2" )
        .arg( bandRole.isEmpty() ? QStringLiteral( "band:1" ) : bandRole )
        .arg( includeMedian ) );

  QString closeErr;
  if ( !out.closeWithError( &closeErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "output flush failed (disk full?): " + closeErr.toStdString() );
  guard.commit();

  Json::Value result( Json::objectValue );
  result["output"] = outputPath;
  result["width"] = width;
  result["height"] = height;
  result["sceneCount"] = sceneCount;
  result["statistics"] = includeMedian ? "count,valid_count,mean,min,max,stddev,median"
                                       : "count,valid_count,mean,min,max,stddev";
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  result["validFraction"] = totalSamples > 0 ? static_cast<double>( totalValid ) / totalSamples : 0.0;
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSide;
  memory["tileHeight"] = tileSide;
  memory["sceneCount"] = sceneCount;
  // byte-true accounting: n(4B)+mean(8B)+m2(8B)+min/max(4B each)+bandBuf+tile
  // = 9 float-equivalent slots per pixel (7 accumulators + 2 buffers);
  // median mode adds one value slot per date.
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
    TemporalTileReader::estimateWorkingSetBytes(
      tileSide, tileSide,
      includeMedian ? static_cast<std::uint64_t>( sceneCount ) + 2 : 2,
      includeMedian ? 7 : 7 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal summary complete" );
  return result;
}

} // namespace sicnu::operators::rs
