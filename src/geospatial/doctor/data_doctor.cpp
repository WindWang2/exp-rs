/***************************************************************************
  geospatial/doctor/data_doctor.cpp
  Geospatial I/O Foundation 4.0 — read-only structured dataset diagnostics.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/doctor/data_doctor.h"

#include "geospatial/formats/format_profiles.h"
#include "geospatial/gdal_guard.h"
#include "geospatial/util/atomic_fs.h"

#include <gdal.h>

#include <cstring>

namespace sicnu::geo
{
namespace
{

void addFinding( Json::Value &findings, const char *check, const char *severity,
                 const std::string &message, Json::Value detail = Json::Value() )
{
  Json::Value finding;
  finding["check"] = check;
  finding["severity"] = severity;
  finding["message"] = message;
  if ( !detail.isNull() )
    finding["detail"] = detail;
  findings.append( finding );
}

bool isRemotePath( const std::string &path )
{
  if ( path.rfind( "/vsi", 0 ) == 0 )
    return true;
  return path.rfind( "http://", 0 ) == 0 || path.rfind( "https://", 0 ) == 0;
}

void doctorSidecars( const std::string &path, Json::Value &findings )
{
  std::vector<std::string> present;
  for ( const std::string &sidecar : atomic_fs::sidecarsFor( path ) )
  {
    if ( atomic_fs::fileExists( sidecar ) )
      present.push_back( sidecar );
  }
  if ( present.empty() )
  {
    addFinding( findings, "sidecars", "ok", "No sidecar files present" );
    return;
  }
  Json::Value list( Json::arrayValue );
  for ( const std::string &sidecar : present )
    list.append( sidecar );
  addFinding( findings, "sidecars", "info", "Sidecar files present", list );
}

} // namespace

Json::Value DoctorReport::toJson() const
{
  Json::Value json;
  json["format_version"] = 1;
  json["kind"] = "doctor_report";
  json["path"] = path;
  json["dataset_kind"] = kind;
  json["driver"] = driver;
  json["readable"] = readable;
  json["error_count"] = errorCount;
  json["warning_count"] = warningCount;
  json["findings"] = findings;
  return json;
}

Json::Value runInspect( const std::string &path, const InspectOptions &options )
{
  return inspectAny( path, options );
}

DoctorReport runDoctor( const std::string &path, const InspectOptions &options )
{
  DoctorReport report;
  report.path = path;

  if ( path.empty() )
  {
    addFinding( report.findings, "readability", "error", "Empty path" );
    report.errorCount = 1;
    return report;
  }

  // ── readability ──────────────────────────────────────────────────────────
  Json::Value canonicalJson;
  try
  {
    canonicalJson = inspectAny( path, options );
    report.readable = true;
    addFinding( report.findings, "readability", "ok", "Dataset opens read-only" );
  }
  catch ( const GeoError &error )
  {
    report.readable = false;
    report.kind = "unreadable";
    addFinding( report.findings, "readability", "error", error.what(), error.details() );
    if ( !atomic_fs::fileExists( path ) )
      addFinding( report.findings, "existence", "error", "File does not exist at the given path" );
    for ( Json::ArrayIndex i = 0; i < report.findings.size(); ++i )
    {
      const std::string severity = report.findings[i]["severity"].asString();
      if ( severity == "error" )
        ++report.errorCount;
      else if ( severity == "warning" )
        ++report.warningCount;
    }
    return report;
  }

  const std::string kind = canonicalJson.get( "kind", "" ).asString();
  report.kind = kind;
  report.driver = canonicalJson.get( "driver", "" ).asString();

  // ── format profile posture ───────────────────────────────────────────────
  const FormatRegistry &registry = FormatRegistry::instance();
  const FormatProfile *profile = registry.profileForPath( path );
  if ( !profile && kind == "raster" && report.driver == "GTiff" )
    profile = registry.find( "GeoTIFF" );
  if ( profile )
  {
    const bool available = registry.driverAvailable( *profile );
    const std::string certification = !available ? "unavailable_in_build"
                                      : profile->certification == Certification::Certified ? "certified"
                                      : profile->certification == Certification::Accessible ? "accessible"
                                                                                            : "unsupported";
    const char *severity = certification == "certified" ? "ok" : "info";
    addFinding( report.findings, "format_profile", severity,
                "Profile " + profile->id + " (" + certification + ")",
                profile->toJson( available ) );
  }
  else
  {
    addFinding( report.findings, "format_profile", "info",
                "No certified profile matches this file type; driver access only" );
  }

  if ( kind == "raster" )
  {
    const RasterMetadata meta = RasterMetadata::fromJson( canonicalJson );

    // ── CRS ──────────────────────────────────────────────────────────
    if ( !meta.crs.valid )
      addFinding( report.findings, "crs", "warning",
                  "No CRS declared — the foundation layer refuses to guess; declare a fallback policy "
                  "explicitly when consuming" );
    else if ( meta.crs.wkt.empty() && meta.crs.authid.empty() )
      addFinding( report.findings, "crs", "error", "CRS block present but empty" );
    else
      addFinding( report.findings, "crs", "ok", "CRS declared",
                  Json::Value( meta.crs.authid.empty() ? meta.crs.wkt.substr( 0, 120 ) : meta.crs.authid ) );
    if ( meta.crs.valid && meta.crs.hasCoordinateEpoch )
      addFinding( report.findings, "coordinate_epoch", "info",
                  "Coordinate epoch " + std::to_string( meta.crs.coordinateEpoch ) );

    // ── georeferencing ───────────────────────────────────────────────
    if ( !meta.hasGeotransform )
      addFinding( report.findings, "geotransform", "warning", "No geotransform; pixel placement unknown" );
    else if ( std::fabs( meta.geotransform[0] ) < 1e-12 && std::fabs( meta.geotransform[3] ) < 1e-12
              && meta.geotransform[1] == 1.0 && meta.geotransform[5] == -1.0 )
      addFinding( report.findings, "geotransform", "warning", "Identity geotransform — likely ungeoreferenced" );
    else
      addFinding( report.findings, "geotransform", "ok", "Geotransform declared" );

    // ── bands ────────────────────────────────────────────────────────
    bool anyNoData = false;
    bool anyScaleOffset = false;
    bool anyRole = false;
    for ( const BandInfo &band : meta.bands )
    {
      anyNoData = anyNoData || band.hasNoData;
      anyScaleOffset = anyScaleOffset || band.hasScale || band.hasOffset;
      anyRole = anyRole || !band.role.empty();
      if ( !band.hasNoData && ( band.dtype == "Float32" || band.dtype == "Float64" ) )
        addFinding( report.findings, "band_nodata", "warning",
                    "Band " + std::to_string( band.index ) + " is floating-point without a declared NoData; "
                    "NaN semantics are ambiguous" );
      if ( ( band.hasScale || band.hasOffset ) && !meta.radiometricState.empty() )
        addFinding( report.findings, "radiometric", "info",
                    "Band " + std::to_string( band.index ) + " carries scale/offset with radiometric state "
                      + meta.radiometricState );
    }
    addFinding( report.findings, "nodata", anyNoData ? "ok" : "warning",
                anyNoData ? "NoData declared on at least one band" : "No NoData declared on any band" );
    addFinding( report.findings, "scale_offset", anyScaleOffset ? "ok" : "info",
                anyScaleOffset ? "Scale/offset present (physical values = stored * scale + offset)"
                               : "No scale/offset (stored values are uncalibrated unless product metadata says otherwise)" );
    addFinding( report.findings, "band_roles", anyRole ? "ok" : "info",
                anyRole ? "Band roles declared (SICNU_BAND_ROLE)" : "No band roles declared" );

    // ── overviews for large rasters ──────────────────────────────────
    const bool large = meta.width >= 2048 || meta.height >= 2048;
    if ( large && meta.overviewCount <= 0 )
      addFinding( report.findings, "overviews", "warning",
                  "Large raster without overviews; display access will be slow. Run io:build_overviews." );
    else if ( meta.overviewCount > 0 )
      addFinding( report.findings, "overviews", "ok",
                  std::to_string( meta.overviewCount ) + " overview level(s)" );

    // ── compression / product context ────────────────────────────────
    if ( meta.compression.empty() )
      addFinding( report.findings, "compression", "info", "No compression (raw storage)" );
    if ( !meta.subdatasets.empty() )
      addFinding( report.findings, "subdatasets", "info",
                  std::to_string( meta.subdatasets.size() / 2 ) + " subdataset(s); use the multidim contract "
                  "or subdataset names for access" );
    if ( meta.hasGcps || meta.hasRpc )
      addFinding( report.findings, "georeferencing_extras", "info",
                  meta.hasGcps ? "Ground control points present" : "RPC coefficients present" );
    if ( meta.sensor.empty() && meta.platform.empty() )
      addFinding( report.findings, "product_metadata", "info",
                  "No sensor/platform declared; run a product adapter over sidecars if applicable" );

    doctorSidecars( path, report.findings );
  }
  else if ( kind == "vector" )
  {
    const VectorMetadata meta = VectorMetadata::fromJson( canonicalJson );
    for ( const VectorLayerInfo &layer : meta.layers )
    {
      if ( !layer.crs.valid )
        addFinding( report.findings, "vector_crs", "warning",
                    "Layer '" + layer.name + "' has no CRS; the foundation layer refuses to guess" );
      if ( layer.featureCount < 0 )
        addFinding( report.findings, "vector_count", "info",
                    "Layer '" + layer.name + "' has no cheap feature count (scan needed for exact count)" );
      if ( layer.geometryTypeName.empty() || layer.geometryTypeName == "None" )
        addFinding( report.findings, "vector_geometry", "info",
                    "Layer '" + layer.name + "' is geometryless" );
      if ( layer.supportsFastSpatialFilter )
        addFinding( report.findings, "vector_spatial_index", "ok",
                    "Layer '" + layer.name + "' supports fast spatial filtering" );
      else
        addFinding( report.findings, "vector_spatial_index", "info",
                    "Layer '" + layer.name + "' has no fast spatial filter (bbox filters will scan)" );
    }
    doctorSidecars( path, report.findings );
  }
  else if ( kind == "multidimensional" )
  {
    const Json::Value &variables = canonicalJson["variables"];
    addFinding( report.findings, "multidim", "info",
                std::to_string( variables.size() ) + " variable(s); slices are lazy through the multidim contract" );
  }

  // ── remote posture ───────────────────────────────────────────────────────
  if ( isRemotePath( path ) )
  {
    if ( options.includeRemoteProbe )
      addFinding( report.findings, "remote_access", "ok",
                  "Dataset opened through a GDAL VSI handle (range reads)" );
    else
      addFinding( report.findings, "remote_access", "info",
                  "Remote path accepted; accessibility was verified by the successful open" );
  }

  for ( Json::ArrayIndex i = 0; i < report.findings.size(); ++i )
  {
    const std::string severity = report.findings[i]["severity"].asString();
    if ( severity == "error" )
      ++report.errorCount;
    else if ( severity == "warning" )
      ++report.warningCount;
  }
  return report;
}

} // namespace sicnu::geo
