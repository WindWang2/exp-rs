// src/processing/algorithms/sar/sar_calibration.cpp
#include "sar_calibration.h"

#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <gdal.h>

#include <cmath>

namespace sicnu::sar
{

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

inline float finishValue( double v, SarDomain domain )
{
  if ( domain == SarDomain::Decibels )
  {
    if ( !( v > 0.0 ) )
      return kNan; // dB of nonpositive power is undefined → NoData
    return static_cast<float>( linearToDb( v ) );
  }
  return static_cast<float>( v );
}
} // namespace

QString sarDomainToString( SarDomain domain )
{
  return domain == SarDomain::Decibels ? QStringLiteral( "db" ) : QStringLiteral( "linear_power" );
}

double calibrateDn( double dn, double calibrationA, double noiseLinear )
{
  const double power = dn * dn - noiseLinear;
  const double denom = calibrationA * calibrationA;
  return power / denom;
}

double sigma0ToGamma0( double sigma0, double incidenceDeg )
{
  return sigma0 / std::cos( incidenceDeg * M_PI / 180.0 );
}

double sigma0ToBeta0( double sigma0, double incidenceDeg )
{
  return sigma0 / std::sin( incidenceDeg * M_PI / 180.0 );
}

double gamma0ToSigma0( double gamma0, double incidenceDeg )
{
  return gamma0 * std::cos( incidenceDeg * M_PI / 180.0 );
}

bool calibrateRaster( const GdalDatasetWrapper &src, int band, double calibrationA,
                      double noiseLinear, SarDomain outputDomain, float nodata,
                      GdalStreamingOutput &dst, int tileDim, int outBand,
                      const QString &polarizations, const QString &sensor,
                      double incidenceDeg, double headingDeg )
{
  GdalBlockStream stream( src, band, tileDim, tileDim, 0 );
  const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *pixels ) {
    std::vector<float> out( static_cast<size_t>( tile.width ) * tile.height );
    for ( int y = 0; y < tile.height; ++y )
    {
      for ( int x = 0; x < tile.width; ++x )
      {
        const float v = pixels[y * tile.width + x];
        if ( !std::isfinite( v ) || v == nodata )
        {
          out[static_cast<size_t>( y ) * tile.width + x] = kNan;
          continue;
        }
        const double sigma0 = calibrateDn( v, calibrationA, noiseLinear );
        out[static_cast<size_t>( y ) * tile.width + x] = finishValue( sigma0, outputDomain );
      }
    }
    return dst.writeTile( outBand, tile, out.data() );
  } );
  if ( ok )
  {
    writeSarOutputMetadata( dst, QStringLiteral( "sigma0" ),
                            sarDomainToString( outputDomain ), polarizations, sensor,
                            incidenceDeg, headingDeg );
  }
  return ok;
}

float readIncidenceTile( const GdalDatasetWrapper &incidenceDs, int xOffset, int yOffset,
                         int width, int height, std::vector<float> &buffer )
{
  buffer.assign( static_cast<size_t>( width ) * height, kNan );
  GDALRasterBandH band = GDALGetRasterBand( static_cast<GDALDatasetH>( incidenceDs.dataset() ), 1 );
  if ( !band )
    return kNan;
  if ( GDALRasterIO( band, GF_Read, xOffset, yOffset, width, height, buffer.data(), width, height,
                     GDT_Float32, 0, 0 ) != CE_None )
    return kNan;
  return 1.0f;
}

bool convertBackscatterRaster( const GdalDatasetWrapper &src, int band,
                               const BackscatterConvertOptions &options, float nodata,
                               GdalStreamingOutput &dst, int tileDim,
                               const QString &polarizations, const QString &sensor,
                               double headingDeg )
{
  const QString from = normalizeCalibration( options.fromCalibration );
  const QString to = normalizeCalibration( options.toCalibration );
  if ( from.isEmpty() || to.isEmpty() )
    return false;
  if ( from == to )
    return false; // caller should skip the run entirely, not stream a copy

  // Per-pixel incidence source (optional).
  GdalDatasetWrapper incidenceDs;
  bool useRasterIncidence = false;
  if ( options.constantIncidenceDeg <= 0.0 && !options.incidenceRasterPath.isEmpty() )
  {
    if ( !incidenceDs.open( options.incidenceRasterPath ) )
      return false;
    if ( incidenceDs.width() != src.width() || incidenceDs.height() != src.height() )
      return false;
    useRasterIncidence = true;
  }
  else if ( options.constantIncidenceDeg <= 0.0 )
  {
    // No incidence information at all: only identity-domain transforms are
    // possible; conversions need geometry.
    return false;
  }

  const double theta0 = options.constantIncidenceDeg;
  GdalBlockStream stream( src, band, tileDim, tileDim, 0 );
  std::vector<float> incidenceTile;
  const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *pixels ) {
    if ( useRasterIncidence &&
         readIncidenceTile( incidenceDs, tile.xOffset, tile.yOffset, tile.width, tile.height,
                            incidenceTile ) != 1.0f )
      return false;

    std::vector<float> out( static_cast<size_t>( tile.width ) * tile.height );
    for ( int y = 0; y < tile.height; ++y )
    {
      for ( int x = 0; x < tile.width; ++x )
      {
        const float v = pixels[y * tile.width + x];
        const size_t idx = static_cast<size_t>( y ) * tile.width + x;
        if ( !std::isfinite( v ) || v == nodata )
        {
          out[idx] = kNan;
          continue;
        }
        double value = v;
        const double thetaDeg =
          useRasterIncidence ? incidenceTile[idx] : theta0;
        if ( !std::isfinite( thetaDeg ) )
        {
          out[idx] = kNan;
          continue;
        }
        // Data domain → linear sigma0 in the data's declared state.
        double sigma0 = value;
        if ( from != QLatin1String( "sigma0" ) )
        {
          if ( from == QLatin1String( "gamma0" ) )
            sigma0 = gamma0ToSigma0( value, thetaDeg );
          else if ( from == QLatin1String( "beta0" ) )
            sigma0 = value * std::sin( thetaDeg * M_PI / 180.0 );
        }
        // Target domain.
        double result = sigma0;
        if ( to == QLatin1String( "sigma0" ) )
          result = sigma0;
        else if ( to == QLatin1String( "gamma0" ) )
          result = sigma0ToGamma0( sigma0, thetaDeg );
        else if ( to == QLatin1String( "beta0" ) )
          result = sigma0ToBeta0( sigma0, thetaDeg );
        out[idx] = finishValue( result, options.outputDomain );
      }
    }
    return dst.writeTile( 1, tile, out.data() );
  } );
  if ( ok )
  {
    writeSarOutputMetadata( dst, to, sarDomainToString( options.outputDomain ),
                            polarizations, sensor, theta0, headingDeg );
  }
  return ok;
}

} // namespace sicnu::sar
