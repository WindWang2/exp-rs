// src/operators/rs/rs_temporal_phenology_operator.cpp
#include "rs_temporal_phenology_operator.h"

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


#include <gdal.h>

#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs
{

using namespace params;
using temporal::TemporalTileReader;

namespace
{
constexpr int kDefaultTileSize = 256;
constexpr int kTypicalSceneCount = 8;
constexpr int kPhenologyBandCount = 7;
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
// Units: day-of-year for sos/pos/eos, days for los, index units otherwise.
const char *const kPhenologyBandNames[kPhenologyBandCount] = {
  "sos", "pos", "eos", "los", "amplitude", "base", "integral"
};
} // namespace

std::string RsTemporalPhenologyOperator::description() const
{
  return "Seasonal phenology metrics per pixel from a vegetation-index time "
         "series: start/peak/end of season (SOS/POS/EOS, day-of-year), season "
         "length (LOS, days), amplitude, base level and the small integral of "
         "the index over the season. Threshold method: SOS/EOS are the "
         "first/last crossings of base + crossingFraction·amplitude inside the "
         "season window [seasonStartDoy, seasonEndDoy] (a window that wraps "
         "the year end is supported). Metrics are computed once per pixel over "
         "the whole series for the requested season window; pixels with fewer "
         "than minValidPerSeason valid in-season samples stay NoData.";
}

Json::Value RsTemporalPhenologyOperator::schema() const
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
  props["duplicate_policy"] = makeEnumParam( "duplicate_policy", "Duplicate acquisition instants",
                                             { "keep_all", "reject" }, "keep_all" );
  props["apply_qa_masking"] = makeBooleanParam( "apply_qa_masking", "Exclude QA/cloud-masked samples", true );
  props["tile_size"] = makeIntegerParam( "tile_size", "Streaming tile size (pixels)", kDefaultTileSize );
  Json::Value crossingFractionParam = makeNumberParam(
      "crossingFraction",
      "SOS/EOS threshold as a fraction of the in-season amplitude", 0.2 );
  setRange( crossingFractionParam, 0.05, 0.95 );
  props["crossingFraction"] = crossingFractionParam;
  Json::Value seasonStartParam = makeIntegerParam(
      "seasonStartDoy", "Season start day-of-year (inclusive; > end wraps the year end)", 1 );
  setRange( seasonStartParam, 1, 366 );
  props["seasonStartDoy"] = seasonStartParam;
  Json::Value seasonEndParam = makeIntegerParam( "seasonEndDoy",
                                                 "Season end day-of-year (inclusive)", 366 );
  setRange( seasonEndParam, 1, 366 );
  props["seasonEndDoy"] = seasonEndParam;
  props["minValidPerSeason"] = makeIntegerParam( "minValidPerSeason",
                                                 "Minimum valid samples inside the season window "
                                                 "(fewer → NoData pixel)",
                                                 3 );
  props["output"] = makeOutputParam( "output",
                                     "Phenology GeoTIFF (bands: sos, pos, eos [day-of-year], "
                                     "los [days], amplitude, base, integral [index units])",
                                     "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Phenology GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Dates in the series", 0 );
  Json::Value metrics = makeStringParam( "metrics", "Phenology metric names in output band order" );
  metrics["type"] = "array";
  metrics["items"] = Json::Value( Json::objectValue );
  metrics["items"]["type"] = "string";
  outputs["metrics"] = metrics;
  outputs["bands"] = makeIntegerParam( "bands", "Output band count (one per metric)", 0 );
  outputs["validPixelFraction"] = makeNumberParam( "validPixelFraction",
                                                   "Pixels with valid metrics / total pixels", 0.0 );
  outputs["timeStart"] = makeStringParam( "timeStart", "First acquisition date in the series (ISO)", "" );
  outputs["timeEnd"] = makeStringParam( "timeEnd", "Last acquisition date in the series (ISO)", "" );
  Json::Value memory = makeStringParam( "memory", "Streaming working-set summary (tile size and estimated bytes)" );
  memory["type"] = "object";
  outputs["memory"] = memory;
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalPhenologyOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "phenology" );
  tags.append( "time-series" );
  meta["tags"] = tags;
  meta["purpose"] = "Per-pixel growing-season metrics (SOS/POS/EOS/LOS/amplitude/base/integral) "
                    "from a vegetation-index series — cropping calendars, drought and land-cover "
                    "change analysis";
  meta["prerequisites"] = "Vegetation index series (e.g. NDVI/EVI) with valid acquisition times; "
                          "common grid and temporal preflight; gap-free series give the most "
                          "stable metrics";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes)";
  meta["workflowHints"] = "Run on rs:temporal_smooth or rs:temporal_gap_fill output for best "
                          "results; tune crossingFraction to move SOS/EOS along the greening "
                          "flanks";
  meta["limitations"] = "Metrics are computed once per pixel over the whole series for the "
                        "requested season window — multi-year data mixes years (per-year splits "
                        "are a follow-up); the kernel needs at least 3 valid in-season samples";
  return meta;
}

Json::Value RsTemporalPhenologyOperator::executionEstimate() const
{
  // Typical 8-date collection: series per scene + 7 metric buffers + pixel
  // gather + read tile.
  const int buffers = kTypicalSceneCount + kPhenologyBandCount + 2;
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4, buffers, 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalPhenologyOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  int scenes = params["scenes"].isArray() ? params["scenes"].size() : kTypicalSceneCount;
  scenes = std::max( scenes, 1 );
  // series per scene + 7 metric buffers + per-pixel series gather + read tile.
  const int buffers = scenes + kPhenologyBandCount + 2;
  return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4, buffers, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalPhenologyOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );
  const double crossingFraction =
      std::clamp( getDouble( params, "crossingFraction", 0.2 ), 0.05, 0.95 );
  const int seasonStartDoy = std::clamp( getInt( params, "seasonStartDoy", 1 ), 1, 366 );
  const int seasonEndDoy = std::clamp( getInt( params, "seasonEndDoy", 366 ), 1, 366 );
  const int minValidPerSeason = std::max( getInt( params, "minValidPerSeason", 3 ), 1 );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 3 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "phenology needs at least 3 scenes (got " +
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
    // documented default: explicit band > role > positional fallback > band 1
    // An EXPLICIT band_role that resolves nowhere is a caller error: silently
    // falling back to band 1 would analyze a different band than requested
    // (e.g. "vh" advertised but absent → VV analyzed instead).
    if ( band <= 0 && !bandRole.isEmpty() && bandOverride <= 0 )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "band_role '" + bandRole.toStdString() +
                                 "' cannot be resolved in scene " +
                                 prepared.collection.scenes().at( s ).path.toStdString() +
                                 "; pass an explicit band or fix the scene metadata" );
    analysisBands[s] = band > 0 ? band : 1;
    anyFallback = anyFallback || fallback;
  }
  if ( anyFallback )
    context.logWarning( "Analysis band resolved by positional fallback for at least one scene; "
                        "pass 'band' or 'bands' to pin it." );

  // Real time axis: days since the collection reference epoch (scene 0).
  std::vector<double> tDays( sceneCount );
  for ( int s = 0; s < sceneCount; ++s )
    tDays[s] = reader.sceneDayOffset( s );

  // Day-of-year per scene from the REAL UTC acquisition instant (0 when the
  // time is invalid) — drives the season extraction and the SOS/POS/EOS
  // reporting units.
  std::vector<int> doyOf( sceneCount, 0 );
  for ( int s = 0; s < sceneCount; ++s )
  {
    const auto &scene = reader.scene( s );
    doyOf[s] = scene.time.valid
                 ? QDateTime::fromMSecsSinceEpoch( scene.time.epochMillis, QTimeZone::utc() )
                     .date()
                     .dayOfYear()
                 : 0;
  }

  const int width = reader.width();
  const int height = reader.height();

  context.reportProgress( 0.05, "Creating phenology output" );
  GdalDatasetWrapper out;
  QString outErr;
  temporal_output::TemporalOutputGuard guard;
  guard.manage( &out, QString::fromStdString( outputPath ) );
  if ( !out.create( QString::fromStdString( outputPath ), width, height, kPhenologyBandCount,
                    static_cast<int>( GDT_Float32 ), reader.geoTransform(), reader.projection(),
                    &outErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to create output: " + outErr.toStdString() );
  for ( int b = 1; b <= kPhenologyBandCount; ++b )
  {
    out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b ),
                        kPhenologyBandNames[b - 1] );
  }

  const int tiles = reader.totalTileCount();
  // Guard against a nominal tile_size parameter pretending the working set is
  // bounded: allocations size the LARGEST CLAMPED tile rect (edge tiles are
  // smaller), and a series gathering that would exceed ~2 GB is rejected up
  // front with guidance instead of being attempted (OOM guard).
  const size_t maxTilePixels =
      static_cast<size_t>( std::min( tileSize, width ) ) *
      static_cast<size_t>( std::min( tileSize, height ) );
  constexpr size_t kMaxSeriesBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
  if ( static_cast<size_t>( sceneCount ) * maxTilePixels * sizeof( float ) >
       kMaxSeriesBytes )
    throw RSOperatorError(
        ErrorCode::InvalidParameter,
        "tile_size " + std::to_string( tileSize ) + " x " +
            std::to_string( sceneCount ) +
            " scenes exceeds the 2 GiB series-gathering budget; reduce tile_size "
        "(or scene count)" );
  const size_t tilePixels = maxTilePixels;

  std::vector<float> tile( tilePixels );
  std::vector<float> series( static_cast<size_t>( sceneCount ) * tilePixels );
  std::vector<float> metricBufs( static_cast<size_t>( kPhenologyBandCount ) * tilePixels );
  std::vector<float> pixSeries( sceneCount );
  std::uint64_t validPixels = 0;
  int tileDone = 0;

  for ( int t = 0; t < tiles; ++t )
  {
    int x = 0, y = 0, w = 0, h = 0;
    reader.tileRect( t, &x, &y, &w, &h );
    const size_t pixels = static_cast<size_t>( w ) * h;

    // Read every scene once into the per-pixel series (NaN = masked/missing).
    for ( int s = 0; s < sceneCount; ++s )
    {
      if ( !reader.readSceneBandTile( s, analysisBands[s], t, tile.data() ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "failed reading scene " +
                                   prepared.collection.scenes().at( s ).path.toStdString() );
      std::copy( tile.data(), tile.data() + pixels, series.data() + s * tilePixels );
      context.throwIfCancelled();
    }

    // One threshold scan per pixel over the whole series for the requested
    // season window (kernel extracts the in-season samples across all years).
    for ( size_t i = 0; i < pixels; ++i )
    {
      int validInSeason = 0;
      for ( int s = 0; s < sceneCount; ++s )
      {
        const float v = series[s * tilePixels + i];
        pixSeries[s] = v;
        if ( !std::isfinite( v ) )
          continue;
        const int doy = doyOf[s];
        const bool inSeason =
          seasonStartDoy <= seasonEndDoy
            ? ( doy >= seasonStartDoy && doy <= seasonEndDoy )
            : ( doy >= seasonStartDoy || doy <= seasonEndDoy );
        if ( inSeason )
          ++validInSeason;
      }
      const bool ok = validInSeason >= minValidPerSeason;
      if ( ok )
      {
        const sicnu::temporal::SeasonalMetrics m = sicnu::temporal::phenologyThreshold(
          pixSeries, tDays, doyOf, seasonStartDoy, seasonEndDoy, crossingFraction );
        const float *values = nullptr;
        float season[kPhenologyBandCount];
        if ( m.valid )
        {
          season[0] = static_cast<float>( m.sos );
          season[1] = static_cast<float>( m.pos );
          season[2] = static_cast<float>( m.eos );
          season[3] = static_cast<float>( m.los );
          season[4] = static_cast<float>( m.amplitude );
          season[5] = static_cast<float>( m.base );
          season[6] = static_cast<float>( m.integral );
          values = season;
          ++validPixels;
        }
        for ( int b = 0; b < kPhenologyBandCount; ++b )
          metricBufs[static_cast<size_t>( b ) * tilePixels + i] =
            values ? values[b] : kNan;
      }
      else
      {
        for ( int b = 0; b < kPhenologyBandCount; ++b )
          metricBufs[static_cast<size_t>( b ) * tilePixels + i] = kNan;
      }
    }
    context.throwIfCancelled();

    for ( int b = 0; b < kPhenologyBandCount; ++b )
    {
      if ( !out.writeBandWindow( b + 1, x, y, w, h,
                                 metricBufs.data() + static_cast<size_t>( b ) * tilePixels ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing phenology band" );
    }

    ++tileDone;
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                            "Phenology tiles " + std::to_string( tileDone ) + "/" +
                                std::to_string( tiles ) );
  }

  temporal_output::writeTemporalDatasetMetadata(
    out, prepared.collection, "rs:temporal_phenology",
    QStringLiteral( "band_role=%1 season=%2-%3 crossing=%4 minValidPerSeason=%5" )
        .arg( bandRole.isEmpty() ? QStringLiteral( "band:1" ) : bandRole )
        .arg( seasonStartDoy )
        .arg( seasonEndDoy )
        .arg( crossingFraction )
        .arg( minValidPerSeason ) );
  if ( !prepared.preflight.commonRadiometricState.isEmpty() )
    GDALSetMetadataItem( static_cast<GDALDatasetH>( out.dataset() ), "SICNU_RADIOMETRIC_STATE",
                         prepared.preflight.commonRadiometricState.toUtf8().constData(), nullptr );

  QString closeErr;
  if ( !out.closeWithError( &closeErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "output flush failed (disk full?): " + closeErr.toStdString() );
  guard.commit();

  const double totalPixels = static_cast<double>( width ) * height;
  Json::Value result( Json::objectValue );
  result["output"] = outputPath;
  result["sceneCount"] = sceneCount;
  Json::Value metrics( Json::arrayValue );
  for ( const char *name : kPhenologyBandNames )
    metrics.append( name );
  result["metrics"] = metrics;
  result["bands"] = kPhenologyBandCount;
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  result["validPixelFraction"] =
      totalPixels > 0 ? static_cast<double>( validPixels ) / totalPixels : 0.0;
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  // series per scene + 7 metric buffers + gather/tile buffers.
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
    TemporalTileReader::estimateWorkingSetBytes( tileSize, tileSize,
                                                 sceneCount + kPhenologyBandCount + 2, 0 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal phenology complete" );
  return result;
}

} // namespace sicnu::operators::rs
