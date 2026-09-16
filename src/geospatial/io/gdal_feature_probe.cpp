/***************************************************************************
  geospatial/io/gdal_feature_probe.cpp
  Geospatial I/O, COG & Interchange 11.0 — runtime GDAL feature report.
 ***************************************************************************/

#include "geospatial/io/gdal_feature_probe.h"
#include "geospatial/util/gdal_compat.h"

#include <gdal.h>

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

void reportMacro( Json::Value &macros, const char *name, bool active )
{
  macros[name] = active;
}

void reportDriver( Json::Value &drivers, GDALDriverH handle )
{
  if ( !handle )
  {
    drivers["present"] = false;
    return;
  }
  drivers["present"] = true;
  const char *isVector = GDALGetMetadataItem( handle, GDAL_DCAP_VECTOR, nullptr );
  const char *canCreate = GDALGetMetadataItem( handle, GDAL_DCAP_CREATE, nullptr );
  const char *canCreateCopy = GDALGetMetadataItem( handle, GDAL_DCAP_CREATECOPY, nullptr );
  drivers["vector"] = isVector && isVector[0];
  drivers["create"] = canCreate && canCreate[0];
  drivers["create_copy"] = canCreateCopy && canCreateCopy[0];
}

} // namespace

Json::Value gdalFeatureReport()
{
  ensureGdalRegistered();
  Json::Value report;
  report["kind"] = "gdal_feature_report";
  report["gdal_release"] = GDALVersionInfo( "RELEASE" );

  Json::Value macros;
  reportMacro( macros, "vsi_open_returns_unique_ptr", SICNU_GDAL_VSI_OPEN_RETURNS_UNIQUE_PTR != 0 );
  reportMacro( macros, "vsi_handle_read_bytes", SICNU_GDAL_VSI_HANDLE_READ_BYTES != 0 );
  reportMacro( macros, "vsi_handle_err_api", SICNU_GDAL_VSI_HANDLE_ERR_API != 0 );
  reportMacro( macros, "vsi_remove_handler", SICNU_GDAL_VSI_REMOVE_HANDLER != 0 );
  reportMacro( macros, "int64_datatypes", SICNU_GDAL_INT64_DATATYPES != 0 );
  report["version_macros"] = macros;

  Json::Value drivers;
  reportDriver( drivers["GTiff"], GDALGetDriverByName( "GTiff" ) );
  reportDriver( drivers["GPKG"], GDALGetDriverByName( "GPKG" ) );
  reportDriver( drivers["GeoJSON"], GDALGetDriverByName( "GeoJSON" ) );
  reportDriver( drivers["FlatGeobuf"], GDALGetDriverByName( "FlatGeobuf" ) );
  reportDriver( drivers["Parquet"], GDALGetDriverByName( "Parquet" ) );
  reportDriver( drivers["netCDF"], GDALGetDriverByName( "netCDF" ) );
  reportDriver( drivers["HDF5"], GDALGetDriverByName( "HDF5" ) );
  reportDriver( drivers["COG"], GDALGetDriverByName( "COG" ) );
  report["drivers"] = drivers;
  return report;
}

} // namespace sicnu::geo::io
