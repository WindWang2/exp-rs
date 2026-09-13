// src/operators/rs/rs_temporal_extract_regions_operator.cpp
#include "rs_temporal_extract_regions_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "processing/algorithms/temporal/temporal_region_table.h"
#include "processing/algorithms/temporal/temporal_stream.h"
#include "processing/framework/resource_estimation.h"

#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <vector>

namespace sicnu::operators::rs
{

using namespace params;
using temporal::TemporalTileReader;

namespace
{
constexpr int kDefaultMaxRegions = 100000;
constexpr size_t kDefaultMedianBudgetFloats = 16ULL * 1024ULL * 1024ULL; // 64 MB
constexpr size_t kMaxWindowPixelsPerRegion = 4ULL * 1024ULL * 1024ULL;   // 4 M pixels

/// RAII: removes the partial CSV on failure/cancel unless committed.
struct CsvOutputGuard
{
  QString path;
  bool committed = false;
  ~CsvOutputGuard()
  {
    if ( !committed && !path.isEmpty() )
      QFile::remove( path );
  }
};

Json::Value loadRegionsFileJson( const std::string &path )
{
  QFile file( QString::fromStdString( path ) );
  if ( !file.open( QIODevice::ReadOnly ) )
    throw RSOperatorError( ErrorCode::FileNotFound,
                           "regions_file not readable: " + path );
  const QByteArray bytes = file.readAll();
  Json::CharReaderBuilder builder;
  std::string errs;
  Json::Value root;
  std::istringstream stream( bytes.toStdString() );
  if ( !Json::parseFromStream( builder, stream, &root, &errs ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "regions_file is not valid JSON: " + errs );
  if ( root.isArray() )
    return root;
  if ( root.isObject() && root["regions"].isArray() )
    return root["regions"];
  throw RSOperatorError( ErrorCode::InvalidParameter,
                         "regions_file must hold a regions array (or {\"regions\": [...]})" );
}
} // namespace

std::string RsTemporalExtractRegionsOperator::description() const
{
  return "Extract per-date statistics for MANY points/polygons from a "
         "multi-date collection in one call. Regions arrive inline "
         "(regions: [{\"id\", \"point\": [x, y] | \"polygon\": [[x, y]...]}]) "
         "or as a JSON file. Per region and date the operator reports mean, "
         "min, max, population stddev, exact median and the valid-observation "
         "count, streamed as CSV rows (region_id,date,t_days,mean,min,max,"
         "stddev,median,valid_count). Points map to the pixel under the "
         "point; polygons rasterize inside their bounding box by pixel "
         "centers. Streaming by date keeps memory O(regions), independent of "
         "the raster size outside the region windows.";
}

Json::Value RsTemporalExtractRegionsOperator::schema() const
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
  Json::Value regions = makeStringParam( "regions", "Regions array: [{\"id\", \"point\"|[x,y] or \"polygon\"|[[x,y]...]}]" );
  regions["type"] = "array";
  regions["items"] = Json::Value( Json::objectValue );
  props["regions"] = regions;
  props["regions_file"] = makeStringParam( "regions_file",
                                           "...or a JSON file holding the regions array", "" );
  Json::Value maxRegions = makeIntegerParam( "max_regions", "Maximum accepted regions (guard)", kDefaultMaxRegions );
  setRange( maxRegions, 1, 10000000 );
  props["max_regions"] = maxRegions;
  props["median_budget_mb"] = makeNumberParam(
      "median_budget_mb", "Exact-median scratch budget in MB; beyond it the median column "
                          "reports NaN instead of over-allocating", 64.0 );
  props["output"] = makeOutputParam( "output",
                                     "Region × date CSV table (region_id,date,t_days,mean,min,"
                                     "max,stddev,median,valid_count)", "csv" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Region × date CSV table", "csv" );
  outputs["regionCount"] = makeIntegerParam( "regionCount", "Regions extracted", 0 );
  outputs["pointRegions"] = makeIntegerParam( "pointRegions", "Point regions", 0 );
  outputs["polygonRegions"] = makeIntegerParam( "polygonRegions", "Polygon regions", 0 );
  outputs["rowsWritten"] = makeIntegerParam( "rowsWritten", "CSV rows (region × date with >= 1 valid sample)", 0 );
  outputs["emptyCells"] = makeIntegerParam( "emptyCells", "Region × date cells with no valid sample (not written)", 0 );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Dates in the series", 0 );
  outputs["medianEnabled"] = makeBooleanParam( "medianEnabled", "Whether the median stayed inside its scratch budget", true );
  outputs["timeStart"] = makeStringParam( "timeStart", "First acquisition date (ISO)", "" );
  outputs["timeEnd"] = makeStringParam( "timeEnd", "Last acquisition date (ISO)", "" );
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalExtractRegionsOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "zonal" );
  tags.append( "time-series" );
  meta["tags"] = tags;
  meta["purpose"] = "One-call multi-region temporal extraction (field networks, parcel "
                    "monitors, 100k-parcel dashboards) — the batch form of "
                    "rs:temporal_extract_series";
  meta["prerequisites"] = "Common grid, acquisition times, regions in the collection CRS";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(scenes × totalRegionWindowPixels) I/O + O(scenes × regions) rows";
  meta["workflowHints"] = "Feed the CSV into dashboards or rs:temporal_region_features for "
                          "per-region ML feature tables; keep region ids stable across runs "
                          "to join time series";
  meta["limitations"] = "Rows are written only for region × date cells with >= 1 valid "
                        "sample (emptyCells reports the omitted count); median beyond the "
                        "scratch budget degrades to NaN (medianEnabled=false); regions "
                        "outside the collection grid are refused";
  return meta;
}

Json::Value RsTemporalExtractRegionsOperator::executionEstimate() const
{
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4,
                                                   2 + kTypicalSceneEstimate, 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalExtractRegionsOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  int scenes = kTypicalSceneEstimate;
  if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
    scenes = std::max<int>( 1, static_cast<int>( params["scenes"].size() ) );
  return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4,
                                                   2 + static_cast<std::uint64_t>( scenes ), 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalExtractRegionsOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );
  const int maxRegions = std::clamp( getInt( params, "max_regions", kDefaultMaxRegions ),
                                     1, 10000000 );
  const double medianBudgetMb = getDouble( params, "median_budget_mb", 64.0 );
  const size_t medianBudgetFloats =
      medianBudgetMb > 0.0
          ? static_cast<size_t>( medianBudgetMb * 1024.0 * 1024.0 / sizeof( float ) )
          : 0;

  // Regions: inline array or file.
  Json::Value regionsJson;
  if ( params.isMember( "regions" ) && params["regions"].isArray() )
  {
    if ( params.isMember( "regions_file" ) && params["regions_file"].isString() &&
         !std::string( params["regions_file"].asString() ).empty() )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "provide either 'regions' or 'regions_file', not both" );
    regionsJson = params["regions"];
  }
  else if ( params.isMember( "regions_file" ) && params["regions_file"].isString() )
  {
    regionsJson = loadRegionsFileJson( params["regions_file"].asString() );
  }
  else
  {
    throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                           "provide 'regions' (array) or 'regions_file' (JSON path)" );
  }

  QVector<temporal::RegionRef> regions;
  QString regionsError;
  if ( !temporal::parseRegionsJson( regionsJson, maxRegions, &regions, &regionsError ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, regionsError.toStdString() );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 1 )
    throw RSOperatorError( ErrorCode::InvalidInputData, "the collection is empty" );

  temporal::TemporalStreamOptions streamOptions;
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

  const int regionCount = regions.size();
  std::vector<temporal::RegionGeometry> geometries;
  geometries.reserve( static_cast<size_t>( regionCount ) );
  std::vector<size_t> insideCounts( static_cast<size_t>( regionCount ), 0 );
  int pointRegions = 0;
  int polygonRegions = 0;
  for ( int r = 0; r < regionCount; ++r )
  {
    temporal::RegionGeometry geom;
    QString geomError;
    if ( !temporal::buildRegionGeometry( regions[r], r, reader.geoTransform(),
                                         reader.width(), reader.height(), &geom, &geomError ) )
      throw RSOperatorError( ErrorCode::InvalidInputData, geomError.toStdString() );
    if ( !geom.isPoint &&
         static_cast<size_t>( geom.w ) * static_cast<size_t>( geom.h ) >
             kMaxWindowPixelsPerRegion )
      throw RSOperatorError(
          ErrorCode::InvalidParameter,
          "region '" + regions[r].id.toStdString() + "' window exceeds the " +
              std::to_string( kMaxWindowPixelsPerRegion ) + "-pixel per-region guard; "
              "split the polygon" );
    insideCounts[static_cast<size_t>( r )] = geom.insideCount();
    pointRegions += geom.isPoint ? 1 : 0;
    polygonRegions += geom.isPoint ? 0 : 1;
    geometries.push_back( std::move( geom ) );
  }

  temporal::RegionDateReducer reducer( regionCount, medianBudgetFloats, insideCounts );

  CsvOutputGuard csvGuard;
  csvGuard.path = QString::fromStdString( outputPath );
  QFile outFile( csvGuard.path );
  if ( !outFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "cannot open output CSV: " + outputPath );
  QTextStream ts( &outFile );
  ts.setCodec( "UTF-8" );
  ts << "region_id,date,t_days,mean,min,max,stddev,median,valid_count\n";

  std::vector<float> window;
  window.reserve( 1024 );
  std::uint64_t rowsWritten = 0;
  std::uint64_t emptyCells = 0;

  for ( int s = 0; s < sceneCount; ++s )
  {
    const double tDay = reader.sceneDayOffset( s );
    const QString dateIso = prepared.collection.scenes().at( s ).time.dateString();
    reducer.beginDate();
    for ( int r = 0; r < regionCount; ++r )
    {
      const auto &geom = geometries[static_cast<size_t>( r )];
      const int samples = static_cast<int>( geom.insideCount() );
      if ( static_cast<int>( window.size() ) < samples )
        window.resize( static_cast<size_t>( samples ) );
      if ( !reader.readSceneBandWindow( s, analysisBands[s], geom.xOff, geom.yOff,
                                        geom.w, geom.h, window.data() ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "failed reading scene " +
                                   prepared.collection.scenes().at( s ).path.toStdString() +
                                   " for region '" + regions[r].id.toStdString() + "'" );
      if ( geom.isPoint )
      {
        reducer.addSample( r, window[0] );
      }
      else
      {
        for ( const int off : geom.insideOffsets )
          reducer.addSample( r, window[static_cast<size_t>( off )] );
      }
      if ( ( r & 1023 ) == 1023 )
        context.throwIfCancelled(); // bounded checkpoint inside large sets
    }
    reducer.endDate();
    context.throwIfCancelled();

    const float kNanF = std::numeric_limits<float>::quiet_NaN();
    for ( int r = 0; r < regionCount; ++r )
    {
      const auto &st = reducer.stats( r );
      if ( st.validCount <= 0 )
      {
        ++emptyCells;
        continue;
      }
      const auto &id = regions[r].id;
      ts << id << ',' << dateIso << ',' << QString::number( tDay, 'f', 3 ) << ','
         << QString::number( st.mean, 'g', 9 ) << ','
         << QString::number( st.min, 'g', 9 ) << ','
         << QString::number( st.max, 'g', 9 ) << ','
         << QString::number( st.stddev, 'g', 9 ) << ','
         << QString::number( st.median, 'g', 9 ) << ',' << st.validCount << '\n';
      ++rowsWritten;
    }
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( s + 1 ) / sceneCount ),
                            "Region extraction date " + std::to_string( s + 1 ) + "/" +
                                std::to_string( sceneCount ) );
  }
  ts.flush();
  if ( ts.status() != QTextStream::Ok )
    throw RSOperatorError( ErrorCode::FileNotWritable, "CSV write failed (disk full?)" );
  outFile.close();
  csvGuard.committed = true;

  Json::Value result( Json::objectValue );
  result["output"] = outputPath;
  result["regionCount"] = regionCount;
  result["pointRegions"] = pointRegions;
  result["polygonRegions"] = polygonRegions;
  result["rowsWritten"] = Json::Value::UInt64( rowsWritten );
  result["emptyCells"] = Json::Value::UInt64( emptyCells );
  result["sceneCount"] = sceneCount;
  result["medianEnabled"] = reducer.medianEnabled();
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  context.reportProgress( 1.0, "Temporal region extraction complete" );
  return result;
}

} // namespace sicnu::operators::rs
