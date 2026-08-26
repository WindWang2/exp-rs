/***************************************************************************
                         qgsdatasourceresolver.h
                         ------------------------
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

#ifndef QGSDATASOURCERESOLVER_H
#define QGSDATASOURCERESOLVER_H

#include "qgis_core.h"
#include "qgis_sip.h"

#include <QString>

#define SIP_NO_FILE

/**
 * \ingroup core
 * \class QgsDataSourceKind
 * \brief Classification of a GIS datasource string.
 *
 * \since QGIS 3.44
 */
enum class QgsDataSourceKind
{
  LocalFile,            //!< Plain local filesystem path (existence check appropriate)
  GdalVirtualPath,      //!< GDAL VSI path: /vsimem/, /vsizip/, /vsicurl/, /vsis3/, etc.
  OgrConnectionString,  //!< OGR connection string: PG:, GPKG:, WFS:, MySQL:, MSSQL:, OCI:, ODBC:, SQLite:
  RemoteUri             //!< Remote URI: http://, https://, ftp://, s3://, gs://, az://
};

/**
 * \ingroup core
 * \class QgsDataSourceResolver
 * \brief Lightweight unified classifier for GIS datasource strings.
 *
 * Centralises the decision whether a datasource string refers to a plain
 * local file, a GDAL VSI virtual path, an OGR connection string or a
 * remote URI. This avoids incorrect QFileInfo::exists() / std::filesystem::exists()
 * rejections of valid VSI paths and connection strings (see GH #560).
 *
 * Classification is case-insensitive and follows a fixed priority order
 * (empty → VSI → Windows drive → OGR → Remote URI → fallback).
 *
 * \since QGIS 3.44
 */
class CORE_EXPORT QgsDataSourceResolver
{
  public:

    //! Classify a datasource string (path, VSI path, connection string or URI)
    static QgsDataSourceKind classify( const QString &source );

    //! Whether a plain local-filesystem existence check (QFileInfo/std::filesystem) is appropriate for this source. True ONLY for LocalFile.
    static bool requiresLocalExistenceCheck( const QString &source );

    //! Human-readable name for diagnostics
    static QString kindToString( QgsDataSourceKind kind );
};

#endif // QGSDATASOURCERESOLVER_H
