// src/processing/algorithms/temporal/temporal_stac_adapter.cpp
#include "temporal_stac_adapter.h"

#include <QDate>
#include <QDateTime>

#include <algorithm>
#include <functional>
#include <map>

namespace sicnu::temporal
{

namespace
{

/// Selects the raster asset a temporal scene consumes: prefer an asset whose
/// type/media field says GeoTIFF/COG, else the first asset whose href ends in
/// a raster-ish extension (.tif/.tiff/.jp2), else null.
struct AssetPick
{
    QString key;
    QString href;
    QString type;
    QStringList bands;
};

AssetPick pickRasterAsset( const Json::Value &assets )
{
  AssetPick pick;
  if ( !assets.isObject() )
    return pick;

  // Pass 1: explicit TIFF/COG media type (in asset-key order).
  // Pass 2: raster-ish href extension (in asset-key order).
  for ( int pass = 0; pass < 2 && pick.href.isEmpty(); ++pass )
  {
    for ( const std::string &key : assets.getMemberNames() )
    {
      const Json::Value &asset = assets[key];
      if ( !asset.isObject() || !asset.isMember( "href" ) || !asset["href"].isString() )
        continue;
      const QString href = QString::fromStdString( asset["href"].asString() );
      const QString type = asset.isMember( "type" ) && asset["type"].isString()
                             ? QString::fromStdString( asset["type"].asString() )
                             : QString();
      const bool tiffType = type.contains( QLatin1String( "tiff" ), Qt::CaseInsensitive );
      const bool rasterExt = href.endsWith( QLatin1String( ".tif" ), Qt::CaseInsensitive ) ||
                             href.endsWith( QLatin1String( ".tiff" ), Qt::CaseInsensitive ) ||
                             href.endsWith( QLatin1String( ".jp2" ), Qt::CaseInsensitive );
      if ( ( pass == 0 && tiffType ) || ( pass == 1 && rasterExt ) )
      {
        pick.key = QString::fromStdString( key );
        pick.href = href;
        pick.type = type;
        const Json::Value &bands = asset["eo:bands"];
        if ( bands.isArray() )
        {
          for ( const Json::Value &b : bands )
          {
            if ( b.isObject() && b.isMember( "name" ) && b["name"].isString() )
              pick.bands << QString::fromStdString( b["name"].asString() );
            else if ( b.isString() )
              pick.bands << QString::fromStdString( b.asString() );
          }
        }
        return pick;
      }
    }
  }
  return pick;
}

QString propertiesString( const Json::Value &properties, const char *key )
{
  return properties.isMember( key ) && properties[key].isString()
           ? QString::fromStdString( properties[key].asString() )
           : QString();
}

double propertiesDouble( const Json::Value &properties, const char *key )
{
  if ( properties.isMember( key ) && properties[key].isNumeric() )
    return properties[key].asDouble();
  return -1.0;
}

} // namespace

bool parseStacItem( const Json::Value &feature, StacItem *out, QString *error )
{
  if ( !feature.isObject() )
  {
    if ( error )
      *error = QStringLiteral( "STAC item is not a JSON object" );
    return false;
  }
  const AssetPick pick = pickRasterAsset( feature["assets"] );
  if ( pick.href.isEmpty() )
  {
    if ( error )
      *error = QStringLiteral( "STAC item has no raster asset (tif/tiff/jp2 href or TIFF type)" );
    return false;
  }
  StacItem item;
  item.id = feature.isMember( "id" ) && feature["id"].isString()
              ? QString::fromStdString( feature["id"].asString() )
              : pick.key;
  item.datetime = propertiesString( feature["properties"], "datetime" );
  if ( item.datetime.isEmpty() )
  {
    if ( error )
      *error = QStringLiteral( "STAC item '%1' has no properties.datetime — acquisition time "
                               "is mandatory scientific metadata" )
                 .arg( item.id );
    return false;
  }
  item.platform = propertiesString( feature["properties"], "platform" );
  item.processingLevel = propertiesString( feature["properties"], "s2:processing_level" );
  if ( item.processingLevel.isEmpty() )
    item.processingLevel = propertiesString( feature["properties"], "processing:level" );
  item.cloudCover = propertiesDouble( feature["properties"], "eo:cloud_cover" );
  item.rasterAssetKey = pick.key;
  item.rasterBands = pick.bands;
  item.rasterHref = pick.href;

  // Footprint bounds + stringified properties (for client-side filters).
  const std::function<void( const Json::Value & )> walk = [&]( const Json::Value &node ) {
    if ( node.isArray() && node.size() >= 2 && node[0].isNumeric() && node[1].isNumeric() )
    {
      const double x = node[0].asDouble();
      const double y = node[1].asDouble();
      if ( !item.hasGeometry )
      {
        item.minX = item.maxX = x;
        item.minY = item.maxY = y;
        item.hasGeometry = true;
      }
      else
      {
        item.minX = std::min( item.minX, x );
        item.maxX = std::max( item.maxX, x );
        item.minY = std::min( item.minY, y );
        item.maxY = std::max( item.maxY, y );
      }
      return;
    }
    if ( node.isArray() )
      for ( const Json::Value &child : node )
        walk( child );
  };
  walk( feature["geometry"]["coordinates"] );
  if ( feature["properties"].isObject() )
  {
    for ( const std::string &key : feature["properties"].getMemberNames() )
    {
      const Json::Value &v = feature["properties"][key];
      if ( v.isString() )
        item.properties[QString::fromStdString( key )] = QString::fromStdString( v.asString() );
      else if ( v.isNumeric() )
        item.properties[QString::fromStdString( key )] = QString::number( v.asDouble() );
      else if ( v.isBool() )
        item.properties[QString::fromStdString( key )] = v.asBool() ? QStringLiteral( "true" )
                                                                     : QStringLiteral( "false" );
    }
  }
  if ( out )
    *out = std::move( item );
  return true;
}

QVector<StacItem> filterStacItems( const QVector<StacItem> &items, const QString &bbox,
                                   const QString &datetime, int limit,
                                   const QString &propertyFilter, QStringList *warnings )
{
  QVector<StacItem> filtered = items;

  // bbox: "minx,miny,maxx,maxy" — drop items whose footprint does not
  // intersect; items without geometry are kept (nothing to test).
  if ( !bbox.trimmed().isEmpty() )
  {
    const QStringList parts = bbox.split( QLatin1Char( ',' ) );
    bool filterOk = parts.size() == 4;
    double minx = 0, miny = 0, maxx = 0, maxy = 0;
    if ( filterOk )
    {
      for ( int i = 0; i < 4; ++i )
      {
        bool partOk = false;
        const double v = parts[i].trimmed().toDouble( &partOk );
        if ( !partOk )
        {
          filterOk = false;
          break;
        }
        switch ( i )
        {
          case 0: minx = v; break;
          case 1: miny = v; break;
          case 2: maxx = v; break;
          case 3: maxy = v; break;
        }
      }
    }
    if ( !filterOk || maxx < minx || maxy < miny )
    {
      if ( warnings )
        *warnings << QStringLiteral( "malformed bbox filter '%1' — ignored" ).arg( bbox );
    }
    else
    {
      QVector<StacItem> byBbox;
      for ( const StacItem &item : filtered )
      {
        if ( !item.hasGeometry ||
             !( item.maxX < minx || item.minX > maxx || item.maxY < miny || item.minY > maxy ) )
          byBbox.append( item );
      }
      filtered = byBbox;
    }
  }

  // datetime: "start/end" (either side may be empty or "..") or a single
  // instant / date (a bare date matches that calendar day).
  if ( !datetime.trimmed().isEmpty() )
  {
    const auto parseDt = []( const QString &text ) {
      QDateTime t = QDateTime::fromString( text, Qt::ISODateWithMs );
      if ( !t.isValid() )
      {
        const QDate d = QDate::fromString( text, Qt::ISODate );
        if ( d.isValid() )
          t = QDateTime( d, QTime( 0, 0 ), Qt::UTC );
      }
      return t;
    };
    const int sep = datetime.indexOf( QLatin1Char( '/' ) );
    QDateTime start, end;
    bool filterOk = true;
    if ( sep >= 0 )
    {
      const QString startText = datetime.left( sep ).trimmed();
      const QString endText = datetime.mid( sep + 1 ).trimmed();
      if ( !startText.isEmpty() && startText != QLatin1String( ".." ) )
        start = parseDt( startText );
      if ( !endText.isEmpty() && endText != QLatin1String( ".." ) )
      {
        end = parseDt( endText );
        if ( end.isValid() && end.time().isNull() )
          end = end.addDays( 1 ); // end date is inclusive
      }
      filterOk = ( start.isNull() || start.isValid() ) && ( end.isNull() || end.isValid() );
    }
    else
    {
      start = parseDt( datetime.trimmed() );
      filterOk = start.isValid();
      // A bare date ("2024-03-15") matches the whole calendar day:
      // the day parsed as 2024-03-15T00:00, so end is the next midnight
      // (range behaviour reused). Otherwise it is a single instant.
      bool isSingleDate = datetime.trimmed().indexOf( QLatin1Char( 'T' ) ) < 0
                          && datetime.trimmed().indexOf( QLatin1Char( ':' ) ) < 0;
      if ( isSingleDate && start.isValid() )
        end = start.addDays( 1 );
      else
        end = start;
    }
    if ( !filterOk )
    {
      if ( warnings )
        *warnings << QStringLiteral( "malformed datetime filter '%1' — ignored" ).arg( datetime );
    }
    else
    {
      QVector<StacItem> byDate;
      for ( const StacItem &item : filtered )
      {
        QDateTime t = QDateTime::fromString( item.datetime, Qt::ISODateWithMs );
        if ( !t.isValid() )
          t = QDateTime::fromString( item.datetime, Qt::ISODate );
        if ( !t.isValid() )
          continue;
        if ( start.isValid() && t < start )
          continue;
        if ( end.isValid() && t > end )
          continue;
        byDate.append( item );
      }
      filtered = byDate;
    }
  }

  // propertyFilter: "key=value" — string equality, or numeric comparison
  // when both sides parse as numbers (eo:cloud_cover<=10 style ranges are a
  // documented follow-up; this is the minimal reliable filter).
  if ( !propertyFilter.trimmed().isEmpty() )
  {
    const int eq = propertyFilter.indexOf( QLatin1Char( '=' ) );
    if ( eq <= 0 )
    {
      if ( warnings )
        *warnings << QStringLiteral( "malformed propertyFilter '%1' — ignored" )
                       .arg( propertyFilter );
    }
    else
    {
      const QString key = propertyFilter.left( eq ).trimmed();
      QString value = propertyFilter.mid( eq + 1 ).trimmed();
      bool valueIsNumber = false;
      const double number = value.toDouble( &valueIsNumber );
      QVector<StacItem> byProperty;
      for ( const StacItem &item : filtered )
      {
        const auto it = item.properties.find( key );
        if ( it == item.properties.end() )
          continue;
        if ( valueIsNumber )
        {
          bool itemOk = false;
          double itemNumber = it->second.toDouble( &itemOk );
          if ( itemOk && itemNumber == number )
            byProperty.append( item );
        }
        else if ( it->second.compare( value, Qt::CaseInsensitive ) == 0 )
        {
          byProperty.append( item );
        }
      }
      filtered = byProperty;
    }
  }

  // Chronological sort (missing datetimes last, id tie-break).
  std::stable_sort( filtered.begin(), filtered.end(),
                    []( const StacItem &a, const StacItem &b ) {
                      const QDateTime ta = QDateTime::fromString( a.datetime, Qt::ISODateWithMs );
                      const QDateTime tb = QDateTime::fromString( b.datetime, Qt::ISODateWithMs );
                      const bool va = ta.isValid();
                      const bool vb = tb.isValid();
                      if ( va != vb )
                        return va;
                      if ( va && ta != tb )
                        return ta < tb;
                      return a.id < b.id;
                    } );

  if ( limit > 0 && filtered.size() > limit )
    filtered.resize( limit );
  return filtered;
}

bool temporalCollectionFromStacItems( const QVector<StacItem> &items,
                                      const QString &name, TemporalCollection *out,
                                      QString *error )
{
  if ( items.size() < 2 )
  {
    if ( error )
      *error = QStringLiteral( "a temporal collection needs at least 2 scenes; got %1" )
                 .arg( items.size() );
    return false;
  }
  TemporalCollection collection;
  collection.setName( name );
  int index = 0;
  for ( const StacItem &item : items )
  {
    TemporalSceneRef scene;
    scene.path = item.rasterHref; // remote /vsicurl/ or local href — the pixel owner
    scene.time = parseAcquisitionTime( item.datetime );
    scene.timeSource = QStringLiteral( "stac" );
    scene.platform = item.platform;
    scene.processingLevel = item.processingLevel;
    scene.originalIndex = index++;
    // eo:bands ride as explicit band-name overrides where the naming maps to
    // the platform's role vocabulary; the shared resolver still warns for
    // roles it cannot resolve.
    for ( int i = 0; i < item.rasterBands.size(); ++i )
    {
      // Only record when the band name looks like a role id the resolver
      // understands (lowercase letters/digits); positional metadata stays
      // with the raster itself.
      const QString band = item.rasterBands.at( i );
      const bool roleLike = !band.isEmpty() &&
                            std::all_of( band.cbegin(), band.cend(), []( QChar c ) {
                              return c.isLower() || c.isDigit() || c == QLatin1Char( '_' );
                            } );
      if ( roleLike )
        scene.bandOverrides[band] = i + 1;
    }
    collection.scenes().push_back( std::move( scene ) );
  }
  collection.sortScenes();
  if ( out )
    *out = std::move( collection );
  return true;
}

bool temporalCollectionFromStacSearch( const Json::Value &searchResponse,
                                       const QString &name, TemporalCollection *out,
                                       QString *error, QStringList *warnings )
{
  Json::Value features = searchResponse["features"];
  if ( !features.isArray() )
  {
    // A bare array of features is accepted too (saved/partial results).
    if ( searchResponse.isArray() )
      features = searchResponse;
    else
    {
      if ( error )
        *error = QStringLiteral( "not a STAC search response (missing features array)" );
      return false;
    }
  }

  QVector<StacItem> items;
  for ( const Json::Value &feature : features )
  {
    StacItem item;
    QString itemError;
    if ( !parseStacItem( feature, &item, &itemError ) )
    {
      if ( warnings )
        *warnings << itemError;
      continue;
    }
    items.append( item );
  }
  if ( items.isEmpty() )
  {
    if ( error )
      *error = QStringLiteral( "no usable STAC items in the search response" );
    return false;
  }
  return temporalCollectionFromStacItems( items, name, out, error );
}

} // namespace sicnu::temporal
