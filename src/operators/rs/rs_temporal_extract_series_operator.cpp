// src/operators/rs/rs_temporal_extract_series_operator.cpp
#include "rs_temporal_extract_series_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "processing/algorithms/temporal/temporal_stats.h"
#include "processing/algorithms/temporal/temporal_stream.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFile>
#include <QPointF>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace sicnu::operators::rs
{

using namespace params;
using temporal::TemporalTileReader;

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

struct MapPoint
{
  double x = 0.0;
  double y = 0.0;
};

/// Even-odd ray casting on pixel centers (map coordinates).
bool pointInPolygon( double px, double py, const std::vector<MapPoint> &poly )
{
  bool inside = false;
  const size_t n = poly.size();
  for ( size_t i = 0, j = n - 1; i < n; j = i++ )
  {
    const double xi = poly[i].x, yi = poly[i].y;
    const double xj = poly[j].x, yj = poly[j].y;
    if ( ( yi > py ) != ( yj > py ) &&
         px < ( xj - xi ) * ( py - yi ) / ( yj - yi ) + xi )
      inside = !inside;
  }
  return inside;
}

/// Map -> pixel for a north-up affine geotransform (rotation is rejected by
/// preflight's same-grid check).
bool mapToPixel( const std::array<double, 6> &gt, double mx, double my, int *col, int *row )
{
  if ( std::abs( gt[1] ) < 1e-15 || std::abs( gt[5] ) < 1e-15 )
    return false;
  if ( !std::isfinite( mx ) || !std::isfinite( my ) )
    return false;
  const double fx = ( mx - gt[0] ) / gt[1];
  const double fy = ( my - gt[3] ) / gt[5];
  if ( !std::isfinite( fx ) || !std::isfinite( fy ) )
    return false;
  *col = static_cast<int>( std::floor( fx ) );
  *row = static_cast<int>( std::floor( fy ) );
  return true;
}

std::vector<MapPoint> parsePolygon( const Json::Value &polygonJson )
{
  std::vector<MapPoint> poly;
  if ( !polygonJson.isArray() )
    return poly;
  for ( const Json::Value &pt : polygonJson )
  {
    if ( !pt.isArray() || pt.size() < 2 || !pt[0].isNumeric() || !pt[1].isNumeric() ||
         !std::isfinite( pt[0].asDouble() ) || !std::isfinite( pt[1].asDouble() ) )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "polygon entries must be finite [x, y] number pairs" );
    poly.push_back( { pt[0].asDouble(), pt[1].asDouble() } );
  }
  if ( poly.size() < 3 )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "polygon needs at least 3 vertices" );
  return poly;
}
} // namespace

std::string RsTemporalExtractSeriesOperator::description() const
{
  return "Extract a time series at a point or inside a polygon ROI from a "
         "multi-date collection. Points return (time, value, valid); ROI "
         "pixels are bounded by the polygon bounding box and summarized per "
         "date (mean/median/min/max/stddev/valid_count). Output: CSV plus the "
         "JSON series; missing observations stay missing (no interpolation).";
}

Json::Value RsTemporalExtractSeriesOperator::schema() const
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
                                         "...or path to a temporal collection descriptor JSON", "" );
  props["band"] = makeIntegerParam( "band", "Explicit analysis band (1-based)", 0 );
  props["band_role"] = makeEnumParam( "band_role", "Analysis band role",
                                      { "blue", "green", "red", "red_edge", "nir", "swir1", "swir2" },
                                      "" );
  Json::Value point = makeStringParam( "point",
                                       "Point as [x, y] in the collection CRS (mutually exclusive "
                                       "with polygon)" );
  point["type"] = "array";
  props["point"] = point;
  Json::Value polygon = makeStringParam( "polygon",
                                         "ROI as a closed [[x, y], ...] vertex ring in the "
                                         "collection CRS (map coordinates)" );
  polygon["type"] = "array";
  props["polygon"] = polygon;
  props["duplicate_policy"] = makeEnumParam( "duplicate_policy", "Duplicate acquisition instants",
                                             { "keep_all", "reject" }, "keep_all" );
  props["apply_qa_masking"] = makeBooleanParam( "apply_qa_masking", "Exclude QA/cloud-masked samples", true );
  props["output"] = makeOutputParam( "output", "Series CSV (time + value or ROI statistics)", "csv" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Series CSV", "csv" );
  outputs["series"] = makeStringParam( "series", "JSON series array in the result" );
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalExtractSeriesOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "time-series" );
  tags.append( "zonal-stats" );
  meta["tags"] = tags;
  meta["purpose"] = "Point / ROI time series (e.g. NDVI trajectory of a field) from a temporal "
                    "collection, as CSV + JSON";
  meta["prerequisites"] = "Common grid, acquisition times (temporal preflight); coordinates in "
                          "the collection CRS";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(roiPixels × scenes) for polygons, O(scenes) for points";
  meta["workflowHints"] = "ROI values bounded by the polygon bbox (never a full-scene scan); "
                          "pair with rs:temporal_index_series stacks for index trajectories";
  meta["limitations"] = "Coordinates must already be in the collection CRS (no reprojection); "
                        "ROI median materializes one scene's valid ROI samples at a time";
  return meta;
}

Json::Value RsTemporalExtractSeriesOperator::executionEstimate() const
{
  return sicnu::processing::makeStreamingEstimate( 256, 256, 1, 4, 4, 0, 1 * 1024 * 1024 );
}

namespace
{

QString formatStat( double v )
{
  return std::isfinite( v ) ? QString::number( v, 'g', 12 ) : QString();
}

} // namespace

Json::Value RsTemporalExtractSeriesOperator::run( const Json::Value &params,
                                                  RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );

  const bool hasPoint = params.isMember( "point" ) && params["point"].isArray() &&
                        params["point"].size() >= 2;
  const bool hasPolygon = params.isMember( "polygon" ) && params["polygon"].isArray();
  if ( hasPoint == hasPolygon )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "provide exactly one of 'point' [x,y] or 'polygon' [[x,y],...]" );
  MapPoint point;
  std::vector<MapPoint> polygon;
  if ( hasPoint )
  {
    if ( !params["point"][0].isNumeric() || !params["point"][1].isNumeric() ||
         !std::isfinite( params["point"][0].asDouble() ) ||
         !std::isfinite( params["point"][1].asDouble() ) )
      throw RSOperatorError( ErrorCode::InvalidParameter, "point must be finite [x, y] numbers" );
    point = { params["point"][0].asDouble(), params["point"][1].asDouble() };
  }
  else
  {
    polygon = parsePolygon( params["polygon"] );
  }

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();

  temporal::TemporalStreamOptions streamOptions;
  streamOptions.applyQaMasking = applyQaMasking;
  streamOptions.tileWidth = 256;
  streamOptions.tileHeight = 256;

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
    // documented default: explicit band > role > positional fallback > band 1
    analysisBands[s] = band > 0 ? band : 1;
    anyFallback = anyFallback || fallback;
  }
  if ( anyFallback )
    context.logWarning( "Analysis band resolved by positional fallback for at least one scene; "
                        "pass 'band' or 'bands' to pin it." );

  const auto gt = reader.geoTransform();
  const int width = reader.width();
  const int height = reader.height();

  Json::Value series( Json::arrayValue );
  QString csv;

  if ( hasPoint )
  {
    int col = 0, row = 0;
    if ( !mapToPixel( gt, point.x, point.y, &col, &row ) )
      throw RSOperatorError( ErrorCode::InvalidInputData, "raster has no usable geotransform" );
    const bool inside = col >= 0 && col < width && row >= 0 && row < height;

    csv = QStringLiteral( "time,value,valid\n" );
    for ( int s = 0; s < sceneCount; ++s )
    {
      const auto &scene = reader.scene( s );
      float v = kNan;
      if ( inside && !reader.readSceneBandPixel( s, analysisBands[s], col, row, &v ) )
        throw RSOperatorError( ErrorCode::GdalError, "pixel read failed" );
      const bool valid = inside && std::isfinite( v );
      Json::Value entry( Json::objectValue );
      entry["time"] = scene.time.valid ? scene.time.iso.toStdString() : Json::Value();
      entry["value"] = valid ? static_cast<double>( v ) : Json::Value();
      entry["valid"] = valid;
      entry["scene"] = scene.path.toStdString();
      series.append( entry );
      csv += QStringLiteral( "%1,%2,%3\n" )
                 .arg( scene.time.valid ? scene.time.iso : QString() )
                 .arg( valid ? QString::number( v, 'g', 12 ) : QString() )
                 .arg( valid ? 1 : 0 );
      context.throwIfCancelled();
      context.reportProgress( 0.1 + 0.85 * ( static_cast<double>( s + 1 ) / sceneCount ),
                              "Point series" );
    }
  }
  else
  {
    // ROI: bounding box window only (goal §26).
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for ( const MapPoint &p : polygon )
    {
      minX = std::min( minX, p.x );
      minY = std::min( minY, p.y );
      maxX = std::max( maxX, p.x );
      maxY = std::max( maxY, p.y );
    }
    int col0 = 0, row0 = 0, col1 = 0, row1 = 0;
    if ( !mapToPixel( gt, minX, maxY, &col0, &row0 ) ||
         !mapToPixel( gt, maxX, minY, &col1, &row1 ) )
      throw RSOperatorError( ErrorCode::InvalidInputData, "raster has no usable geotransform" );
    col0 = std::max( 0, col0 );
    row0 = std::max( 0, row0 );
    col1 = std::min( width - 1, col1 );
    row1 = std::min( height - 1, row1 );
    const int winW = col1 - col0 + 1;
    const int winH = row1 - row0 + 1;
    if ( winW <= 0 || winH <= 0 )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "polygon does not intersect the raster extent" );

    // ROI memory contract: the bbox window is streamed in row bands (never a
    // scene-sized allocation) and the exact per-date median materializes at
    // most kMaxRoiPixels samples per date.
    constexpr std::uint64_t kMaxRoiPixels = 16ull * 1024 * 1024; // 64 MB of floats
    const std::uint64_t bboxPixels = static_cast<std::uint64_t>( winW ) * winH;
    if ( bboxPixels > kMaxRoiPixels )
      throw RSOperatorError(
          ErrorCode::InvalidParameter,
          "ROI bounding box covers " + std::to_string( bboxPixels ) +
              " pixels (limit " + std::to_string( kMaxRoiPixels ) +
              "); use a smaller ROI or derive per-pixel rasters (rs:temporal_summary) "
              "instead of in-memory ROI statistics" );

    std::vector<std::uint8_t> inside( static_cast<size_t>( winW ) * winH, 0 );
    std::uint64_t roiPixels = 0;
    for ( int r = 0; r < winH; ++r )
    {
      context.throwIfCancelled();
      const double py = gt[3] + ( row0 + r + 0.5 ) * gt[5];
      for ( int c = 0; c < winW; ++c )
      {
        const double px = gt[0] + ( col0 + c + 0.5 ) * gt[1];
        if ( pointInPolygon( px, py, polygon ) )
        {
          inside[static_cast<size_t>( r ) * winW + c] = 1;
          ++roiPixels;
        }
      }
    }
    if ( roiPixels == 0 )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "polygon covers no pixel center inside the raster" );

    csv = QStringLiteral( "time,mean,median,min,max,stddev,valid_count,roi_pixels\n" );
    csv += QStringLiteral( "# roi_pixels=%1 stddev=population(N)\n" ).arg( roiPixels );

    // Row-band streaming: bounded window buffer regardless of ROI height.
    const int bandHeight = std::max( 1, std::min( 256, 65536 / std::max( 1, winW ) ) );
    std::vector<float> window( static_cast<size_t>( winW ) * bandHeight );
    std::vector<float> roiValues;
    roiValues.reserve( roiPixels );

    for ( int s = 0; s < sceneCount; ++s )
    {
      const auto &scene = reader.scene( s );
      temporal::stats::WelfordAccumulator acc;
      float mn = std::numeric_limits<float>::infinity();
      float mx = -std::numeric_limits<float>::infinity();
      roiValues.clear();
      for ( int row = 0; row < winH; row += bandHeight )
      {
        context.throwIfCancelled();
        const int rows = std::min( bandHeight, winH - row );
        if ( !reader.readSceneBandWindow( s, analysisBands[s], col0, row0 + row, winW, rows,
                                          window.data() ) )
          throw RSOperatorError( ErrorCode::GdalError, "ROI window read failed" );
        for ( int r = 0; r < rows; ++r )
        {
          const auto *rowFlags = &inside[static_cast<size_t>( row + r ) * winW];
          const float *rowValues = &window[static_cast<size_t>( r ) * winW];
          for ( int c = 0; c < winW; ++c )
          {
            if ( !rowFlags[c] )
              continue;
            const float v = rowValues[c];
            if ( !std::isfinite( v ) )
              continue;
            acc.add( v );
            mn = std::min( mn, v );
            mx = std::max( mx, v );
            roiValues.push_back( v );
          }
        }
      }

      Json::Value entry( Json::objectValue );
      entry["time"] = scene.time.valid ? scene.time.iso.toStdString() : Json::Value();
      entry["scene"] = scene.path.toStdString();
      if ( acc.n > 0 )
      {
        const size_t cnt = roiValues.size();
        std::nth_element( roiValues.begin(), roiValues.begin() + ( cnt - 1 ) / 2,
                          roiValues.end() );
        double median = roiValues[( cnt - 1 ) / 2];
        if ( cnt % 2 == 0 )
        {
          const float lower = *std::max_element( roiValues.begin(), roiValues.begin() + cnt / 2 );
          const float upper = *std::min_element( roiValues.begin() + cnt / 2, roiValues.end() );
          median = 0.5 * ( lower + upper );
        }
        // population stddev (N denominator) — same convention as rs:temporal_summary
        entry["mean"] = acc.mean;
        entry["median"] = median;
        entry["min"] = mn;
        entry["max"] = mx;
        entry["stddev"] = acc.populationStddev();
        entry["stddev_estimator"] = "population";
        entry["valid_count"] = Json::Value::UInt64( acc.n );
        csv += QStringLiteral( "%1,%2,%3,%4,%5,%6,%7\n" )
                   .arg( scene.time.valid ? scene.time.iso : QString() )
                   .arg( formatStat( acc.mean ) )
                   .arg( formatStat( median ) )
                   .arg( formatStat( mn ) )
                   .arg( formatStat( mx ) )
                   .arg( formatStat( acc.populationStddev() ) )
                   .arg( acc.n );
      }
      else
      {
        csv += QStringLiteral( "%1,,,,,,0\n" ).arg( scene.time.valid ? scene.time.iso : QString() );
      }
      entry["roi_pixels"] = Json::Value::UInt64( roiPixels );
      series.append( entry );
      context.reportProgress( 0.1 + 0.85 * ( static_cast<double>( s + 1 ) / sceneCount ),
                              "ROI series" );
    }
  }

  {
    const QString outPath = QString::fromStdString( outputPath );
    QFile f( outPath );
    if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
      throw RSOperatorError( ErrorCode::FileNotWritable,
                             "cannot write CSV: " + outputPath );
    QTextStream ts( &f );
    ts << csv;
    ts.flush(); // surface write errors now, not in the destructor
    if ( ts.status() != QTextStream::Ok )
    {
      ts.resetStatus();
      f.close();
      QFile::remove( outPath ); // no truncated half-successful CSV
      throw RSOperatorError( ErrorCode::FileNotWritable, "CSV write failed" );
    }
  }

  Json::Value result( Json::objectValue );
  result["output"] = outputPath;
  result["series"] = series;
  result["mode"] = hasPoint ? "point" : "roi";
  result["sceneCount"] = sceneCount;
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  result["memory"] = Json::Value( Json::objectValue );
  result["memory"]["peakReaderSlots"] = Json::Value::UInt64( reader.peakSlots() );
  context.reportProgress( 1.0, "Series extraction complete" );
  return result;
}

} // namespace sicnu::operators::rs
