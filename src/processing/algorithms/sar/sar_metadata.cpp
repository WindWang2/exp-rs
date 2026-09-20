// src/processing/algorithms/sar/sar_metadata.cpp
#include "sar_metadata.h"

#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <gdal.h>

#include <QFile>
#include <QTextStream>

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

bool isSarDerivedState( const QString &state )
{
  const QString s = state.trimmed().toLower();
  return s == QLatin1String( kDerivedPairMetricState ) ||
         s == QLatin1String( kDerivedTextureState );
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

QString declaredCalibrationToken( const GdalDatasetWrapper &ds )
{
  return datasetMeta( ds, kCalibrationKey ).trimmed().toLower();
}

SarStateRead readDeclaredSarState( const GdalDatasetWrapper &ds )
{
  SarStateRead read;
  read.calibration = datasetMeta( ds, kCalibrationKey ).trimmed().toLower();
  read.state = datasetMeta( ds, kRadiometricStateKey ).trimmed().toLower();
  if ( !read.calibration.isEmpty() && !read.state.isEmpty() )
  {
    read.conflict = read.calibration != read.state;
    read.token = read.calibration;
    return read;
  }
  read.token = read.calibration.isEmpty() ? read.state : read.calibration;
  return read;
}

QString recognizedSarState( const GdalDatasetWrapper &ds )
{
  const SarStateRead read = readDeclaredSarState( ds );
  if ( read.conflict || read.token.isEmpty() )
    return QString();
  const QString canonical = normalizeCalibration( read.token );
  if ( !canonical.isEmpty() )
    return canonical;
  if ( isSarDerivedState( read.token ) )
    return read.token;
  return QString();
}

SarStateCheck checkDeclaredState( const GdalDatasetWrapper &ds, const QString &required,
                                  QString *reason )
{
  const SarStateRead read = readDeclaredSarState( ds );
  if ( read.conflict )
  {
    if ( reason )
      *reason = QStringLiteral(
                    "input declares conflicting SICNU_SAR_CALIBRATION='%1' and "
                    "SICNU_RADIOMETRIC_STATE='%2'; refusing to guess the radiometric state" )
                    .arg( read.calibration, read.state );
    return SarStateCheck::Refused;
  }
  if ( read.token.isEmpty() )
    return SarStateCheck::OkUndeclared;

  const QString canonical = normalizeCalibration( read.token );
  if ( canonical.isEmpty() )
  {
    if ( reason )
    {
      if ( isSarDerivedState( read.token ) )
        *reason = QStringLiteral(
                      "input declares the derived SAR product '%1' (pair metric / texture), "
                      "which carries no backscatter calibration; this operator requires %2 "
                      "linear power" )
                      .arg( read.token, required );
      else
        *reason = QStringLiteral(
                      "input declares the unrecognized radiometric state token '%1' "
                      "(SICNU_SAR_CALIBRATION / SICNU_RADIOMETRIC_STATE); refusing to "
                      "guess the radiometric state" )
                      .arg( read.token );
    }
    return SarStateCheck::Refused;
  }
  if ( canonical != required )
  {
    if ( reason )
      *reason = QStringLiteral(
                    "input declares SICNU_SAR_CALIBRATION=%1 but this operator requires %2 "
                    "linear power; convert with rs:sar_backscatter (or re-run "
                    "rs:sar_calibrate on the DN product) first" )
                    .arg( canonical, required );
    return SarStateCheck::Refused;
  }
  return SarStateCheck::Ok;
}

bool parseCalibrationLut( const QString &path, int expectedRows, std::vector<double> *values,
                          QString *error )
{
  if ( expectedRows <= 0 )
  {
    if ( error )
      *error = QStringLiteral( "calibration LUT check: raster has no rows" );
    return false;
  }
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
  {
    if ( error )
      *error = QStringLiteral( "cannot open calibration LUT '%1'" ).arg( path );
    return false;
  }
  // The sidecar is data-controlled (declared metadata), so bound the work
  // before reading anything: one value per row, and a generous per-value
  // budget. A file that cannot possibly be a row-exact LUT is refused on its
  // size, never materialized.
  const qint64 maxBytes = static_cast<qint64>( expectedRows ) * 64 + 4096;
  if ( file.size() > maxBytes )
  {
    if ( error )
      *error = QStringLiteral(
                   "calibration LUT '%1' is %2 bytes, far larger than the %3-row budget; one "
                   "value per input row is required" )
                   .arg( path )
                   .arg( file.size() )
                   .arg( expectedRows );
    return false;
  }

  // Streamed line-by-line with an early exit: the count must match exactly,
  // so a surplus row is knowable before the whole file is parsed.
  values->clear();
  values->reserve( static_cast<size_t>( expectedRows ) );
  QTextStream stream( &file );
  int lineNumber = 0;
  while ( stream.atEnd() == false )
  {
    const QString line = stream.readLine().trimmed();
    ++lineNumber;
    if ( values->size() >= static_cast<size_t>( expectedRows ) )
    {
      if ( error )
        *error = QStringLiteral(
                     "calibration LUT '%1' declares more than %2 values; one value per input "
                     "row is required (no interpolation is applied)" )
                     .arg( path )
                     .arg( expectedRows );
      return false;
    }
    if ( line.isEmpty() )
    {
      // A trailing newline is the only tolerated blank.
      if ( lineNumber == 1 && stream.atEnd() )
        break;
      if ( error )
        *error = QStringLiteral( "calibration LUT '%1' has an empty line at row %2" )
                     .arg( path )
                     .arg( lineNumber );
      return false;
    }
    bool ok = false;
    const double value = line.toDouble( &ok );
    if ( !ok || !std::isfinite( value ) || value <= 0.0 )
    {
      if ( error )
        *error = QStringLiteral( "calibration LUT '%1' row %2 is not a finite positive number: '%3'" )
                     .arg( path )
                     .arg( lineNumber )
                     .arg( line );
      return false;
    }
    values->push_back( value );
  }
  if ( static_cast<int>( values->size() ) != expectedRows )
  {
    if ( error )
      *error = QStringLiteral(
                   "calibration LUT '%1' declares %2 values but the raster has %3 rows; one "
                   "value per input row is required (no interpolation is applied)" )
                   .arg( path )
                   .arg( static_cast<int>( values->size() ) )
                   .arg( expectedRows );
    return false;
  }
  return true;
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
