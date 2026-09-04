// src/agent/spatial_tools/result_assessment_tool.cpp
#include "result_assessment_tool.h"

#include "../contracts/spatial_contracts.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <QFileInfo>
#include <QString>

#include "qgsdatasourceresolver.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sicnu::agent::spatial_tools {

using namespace sicnu::agent::contracts;

namespace {

constexpr int kAssessSamples = 512;
constexpr double kConstantStddevEps = 1e-12;
constexpr double kDefaultNodataRatioCap = 0.95;

struct BandSummary
{
  double min = 0.0;
  double max = 0.0;
  double mean = 0.0;
  double stddev = 0.0;
  double validRatio = 0.0;   // non-nodata fraction of the sampled grid
  bool approximate = false;
  std::vector<double> uniqueValues; // small-value categorical sample (capped)
};

/// Bounded decimated statistics in the band's native loop (double accumulate).
BandSummary summarizeBand( GDALRasterBand *band )
{
  BandSummary summary;
  const int w = band->GetXSize();
  const int h = band->GetYSize();
  int bufW = w;
  int bufH = h;
  if ( w > kAssessSamples || h > kAssessSamples )
  {
    const double scale = std::min( static_cast<double>( kAssessSamples ) / w,
                                   static_cast<double>( kAssessSamples ) / h );
    bufW = std::max( 1, static_cast<int>( w * scale ) );
    bufH = std::max( 1, static_cast<int>( h * scale ) );
  }
  summary.approximate = bufW != w || bufH != h;

  std::vector<double> buf( static_cast<size_t>( bufW ) * bufH );
  if ( band->RasterIO( GF_Read, 0, 0, w, h, buf.data(), bufW, bufH, GDT_Float64, 0, 0 ) != CE_None )
    return summary;

  int hasNoData = 0;
  const double nodata = band->GetNoDataValue( &hasNoData );
  double sum = 0.0;
  double sumSq = 0.0;
  summary.min = std::numeric_limits<double>::infinity();
  summary.max = -std::numeric_limits<double>::infinity();
  size_t valid = 0;
  constexpr size_t kMaxUnique = 32;
  for ( const double v : buf )
  {
    if ( !std::isfinite( v ) || ( hasNoData && v == nodata ) )
      continue;
    sum += v;
    sumSq += v * v;
    summary.min = std::min( summary.min, v );
    summary.max = std::max( summary.max, v );
    if ( summary.uniqueValues.size() < kMaxUnique &&
         std::find( summary.uniqueValues.begin(), summary.uniqueValues.end(), v ) ==
           summary.uniqueValues.end() )
      summary.uniqueValues.push_back( v );
    ++valid;
  }
  if ( valid == 0 )
    return summary;
  summary.mean = sum / valid;
  const double variance = std::max( 0.0, sumSq / valid - summary.mean * summary.mean );
  summary.stddev = std::sqrt( variance );
  summary.validRatio = static_cast<double>( valid ) / buf.size();
  return summary;
}

} // namespace

class AssessResultTool final : public SpatialTool
{
  public:
    std::string name() const override { return "spatial:assess_result"; }
    std::string displayName() const override { return "Assess execution result"; }
    std::string description() const override
    {
      return "Scientific sanity check for an execution output (raster or vector) BEFORE trusting "
             "a success status: nodata ratio, suspicious constant output, value range vs "
             "expectations, class count vs expected classes, CRS presence, empty output. "
             "Input: {target, expectations?: {value_min?, value_max?, max_nodata_ratio?, "
             "expected_classes?}, provenance?: {...}}. Returns a ResultAssessment envelope with "
             "per-check pass/fail and an overall pass|warn|fail verdict.";
    }
    std::vector<std::string> tags() const override
    {
      return { "spatial", "verify", "assessment", "quality", "result" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value target( Json::objectValue );
      target["type"] = "string";
      target["description"] = "Output raster/vector path or workspace asset reference";
      props["target"] = target;
      Json::Value expectations( Json::objectValue );
      expectations["type"] = "object";
      expectations["description"] = "Optional expectations: value_min, value_max, max_nodata_ratio, expected_classes";
      props["expectations"] = expectations;
      Json::Value provenance( Json::objectValue );
      provenance["type"] = "object";
      provenance["description"] = "Optional provenance echo (algorithm, params, inputs) merged into the assessment";
      props["provenance"] = provenance;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "target" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["kind"] = Json::Value( Json::objectValue );
      schema["properties"]["verdict"] = Json::Value( Json::objectValue );
      schema["properties"]["checks"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString target = requireStringField( input, "target", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      const std::string targetPath = target.toStdString();
      if ( QgsDataSourceResolver::requiresLocalExistenceCheck( target ) &&
           !QFileInfo::exists( target ) )
        return SpatialToolResult::failure( "Target not found: " + targetPath, "NOT_FOUND", "io", false );

      const Json::Value &expectations = input.get( "expectations", Json::Value( Json::objectValue ) );
      const Json::Value &provenance = input.get( "provenance", Json::Value( Json::objectValue ) );

      std::vector<Json::Value> checks;

      // ---- Raster path ---------------------------------------------------
      GDALDatasetUniquePtr ds( GDALDataset::Open( targetPath.c_str(),
                                                  GDAL_OF_RASTER | GDAL_OF_READONLY ) );
      if ( ds )
      {
        const int bandCount = std::max( 1, ds->GetRasterCount() );
        GDALRasterBand *band = ds->GetRasterBand( 1 );
        const BandSummary summary = summarizeBand( band );
        if ( !band )
          return SpatialToolResult::failure( "No band 1 in target", "IO_ERROR", "io", false );

        Json::Value stats( Json::objectValue );
        stats["min"] = summary.min;
        stats["max"] = summary.max;
        stats["mean"] = summary.mean;
        stats["stddev"] = summary.stddev;
        stats["valid_ratio"] = summary.validRatio;
        stats["approximate"] = summary.approximate;

        // 1. Empty output: nothing valid in the sampled grid.
        const bool empty = summary.validRatio <= 0.0;
        checks.push_back( makeAssessmentCheck( "empty_output", !empty, empty ? "error" : "info",
                                               empty ? "RESULT_EMPTY" : "", stats ) );

        // 2. Nodata ratio vs cap.
        const double nodataRatio = 1.0 - summary.validRatio;
        const double nodataCap = expectations.isMember( "max_nodata_ratio" ) &&
                                     expectations["max_nodata_ratio"].isNumeric()
                                   ? expectations["max_nodata_ratio"].asDouble()
                                   : kDefaultNodataRatioCap;
        {
          Json::Value details( Json::objectValue );
          details["actual"] = nodataRatio;
          details["cap"] = nodataCap;
          const bool ok = nodataRatio <= nodataCap;
          checks.push_back( makeAssessmentCheck( "nodata_ratio", ok, ok ? "info" : "warning",
                                                 ok ? "" : "RESULT_NODATA_RATIO", details ) );
        }

        // 3. Constant-output suspicion (valid data with zero variance).
        if ( !empty )
        {
          const bool constant = summary.stddev <= kConstantStddevEps;
          checks.push_back( makeAssessmentCheck( "constant_output", !constant,
                                                 constant ? "warning" : "info",
                                                 constant ? "RESULT_CONSTANT" : "", stats ) );
        }

        // 4. Value range vs expectations.
        if ( expectations.isMember( "value_min" ) || expectations.isMember( "value_max" ) )
        {
          Json::Value details( Json::objectValue );
          details["actual_min"] = summary.min;
          details["actual_max"] = summary.max;
          bool inRange = true;
          if ( expectations.isMember( "value_min" ) )
          {
            details["expected_min"] = expectations["value_min"];
            inRange = inRange && summary.min >= expectations["value_min"].asDouble() - 1e-9;
          }
          if ( expectations.isMember( "value_max" ) )
          {
            details["expected_max"] = expectations["value_max"];
            inRange = inRange && summary.max <= expectations["value_max"].asDouble() + 1e-9;
          }
          checks.push_back( makeAssessmentCheck( "value_range", inRange, inRange ? "info" : "error",
                                                 inRange ? "" : "RESULT_RANGE_VIOLATION", details ) );
        }

        // 5. Class count for categorical outputs.
        if ( expectations.isMember( "expected_classes" ) && expectations["expected_classes"].isInt() )
        {
          Json::Value details( Json::objectValue );
          const int expected = expectations["expected_classes"].asInt();
          const int observed = static_cast<int>( summary.uniqueValues.size() );
          details["expected_classes"] = expected;
          details["observed_distinct_capped"] = observed;
          details["distinct_capped_at"] = 32;
          const bool ok = observed <= expected; // fewer distinct than expected ⇒ suspicious
          checks.push_back( makeAssessmentCheck( "class_count", ok, ok ? "info" : "warning",
                                                 ok ? "" : "RESULT_CLASS_COUNT", details ) );
        }

        // 6. CRS presence.
        const OGRSpatialReference *srs = ds->GetSpatialRef();
        const bool hasCrs = srs != nullptr;
        checks.push_back( makeAssessmentCheck( "crs_present", hasCrs, hasCrs ? "info" : "warning",
                                               hasCrs ? "" : "RESULT_NO_CRS", Json::Value() ) );

        Json::Value body( Json::objectValue );
        body["stats"] = stats;
        body["band_count"] = bandCount;

        Json::Value checksJson( Json::arrayValue );
        for ( const auto &check : checks )
          checksJson.append( check );
        std::string verdict = "pass";
        for ( const auto &check : checksJson )
        {
          if ( !check["passed"].asBool() && check["severity"].asString() == "error" )
            verdict = "fail";
          else if ( !check["passed"].asBool() && verdict == "pass" )
            verdict = "warn";
        }
        return SpatialToolResult::ok( makeResultAssessment( targetPath, verdict, checksJson,
                                                            provenance ) );
      }

      // ---- Vector path ---------------------------------------------------
      ds.reset( GDALDataset::Open( targetPath.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY ) );
      if ( ds )
      {
        OGRLayer *layer = ds->GetLayer( 0 );
        if ( !layer )
          return SpatialToolResult::failure( "No vector layer in target", "IO_ERROR", "io", false );

        const GIntBig featureCount = layer->GetFeatureCount( /*force=*/false );
        {
          Json::Value details( Json::objectValue );
          details["feature_count"] = static_cast<Json::Int64>( featureCount );
          const bool empty = featureCount == 0;
          checks.push_back( makeAssessmentCheck( "empty_output", !empty, empty ? "error" : "info",
                                                 empty ? "RESULT_EMPTY" : "", details ) );
        }

        // Geometry validity (bounded probe: first 100 features).
        layer->ResetReading();
        int inspected = 0;
        int invalidGeometry = 0;
        while ( inspected < 100 )
        {
          OGRFeatureUniquePtr feature = layer->GetNextFeature();
          if ( !feature )
            break;
          const OGRGeometry *geometry = feature->GetGeometryRef();
          ++inspected;
          if ( geometry && !geometry->IsValid() )
            ++invalidGeometry;
        }
        {
          Json::Value details( Json::objectValue );
          details["inspected"] = inspected;
          details["invalid"] = invalidGeometry;
          const bool ok = invalidGeometry == 0;
          checks.push_back( makeAssessmentCheck( "geometry_validity", ok, ok ? "info" : "warning",
                                                 ok ? "" : "RESULT_INVALID_GEOMETRY", details ) );
        }
        const bool hasCrs = layer->GetSpatialRef() != nullptr;
        checks.push_back( makeAssessmentCheck( "crs_present", hasCrs, hasCrs ? "info" : "warning",
                                               hasCrs ? "" : "RESULT_NO_CRS", Json::Value() ) );

        Json::Value checksJson( Json::arrayValue );
        for ( const auto &check : checks )
          checksJson.append( check );
        std::string verdict = "pass";
        for ( const auto &check : checksJson )
        {
          if ( !check["passed"].asBool() && check["severity"].asString() == "error" )
            verdict = "fail";
          else if ( !check["passed"].asBool() && verdict == "pass" )
            verdict = "warn";
        }
        return SpatialToolResult::ok( makeResultAssessment( targetPath, verdict, checksJson,
                                                            provenance ) );
      }

      return SpatialToolResult::failure( "Target is neither a GDAL raster nor vector: " + targetPath,
                                         "UNSUPPORTED", "validation", false );
    }
};

void registerResultAssessmentTool()
{
  static const bool registered = [] {
    SpatialToolRegistry::instance().registerTool( std::make_shared<AssessResultTool>() );
    return true;
  }();
  Q_UNUSED( registered );
}

} // namespace sicnu::agent::spatial_tools
