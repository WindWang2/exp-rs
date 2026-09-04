// src/agent/spatial_tools/capability_tools.cpp
#include "capability_tools.h"

#include "../contracts/spatial_contracts.h"
#include "../tool_catalog/agent_tool.h"
#include "../tool_catalog/agent_tool_catalog.h"
#include "../workspace_state.h"
#include "data/asset_types.h"
#include "data/data_manager.h"
#include "operators/framework/model_catalog.h"

#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sicnu::agent::spatial_tools {

using namespace sicnu::agent::contracts;
using sicnu::agent::tool_catalog::AgentTool;
using sicnu::agent::tool_catalog::AgentToolCatalog;
using sicnu::agent::tool_catalog::SearchQuery;

namespace {

// --- workspace context detection -------------------------------------------

struct WorkspaceContext
{
  std::vector<std::string> bandRoles;
  std::string crs;
  double resolutionMeters = -1.0;
  long long pixelCount = 0;
  bool gpuAvailable = false;
};

/// Token set from a free-text query, lowercased, punctuation-dropped.
std::set<std::string> tokenize( const std::string &text )
{
  std::set<std::string> tokens;
  std::string current;
  for ( const char c : text )
  {
    if ( std::isalnum( static_cast<unsigned char>( c ) ) )
    {
      current.push_back( static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) ) );
    }
    else if ( !current.empty() )
    {
      tokens.insert( current );
      current.clear();
    }
  }
  if ( !current.empty() )
    tokens.insert( current );
  return tokens;
}

WorkspaceContext detectContext()
{
  WorkspaceContext ctx;
  if ( QgsMapCanvas *canvas = sicnu::agent::AgentServices::instance().mapCanvas() )
  {
    if ( QgsMapLayer *layer = canvas->currentLayer() )
      ctx.crs = layer->crs().authid().toStdString();
  }
  if ( sicnu::data::DataManager *dataManager = sicnu::agent::AgentServices::instance().dataManager() )
  {
    const auto assets = dataManager->assets();
    // Prefer the asset matching the active layer name; else the first raster.
    QgsMapLayer *active = nullptr;
    if ( QgsMapCanvas *canvas = sicnu::agent::AgentServices::instance().mapCanvas() )
      active = canvas->currentLayer();
    const sicnu::data::AssetSnapshot *chosen = nullptr;
    for ( const auto &asset : assets )
    {
      if ( active && asset.displayName() == active->name() )
      {
        chosen = &asset;
        break;
      }
    }
    if ( !chosen )
    {
      for ( const auto &asset : assets )
      {
        if ( asset.kind() == sicnu::data::AssetKind::Raster )
        {
          chosen = &asset;
          break;
        }
      }
    }
    if ( chosen )
    {
      const auto &structure = chosen->structure();
      if ( const auto *raster = std::get_if<sicnu::data::RasterStructure>( &structure ) )
      {
        ctx.pixelCount = static_cast<long long>( raster->width ) * raster->height;
        for ( const auto &band : raster->bands )
        {
          if ( band.role != sicnu::data::BandRole::Unknown )
            ctx.bandRoles.push_back( sicnu::data::bandRoleToString( band.role ).toLower().toStdString() );
        }
        if ( raster->extent.valid && raster->width > 0 )
        {
          const double widthMeters = raster->extent.maximumX - raster->extent.minimumX;
          if ( widthMeters > 0 )
            ctx.resolutionMeters = widthMeters / raster->width;
        }
        if ( ctx.crs.empty() )
        {
          const QString wkt = raster->crsWkt;
          if ( !wkt.isEmpty() )
          {
            // authid extraction without a heavy CRS construction when possible.
            const int start = wkt.indexOf( QLatin1String( "EPSG" ) );
            if ( start >= 0 )
            {
              QString id = wkt.mid( start, 15 );
              id = id.section( ',', 0, 0 );
              id.remove( '"' );
              id.remove( ']' );
              ctx.crs = id.toStdString();
            }
          }
        }
      }
    }
  }
  return ctx;
}

// --- scoring ----------------------------------------------------------------

/// Band-role affinity in [0,1]: |declared ∩ context| / |context|; 0.5 neutral
/// when the tool declares no band requirements.
double bandRoleAffinity( const AgentTool &tool, const WorkspaceContext &ctx )
{
  if ( ctx.bandRoles.empty() )
    return 0.5;
  std::set<std::string> declared;
  for ( const auto &port : tool.inputs )
  {
    if ( !port.rsContract.isObject() )
      continue;
    // Operators declare band requirements either as "bands" (ordered roles)
    // or "band_roles" inside the x-rs-contract object; most current operators
    // declare neither and stay at the neutral 0.5 baseline.
    for ( const char *key : { "bands", "band_roles" } )
    {
      if ( port.rsContract.isMember( key ) && port.rsContract[key].isArray() )
      {
        for ( const auto &band : port.rsContract[key] )
        {
          if ( band.isString() )
            declared.insert( band.asString() );
        }
      }
    }
  }
  if ( declared.empty() )
    return 0.5;
  size_t hits = 0;
  for ( const auto &role : ctx.bandRoles )
  {
    if ( declared.count( role ) )
      ++hits;
  }
  return static_cast<double>( hits ) / ctx.bandRoles.size();
}

} // namespace

// ---------------------------------------------------------------------------
// spatial:search_capabilities
// ---------------------------------------------------------------------------

namespace {

class SearchCapabilitiesTool final : public SpatialTool
{
  public:
    std::string name() const override { return "spatial:search_capabilities"; }
    std::string displayName() const override { return "Search capabilities"; }
    std::string description() const override
    {
      return "Ranked capability discovery over the whole tool/algorithm catalog, fused with the "
             "current workspace context (active dataset band roles, CRS, resolution, GPU). "
             "Input: {query, task_family?, band_roles?, gpu?, deterministic?, large_raster_safe?, "
             "limit?, offset?}. Returns CapabilityCandidate summaries — compatibility score, "
             "reasons, warnings, estimated cost — never full schemas. Pull the schema for the "
             "chosen candidate only via get_tool_schema.";
    }
    std::vector<std::string> tags() const override
    {
      return { "spatial", "capability", "ranking", "discovery", "planning" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      const auto addString = [ &props ]( const char *name, const char *desc ) {
        Json::Value p( Json::objectValue );
        p["type"] = "string";
        p["description"] = desc;
        props[name] = p;
      };
      const auto addBool = [ &props ]( const char *name, const char *desc ) {
        Json::Value p( Json::objectValue );
        p["type"] = "boolean";
        p["description"] = desc;
        props[name] = p;
      };
      addString( "query", "Free-text task description, e.g. 'NDVI of active scene' or 'building segmentation'" );
      addString( "task_family", "Capability facet: task family filter (e.g. spectral_index)" );
      addString( "band_roles", "Comma-separated band roles the tool must accept (e.g. 'red,nir')" );
      addBool( "gpu", "Filter/penalize on GPU availability (defaults to workspace detection; false when unknown)" );
      addBool( "deterministic", "Deterministic algorithms only" );
      addBool( "large_raster_safe", "Large-raster-safe algorithms only" );
      Json::Value limit( Json::objectValue );
      limit["type"] = "integer";
      limit["description"] = "Page size (default 10, max 30)";
      props["limit"] = limit;
      Json::Value offset( Json::objectValue );
      offset["type"] = "integer";
      props["offset"] = offset;
      schema["properties"] = props;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["candidates"] = Json::Value( Json::objectValue );
      schema["properties"]["total"] = Json::Value( Json::objectValue );
      schema["properties"]["next_offset"] = Json::Value( Json::objectValue );
      schema["properties"]["context"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string queryText =
        input.isMember( "query" ) && input["query"].isString() ? input["query"].asString() : "";
      if ( queryText.empty() && !input.isMember( "task_family" ) && !input.isMember( "band_roles" ) )
        return SpatialToolResult::failure(
          "Provide at least one of: query, task_family, band_roles", "INVALID_PARAMETER",
          "validation" );

      SearchQuery facets;
      facets.text = queryText;
      if ( input.isMember( "task_family" ) && input["task_family"].isString() )
        facets.taskFamily = input["task_family"].asString();
      if ( input.isMember( "band_roles" ) && input["band_roles"].isString() )
        facets.bandRoles = input["band_roles"].asString();
      if ( input.isMember( "deterministic" ) && input["deterministic"].isBool() )
        facets.deterministic = input["deterministic"].asBool();
      if ( input.isMember( "large_raster_safe" ) && input["large_raster_safe"].isBool() )
        facets.largeRasterSafeOnly = input["large_raster_safe"].asBool();

      WorkspaceContext ctx = detectContext();
      if ( input.isMember( "gpu" ) && input["gpu"].isBool() )
        ctx.gpuAvailable = input["gpu"].asBool();

      const std::vector<AgentTool> matches = AgentToolCatalog::instance().searchTools( facets );
      const std::set<std::string> queryTokens = tokenize( queryText );

      struct Scored
      {
        Json::Value candidate;
        double score = 0.0;
      };
      std::vector<Scored> scored;
      scored.reserve( matches.size() );
      for ( const auto &tool : matches )
      {
        // Only executable algorithm/catalog capabilities participate in
        // ranking (interaction/view tools are not analysis candidates).
        const bool isAlgorithm = tool.category == sicnu::agent::tool_catalog::ToolCategory::Processing;
        const bool isSpatialAnalysis = tool.name.rfind( "spatial:", 0 ) == 0 ||
                                       tool.name.rfind( "temporal:", 0 ) == 0;
        if ( !isAlgorithm && !isSpatialAnalysis )
          continue;

        double score = 0.0;
        std::vector<std::string> reasons;
        std::vector<std::string> warnings;

        // 1. Task-text relevance (purpose + tags + taskFamily + name).
        if ( !queryTokens.empty() )
        {
          std::string haystack = tool.name + " " + tool.displayName + " " + tool.description;
          haystack += " " + tool.agentMetadata.purpose;
          for ( const auto &tag : tool.tags )
            haystack += " " + tag;
          if ( !tool.agentMetadata.taskFamily.empty() )
            haystack += " " + tool.agentMetadata.taskFamily;
          const std::set<std::string> haystackTokens = tokenize( haystack );
          size_t hits = 0;
          for ( const auto &token : queryTokens )
          {
            if ( haystackTokens.count( token ) )
              ++hits;
          }
          const double relevance = static_cast<double>( hits ) / queryTokens.size();
          score += 0.45 * relevance;
          if ( relevance > 0.2 )
            reasons.push_back( "task text matches (" + std::to_string( hits ) + "/" +
                               std::to_string( queryTokens.size() ) + " terms)" );
        }

        // 2. Band-role affinity with the workspace dataset.
        const double affinity = bandRoleAffinity( tool, ctx );
        score += 0.25 * affinity;
        if ( affinity >= 0.99 && !ctx.bandRoles.empty() )
          reasons.push_back( "all active dataset band roles supported" );

        // 3. GPU fit.
        if ( tool.agentMetadata.gpuAccelerated )
        {
          if ( ctx.gpuAvailable )
          {
            score += 0.05;
            reasons.push_back( "GPU acceleration available" );
          }
          else
          {
            score -= 0.30;
            warnings.push_back( "requires GPU but no GPU is available in this workspace" );
          }
        }

        // 4. Large-raster safety.
        const bool largeRaster = ctx.pixelCount > 50LL * 1000 * 1000;
        if ( largeRaster )
        {
          if ( tool.agentMetadata.largeRasterSafe )
          {
            score += 0.05;
            reasons.push_back( "large-raster safe (streaming)" );
          }
          else
          {
            warnings.push_back( "not marked large-raster safe; workspace raster is large" );
          }
        }

        // 5. Determinism preference.
        if ( tool.agentMetadata.deterministic )
        {
          score += 0.02;
          reasons.push_back( "deterministic" );
        }

        Json::Value cost = makeCostEstimate(
          tool.agentMetadata.costClass.empty() ? "unknown" : tool.agentMetadata.costClass,
          static_cast<Json::Int>( 0 ), static_cast<Json::Int>( 0 ),
          tool.agentMetadata.gpuAccelerated, tool.agentMetadata.largeRasterSafe );
        if ( tool.agentMetadata.execution.isObject() &&
             tool.agentMetadata.execution.isMember( "estimatedRamBytes" ) &&
             tool.agentMetadata.execution["estimatedRamBytes"].isNumeric() )
        {
          cost["estimated_ram_mb"] =
            static_cast<Json::Int>( tool.agentMetadata.execution["estimatedRamBytes"].asInt64() /
                                    ( 1024 * 1024 ) );
        }
        if ( !tool.agentMetadata.memoryPolicy.empty() )
          cost["memory_policy"] = tool.agentMetadata.memoryPolicy;

        Json::Value reasonsJson( Json::arrayValue );
        for ( const auto &r : reasons )
          reasonsJson.append( r );
        Json::Value warningsJson( Json::arrayValue );
        for ( const auto &w : warnings )
          warningsJson.append( w );

        scored.push_back( Scored{ makeCapabilityCandidate( tool.name, "algorithm", score,
                                                           reasonsJson, warningsJson, cost ),
                                  score } );
      }

      std::sort( scored.begin(), scored.end(),
                 []( const Scored &a, const Scored &b ) { return a.score > b.score; } );

      Json::Value all( Json::arrayValue );
      for ( auto &entry : scored )
        all.append( entry.candidate );

      int limit = input.isMember( "limit" ) && input["limit"].isInt() ? input["limit"].asInt() : 10;
      int offset = input.isMember( "offset" ) && input["offset"].isInt() ? input["offset"].asInt() : 0;
      Json::Value page = paginate( all, offset, std::clamp( limit, 1, 30 ) );

      Json::Value out( Json::objectValue );
      out["candidates"] = page["items"];
      out["total"] = page["total"];
      out["offset"] = page["offset"];
      out["next_offset"] = page["next_offset"];

      Json::Value context( Json::objectValue );
      Json::Value roles( Json::arrayValue );
      for ( const auto &role : ctx.bandRoles )
        roles.append( role );
      context["band_roles"] = roles;
      context["crs"] = ctx.crs;
      context["resolution_meters"] = ctx.resolutionMeters;
      context["gpu_available"] = ctx.gpuAvailable;
      out["context"] = context;
      return SpatialToolResult::ok( out );
    }
};

} // namespace

// ---------------------------------------------------------------------------
// spatial:select_model
// ---------------------------------------------------------------------------

namespace {

class SelectModelTool final : public SpatialTool
{
  public:
    std::string name() const override { return "spatial:select_model"; }
    std::string displayName() const override { return "Select model"; }
    std::string description() const override
    {
      return "Automatic model selection: express the TASK and DATA contract, never a hardcoded "
             "model name. Input: {task, band_roles?, sensor?, resolution_m?, gpu_available?, "
             "max_vram_mb?} → ranked ready ModelCandidates with match reasons, readiness, and "
             "artifact resolution hints. The top candidate's name feeds rs:infer's model "
             "parameter.";
    }
    std::vector<std::string> tags() const override
    {
      return { "spatial", "model", "selection", "ranking", "ml" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value task( Json::objectValue );
      task["type"] = "string";
      task["description"] = "Task family, e.g. 'segmentation', 'detection', 'classification'";
      props["task"] = task;
      Json::Value roles( Json::objectValue );
      roles["type"] = "string";
      roles["description"] = "Comma-separated band roles (e.g. 'red,nir')";
      props["band_roles"] = roles;
      Json::Value sensor( Json::objectValue );
      sensor["type"] = "string";
      sensor["description"] = "Sensor name (e.g. 'Sentinel-2')";
      props["sensor"] = sensor;
      Json::Value res( Json::objectValue );
      res["type"] = "number";
      res["description"] = "Spatial resolution in meters";
      props["resolution_m"] = res;
      Json::Value gpu( Json::objectValue );
      gpu["type"] = "boolean";
      gpu["description"] = "GPU availability (default false)";
      props["gpu_available"] = gpu;
      Json::Value vram( Json::objectValue );
      vram["type"] = "integer";
      vram["description"] = "Max VRAM budget in MiB";
      props["max_vram_mb"] = vram;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "task" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["candidates"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const std::string task = input.isMember( "task" ) && input["task"].isString()
                                 ? input["task"].asString()
                                 : std::string();
      if ( task.empty() )
        return SpatialToolResult::failure( "Missing required parameter: task", "INVALID_PARAMETER",
                                           "validation" );

      sicnu::operators::ModelQueryCriteria criteria;
      criteria.task = task;
      if ( input.isMember( "sensor" ) && input["sensor"].isString() )
        criteria.sensor = input["sensor"].asString();
      if ( input.isMember( "band_roles" ) && input["band_roles"].isString() )
      {
        QString list = QString::fromStdString( input["band_roles"].asString() );
        const QStringList parts = list.split( ',', Qt::SkipEmptyParts );
        for ( const QString &part : parts )
          criteria.bandRoles.push_back( part.trimmed().toStdString() );
      }
      if ( input.isMember( "resolution_m" ) && input["resolution_m"].isNumeric() )
        criteria.resolutionMeters = input["resolution_m"].asDouble();
      if ( input.isMember( "gpu_available" ) && input["gpu_available"].isBool() )
        criteria.gpuAvailable = input["gpu_available"].asBool();
      if ( input.isMember( "max_vram_mb" ) && input["max_vram_mb"].isInt() )
        criteria.maxVramMb = input["max_vram_mb"].asInt();

      const std::vector<sicnu::operators::ModelCandidate> ranked =
        sicnu::operators::ModelCatalog::instance().rankModels( criteria );

      Json::Value candidates( Json::arrayValue );
      for ( const auto &candidate : ranked )
      {
        Json::Value reasons( Json::arrayValue );
        for ( const auto &reason : candidate.matchReasons )
          reasons.append( reason );
        Json::Value warnings( Json::arrayValue );
        for ( const auto &problem : candidate.incompatibilityReasons )
          warnings.append( problem );

        const sicnu::operators::ModelInfo &model = candidate.model;
        Json::Value cost = makeCostEstimate( "model", model.runtime.estimatedRamMb,
                                             static_cast<Json::Int>( 0 ), model.gpu,
                                             model.supportsTiling );
        if ( model.estimatedVramMb > 0 )
          cost["estimated_vram_mb"] = model.estimatedVramMb;
        if ( !model.readinessReason.empty() )
          warnings.push_back( "readiness: " + model.readinessReason );

        Json::Value entry = makeCapabilityCandidate( model.name, "model", candidate.score, reasons,
                                                     warnings, cost );
        entry["compatible"] = candidate.compatible;
        entry["readiness"] = sicnu::operators::modelReadinessName( model.readiness );
        entry["task"] = model.task;
        if ( !model.resolvedArtifactPath.empty() )
          entry["artifact_path"] = model.resolvedArtifactPath;
        candidates.append( entry );
      }

      Json::Value out( Json::objectValue );
      out["candidates"] = candidates;
      out["total"] = static_cast<Json::Int>( candidates.size() );
      Json::Value echoedCriteria( Json::objectValue );
      echoedCriteria["task"] = task;
      out["criteria"] = echoedCriteria;
      return SpatialToolResult::ok( out );
    }
};

} // namespace

void registerCapabilityTools()
{
  static const bool registered = [] {
    SpatialToolRegistry::instance().registerTool( std::make_shared<SearchCapabilitiesTool>() );
    SpatialToolRegistry::instance().registerTool( std::make_shared<SelectModelTool>() );
    return true;
  }();
  Q_UNUSED( registered );
}

} // namespace sicnu::agent::spatial_tools
