// rs_pixel_ignore_options.cpp
#include "rs_pixel_ignore_options.h"

#include <QStringList>

#include <gdal_priv.h>

void rsCollectBandNodata( GDALDataset *ds, const QVector<int> &bandIndices,
                          const RsPixelIgnoreOptions &options,
                          std::vector<bool> &bandHasNodata,
                          std::vector<float> &bandNodata )
{
  const size_t B = static_cast<size_t>( bandIndices.size() );
  bandHasNodata.assign( B, false );
  bandNodata.assign( B, 0.f );
  if ( !ds || !options.useSourceNodata )
    return;
  for ( int bi = 0; bi < bandIndices.size(); ++bi )
  {
    int success = 0;
    const double nd = ds->GetRasterBand( bandIndices[bi] )->GetNoDataValue( &success );
    if ( success )
    {
      bandHasNodata[static_cast<size_t>( bi )] = true;
      bandNodata[static_cast<size_t>( bi )] = static_cast<float>( nd );
    }
  }
}

void RsPixelIgnoreOptions::setIgnoreValuesFromText( const QString &text )
{
  ignoreValues.clear();
  const QStringList parts = text.split( QLatin1Char( ',' ), Qt::SkipEmptyParts );
  for ( const QString &p : parts )
  {
    bool ok = false;
    const double v = p.trimmed().toDouble( &ok );
    if ( ok )
      ignoreValues.push_back( v );
  }
}

QString RsPixelIgnoreOptions::ignoreValuesText() const
{
  QStringList parts;
  for ( double v : ignoreValues )
  {
    if ( std::floor( v ) == v && std::abs( v ) < 1e12 )
      parts << QString::number( static_cast<qint64>( v ) );
    else
      parts << QString::number( v, 'g', 12 );
  }
  return parts.join( QLatin1Char( ',' ) );
}
