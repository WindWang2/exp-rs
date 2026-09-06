// src/operators/rs/rs_temporal_sen_trend_operator.cpp
#include "rs_temporal_sen_trend_operator.h"

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
constexpr int kSenOutputBands = 5; // slope, intercept, z, p_value, n
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
const double kDefaultAlpha = 0.05;
} // namespace

std::string RsTemporalSenTrendOperator::description() const
{
  return "Per-pixel non-parametric monotonic trend across a multi-date "
         "collection: Sen's median slope (median of all pairwise day slopes — "
         "robust to outliers) with the tie-corrected Mann-Kendall test "
         "(continuity-corrected z, two-sided p-value). Uses the real "
         "acquisition day offsets. Outputs slope, intercept, z, p_value and "
         "the valid observation count; undefined results are NaN, never zero.";
}

Json::Value RsTemporalSenTrendOperator::schema() const
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
  Json::Value alphaParam = makeNumberParam(
      "alpha", "Significance level reported in significantPixelFraction "
               "(the p-value band is always the full two-sided p)", kDefaultAlpha );
  setRange( alphaParam, 0.001, 0.5 );
  props["alpha"] = alphaParam;
  props["output"] = makeOutputParam( "output",
                                     "Sen trend GeoTIFF (bands: slope, intercept, z, p_value, n)",
                                     "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Sen trend GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Dates in the series", 0 );
  outputs["bands"] = makeIntegerParam( "bands", "Output band count (5)", 0 );
  outputs["significantPixelFraction"] = makeNumberParam(
      "significantPixelFraction", "Pixels with a significant monotonic trend "
      "(p < alpha and >= 3 valid observations) / total pixels", 0.0 );
  outputs["timeStart"] = makeStringParam( "timeStart", "First acquisition date in the series (ISO)", "" );
  outputs["timeEnd"] = makeStringParam( "timeEnd", "Last acquisition date in the series (ISO)", "" );
  Json::Value memory = makeStringParam( "memory", "Streaming working-set summary (tile size and estimated bytes)" );
  memory["type"] = "object";
  outputs["memory"] = memory;
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalSenTrendOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "trend" );
  meta["tags"] = tags;
  meta["purpose"] = "Non-parametric monotonic trend (Sen slope + Mann-Kendall "
                    "significance) — robust per-pixel rates for noisy or "
                    "outlier-laden series where OLS assumptions do not hold";
  meta["prerequisites"] = "Common grid, acquisition times, consistent radiometric "
                          "state (temporal preflight); >= 3 valid observations per pixel";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes²) pairwise slopes per pixel (scenes <= a "
                      "few hundred; the 2 GiB series-gather guard rejects "
                      "unrealistic tile_size × sceneCount combinations)";
  meta["workflowHints"] = "Run on an rs:temporal_index_series band for index "
                          "trends; slopes are per DAY (multiply by 365.25 for "
                          "per-year rates); prefer over rs:temporal_trend when "
                          "the series may contain spikes/residual outliers";
  meta["limitations"] = "Detects monotonic trend only (no seasonality handling — "
                        "detrend via rs:temporal_decompose first when a strong "
                        "seasonal cycle exists); the normal-approximation "
                        "p-value is approximate below ~10 valid observations; "
                        "ties are handled with the Gilbert (1987) correction; "
                        "var(S) presumes distinct acquisition instants — "
                        "same-day duplicates under duplicate_policy=keep_all "
                        "make the test conservative (use reject for exact "
                        "inference)";
  return meta;
}

Json::Value RsTemporalSenTrendOperator::executionEstimate() const
{
  // Typical 8-date collection: series per scene + 5 metric bands + gather/read.
  const int buffers = kTypicalSceneCount + kSenOutputBands + 2;
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4,
                                                   buffers, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalSenTrendOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  int scenes = params["scenes"].isArray() ? params["scenes"].size() : kTypicalSceneCount;
  scenes = std::max( scenes, 1 );
  const int buffers = scenes + kSenOutputBands + 2;
  return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4, buffers, 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalSenTrendOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );
  const double alpha = std::clamp( getDouble( params, "alpha", kDefaultAlpha ), 0.001, 0.5 );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 3 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "the Mann-Kendall test needs at least 3 scenes (got " +
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
    // documented default: explicit band > role > positional fallback > band 1.
    // An EXPLICIT band_role that resolves nowhere is a caller error.
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

  const int width = reader.width();
  const int height = reader.height();

  context.reportProgress( 0.05, "Creating Sen trend output" );
  GdalDatasetWrapper out;
  QString outErr;
  temporal_output::TemporalOutputGuard guard;
  guard.manage( &out, QString::fromStdString( outputPath ) );
  if ( !out.create( QString::fromStdString( outputPath ), width, height, kSenOutputBands,
                    static_cast<int>( GDT_Float32 ), reader.geoTransform(), reader.projection(),
                    &outErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to create output: " + outErr.toStdString() );
  const char *const bandNames[kSenOutputBands] = { "slope", "intercept", "z", "p_value", "n" };
  for ( int b = 1; b <= kSenOutputBands; ++b )
  {
    out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b ),
                        bandNames[b - 1] );
  }

  // OOM guard: the per-pixel series gather must stay bounded (same contract
  // and limit as the other series-gathering temporal operators).
  const int tiles = reader.totalTileCount();
  const size_t maxTilePixels =
      static_cast<size_t>( std::min( tileSize, width ) ) *
      static_cast<size_t>( std::min( tileSize, height ) );
  constexpr size_t kMaxSeriesBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
  if ( static_cast<size_t>( sceneCount ) * maxTilePixels * sizeof( float ) > kMaxSeriesBytes )
    throw RSOperatorError(
        ErrorCode::InvalidParameter,
        "tile_size " + std::to_string( tileSize ) + " x " + std::to_string( sceneCount ) +
            " scenes exceeds the 2 GiB series-gathering budget; reduce tile_size "
        "(or scene count)" );
  const size_t tilePixels = maxTilePixels;

  std::vector<float> tile( tilePixels );
  std::vector<float> series( static_cast<size_t>( sceneCount ) * tilePixels );
  std::vector<float> bandBufs( static_cast<size_t>( kSenOutputBands ) * tilePixels );
  std::vector<float> pixSeries( sceneCount );
  std::uint64_t significantPixels = 0;
  int tileDone = 0;

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
      std::copy( tile.data(), tile.data() + pixels, series.data() + s * tilePixels );
      context.throwIfCancelled();
    }

    for ( size_t i = 0; i < pixels; ++i )
    {
      for ( int s = 0; s < sceneCount; ++s )
        pixSeries[s] = series[s * tilePixels + i];
      const sicnu::temporal::SenTrendResult tr = sicnu::temporal::mannKendallSenSlope( pixSeries, tDays );
      bandBufs[0 * tilePixels + i] = static_cast<float>( tr.slope );
      bandBufs[1 * tilePixels + i] = static_cast<float>( tr.intercept );
      bandBufs[2 * tilePixels + i] = static_cast<float>( tr.z );
      bandBufs[3 * tilePixels + i] = static_cast<float>( tr.pValue );
      bandBufs[4 * tilePixels + i] = static_cast<float>( tr.validCount );
      if ( tr.validCount >= 3 && tr.pValue < alpha )
        ++significantPixels;
    }
    context.throwIfCancelled();

    auto writeBand = [&]( int band, const float *src ) {
      if ( !out.writeBandWindow( band, x, y, w, h, src ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing Sen trend band" );
    };
    for ( int b = 1; b <= kSenOutputBands; ++b )
      writeBand( b, bandBufs.data() + static_cast<size_t>( b - 1 ) * tilePixels );

    ++tileDone;
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                            "Sen trend tiles " + std::to_string( tileDone ) + "/" +
                                std::to_string( tiles ) );
  }

  temporal_output::writeTemporalDatasetMetadata(
    out, prepared.collection, "rs:temporal_sen_trend",
    QStringLiteral( "band_role=%1 alpha=%2" )
        .arg( bandRole.isEmpty() ? QStringLiteral( "band:1" ) : bandRole )
        .arg( alpha ) );
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
  result["bands"] = kSenOutputBands;
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  result["significantPixelFraction"] =
      totalPixels > 0 ? static_cast<double>( significantPixels ) / totalPixels : 0.0;
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
    TemporalTileReader::estimateWorkingSetBytes( tileSize, tileSize,
                                                 sceneCount + kSenOutputBands + 2, 0 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal Sen trend complete" );
  return result;
}

} // namespace sicnu::operators::rs
