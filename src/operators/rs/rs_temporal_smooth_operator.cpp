// src/operators/rs/rs_temporal_smooth_operator.cpp
#include "rs_temporal_smooth_operator.h"

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
constexpr int kDefaultWindow = 7;
constexpr int kDefaultDegree = 2;
constexpr double kDefaultLambda = 10.0;
constexpr int kDefaultMovingAverageWindow = 3;
constexpr int kTypicalSceneEstimate = 8;
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

enum class Method
{
  SavitzkyGolay,
  Whittaker,
  MovingAverage
};

/// Centered boxcar average over the valid (finite) samples inside a window of
/// half-width window/2; NaN when the window holds no valid sample. Double
/// accumulation keeps the result independent of summation splits.
void movingAverage( const std::vector<float> &y, int window, std::vector<float> *out )
{
  const int half = window / 2;
  const int n = static_cast<int>( y.size() );
  for ( int i = 0; i < n; ++i )
  {
    double sum = 0.0;
    int count = 0;
    for ( int k = std::max( 0, i - half ); k <= std::min( n - 1, i + half ); ++k )
    {
      const float v = y[static_cast<size_t>( k )];
      if ( std::isfinite( v ) )
      {
        sum += v;
        ++count;
      }
    }
    ( *out )[static_cast<size_t>( i )] = count > 0 ? static_cast<float>( sum / count ) : kNan;
  }
}

/// "YYYY-MM-DD" tag for band names; falls back to the chronological index
/// when a scene carries no parsable date (preflight normally rejects those).
QString sceneTag( const temporal::TemporalSceneRef &scene, int index )
{
  const QString d = scene.time.dateString();
  return d.isEmpty() ? QStringLiteral( "scene%1" ).arg( index + 1 ) : d;
}
} // namespace

std::string RsTemporalSmoothOperator::description() const
{
  return "Quality-aware smoothing (Savitzky–Golay, Whittaker or moving "
         "average) of a per-pixel time series across a multi-date collection. "
         "NaN samples are treated as absent; one output band per scene date, "
         "same order, so the result stays stackable with acquisition metadata.";
}

Json::Value RsTemporalSmoothOperator::schema() const
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
                                      { "blue", "green", "red", "red_edge", "nir", "swir1",
                                        "swir2", "vv", "vh" },
                                      "" );
  props["duplicate_policy"] = makeEnumParam( "duplicate_policy", "Duplicate acquisition instants",
                                             { "keep_all", "reject" }, "keep_all" );
  props["apply_qa_masking"] = makeBooleanParam( "apply_qa_masking", "Exclude QA/cloud-masked samples", true );
  props["method"] = makeEnumParam( "method",
                                   "savitzky_golay: local polynomial fit (needs degree+1 valid "
                                   "samples per window); whittaker: penalty smoother "
                                   "Σ(y−z)²+λΣ(Δ²z)²; moving_average: centered boxcar of "
                                   "valid samples",
                                   { "savitzky_golay", "whittaker", "moving_average" },
                                   "savitzky_golay" );
  Json::Value window = makeIntegerParam(
      "window", "Savitzky–Golay window length (odd number of samples)", kDefaultWindow );
  setRange( window, 3, 31 );
  props["window"] = window;
  Json::Value degree = makeIntegerParam( "degree", "Savitzky–Golay polynomial degree", kDefaultDegree );
  setRange( degree, 1, 4 );
  props["degree"] = degree;
  Json::Value lambda = makeNumberParam(
      "lambda", "Whittaker smoothing penalty λ (larger = smoother; λ → huge "
                "approaches a straight line)",
      kDefaultLambda );
  setRange( lambda, 1e-9, 1e12 );
  props["lambda"] = lambda;
  Json::Value maWindow = makeIntegerParam(
      "moving_average_window", "Moving-average window length (samples)", kDefaultMovingAverageWindow );
  setRange( maWindow, 3, 31 );
  props["moving_average_window"] = maWindow;
  props["tile_size"] = makeIntegerParam( "tile_size", "Streaming tile size (pixels)", kDefaultTileSize );
  props["output"] = makeOutputParam( "output",
                                     "Smoothed GeoTIFF (one band per scene date: smoothed_<date>)",
                                     "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Smoothed GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Scenes in the series", 0 );
  outputs["method"] = makeStringParam( "method", "Smoothing method applied", "" );
  outputs["bands"] = makeIntegerParam( "bands", "Output band count (= sceneCount)", 0 );
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalSmoothOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "smoothing" );
  tags.append( "savitzky-golay" );
  meta["tags"] = tags;
  meta["purpose"] = "Suppress residual noise in per-pixel time series (cloud/brdf jitter) before "
                    "trend, phenology or anomaly analysis";
  meta["prerequisites"] = "Common grid, acquisition times, consistent radiometric state "
                          "(temporal preflight); at least 3 scenes";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes × window²)";
  meta["workflowHints"] = "Run after rs:temporal_gap_fill on cloudy series; feed the smoothed "
                          "bands into rs:temporal_trend or phenology tools; start with "
                          "method=savitzky_golay window=7 degree=2 and compare against "
                          "method=moving_average";
  meta["limitations"] = "Savitzky–Golay boundary fits shrink the window (shrink-at-boundary) and "
                        "need degree+1 valid samples inside a window; Whittaker solves a banded "
                        "system to a documented 1e-5 relative tolerance — hence grade "
                        "'tolerance'; NaN gaps stay NaN unless the fit can bridge them; the "
                        "working set is O(tile × scenes), not O(tile)";
  return meta;
}

Json::Value RsTemporalSmoothOperator::executionEstimate() const
{
  // 1 read tile + one column-major series tile per scene (typical input).
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4,
                                                   1 + kTypicalSceneEstimate, 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalSmoothOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  int scenes = kTypicalSceneEstimate;
  if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
    scenes = std::max<int>( 1, static_cast<int>( params["scenes"].size() ) );
  // 1 read tile + one series tile per scene date.
  return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4,
                                                   1 + static_cast<std::uint64_t>( scenes ), 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalSmoothOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const QString methodToken = QString::fromStdString(
      getEnum( params, "method", { "savitzky_golay", "whittaker", "moving_average" },
               "savitzky_golay" ) );
  const Method method = methodToken == QLatin1String( "whittaker" )  ? Method::Whittaker
                        : methodToken == QLatin1String( "moving_average" )
                            ? Method::MovingAverage
                            : Method::SavitzkyGolay;
  const int window = getInt( params, "window", kDefaultWindow );
  if ( window < 3 || window > 31 || window % 2 == 0 )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "window must be an odd integer in [3, 31] (got " +
                               std::to_string( window ) + ")" );
  const int degree = getInt( params, "degree", kDefaultDegree );
  if ( degree < 1 || degree > 4 )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "degree must be in [1, 4] (got " + std::to_string( degree ) + ")" );
  const double lambda = getDouble( params, "lambda", kDefaultLambda );
  if ( method == Method::Whittaker && !( lambda > 0.0 ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, "lambda must be > 0" );
  const int maWindow = getInt( params, "moving_average_window", kDefaultMovingAverageWindow );
  if ( maWindow < 3 || maWindow > 31 )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "moving_average_window must be in [3, 31] (got " +
                               std::to_string( maWindow ) + ")" );

  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 3 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "temporal smoothing needs at least 3 scenes (got " +
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

  const int width = reader.width();
  const int height = reader.height();

  context.reportProgress( 0.05, "Creating smoothing output" );
  std::vector<QString> bandNames( sceneCount );
  for ( int s = 0; s < sceneCount; ++s )
    bandNames[s] = QStringLiteral( "smoothed_%1" ).arg(
        sceneTag( prepared.collection.scenes().at( s ), s ) );

  GdalDatasetWrapper out;
  QString outErr;
  temporal_output::TemporalOutputGuard guard;
  guard.manage( &out, QString::fromStdString( outputPath ) );
  if ( !out.create( QString::fromStdString( outputPath ), width, height, sceneCount,
                    static_cast<int>( GDT_Float32 ), reader.geoTransform(), reader.projection(),
                    &outErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to create output: " + outErr.toStdString() );
  for ( int s = 0; s < sceneCount; ++s )
  {
    out.setBandNoDataValue( s + 1, std::numeric_limits<double>::quiet_NaN() );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), s + 1 ),
                        bandNames[s].toUtf8().constData() );
  }

  const int tiles = reader.totalTileCount();
  const size_t tilePixels = static_cast<size_t>( tileSize ) * tileSize;

  // Column-major per-pixel series: series[s * tilePixels + i] — the smoothed
  // values are written back in place, so no second T-tile buffer is needed.
  std::vector<float> series( tilePixels * static_cast<size_t>( sceneCount ) );
  std::vector<float> tile( tilePixels );
  std::vector<float> pixelSeries( static_cast<size_t>( sceneCount ) );
  std::vector<float> smoothed( static_cast<size_t>( sceneCount ), kNan );

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
      std::copy( tile.begin(), tile.begin() + static_cast<std::ptrdiff_t>( pixels ),
                 series.begin() + static_cast<std::ptrdiff_t>( s * tilePixels ) );
      context.throwIfCancelled();
    }

    for ( size_t i = 0; i < pixels; ++i )
    {
      for ( int s = 0; s < sceneCount; ++s )
        pixelSeries[static_cast<size_t>( s )] = series[s * tilePixels + i];
      if ( method == Method::SavitzkyGolay )
        smoothed = temporal::savitzkyGolay( pixelSeries, window, degree );
      else if ( method == Method::Whittaker )
        smoothed = temporal::whittakerSmooth( pixelSeries, {}, lambda );
      else
        movingAverage( pixelSeries, maWindow, &smoothed );
      for ( int s = 0; s < sceneCount; ++s )
        series[s * tilePixels + i] = smoothed[static_cast<size_t>( s )];
    }

    for ( int s = 0; s < sceneCount; ++s )
    {
      if ( !out.writeBandWindow( s + 1, x, y, w, h, series.data() + s * tilePixels ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing smoothed band" );
    }

    ++tileDone;
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                            "Smoothing tiles " + std::to_string( tileDone ) + "/" +
                                std::to_string( tiles ) );
  }

  for ( int s = 0; s < sceneCount; ++s )
  {
    temporal_output::writeBandAcquisitionMetadata( out, s + 1,
                                                   prepared.collection.scenes().at( s ),
                                                   QStringLiteral( "smoothed" ) );
    // writeBandAcquisitionMetadata appends the date to the description; pin
    // the exact "smoothed_<date>" band name on top.
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), s + 1 ),
                        bandNames[s].toUtf8().constData() );
  }
  temporal_output::writeTemporalDatasetMetadata(
    out, prepared.collection, "rs:temporal_smooth",
    QStringLiteral( "method=%1 window=%2 degree=%3 lambda=%4 ma_window=%5" )
        .arg( methodToken )
        .arg( window )
        .arg( degree )
        .arg( lambda )
        .arg( maWindow ) );
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
  result["method"] = methodToken.toStdString();
  result["bands"] = sceneCount;
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  // 1 read tile + one column-major series tile per scene date.
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
    TemporalTileReader::estimateWorkingSetBytes( tileSize, tileSize,
                                                 1 + static_cast<std::uint64_t>( sceneCount ), 0 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal smoothing complete" );
  return result;
}

} // namespace sicnu::operators::rs
