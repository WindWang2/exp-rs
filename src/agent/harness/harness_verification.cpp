// src/agent/harness/harness_verification.cpp
#include "harness_verification.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <cmath>
#include <set>

#include <QFileInfo>

namespace sicnu::agent::harness {

namespace {

constexpr int kMaxProbeSamples = 64;          // bounded read grid per dimension
constexpr int kMaxClassProbeUnique = 4096;    // unique-value cap for class checks

void addCheck( std::vector<VerificationCheck> &checks, const std::string &name, bool passed,
               const std::string &code, const std::string &severity = "info",
               Json::Value details = Json::Value() )
{
  VerificationCheck check;
  check.check = name;
  check.passed = passed;
  check.severity = passed ? severity : "error";
  if ( !passed && !code.empty() )
    check.code = code;
  check.details = std::move( details );
  checks.push_back( std::move( check ) );
}

/// Detects a provenance sidecar: <path>.provenance.json (workflow_runtime
/// convention) or a governance derivation record when the asset is registered.
bool provenanceSidecarExists( const std::string &path )
{
  return QFileInfo::exists( QString::fromStdString( path + ".provenance.json" ) );
}

/// Harness 7.0 uncertainty-path convention (mission Area F/G): recipes may
/// declare an uncertainty companion product; the default writer puts it at
/// <path>.uncertainty.json.
bool uncertaintySidecarExists( const std::string &path )
{
  return QFileInfo::exists( QString::fromStdString( path + ".uncertainty.json" ) );
}

Verdict verdictFromChecks( const std::vector<VerificationCheck> &checks )
{
  bool anyError = false;
  bool anyWarning = false;
  for ( const VerificationCheck &check : checks )
  {
    if ( check.passed )
      continue;
    if ( check.severity == "error" )
      anyError = true;
    else
      anyWarning = true;
  }
  if ( anyError )
    return Verdict::Fail;
  return anyWarning ? Verdict::PassWithWarnings : Verdict::Pass;
}

std::string inferKind( const std::string &path )
{
  const QString lowered =
    QString::fromStdString( path ).toLower();
  if ( lowered.endsWith( QStringLiteral( ".gpkg" ) ) ||
       lowered.endsWith( QStringLiteral( ".shp" ) ) ||
       lowered.endsWith( QStringLiteral( ".geojson" ) ) )
    return "vector";
  return "raster";
}

} // namespace

std::string verdictToString( Verdict verdict )
{
  switch ( verdict )
  {
    case Verdict::Pass:
      return "pass";
    case Verdict::PassWithWarnings:
      return "pass_with_warnings";
    case Verdict::Fail:
      return "fail";
  }
  return "fail";
}

const char *verdictToStringWire( Verdict verdict )
{
  switch ( verdict )
  {
    case Verdict::Pass:
      return "PASS";
    case Verdict::PassWithWarnings:
      return "PASS_WITH_WARNINGS";
    case Verdict::Fail:
      return "FAIL";
  }
  return "FAIL";
}

Json::Value ArtifactVerification::toJson() const
{
  Json::Value v( Json::objectValue );
  v["path"] = path;
  v["kind"] = kind;
  v["verdict"] = verdictToStringWire( verdict );
  Json::Value arr( Json::arrayValue );
  for ( const VerificationCheck &check : checks )
  {
    Json::Value entry( Json::objectValue );
    entry["check"] = check.check;
    entry["passed"] = check.passed;
    entry["severity"] = check.severity;
    if ( !check.code.empty() )
      entry["code"] = check.code;
    if ( check.details.isObject() && !check.details.empty() )
      entry["details"] = check.details;
    arr.append( entry );
  }
  v["checks"] = arr;
  return v;
}

ArtifactVerification verifyArtifact( const std::string &path,
                                     const VerificationExpectations &expectations )
{
  ArtifactVerification result;
  result.path = path;
  result.kind = expectations.kind.empty() ? inferKind( path ) : expectations.kind;

  const QString qpath = QString::fromStdString( path );
  if ( path.empty() )
  {
    addCheck( result.checks, "artifact_exists", false, error_codes::kOutputInvalid );
    result.verdict = Verdict::Fail;
    return result;
  }
  addCheck( result.checks, "artifact_exists", QFileInfo::exists( qpath ),
            error_codes::kOutputInvalid );

  if ( result.kind == "vector" )
  {
    OGRRegisterAll();
    std::unique_ptr<GDALDataset> ds(
      GDALDataset::Open( qpath.toUtf8().constData(), GDAL_OF_VECTOR ) );
    const bool opened = ds != nullptr;
    addCheck( result.checks, "opens", opened, error_codes::kOutputInvalid );
    if ( opened )
    {
      OGRLayer *layer = ds->GetLayer( 0 );
      const bool hasLayer = layer != nullptr;
      addCheck( result.checks, "layer_present", hasLayer, error_codes::kOutputInvalid );
      if ( hasLayer )
      {
        const long long count = static_cast<long long>( layer->GetFeatureCount( FALSE ) );
        Json::Value details;
        details["feature_count"] = static_cast<Json::Int64>( count );
        const bool nonEmpty = !expectations.requireNonEmpty || count > 0;
        addCheck( result.checks, "non_empty", nonEmpty, error_codes::kOutputInvalid,
                  "info", details );
        if ( const char *authid = layer->GetSpatialRef()
                                    ? layer->GetSpatialRef()->GetAuthorityName( nullptr )
                                    : nullptr )
        {
          addCheck( result.checks, "crs_present", true, "" );
          if ( !expectations.crs.empty() )
          {
            const std::string found = std::string( authid ) + ":" +
                                      ( layer->GetSpatialRef()->GetAuthorityCode( nullptr )
                                          ? layer->GetSpatialRef()->GetAuthorityCode( nullptr )
                                          : "" );
            addCheck( result.checks, "crs_matches", found == expectations.crs,
                      error_codes::kCrsMismatch );
          }
        }
        else
        {
          addCheck( result.checks, "crs_present", false, error_codes::kOutputInvalid );
        }
      }
    }
  }
  else
  {
    static const bool kDrivers = [] { GDALAllRegister(); return true; }();
    ( void )kDrivers;
    std::unique_ptr<GDALDataset> ds(
      GDALDataset::Open( qpath.toUtf8().constData(), GDAL_OF_RASTER ) );
    const bool opened = ds != nullptr;
    addCheck( result.checks, "opens", opened, error_codes::kOutputInvalid );
    if ( opened )
    {
      Json::Value dims;
      dims["width"] = ds->GetRasterXSize();
      dims["height"] = ds->GetRasterYSize();
      const bool dimsOk = ds->GetRasterXSize() > 0 && ds->GetRasterYSize() > 0 &&
                          ds->GetRasterCount() > 0;
      addCheck( result.checks, "dimensions", dimsOk, error_codes::kOutputInvalid, "info", dims );

      if ( expectations.width )
        addCheck( result.checks, "width_matches",
                  expectations.width == ds->GetRasterXSize(), error_codes::kGridMismatch );
      if ( expectations.height )
        addCheck( result.checks, "height_matches",
                  expectations.height == ds->GetRasterYSize(), error_codes::kGridMismatch );

      const char *proj = ds->GetProjectionRef();
      const bool crsPresent = proj && *proj;
      addCheck( result.checks, "crs_present", crsPresent, error_codes::kOutputInvalid );
      if ( crsPresent && !expectations.crs.empty() )
      {
        char *wkt = nullptr;
        std::string projection = proj;
        OGRSpatialReference srs;
        srs.importFromWkt( projection.c_str() );
        const char *code = srs.GetAuthorityCode( nullptr );
        const char *name = srs.GetAuthorityName( nullptr );
        const std::string found =
          ( name && code ) ? std::string( name ) + ":" + code : std::string();
        Json::Value details;
        details["expected"] = expectations.crs;
        details["found"] = found;
        addCheck( result.checks, "crs_matches", found == expectations.crs,
                  error_codes::kCrsMismatch, "info", details );
      }

      // Harness 7.0 (Area F): expected-extent coverage. The output extent
      // (geotransform) must cover the declared AOI rectangle; a shortfall
      // means the product does not answer the question it was run for.
      if ( expectations.expectedExtent.isObject() &&
           expectations.expectedExtent.isMember( "xmin" ) )
      {
        double gt[6] = { 0, 1, 0, 0, 0, 1 };
        const bool hasTransform = ds->GetGeoTransform( gt ) == CE_None;
        const bool extentUsable = hasTransform && gt[2] == 0 && gt[4] == 0;
        if ( extentUsable )
        {
          const double xmin = gt[0];
          const double ymax = gt[3];
          const double xmax = gt[0] + gt[1] * ds->GetRasterXSize();
          const double ymin = gt[3] + gt[5] * ds->GetRasterYSize();
          const double aoiXmin = expectations.expectedExtent.get( "xmin", 0 ).asDouble();
          const double aoiYmin = expectations.expectedExtent.get( "ymin", 0 ).asDouble();
          const double aoiXmax = expectations.expectedExtent.get( "xmax", 0 ).asDouble();
          const double aoiYmax = expectations.expectedExtent.get( "ymax", 0 ).asDouble();
          constexpr double kEpsilon = 1e-6;
          Json::Value details;
          details["output"] = [ = ] {
            Json::Value v( Json::arrayValue );
            v.append( xmin ); v.append( ymin ); v.append( xmax ); v.append( ymax );
            return v;
          }();
          details["expected"] = expectations.expectedExtent;
          const bool covers = xmin <= aoiXmin + kEpsilon && ymin <= aoiYmin + kEpsilon &&
                              xmax >= aoiXmax - kEpsilon && ymax >= aoiYmax - kEpsilon;
          addCheck( result.checks, "extent_covers_aoi", covers,
                    error_codes::kOutputInvalid, "info", details );
        }
        else
        {
          Json::Value note;
          note["reason"] = "rotated or missing geotransform; coverage not verifiable";
          addCheck( result.checks, "extent_covers_aoi", false,
                    error_codes::kOutputInvalid, "warning", std::move( note ) );
        }
      }

      // Bounded sample statistics over band 1: finite + NoData fractions.
      GDALRasterBand *band = ds->GetRasterBand( 1 );
      if ( band )
      {
        const int width = ds->GetRasterXSize();
        const int height = ds->GetRasterYSize();
        const int stepX = std::max( 1, width / kMaxProbeSamples );
        const int stepY = std::max( 1, height / kMaxProbeSamples );
        const int samplesX = std::min( width, kMaxProbeSamples );
        const int samplesY = std::min( height, kMaxProbeSamples );
        std::vector<float> buffer( static_cast<size_t>( samplesX ) * samplesY );
        const CPLErr err = band->RasterIO( GF_Read, 0, 0, width, height, buffer.data(),
                                           samplesX, samplesY, GDT_Float32, 0,
                                           sizeof( float ) * samplesX );
        if ( err == CE_None )
        {
          int hasNoData = 0;
          const double noData = band->GetNoDataValue( &hasNoData );
          long long total = 0;
          long long finite = 0;
          long long nodata = 0;
          std::set<float> unique; // bounded class-domain probe
          bool uniqueOverflow = false;
          for ( float value : buffer )
          {
            ++total;
            if ( std::isfinite( value ) )
              ++finite;
            if ( hasNoData && std::fabs( value - static_cast<float>( noData ) ) < 1e-6 )
              ++nodata;
            if ( !uniqueOverflow )
            {
              if ( static_cast<int>( unique.size() ) < kMaxClassProbeUnique )
                unique.insert( value );
              else
                uniqueOverflow = true;
            }
          }
          const double finiteFraction = total ? static_cast<double>( finite ) / total : 0.0;
          const double noDataFraction = total ? static_cast<double>( nodata ) / total : 0.0;
          Json::Value fractions;
          fractions["finite_fraction"] = finiteFraction;
          fractions["nodata_fraction"] = noDataFraction;
          fractions["sampled"] = static_cast<Json::Int64>( total );
          addCheck( result.checks, "finite_fraction",
                    finiteFraction >= expectations.minFiniteFraction,
                    error_codes::kOutputInvalid, "info", fractions );
          addCheck( result.checks, "nodata_fraction",
                    noDataFraction <= expectations.maxNodataFraction,
                    error_codes::kOutputInvalid, "info", fractions );

          if ( expectations.classValues.isArray() && !expectations.classValues.empty() &&
               !uniqueOverflow && expectations.classValues.size() <= kMaxClassProbeUnique )
          {
            bool allKnown = true;
            for ( double value : unique )
            {
              const bool found = std::any_of(
                expectations.classValues.begin(), expectations.classValues.end(),
                [ value ]( const Json::Value &allowed )
                { return std::fabs( allowed.asDouble() - value ) < 1e-6; } );
              if ( !found )
              {
                Json::Value details;
                details["unexpected_value"] = value;
                addCheck( result.checks, "class_values", false, error_codes::kOutputInvalid,
                          "error", details );
                allKnown = false;
                break;
              }
            }
            if ( allKnown )
              addCheck( result.checks, "class_values", true, "" );
          }
        }
        else
        {
          addCheck( result.checks, "sample_read", false, error_codes::kOutputInvalid );
        }
      }
    }
  }

  if ( expectations.requireProvenance )
  {
    addCheck( result.checks, "provenance_present", provenanceSidecarExists( path ),
              error_codes::kOutputInvalid, "warning" );
  }
  if ( expectations.requireUncertainty )
  {
    addCheck( result.checks, "uncertainty_present", uncertaintySidecarExists( path ),
              error_codes::kOutputInvalid, "warning" );
  }

  result.verdict = verdictFromChecks( result.checks );
  return result;
}

Verdict aggregateVerdict( const std::vector<ArtifactVerification> &artifacts )
{
  bool anyFail = false;
  bool anyWarning = false;
  for ( const ArtifactVerification &artifact : artifacts )
  {
    if ( artifact.verdict == Verdict::Fail )
      anyFail = true;
    else if ( artifact.verdict == Verdict::PassWithWarnings )
      anyWarning = true;
  }
  if ( anyFail )
    return Verdict::Fail;
  return anyWarning ? Verdict::PassWithWarnings : Verdict::Pass;
}

} // namespace sicnu::agent::harness
