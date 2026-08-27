/***************************************************************************
                         qgsdatasourceresolver.cpp
                         --------------------------
    begin                : August 2026
    copyright            : (C) 2026 by SICNU Geo RS contributors
    email                : sicnu-geo-rs at sicnu.edu.cn
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsdatasourceresolver.h"

QgsDataSourceKind QgsDataSourceResolver::classify( const QString &source )
{
  if ( source.isEmpty() )
    return QgsDataSourceKind::LocalFile;

  // 2. GDAL VSI path: anything starting with /vsi (covers /vsimem/, /vsizip/, /vsicurl/, /vsis3/, /vsigs/, /vsiaz/, /vsicurl_streaming/, etc.)
  if ( source.startsWith( QStringLiteral( "/vsi" ), Qt::CaseInsensitive ) )
    return QgsDataSourceKind::GdalVirtualPath;

  // 3. Windows drive path: ^[A-Za-z]:[\\/] or ^[A-Za-z]:$  (must precede OGR check to avoid "C:/..." being misclassified)
  if ( source.size() >= 2 && source.at( 0 ).isLetter() && source.at( 1 ) == QLatin1Char( ':' ) )
  {
    if ( source.size() == 2 || source.at( 2 ) == QLatin1Char( '/' ) || source.at( 2 ) == QLatin1Char( '\\' ) )
      return QgsDataSourceKind::LocalFile;
  }

  // 4. OGR connection strings
  if ( source.startsWith( QStringLiteral( "PG:" ), Qt::CaseInsensitive )
       || source.startsWith( QStringLiteral( "MySQL:" ), Qt::CaseInsensitive )
       || source.startsWith( QStringLiteral( "MSSQL:" ), Qt::CaseInsensitive )
       || source.startsWith( QStringLiteral( "OCI:" ), Qt::CaseInsensitive )
       || source.startsWith( QStringLiteral( "ODBC:" ), Qt::CaseInsensitive )
       || source.startsWith( QStringLiteral( "WFS:" ), Qt::CaseInsensitive )
       || source.startsWith( QStringLiteral( "GPKG:" ), Qt::CaseInsensitive )
       || source.startsWith( QStringLiteral( "SQLite:" ), Qt::CaseInsensitive ) )
    return QgsDataSourceKind::OgrConnectionString;

  // 5. Remote URIs
  if ( source.startsWith( QStringLiteral( "http://" ), Qt::CaseInsensitive )
       || source.startsWith( QStringLiteral( "https://" ), Qt::CaseInsensitive )
       || source.startsWith( QStringLiteral( "ftp://" ), Qt::CaseInsensitive )
       || source.startsWith( QStringLiteral( "s3://" ), Qt::CaseInsensitive )
       || source.startsWith( QStringLiteral( "gs://" ), Qt::CaseInsensitive )
       || source.startsWith( QStringLiteral( "az://" ), Qt::CaseInsensitive ) )
    return QgsDataSourceKind::RemoteUri;

  // 6. Fallback
  return QgsDataSourceKind::LocalFile;
}

bool QgsDataSourceResolver::requiresLocalExistenceCheck( const QString &source )
{
  return classify( source ) == QgsDataSourceKind::LocalFile;
}

QString QgsDataSourceResolver::kindToString( QgsDataSourceKind kind )
{
  switch ( kind )
  {
    case QgsDataSourceKind::LocalFile:
      return QStringLiteral( "LocalFile" );
    case QgsDataSourceKind::GdalVirtualPath:
      return QStringLiteral( "GdalVirtualPath" );
    case QgsDataSourceKind::OgrConnectionString:
      return QStringLiteral( "OgrConnectionString" );
    case QgsDataSourceKind::RemoteUri:
      return QStringLiteral( "RemoteUri" );
  }
  return QStringLiteral( "LocalFile" );
}
