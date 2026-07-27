#include "remote_map_capabilities_parser.h"

#include <optional>

#include <QRegularExpression>
#include <QStringList>

#include <QtXml/QDomDocument>
#include <QtXml/QDomElement>

namespace sicnu::display
{

namespace
{

/// Returns the local part of a possibly-prefixed tag name ("ows:Identifier" ->
/// "Identifier"). QDomElement::tagName() preserves the prefix as written.
QString localName( const QString &tagName )
{
  const int colon = tagName.indexOf( QChar( ':' ) );
  return colon < 0 ? tagName : tagName.mid( colon + 1 );
}

/// Collects the text of every direct-or-descendant element named `tagName`
/// under `root`, ignoring empty text. Used for <CRS>/<SRS>/<Format> lists.
QStringList collectText( const QDomElement &root, const QString &tagName )
{
  QStringList result;
  const QDomNodeList nodes = root.elementsByTagName( tagName );
  for ( int i = 0; i < nodes.size(); ++i )
  {
    const QDomElement element = nodes.at( i ).toElement();
    if ( element.isNull() )
      continue;
    const QString text = element.text().trimmed();
    if ( !text.isEmpty() )
      result.append( text );
  }
  return result;
}

/// Collects the <Name> (WMS) or <ows:Identifier> (WMTS) of every named Layer.
/// Layers without a name/identifier are organizational and skipped. `childTag`
/// is the LOCAL name ("Name" or "Identifier"); the match tolerates a namespace
/// prefix (WMTS uses <ows:Identifier>), since QDomElement::tagName() keeps the
/// prefix as written.
QStringList collectLayerNames( const QDomElement &root, const QString &childTag )
{
  QStringList result;
  const QDomNodeList layers = root.elementsByTagName( QStringLiteral( "Layer" ) );
  for ( int i = 0; i < layers.size(); ++i )
  {
    const QDomElement layer = layers.at( i ).toElement();
    if ( layer.isNull() )
      continue;
    // Only a <Name>/<Identifier> that is a DIRECT child of this Layer counts;
    // descendants belong to nested layers (collected in their own iteration).
    // Tolerate a namespace prefix (ows:Identifier) by matching the local name.
    for ( QDomElement child = layer.firstChildElement();
          !child.isNull();
          child = child.nextSiblingElement() )
    {
      const QString local = localName( child.tagName() );
      if ( local != childTag )
        continue;
      const QString name = child.text().trimmed();
      if ( !name.isEmpty() )
      {
        result.append( name );
        break; // one name per layer
      }
    }
  }
  return result;
}

/// Parses a space-separated numeric extent text (WMS 1.3.0
/// EX_GeographicBoundingBox: west south east north).
std::optional<sicnu::data::SpatialExtent>
parseExtentText( const QString &text )
{
  const QStringList parts = text.split( QRegularExpression( "\\s+" ),
                                        Qt::SkipEmptyParts );
  if ( parts.size() != 4 )
    return std::nullopt;
  bool ok[4] = { false, false, false, false };
  sicnu::data::SpatialExtent extent;
  extent.minimumX = parts[0].toDouble( &ok[0] );
  extent.minimumY = parts[1].toDouble( &ok[1] );
  extent.maximumX = parts[2].toDouble( &ok[2] );
  extent.maximumY = parts[3].toDouble( &ok[3] );
  if ( !( ok[0] && ok[1] && ok[2] && ok[3] ) )
    return std::nullopt;
  extent.valid = true;
  return extent;
}

/// Builds the GetMap format list in document order (the first is the preferred
/// format the parser reports as imageFormat). Scoped to a <GetMap> request.
QStringList collectGetMapFormats( const QDomElement &root )
{
  QStringList formats;
  const QDomNodeList getMap =
      root.elementsByTagName( QStringLiteral( "GetMap" ) );
  if ( getMap.isEmpty() )
    return formats;
  formats = collectText( getMap.at( 0 ).toElement(),
                         QStringLiteral( "Format" ) );
  return formats;
}

} // namespace

data::RemoteMapStructure parseWmsCapabilities( const QByteArray &xml )
{
  data::RemoteMapStructure structure;
  structure.service = data::RemoteMapService::Wms;

  QDomDocument doc;
  QString errorMsg;
  int errorLine = 0;
  int errorColumn = 0;
  if ( !doc.setContent( xml, &errorMsg, &errorLine, &errorColumn ) )
    return structure; // malformed / empty XML

  // A WMS Capabilities root is <WMS_Capabilities> (1.3.0) or
  // <WMT_MS_Capabilities> (1.1.1).
  const QDomElement root = doc.documentElement();
  const QString rootName = root.tagName().toLower();
  if ( rootName != QStringLiteral( "wms_capabilities" ) &&
       rootName != QStringLiteral( "wmt_ms_capabilities" ) )
    return structure; // not a WMS Capabilities document

  structure.layerNames = collectLayerNames( root, QStringLiteral( "Name" ) );

  // CRS (1.3.0) or SRS (1.1.1). Prefer 1.3.0 <CRS> when present; fall back to
  // <SRS>. Both may appear multiple times at the root layer.
  structure.crsList = collectText( root, QStringLiteral( "CRS" ) );
  if ( structure.crsList.isEmpty() )
    structure.crsList = collectText( root, QStringLiteral( "SRS" ) );

  const QStringList formats = collectGetMapFormats( root );
  if ( !formats.isEmpty() )
    structure.imageFormat = formats.first();

  // EX_GeographicBoundingBox (1.3.0 text) or LatLonBoundingBox (1.1.1 attrs).
  const QDomNodeList exGeo =
      root.elementsByTagName( QStringLiteral( "EX_GeographicBoundingBox" ) );
  if ( !exGeo.isEmpty() )
  {
    structure.extent =
        parseExtentText( exGeo.at( 0 ).toElement().text() ).value_or( structure.extent );
  }
  else
  {
    const QDomNodeList latLon =
        root.elementsByTagName( QStringLiteral( "LatLonBoundingBox" ) );
    if ( !latLon.isEmpty() )
    {
      const QDomElement bb = latLon.at( 0 ).toElement();
      bool ok[4] = { false, false, false, false };
      structure.extent.minimumX = bb.attribute( QStringLiteral( "minx" ) ).toDouble( &ok[0] );
      structure.extent.minimumY = bb.attribute( QStringLiteral( "miny" ) ).toDouble( &ok[1] );
      structure.extent.maximumX = bb.attribute( QStringLiteral( "maxx" ) ).toDouble( &ok[2] );
      structure.extent.maximumY = bb.attribute( QStringLiteral( "maxy" ) ).toDouble( &ok[3] );
      structure.extent.valid = ok[0] && ok[1] && ok[2] && ok[3];
    }
  }

  structure.valid = true;
  return structure;
}

data::RemoteMapStructure parseWmtsCapabilities( const QByteArray &xml )
{
  data::RemoteMapStructure structure;
  structure.service = data::RemoteMapService::Wmts;

  QDomDocument doc;
  QString errorMsg;
  int errorLine = 0;
  int errorColumn = 0;
  if ( !doc.setContent( xml, &errorMsg, &errorLine, &errorColumn ) )
    return structure;

  const QDomElement root = doc.documentElement();
  // A WMTS Capabilities root is <Capabilities> under the WMTS namespace. We do
  // not require the namespace URI (services vary); the local name is enough for
  // a reachability+metadata parse.
  if ( root.tagName().toLower() != QStringLiteral( "capabilities" ) )
    return structure;

  // WMTS names layers via <Layer>/<ows:Identifier>. elementsByTagName matches
  // the local tag name regardless of namespace prefix.
  structure.layerNames =
      collectLayerNames( root, QStringLiteral( "Identifier" ) );

  // The layer's advertised <Format> (image/png etc.). There may be several;
  // the first is the preferred format.
  const QDomNodeList layers =
      root.elementsByTagName( QStringLiteral( "Layer" ) );
  if ( !layers.isEmpty() )
  {
    const QStringList formats =
        collectText( layers.at( 0 ).toElement(), QStringLiteral( "Format" ) );
    if ( !formats.isEmpty() )
      structure.imageFormat = formats.first();
  }

  // TileMatrixSet parsing (pixelSizeX/Y, z-range) is a documented follow-up.
  structure.valid = true;
  return structure;
}

} // namespace sicnu::display
