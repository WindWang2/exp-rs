// src/operators/rs/rs_temporal_regularize_operator.cpp
#include "rs_temporal_regularize_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "operators/rs/rs_temporal_output.h"
#include "processing/algorithms/temporal/temporal_calendar.h"
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
constexpr int kDefaultCadenceDays = 16;
constexpr double kDefaultMaxWindowDays = 32.0;
constexpr int kTypicalSceneEstimate = 12;
constexpr int kMaxCalendarPoints = 2000; // declared output-width guard

/// "YYYY-MM-DD" of the collection reference epoch (scene 0 of the sorted
/// collection) — the t = 0 anchor of the calendar grid.
QString epochDateString( const temporal::TemporalCollection &collection )
{
  if ( collection.sceneCount() == 0 )
    return QString();
  return collection.scenes().at( 0 ).time.dateString();
}
} // namespace

std::string RsTemporalRegularizeOperator::description() const
{
  return "Re-cast an irregular acquisition series onto a regular calendar "
         "(e.g. every 16 days, or monthly) so downstream series models see a "
         "uniform grid. Methods: nearest (closest observation within "
         "max_window_days, ties -> earlier), window_mean (mean of the "
         "observations inside the window), linear (time-weighted "
         "interpolation between the bracketing observations), whittaker "
         "(penalized smoother defined on the calendar grid; bridges data-free "
         "runs up to max_gap_nodes). No method extrapolates past the observed "
         "span. Every calendar point carries valid_count / filled_count "
         "provenance bands so synthetic values stay distinguishable from "
         "observed ones.";
}

Json::Value RsTemporalRegularizeOperator::schema() const
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
                                         "...or workspace collection id / descriptor JSON path", "" );
  props["band"] = makeIntegerParam( "band", "Explicit analysis band (1-based)", 0 );
  props["band_role"] = makeEnumParam( "band_role", "Analysis band role",
                                      { "blue", "green", "red", "red_edge", "nir", "swir1",
                                        "swir2", "vv", "vh" },
                                      "" );
  props["duplicate_policy"] = makeEnumParam( "duplicate_policy", "Duplicate acquisition instants",
                                             { "keep_all", "reject" }, "keep_all" );
  props["apply_qa_masking"] = makeBooleanParam( "apply_qa_masking", "Exclude QA/cloud-masked samples", true );
  props["cadence"] = makeStringParam( "cadence",
                                      "Calendar step: '<N>d' (e.g. '16d') or 'month'. Derived from the "
                                      "first acquisition (the grid anchor)",
                                      "16d" );
  props["method"] = makeEnumParam( "method",
                                   "nearest: closest observation in the window; window_mean: mean of "
                                   "the window's observations; linear: day-weighted interpolation; "
                                   "whittaker: penalized fit on the calendar grid",
                                   { "nearest", "window_mean", "linear", "whittaker" }, "linear" );
  Json::Value maxWindow = makeNumberParam(
      "max_window_days", "Nearest / window_mean membership radius (days)", kDefaultMaxWindowDays );
  setRange( maxWindow, 0.0, 3660.0 );
  props["max_window_days"] = maxWindow;
  Json::Value lambda = makeNumberParam( "lambda", "Whittaker smoothness penalty (> 0)", 10.0 );
  setRange( lambda, 0.000001, 100000.0 );
  props["lambda"] = lambda;
  Json::Value maxGapNodes = makeIntegerParam(
      "max_gap_nodes", "Whittaker: longest observation-free run (calendar nodes) the penalty may bridge", 6 );
  setRange( maxGapNodes, 1, 365 );
  props["max_gap_nodes"] = maxGapNodes;
  props["tile_size"] = makeIntegerParam( "tile_size", "Streaming tile size (pixels)", kDefaultTileSize );
  props["output"] = makeOutputParam(
      "output",
      "Regularized GeoTIFF (one reg_<date> band per calendar point, plus "
      "valid_count and filled_count provenance bands)",
      "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Regularized GeoTIFF", "tif" );
  outputs["calendarPoints"] = makeIntegerParam( "calendarPoints", "Calendar grid size", 0 );
  outputs["cadenceDays"] = makeNumberParam( "cadenceDays", "Effective cadence (days; monthly uses the mean month length)", 0.0 );
  outputs["calendarStart"] = makeStringParam( "calendarStart", "First calendar point (ISO)", "" );
  outputs["calendarEnd"] = makeStringParam( "calendarEnd", "Last calendar point (ISO)", "" );
  outputs["filledFraction"] = makeNumberParam( "filledFraction",
                                               "Synthetic (filled) values / finite values", 0.0 );
  outputs["bands"] = makeIntegerParam( "bands", "Output band count (= calendarPoints + 2)", 0 );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Scenes in the input series", 0 );
  Json::Value memory = makeStringParam( "memory", "Streaming working-set summary" );
  memory["type"] = "object";
  outputs["memory"] = memory;
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output", "cadence" } );
  return root;
}

Json::Value RsTemporalRegularizeOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "resampling" );
  tags.append( "regular-calendar" );
  meta["tags"] = tags;
  meta["purpose"] = "Normalize irregular acquisitions (cloud-gapped, mixed revisit) onto a "
                    "uniform calendar for smoothing / phenology / change models";
  meta["prerequisites"] = "Common grid, acquisition times, consistent radiometric state "
                          "(temporal preflight); at least 2 scenes";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes + tile × calendarPoints)";
  meta["workflowHints"] = "Typical chain: rs:temporal_regularize -> rs:temporal_phenology or "
                          "rs:temporal_harmonic_breaks; check filledFraction before trusting "
                          "downstream statistics; the filled_count band flags synthetic values "
                          "per pixel";
  meta["limitations"] = "No method extrapolates past the first/last observation; whittaker "
                        "values are synthetic wherever no observation maps to the calendar "
                        "node (see filled_count); the calendar anchor is the first acquisition "
                        "date, so different collections anchor differently";
  return meta;
}

Json::Value RsTemporalRegularizeOperator::executionEstimate() const
{
  // 1 read tile + one series tile per scene + 3 output tiles (value +
  // valid_count + filled_count), typical cadence estimate.
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4,
                                                   2 + kTypicalSceneEstimate, 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalRegularizeOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  int scenes = kTypicalSceneEstimate;
  if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
    scenes = std::max<int>( 1, static_cast<int>( params["scenes"].size() ) );
  return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4,
                                                   2 + static_cast<std::uint64_t>( scenes ), 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalRegularizeOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const QString cadenceToken = QString::fromStdString( getString( params, "cadence", "16d" ) );

  temporal::CalendarSpec spec;
  if ( !temporal::parseCadenceToken( cadenceToken, &spec ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "cadence must be '<N>d' (e.g. '16d') or 'month'" );

  const QString methodToken = QString::fromStdString(
      getEnum( params, "method", { "nearest", "window_mean", "linear", "whittaker" }, "linear" ) );
  temporal::RegularizeOptions options;
  if ( methodToken == QLatin1String( "nearest" ) )
    options.method = temporal::RegularizeMethod::Nearest;
  else if ( methodToken == QLatin1String( "window_mean" ) )
    options.method = temporal::RegularizeMethod::WindowMean;
  else if ( methodToken == QLatin1String( "whittaker" ) )
    options.method = temporal::RegularizeMethod::Whittaker;
  else
    options.method = temporal::RegularizeMethod::Linear;
  options.maxWindowDays = getDouble( params, "max_window_days", kDefaultMaxWindowDays );
  if ( !( options.maxWindowDays >= 0.0 ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, "max_window_days must be >= 0" );
  options.lambda = getDouble( params, "lambda", 10.0 );
  if ( !( options.lambda > 0.0 ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, "lambda must be > 0" );
  options.maxGapNodes = std::clamp( getInt( params, "max_gap_nodes", 6 ), 1, 365 );

  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 2 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "regularization needs at least 2 scenes (got " +
                               std::to_string( sceneCount ) + ")" );
  const QString epochDate = epochDateString( prepared.collection );
  if ( epochDate.isEmpty() )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "collection reference scene has no parsable acquisition date" );

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

  std::vector<double> tDays( sceneCount );
  for ( int s = 0; s < sceneCount; ++s )
    tDays[s] = reader.sceneDayOffset( s );

  // Calendar over the observed day-offset span (the reader's span == the
  // observations' span by construction).
  const std::vector<temporal::CalendarPoint> calendar = temporal::buildRegularCalendar(
      epochDate, tDays.front(), tDays.back(), spec );
  if ( calendar.empty() || static_cast<int>( calendar.size() ) > kMaxCalendarPoints )
    throw RSOperatorError(
        ErrorCode::InvalidParameter,
        "calendar has " + std::to_string( calendar.size() ) +
            " points for cadence '" + cadenceToken.toStdString() + "' over the observed span"
            " (guard " + std::to_string( kMaxCalendarPoints ) + "); widen the cadence" );
  const int calCount = static_cast<int>( calendar.size() );

  const int width = reader.width();
  const int height = reader.height();
  const int bandCount = calCount + 2; // reg_<date> bands + valid_count + filled_count

  context.reportProgress( 0.05, "Creating regularized output" );
  std::vector<QString> bandNames( static_cast<size_t>( bandCount ) );
  for ( int c = 0; c < calCount; ++c )
    bandNames[static_cast<size_t>( c )] =
        QStringLiteral( "reg_%1" ).arg( calendar[static_cast<size_t>( c )].isoDate );
  bandNames[static_cast<size_t>( calCount )] = QStringLiteral( "valid_count" );
  bandNames[static_cast<size_t>( calCount + 1 )] = QStringLiteral( "filled_count" );

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
                        bandNames[static_cast<size_t>( b - 1 )].toUtf8().constData() );
  }

  // Working-set guard (same budget discipline as gap_fill): the gathered
  // series tile + the band-major regularized tile must stay under ~2 GiB.
  const size_t maxTilePixels =
      static_cast<size_t>( std::min( tileSize, width ) ) *
      static_cast<size_t>( std::min( tileSize, height ) );
  constexpr size_t kMaxSeriesBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
  const size_t tileFloatsPerPixel = static_cast<size_t>( sceneCount ) + static_cast<size_t>( calCount );
  if ( tileFloatsPerPixel * maxTilePixels * sizeof( float ) > kMaxSeriesBytes )
    throw RSOperatorError(
        ErrorCode::InvalidParameter,
        "tile_size " + std::to_string( tileSize ) + " x (" +
            std::to_string( sceneCount ) + " scenes + " + std::to_string( calCount ) +
            " calendar points) exceeds the 2 GiB tile budget; reduce tile_size "
            "(or widen the cadence)" );
  const size_t tilePixels = maxTilePixels;

  // Column-major per-pixel series: series[s * tilePixels + i]; band-major
  // regularized values: values[c * tilePixels + i] (contiguous per calendar
  // point -> written straight to the output band).
  std::vector<float> series( tilePixels * static_cast<size_t>( sceneCount ) );
  std::vector<float> values( tilePixels * static_cast<size_t>( calCount ) );
  std::vector<float> tile( tilePixels );
  std::vector<float> validCountTile( tilePixels );
  std::vector<float> filledCountTile( tilePixels );
  std::vector<float> pixelSeries( static_cast<size_t>( sceneCount ) );

  std::uint64_t totalFinite = 0;
  std::uint64_t totalFilled = 0;
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
        pixelSeries[static_cast<size_t>( s )] = series[s * tilePixels + i];

      const temporal::RegularizedSeries reg = temporal::regularizeSeries(
          pixelSeries, tDays, calendar, options );
      float validCount = 0.0f;
      float filledCount = 0.0f;
      for ( int c = 0; c < calCount; ++c )
      {
        const auto &p = reg.points[static_cast<size_t>( c )];
        values[static_cast<size_t>( c ) * tilePixels + i] = p.value;
        validCount += static_cast<float>( p.validObservations );
        filledCount += p.filled ? 1.0f : 0.0f;
        if ( std::isfinite( p.value ) )
        {
          ++totalFinite;
          if ( p.filled )
            ++totalFilled;
        }
      }
      validCountTile[i] = validCount;
      filledCountTile[i] = filledCount;
    }

    for ( int c = 0; c < calCount; ++c )
    {
      if ( !out.writeBandWindow( c + 1, x, y, w, h,
                                 values.data() + static_cast<std::ptrdiff_t>( c ) *
                                                     static_cast<std::ptrdiff_t>( tilePixels ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing regularized band" );
      context.throwIfCancelled();
    }
    if ( !out.writeBandWindow( calCount + 1, x, y, w, h, validCountTile.data() ) )
      throw RSOperatorError( ErrorCode::GdalError, "failed writing valid_count band" );
    if ( !out.writeBandWindow( calCount + 2, x, y, w, h, filledCountTile.data() ) )
      throw RSOperatorError( ErrorCode::GdalError, "failed writing filled_count band" );

    ++tileDone;
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                            "Regularize tiles " + std::to_string( tileDone ) + "/" +
                                std::to_string( tiles ) );
  }

  for ( int c = 0; c < calCount; ++c )
  {
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), c + 1 ),
                        bandNames[static_cast<size_t>( c )].toUtf8().constData() );
  }
  temporal_output::writeTemporalDatasetMetadata(
      out, prepared.collection, "rs:temporal_regularize",
      QStringLiteral( "cadence=%1 method=%2" ).arg( cadenceToken, methodToken ) );
  if ( !prepared.preflight.commonRadiometricState.isEmpty() )
    GDALSetMetadataItem( static_cast<GDALDatasetH>( out.dataset() ), "SICNU_RADIOMETRIC_STATE",
                         prepared.preflight.commonRadiometricState.toUtf8().constData(), nullptr );

  QString closeErr;
  if ( !out.closeWithError( &closeErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "output flush failed (disk full?): " + closeErr.toStdString() );
  guard.commit();

  Json::Value result( Json::objectValue );
  result["output"] = outputPath;
  result["calendarPoints"] = calCount;
  result["cadenceDays"] = spec.cadence == temporal::CalendarCadence::Days
                              ? Json::Value( spec.cadenceDays )
                              : Json::Value( 30.4375 );
  result["calendarStart"] = calendar.front().isoDate.toStdString();
  result["calendarEnd"] = calendar.back().isoDate.toStdString();
  result["filledFraction"] =
      totalFinite > 0
          ? static_cast<double>( totalFilled ) / static_cast<double>( totalFinite )
          : 0.0;
  result["bands"] = bandCount;
  result["sceneCount"] = sceneCount;
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
      TemporalTileReader::estimateWorkingSetBytes(
          tileSize, tileSize,
          3 + static_cast<std::uint64_t>( sceneCount ) + static_cast<std::uint64_t>( calCount ),
          0 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal regularize complete" );
  return result;
}

} // namespace sicnu::operators::rs
