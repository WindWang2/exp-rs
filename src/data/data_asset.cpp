#include "data_asset.h"

#include <utility>

#include <QJsonArray>
#include <QJsonObject>

namespace sicnu::data
{

QJsonObject RemoteMapStructure::toJson() const
{
  QJsonObject json;
  json.insert( QStringLiteral( "service" ), serviceToString( service ) );
  json.insert( QStringLiteral( "layerNames" ), QJsonArray::fromStringList( layerNames ) );
  json.insert( QStringLiteral( "crsList" ), QJsonArray::fromStringList( crsList ) );
  QJsonObject extentJson;
  extentJson.insert( QStringLiteral( "minimumX" ), extent.minimumX );
  extentJson.insert( QStringLiteral( "minimumY" ), extent.minimumY );
  extentJson.insert( QStringLiteral( "maximumX" ), extent.maximumX );
  extentJson.insert( QStringLiteral( "maximumY" ), extent.maximumY );
  extentJson.insert( QStringLiteral( "valid" ), extent.valid );
  json.insert( QStringLiteral( "extent" ), extentJson );
  json.insert( QStringLiteral( "imageFormat" ), imageFormat );
  if ( pixelSizeX.has_value() )
    json.insert( QStringLiteral( "pixelSizeX" ), pixelSizeX.value() );
  if ( pixelSizeY.has_value() )
    json.insert( QStringLiteral( "pixelSizeY" ), pixelSizeY.value() );
  json.insert( QStringLiteral( "zMin" ), zMin );
  json.insert( QStringLiteral( "zMax" ), zMax );
  json.insert( QStringLiteral( "valid" ), valid );
  return json;
}

Result<RemoteMapStructure> RemoteMapStructure::fromJson( const QJsonObject &json )
{
  RemoteMapStructure s;
  const QString serviceName =
    json.value( QStringLiteral( "service" ) ).toString();
  const std::optional<RemoteMapService> parsedService =
    serviceFromString( serviceName );
  if ( !parsedService.has_value() )
  {
    return Result<RemoteMapStructure>::failure(
      Diagnostic{ QStringLiteral( "remote_map.service_invalid" ),
                  QStringLiteral( "Unknown remote-map service kind: %1" )
                    .arg( serviceName ),
                  DiagnosticSeverity::Error } );
  }
  s.service = *parsedService;

  const QJsonArray layers = json.value( QStringLiteral( "layerNames" ) ).toArray();
  for ( const QJsonValue &layer : layers )
    s.layerNames.append( layer.toString() );
  const QJsonArray crss = json.value( QStringLiteral( "crsList" ) ).toArray();
  for ( const QJsonValue &crs : crss )
    s.crsList.append( crs.toString() );

  const QJsonObject extentJson =
    json.value( QStringLiteral( "extent" ) ).toObject();
  s.extent.minimumX = extentJson.value( QStringLiteral( "minimumX" ) ).toDouble();
  s.extent.minimumY = extentJson.value( QStringLiteral( "minimumY" ) ).toDouble();
  s.extent.maximumX = extentJson.value( QStringLiteral( "maximumX" ) ).toDouble();
  s.extent.maximumY = extentJson.value( QStringLiteral( "maximumY" ) ).toDouble();
  s.extent.valid = extentJson.value( QStringLiteral( "valid" ) ).toBool();

  s.imageFormat = json.value( QStringLiteral( "imageFormat" ) ).toString();
  if ( json.contains( QStringLiteral( "pixelSizeX" ) ) )
    s.pixelSizeX = json.value( QStringLiteral( "pixelSizeX" ) ).toDouble();
  if ( json.contains( QStringLiteral( "pixelSizeY" ) ) )
    s.pixelSizeY = json.value( QStringLiteral( "pixelSizeY" ) ).toDouble();
  s.zMin = json.value( QStringLiteral( "zMin" ) ).toInt();
  s.zMax = json.value( QStringLiteral( "zMax" ) ).toInt();
  s.valid = json.value( QStringLiteral( "valid" ) ).toBool();
  return Result<RemoteMapStructure>::success( std::move( s ) );
}

QString RemoteMapStructure::serviceToString( RemoteMapService kind )
{
  switch ( kind )
  {
    case RemoteMapService::Wms: return QStringLiteral( "wms" );
    case RemoteMapService::Wmts: return QStringLiteral( "wmts" );
    case RemoteMapService::Tms: return QStringLiteral( "tms" );
    case RemoteMapService::Xyz: return QStringLiteral( "xyz" );
  }
  return QStringLiteral( "wms" );
}

std::optional<RemoteMapService> RemoteMapStructure::serviceFromString(
  const QString &name )
{
  if ( name == QStringLiteral( "wms" ) ) return RemoteMapService::Wms;
  if ( name == QStringLiteral( "wmts" ) ) return RemoteMapService::Wmts;
  if ( name == QStringLiteral( "tms" ) ) return RemoteMapService::Tms;
  if ( name == QStringLiteral( "xyz" ) ) return RemoteMapService::Xyz;
  return std::nullopt;
}

} // namespace sicnu::data
