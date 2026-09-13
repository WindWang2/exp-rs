// src/operators/rs/rs_temporal_harmonic_breaks_operator.cpp
#include "rs_temporal_harmonic_breaks_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "operators/rs/rs_temporal_output.h"
#include "processing/algorithms/temporal/temporal_change.h"
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
constexpr int kDefaultMaxBreaks = 3;
constexpr int kMaxBreaksLimit = 8;
constexpr int kTypicalSceneEstimate = 12;
} // namespace

std::string RsTemporalHarmonicBreaksOperator::description() const
{
  return "Joint seasonal-trend change segmentation: each segment models a "
         "linear trend PLUS seasonal harmonics (sin/cos of 2*PI*k*t/365.25), "
         "and breaks are detected as trend changes of the "
         "seasonality-adjusted residual, greedily, while each split lowers "
         "the residual RSS by more than minImprovement and both sides keep "
         "the minimum segment length. Greedy BFAST/CCDC-inspired method - "
         "NOT the full BFAST (no iterative season/trend alternation) or CCDC "
         "(no L1 / per-segment model selection). Outputs per pixel: break "
         "count, per-break calendar day offsets and fitted-level jump "
         "magnitudes, first/last segment slopes, overall RMSE/R2; with a "
         "declared disturbance direction, the first onset day and its "
         "recovery length (days; negative when never recovered).";
}

Json::Value RsTemporalHarmonicBreaksOperator::schema() const
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
  Json::Value harmonics = makeIntegerParam( "harmonics", "Seasonal sin/cos pairs per segment (1-3)", 2 );
  setRange( harmonics, 1, 3 );
  props["harmonics"] = harmonics;
  Json::Value maxBreaks = makeIntegerParam( "maxBreaks", "Maximum breaks per pixel (segments = maxBreaks + 1)", kDefaultMaxBreaks );
  setRange( maxBreaks, 0, kMaxBreaksLimit );
  props["maxBreaks"] = maxBreaks;
  props["minSegmentDays"] = makeNumberParam(
      "minSegmentDays", "Minimum segment length in days - converted to a sample count "
                        "from the collection time span", 90.0 );
  Json::Value minImprovement = makeNumberParam(
      "minImprovement", "Minimum relative residual-RSS reduction to accept a split", 0.1 );
  setRange( minImprovement, 0.01, 1.0 );
  props["minImprovement"] = minImprovement;
  props["robust"] = makeBooleanParam( "robust",
                                      "IRLS Huber reweighting inside the final segment fits "
                                      "(dampens outliers; does not move breaks)", false );
  props["direction"] = makeEnumParam( "direction",
                                      "Disturbance direction for onset/recovery: 'decrease' "
                                      "models NDVI-like loss, 'increase' gain; 'none' skips "
                                      "the onset bands",
                                      { "none", "decrease", "increase" }, "none" );
  props["minMagnitude"] = makeNumberParam(
      "minMagnitude", "Minimum fitted-level jump (output units) for a break to count as an "
                      "onset", 0.0 );
  props["recoveryTolerance"] = makeNumberParam(
      "recoveryTolerance", "Fitted level must return within this absolute tolerance of the "
                           "pre-break level to count as recovered", 0.0 );
  props["tile_size"] = makeIntegerParam( "tile_size", "Streaming tile size (pixels)", kDefaultTileSize );
  props["output"] = makeOutputParam(
      "output",
      "Segmentation GeoTIFF: breaks_count, break_day_k, break_mag_k, "
      "slope_first, slope_last, rmse, r2, valid_count (+ onset_day, "
      "recovery_days when direction != none)", "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Segmentation GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Scenes in the series", 0 );
  outputs["pixelsWithBreaks"] = makeIntegerParam( "pixelsWithBreaks", "Pixels with >= 1 break", 0 );
  outputs["meanBreakMagnitude"] = makeNumberParam( "meanBreakMagnitude",
                                                   "Mean fitted-level jump over all reported breaks", 0.0 );
  outputs["epochDate"] = makeStringParam( "epochDate",
                                          "Break days are offsets from this first acquisition (ISO)", "" );
  Json::Value memory = makeStringParam( "memory", "Streaming working-set summary" );
  memory["type"] = "object";
  outputs["memory"] = memory;
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalHarmonicBreaksOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "change-detection" );
  tags.append( "harmonic-model" );
  tags.append( "segmentation" );
  meta["tags"] = tags;
  meta["purpose"] = "Detect land-surface regime changes that a global harmonic fit smooths "
                    "over and a linear-only segmentation seasons over: deforestation, flood "
                    "recovery, double-cropping shifts";
  meta["prerequisites"] = "Common grid, acquisition times, consistent radiometric state; "
                          ">= ~2 years of data for stable harmonics; at least 4 valid samples";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes × iterations + tile × segments × scenes × terms²)";
  meta["workflowHints"] = "Run on a smooth series (rs:temporal_smooth or rs:temporal_gap_fill "
                          "first). Chain with rs:threshold_raster on break_mag_1 to flag "
                          "large-change pixels; with direction=decrease the onset_day band "
                          "gives disturbance dates and recovery_days the regrowth length";
  meta["limitations"] = "Greedy splits are not a global optimum; harmonics use a fixed "
                        "365.25-day period (no per-segment model selection); the method is "
                        "BFAST/CCDC-inspired, not a full reimplementation; break day "
                        "resolution is the acquisition spacing";
  return meta;
}

Json::Value RsTemporalHarmonicBreaksOperator::executionEstimate() const
{
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4,
                                                   2 + kTypicalSceneEstimate, 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalHarmonicBreaksOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  int scenes = kTypicalSceneEstimate;
  if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
    scenes = std::max<int>( 1, static_cast<int>( params["scenes"].size() ) );
  return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4,
                                                   2 + static_cast<std::uint64_t>( scenes ), 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalHarmonicBreaksOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );

  const int harmonics = std::clamp( getInt( params, "harmonics", 2 ), 1, 3 );
  const int maxBreaks = std::clamp( getInt( params, "maxBreaks", kDefaultMaxBreaks ), 0, kMaxBreaksLimit );
  double minSegmentDays = getDouble( params, "minSegmentDays", 90.0 );
  if ( !( minSegmentDays > 0.0 ) )
    minSegmentDays = 90.0;
  const double minImprovement = getDouble( params, "minImprovement", 0.1 );
  if ( !( minImprovement > 0.0 && minImprovement <= 1.0 ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "minImprovement must be in (0, 1]" );
  const bool robust = getBool( params, "robust", false );
  const QString directionToken = QString::fromStdString(
      getEnum( params, "direction", { "none", "decrease", "increase" }, "none" ) );
  const bool decrease = directionToken == QLatin1String( "decrease" );
  const bool increase = directionToken == QLatin1String( "increase" );
  const double minMagnitude = getDouble( params, "minMagnitude", 0.0 );
  const double recoveryTolerance = getDouble( params, "recoveryTolerance", 0.0 );

  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 4 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "harmonic segmentation needs at least 4 scenes (got " +
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

  // minSegmentDays -> minimum segment sample count (the breakpoints operator
  // derivation: spacing = totalSpan/(N-1)).
  const double totalSpan = tDays.back() - tDays.front();
  int minSegment = 3;
  if ( totalSpan > 0.0 && sceneCount > 1 )
  {
    const double spacing = totalSpan / static_cast<double>( sceneCount - 1 );
    minSegment = std::clamp(
        static_cast<int>( std::lround( minSegmentDays / spacing ) ), 3, sceneCount / 2 );
  }

  // ISO date of a break day offset (the epoch is scene 0's acquisition date).
  const QString epochDate = prepared.collection.scenes().at( 0 ).time.dateString();
  const QDate epochQDate = QDate::fromString( epochDate, Qt::ISODate );

  const bool wantOnset = decrease || increase;
  const int bandCount = 1 +                     // breaks_count
                        maxBreaks +             // break_day_k
                        maxBreaks +             // break_mag_k
                        2 +                     // slope_first, slope_last
                        2 +                     // rmse, r2
                        1 +                     // valid_count
                        ( wantOnset ? 2 : 0 );  // onset_day, recovery_days
  const int width = reader.width();
  const int height = reader.height();

  context.reportProgress( 0.05, "Creating harmonic-breaks output" );
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
  auto describeBand = [&]( const QString &name ) {
    out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b ),
                        name.toUtf8().constData() );
    ++b;
  };
  describeBand( "breaks_count" );
  for ( int j = 1; j <= maxBreaks; ++j )
    describeBand( QStringLiteral( "break_day_%1" ).arg( j ) );
  for ( int j = 1; j <= maxBreaks; ++j )
    describeBand( QStringLiteral( "break_mag_%1" ).arg( j ) );
  describeBand( "slope_first" );
  describeBand( "slope_last" );
  describeBand( "rmse" );
  describeBand( "r2" );
  describeBand( "valid_count" );
  if ( wantOnset )
  {
    describeBand( "onset_day" );
    describeBand( "recovery_days" );
  }

  const size_t maxTilePixels =
      static_cast<size_t>( std::min( tileSize, width ) ) *
      static_cast<size_t>( std::min( tileSize, height ) );
  constexpr size_t kMaxSeriesBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
  if ( static_cast<size_t>( sceneCount ) * maxTilePixels * sizeof( float ) > kMaxSeriesBytes )
    throw RSOperatorError(
        ErrorCode::InvalidParameter,
        "tile_size " + std::to_string( tileSize ) + " x " +
            std::to_string( sceneCount ) +
            " scenes exceeds the 2 GiB series-gathering budget; reduce tile_size" );
  const size_t tilePixels = maxTilePixels;

  std::vector<float> tile( tilePixels );
  std::vector<float> series( static_cast<size_t>( sceneCount ) * tilePixels );
  std::vector<float> countBuf( tilePixels );
  std::vector<float> dayBufs( static_cast<size_t>( maxBreaks ) * tilePixels );
  std::vector<float> magBufs( static_cast<size_t>( maxBreaks ) * tilePixels );
  std::vector<float> slopeFirstBuf( tilePixels );
  std::vector<float> slopeLastBuf( tilePixels );
  std::vector<float> rmseBuf( tilePixels );
  std::vector<float> r2Buf( tilePixels );
  std::vector<float> validBuf( tilePixels );
  std::vector<float> onsetBuf( tilePixels );
  std::vector<float> recoveryBuf( tilePixels );
  std::vector<float> pixSeries( static_cast<size_t>( sceneCount ) );

  std::uint64_t pixelsWithBreaks = 0;
  double magnitudeSum = 0.0;
  std::uint64_t magnitudeCount = 0;
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

      const temporal::SeasonalTrendBreaksResult fit = temporal::fitSeasonalTrendBreaks(
          pixSeries, tDays, harmonics, maxBreaks, minSegment, minImprovement, robust );

      countBuf[i] = static_cast<float>( fit.breaks.size() );
      for ( int j = 0; j < maxBreaks; ++j )
      {
        const bool has = j < static_cast<int>( fit.breaks.size() );
        dayBufs[static_cast<size_t>( j ) * tilePixels + i] =
            has ? static_cast<float>( fit.breaks[static_cast<size_t>( j )].breakDays )
                : std::numeric_limits<float>::quiet_NaN();
        magBufs[static_cast<size_t>( j ) * tilePixels + i] =
            has ? static_cast<float>( fit.breaks[static_cast<size_t>( j )].magnitude )
                : std::numeric_limits<float>::quiet_NaN();
        if ( has && std::isfinite( fit.breaks[static_cast<size_t>( j )].magnitude ) )
        {
          magnitudeSum += fit.breaks[static_cast<size_t>( j )].magnitude;
          ++magnitudeCount;
        }
      }
      if ( !fit.breaks.empty() )
        ++pixelsWithBreaks;
      slopeFirstBuf[i] = fit.segments.empty()
                             ? std::numeric_limits<float>::quiet_NaN()
                             : static_cast<float>( fit.segments.front().slopePerDay );
      slopeLastBuf[i] = fit.segments.empty()
                            ? std::numeric_limits<float>::quiet_NaN()
                            : static_cast<float>( fit.segments.back().slopePerDay );
      rmseBuf[i] = std::isfinite( fit.rmse ) ? static_cast<float>( fit.rmse )
                                             : std::numeric_limits<float>::quiet_NaN();
      r2Buf[i] = std::isfinite( fit.r2 ) ? static_cast<float>( fit.r2 )
                                         : std::numeric_limits<float>::quiet_NaN();
      validBuf[i] = static_cast<float>( fit.validCount );
      if ( wantOnset )
      {
        double recoveryDays = 0.0;
        const double onset = temporal::disturbanceOnset(
            fit.fitted, tDays, fit.breaks, decrease, minMagnitude, &recoveryDays,
            recoveryTolerance );
        onsetBuf[i] = onset >= 0.0 ? static_cast<float>( onset )
                                   : std::numeric_limits<float>::quiet_NaN();
        recoveryBuf[i] = std::isfinite( recoveryDays )
                             ? static_cast<float>( recoveryDays )
                             : std::numeric_limits<float>::quiet_NaN();
      }
      context.throwIfCancelled();
    }

    int ob = 1;
    auto writeBand = [&]( const std::vector<float> &buf ) {
      if ( !out.writeBandWindow( ob++, x, y, w, h, buf.data() ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing output band" );
    };
    writeBand( countBuf ); // band 1: breaks_count
    ob = 2;
    for ( int j = 0; j < maxBreaks; ++j )
    {
      if ( !out.writeBandWindow(
               ob++, x, y, w, h,
               dayBufs.data() + static_cast<std::ptrdiff_t>( j ) *
                                    static_cast<std::ptrdiff_t>( tilePixels ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing break_day band" );
    }
    ob = 2 + maxBreaks;
    for ( int j = 0; j < maxBreaks; ++j )
    {
      if ( !out.writeBandWindow(
               ob++, x, y, w, h,
               magBufs.data() + static_cast<std::ptrdiff_t>( j ) *
                                    static_cast<std::ptrdiff_t>( tilePixels ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing break_mag band" );
    }
    ob = 2 + 2 * maxBreaks;
    writeBand( slopeFirstBuf );
    writeBand( slopeLastBuf );
    writeBand( rmseBuf );
    writeBand( r2Buf );
    writeBand( validBuf );
    if ( wantOnset )
    {
      writeBand( onsetBuf );
      writeBand( recoveryBuf );
    }

    ++tileDone;
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                            "Harmonic-break tiles " + std::to_string( tileDone ) + "/" +
                                std::to_string( tiles ) );
  }

  temporal_output::writeTemporalDatasetMetadata(
      out, prepared.collection, "rs:temporal_harmonic_breaks",
      QStringLiteral( "harmonics=%1 maxBreaks=%2 direction=%3 robust=%4" )
          .arg( harmonics )
          .arg( maxBreaks )
          .arg( directionToken )
          .arg( robust ? 1 : 0 ) );
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
  result["sceneCount"] = sceneCount;
  result["pixelsWithBreaks"] = Json::Value::UInt64( pixelsWithBreaks );
  result["meanBreakMagnitude"] =
      magnitudeCount > 0 ? magnitudeSum / static_cast<double>( magnitudeCount ) : 0.0;
  result["epochDate"] = epochDate.toStdString();
  result["method"] = "greedy seasonality-adjusted trend-break segmentation with "
                     "per-segment harmonic+trend refit (BFAST/CCDC-inspired; not full "
                     "BFAST/CCDC)";
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
      TemporalTileReader::estimateWorkingSetBytes(
          tileSize, tileSize, 3 + static_cast<std::uint64_t>( sceneCount ), 0 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal harmonic breaks complete" );
  return result;
}

} // namespace sicnu::operators::rs
