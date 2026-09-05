// src/processing/algorithms/temporal/temporal_stac_adapter.cpp
#include "temporal_stac_adapter.h"

#include "spatiotemporal_contracts.h"
#include "data/providers/gdal_raster_source_provider.h"

#include <QDate>
#include <QDateTime>
#include <QTime>

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
    QString title;
    QStringList roles;
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
        if ( asset.isMember( "title" ) && asset["title"].isString() )
          pick.title = QString::fromStdString( asset["title"].asString() );
        const Json::Value &roles = asset["roles"];
        if ( roles.isArray() )
        {
          for ( const Json::Value &r : roles )
          {
            if ( r.isString() )
              pick.roles << QString::fromStdString( r.asString() );
          }
        }
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

QStringList propertiesStringArray( const Json::Value &properties, const char *key )
{
  QStringList out;
  if ( properties.isMember( key ) && properties[key].isArray() )
  {
    for ( const Json::Value &v : properties[key] )
    {
      if ( v.isString() )
        out << QString::fromStdString( v.asString() );
    }
  }
  return out;
}

/// Multimodal STAC → observation vocabulary (Platform 3.0). Deterministic,
/// documented precedence:
///   1. sar:polarizations present                      → "sar"
///   2. platform/sensor/id matches the SAR vocabulary  → "sar"
///   3. properties.data_type == "dem", the selected asset advertises a DEM
///      token (key/title/type/roles), or the platform/id matches the DEM
///      vocabulary                                    → "dem"
///   4. platform matches the optical vocabulary        → "optical"
///   5. else "" (unknown — contracts may still infer from scene clues)
QString inferStacModality( const StacItem &item, const AssetPick &pick,
                           const Json::Value &properties )
{
  const QString platform = item.platform;
  const QString combined = QStringLiteral( "%1 %2 %3" ).arg( platform, item.sensor, item.id );
  const bool sarTokens =
    combined.contains( QLatin1String( "SENTINEL-1" ), Qt::CaseInsensitive ) ||
    combined.contains( QLatin1String( "SENTINEL 1" ), Qt::CaseInsensitive ) ||
    combined.contains( QLatin1String( "ALOS" ), Qt::CaseInsensitive ) ||
    combined.contains( QLatin1String( "TERRASAR" ), Qt::CaseInsensitive ) ||
    combined.contains( QLatin1String( "RADARSAT" ), Qt::CaseInsensitive ) ||
    combined.contains( QLatin1String( "GAOFEN-3" ), Qt::CaseInsensitive );
  if ( !item.polarizations.isEmpty() )
    return QStringLiteral( "sar" );
  if ( sarTokens )
    return QStringLiteral( "sar" );
  const QString assetText = QStringLiteral( "%1 %2 %3 %4" )
                              .arg( pick.key, pick.title, pick.type, pick.roles.join( QLatin1Char( ' ' ) ) );
  const bool demAsset = assetText.contains( QLatin1String( "dem" ), Qt::CaseInsensitive ) ||
                        assetText.contains( QLatin1String( "elevation" ), Qt::CaseInsensitive );
  const bool demProperties =
    ( properties.isMember( "data_type" ) && properties["data_type"].isString() &&
      QString::fromStdString( properties["data_type"].asString() ).compare( QLatin1String( "dem" ),
                                                                            Qt::CaseInsensitive ) == 0 );
  const bool demPlatform =
    combined.contains( QLatin1String( "COPERNICUS DEM" ), Qt::CaseInsensitive ) ||
    combined.contains( QLatin1String( "SRTM" ), Qt::CaseInsensitive ) ||
    combined.contains( QLatin1String( "NASADEM" ), Qt::CaseInsensitive ) ||
    combined.contains( QLatin1String( "GDEM" ), Qt::CaseInsensitive ) ||
    combined.contains( QLatin1String( "AW3D" ), Qt::CaseInsensitive );
  if ( demAsset || demProperties || demPlatform )
    return QStringLiteral( "dem" );
  if ( platform.contains( QLatin1String( "SENTINEL-2" ), Qt::CaseInsensitive ) ||
       platform.contains( QLatin1String( "SENTINEL 2" ), Qt::CaseInsensitive ) ||
       platform.contains( QLatin1String( "LANDSAT" ), Qt::CaseInsensitive ) ||
       platform.contains( QLatin1String( "MODIS" ), Qt::CaseInsensitive ) ||
       platform.contains( QLatin1String( "PLANET" ), Qt::CaseInsensitive ) ||
       platform.contains( QLatin1String( "WORLDVIEW" ), Qt::CaseInsensitive ) ||
       platform.contains( QLatin1String( "GF-1" ), Qt::CaseInsensitive ) ||
       platform.contains( QLatin1String( "GF-2" ), Qt::CaseInsensitive ) ||
       platform.contains( QLatin1String( "GF-6" ), Qt::CaseInsensitive ) ||
       platform.contains( QLatin1String( "ZY-3" ), Qt::CaseInsensitive ) ||
       platform.contains( QLatin1String( "SENTINEL-3" ), Qt::CaseInsensitive ) )
    return QStringLiteral( "optical" );
  return QString();
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
  // Multimodal observation attributes (Platform 3.0): sar:polarizations,
  // sar:instrument_mode, eo:gsd, then modality via the documented precedence.
  item.polarizations = normalizePolarizations(
    propertiesStringArray( feature["properties"], "sar:polarizations" ) );
  item.sensor = propertiesString( feature["properties"], "sar:instrument_mode" );
  if ( item.sensor.isEmpty() )
    item.sensor = propertiesString( feature["properties"], "sar:frequency_band" );
  const double gsd = propertiesDouble( feature["properties"], "eo:gsd" );
  item.gsd = gsd > 0.0 ? gsd : 0.0;
  item.modality = inferStacModality( item, pick, feature["properties"] );
  item.rasterHref =
    sicnu::data::providers::GdalRasterSourceProvider::normalizeRemoteRasterSource( pick.href );

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
        item.properties[QString::fromStdString( key )] =
          QString::number( v.asDouble(), 'g', 17 );
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
    struct ParsedBound
    {
      QDateTime dt;
      bool wholeDay = false;
    };
    // Date-only inputs are flagged from the *text* (no 'T', no ':'). QTime(0,0)
    // is valid and non-null, so time().isNull() cannot detect a date-only bound.
    const auto parseBound = []( const QString &text ) {
      ParsedBound out;
      if ( text.isEmpty() || text == QLatin1String( ".." ) )
        return out;
      const bool dateOnly = text.indexOf( QLatin1Char( 'T' ) ) < 0
                            && text.indexOf( QLatin1Char( ':' ) ) < 0;
      if ( dateOnly )
      {
        const QDate d = QDate::fromString( text, Qt::ISODate );
        if ( d.isValid() )
        {
          out.dt = QDateTime( d, QTime( 0, 0 ), Qt::UTC );
          out.wholeDay = true;
        }
        return out;
      }
      out.dt = QDateTime::fromString( text, Qt::ISODateWithMs );
      if ( !out.dt.isValid() )
        out.dt = QDateTime::fromString( text, Qt::ISODate );
      return out;
    };
    const int sep = datetime.indexOf( QLatin1Char( '/' ) );
    QDateTime start, end;
    bool filterOk = true;
    if ( sep >= 0 )
    {
      const QString startText = datetime.left( sep ).trimmed();
      const QString endText = datetime.mid( sep + 1 ).trimmed();
      const bool startSpecified = !startText.isEmpty() && startText != QLatin1String( ".." );
      const bool endSpecified = !endText.isEmpty() && endText != QLatin1String( ".." );
      if ( startSpecified )
      {
        const ParsedBound parsed = parseBound( startText );
        if ( !parsed.dt.isValid() )
          filterOk = false;
        else
          start = parsed.dt;
      }
      if ( endSpecified )
      {
        const ParsedBound parsed = parseBound( endText );
        if ( !parsed.dt.isValid() )
          filterOk = false;
        else
          // A date-only end bound includes that whole calendar day
          // (half-open [start, next-midnight], matching the single-date path).
          end = parsed.wholeDay ? parsed.dt.addDays( 1 ) : parsed.dt;
      }
    }
    else
    {
      const ParsedBound parsed = parseBound( datetime.trimmed() );
      filterOk = parsed.dt.isValid();
      start = parsed.dt;
      end = parsed.wholeDay ? parsed.dt.addDays( 1 ) : parsed.dt;
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
    scene.cloudCoverPercent = item.cloudCover;
    // Multimodal observation contract: STAC-declared attributes ride on the
    // scene ref (serialized only-when-claimed, descriptor stays v1-stable).
    scene.modality = item.modality;
    scene.sensor = item.sensor;
    scene.polarizations = item.polarizations;
    scene.resolutionMeters = item.gsd;
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
