// src/agent/spatial_tools/sample_tools.cpp
#include "sample_tools.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <ogr_spatialref.h>

#include <QFileInfo>
#include <QString>

#include "qgsdatasourceresolver.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sicnu::agent::spatial_tools {

namespace {

constexpr int kMaxSamplePoints = 64;
constexpr int kMaxSampleFeatures = 20;
constexpr int kMaxCompareSamples = 64;

SpatialToolResult openRaster( const std::string &path, GDALDatasetUniquePtr &ds )
{
  if ( path.empty() )
    return SpatialToolResult::failure( "Missing raster path", "INVALID_PARAMETER", "validation" );
  if ( QgsDataSourceResolver::requiresLocalExistenceCheck( QString::fromStdString( path ) ) &&
       !QFileInfo::exists( QString::fromStdString( path ) ) )
    return SpatialToolResult::failure( "Raster file not found: " + path, "NOT_FOUND", "io", false );
  ds.reset( GDALDataset::Open( path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY ) );
  if ( !ds )
    return SpatialToolResult::failure( "GDAL could not open raster: " + path, "IO_ERROR", "io", false );
  return SpatialToolResult::ok( Json::Value( Json::objectValue ) );
}

/// World → pixel using the dataset geotransform (nearest).
bool worldToPixel( const double gt[6], double x, double y, int width, int height, int *px, int *py )
{
  const double det = gt[1] * gt[5] - gt[2] * gt[4];
  if ( std::fabs( det ) < 1e-12 )
    return false;
  const double dx = x - gt[0];
  const double dy = y - gt[3];
  const double col = ( gt[5] * dx - gt[2] * dy ) / det;
  const double row = ( -gt[4] * dx + gt[1] * dy ) / det;
  *px = static_cast<int>( std::floor( col ) );
  *py = static_cast<int>( std::floor( row ) );
  return *px >= 0 && *py >= 0 && *px < width && *py < height;
}

} // namespace

// ---------------------------------------------------------------------------
// spatial:sample_pixels
// ---------------------------------------------------------------------------

namespace {

class SamplePixelsTool final : public SpatialTool
{
  public:
    std::string name() const override { return "spatial:sample_pixels"; }
    std::string displayName() const override { return "Sample raster pixels"; }
    std::string description() const override
    {
      return "Read pixel values at explicit coordinates (max 64 points) from a raster band — "
             "ground-truthing for assessments without loading the file. Input: {path, points: "
             "[{x, y} | {lon, lat}], band?, crs?} where x/y are in the raster CRS (or lon/lat "
             "with crs=\"EPSG:4326\"). Returns per-point value/nodata status. Bounded, fast, "
             "read-only.";
    }
    std::vector<std::string> tags() const override
    {
      return { "spatial", "raster", "sample", "pixels", "verify" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value path( Json::objectValue );
      path["type"] = "string";
      props["path"] = path;
      Json::Value points( Json::objectValue );
      points["type"] = "array";
      points["description"] = "Up to 64 coordinate objects: {x, y} (raster CRS) or {lon, lat} with crs=EPSG:4326";
      points["maxItems"] = kMaxSamplePoints;
      points["items"] = Json::Value( Json::objectValue );
      props["points"] = points;
      Json::Value band( Json::objectValue );
      band["type"] = "integer";
      band["description"] = "1-based band index (default 1)";
      props["band"] = band;
      Json::Value crs( Json::objectValue );
      crs["type"] = "string";
      crs["description"] = "CRS of the input coordinates (default: raster CRS; \"EPSG:4326\" reads as lon/lat)";
      props["crs"] = crs;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "path" );
      required.append( "points" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["samples"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const std::string path = input.isMember( "path" ) && input["path"].isString()
                                 ? input["path"].asString()
                                 : std::string();
      if ( path.empty() )
        return SpatialToolResult::failure( "Missing required parameter: path", "INVALID_PARAMETER",
                                           "validation" );
      const Json::Value &points = input["points"];
      if ( !points.isArray() || points.empty() )
        return SpatialToolResult::failure( "Missing required parameter: points (non-empty array)",
                                           "INVALID_PARAMETER", "validation" );
      if ( static_cast<int>( points.size() ) > kMaxSamplePoints )
        return SpatialToolResult::failure(
          "Too many points (" + std::to_string( points.size() ) + "); cap is " +
            std::to_string( kMaxSamplePoints ),
          "LIMIT_EXCEEDED", "validation", false );

      GDALDatasetUniquePtr ds;
      if ( SpatialToolResult open = openRaster( path, ds ); !open.success )
        return open;

      const int bandIndex =
        input.isMember( "band" ) && input["band"].isInt() ? std::max( 1, input["band"].asInt() ) : 1;
      GDALRasterBand *band = ds->GetRasterBand( bandIndex );
      if ( !band )
        return SpatialToolResult::failure( "Band " + std::to_string( bandIndex ) + " not found",
                                           "INVALID_PARAMETER", "validation" );

      double gt[6] = { 0.0 };
      const bool hasGt = ds->GetGeoTransform( gt ) == CE_None;

      // Optional coordinate transform into the raster CRS.
      OGRSpatialReference targetSrs;
      bool haveTargetCrs = false;
      if ( const OGRSpatialReference *srs = ds->GetSpatialRef() )
      {
        targetSrs = *srs;
        haveTargetCrs = true;
      }
      OGRSpatialReference sourceSrs;
      bool transformCoords = false;
      if ( input.isMember( "crs" ) && input["crs"].isString() )
      {
        const std::string crsText = input["crs"].asString();
        if ( sourceSrs.SetFromUserInput( crsText.c_str() ) != OGRERR_NONE )
          return SpatialToolResult::failure( "Unparseable crs: " + crsText, "INVALID_PARAMETER",
                                             "validation" );
        if ( haveTargetCrs && !sourceSrs.IsSame( &targetSrs ) )
          transformCoords = true;
      }

      int hasNoData = 0;
      const double nodata = band->GetNoDataValue( &hasNoData );

      Json::Value samples( Json::arrayValue );
      int outOfBounds = 0;
      for ( const auto &point : points )
      {
        if ( !point.isObject() )
          continue;
        double x = 0.0;
        double y = 0.0;
        if ( point.isMember( "lon" ) && point.isMember( "lat" ) )
        {
          x = point["lon"].asDouble();
          y = point["lat"].asDouble();
        }
        else if ( point.isMember( "x" ) && point.isMember( "y" ) )
        {
          x = point["x"].asDouble();
          y = point["y"].asDouble();
        }
        else
        {
          Json::Value s( Json::objectValue );
          s["error"] = "point needs {x, y} or {lon, lat}";
          samples.append( s );
          continue;
        }

        Json::Value s( Json::objectValue );
        s["x"] = x;
        s["y"] = y;
        if ( transformCoords )
        {
          OGRPoint ogrPoint( x, y );
          ogrPoint.assignSpatialReference( &sourceSrs );
          if ( !ogrPoint.transformTo( &targetSrs ) )
          {
            s["error"] = "coordinate transform failed";
            samples.append( s );
            continue;
          }
          x = ogrPoint.getX();
          y = ogrPoint.getY();
        }

        int px = 0;
        int py = 0;
        if ( !hasGt || !worldToPixel( gt, x, y, ds->GetRasterXSize(), ds->GetRasterYSize(), &px, &py ) )
        {
          s["status"] = "out_of_bounds";
          samples.append( s );
          ++outOfBounds;
          continue;
        }
        double value = 0.0;
        if ( band->RasterIO( GF_Read, px, py, 1, 1, &value, 1, 1, GDT_Float64, 0, 0 ) != CE_None )
        {
          s["status"] = "read_error";
          samples.append( s );
          continue;
        }
        s["status"] = ( hasNoData && value == nodata ) ? "nodata" : "ok";
        s["value"] = value;
        s["pixel"] = Json::Value( Json::objectValue );
        s["pixel"]["x"] = px;
        s["pixel"]["y"] = py;
        samples.append( s );
      }

      Json::Value out( Json::objectValue );
      out["path"] = path;
      out["band"] = bandIndex;
      out["nodata"] = hasNoData ? Json::Value( nodata ) : Json::Value();
      out["samples"] = samples;
      out["out_of_bounds"] = outOfBounds;
      return SpatialToolResult::ok( out );
    }
};

} // namespace

// ---------------------------------------------------------------------------
// spatial:sample_features
// ---------------------------------------------------------------------------

namespace {

class SampleFeaturesTool final : public SpatialTool
{
  public:
    std::string name() const override { return "spatial:sample_features"; }
    std::string displayName() const override { return "Sample vector features"; }
    std::string description() const override
    {
      return "Read up to 20 features (bounded attribute + geometry-summary rows) from a vector "
             "source, optionally filtered by an attribute clause and a bbox — enough for an "
             "agent to verify classification/category values without dumping the dataset.";
    }
    std::vector<std::string> tags() const override
    {
      return { "spatial", "vector", "sample", "features", "attributes" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value path( Json::objectValue );
      path["type"] = "string";
      props["path"] = path;
      Json::Value limit( Json::objectValue );
      limit["type"] = "integer";
      limit["description"] = "Max features (default 10, cap 20)";
      props["limit"] = limit;
      Json::Value fields( Json::objectValue );
      fields["type"] = "array";
      fields["description"] = "Optional field names to include (default: first 8)";
      props["fields"] = fields;
      Json::Value where( Json::objectValue );
      where["type"] = "string";
      where["description"] = "Optional OGR attribute filter, e.g. \"class = 'water'\"";
      props["where"] = where;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "path" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["features"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string path = input.isMember( "path" ) && input["path"].isString()
                                 ? input["path"].asString()
                                 : std::string();
      if ( path.empty() )
        return SpatialToolResult::failure( "Missing required parameter: path", "INVALID_PARAMETER",
                                           "validation" );
      int limit = input.isMember( "limit" ) && input["limit"].isInt() ? input["limit"].asInt() : 10;
      limit = std::clamp( limit, 1, kMaxSampleFeatures );

      if ( QgsDataSourceResolver::requiresLocalExistenceCheck( QString::fromStdString( path ) ) &&
           !QFileInfo::exists( QString::fromStdString( path ) ) )
        return SpatialToolResult::failure( "Vector file not found: " + path, "NOT_FOUND", "io", false );

      GDALDatasetUniquePtr ds( GDALDataset::Open( path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY ) );
      if ( !ds )
        return SpatialToolResult::failure( "GDAL could not open vector: " + path, "IO_ERROR", "io", false );
      OGRLayer *layer = ds->GetLayer( 0 );
      if ( !layer )
        return SpatialToolResult::failure( "No vector layer in: " + path, "IO_ERROR", "io", false );

      if ( input.isMember( "where" ) && input["where"].isString() )
      {
        if ( layer->SetAttributeFilter( input["where"].asString().c_str() ) != OGRERR_NONE )
          return SpatialToolResult::failure( "Invalid attribute filter", "INVALID_PARAMETER",
                                             "validation" );
      }

      // Field selection (capped).
      const OGRFeatureDefn *defn = layer->GetLayerDefn();
      std::vector<int> fieldIndices;
      if ( input.isMember( "fields" ) && input["fields"].isArray() )
      {
        for ( const auto &name : input["fields"] )
        {
          const int idx = defn->GetFieldIndex( name.asString().c_str() );
          if ( idx >= 0 )
            fieldIndices.push_back( idx );
        }
      }
      else
      {
        const int count = std::min( defn->GetFieldCount(), 8 );
        for ( int i = 0; i < count; ++i )
          fieldIndices.push_back( i );
      }

      const Json::Value::Int64 total = layer->GetFeatureCount( /*force=*/false );
      layer->ResetReading();
      Json::Value features( Json::arrayValue );
      while ( static_cast<int>( features.size() ) < limit )
      {
        OGRFeatureUniquePtr feature = layer->GetNextFeature();
        if ( !feature )
          break;
        Json::Value f( Json::objectValue );
        f["id"] = feature->GetFID();
        Json::Value attrs( Json::objectValue );
        for ( int idx : fieldIndices )
        {
          const OGRFieldDefn *fieldDef = defn->GetFieldDefn( idx );
          if ( !fieldDef )
            continue;
          if ( feature->IsFieldNull( idx ) )
          {
            attrs[fieldDef->GetNameRef()] = Json::Value();
          }
          else if ( fieldDef->GetType() == OFTInteger )
          {
            attrs[fieldDef->GetNameRef()] = feature->GetFieldAsInteger( idx );
          }
          else if ( fieldDef->GetType() == OFTInteger64 )
          {
            attrs[fieldDef->GetNameRef()] = static_cast<Json::Int64>( feature->GetFieldAsInteger64( idx ) );
          }
          else if ( fieldDef->GetType() == OFTReal )
          {
            attrs[fieldDef->GetNameRef()] = feature->GetFieldAsDouble( idx );
          }
          else
          {
            attrs[fieldDef->GetNameRef()] = feature->GetFieldAsString( idx );
          }
        }
        f["attributes"] = attrs;
        const OGRGeometry *geometry = feature->GetGeometryRef();
        if ( geometry && !geometry->IsEmpty() )
        {
          OGREnvelope env;
          geometry->getEnvelope( &env );
          Json::Value bbox( Json::objectValue );
          bbox["minx"] = env.MinX;
          bbox["miny"] = env.MinY;
          bbox["maxx"] = env.MaxX;
          bbox["maxy"] = env.MaxY;
          f["bbox"] = bbox;
        }
        features.append( f );
      }

      Json::Value out( Json::objectValue );
      out["path"] = path;
      out["feature_count"] = static_cast<Json::Int64>( total );
      out["sampled"] = static_cast<Json::Int>( features.size() );
      out["features"] = features;
      return SpatialToolResult::ok( out );
    }
};

} // namespace

// ---------------------------------------------------------------------------
// spatial:compare_rasters
// ---------------------------------------------------------------------------

namespace {

/// Decimated read of one band into doubles (bounded grid).
bool decimatedRead( GDALRasterBand *band, int bufW, int bufH, std::vector<double> &out,
                    double *nodataOut, bool *hasNodata )
{
  const int w = band->GetXSize();
  const int h = band->GetYSize();
  out.resize( static_cast<size_t>( bufW ) * bufH );
  if ( band->RasterIO( GF_Read, 0, 0, w, h, out.data(), bufW, bufH, GDT_Float64, 0, 0 ) != CE_None )
    return false;
  int hasNoData = 0;
  *nodataOut = band->GetNoDataValue( &hasNoData );
  *hasNodata = hasNoData != 0;
  return true;
}

class CompareRastersTool final : public SpatialTool
{
  public:
    std::string name() const override { return "spatial:compare_rasters"; }
    std::string displayName() const override { return "Compare rasters"; }
    std::string description() const override
    {
      return "Bounded difference verdict between two rasters (e.g. change-detection result vs "
             "truth, or two processing variants): grid compatibility (size/CRS/pixel size), "
             "decimated-difference mean/max absolute deviation, nodata ratios. Verdict: "
             "identical | within_tolerance | different | incomparable.";
    }
    std::vector<std::string> tags() const override
    {
      return { "spatial", "raster", "compare", "difference", "verify" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value a( Json::objectValue );
      a["type"] = "string";
      a["description"] = "Path of raster A";
      props["a"] = a;
      Json::Value b( Json::objectValue );
      b["type"] = "string";
      b["description"] = "Path of raster B";
      props["b"] = b;
      Json::Value tol( Json::objectValue );
      tol["type"] = "number";
      tol["description"] = "Absolute tolerance for 'within_tolerance' (default 0.0)";
      props["tolerance"] = tol;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "a" );
      required.append( "b" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["verdict"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string pathA = input.isMember( "a" ) && input["a"].isString() ? input["a"].asString() : "";
      const std::string pathB = input.isMember( "b" ) && input["b"].isString() ? input["b"].asString() : "";
      if ( pathA.empty() || pathB.empty() )
        return SpatialToolResult::failure( "Missing required parameters: a, b", "INVALID_PARAMETER",
                                           "validation" );
      const double tolerance = input.isMember( "tolerance" ) && input["tolerance"].isNumeric()
                                 ? input["tolerance"].asDouble()
                                 : 0.0;

      GDALDatasetUniquePtr dsA;
      if ( SpatialToolResult open = openRaster( pathA, dsA ); !open.success )
        return open;
      GDALDatasetUniquePtr dsB;
      if ( SpatialToolResult open = openRaster( pathB, dsB ); !open.success )
        return open;

      Json::Value out( Json::objectValue );
      out["a"] = pathA;
      out["b"] = pathB;

      // Grid compatibility.
      const bool sameSize = dsA->GetRasterXSize() == dsB->GetRasterXSize() &&
                            dsA->GetRasterYSize() == dsB->GetRasterYSize();
      bool sameCrs = true;
      const OGRSpatialReference *srsA = dsA->GetSpatialRef();
      const OGRSpatialReference *srsB = dsB->GetSpatialRef();
      if ( srsA && srsB )
        sameCrs = srsA->IsSame( srsB );
      else
        sameCrs = ( srsA == srsB ); // both null → "same" (both undefined)
      Json::Value grid( Json::objectValue );
      grid["same_size"] = sameSize;
      grid["same_crs"] = sameCrs;
      out["grid"] = grid;
      if ( !sameSize )
      {
        out["verdict"] = "incomparable";
        Json::Value detail( Json::objectValue );
        detail["a"] = Json::Value( Json::objectValue );
        detail["a"]["width"] = dsA->GetRasterXSize();
        detail["a"]["height"] = dsA->GetRasterYSize();
        detail["b"] = Json::Value( Json::objectValue );
        detail["b"]["width"] = dsB->GetRasterXSize();
        detail["b"]["height"] = dsB->GetRasterYSize();
        out["sizes"] = detail;
        return SpatialToolResult::ok( out );
      }

      // Bounded decimated read of band 1 of each.
      const int w = dsA->GetRasterXSize();
      const int h = dsA->GetRasterYSize();
      int bufW = w;
      int bufH = h;
      if ( w > kMaxCompareSamples || h > kMaxCompareSamples )
      {
        const double scale = std::min( static_cast<double>( kMaxCompareSamples ) / w,
                                       static_cast<double>( kMaxCompareSamples ) / h );
        bufW = std::max( 1, static_cast<int>( w * scale ) );
        bufH = std::max( 1, static_cast<int>( h * scale ) );
      }
      GDALRasterBand *bandA = dsA->GetRasterBand( 1 );
      GDALRasterBand *bandB = dsB->GetRasterBand( 1 );
      if ( !bandA || !bandB )
      {
        out["verdict"] = "incomparable";
        out["reason"] = "missing band 1";
        return SpatialToolResult::ok( out );
      }
      std::vector<double> pixelsA;
      std::vector<double> pixelsB;
      double nodataA = 0.0;
      double nodataB = 0.0;
      bool hasNodataA = false;
      bool hasNodataB = false;
      if ( !decimatedRead( bandA, bufW, bufH, pixelsA, &nodataA, &hasNodataA ) ||
           !decimatedRead( bandB, bufW, bufH, pixelsB, &nodataB, &hasNodataB ) )
        return SpatialToolResult::failure( "Decimated read failed", "IO_ERROR", "io", true );

      double diffSum = 0.0;
      double maxAbsDiff = 0.0;
      size_t compared = 0;
      size_t skipped = 0;
      size_t invalidA = 0;
      size_t invalidB = 0;
      const size_t n = pixelsA.size();
      for ( size_t i = 0; i < n; ++i )
      {
        const double va = pixelsA[i];
        const double vb = pixelsB[i];
        const bool badA = !std::isfinite( va ) || ( hasNodataA && va == nodataA );
        const bool badB = !std::isfinite( vb ) || ( hasNodataB && vb == nodataB );
        if ( badA )
          ++invalidA;
        if ( badB )
          ++invalidB;
        if ( badA || badB )
        {
          ++skipped;
          continue;
        }
        const double diff = vb - va;
        diffSum += diff;
        maxAbsDiff = std::max( maxAbsDiff, std::fabs( diff ) );
        ++compared;
      }

      Json::Value diff( Json::objectValue );
      diff["mean"] = compared > 0 ? diffSum / compared : 0.0;
      diff["max_abs"] = maxAbsDiff;
      diff["samples_compared"] = static_cast<Json::UInt64>( compared );
      diff["samples_skipped"] = static_cast<Json::UInt64>( skipped );
      diff["approximate"] = bufW != w || bufH != h;
      out["difference"] = diff;

      Json::Value nodata( Json::objectValue );
      nodata["a_ratio"] = n > 0 ? static_cast<double>( invalidA ) / n : 0.0;
      nodata["b_ratio"] = n > 0 ? static_cast<double>( invalidB ) / n : 0.0;
      out["nodata_ratio"] = nodata;

      if ( compared == 0 )
        out["verdict"] = "incomparable";
      else if ( maxAbsDiff == 0.0 )
        out["verdict"] = "identical";
      else if ( maxAbsDiff <= tolerance )
        out["verdict"] = "within_tolerance";
      else
        out["verdict"] = "different";
      return SpatialToolResult::ok( out );
    }
};

} // namespace

void registerSampleTools()
{
  static const bool registered = [] {
    SpatialToolRegistry::instance().registerTool( std::make_shared<SamplePixelsTool>() );
    SpatialToolRegistry::instance().registerTool( std::make_shared<SampleFeaturesTool>() );
    SpatialToolRegistry::instance().registerTool( std::make_shared<CompareRastersTool>() );
    return true;
  }();
  Q_UNUSED( registered );
}

} // namespace sicnu::agent::spatial_tools
