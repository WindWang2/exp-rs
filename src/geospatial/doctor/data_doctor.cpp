/***************************************************************************
  geospatial/doctor/data_doctor.cpp
  Geospatial I/O Foundation 4.0 — read-only structured dataset diagnostics.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/doctor/data_doctor.h"

#include "geospatial/crs/grid_descriptor.h"
#include "geospatial/formats/format_profiles.h"
#include "geospatial/gdal_guard.h"
#include "geospatial/remote/remote_source_validator.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/util/resource_uri.h"

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

/// Remediation advice for a finding check — ADVICE ONLY: the doctor never
/// executes conversions or mutations itself (auto_fixable stays false).
const char *remediationFor( const std::string &check )
{
  static const struct
  {
    const char *check;
    const char *advice;
  } kRemediations[] = {
    { "readability", "Verify the path and that the file exists; run the doctor again after fixing" },
    { "existence", "Check the path spelling and that the file was not moved or deleted" },
    { "crs", "Declare a CRS for the source, or consume it with an explicit fallback policy "
             "(CrsPolicy::allowDeclaredFallback with a declared fallbackCrs)" },
    { "vector_crs", "Assign the layer CRS in the source, or transform it explicitly (io:vector_convert "
                    "with targetCrs) before analysis" },
    { "geotransform", "Georeference the source (world file, metadata) or consume it with an explicit "
                      "placement contract; the layer refuses to guess placement" },
    { "band_nodata", "Declare a NoData value for floating-point bands so masking is unambiguous" },
    { "overviews", "Run io:build_overviews (e.g. GAUSS, levels 2/4/8/16) to speed up display access" },
    { "product_metadata", "Run the product adapter over the scene sidecars (io:product describe) to "
                          "attach sensor/platform vocabulary" },
    { "remote_access", "Verify network reachability and credentials; the range cache serves only what "
                       "the origin still confirms" },
    { "format_profile", "Consume through the profiled contracts, or re-encode into a certified format "
                        "(see docs/io/certified-formats.md)" },
  };
  for ( const auto &entry : kRemediations )
  {
    if ( check == entry.check )
      return entry.advice;
  }
  return nullptr;
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
  json["format_version"] = 1;   // legacy consumer compat
  json["doctor_version"] = 2;   // 7.0: sections + remediation
  json["kind"] = "doctor_report";
  json["path"] = path;
  json["dataset_kind"] = kind;
  json["driver"] = driver;
  json["readable"] = readable;
  json["error_count"] = errorCount;
  json["warning_count"] = warningCount;
  json["findings"] = findings;
  json["identity"] = identity;
  json["format"] = format;
  json["grid"] = grid;
  json["remote"] = remote;
  Json::Value remediationJson( Json::arrayValue );
  for ( const DoctorRemediation &item : remediation )
    remediationJson.append( item.toJson() );
  json["remediation"] = remediationJson;
  return json;
}

Json::Value DoctorRemediation::toJson() const
{
  Json::Value json;
  json["check"] = check;
  json["severity"] = severity;
  json["advice"] = advice;
  json["auto_fixable"] = autoFixable;
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

  // ── identity (7.0): classification works with or without a dataset ──────
  {
    const ResourceUri uri = ResourceUri::parse( path );
    report.identity["resource_kind"] = resourceKindName( uri.kind );
    report.identity["display_path"] = uri.display();
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
    // Remediation rides the early exit too — unreadable inputs deserve
    // advice, not a bare failure.
    for ( Json::ArrayIndex i = 0; i < report.findings.size(); ++i )
    {
      const Json::Value &finding = report.findings[i];
      const std::string severity = finding["severity"].asString();
      if ( severity != "error" && severity != "warning" )
        continue;
      DoctorRemediation item;
      item.check = finding["check"].asString();
      item.severity = severity;
      const char *advice = remediationFor( item.check );
      item.advice = advice ? advice : "Review this finding manually";
      item.autoFixable = false;
      report.remediation.push_back( item );
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

  // ── identity (7.0): classification + redacted display form ──────────
  {
    const ResourceUri uri = ResourceUri::parse( path );
    report.identity["resource_kind"] = resourceKindName( uri.kind );
    report.identity["display_path"] = uri.display();
    report.identity["dataset_kind"] = kind;
    report.identity["driver"] = report.driver;
  }

  // ── format capabilities (7.0): one read-only introspection pass ─────
  try
  {
    const ResolvedCapabilities capabilities = resolveDatasetCapabilities( path );
    report.format["capabilities"] = capabilityNames( capabilities.caps ).empty()
                                      ? Json::Value( Json::arrayValue )
                                      : [ &capabilities ] {
                                          Json::Value list( Json::arrayValue );
                                          for ( const std::string &name : capabilityNames( capabilities.caps ) )
                                            list.append( name );
                                          return list;
                                        }();
    report.format["profile"] = capabilities.profileId;
    report.format["driver"] = capabilities.driver;
    report.format["remote"] = capabilities.remote;
    report.format["is_cog"] = capabilities.isCog;
    report.format["notes"] = capabilities.notes;
  }
  catch ( const GeoError &error )
  {
    report.format["unavailable"] = error.what();
  }

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

    // ── grid verdict (7.0): north-up is a special case, never an assumption
    const GridDescriptor descriptor = GridDescriptor::fromMetadata( meta );
    report.grid["kind"] = gridKindName( descriptor.kind );
    report.grid["rotation_degrees"] = descriptor.rotationDegrees;
    report.grid["flipped_x"] = descriptor.flippedX;
    report.grid["flipped_y"] = descriptor.flippedY;
    report.grid["has_gcps"] = descriptor.hasGcps;
    report.grid["has_rpc"] = descriptor.hasRpcs;
    report.grid["resampling_category"] =
      resamplingCategoryName( resamplingCategoryFor( meta ) );
    addFinding( report.findings, "grid_kind", "info",
                std::string( "Placement: " ) + gridKindName( descriptor.kind ) );

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

  // ── remote posture (7.0): bounded validator identity for remote sources ─
  if ( isRemotePath( path ) )
  {
    if ( options.includeRemoteProbe && RemoteSourceValidator::isRemoteUrl( path ) )
    {
      const RemoteSourceValidator validator = RemoteSourceValidator::probe( path );
      report.remote = validator.identity().toJson();
      const bool healthy = validator.identity().state != RemoteSourceState::Offline;
      addFinding( report.findings, "remote_access", healthy ? "ok" : "warning",
                  std::string( "Remote identity: " ) + remoteSourceStateName( validator.identity().state ) +
                    ( validator.identity().lastError.empty() ? "" : " (" + validator.identity().lastError + ")" ) );
    }
    else
      addFinding( report.findings, "remote_access", "info",
                  "Remote path accepted; accessibility was verified by the successful open "
                  "(pass includeRemoteProbe for a bounded identity probe)" );
  }

  // ── remediation (7.0): advice for every error/warning finding ───────────
  for ( Json::ArrayIndex i = 0; i < report.findings.size(); ++i )
  {
    const Json::Value &finding = report.findings[i];
    const std::string severity = finding["severity"].asString();
    if ( severity != "error" && severity != "warning" )
      continue;
    DoctorRemediation item;
    item.check = finding["check"].asString();
    item.severity = severity;
    const char *advice = remediationFor( item.check );
    item.advice = advice ? advice : "Review this finding manually";
    item.autoFixable = false; // advice only — no destructive auto-conversion
    report.remediation.push_back( item );
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
