/***************************************************************************
  geospatial/io/vector_interchange.cpp
  Geospatial I/O, COG & Interchange 11.0 — capability-gated vector interchange.
 ***************************************************************************/

#include "geospatial/io/vector_interchange.h"

#include "geospatial/formats/format_profiles.h"
#include "geospatial/io/param_guard.h"
#include "geospatial/util/resource_uri.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <cpl_conv.h>
#include <cpl_error.h>

#include <set>
#include <vector>

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

const FormatProfile *certifiedVectorProfile( const std::string &driver )
{
  for ( const FormatProfile &profile : FormatRegistry::instance().profiles() )
  {
    if ( profile.family != FormatFamily::Vector )
      continue;
    for ( const std::string &candidate : profile.driverNames )
    {
      if ( candidate == driver )
        return &profile;
    }
  }
  return nullptr;
}

} // namespace

Json::Value VectorTargetCheck::toJson() const
{
  Json::Value json;
  json["usable"] = usable;
  json["certified_profile"] = certifiedProfile;
  if ( !profileId.empty() )
    json["profile_id"] = profileId;
  if ( !reasonCode.empty() )
    json["reason"] = reasonCode;
  if ( !message.empty() )
    json["message"] = message;
  return json;
}

VectorTargetCheck checkVectorWriteTarget( const std::string &driver )
{
  ensureGdalRegistered();
  VectorTargetCheck check;

  if ( driver.empty() )
  {
    check.reasonCode = "driver_missing";
    check.message = "no driver declared";
    return check;
  }

  GDALDriverH handle = GDALGetDriverByName( driver.c_str() );
  if ( !handle )
  {
    check.reasonCode = "driver_missing";
    check.message = "GDAL driver '" + driver + "' is not available in this build";
    return check;
  }

  const char *isVector = GDALGetMetadataItem( handle, GDAL_DCAP_VECTOR, nullptr );
  if ( !isVector || isVector[0] == '\0' )
  {
    check.reasonCode = "not_vector";
    check.message = "driver '" + driver + "' is not a vector (OGR) driver";
    return check;
  }

  const char *canCreate = GDALGetMetadataItem( handle, GDAL_DCAP_CREATE, nullptr );
  if ( !canCreate || canCreate[0] == '\0' )
  {
    check.reasonCode = "not_create_capable";
    check.message = "driver '" + driver + "' cannot create datasets in this build";
    return check;
  }

  check.usable = true;
  if ( const FormatProfile *profile = certifiedVectorProfile( driver ) )
  {
    check.certifiedProfile = true;
    check.profileId = profile->id;
  }
  return check;
}

Json::Value vectorInterchangeCapabilities()
{
  ensureGdalRegistered();

  // The canonical interchange set always appears (absence is information),
  // then anything else the registry knows that the report has not covered.
  static const char *kInterchangeDrivers[] = { "GPKG", "GeoJSON", "GeoJSONSeq", "FlatGeobuf", "Parquet",
                                               "CSV", "ESRI Shapefile" };
  std::set<std::string> drivers( std::begin( kInterchangeDrivers ), std::end( kInterchangeDrivers ) );
  for ( const FormatProfile &profile : FormatRegistry::instance().profiles() )
  {
    if ( profile.family != FormatFamily::Vector )
      continue;
    for ( const std::string &name : profile.driverNames )
      drivers.insert( name );
  }

  Json::Value report;
  report["kind"] = "vector_interchange_capabilities";
  Json::Value entries( Json::arrayValue );
  for ( const std::string &driver : drivers )
  {
    const VectorTargetCheck check = checkVectorWriteTarget( driver );
    Json::Value entry = check.toJson();
    entry["driver"] = driver;
    entries.append( entry );
  }
  report["drivers"] = entries;
  return report;
}

} // namespace sicnu::geo::io
