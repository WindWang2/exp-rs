// src/agent/tool_catalog/interaction_tool_provider.cpp
#include "interaction_tool_provider.h"

namespace sicnu::agent::tool_catalog {

namespace {

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

  schema["properties"] = props;
  Json::Value req( Json::arrayValue );
  req.append( "bbox" );
  schema["required"] = req;

  tool.inputSchema = schema;

  sicnu::processing::PortDescriptor bboxPort;
  bboxPort.name = "bbox";
  bboxPort.displayName = "Bounding Box";
  bboxPort.type = sicnu::processing::DataType::BoundingBox;
  bboxPort.required = true;
  tool.inputs.push_back( bboxPort );

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
  tool.description = "Set raster RGB band composite rendering mode and channel assignment (e.g. true-color RGB or false-color NIR-R-G) to show raster on map canvas.";
  tool.tags = { "raster", "band", "composite", "rgb", "display", "show raster", "visualization", "render" };

  Json::Value schema( Json::objectValue );
  schema["type"] = "object";

  Json::Value props( Json::objectValue );

  Json::Value layerId( Json::objectValue );
  layerId["type"] = "string";
  layerId["description"] = "Layer ID or layer name of the raster on map canvas";
  props["layer_id"] = layerId;

  Json::Value rBand( Json::objectValue );
  rBand["type"] = "integer";
  rBand["description"] = "1-based band index for Red channel";
  props["red_band"] = rBand;

  Json::Value gBand( Json::objectValue );
  gBand["type"] = "integer";
  gBand["description"] = "1-based band index for Green channel";
  props["green_band"] = gBand;

  Json::Value bBand( Json::objectValue );
  bBand["type"] = "integer";
  bBand["description"] = "1-based band index for Blue channel";
  props["blue_band"] = bBand;

  schema["properties"] = props;

  Json::Value req( Json::arrayValue );
  req.append( "layer_id" );
  req.append( "red_band" );
  req.append( "green_band" );
  req.append( "blue_band" );
  schema["required"] = req;

  tool.inputSchema = schema;

  sicnu::processing::PortDescriptor pLayer;
  pLayer.name = "layer_id";
  pLayer.displayName = "Layer ID";
  pLayer.type = sicnu::processing::DataType::Raster;
  pLayer.required = true;
  tool.inputs.push_back( pLayer );

  sicnu::processing::PortDescriptor pR;
  pR.name = "red_band";
  pR.displayName = "Red Band";
  pR.type = sicnu::processing::DataType::Integer;
  pR.required = true;
  tool.inputs.push_back( pR );

  sicnu::processing::PortDescriptor pG;
  pG.name = "green_band";
  pG.displayName = "Green Band";
  pG.type = sicnu::processing::DataType::Integer;
  pG.required = true;
  tool.inputs.push_back( pG );

  sicnu::processing::PortDescriptor pB;
  pB.name = "blue_band";
  pB.displayName = "Blue Band";
  pB.type = sicnu::processing::DataType::Integer;
  pB.required = true;
  tool.inputs.push_back( pB );

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
  tool.description = "Configure contrast enhancement and histogram stretch (min-max, 2% cumulative cut, standard deviation) to show raster with clear contrast on map canvas.";
  tool.tags = { "raster", "stretch", "contrast", "display", "show raster", "visualization", "histogram", "enhancement" };

  Json::Value schema( Json::objectValue );
  schema["type"] = "object";

  Json::Value props( Json::objectValue );

  Json::Value layerId( Json::objectValue );
  layerId["type"] = "string";
  layerId["description"] = "Layer ID or layer name of the raster on map canvas";
  props["layer_id"] = layerId;

  Json::Value stretchType( Json::objectValue );
  stretchType["type"] = "string";
  stretchType["description"] = "Stretch algorithm: 'min_max', 'percentile_2_98', 'std_dev_2', 'none'";
  Json::Value enumOpts( Json::arrayValue );
  enumOpts.append( "min_max" );
  enumOpts.append( "percentile_2_98" );
  enumOpts.append( "std_dev_2" );
  enumOpts.append( "none" );
  stretchType["enum"] = enumOpts;
  props["stretch_type"] = stretchType;

  Json::Value minVal( Json::objectValue );
  minVal["type"] = "number";
  minVal["description"] = "Manual minimum display value (optional)";
  props["min_val"] = minVal;

  Json::Value maxVal( Json::objectValue );
  maxVal["type"] = "number";
  maxVal["description"] = "Manual maximum display value (optional)";
  props["max_val"] = maxVal;

  schema["properties"] = props;

  Json::Value req( Json::arrayValue );
  req.append( "layer_id" );
  req.append( "stretch_type" );
  schema["required"] = req;

  tool.inputSchema = schema;

  sicnu::processing::PortDescriptor pLayer;
  pLayer.name = "layer_id";
  pLayer.displayName = "Layer ID";
  pLayer.type = sicnu::processing::DataType::Raster;
  pLayer.required = true;
  tool.inputs.push_back( pLayer );

  sicnu::processing::PortDescriptor pType;
  pType.name = "stretch_type";
  pType.displayName = "Stretch Type";
  pType.type = sicnu::processing::DataType::Enum;
  pType.required = true;
  pType.enumOptions = { "min_max", "percentile_2_98", "std_dev_2", "none" };
  tool.inputs.push_back( pType );

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
}

std::vector<AgentTool> InteractionToolProvider::provideTools() const
{
  std::lock_guard<std::mutex> lock( mMutex );
  std::vector<AgentTool> result;
  result.reserve( mTools.size() );
  for ( const auto &pair : mTools )
  {
    result.push_back( pair.second );
  }
  return result;
}

std::optional<AgentTool> InteractionToolProvider::findTool( const std::string &name ) const
{
  std::lock_guard<std::mutex> lock( mMutex );
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
