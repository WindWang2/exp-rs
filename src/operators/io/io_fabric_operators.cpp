/***************************************************************************
 * io_fabric_operators.cpp — fabric 10.0 operator family (thin adapters).
 ***************************************************************************/
#include "io_fabric_operators.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_schema.h"

#include "geospatial/fabric/prefetch.h"
#include "geospatial/fabric/query_planner.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/util/resource_uri.h"
#include "geospatial/fabric/query_planner.h"
#include "geospatial/raster/raster_writer.h"

#include <json/json.h>

namespace sicnu::operators::io
{

namespace
{

using sicnu::geo::AssetRecord;
using sicnu::geo::CatalogQuery;
using sicnu::geo::CubeChunkShape;
using sicnu::geo::CubeSlice;
using sicnu::geo::FabricIntent;
using sicnu::geo::VirtualCubeGrid;

/// Foundation GeoError → operator ErrorCode (same mapping as the io:*
/// family's mapGeoCode — mirrored because that one is file-local).
ErrorCode mapFabricCode( sicnu::geo::ErrorCode code )
{
  using G = sicnu::geo::ErrorCode;
  switch ( code )
  {
    case G::InvalidArgument: return ErrorCode::InvalidParameter;
    case G::OpenFailed: return ErrorCode::FileNotReadable;
    case G::DriverMissing: return ErrorCode::GdalError;
    case G::MissingCrs: return ErrorCode::InvalidParameter;
    case G::InvalidCrs: return ErrorCode::InvalidParameter;
    case G::TransformFailed: return ErrorCode::GdalError;
    case G::WriteFailed: return ErrorCode::FileNotWritable;
    case G::FidelityLoss: return ErrorCode::InvalidInputData;
    case G::Unsupported: return ErrorCode::GdalError;
    case G::Cancelled: return ErrorCode::Cancelled;
    case G::IoError: return ErrorCode::FileNotWritable;
    case G::NotFound: return ErrorCode::FileNotFound;
    case G::PermissionDenied: return ErrorCode::FileNotReadable;
    case G::UnsupportedFormat: return ErrorCode::InvalidInputData;
    case G::UnsupportedProduct: return ErrorCode::InvalidInputData;
    case G::InvalidMetadata: return ErrorCode::InvalidInputData;
    case G::CorruptData: return ErrorCode::InvalidInputData;
    case G::NetworkError: return ErrorCode::GdalError;
    case G::Timeout: return ErrorCode::ExternalProcessTimeout;
    case G::ResourceExhausted: return ErrorCode::OutOfRange;
    case G::Incompatible: return ErrorCode::InvalidInputData;
  }
  return ErrorCode::Unknown;
}

/// Runs `body`, translating foundation GeoError into RSOperatorError (the
/// io:* family's guard, mirrored here so this file stays self-contained).
Json::Value fabricGuarded( const std::function<Json::Value()> &body )
{
  try
  {
    return body();
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    throw RSOperatorError( mapFabricCode( error.code() ), error.what(), error.details() );
  }
}

FabricIntent intentFrom( const Json::Value &params )
{
  return sicnu::geo::fabricIntentFromJson( params );
}

Json::Value recordsJson( const sicnu::geo::CatalogService::SearchAllResult &result )
{
  Json::Value records( Json::arrayValue );
  for ( const AssetRecord &record : result.records )
  {
    Json::Value entry;
    entry["id"] = record.id;
    entry["displayPath"] = sicnu::geo::ResourceUri::parse( record.path ).display();
    entry["instantUtc"] = record.datetimeUtc;
    entry["collection"] = record.collection;
    entry["cloudCover"] = record.cloudCover;
    entry["hasCloudCover"] = record.hasCloudCover;
    records.append( entry );
  }
  return records;
}

} // namespace

std::string IoCatalogSearchOperator::description() const
{
  return "Query a catalog (local STAC tree, remote STAC API) with one filter vocabulary: "
         "bbox, temporal range, collections, ids, cloud cover, platform, sensors, asset role. "
         "Bounded, cancel-friendly, offline-typed; display paths stay credential-redacted.";
}

Json::Value IoCatalogSearchOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["catalog"] = makeStringParam( "catalog", "Catalog root (local STAC tree or STAC API URL)" );
  params["bounds"] = makeStringParam( "bounds", "Spatial filter [minX,minY,maxX,maxY] (JSON array)" );
  params["temporalStartUtc"] = makeStringParam( "temporalStartUtc", "Inclusive UTC instant bound", "" );
  params["temporalEndUtc"] = makeStringParam( "temporalEndUtc", "Exclusive UTC instant bound", "" );
  params["cloudCoverMax"] = makeNumberParam( "cloudCoverMax", "Cloud cover ceiling (percent)" );
  params["platform"] = makeStringParam( "platform", "Platform equality filter", "" );
  params["assetRole"] = makeStringParam( "assetRole", "Qualifying asset role (default: data)", "" );
  params["limit"] = makeIntegerParam( "limit", "Page size (0 = backend default)" );
  params["maxItems"] = makeIntegerParam( "maxItems", "Hard crawl bound" );
  Json::Value root = makeRootSchema( "Search Catalog", description(), params, Json::Value() );
  root["required"] = makeRequired( { "catalog" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoCatalogSearchOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "cloud-data-fabric-10";
  meta["purpose"] = "One catalog query vocabulary across local and remote STAC";
  meta["limitations"] = "Read-only; bounded crawls; offline refuses remote sources by type.";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "catalog" );
  return meta;
}

Json::Value IoCatalogSearchOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return fabricGuarded( [ & ] {
    sicnu::geo::CatalogServiceOptions options;
    const sicnu::geo::CatalogService service =
      sicnu::geo::openCatalogService( sicnu::operators::params::requireString( params, "catalog" ),
                                      options );
    // The shared intent parser IS the query parser (one vocabulary).
    const sicnu::geo::CatalogQuery query = sicnu::geo::fabricIntentFromJson( params ).query;
    const sicnu::geo::CancelToken cancel;
    const sicnu::geo::CatalogService::SearchAllResult result = service.searchAll( query, cancel );
    Json::Value out;
    out["backend"] = sicnu::geo::catalogBackendKindName( service.info().kind );
    out["records"] = recordsJson( result );
    out["truncatedByCap"] = result.truncatedByCap;
    out["unresolvable"] = static_cast<Json::UInt64>( result.unresolvable );
    out["clientFilteredOut"] = static_cast<Json::UInt64>( result.clientFilteredOut );
    context.reportProgressForced( 1.0, "catalog search complete" );
    return out;
  } );
}

std::string IoCubePlanOperator::description() const
{
  return "Build an inspectable, costed execution plan for a virtual data cube: catalog query "
         "through chunk planning with scene/chunk/bytes/memory/remote-call estimates. Plans are "
         "JSON-stable and executable by io:cube_window / io:cache_prefetch semantics.";
}

Json::Value IoCubePlanOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["catalog"] = makeStringParam( "catalog", "Catalog root (local STAC tree or STAC API URL)" );
  params["sceneBudget"] = makeIntegerParam( "sceneBudget", "Maximum selected scenes" );
  params["executionBudgetBytes"] =
    makeIntegerParam( "executionBudgetBytes", "Total byte budget across one execution" );
  Json::Value root = makeRootSchema( "Plan Data Cube", description(), params, Json::Value() );
  root["required"] = makeRequired( { "catalog" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoCubePlanOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "cloud-data-fabric-10";
  meta["purpose"] = "Explainable bounded plans over cloud EO assets";
  meta["limitations"] = "Plans never execute; memory contract is O(page + selected scenes).";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "cube" );
  return meta;
}

Json::Value IoCubePlanOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return fabricGuarded( [ & ] {
    const sicnu::geo::FabricIntent intent = intentFrom( params );
    const sicnu::geo::CancelToken cancel;
    const sicnu::geo::FabricPlan plan = sicnu::geo::planFabric( intent, {}, cancel );
    context.reportProgressForced( 1.0, "cube plan complete" );
    return plan.toJson();
  } );
}

std::string IoCubeWindowOperator::description() const
{
  return "Execute one virtual-cube window read over the selected scene assets and publish the "
         "result as GeoTIFF (atomic). FirstWins overlap honors declared NoData; the result "
         "carries per-asset provenance. Cross-CRS assets are per-asset failures, never a warp.";
}

Json::Value IoCubeWindowOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["catalog"] = makeStringParam( "catalog", "Catalog root (local STAC tree or STAC API URL)" );
  params["output"] = makeOutputParam( "output", "Output GeoTIFF path" );
  params["bandIndex"] = makeIntegerParam( "bandIndex", "Source band index inside each asset" );
  params["bandRole"] = makeStringParam( "bandRole", "Resolve bands by canonical role instead", "" );
  params["mirrorDirectory"] = makeStringParam( "mirrorDirectory", "Prefer token-matched mirror hits", "" );
  // The window was declared required but never declared as a property —
  // the projection gate (ContractDescriptor) refuses dangling requireds.
  Json::Value window( Json::objectValue );
  window["name"] = "window";
  window["type"] = "object";
  window["description"] = "Window {x,y,w,h} in the shared catalog grid (pixels)";
  params["window"] = window;
  Json::Value root = makeRootSchema( "Read Cube Window", description(), params, Json::Value() );
  root["required"] = makeRequired( { "catalog", "output", "window" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoCubeWindowOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "cloud-data-fabric-10";
  meta["purpose"] = "On-demand window materialization over scene collections";
  meta["limitations"] = "Same-CRS grids only (reproject upstream); memory O(window).";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "cube" );
  return meta;
}

Json::Value IoCubeWindowOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return fabricGuarded( [ & ] {
    using namespace sicnu::operators::params;
    sicnu::geo::FabricIntent intent = intentFrom( params );
    if ( !intent.hasWindow )
      throw sicnu::geo::GeoError( sicnu::geo::ErrorCode::InvalidArgument,
                                  "io:cube_window needs a window{x,y,w,h} object" );
    const sicnu::geo::CancelToken cancel;
    const sicnu::geo::FabricPlan plan = sicnu::geo::planFabric( intent, {}, cancel );

    sicnu::geo::VirtualCubeReadOptions readOptions;
    readOptions.bandIndex = static_cast<int>( getInt( params, "bandIndex", 1 ) );
    readOptions.bandRole = getString( params, "bandRole", "" );
    readOptions.mirrorDirectory = getString( params, "mirrorDirectory", "" );

    sicnu::geo::FabricExecutionReport report;
    const sicnu::geo::VirtualCubeWindowResult window =
      sicnu::geo::executeWindow( plan, readOptions, report, cancel );

    const std::string output = requireString( params, "output" );
    sicnu::geo::atomic_fs::writeFileAtomic( output, [ & ]( const std::string &staged ) {
      // The band declares the window's NoData (uncovered cells are NOT
      // valid data) and keeps Float64 precision (no silent narrowing).
      sicnu::geo::RasterBandSpec band;
      band.dtype = "Float64";
      band.hasNoData = true;
      band.noDataIsNaN = window.noDataIsNaN;
      band.noDataValue = window.noDataIsNaN ? 0.0 : window.gridNoData;
      sicnu::geo::RasterWriter writer =
        sicnu::geo::RasterWriter::create( staged, window.width, window.height, { band },
                                          { "GTiff", { "TILED=YES", "BLOCKXSIZE=64", "BLOCKYSIZE=64" },
                                            true } );
      const VirtualCubeGrid &grid = plan.grid();
      // A derived grid may legally carry no CRS (every probed asset CRS-less):
      // publish without a CRS label rather than throwing after the read.
      if ( grid.crs.valid && !grid.crs.authid.empty() )
        writer.setCrs( sicnu::geo::Crs::fromAuthid( grid.crs.authid ) );
      writer.setGeotransform( { grid.minX, grid.scaleX, 0.0, grid.maxY, 0.0, -grid.scaleY } );
      writer.writeWindow( 1, { 0, 0, window.width, window.height }, window.values.data() );
      writer.finalize();
    } );

    Json::Value out;
    out["output"] = output;
    out["width"] = window.width;
    out["height"] = window.height;
    out["gridNoData"] = window.gridNoData;
    out["provenance"] = window.provenanceJson();
    out["report"] = report.toJson();
    context.reportProgressForced( 1.0, "cube window complete" );
    return out;
  } );
}

std::string IoCachePrefetchOperator::description() const
{
  return "Warm the range cache for a chunk plan: every chunk's source window is read through "
         "/vsirangecache/ so later real reads hit local blocks. Bounded by maxBytes, measured "
         "by the cache telemetry, honest per-chunk outcomes.";
}

Json::Value IoCachePrefetchOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["catalog"] = makeStringParam( "catalog", "Catalog root (local STAC tree or STAC API URL)" );
  params["maxBytes"] = makeIntegerParam( "maxBytes", "Origin bytes this run may pull (0 = plan estimate)" );
  params["chunkWindow"] = makeIntegerParam( "chunkWindow", "Chunks materialized per internal window" );
  params["mirrorDirectory"] = makeStringParam( "mirrorDirectory", "Skip chunks already mirrored", "" );
  Json::Value root = makeRootSchema( "Prefetch Cache", description(), params, Json::Value() );
  root["required"] = makeRequired( { "catalog" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoCachePrefetchOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "cloud-data-fabric-10";
  meta["purpose"] = "Bounded cache warming over chunk plans";
  meta["limitations"] = "Requires the range cache installed; remote chunks only.";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "cache" );
  return meta;
}

Json::Value IoCachePrefetchOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return fabricGuarded( [ & ] {
    using namespace sicnu::operators::params;
    const sicnu::geo::FabricIntent intent = intentFrom( params );
    const sicnu::geo::CancelToken cancel;
    const sicnu::geo::FabricPlan plan = sicnu::geo::planFabric( intent, {}, cancel );

    sicnu::geo::PrefetchOptions options;
    options.maxBytes = params.get( "maxBytes", 0 ).asUInt64();
    options.chunkWindow = static_cast<std::size_t>( getInt( params, "chunkWindow", 64 ) );
    options.mirrorDirectory = getString( params, "mirrorDirectory", "" );

    const sicnu::geo::PrefetchReport report = sicnu::geo::prefetchChunks( plan, options, cancel );
    context.reportProgressForced( 1.0, "prefetch complete" );
    Json::Value out = report.toJson();
    out["plan"] = plan.toJson();
    return out;
  } );
}

} // namespace sicnu::operators::io
