#include "crs_presets.h"

#include <QSet>
#include <QSettings>

namespace
{
  // Static lazy-initialized data
  const QList<CrsPreset> &presets()
  {
    static const QList<CrsPreset> sPresets = {
      // -- Global --
      { QStringLiteral( "WGS 84" ),        4326, QStringLiteral( "World Geodetic System 1984" ),                 QStringLiteral( "Global" ) },
      { QStringLiteral( "Web Mercator" ),  3857, QStringLiteral( "Web mapping standard (Pseudo-Mercator)" ),     QStringLiteral( "Global" ) },

      // -- UTM (common zones for China) --
      { QStringLiteral( "UTM Zone 49N" ), 32649, QStringLiteral( "Universal Transverse Mercator Zone 49 North" ), QStringLiteral( "UTM" ) },
      { QStringLiteral( "UTM Zone 50N" ), 32650, QStringLiteral( "Universal Transverse Mercator Zone 50 North" ), QStringLiteral( "UTM" ) },
      { QStringLiteral( "UTM Zone 51N" ), 32651, QStringLiteral( "Universal Transverse Mercator Zone 51 North" ), QStringLiteral( "UTM" ) },

      // -- China: CGCS2000 / 3-degree Gauss-Kruger zones 25-33 --
      { QStringLiteral( "CGCS2000 / 3-degree GK Zone 25" ), 4547, QStringLiteral( "China Geodetic Coordinate System 2000, 3-degree GK zone 25" ), QStringLiteral( "China" ) },
      { QStringLiteral( "CGCS2000 / 3-degree GK Zone 26" ), 4548, QStringLiteral( "China Geodetic Coordinate System 2000, 3-degree GK zone 26" ), QStringLiteral( "China" ) },
      { QStringLiteral( "CGCS2000 / 3-degree GK Zone 27" ), 4549, QStringLiteral( "China Geodetic Coordinate System 2000, 3-degree GK zone 27" ), QStringLiteral( "China" ) },
      { QStringLiteral( "CGCS2000 / 3-degree GK Zone 28" ), 4550, QStringLiteral( "China Geodetic Coordinate System 2000, 3-degree GK zone 28" ), QStringLiteral( "China" ) },
      { QStringLiteral( "CGCS2000 / 3-degree GK Zone 29" ), 4551, QStringLiteral( "China Geodetic Coordinate System 2000, 3-degree GK zone 29" ), QStringLiteral( "China" ) },
      { QStringLiteral( "CGCS2000 / 3-degree GK Zone 30" ), 4552, QStringLiteral( "China Geodetic Coordinate System 2000, 3-degree GK zone 30" ), QStringLiteral( "China" ) },
      { QStringLiteral( "CGCS2000 / 3-degree GK Zone 31" ), 4553, QStringLiteral( "China Geodetic Coordinate System 2000, 3-degree GK zone 31" ), QStringLiteral( "China" ) },
      { QStringLiteral( "CGCS2000 / 3-degree GK Zone 32" ), 4554, QStringLiteral( "China Geodetic Coordinate System 2000, 3-degree GK zone 32" ), QStringLiteral( "China" ) },
      { QStringLiteral( "CGCS2000 / 3-degree GK Zone 33" ), 4555, QStringLiteral( "China Geodetic Coordinate System 2000, 3-degree GK zone 33" ), QStringLiteral( "China" ) },

      // -- China: Beijing 1954 / Gauss-Kruger zones 13-23 --
      { QStringLiteral( "Beijing 1954 / GK Zone 13" ), 21413, QStringLiteral( "Beijing 1954 Gauss-Kruger zone 13" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Beijing 1954 / GK Zone 14" ), 21414, QStringLiteral( "Beijing 1954 Gauss-Kruger zone 14" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Beijing 1954 / GK Zone 15" ), 21415, QStringLiteral( "Beijing 1954 Gauss-Kruger zone 15" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Beijing 1954 / GK Zone 16" ), 21416, QStringLiteral( "Beijing 1954 Gauss-Kruger zone 16" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Beijing 1954 / GK Zone 17" ), 21417, QStringLiteral( "Beijing 1954 Gauss-Kruger zone 17" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Beijing 1954 / GK Zone 18" ), 21418, QStringLiteral( "Beijing 1954 Gauss-Kruger zone 18" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Beijing 1954 / GK Zone 19" ), 21419, QStringLiteral( "Beijing 1954 Gauss-Kruger zone 19" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Beijing 1954 / GK Zone 20" ), 21420, QStringLiteral( "Beijing 1954 Gauss-Kruger zone 20" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Beijing 1954 / GK Zone 21" ), 21421, QStringLiteral( "Beijing 1954 Gauss-Kruger zone 21" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Beijing 1954 / GK Zone 22" ), 21422, QStringLiteral( "Beijing 1954 Gauss-Kruger zone 22" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Beijing 1954 / GK Zone 23" ), 21423, QStringLiteral( "Beijing 1954 Gauss-Kruger zone 23" ), QStringLiteral( "China" ) },

      // -- China: Xian 1980 / Gauss-Kruger zones 13-23 --
      { QStringLiteral( "Xian 1980 / GK Zone 13" ), 2327, QStringLiteral( "Xian 1980 Gauss-Kruger zone 13" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Xian 1980 / GK Zone 14" ), 2328, QStringLiteral( "Xian 1980 Gauss-Kruger zone 14" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Xian 1980 / GK Zone 15" ), 2329, QStringLiteral( "Xian 1980 Gauss-Kruger zone 15" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Xian 1980 / GK Zone 16" ), 2330, QStringLiteral( "Xian 1980 Gauss-Kruger zone 16" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Xian 1980 / GK Zone 17" ), 2331, QStringLiteral( "Xian 1980 Gauss-Kruger zone 17" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Xian 1980 / GK Zone 18" ), 2332, QStringLiteral( "Xian 1980 Gauss-Kruger zone 18" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Xian 1980 / GK Zone 19" ), 2333, QStringLiteral( "Xian 1980 Gauss-Kruger zone 19" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Xian 1980 / GK Zone 20" ), 2334, QStringLiteral( "Xian 1980 Gauss-Kruger zone 20" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Xian 1980 / GK Zone 21" ), 2335, QStringLiteral( "Xian 1980 Gauss-Kruger zone 21" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Xian 1980 / GK Zone 22" ), 2336, QStringLiteral( "Xian 1980 Gauss-Kruger zone 22" ), QStringLiteral( "China" ) },
      { QStringLiteral( "Xian 1980 / GK Zone 23" ), 2337, QStringLiteral( "Xian 1980 Gauss-Kruger zone 23" ), QStringLiteral( "China" ) },

      // -- Regional --
      { QStringLiteral( "NAD83" ),    4269, QStringLiteral( "North American Datum 1983" ),                QStringLiteral( "Regional" ) },
      { QStringLiteral( "ETRS89" ),   4258, QStringLiteral( "European Terrestrial Reference System 1989" ), QStringLiteral( "Regional" ) },
      { QStringLiteral( "GDA2020" ), 7844, QStringLiteral( "Geocentric Datum of Australia 2020" ),       QStringLiteral( "Regional" ) },
    };
    return sPresets;
  }
} // anonymous namespace


QList<CrsPreset> CrsPresets::allPresets()
{
  return presets();
}

QList<CrsPreset> CrsPresets::presetsByCategory( const QString &category )
{
  QList<CrsPreset> result;
  for ( const auto &p : presets() )
  {
    if ( p.category == category )
      result.append( p );
  }
  return result;
}

QStringList CrsPresets::categories()
{
  QSet<QString> seen;
  QStringList result;
  for ( const auto &p : presets() )
  {
    if ( !seen.contains( p.category ) )
    {
      seen.insert( p.category );
      result.append( p.category );
    }
  }
  return result;
}

std::optional<CrsPreset> CrsPresets::presetForEpsg( int epsg )
{
  for ( const auto &p : presets() )
  {
    if ( p.epsgCode == epsg )
      return p;
  }
  return std::nullopt;
}

void CrsPresets::addRecentCrs( int epsg )
{
  if ( epsg <= 0 )
    return;

  QSettings settings( QStringLiteral( "SICNU" ), QStringLiteral( "RSStudio" ) );
  QStringList recent = settings.value( QStringLiteral( "recent_crs" ) ).toStringList();

  const QString epsgStr = QString::number( epsg );

  // Remove if already present, then prepend
  recent.removeAll( epsgStr );
  recent.prepend( epsgStr );

  // Limit to 10 entries
  while ( recent.size() > 10 )
    recent.removeLast();

  settings.setValue( QStringLiteral( "recent_crs" ), recent );
}

QList<CrsPreset> CrsPresets::recentPresets()
{
  QSettings settings( QStringLiteral( "SICNU" ), QStringLiteral( "RSStudio" ) );
  QStringList recent = settings.value( QStringLiteral( "recent_crs" ) ).toStringList();

  QList<CrsPreset> result;
  for ( const QString &epsgStr : recent )
  {
    bool ok = false;
    int epsg = epsgStr.toInt( &ok );
    if ( !ok || epsg <= 0 )
      continue;

    auto preset = presetForEpsg( epsg );
    if ( preset )
    {
      CrsPreset p = *preset;
      p.category = QStringLiteral( "Recently Used" );
      result.append( p );
    }
  }
  return result;
}
