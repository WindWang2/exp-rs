// src/operators/rs/rs_temporal_decompose_operator.cpp
#include "rs_temporal_decompose_operator.h"

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
constexpr double kDefaultTrendLambda = 1e4;
constexpr int kDefaultSeasonalWindowDays = 15;
constexpr int kTypicalSceneEstimate = 8;
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

enum class Component
{
  Trend,
  Seasonal,
  Remainder
};

/// Parses + validates the "components" array: unknown tokens are rejected,
/// duplicates collapse to their first occurrence, order is preserved. An
/// empty/missing array means all three components.
std::vector<Component> parseComponents( const Json::Value &params )
{
  const std::vector<std::string> tokens = getStringArray( params, "components" );
  std::vector<Component> out;
  for ( const auto &raw : tokens )
  {
    const QString token = QString::fromStdString( raw );
    Component c = Component::Trend;
    if ( token.compare( QLatin1String( "trend" ), Qt::CaseInsensitive ) == 0 )
      c = Component::Trend;
    else if ( token.compare( QLatin1String( "seasonal" ), Qt::CaseInsensitive ) == 0 )
      c = Component::Seasonal;
    else if ( token.compare( QLatin1String( "remainder" ), Qt::CaseInsensitive ) == 0 )
      c = Component::Remainder;
    else
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "unknown decomposition component: " + raw );
    if ( std::find( out.begin(), out.end(), c ) == out.end() )
      out.push_back( c );
  }
  if ( out.empty() )
    out = { Component::Trend, Component::Seasonal, Component::Remainder };
  return out;
}

QString componentToken( Component c )
{
  switch ( c )
  {
    case Component::Trend:
      return QStringLiteral( "trend" );
    case Component::Seasonal:
      return QStringLiteral( "seasonal" );
    case Component::Remainder:
      return QStringLiteral( "remainder" );
  }
  return QStringLiteral( "trend" );
}

/// "YYYY-MM-DD" tag for band names; falls back to the chronological index
/// when a scene carries no parsable date (preflight normally rejects those).
QString sceneTag( const temporal::TemporalSceneRef &scene, int index )
{
  const QString d = scene.time.dateString();
  return d.isEmpty() ? QStringLiteral( "scene%1" ).arg( index + 1 ) : d;
}
} // namespace

std::string RsTemporalDecomposeOperator::description() const
{
  return "Additive seasonal-trend decomposition of a per-pixel time series: "
         "a Whittaker-smoothed trend, a day-of-year climatology seasonal "
         "component (circularly smoothed over seasonal_window_days) and a "
         "remainder, computed on real acquisition day offsets. Bands are "
         "grouped per requested component, one band per scene date.";
}

Json::Value RsTemporalDecomposeOperator::schema() const
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
  Json::Value trendLambda = makeNumberParam(
      "trend_lambda", "Whittaker penalty for the trend (larger = smoother trend; too small "
                      "absorbs the seasonal signal)",
      kDefaultTrendLambda );
  setRange( trendLambda, 1e-9, 1e12 );
  props["trend_lambda"] = trendLambda;
  Json::Value seasonalWindow = makeIntegerParam(
      "seasonal_window_days", "Circular smoothing window of the day-of-year climatology (days)",
      kDefaultSeasonalWindowDays );
  setRange( seasonalWindow, 1, 61 );
  props["seasonal_window_days"] = seasonalWindow;
  Json::Value components = makeStringParam(
      "components",
      "Components to output, in order (subset of trend|seasonal|remainder; "
      "default: all three). Bands are grouped per component." );
  components["type"] = "array";
  components["items"] = Json::Value( Json::objectValue );
  components["items"]["type"] = "string";
  Json::Value componentEnum( Json::arrayValue );
  componentEnum.append( "trend" );
  componentEnum.append( "seasonal" );
  componentEnum.append( "remainder" );
  components["items"]["enum"] = componentEnum;
  props["components"] = components;
  props["tile_size"] = makeIntegerParam( "tile_size", "Streaming tile size (pixels)", kDefaultTileSize );
  props["output"] = makeOutputParam(
      "output",
      "Decomposition GeoTIFF (band groups per component: <component>_<date>)",
      "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Decomposition GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Scenes in the series", 0 );
  outputs["components"] = makeStringParam( "components", "Components written (in band order)", "" );
  outputs["bands"] = makeIntegerParam( "bands", "Output band count", 0 );
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalDecomposeOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "decomposition" );
  tags.append( "seasonal" );
  meta["tags"] = tags;
  meta["purpose"] = "Split a noisy series into slow change (trend), recurring seasonality "
                    "(seasonal) and anomalies (remainder) for phenology and disturbance "
                    "screening";
  meta["prerequisites"] = "Common grid, acquisition times, consistent radiometric state "
                          "(temporal preflight); at least 3 scenes; multi-year coverage for a "
                          "meaningful seasonal component";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  // tolerance-grade: deterministic flag stays false so the execution
  // cache never serves these artifacts (task_center cache gate).
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes × T)";
  meta["workflowHints"] = "Use the trend bands for slow change, seasonal bands for phenology "
                          "timing and remainder bands for anomaly screening (pairs with "
                          "rs:temporal_anomaly); run rs:temporal_gap_fill first on cloudy "
                          "series";
  meta["limitations"] = "Day-of-year climatology needs multi-year data to separate seasons — "
                        "with < 2 years the seasonal band is detrended anomalies; a too-small "
                        "trend_lambda absorbs the seasonal signal into the trend; Whittaker "
                        "trend is tolerance-grade (documented 1e-5); the working set is "
                        "O(tile × scenes), not O(tile)";
  return meta;
}

Json::Value RsTemporalDecomposeOperator::executionEstimate() const
{
  // 1 read tile + three column-major component tiles per scene (trend reuses
  // the series buffer) for a typical input.
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4,
                                                   1 + 3 * kTypicalSceneEstimate, 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalDecomposeOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  int scenes = kTypicalSceneEstimate;
  if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
    scenes = std::max<int>( 1, static_cast<int>( params["scenes"].size() ) );
  // 1 read tile + three column-major component tiles per scene date.
  return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4,
                                                   1 + 3 * static_cast<std::uint64_t>( scenes ),
                                                   0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalDecomposeOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const double trendLambda = getDouble( params, "trend_lambda", kDefaultTrendLambda );
  if ( !( trendLambda > 0.0 ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, "trend_lambda must be > 0" );
  const int seasonalWindowDays = getInt( params, "seasonal_window_days", kDefaultSeasonalWindowDays );
  if ( seasonalWindowDays < 1 || seasonalWindowDays > 61 )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "seasonal_window_days must be in [1, 61] (got " +
                               std::to_string( seasonalWindowDays ) + ")" );
  const std::vector<Component> components = parseComponents( params );

  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 3 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "temporal decomposition needs at least 3 scenes (got " +
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

  // Real time coordinates for the kernels: day offsets from the collection
  // reference epoch, plus the calendar day-of-year for the climatology (0 =
  // invalid/unknown time — the kernel excludes such samples from their doy
  // mean). Both derive from the actual UTC instants, never array indices.
  std::vector<double> tDays( sceneCount );
  std::vector<int> doyOf( sceneCount, 0 );
  for ( int s = 0; s < sceneCount; ++s )
  {
    tDays[s] = reader.sceneDayOffset( s );
    const auto &time = prepared.collection.scenes().at( s ).time;
    if ( time.valid )
      doyOf[s] =
          QDateTime::fromMSecsSinceEpoch( time.epochMillis, QTimeZone::utc() ).date().dayOfYear();
  }

  const int width = reader.width();
  const int height = reader.height();
  const int bandCount = static_cast<int>( components.size() ) * sceneCount;

  context.reportProgress( 0.05, "Creating decomposition output" );
  // Band layout: groups follow the components order; within a group, scenes
  // keep chronological order ("trend_<date>", "seasonal_<date>", ...).
  std::vector<QString> bandNames( static_cast<size_t>( bandCount ) );
  std::vector<Component> bandComponents( static_cast<size_t>( bandCount ) );
  std::vector<int> bandScenes( static_cast<size_t>( bandCount ) );
  {
    int b = 0;
    for ( const Component c : components )
    {
      for ( int s = 0; s < sceneCount; ++s, ++b )
      {
        bandComponents[static_cast<size_t>( b )] = c;
        bandScenes[static_cast<size_t>( b )] = s;
        bandNames[static_cast<size_t>( b )] =
            QStringLiteral( "%1_%2" )
                .arg( componentToken( c ),
                      sceneTag( prepared.collection.scenes().at( s ), s ) );
      }
    }
  }

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

  // Column-major per-pixel series: series[s * tilePixels + i]. The trend is
  // written back into the series buffer in place; seasonal and remainder get
  // their own column-major buffers (three T-tiles total).
  std::vector<float> series( tilePixels * static_cast<size_t>( sceneCount ) );
  std::vector<float> seasonalOut( tilePixels * static_cast<size_t>( sceneCount ) );
  std::vector<float> remainderOut( tilePixels * static_cast<size_t>( sceneCount ) );
  std::vector<float> tile( tilePixels );
  std::vector<float> pixelSeries( static_cast<size_t>( sceneCount ) );

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
      const temporal::DecompositionResult dec =
          temporal::seasonalDecompose( pixelSeries, tDays, doyOf, trendLambda,
                                       seasonalWindowDays );
      for ( int s = 0; s < sceneCount; ++s )
      {
        series[s * tilePixels + i] = dec.trend[static_cast<size_t>( s )];
        seasonalOut[s * tilePixels + i] = dec.seasonal[static_cast<size_t>( s )];
        remainderOut[s * tilePixels + i] = dec.remainder[static_cast<size_t>( s )];
      }
    }

    int band = 1;
    for ( const Component c : components )
    {
      const float *src = c == Component::Trend        ? series.data()
                         : c == Component::Seasonal   ? seasonalOut.data()
                                                      : remainderOut.data();
      for ( int s = 0; s < sceneCount; ++s, ++band )
      {
        if ( !out.writeBandWindow( band, x, y, w, h, src + s * tilePixels ) )
          throw RSOperatorError( ErrorCode::GdalError,
                                 "failed writing decomposition band " + std::to_string( band ) );
      }
    }

    ++tileDone;
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                            "Decomposition tiles " + std::to_string( tileDone ) + "/" +
                                std::to_string( tiles ) );
  }

  {
    int b = 1;
    for ( const Component c : components )
    {
      for ( int s = 0; s < sceneCount; ++s, ++b )
      {
        temporal_output::writeBandAcquisitionMetadata( out, b,
                                                       prepared.collection.scenes().at( s ),
                                                       componentToken( c ) );
        // writeBandAcquisitionMetadata appends the date to the description;
        // pin the exact "<component>_<date>" band name on top.
        GDALSetDescription(
          GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b ),
          bandNames[static_cast<size_t>( b - 1 )].toUtf8().constData() );
      }
    }
  }
  QString componentSummary;
  for ( const Component c : components )
  {
    if ( !componentSummary.isEmpty() )
      componentSummary += QLatin1Char( ',' );
    componentSummary += componentToken( c );
  }
  temporal_output::writeTemporalDatasetMetadata(
    out, prepared.collection, "rs:temporal_decompose",
    QStringLiteral( "trend_lambda=%1 seasonal_window_days=%2 components=%3" )
        .arg( trendLambda )
        .arg( seasonalWindowDays )
        .arg( componentSummary ) );
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
  Json::Value componentList( Json::arrayValue );
  for ( const Component c : components )
    componentList.append( componentToken( c ).toStdString() );
  result["components"] = componentList;
  result["bands"] = bandCount;
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  // 1 read tile + three column-major component tiles per scene date.
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
    TemporalTileReader::estimateWorkingSetBytes( tileSize, tileSize,
                                                 1 + 3 * static_cast<std::uint64_t>( sceneCount ),
                                                 0 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal decomposition complete" );
  return result;
}

} // namespace sicnu::operators::rs
