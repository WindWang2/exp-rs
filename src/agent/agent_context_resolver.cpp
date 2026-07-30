// src/agent/agent_context_resolver.cpp
#include "agent_context_resolver.h"

#include <QJsonArray>
#include <QJsonObject>

namespace sicnu::agent
{

QJsonObject AgentContextResolver::buildContextSnapshot( data::DataManager *dataManager, ActiveViewHost *viewHost )
{
  return WorkspaceSnapshot::capture( dataManager, viewHost ).toJson();
}

QString AgentContextResolver::formatSystemContextPrompt( const WorkspaceSnapshot &snapshot )
{
  return snapshot.toSystemPromptHeader();
}

QString AgentContextResolver::formatSystemContextPrompt( const QJsonObject &snapshot )
{
  QString prompt;
  prompt += QStringLiteral( "[WORKSPACE CONTEXT]\n" );

  if ( snapshot.contains( QStringLiteral( "assets" ) ) )
  {
    QJsonArray assets = snapshot[QStringLiteral( "assets" )].toArray();
    if ( assets.isEmpty() )
    {
      prompt += QStringLiteral( "Loaded Data Assets: (None)\n" );
    }
    else
    {
      prompt += QStringLiteral( "Loaded Data Assets:\n" );
      for ( const auto &val : assets )
      {
        QJsonObject obj = val.toObject();
        QString id = obj[QStringLiteral( "id" )].toString();
        QString name = obj[QStringLiteral( "displayName" )].toString();
        QString path = obj[QStringLiteral( "path" )].toString();
        QString kind = obj[QStringLiteral( "kind" )].toString();

        prompt += QString( "- Asset '%1' (%2) [%3]" ).arg( id, name, kind );

        if ( obj.contains( QStringLiteral( "bands" ) ) )
        {
          prompt += QString( " %1x%2, %3 bands" )
                      .arg( obj[QStringLiteral( "width" )].toInt() )
                      .arg( obj[QStringLiteral( "height" )].toInt() )
                      .arg( obj[QStringLiteral( "bands" )].toInt() );
        }
        else if ( obj.contains( QStringLiteral( "layerCount" ) ) )
        {
          prompt += QString( " %1 vector layers" )
                      .arg( obj[QStringLiteral( "layerCount" )].toInt() );
        }

        if ( !path.isEmpty() )
        {
          prompt += QString( ", Path: %1" ).arg( path );
        }
        prompt += QStringLiteral( "\n" );
      }
    }
  }

  if ( snapshot.contains( QStringLiteral( "mapView" ) ) )
  {
    QJsonObject mapObj = snapshot[QStringLiteral( "mapView" )].toObject();
    prompt += QStringLiteral( "Map View State:\n" );
    if ( mapObj.contains( QStringLiteral( "crs" ) ) )
      prompt += QString( "- CRS: %1\n" ).arg( mapObj[QStringLiteral( "crs" )].toString() );
    if ( mapObj.contains( QStringLiteral( "extent" ) ) )
      prompt += QString( "- Extent: %1\n" ).arg( mapObj[QStringLiteral( "extent" )].toString() );
    if ( mapObj.contains( QStringLiteral( "selectedLayer" ) ) )
      prompt += QString( "- Selected Layer: %1\n" ).arg( mapObj[QStringLiteral( "selectedLayer" )].toString() );
  }

  return prompt;
}

} // namespace sicnu::agent
