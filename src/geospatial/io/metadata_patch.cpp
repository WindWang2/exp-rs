/***************************************************************************
  geospatial/io/metadata_patch.cpp
  Geospatial I/O, COG & Interchange 11.0 — validated metadata write-back.
 ***************************************************************************/

#include "geospatial/io/metadata_patch.h"

#include "geospatial/io/finalize_manifest.h"
#include "geospatial/io/param_guard.h"
#include "geospatial/gdal_guard.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/util/resource_uri.h"
#include "geospatial/util/time_normalization.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_conv.h>
#include <cpl_error.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

namespace sicnu::geo::io
{

namespace
{

void ensureGdalRegistered()
{
  static bool registered = [] {
    GDALAllRegister();
    return true;
  }();
  ( void )registered;
}

bool isNumericField( const std::string &field )
{
  return field == "scale" || field == "offset" || field == "nodata" || field == "wavelength_nm"
         || field == "fwhm_nm";
}

bool isBandField( const std::string &field )
{
  return isNumericField( field ) || field == "unit" || field == "role" || field == "color_interpretation";
}

bool isDatasetField( const std::string &field )
{
  return field == "sensor" || field == "platform" || field == "product_id" || field == "processing_level"
         || field == "acquisition_time";
}

std::int64_t nowEpochNanos()
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::nanoseconds>( now ).count();
}

/// Band metadata domain keys mirror RasterWriter's at-create spellings so a
/// patch and a create produce the identical on-disk representation.
void applyBandPatch( GDALRasterBand *band, const MetadataPatch &patch )
{
  if ( isNumericField( patch.field ) )
  {
    const double value = std::stod( patch.value );
    if ( std::isnan( value ) && patch.field != "nodata" )
      throw GeoError( ErrorCode::InvalidArgument, "patch: NaN is only legal for the nodata field" );
    if ( patch.field == "scale" )
      band->SetScale( value );
    else if ( patch.field == "offset" )
      band->SetOffset( value );
    else if ( patch.field == "nodata" )
      band->SetNoDataValue( value );
    else if ( patch.field == "wavelength_nm" )
      band->SetMetadataItem( "WAVELENGTH", patch.value.c_str(), nullptr );
    else if ( patch.field == "fwhm_nm" )
      band->SetMetadataItem( "FWHM", patch.value.c_str(), nullptr );
    return;
  }
  if ( patch.field == "unit" )
  {
    band->SetUnitType( patch.value.c_str() );
    return;
  }
  if ( patch.field == "role" )
  {
    band->SetMetadataItem( "SICNU_BAND_ROLE", patch.value.c_str(), nullptr );
    return;
  }
  if ( patch.field == "color_interpretation" )
  {
    const GDALColorInterp interp = GDALGetColorInterpretationByName( patch.value.c_str() );
    if ( interp == GCI_Undefined && patch.value != "Undefined" )
      throw GeoError( ErrorCode::InvalidArgument, "patch: unknown color interpretation '" + patch.value + "'" );
    band->SetColorInterpretation( interp );
    return;
  }
  throw GeoError( ErrorCode::InvalidArgument, "patch: field '" + patch.field + "' is not a band field" );
}

void applyDatasetPatch( GDALDataset *dataset, const MetadataPatch &patch )
{
  // SICNU_* stamps per ADR 0114 (declared-only vocabulary).
  std::string key;
  if ( patch.field == "sensor" )
    key = "SICNU_SENSOR";
  else if ( patch.field == "platform" )
    key = "SICNU_PRODUCT_SPACECRAFT";
  else if ( patch.field == "product_id" )
    key = "SICNU_PRODUCT_ID";
  else if ( patch.field == "processing_level" )
    key = "SICNU_PROCESSING_LEVEL";
  else if ( patch.field == "acquisition_time" )
    key = "SICNU_ACQUISITION_DATE";
  else
    throw GeoError( ErrorCode::InvalidArgument, "patch: field '" + patch.field + "' is not a dataset field" );
  dataset->SetMetadataItem( key.c_str(), patch.value.c_str(), nullptr );
}

/// Read-back verification through the freshly reopened dataset.
std::string readBackBandField( GDALRasterBand *band, const std::string &field )
{
  char buffer[64] = {};
  if ( field == "scale" )
    std::snprintf( buffer, sizeof( buffer ), "%.17g", band->GetScale( nullptr ) );
  else if ( field == "offset" )
    std::snprintf( buffer, sizeof( buffer ), "%.17g", band->GetOffset( nullptr ) );
  else if ( field == "nodata" )
    std::snprintf( buffer, sizeof( buffer ), "%.17g", band->GetNoDataValue( nullptr ) );
  else if ( field == "wavelength_nm" )
  {
    const char *value = band->GetMetadataItem( "WAVELENGTH", nullptr );
    return value ? value : "";
  }
  else if ( field == "fwhm_nm" )
  {
    const char *value = band->GetMetadataItem( "FWHM", nullptr );
    return value ? value : "";
  }
  else if ( field == "unit" )
    return band->GetUnitType() ? band->GetUnitType() : "";
  else if ( field == "role" )
  {
    const char *value = band->GetMetadataItem( "SICNU_BAND_ROLE", nullptr );
    return value ? value : "";
  }
  else if ( field == "color_interpretation" )
    return GDALGetColorInterpretationName( band->GetColorInterpretation() );
  return buffer;
}

std::string readBackDatasetField( GDALDataset *dataset, const std::string &field )
{
  std::string key;
  if ( field == "sensor" )
    key = "SICNU_SENSOR";
  else if ( field == "platform" )
    key = "SICNU_PRODUCT_SPACECRAFT";
  else if ( field == "product_id" )
    key = "SICNU_PRODUCT_ID";
  else if ( field == "processing_level" )
    key = "SICNU_PROCESSING_LEVEL";
  else if ( field == "acquisition_time" )
    key = "SICNU_ACQUISITION_DATE";
  const char *value = dataset->GetMetadataItem( key.c_str(), nullptr );
  return value ? value : "";
}

} // namespace

Json::Value MetadataPatchReport::toJson() const
{
  Json::Value json;
  json["applied"] = applied;
  json["path_display"] = displayPath;
  Json::Value fields( Json::arrayValue );
  for ( const std::string &field : appliedFields )
    fields.append( field );
  json["applied_fields"] = fields;
  Json::Value warningsArray( Json::arrayValue );
  for ( const std::string &warning : warnings )
    warningsArray.append( warning );
  json["warnings"] = warningsArray;
  json["manifest_updated"] = manifestUpdated;
  return json;
}

MetadataPatchReport applyMetadataPatch( const std::string &path, const std::vector<MetadataPatch> &patches )
{
  ensureGdalRegistered();
  MetadataPatchReport report;
  report.displayPath = ResourceUri::parse( path ).display();

  // ---- Phase 1: validate everything BEFORE touching the dataset ----------
  if ( patches.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "patch: no patches requested" );
  const RasterMetadata shape = inspectRaster( path ); // also proves readability

  std::set<std::string> seenFields;
  for ( const MetadataPatch &patch : patches )
  {
    if ( patch.field.empty() )
      throw GeoError( ErrorCode::InvalidArgument, "patch: empty field name" );
    if ( !seenFields.insert( std::to_string( patch.band ) + "/" + patch.field ).second )
      throw GeoError( ErrorCode::InvalidArgument, "patch: duplicate field '" + patch.field + "'" );
    if ( patch.band < 0 || patch.band > shape.bandCount )
    {
      Json::Value details;
      details["band"] = patch.band;
      details["band_count"] = shape.bandCount;
      throw GeoError( ErrorCode::InvalidArgument, "patch: band index out of range", details );
    }
    if ( patch.band == 0 && !isDatasetField( patch.field ) )
      throw GeoError( ErrorCode::InvalidArgument, "patch: field '" + patch.field + "' requires a band index" );
    if ( patch.band > 0 && !isBandField( patch.field ) )
      throw GeoError( ErrorCode::InvalidArgument, "patch: field '" + patch.field + "' is not applicable per-band" );
    if ( isNumericField( patch.field ) )
    {
      std::size_t consumed = 0;
      double numeric = 0;
      try
      {
        numeric = std::stod( patch.value, &consumed );
      }
      catch ( const std::exception & )
      {
        throw GeoError( ErrorCode::InvalidArgument, "patch: field '" + patch.field + "' needs a numeric value" );
      }
      if ( consumed != patch.value.size() )
        throw GeoError( ErrorCode::InvalidArgument, "patch: trailing characters in numeric value" );
      // Phase-1 legality: NaN is only a nodata value — refuse here so a bad
      // batch can never reach the open-for-update phase.
      if ( std::isnan( numeric ) && patch.field != "nodata" )
        throw GeoError( ErrorCode::InvalidArgument, "patch: NaN is only legal for the nodata field" );
    }
    if ( patch.field == "color_interpretation" && patch.value != "Undefined"
         && GDALGetColorInterpretationByName( patch.value.c_str() ) == GCI_Undefined )
      throw GeoError( ErrorCode::InvalidArgument, "patch: unknown color interpretation '" + patch.value + "'" );
    if ( patch.field == "acquisition_time" )
    {
      if ( !parseIso8601Instant( patch.value ).ok )
        throw GeoError( ErrorCode::InvalidArgument,
                        "patch: acquisition_time must be a full ISO-8601 instant (never a date-only guess)" );
    }
  }

  // ---- Phase 2: open for update (capability gate) and apply ---------------
  QuietCplErrors quietErrors;
  GDALDataset *dataset = GDALDataset::Open( path.c_str(), GDAL_OF_UPDATE | GDAL_OF_RASTER );
  if ( !dataset )
  {
    const char *lastError = CPLGetLastErrorMsg();
    Json::Value details;
    details["display"] = report.displayPath;
    details["reason"] = "driver may not support update or the medium is read-only";
    if ( lastError && *lastError )
      details["gdal_error"] = lastError;
    throw GeoError( ErrorCode::OpenFailed, "patch: cannot open dataset for update", details );
  }

  try
  {
    for ( const MetadataPatch &patch : patches )
    {
      if ( patch.band > 0 )
      {
        GDALRasterBand *band = dataset->GetRasterBand( patch.band );
        if ( !band )
          throw GeoError( ErrorCode::WriteFailed, "patch: band vanished during transaction" );
        applyBandPatch( band, patch );
      }
      else
        applyDatasetPatch( dataset, patch );
    }
  }
  catch ( ... )
  {
    // The contract promises a closed dataset on every failure path — never
    // a leaked handle.
    GDALClose( dataset );
    throw;
  }
  // Flush errors must fail the transaction.
  if ( dataset->FlushCache() != CE_None )
  {
    GDALClose( dataset );
    throw GeoError( ErrorCode::WriteFailed, "patch: flush failed; changes may be partial" );
  }

  // ---- Phase 3: read-back verification through a FRESH read-only open ----
  std::vector<std::string> expected;
  expected.reserve( patches.size() );
  for ( const MetadataPatch &patch : patches )
    expected.push_back( patch.value );
  GDALClose( dataset );

  QuietCplErrors verifyQuietErrors;
  GDALDataset *verify = GDALDataset::Open( path.c_str(), GDAL_OF_READONLY | GDAL_OF_RASTER );
  if ( !verify )
    throw GeoError( ErrorCode::OpenFailed, "patch: verification open failed" );
  for ( std::size_t i = 0; i < patches.size(); ++i )
  {
    const MetadataPatch &patch = patches[i];
    const std::string actual = patch.band > 0
                                 ? readBackBandField( verify->GetRasterBand( patch.band ), patch.field )
                                 : readBackDatasetField( verify, patch.field );
    // Numeric round-trips compare as doubles (formatting may differ); text
    // compares verbatim. An empty read-back is a mismatch — never a parse
    // crash.
    bool equal;
    if ( isNumericField( patch.field ) && !actual.empty()
         && actual.find_first_not_of( "-+.eE0123456789" ) == std::string::npos )
    {
      const double actualValue = std::stod( actual );
      equal = std::isnan( actualValue ) ? std::isnan( std::stod( patch.value ) )
                                        : actualValue == std::stod( patch.value );
    }
    else
      equal = actual == patch.value;
    if ( !equal )
    {
      GDALClose( verify );
      Json::Value details;
      details["field"] = patch.field;
      details["expected"] = patch.value;
      details["actual"] = actual;
      throw GeoError( ErrorCode::WriteFailed, "patch: read-back verification failed — the driver did not persist the value", details );
    }
    report.appliedFields.push_back( patch.field );
  }
  GDALClose( verify );

  // Durability of the metadata transaction.
  atomic_fs::fsyncFile( path );

  // ---- Phase 4: provenance continuity -------------------------------------
  // A finalize manifest binds the digest to the pre-patch bytes; refresh it
  // so verifyDataset keeps telling the truth after the patch.
  try
  {
    Json::Value manifest = readFinalizeManifest( path );
    FinalizeManifestFields fields;
    fields.producer = manifest["producer"].asString();
    fields.driver = manifest["driver"].asString();
    fields.width = manifest["shape"]["width"].asInt();
    fields.height = manifest["shape"]["height"].asInt();
    fields.bandCount = manifest["shape"]["band_count"].asInt();
    fields.dtype = manifest["shape"]["dtype"].asString();
    fields.crsAuthid = manifest["crs"].asString();
    for ( const Json::Value &option : manifest["creation_options"] )
      fields.creationOptions.push_back( option.asString() );
    const Json::Value refreshed = buildFinalizeManifest( path, fields );
    Json::Value history( Json::arrayValue );
    if ( manifest.isMember( "patches" ) )
      history = manifest["patches"];
    Json::Value patchEntry;
    patchEntry["utc"] = instantToUtcString( nowEpochNanos() );
    Json::Value patchFields( Json::arrayValue );
    for ( const std::string &field : report.appliedFields )
      patchFields.append( field );
    patchEntry["fields"] = patchFields;
    history.append( patchEntry );
    const bool hadStamp = manifest.isMember( "finalized_utc" );
    const Json::Value originalStamp = manifest["finalized_utc"];
    manifest = refreshed;
    if ( hadStamp )
      manifest["finalized_utc"] = originalStamp; // publish time survives the patch
    manifest["patches"] = history;
    writeFinalizeManifest( path, manifest );
    report.manifestUpdated = true;
  }
  catch ( const GeoError &error )
  {
    // Absent manifest: nothing to refresh — patches still succeeded. Any
    // other manifest problem leaves the OLD digest in place, which
    // verifyDataset will report as a digest mismatch (fail-closed), so name
    // the situation honestly.
    if ( error.code() == ErrorCode::NotFound )
      report.warnings.push_back( "no finalize manifest present; digest provenance not refreshed" );
    else
      report.warnings.push_back( "finalize manifest present but unreadable; stale digest kept and will "
                                 "report as a mismatch — verify with io:verify_dataset" );
  }
  catch ( const std::exception & )
  {
    // Foreign-typed JSON values must never escape past an applied patch.
    report.warnings.push_back( "finalize manifest malformed; stale digest kept and will report as a "
                                "mismatch — verify with io:verify_dataset" );
  }

  report.applied = true;
  return report;
}

} // namespace sicnu::geo::io
