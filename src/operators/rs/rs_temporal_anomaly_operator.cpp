// src/operators/rs/rs_temporal_anomaly_operator.cpp
#include "rs_temporal_anomaly_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "operators/rs/rs_temporal_output.h"
#include "processing/algorithms/temporal/temporal_stats.h"
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
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
const char *const kAnomalyBandNames[] = { "anomaly", "baseline_mean", "baseline_n" };
} // namespace

std::string RsTemporalAnomalyOperator::description() const
{
  return "Per-pixel anomaly of a target date against a baseline date range of "
         "the same collection: z-score or difference from the baseline mean. "
         "Two streaming passes (baseline accumulation, target scoring); "
         "degenerate baselines (stddev 0, too few observations) yield NoData "
         "instead of silent numbers.";
}

Json::Value RsTemporalAnomalyOperator::schema() const
{
  using namespace schema;
  Json::Value props( Json::objectValue );
  Json::Value scenes = makeStringParam( "scenes",
                                        "Scenes: array of {path, time?, bands?} or bare paths" );
  scenes["type"] = "array";
  scenes["items"] = Json::Value( Json::objectValue );
  scenes["items"]["type"] = "string";
  props["scenes"] = scenes;
  props["collection"] = makeStringParam( "collection",
                                         "...or path to a temporal collection descriptor JSON", "" );
  props["band"] = makeIntegerParam( "band", "Explicit analysis band (1-based)", 0 );
  props["band_role"] = makeEnumParam( "band_role", "Analysis band role",
                                      { "blue", "green", "red", "red_edge", "nir", "swir1", "swir2" },
                                      "" );
  props["method"] = makeEnumParam( "method",
                                   "zscore = (y-mean)/stddev (sample stddev); difference = y-mean",
                                   { "zscore", "difference" }, "zscore" );
  props["target_time"] = makeStringParam( "target_time",
                                          "ISO date/time of the analysed scene (default: latest "
                                          "scene)" );
  props["baseline_start"] = makeStringParam( "baseline_start",
                                             "Baseline window start (ISO date, inclusive; default: "
                                             "earliest scene)" );
  props["baseline_end"] = makeStringParam( "baseline_end",
                                           "Baseline window end (ISO date, inclusive; default: "
                                           "latest scene). The target scene is always excluded." );
  props["min_observations"] = makeIntegerParam( "min_observations",
                                                "Minimum baseline observations for a valid score",
                                                2 );
  props["duplicate_policy"] = makeEnumParam( "duplicate_policy", "Duplicate acquisition instants",
                                             { "keep_all", "reject" }, "keep_all" );
  props["apply_qa_masking"] = makeBooleanParam( "apply_qa_masking", "Exclude QA/cloud-masked samples", true );
  props["tile_size"] = makeIntegerParam( "tile_size", "Streaming tile size (pixels)", kDefaultTileSize );
  props["output"] = makeOutputParam( "output",
                                     "Anomaly GeoTIFF (bands: anomaly, baseline_mean, baseline_n)",
                                     "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Anomaly GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Baseline scenes used", 0 );
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalAnomalyOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "anomaly" );
  tags.append( "change" );
  meta["tags"] = tags;
  meta["purpose"] = "Detect when one date deviates from the collection's normal state "
                    "(disturbance, flooding, drought stress)";
  meta["prerequisites"] = "Common grid, acquisition times, consistent radiometric state "
                          "(temporal preflight)";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × baselineScenes)";
  meta["workflowHints"] = "Baseline defaults to the whole collection minus the target scene; "
                          "narrow it with baseline_start/baseline_end for seasonal normals";
  meta["limitations"] = "Zero-variance baselines give NoData z-scores; a single target scene "
                        "per run (loop the operator for multiple targets)";
  return meta;
}

Json::Value RsTemporalAnomalyOperator::executionEstimate() const
{
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4, 6, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalAnomalyOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4, 9, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalAnomalyOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const QString method = QString::fromStdString(
      getEnum( params, "method", { "zscore", "difference" }, "zscore" ) );
  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int minObservations = std::max( 2, getInt( params, "min_observations", 2 ) );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();

  // Target scene: explicit ISO match (date part), else the latest scene.
  int targetIndex = -1;
  if ( params.isMember( "target_time" ) && params["target_time"].isString() )
  {
    const auto targetTime = temporal::parseAcquisitionTime(
      QString::fromStdString( params["target_time"].asString() ) );
    if ( !targetTime.valid )
      throw RSOperatorError( ErrorCode::InvalidParameter, "invalid target_time" );
    for ( int s = 0; s < sceneCount; ++s )
    {
      if ( prepared.collection.scenes().at( s ).time.dateString() == targetTime.dateString() )
      {
        targetIndex = s;
        break;
      }
    }
    if ( targetIndex < 0 )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "no scene matches target_time " + targetTime.iso.toStdString() );
  }
  else
  {
    for ( int s = sceneCount - 1; s >= 0; --s )
      if ( prepared.collection.scenes().at( s ).time.valid )
      {
        targetIndex = s;
        break;
      }
    if ( targetIndex < 0 )
      throw RSOperatorError( ErrorCode::InvalidInputData, "no scene carries a valid time" );
  }

  // Baseline window (inclusive), target excluded. A date-only end bound
  // covers the whole end day (no silent same-day drops).
  temporal::AcquisitionTime baselineStart, baselineEnd;
  if ( params.isMember( "baseline_start" ) && params["baseline_start"].isString() )
  {
    baselineStart = temporal::parseAcquisitionTime(
      QString::fromStdString( params["baseline_start"].asString() ) );
    if ( !baselineStart.valid )
      throw RSOperatorError( ErrorCode::InvalidParameter, "invalid baseline_start" );
  }
  if ( params.isMember( "baseline_end" ) && params["baseline_end"].isString() )
  {
    baselineEnd = temporal::parseAcquisitionTime(
      QString::fromStdString( params["baseline_end"].asString() ) );
    if ( !baselineEnd.valid )
      throw RSOperatorError( ErrorCode::InvalidParameter, "invalid baseline_end" );
  }

  // A duplicate-instant twin of the target would bias the baseline toward
  // the target value itself — exclude every scene sharing its instant.
  const qint64 targetEpoch = prepared.collection.scenes().at( targetIndex ).time.epochMillis;
  const qint64 baselineEndMs =
      baselineEnd.valid && baselineEnd.precision == temporal::TimePrecision::Date
          ? baselineEnd.epochMillis + 86400000LL - 1
          : baselineEnd.epochMillis;
  std::vector<int> baselineIndices;
  for ( int s = 0; s < sceneCount; ++s )
  {
    if ( s == targetIndex )
      continue;
    const auto &t = prepared.collection.scenes().at( s ).time;
    if ( !t.valid )
      continue;
    if ( t.epochMillis == targetEpoch )
      continue;
    if ( baselineStart.valid && t.epochMillis < baselineStart.epochMillis )
      continue;
    if ( baselineEnd.valid && t.epochMillis > baselineEndMs )
      continue;
    baselineIndices.push_back( s );
  }
  if ( baselineIndices.empty() )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "baseline window contains no scene (target excluded)" );
  if ( static_cast<int>( baselineIndices.size() ) < minObservations )
    context.logWarning( "baseline has " + std::to_string( baselineIndices.size() ) +
                        " observation(s) < min_observations " + std::to_string( minObservations ) +
                        "; anomaly output will be NoData" );

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
    // documented default: explicit band > role > positional fallback > band 1
    analysisBands[s] = band > 0 ? band : 1;
    anyFallback = anyFallback || fallback;
  }
  if ( anyFallback )
    context.logWarning( "Analysis band resolved by positional fallback for at least one scene; "
                        "pass 'band' or 'bands' to pin it." );

  const int width = reader.width();
  const int height = reader.height();

  context.reportProgress( 0.05, "Creating anomaly output" );
  GdalDatasetWrapper out;
  QString outErr;
  temporal_output::TemporalOutputGuard guard;
  guard.manage( &out, QString::fromStdString( outputPath ) );
  if ( !out.create( QString::fromStdString( outputPath ), width, height, 3,
                    static_cast<int>( GDT_Float32 ), reader.geoTransform(), reader.projection(),
                    &outErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to create output: " + outErr.toStdString() );
  for ( int b = 1; b <= 3; ++b )
  {
    out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b ),
                        kAnomalyBandNames[b - 1] );
  }

  const int tiles = reader.totalTileCount();
  const size_t tilePixels = static_cast<size_t>( tileSize ) * tileSize;
  std::vector<float> tile( tilePixels ), targetTile( tilePixels );
  std::vector<temporal::stats::WelfordAccumulator> welford( tilePixels );
  std::vector<float> bandBuf( tilePixels );
  int tileDone = 0;

  for ( int t = 0; t < tiles; ++t )
  {
    int x = 0, y = 0, w = 0, h = 0;
    reader.tileRect( t, &x, &y, &w, &h );
    const size_t pixels = static_cast<size_t>( w ) * h;
    for ( size_t i = 0; i < pixels; ++i )
      welford[i] = temporal::stats::WelfordAccumulator{};

    // Pass 1: baseline accumulation.
    for ( int s : baselineIndices )
    {
      if ( !reader.readSceneBandTile( s, analysisBands[s], t, tile.data() ) )
        throw RSOperatorError(
            ErrorCode::GdalError,
            "failed reading baseline scene " +
                prepared.collection.scenes().at( s ).path.toStdString() );
      for ( size_t i = 0; i < pixels; ++i )
        if ( std::isfinite( tile[i] ) )
          welford[i].add( tile[i] );
      context.throwIfCancelled();
    }

    // Pass 2: target scoring.
    if ( !reader.readSceneBandTile( targetIndex, analysisBands[targetIndex], t,
                                    targetTile.data() ) )
      throw RSOperatorError( ErrorCode::GdalError, "failed reading target scene" );

    for ( int band = 1; band <= 3; ++band )
    {
      for ( size_t i = 0; i < pixels; ++i )
      {
        const auto &acc = welford[i];
        if ( band == 3 )
        {
          bandBuf[i] = static_cast<float>( acc.n );
          continue;
        }
        if ( band == 2 )
        {
          bandBuf[i] = acc.n > 0 ? static_cast<float>( acc.mean ) : kNan;
          continue;
        }
        // anomaly band
        const float y = targetTile[i];
        const bool enough = acc.n >= static_cast<std::uint64_t>( minObservations );
        if ( !std::isfinite( y ) || !enough || acc.n == 0 )
        {
          bandBuf[i] = kNan;
          continue;
        }
        if ( method == QLatin1String( "difference" ) )
        {
          bandBuf[i] = y - static_cast<float>( acc.mean );
        }
        else
        {
          const double sd = acc.sampleStddev();
          bandBuf[i] = sd > 0.0 ? static_cast<float>( ( y - acc.mean ) / sd ) : kNan;
        }
      }
      if ( !out.writeBandWindow( band, x, y, w, h, bandBuf.data() ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing anomaly band" );
    }

    ++tileDone;
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                            "Anomaly tiles " + std::to_string( tileDone ) + "/" +
                                std::to_string( tiles ) );
  }

  temporal_output::writeTemporalDatasetMetadata(
    out, prepared.collection, "rs:temporal_anomaly",
    QStringLiteral( "method=%1 target=%2 baseline_n=%3" )
        .arg( method, prepared.collection.scenes().at( targetIndex ).time.iso )
        .arg( baselineIndices.size() ) );

  QString closeErr;
  if ( !out.closeWithError( &closeErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "output flush failed (disk full?): " + closeErr.toStdString() );
  guard.commit();

  Json::Value result( Json::objectValue );
  result["output"] = outputPath;
  result["method"] = method.toStdString();
  result["targetTime"] = prepared.collection.scenes().at( targetIndex ).time.iso.toStdString();
  result["baselineCount"] = static_cast<Json::Int>( baselineIndices.size() );
  result["baselineInsufficient"] = static_cast<int>( baselineIndices.size() ) < minObservations;
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  // Welford = 24 B = 6 float slots; tile + targetTile + bandBuf buffers.
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
    TemporalTileReader::estimateWorkingSetBytes( tileSize, tileSize, 3, 6 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal anomaly complete" );
  return result;
}

} // namespace sicnu::operators::rs
