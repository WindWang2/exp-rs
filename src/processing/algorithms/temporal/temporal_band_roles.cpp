// src/processing/algorithms/temporal/temporal_band_roles.cpp
#include "temporal_band_roles.h"

#include "data/band_role.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <algorithm>

namespace sicnu::temporal
{

int findBandWithRole( const GdalDatasetWrapper &ds, sicnu::data::BandRole role )
{
  const QByteArray roleId = sicnu::data::bandRoleToString( role ).toLatin1();
  const int bandCount = ds.bandCount();
  for ( int b = 1; b <= bandCount; ++b )
  {
    if ( ds.bandMetadataItem( b, "SICNU_BAND_ROLE" ) == QLatin1String( roleId ) )
      return b;
  }
  return 0;
}

int positionalFallbackBand( const QString &roleId, int bandCount )
{
  if ( roleId == QLatin1String( "nir" ) )
    return 4;
  if ( roleId == QLatin1String( "red" ) )
    return 3;
  if ( roleId == QLatin1String( "green" ) )
    return 2;
  if ( roleId == QLatin1String( "blue" ) )
    return 1;
  if ( roleId == QLatin1String( "swir1" ) || roleId == QLatin1String( "swir" ) )
    return 5;
  if ( roleId == QLatin1String( "swir2" ) )
    return bandCount > 0 ? std::min( 6, bandCount ) : 6;
  if ( roleId == QLatin1String( "red_edge" ) || roleId == QLatin1String( "rededge" ) )
    return bandCount > 0 ? std::min( 5, bandCount ) : 5;
  return 0;
}

int resolveBand( const GdalDatasetWrapper &ds, const QString &roleId,
                 int overrideBand, bool *usedFallback )
{
  if ( usedFallback )
    *usedFallback = false;
  if ( overrideBand > 0 )
    return overrideBand;

  const sicnu::data::BandRole role = sicnu::data::bandRoleFromString( roleId );
  if ( role != sicnu::data::BandRole::Unknown )
  {
    const int byRole = findBandWithRole( ds, role );
    if ( byRole > 0 )
      return byRole;
  }
  // swir1 may fall back to a declared swir2 role and vice versa (mirrors the
  // single-scene operator).
  if ( roleId == QLatin1String( "swir1" ) )
  {
    const int swir2 = findBandWithRole( ds, sicnu::data::BandRole::SWIR2 );
    if ( swir2 > 0 )
      return swir2;
  }
  else if ( roleId == QLatin1String( "swir2" ) )
  {
    const int swir1 = findBandWithRole( ds, sicnu::data::BandRole::SWIR1 );
    if ( swir1 > 0 )
      return swir1;
  }

  const int fallback = positionalFallbackBand( roleId, ds.bandCount() );
  if ( fallback > 0 && usedFallback )
    *usedFallback = true;
  return fallback;
}

QStringList temporalBandRoleIds()
{
  return { QStringLiteral( "blue" ), QStringLiteral( "green" ), QStringLiteral( "red" ),
           QStringLiteral( "red_edge" ), QStringLiteral( "nir" ),
           QStringLiteral( "swir1" ), QStringLiteral( "swir2" ) };
}

} // namespace sicnu::temporal
