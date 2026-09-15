// src/operators/rs/rs_temporal_seasonal_breaks_operator.cpp
#include "rs_temporal_seasonal_breaks_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "operators/rs/rs_temporal_output.h"
#include "processing/algorithms/temporal/temporal_change.h"
#include "processing/algorithms/temporal/temporal_selection.h"
#include "processing/algorithms/temporal/temporal_stream.h"
#include "processing/algorithms/temporal/temporal_uncertainty.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QDateTime>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
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
// Break kind band encoding (documented in the schema + docs row).
constexpr float kKindNone = 0.0f;
constexpr float kKindTrend = 1.0f;
constexpr float kKindSeasonal = 2.0f;
constexpr float kKindBoth = 3.0f;
constexpr float kKindUntestable = 4.0f;

float kindToBand( temporal::BreakKind kind )
{
  switch ( kind )
  {
    case temporal::BreakKind::None:
      return kKindNone;
    case temporal::BreakKind::TrendOnly:
      return kKindTrend;
    case temporal::BreakKind::SeasonalOnly:
      return kKindSeasonal;
    case temporal::BreakKind::Both:
      return kKindBoth;
    case temporal::BreakKind::Untestable:
      break;
  }
  return kKindUntestable;
}
} // namespace

std::string RsTemporalSeasonalBreaksOperator::description() const
{
  return "Seasonal-component break detection with trend/seasonal "
         "attribution: segments each pixel with the shared joint "
         "harmonic+trend kernel, then tests every break with nested "
         "weighted-LS F comparisons — did the seasonal basis (harmonic "
         "sin/cos amplitude/phase) change beyond the trend change? Per "
         "break: attribution kind, day offset, fitted-level jump, seasonal "
         "coefficient shift, harmonic-1 amplitude/phase change, F statistic "
         "and p-value. BFAST-inspired seasonal-change testing - NOT full "
         "BFAST (no iterative alternation) and NOT CCDC (no L1 / online "
         "model update). Optional per-break bootstrap confidence intervals "
         "on the level jump (seeded, deterministic; refused - never "
         "fabricated - when too many refits fail).";
}

Json::Value RsTemporalSeasonalBreaksOperator::schema() const
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
  Json::Value harmonics =
    makeIntegerParam( "harmonics", "Seasonal sin/cos pairs per segment (1-3)", 2 );
  setRange( harmonics, 1, 3 );
  props["harmonics"] = harmonics;
  Json::Value maxBreaks =
    makeIntegerParam( "maxBreaks", "Maximum breaks per pixel (segments = maxBreaks + 1)",
                      kDefaultMaxBreaks );
  setRange( maxBreaks, 0, kMaxBreaksLimit );
  props["maxBreaks"] = maxBreaks;
  props["minSegmentDays"] = makeNumberParam(
      "minSegmentDays",
      "Minimum segment length in days - converted to a sample count from the "
      "collection time span",
      90.0 );
  Json::Value minImprovement = makeNumberParam(
      "minImprovement", "Minimum relative residual-RSS reduction to accept a split", 0.1 );
  setRange( minImprovement, 0.01, 1.0 );
  props["minImprovement"] = minImprovement;
  props["robust"] = makeBooleanParam(
      "robust",
      "IRLS Huber reweighting inside the segmentation's final segment fits "
      "(attribution tests always use plain fits)",
      false );
  Json::Value alpha = makeNumberParam(
      "alpha", "Significance level for both nested F tests (attribution)", 0.05 );
  setRange( alpha, 0.001, 0.5 );
  props["alpha"] = alpha;
  props["compute_ci"] = makeBooleanParam(
      "compute_ci",
      "Per-break bootstrap confidence intervals on the level jump "
      "(adds mag_ci_lo_k / mag_ci_hi_k bands; slower)",
      false );
  Json::Value ciLevel =
    makeNumberParam( "ci_level", "Confidence level for the bootstrap intervals", 0.95 );
  setRange( ciLevel, 0.5, 0.999 );
  props["ci_level"] = ciLevel;
  Json::Value bootstrapResamples = makeIntegerParam(
      "bootstrap_resamples", "Resamples per break for the bootstrap CI", 99 );
  setRange( bootstrapResamples, 9, 999 );
  props["bootstrap_resamples"] = bootstrapResamples;
  props["bootstrap_seed"] =
    makeIntegerParam( "bootstrap_seed", "Seed for the deterministic bootstrap", 20260915 );
  props["tile_size"] =
    makeIntegerParam( "tile_size", "Streaming tile size (pixels)", kDefaultTileSize );
  props["output"] = makeOutputParam(
      "output",
      "Attribution GeoTIFF: breaks_count, break_kind_k "
      "(0=none,1=trend,2=seasonal,3=both,4=untestable), break_day_k, "
      "break_mag_k, break_seasonal_shift_k, break_pvalue_k, "
      "[mag_ci_lo_1..k then mag_ci_hi_1..k when compute_ci], rmse, r2, "
      "valid_count",
      "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] =
    makeOutputParam( "output", "Attribution GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Scenes in the series", 0 );
  outputs["pixelsWithBreaks"] =
    makeIntegerParam( "pixelsWithBreaks", "Pixels with >= 1 break", 0 );
  outputs["pixelsWithSeasonalBreaks"] = makeIntegerParam(
      "pixelsWithSeasonalBreaks",
      "Pixels with at least one seasonal-only or both-kind break", 0 );
  outputs["epochDate"] =
    makeStringParam( "epochDate", "Break days are offsets from this first acquisition (ISO)", "" );
  Json::Value memory = makeStringParam( "memory", "Streaming working-set summary" );
  memory["type"] = "object";
  outputs["memory"] = memory;
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalSeasonalBreaksOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "change-detection" );
  tags.append( "seasonal-component" );
  tags.append( "attribution" );
  tags.append( "uncertainty" );
  meta["tags"] = tags;
  meta["purpose"] = "Separate land-surface changes that move the SEASONAL "
                    "behavior (irrigation onset, crop-type switch, phenology "
                    "shift) from pure level/slope disturbances "
                    "(clear-cut, flood, burn)";
  meta["prerequisites"] = "Common grid, acquisition times, consistent "
                          "radiometric state; >= ~2 years for stable "
                          "harmonics; >= 4 valid samples";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] =
    "O(tile × scenes × iterations × terms²) segmentation + "
    "O(tile × breaks × scenes × terms²) attribution "
    "(+ bootstrap_resamples × that when compute_ci)";
  meta["workflowHints"] =
    "Chain with rs:temporal_harmonic_breaks when only level disturbances "
    "matter; break_kind lets rs:threshold_raster separate seasonal from "
    "trend change maps";
  meta["limitations"] =
    "Greedy segmentation is not a global optimum; attribution F tests are "
    "asymptotic (Gaussian residual assumption); kind encoding is nominal "
    "(0..4); with compute_ci the intervals are percentile bootstrap - they "
    "are refused (NaN) when < 60% of refits succeed";
  return meta;
}

Json::Value RsTemporalSeasonalBreaksOperator::executionEstimate() const
{
  return sicnu::processing::makeStreamingEstimate(
      kDefaultTileSize, kDefaultTileSize, 1, 4, 2 + kTypicalSceneEstimate, 0,
      4 * 1024 * 1024 );
}

Json::Value RsTemporalSeasonalBreaksOperator::estimateExecution(
    const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  int scenes = kTypicalSceneEstimate;
  if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
    scenes = std::max<int>( 1, static_cast<int>( params["scenes"].size() ) );
  return sicnu::processing::makeStreamingEstimate(
      tileSize, tileSize, 1, 4, 2 + static_cast<std::uint64_t>( scenes ), 0,
      4 * 1024 * 1024 );
}

Json::Value RsTemporalSeasonalBreaksOperator::run( const Json::Value &params,
                                                   RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );

  const int harmonics = std::clamp( getInt( params, "harmonics", 2 ), 1, 3 );
  const int maxBreaks =
    std::clamp( getInt( params, "maxBreaks", kDefaultMaxBreaks ), 0, kMaxBreaksLimit );
  double minSegmentDays = getDouble( params, "minSegmentDays", 90.0 );
  if ( !( minSegmentDays > 0.0 ) )
    minSegmentDays = 90.0;
  const double minImprovement = getDouble( params, "minImprovement", 0.1 );
  if ( !( minImprovement > 0.0 && minImprovement <= 1.0 ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "minImprovement must be in (0, 1]" );
  const bool robust = getBool( params, "robust", false );
  const double alpha = getDouble( params, "alpha", 0.05 );
  if ( !( alpha > 0.001 && alpha <= 0.5 ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, "alpha must be in (0.001, 0.5]" );
  const bool computeCi = getBool( params, "compute_ci", false );
  const double ciLevel = getDouble( params, "ci_level", 0.95 );
  const int bootstrapResamples = std::clamp( getInt( params, "bootstrap_resamples", 99 ), 9, 999 );
  const int bootstrapSeed = getInt( params, "bootstrap_seed", 20260915 );

  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 4 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "seasonal break attribution needs at least 4 scenes (got " +
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

  std::vector<double> tDays( sceneCount );
  for ( int s = 0; s < sceneCount; ++s )
    tDays[s] = reader.sceneDayOffset( s );

  const double totalSpan = tDays.back() - tDays.front();
  int minSegment = 3;
  if ( totalSpan > 0.0 && sceneCount > 1 )
  {
    const double spacing = totalSpan / static_cast<double>( sceneCount - 1 );
    minSegment = std::clamp(
        static_cast<int>( std::lround( minSegmentDays / spacing ) ), 3,
        std::max( 3, sceneCount / 2 ) );
  }

  const QString epochDate = prepared.collection.scenes().at( 0 ).time.dateString();

  temporal::BreakAttributionOptions attributionOptions;
  attributionOptions.harmonics = harmonics;
  attributionOptions.alpha = alpha;

  const int bandCount = 1 +                              // breaks_count
                        maxBreaks +                      // break_kind_k
                        maxBreaks +                      // break_day_k
                        maxBreaks +                      // break_mag_k
                        maxBreaks +                      // break_seasonal_shift_k
                        maxBreaks +                      // break_pvalue_k
                        ( computeCi ? 2 * maxBreaks : 0 ) +  // mag_ci_lo/hi_k
                        2 +                              // rmse, r2
                        1;                               // valid_count
  const int width = reader.width();
  const int height = reader.height();

  context.reportProgress( 0.05, "Creating seasonal-breaks output" );
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
  describeBand( "breaks_count" );
  for ( int j = 1; j <= maxBreaks; ++j )
    describeBand( QStringLiteral( "break_kind_%1" ).arg( j ) );
  for ( int j = 1; j <= maxBreaks; ++j )
    describeBand( QStringLiteral( "break_day_%1" ).arg( j ) );
  for ( int j = 1; j <= maxBreaks; ++j )
    describeBand( QStringLiteral( "break_mag_%1" ).arg( j ) );
  for ( int j = 1; j <= maxBreaks; ++j )
    describeBand( QStringLiteral( "break_seasonal_shift_%1" ).arg( j ) );
  for ( int j = 1; j <= maxBreaks; ++j )
    describeBand( QStringLiteral( "break_pvalue_%1" ).arg( j ) );
  if ( computeCi )
  {
    for ( int j = 1; j <= maxBreaks; ++j )
      describeBand( QStringLiteral( "mag_ci_lo_%1" ).arg( j ) );
    for ( int j = 1; j <= maxBreaks; ++j )
      describeBand( QStringLiteral( "mag_ci_hi_%1" ).arg( j ) );
  }
  describeBand( "rmse" );
  describeBand( "r2" );
  describeBand( "valid_count" );

  const size_t maxTilePixels =
      static_cast<size_t>( std::min( tileSize, width ) ) *
      static_cast<size_t>( std::min( tileSize, height ) );
  constexpr size_t kMaxSeriesBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
  const size_t tileFloatsPerPixel = 3 * static_cast<size_t>( sceneCount ) +
                                    5 * static_cast<size_t>( maxBreaks ) +
                                    ( computeCi ? 2 * static_cast<size_t>( maxBreaks ) : 0 ) + 5;
  if ( tileFloatsPerPixel * maxTilePixels * sizeof( float ) > kMaxSeriesBytes )
    throw RSOperatorError(
        ErrorCode::InvalidParameter,
        "tile_size " + std::to_string( tileSize ) + " x " + std::to_string( sceneCount ) +
            " scenes exceeds the 2 GiB tile working-set budget; reduce tile_size" );
  const size_t tilePixels = maxTilePixels;

  std::vector<float> tile( tilePixels );
  std::vector<float> series( static_cast<size_t>( sceneCount ) * tilePixels );
  std::vector<float> countBuf( tilePixels );
  std::vector<float> kindBufs( static_cast<size_t>( maxBreaks ) * tilePixels );
  std::vector<float> dayBufs( static_cast<size_t>( maxBreaks ) * tilePixels );
  std::vector<float> magBufs( static_cast<size_t>( maxBreaks ) * tilePixels );
  std::vector<float> shiftBufs( static_cast<size_t>( maxBreaks ) * tilePixels );
  std::vector<float> pvalueBufs( static_cast<size_t>( maxBreaks ) * tilePixels );
  std::vector<float> ciLoBufs( computeCi ? static_cast<size_t>( maxBreaks ) * tilePixels : 0 );
  std::vector<float> ciHiBufs( computeCi ? static_cast<size_t>( maxBreaks ) * tilePixels : 0 );
  std::vector<float> rmseBuf( tilePixels );
  std::vector<float> r2Buf( tilePixels );
  std::vector<float> validBuf( tilePixels );
  std::vector<float> pixSeries( static_cast<size_t>( sceneCount ) );

  std::uint64_t pixelsWithBreaks = 0;
  std::uint64_t pixelsWithSeasonalBreaks = 0;
  int tileDone = 0;
  const int tiles = reader.totalTileCount();

  // Per-pixel bootstrap statistic: first-break level jump of the refit.
  temporal::BootstrapOptions bootstrapOptions;
  bootstrapOptions.resamples = bootstrapResamples;
  bootstrapOptions.ciLevel = ciLevel;
  bootstrapOptions.seed = static_cast<std::uint32_t>( bootstrapSeed );
  // Per-break statistic: the level jump of the refit's break that is
  // CHRONOLOGICALLY k-th (same ordinal as the point estimate being
  // intervalled). NaN when the refit finds fewer breaks.
  const auto magnitudeStatisticFor = [&, harmonics, maxBreaks]( int k )
      -> std::function<double( const std::vector<float> & )> {
    return [&, harmonics, maxBreaks, k]( const std::vector<float> &resampled ) {
      const temporal::SeasonalTrendBreaksResult r = temporal::fitSeasonalTrendBreaks(
          resampled, tDays, harmonics, maxBreaks, minSegment, minImprovement,
          robust );
      if ( k >= static_cast<int>( r.breaks.size() ) )
        return std::numeric_limits<double>::quiet_NaN();
      return r.breaks[static_cast<size_t>( k )].magnitude;
    };
  };

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
      temporal::BreakAttributionResult attribution;
      if ( !fit.breaks.empty() )
        attribution = temporal::attributeSeasonalTrendBreaks(
            fit, pixSeries, tDays, attributionOptions );

      countBuf[i] = static_cast<float>( fit.breaks.size() );
      if ( !fit.breaks.empty() )
      {
        ++pixelsWithBreaks;
        bool seasonal = false;
        for ( const auto &br : attribution.breaks )
          if ( br.kind == temporal::BreakKind::SeasonalOnly ||
               br.kind == temporal::BreakKind::Both )
            seasonal = true;
        if ( seasonal )
          ++pixelsWithSeasonalBreaks;
      }
      // Per-break bootstrap CIs, computed ONCE per pixel before the band
      // loop (break j gets the ordinal-j statistic and its own pixel-scoped
      // deterministic seed; no redundant refits for absent breaks).
      std::vector<float> ciLoPerBreak, ciHiPerBreak;
      if ( computeCi && !fit.breaks.empty() )
      {
        ciLoPerBreak.assign(
            fit.breaks.size(), std::numeric_limits<float>::quiet_NaN() );
        ciHiPerBreak.assign(
            fit.breaks.size(), std::numeric_limits<float>::quiet_NaN() );
        for ( int j = 0; j < static_cast<int>( fit.breaks.size() ); ++j )
        {
          // Deterministic (pixel, break)-scoped stream: seed + linear pixel
          // index x (tile-local) + break ordinal — identical across reruns,
          // decorrelated within a tile.
          temporal::BootstrapOptions pixelOptions = bootstrapOptions;
          pixelOptions.seed =
            bootstrapOptions.seed +
            static_cast<std::uint32_t>( i * static_cast<size_t>( maxBreaks + 1 ) +
                                        static_cast<size_t>( j ) + 1 );
          const temporal::BootstrapCi ci = temporal::residualBootstrapCi(
              pixSeries, fit.fitted, magnitudeStatisticFor( j ), pixelOptions );
          if ( ci.valid )
          {
            ciLoPerBreak[static_cast<size_t>( j )] =
              static_cast<float>( ci.lower );
            ciHiPerBreak[static_cast<size_t>( j )] =
              static_cast<float>( ci.upper );
          }
        }
      }

      for ( int j = 0; j < maxBreaks; ++j )
      {
        const bool has = j < static_cast<int>( fit.breaks.size() );
        const float nan = std::numeric_limits<float>::quiet_NaN();
        kindBufs[static_cast<size_t>( j ) * tilePixels + i] =
          has ? kindToBand( attribution.breaks[static_cast<size_t>( j )].kind ) : nan;
        dayBufs[static_cast<size_t>( j ) * tilePixels + i] =
          has ? static_cast<float>( fit.breaks[static_cast<size_t>( j )].breakDays ) : nan;
        magBufs[static_cast<size_t>( j ) * tilePixels + i] =
          has ? static_cast<float>( attribution.breaks[static_cast<size_t>( j )].trendMagnitude )
              : nan;
        shiftBufs[static_cast<size_t>( j ) * tilePixels + i] =
          has ? static_cast<float>( attribution.breaks[static_cast<size_t>( j )].seasonalShift )
              : nan;
        pvalueBufs[static_cast<size_t>( j ) * tilePixels + i] =
          has ? static_cast<float>( attribution.breaks[static_cast<size_t>( j )].pValue ) : nan;
        if ( computeCi )
        {
          const bool hasCi =
            has && j < static_cast<int>( ciLoPerBreak.size() );
          ciLoBufs[static_cast<size_t>( j ) * tilePixels + i] =
            hasCi ? ciLoPerBreak[static_cast<size_t>( j )] : nan;
          ciHiBufs[static_cast<size_t>( j ) * tilePixels + i] =
            hasCi ? ciHiPerBreak[static_cast<size_t>( j )] : nan;
        }
      }
      rmseBuf[i] = std::isfinite( fit.rmse )
                     ? static_cast<float>( fit.rmse )
                     : std::numeric_limits<float>::quiet_NaN();
      r2Buf[i] = std::isfinite( fit.r2 ) ? static_cast<float>( fit.r2 )
                                         : std::numeric_limits<float>::quiet_NaN();
      validBuf[i] = static_cast<float>( fit.validCount );
      context.throwIfCancelled();
    }

    int ob = 1;
    auto writeBandWindow = [&]( const std::vector<float> &buf, int &band ) {
      if ( !out.writeBandWindow( band++, x, y, w, h, buf.data() ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing output band" );
    };
    writeBandWindow( countBuf, ob );
    auto writeSlice = [&]( const std::vector<float> &bufs, int j, int &band ) {
      if ( !out.writeBandWindow(
               band++, x, y, w, h,
               bufs.data() + static_cast<std::ptrdiff_t>( j ) *
                                 static_cast<std::ptrdiff_t>( tilePixels ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing output band" );
    };
    for ( int j = 0; j < maxBreaks; ++j )
      writeSlice( kindBufs, j, ob );
    for ( int j = 0; j < maxBreaks; ++j )
      writeSlice( dayBufs, j, ob );
    for ( int j = 0; j < maxBreaks; ++j )
      writeSlice( magBufs, j, ob );
    for ( int j = 0; j < maxBreaks; ++j )
      writeSlice( shiftBufs, j, ob );
    for ( int j = 0; j < maxBreaks; ++j )
      writeSlice( pvalueBufs, j, ob );
    if ( computeCi )
    {
      for ( int j = 0; j < maxBreaks; ++j )
        writeSlice( ciLoBufs, j, ob );
      for ( int j = 0; j < maxBreaks; ++j )
        writeSlice( ciHiBufs, j, ob );
    }
    writeBandWindow( rmseBuf, ob );
    writeBandWindow( r2Buf, ob );
    writeBandWindow( validBuf, ob );

    ++tileDone;
    context.reportProgress(
        0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
        "Seasonal-break tiles " + std::to_string( tileDone ) + "/" +
            std::to_string( tiles ) );
  }

  temporal_output::writeTemporalDatasetMetadata(
      out, prepared.collection, "rs:temporal_seasonal_breaks",
      QStringLiteral( "harmonics=%1 maxBreaks=%2 alpha=%3 robust=%4 compute_ci=%5" )
          .arg( harmonics )
          .arg( maxBreaks )
          .arg( alpha )
          .arg( robust ? 1 : 0 )
          .arg( computeCi ? 1 : 0 ) );
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
  result["pixelsWithBreaks"] = Json::Value::UInt64( pixelsWithBreaks );
  result["pixelsWithSeasonalBreaks"] =
    Json::Value::UInt64( pixelsWithSeasonalBreaks );
  result["epochDate"] = epochDate.toStdString();
  result["method"] =
    "joint harmonic+trend segmentation with nested-F seasonal-change "
    "attribution (BFAST-inspired; not full BFAST/CCDC)"
    + std::string( computeCi ? "; seeded residual-bootstrap level-jump CI" : "" );
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
      TemporalTileReader::estimateWorkingSetBytes(
          tileSize, tileSize,
          6 + 5 * static_cast<std::uint64_t>( sceneCount ) +
              ( computeCi ? 2 : 0 ) * static_cast<std::uint64_t>( maxBreaks ),
          0 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal seasonal breaks complete" );
  return result;
}

} // namespace sicnu::operators::rs
