// src/agent/spatial_tools/io_tools.cpp
// Foundation 5.0 — thin read-only I/O tools over src/geospatial (ADR 0136).
// The wrappers translate JSON in/out; every answer comes from the foundation
// contracts (probe / capabilities / product registry). No parsing here.

#include "io_tools.h"

#include "geospatial/formats/format_profiles.h"
#include "geospatial/products/product_registry.h"
#include "geospatial/probe/probe.h"

#include <string>

namespace sicnu::agent::spatial_tools {

namespace {

using sicnu::agent::spatial_tools::SpatialTool;
using sicnu::agent::spatial_tools::SpatialToolResult;

std::string requirePath( const Json::Value &input, SpatialToolResult &result )
{
  if ( !input.isObject() || !input.isMember( "path" ) || !input["path"].isString() ||
       input["path"].asString().empty() )
  {
    result = SpatialToolResult::failure( "io: 'path' (string) is required" );
    return std::string();
  }
  return input["path"].asString();
}

Json::Value pathSchema()
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value path( Json::objectValue );
  path["type"] = "string";
  path["description"] = "Dataset or product path/URI: local file, .SAFE/MTL directory, "
                        "http(s) URL, GDAL VSI handle or subdataset selector";
  props["path"] = path;
  schema["properties"] = props;
  Json::Value required( Json::arrayValue );
  required.append( "path" );
  schema["required"] = required;
  return schema;
}

class IoProbeTool final : public SpatialTool
{
  public:
    std::string name() const override { return "io:probe"; }
    std::string displayName() const override { return "Probe dataset"; }
    std::string description() const override
    {
      return "Identifies a data resource: format + driver + certification, whether it is a "
             "Cloud Optimized GeoTIFF (structural check), and which sensor product family "
             "(Landsat/Sentinel-1/Sentinel-2/MODIS) claims it. Read-only and bounded — never "
             "scans pixels. Input: {path}. Use before importing or answering data questions.";
    }
    std::vector<std::string> tags() const override { return { "io", "probe", "metadata" }; }
    Json::Value inputSchema() const override { return pathSchema(); }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      SpatialToolResult failure;
      const std::string path = requirePath( input, failure );
      if ( path.empty() )
        return failure;
      try
      {
        sicnu::geo::ProbeOptions options;
        options.includeMetadata = true;
        return SpatialToolResult::ok( sicnu::geo::probeResource( path, options ).toJson() );
      }
      catch ( const sicnu::geo::GeoError &error )
      {
        return SpatialToolResult::failure( error.what() );
      }
      catch ( const std::exception &error )
      {
        // Tool wrappers never let foreign exceptions escape as crashes.
        return SpatialToolResult::failure( error.what() );
      }
    }
};

class IoCapabilitiesTool final : public SpatialTool
{
  public:
    std::string name() const override { return "io:capabilities"; }
    std::string displayName() const override { return "Dataset capabilities"; }
    std::string description() const override
    {
      return "What can this dataset do: window/block reads, multidim slices, subdatasets, "
             "CRS/NoData/mask/overviews, remote range support, vector attributes. One "
             "read-only open; answers per dataset, not per format. Input: {path}.";
    }
    std::vector<std::string> tags() const override { return { "io", "capability", "metadata" }; }
    Json::Value inputSchema() const override { return pathSchema(); }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      SpatialToolResult failure;
      const std::string path = requirePath( input, failure );
      if ( path.empty() )
        return failure;
      try
      {
        return SpatialToolResult::ok( sicnu::geo::resolveDatasetCapabilities( path ).toJson() );
      }
      catch ( const sicnu::geo::GeoError &error )
      {
        return SpatialToolResult::failure( error.what() );
      }
      catch ( const std::exception &error )
      {
        // Tool wrappers never let foreign exceptions escape as crashes.
        return SpatialToolResult::failure( error.what() );
      }
    }
};

class IoProductTool final : public SpatialTool
{
  public:
    std::string name() const override { return "io:product"; }
    std::string displayName() const override { return "Describe sensor product"; }
    std::string description() const override
    {
      return "Normalized sensor-product metadata (platform/sensor/level/acquisition/orbit/"
             "polarizations/cloud) plus the constituent asset list (measurements, masks, "
             "annotations with band roles/resolutions) and a completeness verdict. Read-only. "
             "Input: {path} (product directory or metadata sidecar).";
    }
    std::vector<std::string> tags() const override { return { "io", "product", "metadata" }; }
    Json::Value inputSchema() const override { return pathSchema(); }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      SpatialToolResult failure;
      const std::string path = requirePath( input, failure );
      if ( path.empty() )
        return failure;
      try
      {
        return SpatialToolResult::ok(
          sicnu::geo::ProductAdapterRegistry::instance().describe( path ).toJson() );
      }
      catch ( const sicnu::geo::GeoError &error )
      {
        return SpatialToolResult::failure( error.what() );
      }
      catch ( const std::exception &error )
      {
        // Tool wrappers never let foreign exceptions escape as crashes.
        return SpatialToolResult::failure( error.what() );
      }
    }
};

} // namespace

void registerIoTools()
{
  static const bool registered = [] {
    SpatialToolRegistry::instance().registerTool( std::make_shared<IoProbeTool>() );
    SpatialToolRegistry::instance().registerTool( std::make_shared<IoCapabilitiesTool>() );
    SpatialToolRegistry::instance().registerTool( std::make_shared<IoProductTool>() );
    return true;
  }();
  Q_UNUSED( registered );
}

} // namespace sicnu::agent::spatial_tools
