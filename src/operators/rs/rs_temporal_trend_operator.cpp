// src/operators/rs/rs_temporal_trend_operator.cpp
#include "rs_temporal_trend_operator.h"

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
const char *const kTrendBandNames[] = { "slope", "intercept", "r2", "n", "rmse" };
} // namespace

std::string RsTemporalTrendOperator::description() const
{
  return "Per-pixel linear trend (ordinary least squares) across a multi-date "
         "collection using real acquisition time intervals (days since the "
         "collection reference epoch). Outputs slope, intercept, R², valid "
         "observation count and RMSE; the regression accumulates online "
         "(numerically stable, O(tile) memory independent of date count).";
}

Json::Value RsTemporalTrendOperator::schema() const
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
  props["output"] = makeOutputParam( "output",
                                     "Trend GeoTIFF (bands: slope [per day], intercept, r2, n, rmse)",
                                     "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Trend GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Scenes in the regression", 0 );
  return makeRootSchema( displayName(), description(), props, outputs );
}

Json::Value RsTemporalTrendOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "trend" );
  tags.append( "regression" );
  meta["tags"] = tags;
  meta["purpose"] = "Per-pixel linear trend with real time intervals — slope/R²/intercept for "
                    "change-over-time analysis (e.g. vegetation decline)";
  meta["prerequisites"] = "Common grid, acquisition times, consistent radiometric state "
                          "(temporal preflight); slope units are per DAY";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes)";
  meta["workflowHints"] = "Run on an rs:temporal_index_series output band for index trends; "
                          "multiply slope by 365.25 for per-year rates";
  meta["limitations"] = "Linear model only; date-precision times give day-resolution "
                        "regressors; R² of a zero-variance series is reported as 1.0 "
                        "(perfectly explained flat fit)";
  return meta;
}

Json::Value RsTemporalTrendOperator::executionEstimate() const
{
  return makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4, 7, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalTrendOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  // 2 tile buffers + OnlineRegression (48 B = 12 float slots) per pixel
  return makeStreamingEstimate( tileSize, tileSize, 1, 4, 14, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalTrendOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 2 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "linear trend needs at least 2 scenes (got " +
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

  // Real regressors: days since the collection reference epoch (scene 0).
  std::vector<double> tDays( sceneCount );
  for ( int s = 0; s < sceneCount; ++s )
    tDays[s] = reader.sceneDayOffset( s );

  const int width = reader.width();
  const int height = reader.height();

  context.reportProgress( 0.05, "Creating trend output" );
  GdalDatasetWrapper out;
  QString outErr;
  temporal_output::TemporalOutputGuard guard;
  guard.manage( &out, QString::fromStdString( outputPath ) );
  if ( !out.create( QString::fromStdString( outputPath ), width, height, 5,
                    static_cast<int>( GDT_Float32 ), reader.geoTransform(), reader.projection(),
                    &outErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to create output: " + outErr.toStdString() );
  for ( int b = 1; b <= 5; ++b )
  {
    out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b ),
                        kTrendBandNames[b - 1] );
  }

  const int tiles = reader.totalTileCount();
  const size_t tilePixels = static_cast<size_t>( tileSize ) * tileSize;

  std::vector<float> tile( tilePixels );
  std::vector<temporal::stats::OnlineRegression> reg( tilePixels );
  std::vector<float> bandBuf( tilePixels );
  std::uint64_t pixelsWithTrend = 0;
  int tileDone = 0;

  for ( int t = 0; t < tiles; ++t )
  {
    int x = 0, y = 0, w = 0, h = 0;
    reader.tileRect( t, &x, &y, &w, &h );
    const size_t pixels = static_cast<size_t>( w ) * h;
    for ( size_t i = 0; i < pixels; ++i )
      reg[i] = temporal::stats::OnlineRegression{};

    for ( int s = 0; s < sceneCount; ++s )
    {
      if ( !reader.readSceneBandTile( s, analysisBands[s], t, tile.data() ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "failed reading scene " +
                                   prepared.collection.scenes().at( s ).path.toStdString() );
      const double td = tDays[s];
      for ( size_t i = 0; i < pixels; ++i )
      {
        const float v = tile[i];
        if ( std::isfinite( v ) )
          reg[i].add( td, v );
      }
      context.throwIfCancelled();
    }

    auto writeBand = [&]( int band, auto fill ) {
      for ( size_t i = 0; i < pixels; ++i )
        bandBuf[i] = fill( i );
      if ( !out.writeBandWindow( band, x, y, w, h, bandBuf.data() ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing trend band" );
    };
    writeBand( 1, [&]( size_t i ) { return reg[i].solvable() ? static_cast<float>( reg[i].slope() ) : kNan; } );
    writeBand( 2, [&]( size_t i ) { return reg[i].solvable() ? static_cast<float>( reg[i].intercept() ) : kNan; } );
    writeBand( 3, [&]( size_t i ) { return reg[i].solvable() ? static_cast<float>( reg[i].r2() ) : kNan; } );
    writeBand( 4, [&]( size_t i ) { return static_cast<float>( reg[i].n ); } );
    writeBand( 5, [&]( size_t i ) {
      return ( reg[i].n > 2 && reg[i].solvable() ) ? static_cast<float>( reg[i].rmse() ) : kNan;
    } );
    for ( size_t i = 0; i < pixels; ++i )
      if ( reg[i].solvable() )
        ++pixelsWithTrend;

    ++tileDone;
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                            "Trend tiles " + std::to_string( tileDone ) + "/" +
                                std::to_string( tiles ) );
  }

  temporal_output::writeTemporalDatasetMetadata(
    out, prepared.collection, "rs:temporal_trend",
    QStringLiteral( "band_role=%1 slope_units=per_day" )
        .arg( bandRole.isEmpty() ? QStringLiteral( "band:1" ) : bandRole ) );
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
  result["slopeUnits"] = "per_day";
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  result["trendPixelFraction"] =
      totalPixels > 0 ? static_cast<double>( pixelsWithTrend ) / totalPixels : 0.0;
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  // OnlineRegression is 8+5*8=48 B = 12 float slots; plus tile + bandBuf.
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
    TemporalTileReader::estimateWorkingSetBytes( tileSize, tileSize, 2, 12 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal trend complete" );
  return result;
}

} // namespace sicnu::operators::rs
