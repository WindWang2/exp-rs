// src/agent/spatial_tools/terrain_spatial_tools.cpp — terrain agent tools
// (track terrain-hydrology-11). Both tools are read-only helpers that
// delegate to the terrain kernels — no second implementation.
#include "terrain_spatial_tools.h"

#include "processing/algorithms/terrain_viewshed.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <gdal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::agent::spatial_tools {
namespace {

bool ensureGdal()
{
    static const bool ok = [] {
        GDALAllRegister();
        return true;
    }();
    return ok;
}

bool parseCellPair( const Json::Value &value, double &col, double &row )
{
    if ( !value.isString() )
        return false;
    const std::string text = value.asString();
    const auto comma = text.find( ',' );
    if ( comma == std::string::npos )
        return false;
    try
    {
        col = std::stod( text.substr( 0, comma ) );
        row = std::stod( text.substr( comma + 1 ) );
    }
    catch ( const std::exception & )
    {
        return false;
    }
    return true;
}

} // namespace

std::string TerrainProfileTool::description() const
{
    return "Elevation profile along a straight pixel line over a DEM raster. "
           "Returns per-sample elevations with map-unit distances plus a "
           "min/max/gain summary. Read-only; nearest-cell sampling.";
}

std::vector<std::string> TerrainProfileTool::tags() const
{
    return { "terrain", "dem", "profile", "read_only" };
}

Json::Value TerrainProfileTool::inputSchema() const
{
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    Json::Value props( Json::objectValue );
    {
        Json::Value p( Json::objectValue );
        p["type"] = "string";
        p["description"] = "DEM raster path";
        props["raster"] = p;
    }
    {
        Json::Value p( Json::objectValue );
        p["type"] = "string";
        p["description"] = "Start point as 'col,row' (zero-based pixels)";
        props["from"] = p;
    }
    {
        Json::Value p( Json::objectValue );
        p["type"] = "string";
        p["description"] = "End point as 'col,row' (zero-based pixels)";
        props["to"] = p;
    }
    {
        Json::Value p( Json::objectValue );
        p["type"] = "integer";
        p["description"] = "Number of samples along the line (2–512, default 64)";
        props["samples"] = p;
    }
    schema["properties"] = props;
    Json::Value required( Json::arrayValue );
    required.append( "raster" );
    required.append( "from" );
    required.append( "to" );
    schema["required"] = required;
    return schema;
}

Json::Value TerrainProfileTool::outputSchema() const
{
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    Json::Value props( Json::objectValue );
    {
        Json::Value p( Json::objectValue );
        p["type"] = "array";
        p["description"] = "Profile samples: col, row, distance (map units), elevation";
        props["samples"] = p;
    }
    {
        Json::Value p( Json::objectValue );
        p["type"] = "object";
        p["description"] = "min/max/mean elevation, total distance, elevation gain/loss";
        props["summary"] = p;
    }
    schema["properties"] = props;
    return schema;
}

SpatialToolResult TerrainProfileTool::execute( const Json::Value &input )
{
    if ( !input.isObject() )
        return SpatialToolResult::failure( "input must be a JSON object", "INVALID_PARAMETER", "validation" );
    ensureGdal();

    const std::string raster = input["raster"].isString() ? input["raster"].asString() : "";
    if ( raster.empty() )
        return SpatialToolResult::failure( "'raster' is required", "INVALID_PARAMETER", "validation" );
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    if ( !parseCellPair( input["from"], x0, y0 ) || !parseCellPair( input["to"], x1, y1 ) )
        return SpatialToolResult::failure( "'from' and 'to' must be 'col,row' strings", "INVALID_PARAMETER",
                        "validation" );

    int samples = 64;
    if ( input.isMember( "samples" ) && input["samples"].isNumeric() )
        samples = input["samples"].asInt();
    samples = std::clamp( samples, 2, 512 );

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( raster ) ) )
        return SpatialToolResult::failure( "failed to open raster: " + raster, "NOT_FOUND", "io" );
    const int width = ds.width();
    const int height = ds.height();
    if ( width <= 0 || height <= 0 )
        return SpatialToolResult::failure( "raster is empty", "INVALID_INPUT", "io" );
    bool hasNodata = false;
    const double nodataD = ds.bandNoDataValue( 1, &hasNodata );
    const std::array<double, 6> gt = ds.geoTransform();
    const double cellX = std::abs( gt[1] );
    const double cellY = std::abs( gt[5] );

    Json::Value out( Json::objectValue );
    Json::Value sampleList( Json::arrayValue );
    double minZ = std::numeric_limits<double>::infinity();
    double maxZ = -std::numeric_limits<double>::infinity();
    double sumZ = 0.0;
    double gain = 0.0;
    double loss = 0.0;
    double prevZ = 0.0;
    int validSamples = 0;
    for ( int k = 0; k < samples; ++k )
    {
        const double t = static_cast<double>( k ) / static_cast<double>( samples - 1 );
        const double fx = x0 + t * ( x1 - x0 );
        const double fy = y0 + t * ( y1 - y0 );
        const int cx = static_cast<int>( std::lround( fx ) );
        const int cy = static_cast<int>( std::lround( fy ) );
        if ( cx < 0 || cy < 0 || cx >= width || cy >= height )
            continue;
        float z = 0.0f;
        if ( !ds.readBandWindow( 1, cx, cy, 1, 1, &z ) )
            continue;
        if ( hasNodata && ( z == static_cast<float>( nodataD ) || std::isnan( z ) ) )
            continue;
        const double mapX = gt[0] + ( fx + 0.5 ) * gt[1] + ( fy + 0.5 ) * gt[2];
        const double mapY = gt[3] + ( fx + 0.5 ) * gt[4] + ( fy + 0.5 ) * gt[5];
        const double dist = std::hypot( ( fx - x0 ) * cellX, ( fy - y0 ) * cellY );
        Json::Value s( Json::objectValue );
        s["col"] = cx;
        s["row"] = cy;
        s["distance"] = dist;
        s["elevation"] = z;
        sampleList.append( s );
        minZ = std::min( minZ, static_cast<double>( z ) );
        maxZ = std::max( maxZ, static_cast<double>( z ) );
        sumZ += z;
        if ( validSamples > 0 )
        {
            if ( z > prevZ )
                gain += z - prevZ;
            else
                loss += prevZ - z;
        }
        prevZ = z;
        ++validSamples;
    }
    if ( validSamples == 0 )
        return SpatialToolResult::failure( "profile line lies outside the raster or is fully NoData",
                        "INVALID_PARAMETER", "validation" );

    out["samples"] = sampleList;
    Json::Value summary( Json::objectValue );
    summary["minElevation"] = minZ;
    summary["maxElevation"] = maxZ;
    summary["meanElevation"] = sumZ / validSamples;
    summary["totalDistance"] = std::hypot( ( x1 - x0 ) * cellX, ( y1 - y0 ) * cellY );
    summary["elevationGain"] = gain;
    summary["elevationLoss"] = loss;
    summary["distanceUnits"] =
        "map units of the raster CRS (cell-size derived)";
    out["summary"] = summary;
    return SpatialToolResult::ok( std::move( out ) );
}

std::string TerrainViewshedInspectTool::description() const
{
    return "Quick single-observer viewshed summary over a DEM without writing "
           "a raster: visible-cell count/fraction within an analysis radius "
           "plus the horizon profile (max terrain angle per azimuth sector). "
           "Read-only.";
}

std::vector<std::string> TerrainViewshedInspectTool::tags() const
{
    return { "terrain", "dem", "viewshed", "horizon", "read_only" };
}

Json::Value TerrainViewshedInspectTool::inputSchema() const
{
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    Json::Value props( Json::objectValue );
    {
        Json::Value p( Json::objectValue );
        p["type"] = "string";
        p["description"] = "DEM raster path";
        props["raster"] = p;
    }
    {
        Json::Value p( Json::objectValue );
        p["type"] = "string";
        p["description"] = "Observer as 'col,row' (zero-based pixels)";
        props["observer"] = p;
    }
    {
        Json::Value p( Json::objectValue );
        p["type"] = "number";
        p["description"] = "Observer eye height above ground (default 1.7 m)";
        props["observer_height"] = p;
    }
    {
        Json::Value p( Json::objectValue );
        p["type"] = "number";
        p["description"] = "Analysis radius in map units (default 0 = full frame, "
                           "capped at 2^24 cells for this quick check)";
        props["radius"] = p;
    }
    schema["properties"] = props;
    Json::Value required( Json::arrayValue );
    required.append( "raster" );
    required.append( "observer" );
    schema["required"] = required;
    return schema;
}

Json::Value TerrainViewshedInspectTool::outputSchema() const
{
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    Json::Value props( Json::objectValue );
    {
        Json::Value p( Json::objectValue );
        p["type"] = "integer";
        p["description"] = "Visible cell count within the radius";
        props["visibleCells"] = p;
    }
    {
        Json::Value p( Json::objectValue );
        p["type"] = "number";
        p["description"] = "Visible fraction of the analysed cells";
        props["visibleFraction"] = p;
    }
    {
        Json::Value p( Json::objectValue );
        p["type"] = "array";
        p["description"] = "Horizon sectors: azimuth and max terrain angle (degrees)";
        props["horizon"] = p;
    }
    schema["properties"] = props;
    return schema;
}

SpatialToolResult TerrainViewshedInspectTool::execute( const Json::Value &input )
{
    if ( !input.isObject() )
        return SpatialToolResult::failure( "input must be a JSON object", "INVALID_PARAMETER", "validation" );
    ensureGdal();

    const std::string raster = input["raster"].isString() ? input["raster"].asString() : "";
    if ( raster.empty() )
        return SpatialToolResult::failure( "'raster' is required", "INVALID_PARAMETER", "validation" );
    double obsCol = 0.0;
    double obsRow = 0.0;
    if ( !parseCellPair( input["observer"], obsCol, obsRow ) )
        return SpatialToolResult::failure( "'observer' must be a 'col,row' string", "INVALID_PARAMETER",
                        "validation" );

    double observerHeight = 1.7;
    if ( input.isMember( "observer_height" ) && input["observer_height"].isNumeric() )
        observerHeight = input["observer_height"].asDouble();
    double radius = 0.0;
    if ( input.isMember( "radius" ) && input["radius"].isNumeric() )
        radius = input["radius"].asDouble();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( raster ) ) )
        return SpatialToolResult::failure( "failed to open raster: " + raster, "NOT_FOUND", "io" );
    const int width = ds.width();
    const int height = ds.height();
    if ( width <= 0 || height <= 0 )
        return SpatialToolResult::failure( "raster is empty", "INVALID_INPUT", "io" );
    // Quick-check budget: this tool is for interactive inspection, not full
    // production runs — cap at 2^24 cells and tell the agent to use
    // rs:terrain_viewshed for more.
    if ( static_cast<double>( width ) * static_cast<double>( height ) > 16777216.0 )
        return SpatialToolResult::failure(
            "raster exceeds the 2^24-cell quick-inspect budget; use the "
            "rs:terrain_viewshed operator instead",
            "TOO_LARGE", "validation" );
    bool hasNodata = false;
    const double nodataD = ds.bandNoDataValue( 1, &hasNodata );
    const float nodata =
        hasNodata && std::isfinite( nodataD ) ? static_cast<float>( nodataD )
                                              : std::numeric_limits<float>::quiet_NaN();
    const std::array<double, 6> gt = ds.geoTransform();
    const double cellSizeX = std::abs( gt[1] );
    const double cellSizeY = std::abs( gt[5] );

    const size_t n = static_cast<size_t>( width ) * height;
    std::vector<float> dem( n );
    if ( !ds.readBandWindow( 1, 0, 0, width, height, dem.data() ) )
        return SpatialToolResult::failure( "failed to read the DEM band", "IO", "runtime" );

    TerrainVisibility::ViewshedParams params;
    params.obsCol = obsCol;
    params.obsRow = obsRow;
    params.observerHeight = observerHeight;
    params.radius = radius;
    std::vector<std::uint8_t> visible;
    if ( !TerrainVisibility::viewshedR3( dem.data(), width, height, nodata, cellSizeX,
                                         cellSizeY, params, &visible, 255 ) )
        return SpatialToolResult::failure( "viewshed failed (observer outside the grid or on NoData?)",
                        "INVALID_PARAMETER", "validation" );
    Json::UInt64 visibleCells = 0;
    Json::UInt64 analysed = 0;
    for ( const std::uint8_t v : visible )
    {
        if ( v == 255 )
            continue;
        ++analysed;
        visibleCells += v ? 1 : 0;
    }

    Json::Value out( Json::objectValue );
    out["visibleCells"] = visibleCells;
    out["visibleFraction"] =
        analysed > 0 ? static_cast<double>( visibleCells ) / static_cast<double>( analysed )
                     : 0.0;

    TerrainVisibility::HorizonProfile horizon;
    if ( TerrainVisibility::horizonProfile( dem.data(), width, height, nodata, cellSizeX,
                                            cellSizeY, obsCol, obsRow, observerHeight,
                                            radius, 0.0, 15.0, &horizon ) )
    {
        Json::Value sectors( Json::arrayValue );
        for ( std::size_t s = 0; s < horizon.azimuths.size(); ++s )
        {
            Json::Value sec( Json::objectValue );
            sec["azimuth"] = horizon.azimuths[s];
            sec["angle"] = horizon.angles[s];
            sectors.append( sec );
        }
        out["horizon"] = sectors;
    }
    return SpatialToolResult::ok( std::move( out ) );
}

void registerTerrainTools()
{
    static const bool registered = [] {
        SpatialToolRegistry::instance().registerTool(
            std::make_shared<TerrainProfileTool>() );
        SpatialToolRegistry::instance().registerTool(
            std::make_shared<TerrainViewshedInspectTool>() );
        return true;
    }();
    Q_UNUSED( registered );
}

} // namespace sicnu::agent::spatial_tools
