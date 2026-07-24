#include "gdal_runtime.h"

#include <mutex>

#include <gdal.h>

namespace sicnu::data::providers
{

namespace
{
std::once_flag g_gdalInitFlag;
}

void ensureGdalRuntime()
{
  std::call_once( g_gdalInitFlag, [] { GDALAllRegister(); } );
}

} // namespace sicnu::data::providers
