// src/operators/rs/rs_temporal_harmonic_fit_operator.cpp
#include "rs_temporal_harmonic_fit_operator.h"

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
constexpr int kDefaultHarmonics = 2;
constexpr int kTypicalSceneCount = 8;
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

/// "fitted_<date>" per scene; scene index when the acquisition time is
/// invalid (preflight normally rejects those before we get here).
QString fittedBandDescription( const sicnu::temporal::TemporalSceneRef &scene, int sceneIndex )
{
  return scene.time.valid
           ? QStringLiteral( "fitted_" ) + scene.time.dateString()
           : QStringLiteral( "fitted_%1" ).arg( sceneIndex + 1 );
}
} // namespace

std::string RsTemporalHarmonicFitOperator::description() const
{
  return "Per-pixel harmonic regression (annual + sub-annual Fourier terms) "
         "across a multi-date collection: y(t) = a0 + Σ_k [ a_k·sin(2πkt/365.25) "
         "+ b_k·cos(2πkt/365.25) ] with t in real acquisition days since the "
         "collection reference epoch. Solved in closed form (weighted least "
         "squares) with optional IRLS outlier damping. Outputs one fitted band "
         "per acquisition date plus RMSE and R²; optionally the raw "
         "coefficients. Pixels with fewer than minObservations valid samples "
         "stay NoData.";
}

Json::Value RsTemporalHarmonicFitOperator::schema() const
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
  Json::Value harmonicsParam = makeIntegerParam( "harmonics",
                                                 "Number of sin/cos harmonic pairs (1 = annual only)",
                                                 kDefaultHarmonics );
  setRange( harmonicsParam, 1, 6 );
  props["harmonics"] = harmonicsParam;
  props["robust"] = makeBooleanParam( "robust", "Damp outliers with IRLS reweighting (Huber-like, 1.5·MAD scale)",
                                      false );
  props["minObservations"] = makeIntegerParam( "minObservations",
                                               "Minimum valid samples per pixel (fewer → NoData)", 6 );
  props["writeCoefficients"] = makeBooleanParam( "writeCoefficients",
                                                 "Append coefficient bands "
                                                 "(coef_intercept, coef_sin1, coef_cos1, ...)",
                                                 false );
  props["output"] = makeOutputParam( "output",
                                     "Harmonic-fit GeoTIFF (bands: fitted per date, rmse, r2"
                                     "[, coefficients])",
                                     "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Harmonic-fit GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Dates in the regression", 0 );
  outputs["harmonics"] = makeIntegerParam( "harmonics", "Harmonic pairs used in the fit", 0 );
  outputs["robust"] = makeBooleanParam( "robust", "Whether IRLS outlier damping was applied", false );
  outputs["bands"] = makeIntegerParam( "bands", "Output band count (fitted + rmse + r2 [, coefficients])", 0 );
  outputs["fittedPixelFraction"] = makeNumberParam( "fittedPixelFraction",
                                                    "Pixels with a successful fit / total pixels", 0.0 );
  outputs["timeStart"] = makeStringParam( "timeStart", "First acquisition date in the series (ISO)", "" );
  outputs["timeEnd"] = makeStringParam( "timeEnd", "Last acquisition date in the series (ISO)", "" );
  Json::Value memory = makeStringParam( "memory", "Streaming working-set summary (tile size and estimated bytes)" );
  memory["type"] = "object";
  outputs["memory"] = memory;
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalHarmonicFitOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "harmonic" );
  tags.append( "regression" );
  meta["tags"] = tags;
  meta["purpose"] = "Per-pixel harmonic regression (annual + sub-annual Fourier terms) for seasonal "
                    "signal modeling — reconstructs the series at each acquisition date and "
                    "exposes amplitude/phase through the coefficients";
  meta["prerequisites"] = "Common grid, acquisition times, consistent radiometric state "
                          "(temporal preflight); at least minObservations valid samples per pixel";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes × terms²)";
  meta["workflowHints"] = "Fit an rs:temporal_index_series or rs:temporal_smooth band for seasonal "
                          "modeling; the fitted bands reproduce the series at each acquisition "
                          "date; robust=true damps cloud/outlier residuals";
  meta["limitations"] = "Assumes a 365.25-day annual period; underdetermined pixels "
                        "(valid samples < 1 + 2·harmonics) stay NoData; robust mode damps "
                        "outliers but does not reject them";
  return meta;
}

Json::Value RsTemporalHarmonicFitOperator::executionEstimate() const
{
  // Typical 8-date collection: series + fitted per scene, RMSE/R², coefficient
  // rows (1 + 2·harmonics), pixel gather + read tile.
  const int buffers = 2 * kTypicalSceneCount + ( 1 + 2 * kDefaultHarmonics ) + 3;
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4, buffers, 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalHarmonicFitOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const int harmonics = std::clamp( getInt( params, "harmonics", kDefaultHarmonics ), 1, 6 );
  const bool writeCoefficients = getBool( params, "writeCoefficients", false );
  int scenes = params["scenes"].isArray() ? params["scenes"].size() : kTypicalSceneCount;
  scenes = std::max( scenes, 1 );
  const int coefBands = writeCoefficients ? ( 1 + 2 * harmonics ) : 0;
  // series + fitted per scene, RMSE/R² tile buffers, coefficient rows,
  // per-pixel series gather + read tile.
  const int buffers = 2 * scenes + coefBands + 3;
  return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4, buffers, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalHarmonicFitOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );
  const int harmonics = std::clamp( getInt( params, "harmonics", kDefaultHarmonics ), 1, 6 );
  const bool robust = getBool( params, "robust", false );
  const int minObservations = std::max( getInt( params, "minObservations", 6 ), 1 );
  const bool writeCoefficients = getBool( params, "writeCoefficients", false );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 4 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "harmonic fit needs at least 4 scenes (got " +
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

  // Real regressors: days since the collection reference epoch (scene 0).
  std::vector<double> tDays( sceneCount );
  for ( int s = 0; s < sceneCount; ++s )
    tDays[s] = reader.sceneDayOffset( s );

  // Day-of-year per scene (UTC calendar date of the acquisition; 0 when the
  // time is invalid). Not used by the harmonic design matrix (t is the day
  // offset) but computed for the uniform temporal-operator prologue.
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

  const int coefBands = writeCoefficients ? ( 1 + 2 * harmonics ) : 0;
  const int bandCount = sceneCount + 2 + coefBands;
  const int width = reader.width();
  const int height = reader.height();

  context.reportProgress( 0.05, "Creating harmonic fit output" );
  GdalDatasetWrapper out;
  QString outErr;
  temporal_output::TemporalOutputGuard guard;
  guard.manage( &out, QString::fromStdString( outputPath ) );
  if ( !out.create( QString::fromStdString( outputPath ), width, height, bandCount,
                    static_cast<int>( GDT_Float32 ), reader.geoTransform(), reader.projection(),
                    &outErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to create output: " + outErr.toStdString() );

  // Band descriptions: fitted_<date> per scene, then rmse, r2, coefficients.
  std::vector<QString> bandNames;
  bandNames.reserve( bandCount );
  for ( int s = 0; s < sceneCount; ++s )
    bandNames.push_back( fittedBandDescription( prepared.collection.scenes().at( s ), s ) );
  bandNames.push_back( QStringLiteral( "rmse" ) );
  bandNames.push_back( QStringLiteral( "r2" ) );
  if ( writeCoefficients )
  {
    bandNames.push_back( QStringLiteral( "coef_intercept" ) );
    for ( int k = 1; k <= harmonics; ++k )
    {
      bandNames.push_back( QStringLiteral( "coef_sin%1" ).arg( k ) );
      bandNames.push_back( QStringLiteral( "coef_cos%1" ).arg( k ) );
    }
  }
  for ( int b = 1; b <= bandCount; ++b )
  {
    out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b ),
                        bandNames[b - 1].toUtf8().constData() );
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
  std::vector<float> fitted( static_cast<size_t>( sceneCount ) * tilePixels );
  std::vector<float> rmseBuf( tilePixels );
  std::vector<float> r2Buf( tilePixels );
  std::vector<float> coefBufs( static_cast<size_t>( coefBands ) * tilePixels );
  std::vector<float> pixSeries( sceneCount );
  std::uint64_t fittedPixels = 0;
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

    // One closed-form harmonic fit per pixel over the whole series.
    for ( size_t i = 0; i < pixels; ++i )
    {
      int valid = 0;
      for ( int s = 0; s < sceneCount; ++s )
      {
        const float v = series[s * tilePixels + i];
        pixSeries[s] = v;
        if ( std::isfinite( v ) )
          ++valid;
      }
      bool ok = valid >= minObservations;
      sicnu::temporal::HarmonicFitResult fit;
      if ( ok )
      {
        fit = sicnu::temporal::harmonicFit( pixSeries, tDays, harmonics, robust );
        // The kernel leaves coefficients empty on a singular/underdetermined
        // system (valid < terms) — treat that as NoData.
        ok = !fit.coefficients.empty();
      }
      if ( ok )
      {
        for ( int s = 0; s < sceneCount; ++s )
          fitted[s * tilePixels + i] = fit.fitted[s];
        rmseBuf[i] = static_cast<float>( fit.rmse );
        r2Buf[i] = static_cast<float>( fit.r2 );
        for ( int c = 0; c < coefBands; ++c )
          coefBufs[static_cast<size_t>( c ) * tilePixels + i] =
            static_cast<float>( fit.coefficients[c] );
        ++fittedPixels;
      }
      else
      {
        for ( int s = 0; s < sceneCount; ++s )
          fitted[s * tilePixels + i] = kNan;
        rmseBuf[i] = kNan;
        r2Buf[i] = kNan;
        for ( int c = 0; c < coefBands; ++c )
          coefBufs[static_cast<size_t>( c ) * tilePixels + i] = kNan;
      }
    }
    context.throwIfCancelled();

    auto writeBand = [&]( int band, const float *src ) {
      if ( !out.writeBandWindow( band, x, y, w, h, src ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing harmonic fit band" );
    };
    for ( int s = 0; s < sceneCount; ++s )
      writeBand( s + 1, fitted.data() + s * tilePixels );
    writeBand( sceneCount + 1, rmseBuf.data() );
    writeBand( sceneCount + 2, r2Buf.data() );
    for ( int c = 0; c < coefBands; ++c )
      writeBand( sceneCount + 3 + c, coefBufs.data() + static_cast<size_t>( c ) * tilePixels );

    ++tileDone;
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                            "Harmonic fit tiles " + std::to_string( tileDone ) + "/" +
                                std::to_string( tiles ) );
  }

  // Per-band acquisition metadata for the fitted stack; the helper sets its
  // own description, so re-apply the exact "fitted_<date>" form afterwards.
  for ( int s = 0; s < sceneCount; ++s )
  {
    const auto &scene = prepared.collection.scenes().at( s );
    temporal_output::writeBandAcquisitionMetadata( out, s + 1, scene, QStringLiteral( "fitted" ) );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), s + 1 ),
                        bandNames[s].toUtf8().constData() );
  }

  temporal_output::writeTemporalDatasetMetadata(
    out, prepared.collection, "rs:temporal_harmonic_fit",
    QStringLiteral( "band_role=%1 harmonics=%2 robust=%3 minObservations=%4" )
        .arg( bandRole.isEmpty() ? QStringLiteral( "band:1" ) : bandRole )
        .arg( harmonics )
        .arg( robust ? QStringLiteral( "true" ) : QStringLiteral( "false" ) )
        .arg( minObservations ) );
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
  result["harmonics"] = harmonics;
  result["robust"] = robust;
  result["bands"] = bandCount;
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  result["fittedPixelFraction"] =
      totalPixels > 0 ? static_cast<double>( fittedPixels ) / totalPixels : 0.0;
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  // series + fitted per scene, RMSE/R² + coefficient rows + gather/tile buffers.
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
    TemporalTileReader::estimateWorkingSetBytes( tileSize, tileSize,
                                                 2 * sceneCount + coefBands + 3, 0 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal harmonic fit complete" );
  return result;
}

} // namespace sicnu::operators::rs
