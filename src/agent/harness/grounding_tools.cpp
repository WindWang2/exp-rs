// src/agent/harness/grounding_tools.cpp
#include "grounding_tools.h"

#include "agent/tool_catalog/agent_tool_catalog.h"
#include "agent/workspace_state.h"
#include "contracts/spatial_contracts.h"
#include "entity_resolver.h"
#include "harness_error.h"

#include <QCryptographicHash>

#include <algorithm>
#include <cctype>

namespace sicnu::agent::harness {

using namespace sicnu::agent::spatial_tools;

namespace {

Json::Value objectSchema( Json::Value properties, Json::Value required )
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  schema["properties"] = std::move( properties );
  if ( required.isArray() && !required.empty() )
    schema["required"] = std::move( required );
  return schema;
}

SpatialToolPtr findTool( const std::string &name )
{
  return SpatialToolRegistry::instance().find( name ).value_or( nullptr );
}

bool roleOrProductSuggestsSar( const Json::Value &inspect )
{
  static const char *kSarRoles[] = { "hh", "hv", "vv", "vh", "co-pol", "cross-pol" };
  if ( inspect.isMember( "bands" ) && inspect["bands"].isArray() )
  {
    for ( const auto &band : inspect["bands"] )
    {
      if ( !band.isMember( "role" ) )
        continue;
      const std::string role = band["role"].asString();
      std::string lowered = role;
      std::transform( lowered.begin(), lowered.end(), lowered.begin(),
                      []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
      for ( const char *sarRole : kSarRoles )
      {
        if ( lowered == sarRole )
          return true;
      }
    }
  }
  static const char *kSarProductKeys[] = { "SICNU_PRODUCT_TYPE", "SICNU_SPACECRAFT" };
  for ( const char *key : kSarProductKeys )
  {
    if ( !inspect.isMember( key ) )
      continue;
    std::string value = inspect[key].asString();
    std::transform( value.begin(), value.end(), value.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    if ( value.find( "sentinel-1" ) != std::string::npos ||
         value.find( "sentinel1" ) != std::string::npos ||
         value.find( "sar" ) != std::string::npos ||
         value.find( "terrasar" ) != std::string::npos ||
         value.find( "radarsat" ) != std::string::npos ||
         value.find( "alos" ) != std::string::npos ||
         value.find( "palsar" ) != std::string::npos )
      return true;
  }
  if ( inspect.isMember( "driver" ) )
  {
    std::string driver = inspect["driver"].asString();
    std::transform( driver.begin(), driver.end(), driver.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    static const char *kSarDrivers[] = { "sentinel1", "ceos_sar", "terrasarx", "radarsat",
                                         "cosmo_skymed", "safe" };
    for ( const char *sarDriver : kSarDrivers )
    {
      if ( driver == sarDriver )
        return true;
    }
  }
  return false;
}

bool roleOrProductSuggestsDem( const Json::Value &inspect )
{
  if ( inspect.isMember( "bands" ) && inspect["bands"].isArray() )
  {
    for ( const auto &band : inspect["bands"] )
    {
      if ( !band.isMember( "role" ) )
        continue;
      std::string role = band["role"].asString();
      std::transform( role.begin(), role.end(), role.begin(),
                      []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
      if ( role == "dem" || role == "elevation" || role == "height" )
        return true;
    }
  }
  if ( inspect.isMember( "SICNU_PRODUCT_TYPE" ) )
  {
    std::string value = inspect["SICNU_PRODUCT_TYPE"].asString();
    std::transform( value.begin(), value.end(), value.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return value.find( "dem" ) != std::string::npos || value.find( "elevation" ) != std::string::npos;
  }
  return false;
}

bool suggestsOptical( const Json::Value &inspect )
{
  if ( inspect.isMember( "bands" ) && inspect["bands"].isArray() )
  {
    for ( const auto &band : inspect["bands"] )
    {
      if ( band.isMember( "wavelength" ) )
        return true;
      if ( !band.isMember( "role" ) )
        continue;
      std::string role = band["role"].asString();
      std::transform( role.begin(), role.end(), role.begin(),
                      []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
      static const char *kOpticalRoles[] = { "red", "green", "blue", "nir", "swir",
                                             "rededge", "panchromatic", "cirrus", "coastal" };
      for ( const char *opticalRole : kOpticalRoles )
      {
        if ( role == opticalRole )
          return true;
      }
    }
  }
  return false;
}

class UnderstandTool final : public SpatialTool
{
  public:
    std::string name() const override { return "spatial:understand"; }
    std::string displayName() const override { return "Dataset Understanding"; }
    std::string description() const override
    {
      return "Answers 'what is this dataset' with a typed DatasetUnderstanding "
             "document — never guess: pass any resolvable reference (stable "
             "asset-N id, governed asset UUID, path, or unambiguous display "
             "name). Returns source kind, size, pixel size, extent, CRS, band "
             "roles, radiometric state, modality (sar/optical/dem/unknown), and "
             "the stable entity ids for later calls. Read-only.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "grounding", "dataset", "modality", "understanding" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value asset( Json::objectValue );
      asset["type"] = "string";
      asset["description"] = "Dataset reference: asset-N id, asset UUID, path, or display name.";
      props["asset"] = asset;
      Json::Value stats( Json::objectValue );
      stats["type"] = "boolean";
      stats["description"] = "Include decimated per-band statistics (slower).";
      props["stats"] = stats;
      Json::Value required( Json::arrayValue );
      required.append( "asset" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["dataset_understanding"] = Json::Value( Json::objectValue );
      props["entity"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isObject() || !input.isMember( "asset" ) || !input["asset"].isString() )
        return SpatialToolResult::failure( "missing string parameter 'asset'",
                                           error_codes::kInvalidParameter, "validation" );

      HarnessError error;
      const std::optional<ResolvedDataset> resolved =
        resolveDatasetRef( QString::fromStdString( input["asset"].asString() ), &error );
      if ( !resolved )
        return SpatialToolResult::failure( error.summary, error.code, "validation" );

      Json::Value inspectInput;
      inspectInput["path"] = resolved->path.toStdString();
      if ( input.isMember( "stats" ) && input["stats"].isBool() )
        inspectInput["stats"] = input["stats"];

      // Raster first (the dominant RS case); fall back to vector. Whichever
      // inspection succeeds defines the source kind — no guessing from names.
      Json::Value inspectOutput;
      bool isRaster = true;
      if ( const SpatialToolPtr raster = findTool( "spatial:raster_inspect" ) )
      {
        const SpatialToolResult result = raster->execute( inspectInput );
        if ( result.success )
          inspectOutput = result.output;
      }
      if ( inspectOutput.empty() )
      {
        isRaster = false;
        if ( const SpatialToolPtr vector = findTool( "spatial:vector_inspect" ) )
        {
          const SpatialToolResult result = vector->execute( inspectInput );
          if ( result.success )
            inspectOutput = result.output;
        }
      }
      if ( inspectOutput.empty() )
      {
        return SpatialToolResult::failure(
          "File exists but opens neither as a raster nor as a vector dataset: "
          + resolved->path.toStdString(),
          error_codes::kDatasetNotFound, "io" );
      }

      Json::Value understanding = isRaster
        ? sicnu::agent::contracts::datasetUnderstandingFromRasterInspect( inspectOutput )
        : sicnu::agent::contracts::datasetUnderstandingFromVectorInspect( inspectOutput );
      if ( isRaster )
        understanding["modality"] = inferModality( inspectOutput );
      understanding["entity"] = resolved->toJson();

      Json::Value out( Json::objectValue );
      out["dataset_understanding"] = understanding;
      return SpatialToolResult::ok( std::move( out ) );
    }
};

QString contextRevision( const Json::Value &state )
{
  const std::string serialized = Json::writeString( Json::StreamWriterBuilder(), state );
  const QByteArray digest = QCryptographicHash::hash(
    QByteArray::fromStdString( serialized ), QCryptographicHash::Sha256 );
  return QString::fromLatin1( digest.left( 12 ).toHex() );
}

class ContextTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:context"; }
    std::string displayName() const override { return "Typed Spatial Context"; }
    std::string description() const override
    {
      return "Compact typed spatial context: current project, datasets with "
             "stable asset-N ids, visible/selected layers, extent, CRS, "
             "running tasks, recent outputs, and workflow runs. Pass "
             "if_revision from your previous call to receive a cheap "
             "{unchanged:true} when nothing moved. The revision changes on "
             "project switch, layer change, dataset replacement, task, or "
             "workflow completion — always re-read after those events.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "context", "workspace", "revision" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value rev( Json::objectValue );
      rev["type"] = "string";
      rev["description"] = "Revision returned by your last context read.";
      props["if_revision"] = rev;
      Json::Value limit( Json::objectValue );
      limit["type"] = "integer";
      limit["description"] = "recent_outputs bound (default 8, max 50).";
      props["recent_outputs_limit"] = limit;
      return objectSchema( std::move( props ), Json::Value() );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["revision"] = Json::Value( Json::objectValue );
      props["unchanged"] = Json::Value( Json::objectValue );
      props["context"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      int recentLimit = 8;
      if ( input.isMember( "recent_outputs_limit" ) && input["recent_outputs_limit"].isNumeric() )
        recentLimit = std::clamp( input["recent_outputs_limit"].asInt(), 1, 50 );

      const Json::Value state = sicnu::agent::buildWorkspaceState(
        AgentServices::instance().dataManager(),
        AgentServices::instance().mapCanvas(),
        {},
        recentLimit );

      const QString revision = contextRevision( state );
      Json::Value out( Json::objectValue );
      out["revision"] = revision.toStdString();
      if ( input.isMember( "if_revision" ) && input["if_revision"].isString() )
      {
        const std::string seen = input["if_revision"].asString();
        if ( !seen.empty() && seen == revision.toStdString() )
        {
          out["unchanged"] = true;
          return SpatialToolResult::ok( std::move( out ) );
        }
      }
      out["unchanged"] = false;
      out["context"] = state;
      return SpatialToolResult::ok( std::move( out ) );
    }
};

} // namespace

std::string inferModality( const Json::Value &rasterInspect )
{
  if ( roleOrProductSuggestsSar( rasterInspect ) )
    return "sar";
  if ( roleOrProductSuggestsDem( rasterInspect ) )
    return "dem";
  if ( suggestsOptical( rasterInspect ) )
    return "optical";
  return "unknown";
}

void registerGroundingTools()
{
  static bool registered = false;
  if ( registered )
    return;
  registered = true;
  auto &registry = SpatialToolRegistry::instance();
  registry.registerTool( std::make_shared<UnderstandTool>() );
  registry.registerTool( std::make_shared<ContextTool>() );
}

} // namespace sicnu::agent::harness
