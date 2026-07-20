// rs_pixel_ignore_options.cpp
#include "rs_pixel_ignore_options.h"

#include <QStringList>

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
