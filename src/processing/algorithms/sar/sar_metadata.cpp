// src/processing/algorithms/sar/sar_metadata.cpp
#include "sar_metadata.h"

#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <gdal.h>

#include <cmath>

namespace sicnu::sar
{

bool isSarRadiometricState( const QString &state )
{
  const QString s = state.toLower();
  return s == QLatin1String( "sigma0" ) || s == QLatin1String( "gamma0" ) ||
         s == QLatin1String( "beta0" ) || s == QLatin1String( "dn" ) ||
         s == QLatin1String( "digital_number" );
}

QString datasetMeta( const GdalDatasetWrapper &ds, const char *key )
{
  if ( !ds.isValid() )
    return QString();
  const char *value =
    GDALGetMetadataItem( static_cast<GDALDatasetH>( ds.dataset() ), key, nullptr );
  return value ? QString::fromUtf8( value ) : QString();
}

double linearToDb( double power )
{
  return 10.0 * std::log10( power );
}

double dbToLinear( double db )
{
  return std::pow( 10.0, db / 10.0 );
}

QString normalizeCalibration( const QString &token )
{
  const QString t = token.trimmed().toLower();
  if ( t == QLatin1String( "sigma0" ) || t == QLatin1String( "sigma_naught" ) ||
       t == QLatin1String( "sigma" ) )
    return QStringLiteral( "sigma0" );
  if ( t == QLatin1String( "gamma0" ) || t == QLatin1String( "gamma" ) )
    return QStringLiteral( "gamma0" );
  if ( t == QLatin1String( "beta0" ) || t == QLatin1String( "beta" ) )
    return QStringLiteral( "beta0" );
  if ( t == QLatin1String( "dn" ) || t == QLatin1String( "digital_number" ) )
    return QStringLiteral( "dn" );
  return QString();
}

void writeSarDatasetMetadata( void *datasetHandle,
                              const QString &calibration,
                              const QString &domain,
                              const QString &polarizations,
                              const QString &sensor,
                              double incidenceDeg,
                              double headingDeg )
{
  GDALDatasetH ds = static_cast<GDALDatasetH>( datasetHandle );
  if ( !ds )
    return;
  GDALSetMetadataItem( ds, kModalityKey, "sar", nullptr );
  if ( !calibration.isEmpty() )
    GDALSetMetadataItem( ds, kCalibrationKey, calibration.toUtf8().constData(), nullptr );
  if ( !domain.isEmpty() )
    GDALSetMetadataItem( ds, kDomainKey, domain.toUtf8().constData(), nullptr );
  if ( !polarizations.isEmpty() )
    GDALSetMetadataItem( ds, kPolarizationsKey, polarizations.toUtf8().constData(), nullptr );
  if ( !sensor.isEmpty() )
    GDALSetMetadataItem( ds, kSensorKey, sensor.toUtf8().constData(), nullptr );
  if ( incidenceDeg > 0.0 )
    GDALSetMetadataItem( ds, kIncidenceKey,
                         QString::number( incidenceDeg, 'g', 10 ).toUtf8().constData(), nullptr );
  if ( headingDeg != 0.0 )
    GDALSetMetadataItem( ds, kHeadingKey,
                         QString::number( headingDeg, 'g', 10 ).toUtf8().constData(), nullptr );
}

void writeSarOutputMetadata( GdalStreamingOutput &output,
                             const QString &calibration,
                             const QString &domain,
                             const QString &polarizations,
                             const QString &sensor,
                             double incidenceDeg,
                             double headingDeg )
{
  if ( !output.isOpen() )
    return;
  output.setMetadataItem( QString::fromLatin1( kModalityKey ), QStringLiteral( "sar" ) );
  if ( !calibration.isEmpty() )
    output.setMetadataItem( QString::fromLatin1( kCalibrationKey ), calibration );
  if ( !domain.isEmpty() )
    output.setMetadataItem( QString::fromLatin1( kDomainKey ), domain );
  if ( !polarizations.isEmpty() )
    output.setMetadataItem( QString::fromLatin1( kPolarizationsKey ), polarizations );
  if ( !sensor.isEmpty() )
    output.setMetadataItem( QString::fromLatin1( kSensorKey ), sensor );
  if ( incidenceDeg > 0.0 )
    output.setMetadataItem( QString::fromLatin1( kIncidenceKey ),
                            QString::number( incidenceDeg, 'g', 10 ) );
  if ( headingDeg != 0.0 )
    output.setMetadataItem( QString::fromLatin1( kHeadingKey ), QString::number( headingDeg, 'g', 10 ) );
}

QString readCalibration( const GdalDatasetWrapper &ds )
{
  return normalizeCalibration( datasetMeta( ds, kCalibrationKey ) );
}

QString readDomain( const GdalDatasetWrapper &ds )
{
  const QString d = datasetMeta( ds, kDomainKey ).toLower();
  if ( d == QLatin1String( "db" ) || d == QLatin1String( "decibels" ) )
    return QStringLiteral( "db" );
  if ( d == QLatin1String( "linear_power" ) || d == QLatin1String( "linear" ) )
    return QStringLiteral( "linear_power" );
  return QString();
}

} // namespace sicnu::sar
