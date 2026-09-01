// src/operators/rs/rs_temporal_index_operator.cpp
#include "rs_temporal_index_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "operators/rs/rs_temporal_output.h"
#include "processing/algorithms/math_utils.h"
#include "processing/algorithms/spectral_indices.h"
#include "processing/algorithms/temporal/temporal_stream.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"


#include <gdal.h>

#include <algorithm>
#include <cmath>
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

const std::vector<std::string> &indexNames()
{
  static const std::vector<std::string> names = { "NDVI", "EVI",  "SAVI", "NDWI", "NDBI",
                                                  "MNDWI", "NBR", "NDRE", "NDSI", "NDTI" };
  return names;
}

/// Role pairs per index — identical to the single-scene rs:spectral_index
/// dispatch (rs_spectral_index_operator.cpp); the kernels are shared, so only
/// the band routing lives here.
struct IndexRoles
{
  const char *a = nullptr;
  const char *b = nullptr;
  const char *c = nullptr; // EVI blue
};

IndexRoles rolesForIndex( const std::string &index )
{
  if ( index == "NDVI" )
    return { "nir", "red" };
  if ( index == "EVI" )
    return { "nir", "red", "blue" };
  if ( index == "SAVI" )
    return { "nir", "red" };
  if ( index == "NDWI" )
    return { "green", "nir" };
  if ( index == "NDBI" )
    return { "swir1", "nir" };
  if ( index == "MNDWI" )
    return { "green", "swir1" };
  if ( index == "NBR" )
    return { "nir", "swir2" };
  if ( index == "NDRE" )
    return { "nir", "red_edge" };
  if ( index == "NDSI" )
    return { "green", "swir1" };
  if ( index == "NDTI" )
    return { "swir1", "swir2" };
  return {};
}

bool computeIndexTile( const std::string &index, const float *a, const float *b, const float *c,
                       float *out, size_t count )
{
  if ( index == "NDVI" )
    return SpectralIndices::ndvi( a, b, out, count );
  if ( index == "EVI" )
    return SpectralIndices::evi( a, b, c, out, count );
  if ( index == "SAVI" )
    return SpectralIndices::savi( a, b, out, count );
  if ( index == "NDWI" )
    return SpectralIndices::ndwi( a, b, out, count );
  if ( index == "NDBI" )
    return SpectralIndices::ndbi( a, b, out, count );
  if ( index == "MNDWI" )
    return SpectralIndices::mndwi( a, b, out, count );
  // Ratio indices reduce to the same shared kernel the single-scene operator
  // uses for NBR/NDRE/NDSI/NDTI.
  return MathUtils::normalizedDifference( a, b, out, count );
}
} // namespace

std::string RsTemporalIndexSeriesOperator::description() const
{
  return "Spectral index time series: computes NDVI/EVI/SAVI/NDWI/NDBI/MNDWI/"
         "NBR/NDRE/NDSI/NDTI for every date of a temporal collection using the "
         "single-scene spectral-index kernels, output as one stacked raster "
         "(one band per date) with per-band acquisition metadata.";
}

Json::Value RsTemporalIndexSeriesOperator::schema() const
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
  props["index"] = makeEnumParam( "index", "Spectral index (kernel-identical to rs:spectral_index)",
                                  indexNames(), "NDVI" );
  Json::Value bands = makeStringParam( "bands",
                                       "Global band overrides, e.g. {\"nir\": 4, \"red\": 3} "
                                       "(per-scene 'bands' entries also honored)" );
  bands["type"] = "object";
  props["bands"] = bands;
  props["duplicate_policy"] = makeEnumParam( "duplicate_policy", "Duplicate acquisition instants",
                                             { "keep_all", "reject" }, "keep_all" );
  props["apply_qa_masking"] = makeBooleanParam( "apply_qa_masking", "Treat QA/cloud-masked samples "
                                                                    "as NoData in the index inputs",
                                                true );
  props["tile_size"] = makeIntegerParam( "tile_size", "Streaming tile size (pixels)", kDefaultTileSize );
  props["output"] = makeOutputParam( "output", "Index-series GeoTIFF (band per date)", "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Index-series GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Dates in the series", 0 );
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "index", "output" } );
  return root;
}

Json::Value RsTemporalIndexSeriesOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "spectral-index" );
  tags.append( "time-series" );
  meta["tags"] = tags;
  meta["purpose"] = "Per-date spectral index stack from a multi-date collection, reusing the "
                    "single-scene index kernels (identical semantics)";
  meta["prerequisites"] = "Common grid, acquisition times, index band roles resolvable in every "
                          "scene (temporal preflight)";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes)";
  meta["workflowHints"] = "Feed the stack into rs:temporal_summary / rs:temporal_trend for "
                          "index-based change analysis";
  meta["limitations"] = "One output band per date; very long series produce very large stacks";
  return meta;
}

Json::Value RsTemporalIndexSeriesOperator::executionEstimate() const
{
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 3, 4, 4, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalIndexSeriesOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const std::string index = getEnum( params, "index", indexNames(), "NDVI" );
  const int kernelInputs = index == "EVI" ? 3 : 2;
  return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4, kernelInputs + 1, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalIndexSeriesOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const std::string index = getEnum( params, "index", indexNames(), "NDVI" );
  const IndexRoles roles = rolesForIndex( index );
  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );

  // Preflight requires every index band role in every scene.
  std::vector<QString> requiredRoles;
  if ( roles.a )
    requiredRoles.emplace_back( QLatin1String( roles.a ) );
  if ( roles.b )
    requiredRoles.emplace_back( QLatin1String( roles.b ) );
  if ( roles.c )
    requiredRoles.emplace_back( QLatin1String( roles.c ) );

  auto prepared = temporal_input::prepareTemporalRun( params, context, requiredRoles, {}, 0 );
  const int sceneCount = prepared.collection.sceneCount();

  temporal::TemporalStreamOptions streamOptions;
  streamOptions.tileWidth = tileSize;
  streamOptions.tileHeight = tileSize;
  streamOptions.applyQaMasking = applyQaMasking;

  QString readerError;
  TemporalTileReader reader( prepared.collection, prepared.preflight, streamOptions, &readerError );
  if ( !readerError.isEmpty() )
    throw RSOperatorError( ErrorCode::GdalError, readerError.toStdString() );

  // Resolve kernel input bands per scene (role metadata > positional fallback).
  std::vector<int> bandA( sceneCount ), bandB( sceneCount ), bandC( sceneCount, 0 );
  bool anyFallback = false;
  for ( int s = 0; s < sceneCount; ++s )
  {
    bool fa = false, fb = false, fc = false;
    bandA[s] = reader.bandForRole( s, QLatin1String( roles.a ), 0, &fa );
    bandB[s] = reader.bandForRole( s, QLatin1String( roles.b ), 0, &fb );
    if ( roles.c )
      bandC[s] = reader.bandForRole( s, QLatin1String( roles.c ), 0, &fc );
    if ( bandA[s] <= 0 || bandB[s] <= 0 || ( roles.c && bandC[s] <= 0 ) )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "cannot resolve index bands for scene " + std::to_string( s ) );
    anyFallback = anyFallback || fa || fb || fc;
  }
  if ( anyFallback )
    context.logWarning( "Index bands resolved by positional fallback for at least one scene "
                        "(no SICNU_BAND_ROLE metadata); pass 'bands' to pin them." );

  const int width = reader.width();
  const int height = reader.height();
  const size_t tilePixels = static_cast<size_t>( tileSize ) * tileSize;

  context.reportProgress( 0.05, "Creating index series output" );
  GdalDatasetWrapper out;
  QString outErr;
  temporal_output::TemporalOutputGuard guard;
  guard.manage( &out, QString::fromStdString( outputPath ) );
  if ( !out.create( QString::fromStdString( outputPath ), width, height, sceneCount,
                    static_cast<int>( GDT_Float32 ), reader.geoTransform(), reader.projection(),
                    &outErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to create output: " + outErr.toStdString() );
  for ( int b = 1; b <= sceneCount; ++b )
    out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );

  std::vector<float> tileA( tilePixels ), tileB( tilePixels ), tileC, outTile( tilePixels );
  if ( roles.c )
    tileC.resize( tilePixels );

  const int tiles = reader.totalTileCount();
  int tileDone = 0;
  std::uint64_t totalValid = 0;

  for ( int t = 0; t < tiles; ++t )
  {
    int x = 0, y = 0, w = 0, h = 0;
    reader.tileRect( t, &x, &y, &w, &h );
    const size_t pixels = static_cast<size_t>( w ) * h;

    for ( int s = 0; s < sceneCount; ++s )
    {
      if ( !reader.readSceneBandTile( s, bandA[s], t, tileA.data() ) ||
           !reader.readSceneBandTile( s, bandB[s], t, tileB.data() ) ||
           ( roles.c && !reader.readSceneBandTile( s, bandC[s], t, tileC.data() ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "failed reading index inputs for scene " + std::to_string( s ) );

      // NaN propagation matches the single-scene kernels: any non-finite
      // input yields NaN via safeDiv arithmetic.
      if ( !computeIndexTile( index, tileA.data(), tileB.data(),
                              roles.c ? tileC.data() : nullptr, outTile.data(), pixels ) )
        throw RSOperatorError( ErrorCode::ComputationError, "index kernel failed" );

      if ( !out.writeBandWindow( s + 1, x, y, w, h, outTile.data() ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing index series band" );
      for ( size_t i = 0; i < pixels; ++i )
      {
        if ( std::isfinite( outTile[i] ) )
          ++totalValid;
      }
      context.throwIfCancelled();
    }

    ++tileDone;
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                            "Index series tiles " + std::to_string( tileDone ) + "/" +
                                std::to_string( tiles ) );
  }

  for ( int s = 0; s < sceneCount; ++s )
    temporal_output::writeBandAcquisitionMetadata( out, s + 1,
                                                   prepared.collection.scenes().at( s ),
                                                   QString::fromStdString( index ) );
  temporal_output::writeTemporalDatasetMetadata(
    out, prepared.collection, "rs:temporal_index_series",
    QStringLiteral( "index=%1" ).arg( QString::fromStdString( index ) ) );

  QString closeErr;
  if ( !out.closeWithError( &closeErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "output flush failed (disk full?): " + closeErr.toStdString() );
  guard.commit();

  const double totalSamples = static_cast<double>( width ) * height * sceneCount;
  Json::Value result( Json::objectValue );
  result["output"] = outputPath;
  result["index"] = index;
  result["sceneCount"] = sceneCount;
  result["width"] = width;
  result["height"] = height;
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  result["validFraction"] = totalSamples > 0 ? static_cast<double>( totalValid ) / totalSamples : 0.0;
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
    TemporalTileReader::estimateWorkingSetBytes( tileSize, tileSize,
                                                 roles.c ? 4 : 3, 0 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Index series complete" );
  return result;
}

} // namespace sicnu::operators::rs
