// src/operators/rs/rs_temporal_model_select_operator.cpp
#include "rs_temporal_model_select_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "operators/rs/rs_temporal_output.h"
#include "processing/algorithms/temporal/temporal_selection.h"
#include "processing/algorithms/temporal/temporal_stream.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QDateTime>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace sicnu::operators::rs
{

using namespace params;
using temporal::TemporalTileReader;

namespace
{
constexpr int kDefaultTileSize = 256;
constexpr int kTypicalSceneEstimate = 12;
} // namespace

std::string RsTemporalModelSelectOperator::description() const
{
  return "Bounded per-segment model selection for seasonal-trend series: "
         "every pixel's candidate grid {harmonic order 0..maxHarmonics} x "
         "{break budget 0..maxBreaks} is scored with AICc, BIC, or "
         "deterministic contiguous-block cross-validation, and the winner "
         "(exact ties resolve to the smallest model) is reported with its "
         "parameter count, score, and segments actually fitted. An explicit, "
         "bounded, explainable selection - NOT CCDC (no L1, no online "
         "per-element model update). Degenerate pixels (too few valid "
         "samples, no fittable candidate) report NaN - a refusal, never a "
         "fabricated model.";
}

Json::Value RsTemporalModelSelectOperator::schema() const
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
  Json::Value maxHarmonics = makeIntegerParam(
      "maxHarmonics", "Largest harmonic-order candidate (candidates 0..maxHarmonics)", 2 );
  setRange( maxHarmonics, 0, 3 );
  props["maxHarmonics"] = maxHarmonics;
  Json::Value maxBreaks = makeIntegerParam(
      "maxBreaks", "Largest break-budget candidate (candidates 0..maxBreaks)", 2 );
  setRange( maxBreaks, 0, 4 );
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
  props["penalty"] = makeEnumParam(
      "penalty",
      "Selection criterion: aicc (small-sample corrected), bic, block_cv "
      "(deterministic contiguous folds)",
      { "aicc", "bic", "block_cv" }, "aicc" );
  Json::Value cvFolds =
    makeIntegerParam( "cvFolds", "Contiguous folds for block_cv (2-10)", 4 );
  setRange( cvFolds, 2, 10 );
  props["cvFolds"] = cvFolds;
  props["robust"] = makeBooleanParam(
      "robust", "IRLS Huber reweighting inside candidate segment fits", false );
  props["tile_size"] =
    makeIntegerParam( "tile_size", "Streaming tile size (pixels)", kDefaultTileSize );
  props["output"] = makeOutputParam(
      "output",
      "Selection GeoTIFF: selected_harmonics, selected_max_breaks, "
      "selected_params, selected_score, valid_count",
      "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Selection GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Scenes in the series", 0 );
  outputs["pixelsFitted"] =
    makeIntegerParam( "pixelsFitted", "Pixels with a selected model", 0 );
  Json::Value byHarmonics = makeStringParam( "pixelsByHarmonics",
                                             "Selected-harmonics histogram (key = order)" );
  byHarmonics["type"] = "object";
  outputs["pixelsByHarmonics"] = byHarmonics;
  Json::Value byBreaks = makeStringParam( "pixelsByBreaks",
                                          "Selected-break-budget histogram (key = budget)" );
  byBreaks["type"] = "object";
  outputs["pixelsByBreaks"] = byBreaks;
  Json::Value memory = makeStringParam( "memory", "Streaming working-set summary" );
  memory["type"] = "object";
  outputs["memory"] = memory;
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalModelSelectOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "model-selection" );
  tags.append( "harmonic-model" );
  meta["tags"] = tags;
  meta["purpose"] = "Pick the seasonal-trend complexity a series actually "
                    "supports before interpreting fits: crop-cycle order, "
                    "harmonic resolution, segmentation budget";
  meta["prerequisites"] = "Common grid, acquisition times, consistent "
                          "radiometric state; >= 4 valid samples";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] =
    "O(candidates × tile × scenes × iterations × terms²) — up to "
    "(maxHarmonics+1) × (maxBreaks+1) fits per pixel; × (cvFolds+1) for "
    "block_cv";
  meta["workflowHints"] =
    "Run before rs:temporal_harmonic_breaks / rs:temporal_seasonal_breaks to "
    "justify the harmonic order; feed selected_max_breaks into the break "
    "budget of the change operators";
  meta["limitations"] =
    "Bounded grid only (the true optimum may sit outside it); AICc/BIC "
    "assume Gaussian residuals; block_cv refits per fold and is markedly "
    "slower; candidate order and tie rules are documented and fixed";
  return meta;
}

Json::Value RsTemporalModelSelectOperator::executionEstimate() const
{
  return sicnu::processing::makeStreamingEstimate(
      kDefaultTileSize, kDefaultTileSize, 1, 4, 2 + kTypicalSceneEstimate, 0,
      3 * 1024 * 1024 );
}

Json::Value RsTemporalModelSelectOperator::estimateExecution(
    const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  int scenes = kTypicalSceneEstimate;
  if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
    scenes = std::max<int>( 1, static_cast<int>( params["scenes"].size() ) );
  return sicnu::processing::makeStreamingEstimate(
      tileSize, tileSize, 1, 4, 2 + static_cast<std::uint64_t>( scenes ), 0,
      3 * 1024 * 1024 );
}

Json::Value RsTemporalModelSelectOperator::run( const Json::Value &params,
                                                RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );

  const int maxHarmonics = std::clamp( getInt( params, "maxHarmonics", 2 ), 0, 3 );
  const int maxBreaks = std::clamp( getInt( params, "maxBreaks", 2 ), 0, 4 );
  double minSegmentDays = getDouble( params, "minSegmentDays", 90.0 );
  if ( !( minSegmentDays > 0.0 ) )
    minSegmentDays = 90.0;
  const double minImprovement = getDouble( params, "minImprovement", 0.1 );
  if ( !( minImprovement > 0.0 && minImprovement <= 1.0 ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "minImprovement must be in (0, 1]" );
  const std::string penaltyToken =
    getEnum( params, "penalty", { "aicc", "bic", "block_cv" }, "aicc" );
  temporal::SelectionPenalty penalty = temporal::SelectionPenalty::AICc;
  if ( penaltyToken == "bic" )
    penalty = temporal::SelectionPenalty::BIC;
  else if ( penaltyToken == "block_cv" )
    penalty = temporal::SelectionPenalty::BlockCv;
  const int cvFolds = std::clamp( getInt( params, "cvFolds", 4 ), 2, 10 );
  const bool robust = getBool( params, "robust", false );

  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 4 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "model selection needs at least 4 scenes (got " +
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

  temporal::ModelSelectionOptions selectionOptions;
  selectionOptions.maxHarmonics = maxHarmonics;
  selectionOptions.maxBreaks = maxBreaks;
  selectionOptions.minSegment = minSegment;
  selectionOptions.minImprovement = minImprovement;
  selectionOptions.penalty = penalty;
  selectionOptions.cvFolds = cvFolds;
  selectionOptions.robust = robust;

  constexpr int kBandCount = 5;  // harmonics, maxBreaks, params, score, valid_count
  const int width = reader.width();
  const int height = reader.height();

  context.reportProgress( 0.05, "Creating model-select output" );
  GdalDatasetWrapper out;
  QString outErr;
  temporal_output::TemporalOutputGuard guard;
  guard.manage( &out, QString::fromStdString( outputPath ) );
  if ( !out.create( QString::fromStdString( outputPath ), width, height, kBandCount,
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
  describeBand( "selected_harmonics" );
  describeBand( "selected_max_breaks" );
  describeBand( "selected_params" );
  describeBand( "selected_score" );
  describeBand( "valid_count" );

  const size_t maxTilePixels =
      static_cast<size_t>( std::min( tileSize, width ) ) *
      static_cast<size_t>( std::min( tileSize, height ) );
  constexpr size_t kMaxSeriesBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
  const size_t tileFloatsPerPixel =
    2 * static_cast<size_t>( sceneCount ) + 5;
  if ( tileFloatsPerPixel * maxTilePixels * sizeof( float ) > kMaxSeriesBytes )
    throw RSOperatorError(
        ErrorCode::InvalidParameter,
        "tile_size " + std::to_string( tileSize ) + " x " + std::to_string( sceneCount ) +
            " scenes exceeds the 2 GiB tile working-set budget; reduce tile_size" );
  const size_t tilePixels = maxTilePixels;

  std::vector<float> tile( tilePixels );
  std::vector<float> series( static_cast<size_t>( sceneCount ) * tilePixels );
  std::vector<float> harmonicsBuf( tilePixels );
  std::vector<float> breaksBuf( tilePixels );
  std::vector<float> paramsBuf( tilePixels );
  std::vector<float> scoreBuf( tilePixels );
  std::vector<float> validBuf( tilePixels );
  std::vector<float> pixSeries( static_cast<size_t>( sceneCount ) );

  std::uint64_t pixelsFitted = 0;
  std::map<std::string, std::uint64_t> byHarmonics;
  std::map<std::string, std::uint64_t> byBreaks;
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

      const temporal::ModelSelectionResult selection = temporal::selectSeasonalTrendModel(
          pixSeries, tDays, selectionOptions );
      const float nan = std::numeric_limits<float>::quiet_NaN();
      if ( selection.selected )
      {
        ++pixelsFitted;
        harmonicsBuf[i] = static_cast<float>( selection.selectedHarmonics );
        breaksBuf[i] = static_cast<float>( selection.selectedMaxBreaks );
        paramsBuf[i] = static_cast<float>( selection.selectedParamCount );
        scoreBuf[i] = std::isfinite( selection.selectedScore )
                        ? static_cast<float>( selection.selectedScore )
                        : nan;
        ++byHarmonics[std::to_string( selection.selectedHarmonics )];
        ++byBreaks[std::to_string( selection.selectedMaxBreaks )];
      }
      else
      {
        harmonicsBuf[i] = nan;
        breaksBuf[i] = nan;
        paramsBuf[i] = nan;
        scoreBuf[i] = nan;
      }
      validBuf[i] = static_cast<float>(
          std::count_if( pixSeries.begin(), pixSeries.end(),
                         []( float v ) { return std::isfinite( v ); } ) );
      context.throwIfCancelled();
    }

    int ob = 1;
    auto writeBandWindow = [&]( const std::vector<float> &buf ) {
      if ( !out.writeBandWindow( ob++, x, y, w, h, buf.data() ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing output band" );
    };
    writeBandWindow( harmonicsBuf );
    writeBandWindow( breaksBuf );
    writeBandWindow( paramsBuf );
    writeBandWindow( scoreBuf );
    writeBandWindow( validBuf );

    ++tileDone;
    context.reportProgress(
        0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
        "Model-select tiles " + std::to_string( tileDone ) + "/" +
            std::to_string( tiles ) );
  }

  temporal_output::writeTemporalDatasetMetadata(
      out, prepared.collection, "rs:temporal_model_select",
      QStringLiteral( "maxHarmonics=%1 maxBreaks=%2 penalty=%3 cvFolds=%4 robust=%5" )
          .arg( maxHarmonics )
          .arg( maxBreaks )
          .arg( QString::fromStdString( penaltyToken ) )
          .arg( cvFolds )
          .arg( robust ? 1 : 0 ) );
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
  result["pixelsFitted"] = Json::Value::UInt64( pixelsFitted );
  Json::Value histH( Json::objectValue );
  for ( const auto &kv : byHarmonics )
    histH[kv.first] = Json::Value::UInt64( kv.second );
  result["pixelsByHarmonics"] = histH;
  Json::Value histB( Json::objectValue );
  for ( const auto &kv : byBreaks )
    histB[kv.first] = Json::Value::UInt64( kv.second );
  result["pixelsByBreaks"] = histB;
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
      TemporalTileReader::estimateWorkingSetBytes(
          tileSize, tileSize, 7 + static_cast<std::uint64_t>( sceneCount ), 0 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal model select complete" );
  return result;
}

} // namespace sicnu::operators::rs
