// src/operators/rs/rs_temporal_breakpoints_operator.cpp
#include "rs_temporal_breakpoints_operator.h"

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
constexpr int kDefaultMaxBreaks = 2;
constexpr int kMaxBreaksLimit = 8;
constexpr int kTypicalSceneCount = 8;
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
} // namespace

std::string RsTemporalBreakpointsOperator::description() const
{
  return "Per-pixel piecewise-linear trend segmentation with break detection "
         "(BSFAST-lite): the series (real acquisition day offsets) is split "
         "greedily where an additional OLS segment reduces the residual sum of "
         "squares the most, while the relative RSS reduction exceeds "
         "minImprovement and both sides keep the minimum segment sample count "
         "(derived from minSegmentDays and the collection time span). Outputs "
         "the break count, break dates (day offsets from the first "
         "acquisition), one per-day slope per segment and the overall RMSE.";
}

Json::Value RsTemporalBreakpointsOperator::schema() const
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
  Json::Value maxBreaksParam = makeIntegerParam(
      "maxBreaks", "Maximum number of breaks (segments = maxBreaks + 1)", kDefaultMaxBreaks );
  setRange( maxBreaksParam, 0, kMaxBreaksLimit );
  props["maxBreaks"] = maxBreaksParam;
  props["minSegmentDays"] = makeNumberParam( "minSegmentDays",
                                             "Minimum segment length in days — converted to a "
                                             "minimum segment sample count from the collection "
                                             "time span",
                                             90.0 );
  Json::Value minImprovementParam = makeNumberParam(
      "minImprovement", "Minimum relative RSS reduction to accept a split", 0.1 );
  setRange( minImprovementParam, 0.01, 1.0 );
  props["minImprovement"] = minImprovementParam;
  props["outputBreakDates"] = makeBooleanParam( "outputBreakDates",
                                                "Write one break_date_k band per allowed break "
                                                "(day offsets; NoData beyond a pixel's break count)",
                                                true );
  props["output"] = makeOutputParam( "output",
                                     "Breakpoints GeoTIFF (bands: break_count"
                                     "[, break_date_1..k,] slope_seg_1..k+1, rmse)",
                                     "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Breakpoints GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Dates in the series", 0 );
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalBreakpointsOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "breakpoint" );
  tags.append( "change-detection" );
  meta["tags"] = tags;
  meta["purpose"] = "Per-pixel piecewise-linear trend segmentation (BSFAST-lite) — locates WHEN a "
                    "time series changed (break dates) and the before/after rates (per-segment "
                    "per-day slopes), e.g. disturbance and recovery analysis";
  meta["prerequisites"] = "Common grid, acquisition times, consistent radiometric state "
                          "(temporal preflight); a break needs the minimum segment sample count "
                          "on each side";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes × breaks)";
  meta["workflowHints"] = "Run on rs:temporal_smooth / rs:temporal_gap_fill output or an "
                          "rs:temporal_index_series band; break dates are day offsets from the "
                          "first acquisition (multiply segment slopes by 365.25 for per-year "
                          "rates)";
  meta["limitations"] = "Greedy binary segmentation (not the global optimum); a break needs >= "
                        "minSegment samples on each side; segments without enough valid samples "
                        "degenerate to a flat mean fit";
  return meta;
}

Json::Value RsTemporalBreakpointsOperator::executionEstimate() const
{
  // Typical 8-date collection, default maxBreaks: series per scene + count +
  // break-date rows + slope rows + RMSE + pixel gather + read tile.
  const int buffers = kTypicalSceneCount + ( 1 + kDefaultMaxBreaks + ( kDefaultMaxBreaks + 1 ) + 1 ) + 2;
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4, buffers, 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalBreakpointsOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const int maxBreaks = std::clamp( getInt( params, "maxBreaks", kDefaultMaxBreaks ), 0, kMaxBreaksLimit );
  const bool outputBreakDates = getBool( params, "outputBreakDates", true );
  int scenes = params["scenes"].isArray() ? params["scenes"].size() : kTypicalSceneCount;
  scenes = std::max( scenes, 1 );
  const int metricBuffers = 1 + ( outputBreakDates ? maxBreaks : 0 ) + ( maxBreaks + 1 ) + 1;
  // series per scene + metric tile buffers + per-pixel series gather + read tile.
  const int buffers = scenes + metricBuffers + 2;
  return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4, buffers, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalBreakpointsOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );
  const int maxBreaks = std::clamp( getInt( params, "maxBreaks", kDefaultMaxBreaks ), 0, kMaxBreaksLimit );
  double minSegmentDays = getDouble( params, "minSegmentDays", 90.0 );
  if ( !( minSegmentDays > 0.0 ) )
    minSegmentDays = 90.0;
  const double minImprovement = std::clamp( getDouble( params, "minImprovement", 0.1 ), 0.01, 1.0 );
  const bool outputBreakDates = getBool( params, "outputBreakDates", true );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 2 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "breakpoint detection needs at least 2 scenes (got " +
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
    analysisBands[s] = band > 0 ? band : 1;
    anyFallback = anyFallback || fallback;
  }
  if ( anyFallback )
    context.logWarning( "Analysis band resolved by positional fallback for at least one scene; "
                        "pass 'band' or 'bands' to pin it." );

  // Real time axis: days since the collection reference epoch (scene 0).
  // Break dates are reported in the same units.
  std::vector<double> tDays( sceneCount );
  for ( int s = 0; s < sceneCount; ++s )
    tDays[s] = reader.sceneDayOffset( s );

  // Day-of-year per scene from the REAL UTC acquisition instant (0 when the
  // time is invalid). Not used by the segmentation (it works on day offsets)
  // but computed for the uniform temporal-operator prologue.
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

  // minSegmentDays → a minimum SEGMENT SAMPLE COUNT: roughly regular sampling
  // puts about totalSpan/minSegmentDays scenes inside any window of that
  // span. Simplest correct conversion (documented):
  //   minSegment = clamp( floor(totalSpan / minSegmentDays), 1, sceneCount )
  // e.g. a 90-day minimum over a 720-day span → 8 samples per segment. The
  // kernel additionally floors this at 3 and requires 2·minSegment samples to
  // consider a split at all.
  const double totalSpan = tDays.back() - tDays.front();
  int minSegment = 1;
  if ( totalSpan > 0.0 )
    minSegment =
      std::clamp( static_cast<int>( std::floor( totalSpan / minSegmentDays ) ), 1, sceneCount );

  const int dateBands = outputBreakDates ? maxBreaks : 0;
  const int bandCount = 1 + dateBands + ( maxBreaks + 1 ) + 1; // default 2 breaks + dates → 7
  const int width = reader.width();
  const int height = reader.height();

  context.reportProgress( 0.05, "Creating breakpoints output" );
  GdalDatasetWrapper out;
  QString outErr;
  temporal_output::TemporalOutputGuard guard;
  guard.manage( &out, QString::fromStdString( outputPath ) );
  if ( !out.create( QString::fromStdString( outputPath ), width, height, bandCount,
                    static_cast<int>( GDT_Float32 ), reader.geoTransform(), reader.projection(),
                    &outErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to create output: " + outErr.toStdString() );
  int b = 1;
  out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
  GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b++ ),
                      "break_count" );
  for ( int j = 1; outputBreakDates && j <= maxBreaks; ++j )
  {
    out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b++ ),
                        QStringLiteral( "break_date_%1" ).arg( j ).toUtf8().constData() );
  }
  for ( int j = 1; j <= maxBreaks + 1; ++j )
  {
    out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b++ ),
                        QStringLiteral( "slope_seg_%1" ).arg( j ).toUtf8().constData() );
  }
  out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
  GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b++ ), "rmse" );

  const int tiles = reader.totalTileCount();
  const size_t tilePixels = static_cast<size_t>( tileSize ) * tileSize;

  std::vector<float> tile( tilePixels );
  std::vector<float> series( static_cast<size_t>( sceneCount ) * tilePixels );
  std::vector<float> countBuf( tilePixels );
  std::vector<float> dateBufs( static_cast<size_t>( dateBands ) * tilePixels );
  std::vector<float> slopeBufs( static_cast<size_t>( maxBreaks + 1 ) * tilePixels );
  std::vector<float> rmseBuf( tilePixels );
  std::vector<float> pixSeries( sceneCount );
  std::uint64_t brokenPixels = 0;
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

    // One greedy segmentation per pixel over the whole series.
    for ( size_t i = 0; i < pixels; ++i )
    {
      for ( int s = 0; s < sceneCount; ++s )
        pixSeries[s] = series[s * tilePixels + i];
      const sicnu::temporal::BreakpointResult br = sicnu::temporal::piecewiseLinearTrend(
        pixSeries, tDays, maxBreaks, minSegment, minImprovement );
      const int breaks = static_cast<int>( br.breakIndices.size() );
      countBuf[i] = static_cast<float>( breaks );
      for ( int j = 0; j < dateBands; ++j )
        dateBufs[static_cast<size_t>( j ) * tilePixels + i] =
          j < breaks ? static_cast<float>( tDays[br.breakIndices[j]] ) : kNan;
      const int segments = static_cast<int>( br.slopes.size() );
      for ( int j = 0; j <= maxBreaks; ++j )
        slopeBufs[static_cast<size_t>( j ) * tilePixels + i] =
          j < segments ? static_cast<float>( br.slopes[j] ) : kNan;
      rmseBuf[i] = static_cast<float>( br.rmse );
      if ( breaks >= 1 )
        ++brokenPixels;
    }
    context.throwIfCancelled();

    auto writeBand = [&]( int band, const float *src ) {
      if ( !out.writeBandWindow( band, x, y, w, h, src ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing breakpoints band" );
    };
    int band = 1;
    writeBand( band++, countBuf.data() );
    for ( int j = 0; j < dateBands; ++j )
      writeBand( band++, dateBufs.data() + static_cast<size_t>( j ) * tilePixels );
    for ( int j = 0; j <= maxBreaks; ++j )
      writeBand( band++, slopeBufs.data() + static_cast<size_t>( j ) * tilePixels );
    writeBand( band++, rmseBuf.data() );

    ++tileDone;
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                            "Breakpoint tiles " + std::to_string( tileDone ) + "/" +
                                std::to_string( tiles ) );
  }

  temporal_output::writeTemporalDatasetMetadata(
    out, prepared.collection, "rs:temporal_breakpoints",
    QStringLiteral( "band_role=%1 maxBreaks=%2 minSegmentDays=%3 minSegment=%4 minImprovement=%5" )
        .arg( bandRole.isEmpty() ? QStringLiteral( "band:1" ) : bandRole )
        .arg( maxBreaks )
        .arg( minSegmentDays )
        .arg( minSegment )
        .arg( minImprovement ) );
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
  result["maxBreaks"] = maxBreaks;
  result["bands"] = bandCount;
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  result["brokenPixelFraction"] =
      totalPixels > 0 ? static_cast<double>( brokenPixels ) / totalPixels : 0.0;
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  // series per scene + count/date/slope/RMSE buffers + gather/tile buffers.
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
    TemporalTileReader::estimateWorkingSetBytes( tileSize, tileSize,
                                                 sceneCount + 1 + dateBands + ( maxBreaks + 1 ) + 1 + 2,
                                                 0 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal breakpoints complete" );
  return result;
}

} // namespace sicnu::operators::rs
