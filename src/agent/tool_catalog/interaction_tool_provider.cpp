// src/agent/tool_catalog/interaction_tool_provider.cpp
#include "interaction_tool_provider.h"
#include <algorithm>
#include <unordered_set>
#include <vector>

#include "agent/interaction_tool_registry.h"

namespace sicnu::agent::tool_catalog {

namespace {

// Band-selector property schema (#701): role string OR 1-based band number.
// A property with no "type"/"anyOf" is invalid JSON Schema for strict
// validators (TypeBox/Pi reject the whole tool schema), so the union the
// handler actually accepts is modeled explicitly.
Json::Value makeBandChannelProp( const char *description )
{
  Json::Value prop( Json::objectValue );
  Json::Value asRole( Json::objectValue );
  asRole["type"] = "string";
  Json::Value asNumber( Json::objectValue );
  asNumber["type"] = "integer";
  Json::Value anyOf( Json::arrayValue );
  anyOf.append( asRole );
  anyOf.append( asNumber );
  prop["anyOf"] = anyOf;
  prop["description"] = description;
  return prop;
}

// Derive pass shared by resetDefaults()/provideTools()/findTool() (#701):
// adds every dispatchable InteractionToolRegistry tool that is not already
// described here, so tools registered into the registry AFTER the provider
// (or the catalog) was first constructed still appear. The old behavior
// froze the snapshot at construction and made post-registration tools
// undiscoverable until an explicit resetDefaults(). Registry-derived entries
// never overwrite an existing entry: the static factories keep their richer
// output ports, and explicit registerTool() customizations are preserved.
// Rebuilds are also REMOVAL-aware: names previously merged from the registry
// but no longer listed are dropped, so unregisterTool() propagates on the
// next rebuild instead of serving a stale tool forever (live-sync review
// tail). Only touched during catalog rebuilds, which AgentToolCatalog
// serializes under its mutex — no separate guard needed.
void mergeRegistryToolsInto( std::unordered_map<std::string, AgentTool> &tools )
{
  static std::unordered_set<std::string> sRegistrySourcedNames;
  try
  {
    const std::vector<sicnu::agent::InteractionToolDefinition> defs =
      sicnu::agent::InteractionToolRegistry::instance().listTools();
    for ( const auto &name : sRegistrySourcedNames )
      tools.erase( name );
    sRegistrySourcedNames.clear();
    for ( const auto &def : defs )
    {
      if ( def.name.rfind( "data:", 0 ) == 0 )
        continue;
      if ( tools.find( def.name ) != tools.end() )
        continue;
      AgentTool tool;
      tool.name = def.name;
      tool.displayName = def.displayName;
      tool.category = ToolCategory::Interaction;
      tool.group = def.category;
      tool.description = def.description;
      tool.inputSchema = def.inputSchema;
      tools[def.name] = std::move( tool );
      sRegistrySourcedNames.insert( def.name );
    }
  }
  catch ( ... )
  {
    // Registry unavailable (early static init): the static set stands.
  }
}

AgentTool makeDrawRoiTool()
{
  AgentTool tool;
  tool.name = "canvas:draw_roi";
  tool.displayName = "Draw ROI on Canvas";
  tool.category = ToolCategory::Interaction;
  tool.group = "canvas";
  tool.description = "Draw a Region of Interest (ROI) bounding box rubber band on the active map canvas and return/store the geometry in canvas CRS.";
  tool.tags = { "canvas", "roi", "draw", "interaction", "selection", "geometry", "bbox" };

  Json::Value schema( Json::objectValue );
  schema["type"] = "object";

  Json::Value props( Json::objectValue );
  Json::Value bbox( Json::objectValue );
  bbox["type"] = "object";
  bbox["description"] = "Bounding box coordinates {xmin, ymin, xmax, ymax} in canvas CRS";
  Json::Value bboxProps( Json::objectValue );
  bboxProps["xmin"] = Json::Value( Json::objectValue );
  bboxProps["xmin"]["type"] = "number";
  bboxProps["ymin"] = Json::Value( Json::objectValue );
  bboxProps["ymin"]["type"] = "number";
  bboxProps["xmax"] = Json::Value( Json::objectValue );
  bboxProps["xmax"]["type"] = "number";
  bboxProps["ymax"] = Json::Value( Json::objectValue );
  bboxProps["ymax"]["type"] = "number";
  bbox["properties"] = bboxProps;
  Json::Value bboxReq( Json::arrayValue );
  bboxReq.append( "xmin" );
  bboxReq.append( "ymin" );
  bboxReq.append( "xmax" );
  bboxReq.append( "ymax" );
  bbox["required"] = bboxReq;
  props["bbox"] = bbox;

  Json::Value geomProp( Json::objectValue );
  geomProp["type"] = "string";
  geomProp["description"] = "WKT polygon or geometry string defining the ROI (alternative to bbox)";
  props["geometry"] = geomProp;

  Json::Value crsProp( Json::objectValue );
  crsProp["type"] = "string";
  crsProp["description"] = "CRS auth ID (e.g. EPSG:4326), when omitted canvas CRS is assumed";
  props["crs"] = crsProp;

  schema["properties"] = props;
  // Single-owner: registry owns ROI schema (bbox/geometry/crs), none required — align with createRoiSetSchema.
  tool.inputSchema = schema;

  sicnu::processing::PortDescriptor bboxPort;
  bboxPort.name = "bbox";
  bboxPort.displayName = "Bounding Box";
  bboxPort.type = sicnu::processing::DataType::BoundingBox;
  bboxPort.required = false;
  tool.inputs.push_back( bboxPort );

  sicnu::processing::PortDescriptor geomPortIn;
  geomPortIn.name = "geometry";
  geomPortIn.displayName = "Geometry WKT";
  geomPortIn.type = sicnu::processing::DataType::String;
  geomPortIn.required = false;
  tool.inputs.push_back( geomPortIn );

  sicnu::processing::PortDescriptor crsPort;
  crsPort.name = "crs";
  crsPort.displayName = "CRS";
  crsPort.type = sicnu::processing::DataType::String;
  crsPort.required = false;
  tool.inputs.push_back( crsPort );

  sicnu::processing::PortDescriptor geomPort;
  geomPort.name = "geometry";
  geomPort.displayName = "Geometry WKT";
  geomPort.type = sicnu::processing::DataType::String;
  tool.outputs.push_back( geomPort );

  tool.agentMetadata.purpose = "Draw ROI rubber band on map canvas for visual area selection";
  tool.agentMetadata.tags = tool.tags;

  return tool;
}

AgentTool makeSetBandCompositeTool()
{
  AgentTool tool;
  tool.name = "raster:set_band_composite";
  tool.displayName = "Set Raster Band Composite";
  tool.category = ToolCategory::Interaction;
  tool.group = "display";
  tool.description = "Set raster RGB band composite rendering mode and channel assignment (e.g. true-color RGB or false-color NIR-R-G) to show raster on map canvas. Supports semantic band roles ('red','nir',etc) or 1-based band numbers; also gray and opacity.";
  tool.tags = { "raster", "band", "composite", "rgb", "display", "show raster", "visualization", "render" };

  Json::Value schema( Json::objectValue );
  schema["type"] = "object";

  Json::Value props( Json::objectValue );

  Json::Value layerProp( Json::objectValue );
  layerProp["type"] = "string";
  layerProp["description"] = "Optional raster layer ID, name or asset ID; when omitted active raster is used";
  props["layer"] = layerProp;

  // Backward compat aliases
  Json::Value layerId( Json::objectValue );
  layerId["type"] = "string";
  layerId["description"] = "Layer ID or layer name (alias for layer)";
  props["layer_id"] = layerId;

  props["red"] = makeBandChannelProp( "Red channel band role (e.g. 'red','nir') or 1-based band number" );
  props["red_band"] = makeBandChannelProp( "Red channel band role or 1-based band number (alias for red)" );

  props["green"] = makeBandChannelProp( "Green channel band role or 1-based band number" );
  props["green_band"] = makeBandChannelProp( "Green channel band role or 1-based band number (alias for green)" );

  props["blue"] = makeBandChannelProp( "Blue channel band role or 1-based band number" );
  props["blue_band"] = makeBandChannelProp( "Blue channel band role or 1-based band number (alias for blue)" );

  props["gray"] = makeBandChannelProp( "Single channel band role or 1-based band number for grayscale rendering (optional)" );

  Json::Value opacityProp( Json::objectValue );
  opacityProp["type"] = "number";
  opacityProp["description"] = "Opacity 0.0-1.0 (optional)";
  props["opacity"] = opacityProp;

  schema["properties"] = props;
  // No required — layer and channels are optional per registry (active layer fallback).
  tool.inputSchema = schema;

  sicnu::processing::PortDescriptor pLayer;
  pLayer.name = "layer";
  pLayer.displayName = "Layer";
  pLayer.type = sicnu::processing::DataType::Raster;
  pLayer.required = false;
  tool.inputs.push_back( pLayer );

  for (auto name : {"red","green","blue","gray"}) {
    sicnu::processing::PortDescriptor p;
    p.name = name;
    p.displayName = name;
    p.type = sicnu::processing::DataType::String;
    p.required = false;
    tool.inputs.push_back(p);
  }

  sicnu::processing::PortDescriptor pOut;
  pOut.name = "success";
  pOut.displayName = "Success";
  pOut.type = sicnu::processing::DataType::Boolean;
  tool.outputs.push_back( pOut );

  tool.agentMetadata.purpose = "Set raster band rendering composite to show raster on map";
  tool.agentMetadata.tags = tool.tags;

  return tool;
}

AgentTool makeSetStretchTool()
{
  AgentTool tool;
  tool.name = "raster:set_stretch";
  tool.displayName = "Set Raster Display Stretch";
  tool.category = ToolCategory::Interaction;
  tool.group = "display";
  tool.description = "Configure contrast enhancement and histogram stretch (min-max, 2% cumulative cut, standard deviation) to show raster with clear contrast on map canvas. Supports method/min/max/lower/upper/factor.";
  tool.tags = { "raster", "stretch", "contrast", "display", "show raster", "visualization", "histogram", "enhancement" };

  Json::Value schema( Json::objectValue );
  schema["type"] = "object";

  Json::Value props( Json::objectValue );

  Json::Value layerProp( Json::objectValue );
  layerProp["type"] = "string";
  layerProp["description"] = "Optional raster layer ID, name or asset ID; when omitted active raster is used";
  props["layer"] = layerProp;
  Json::Value layerId( Json::objectValue );
  layerId["type"] = "string";
  layerId["description"] = "Layer ID alias for layer";
  props["layer_id"] = layerId;

  Json::Value methodProp( Json::objectValue );
  methodProp["type"] = "string";
  methodProp["description"] = "Stretch method: 'minimum_maximum','percent_clip','stddev','none' (also accepts legacy min_max/percentile_2_98/std_dev_2)";
  Json::Value enumOpts( Json::arrayValue );
  enumOpts.append( "minimum_maximum" );
  enumOpts.append( "percent_clip" );
  enumOpts.append( "stddev" );
  enumOpts.append( "none" );
  enumOpts.append( "min_max" );
  enumOpts.append( "percentile_2_98" );
  enumOpts.append( "std_dev_2" );
  methodProp["enum"] = enumOpts;
  props["method"] = methodProp;
  props["stretch_type"] = methodProp;

  for (auto name : {"lower","upper","factor","min","max","min_val","max_val"}) {
    Json::Value v( Json::objectValue );
    v["type"] = "number";
    v["description"] = std::string(name) + " (optional)";
    props[name] = v;
  }

  schema["properties"] = props;
  Json::Value req( Json::arrayValue );
  req.append( "method" );
  schema["required"] = req;

  tool.inputSchema = schema;

  for (auto name : {"layer","method","lower","upper","factor","min","max"}) {
    sicnu::processing::PortDescriptor p;
    p.name = name;
    p.displayName = name;
    p.type = (std::string(name)=="layer"||std::string(name)=="method")? sicnu::processing::DataType::String : sicnu::processing::DataType::Integer;
    p.required = (std::string(name)=="method");
    if (std::string(name)=="method") p.enumOptions = { "minimum_maximum","percent_clip","stddev","none","min_max","percentile_2_98","std_dev_2" };
    tool.inputs.push_back(p);
  }

  sicnu::processing::PortDescriptor pOut;
  pOut.name = "success";
  pOut.displayName = "Success";
  pOut.type = sicnu::processing::DataType::Boolean;
  tool.outputs.push_back( pOut );

  tool.agentMetadata.purpose = "Adjust raster contrast enhancement to show raster clearly";
  tool.agentMetadata.tags = tool.tags;

  return tool;
}

AgentTool makeZoomToExtentTool()
{
  AgentTool tool;
  tool.name = "canvas:zoom_to_extent";
  tool.displayName = "Zoom Canvas to Extent";
  tool.category = ToolCategory::Interaction;
  tool.group = "canvas";
  tool.description = "Zoom and pan the map canvas to a specified extent or layer bounding box.";
  tool.tags = { "canvas", "navigation", "zoom", "pan", "view", "extent" };

  Json::Value schema( Json::objectValue );
  schema["type"] = "object";

  Json::Value props( Json::objectValue );
  Json::Value layerId( Json::objectValue );
  layerId["type"] = "string";
  layerId["description"] = "Optional layer ID to zoom to";
  props["layer_id"] = layerId;

  Json::Value bbox( Json::objectValue );
  bbox["type"] = "object";
  bbox["description"] = "Optional bounding box {xmin, ymin, xmax, ymax}";
  props["bbox"] = bbox;

  schema["properties"] = props;
  tool.inputSchema = schema;

  sicnu::processing::PortDescriptor pLayer;
  pLayer.name = "layer_id";
  pLayer.displayName = "Layer ID";
  pLayer.type = sicnu::processing::DataType::String;
  pLayer.required = false;
  tool.inputs.push_back( pLayer );

  tool.agentMetadata.purpose = "Zoom and pan the map canvas";
  tool.agentMetadata.tags = tool.tags;

  return tool;
}

} // namespace

InteractionToolProvider::InteractionToolProvider()
{
  resetDefaults();
}

void InteractionToolProvider::resetDefaults()
{
  // Single source of truth (#622): descriptors derive from the live
  // InteractionToolRegistry (the same definitions tools/call dispatches
  // through), replacing the hand-duplicated static set that had drifted
  // (stale enums, wrong required flags) and omitted ~10 dispatchable tools.
  // The static factories below are kept for the richer output ports of the
  // two display tools; registry entries only fill gaps.
  std::lock_guard<std::mutex> lock( mMutex );
  mTools.clear();

  auto drawRoi = makeDrawRoiTool();
  mTools[drawRoi.name] = drawRoi;

  auto setBand = makeSetBandCompositeTool();
  mTools[setBand.name] = setBand;

  auto setStretch = makeSetStretchTool();
  mTools[setStretch.name] = setStretch;

  auto zoom = makeZoomToExtentTool();
  mTools[zoom.name] = zoom;
  // Derive any registry tool not already covered above so every dispatchable
  // interaction tool is advertised (view:*, roi:*, raster:get/reset_display...).
  // data:* is excluded: those tools are owned by DataToolProvider, and the
  // registry unconditionally registers them too — deriving them here listed
  // every data:* tool twice in the catalog (#641).
  mergeRegistryToolsInto( mTools );
}

std::vector<AgentTool> InteractionToolProvider::provideTools() const
{
  std::lock_guard<std::mutex> lock( mMutex );
  // Live re-sync (#701): pick up interaction tools registered after the
  // provider was constructed instead of serving the construction snapshot.
  mergeRegistryToolsInto( mTools );
  std::vector<AgentTool> result;
  result.reserve( mTools.size() );
  for ( const auto &pair : mTools )
  {
    result.push_back( pair.second );
  }
  // Deterministic listing order (#634).
  std::sort( result.begin(), result.end(),
             []( const AgentTool &a, const AgentTool &b ) { return a.name < b.name; } );
  return result;
}

std::optional<AgentTool> InteractionToolProvider::findTool( const std::string &name ) const
{
  std::lock_guard<std::mutex> lock( mMutex );
  // Same live re-sync as provideTools(): a tool registered after construction
  // must be findable by name too.
  mergeRegistryToolsInto( mTools );
  auto it = mTools.find( name );
  if ( it != mTools.end() )
    return it->second;

  // Underscore normalization fallback
  for ( const auto &pair : mTools )
  {
    std::string norm = pair.first;
    for ( auto &ch : norm )
    {
      if ( ch == ':' || ch == '-' ) ch = '_';
    }
    if ( norm == name )
      return pair.second;
  }

  return std::nullopt;
}

void InteractionToolProvider::registerTool( const AgentTool &tool )
{
  std::lock_guard<std::mutex> lock( mMutex );
  mTools[tool.name] = tool;
}

bool InteractionToolProvider::unregisterTool( const std::string &name )
{
  std::lock_guard<std::mutex> lock( mMutex );
  return mTools.erase( name ) > 0;
}

} // namespace sicnu::agent::tool_catalog
