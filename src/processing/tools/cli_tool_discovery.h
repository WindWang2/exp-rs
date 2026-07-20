#pragma once

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace CliToolDiscovery
{

QStringList discoverOtbApplicationNames();
QStringList discoverGdalToolNames();

QJsonObject makeOtbDiscoveredConfig( const QString &applicationName );
QJsonObject makeGdalDiscoveredConfig( const QString &toolName );

QString otbAlgorithmId( const QString &applicationName );
QString gdalAlgorithmId( const QString &toolName );

QSet<QString> handcraftedOtbApplicationNames();
QSet<QString> handcraftedGdalToolNames();

} // namespace CliToolDiscovery