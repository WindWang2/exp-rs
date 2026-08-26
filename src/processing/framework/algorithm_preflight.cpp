// src/processing/framework/algorithm_preflight.cpp
#include "algorithm_preflight.h"

#include "atomic_algorithm_registry.h"
#include "gdal/gdal_dataset_wrapper.h"
#include "resource_estimation.h"
#include "schema_validator.h"
#include "qgsdatasourceresolver.h"

#include <cpl_error.h>
#include <gdal.h>

#include <QString>

#include <filesystem>
#include <string>

namespace sicnu::processing {

namespace {

std::string paramPathValue( const Json::Value &value )
{
  if ( value.isString() )
    return value.asString();
  if ( value.isObject() )
  {
    for ( const char *key : { "path", "source", "uri" } )
      if ( value.isMember( key ) && value[key].isString() )
        return value[key].asString();
  }
  return {};
}

/// Lightweight GDAL probe of a raster file. Returns an object with size/bands/
/// dtype/crs/radiometric-state; empty object when the file cannot be opened.
/// For non-local sources (VSI / OGR connection / remote URI) the filesystem
/// existence check is skipped and GDALOpenEx is used as the source of truth.
Json::Value probeRasterDataset( const std::string &path )
{
  Json::Value info( Json::objectValue );
  info["path"] = path;

  const QString qpath = QString::fromStdString( path );
  const bool requiresLocal = QgsDataSourceResolver::requiresLocalExistenceCheck( qpath );

  if ( requiresLocal )
  {
    std::error_code ec;
    const bool exists = std::filesystem::exists( path, ec ) && !ec;
    info["exists"] = exists;
    if ( !exists )
      return info;

    GdalDatasetWrapper ds;
    if ( !ds.open( qpath ) )
    {
      info["openError"] = ds.lastError().toStdString();
      return info;
    }

    info["width"] = ds.width();
    info["height"] = ds.height();
    info["bandCount"] = ds.bandCount();
    if ( ds.bandCount() >= 1 )
    {
      const int dtype = ds.bandDataType( 1 );
      info["dataType"] = gdalBytesPerSample( dtype ) > 0
                           ? ( gdalBytesPerSample( dtype ) * 8 )
                           : 0; // bit depth
    }
    info["crs"] = ds.projection().toStdString();

    // Dataset-level radiometric state metadata (importers write this).
    if ( void *h = ds.dataset() )
    {
      if ( const char *rs = GDALGetMetadataItem( static_cast<GDALDatasetH>( h ),
                                                 "SICNU_RADIOMETRIC_STATE", nullptr ) )
      {
        info["radiometricState"] = rs;
      }
    }
    return info;
  }

  // Non-local (VSI / connection string / remote URI): probe via GDAL directly.
  GDALAllRegister();
  CPLErrorReset();
  GDALDatasetH hDS = GDALOpenEx( path.c_str(),
                                 GDAL_OF_RASTER | GDAL_OF_READONLY | GDAL_OF_VERBOSE_ERROR,
                                 nullptr, nullptr, nullptr );
  if ( !hDS )
  {
    info["exists"] = false;
    info["open_attempted"] = true;
    const char *msg = CPLGetLastErrorMsg();
    if ( msg && msg[0] )
      info["openError"] = msg;
    CPLErrorReset();
    return info;
  }

  info["exists"] = true;
  info["open_attempted"] = true;
  info["width"] = GDALGetRasterXSize( hDS );
  info["height"] = GDALGetRasterYSize( hDS );
  info["bandCount"] = GDALGetRasterCount( hDS );
  if ( GDALGetRasterCount( hDS ) >= 1 )
  {
    GDALRasterBandH band = GDALGetRasterBand( hDS, 1 );
    if ( band )
    {
      const GDALDataType dt = GDALGetRasterDataType( band );
      const int bytes = GDALGetDataTypeSizeBytes( dt );
      info["dataType"] = bytes > 0 ? bytes * 8 : 0;
    }
  }
  if ( const char *proj = GDALGetProjectionRef( hDS ) )
    info["crs"] = proj;
  else
    info["crs"] = "";
  if ( const char *rs = GDALGetMetadataItem( hDS, "SICNU_RADIOMETRIC_STATE", nullptr ) )
    info["radiometricState"] = rs;

  GDALClose( hDS );
  CPLErrorReset();
  return info;
}

/// Records a warning/blocker with a stable code.
void addIssue( Json::Value &issues, const std::string &code, const std::string &message )
{
  Json::Value item( Json::objectValue );
  item["code"] = code;
  item["message"] = message;
  issues.append( item );
}

} // namespace

Json::Value preflightAlgorithm( const std::string &algorithmId, const Json::Value &params )
{
  const auto adapter = AtomicAlgorithmRegistry::instance().findAdapter( algorithmId );
  if ( !adapter )
  {
    Json::Value result( Json::objectValue );
    result["algorithmId"] = algorithmId;
    result["valid"] = false;
    Json::Value blockers( Json::arrayValue );
    addIssue( blockers, "algorithm_not_registered", "Algorithm not registered: " + algorithmId );
    result["blockers"] = blockers;
    return result;
  }
  return preflightAdapter( *adapter, params );
}

Json::Value preflightAdapter( const AtomicAlgorithmAdapter &adapter, const Json::Value &params )
{
  Json::Value result( Json::objectValue );
  result["algorithmId"] = adapter.algorithmId();

  const AlgorithmDescriptor desc = adapter.descriptor();

  // --- 1. Schema validation (shared validator, no filesystem access) -------
  const ParameterValidationResult validation =
    validateParameters( params, desc, UnknownParameterPolicy::Warn );
  result["schemaValidation"] = validation.toJson();

  // --- 2. Parameter normalization / missing / unknown ----------------------
  Json::Value paramReport( Json::objectValue );
  Json::Value missingArr( Json::arrayValue );
  Json::Value unknownArr( Json::arrayValue );
  for ( const auto &port : desc.inputs )
  {
    if ( port.required && !params.isMember( port.name ) )
      missingArr.append( port.name );
  }
  for ( const auto &member : params.getMemberNames() )
  {
    bool declared = false;
    for ( const auto &port : desc.inputs )
      if ( port.name == member ) { declared = true; break; }
    if ( !declared )
      unknownArr.append( member );
  }
  paramReport["missing"] = missingArr;
  paramReport["unknown"] = unknownArr;
  result["parameters"] = paramReport;

  // --- 3. Dataset probes for raster/vector inputs --------------------------
  Json::Value datasets( Json::objectValue );
  std::vector<std::pair<std::string, Json::Value>> probedRasters; // name, info
  for ( const auto &port : desc.inputs )
  {
    if ( port.type != DataType::Raster && port.type != DataType::Vector )
      continue;
    if ( !params.isMember( port.name ) )
      continue;
    const std::string path = paramPathValue( params[port.name] );
    if ( path.empty() )
      continue;
    if ( port.type == DataType::Raster )
    {
      const Json::Value info = probeRasterDataset( path );
      datasets[port.name] = info;
      if ( info.isMember( "width" ) )
        probedRasters.emplace_back( port.name, info );
    }
    else
    {
      Json::Value info( Json::objectValue );
      info["path"] = path;
      const QString qpath = QString::fromStdString( path );
      if ( QgsDataSourceResolver::requiresLocalExistenceCheck( qpath ) )
      {
        std::error_code ec;
        info["exists"] = std::filesystem::exists( path, ec ) && !ec;
      }
      else
      {
        GDALAllRegister();
        CPLErrorReset();
        GDALDatasetH hDS = GDALOpenEx( path.c_str(),
                                       GDAL_OF_VECTOR | GDAL_OF_READONLY | GDAL_OF_VERBOSE_ERROR,
                                       nullptr, nullptr, nullptr );
        if ( hDS )
        {
          info["exists"] = true;
          info["open_attempted"] = true;
          GDALClose( hDS );
          CPLErrorReset();
        }
        else
        {
          info["exists"] = false;
          info["open_attempted"] = true;
          const char *msg = CPLGetLastErrorMsg();
          if ( msg && msg[0] )
            info["openError"] = msg;
          CPLErrorReset();
        }
      }
      datasets[port.name] = info;
    }
  }
  result["datasets"] = datasets;

  // --- 4. Compatibility checks ---------------------------------------------
  Json::Value compat( Json::objectValue );
  compat["ok"] = true;
  Json::Value compatIssues( Json::arrayValue );

  // Same-grid / CRS consistency across raster inputs (same-grid operators).
  bool sameGridRequested = false;
  for ( const auto &port : desc.inputs )
  {
    if ( port.rsContract.isObject() && port.rsContract.isMember( "gridRelation" )
         && port.rsContract["gridRelation"].isString()
         && port.rsContract["gridRelation"].asString() == "same-grid" )
    {
      sameGridRequested = true;
      break;
    }
  }
  if ( sameGridRequested && probedRasters.size() >= 2 )
  {
    const auto &first = probedRasters.front().second;
    for ( std::size_t i = 1; i < probedRasters.size(); ++i )
    {
      const auto &other = probedRasters[i].second;
      if ( first.isMember( "width" ) && other.isMember( "width" )
           && ( first["width"].asInt() != other["width"].asInt()
                || first["height"].asInt() != other["height"].asInt() ) )
      {
        addIssue( compatIssues, "grid_mismatch",
                  "Raster inputs '" + probedRasters.front().first + "' and '"
                  + probedRasters[i].first + "' have different dimensions; the "
                  "algorithm requires a same-grid relation." );
      }
      const std::string crsA = first["crs"].asString();
      const std::string crsB = other["crs"].asString();
      if ( !crsA.empty() && !crsB.empty() && crsA != crsB )
      {
        addIssue( compatIssues, "crs_mismatch",
                  "Raster inputs '" + probedRasters.front().first + "' and '"
                  + probedRasters[i].first + "' have different CRS (" + crsA
                  + " vs " + crsB + ")." );
      }
    }
  }

  // Band-count contract (x-rs-contract bands.min).
  for ( const auto &port : desc.inputs )
  {
    if ( !port.rsContract.isObject() || !params.isMember( port.name ) )
      continue;
    const auto &contract = port.rsContract;
    if ( !contract.isMember( "bands" ) || !contract["bands"].isObject() )
      continue;
    if ( contract["bands"].isMember( "min" ) && contract["bands"]["min"].isInt() )
    {
      const Json::Value &info = datasets[port.name];
      if ( info.isMember( "bandCount" )
           && info["bandCount"].asInt() < contract["bands"]["min"].asInt() )
      {
        addIssue( compatIssues, "band_count_mismatch",
                  "Input '" + port.name + "' has " + std::to_string( info["bandCount"].asInt() )
                  + " bands but the algorithm requires at least "
                  + std::to_string( contract["bands"]["min"].asInt() ) + "." );
      }
    }
  }

  // Radiometric-state contract (x-rs-contract radiometricState[]).
  for ( const auto &port : desc.inputs )
  {
    if ( !port.rsContract.isObject() || !params.isMember( port.name ) )
      continue;
    const auto &contract = port.rsContract;
    if ( !contract.isMember( "radiometricState" ) || !contract["radiometricState"].isArray() )
      continue;
    const Json::Value &info = datasets[port.name];
    if ( !info.isMember( "radiometricState" ) )
      continue;
    const std::string state = info["radiometricState"].asString();
    bool accepted = false;
    for ( const auto &allowed : contract["radiometricState"] )
      if ( allowed.isString() && allowed.asString() == state ) { accepted = true; break; }
    if ( !accepted )
    {
      addIssue( compatIssues, "radiometric_state_mismatch",
                "Input '" + port.name + "' carries radiometric state '" + state
                + "' which is not accepted by this algorithm." );
    }
  }

  // File existence / open blockers. Non-local sources that were probed via
  // GDALOpenEx and failed produce gdal_open_failed; local files keep
  // input_not_found. The open_attempted flag distinguishes the two.
  for ( auto it = datasets.begin(); it != datasets.end(); ++it )
  {
    const Json::Value &info = *it;
    if ( info.isMember( "exists" ) && !info["exists"].asBool() )
    {
      const bool isGdalProbe = info.isMember( "open_attempted" ) && info["open_attempted"].asBool();
      if ( isGdalProbe )
      {
        addIssue( compatIssues, "gdal_open_failed",
                  "GDAL/OGR could not open datasource: " + info["path"].asString() );
      }
      else
      {
        addIssue( compatIssues, "input_not_found",
                  "Input file does not exist: " + info["path"].asString() );
      }
    }
  }

  compat["issues"] = compatIssues;
  compat["ok"] = compatIssues.empty();
  result["compatibility"] = compat;

  // --- 5. Resource estimate (dynamic preferred, static fallback) -----------
  Json::Value estimate = adapter.estimateExecution( params );
  if ( estimate.isObject() )
  {
    if ( !estimate.empty() )
    {
      if ( !estimate.isMember( "basis" ) )
        estimate["basis"] = "static";
      result["resources"] = estimate;
    }
    else
    {
      Json::Value unknown( Json::objectValue );
      unknown["basis"] = "unknown";
      unknown["estimatedRamBytes"] = 0;
      result["resources"] = unknown;
    }
  }

  // --- 6. Agent metadata summary -------------------------------------------
  Json::Value meta( Json::objectValue );
  const AgentMetadata &am = desc.agentMetadata;
  meta["memoryPolicy"] = am.memoryPolicy;
  meta["largeRasterSafe"] = am.largeRasterSafe;
  meta["supportsCancellation"] = am.supportsCancellation;
  meta["deterministic"] = am.deterministic;
  if ( !am.costClass.empty() )
    meta["costClass"] = am.costClass;
  result["metadata"] = meta;

  // --- 7. Warnings / blockers ----------------------------------------------
  Json::Value warnings( Json::arrayValue );
  for ( const auto &w : validation.warnings )
    warnings.append( w.toJson() );

  // Cancellation advisory: full-scene operators cannot be cancelled mid-run.
  if ( !am.largeRasterSafe )
    addIssue( warnings, "large_raster_unsafe",
              "Memory policy '" + am.memoryPolicy
              + "' may load the full scene; prefer a streaming operator for large rasters." );

  result["warnings"] = warnings;

  Json::Value blockers( Json::arrayValue );
  if ( !validation.ok() )
    addIssue( blockers, "schema_invalid", "Parameter schema validation failed (see schemaValidation)." );
  if ( !compat["ok"].asBool() )
    addIssue( blockers, "compatibility_failed", "Dataset compatibility checks failed (see compatibility)." );
  result["blockers"] = blockers;

  result["valid"] = validation.ok() && compat["ok"].asBool();
  return result;
}

} // namespace sicnu::processing
