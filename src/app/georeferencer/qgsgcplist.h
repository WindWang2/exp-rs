/***************************************************************************
    qgsgcplist.h - SICNU .points v2 codec
     --------------------------------------
    Date                 : 2026-06-02 (SICNU port)
    Originally           : 27-Feb-2009, (c) 2009 Manuel Massing
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef QGS_GCP_LIST_H
#define QGS_GCP_LIST_H

#include "qgscoordinatereferencesystem.h"
#include "qgsgcppoint.h"

#include <QString>
#include <QVector>

/**
 * SICNU .points v2 file codec over QgsGcpPoint VALUES (ADR 0020).
 *
 * This used to be the QgsGCPList container class (owning QgsGcpPoint* with
 * change signals and residual recomputation). ADR 0020 made the
 * Georeferencing Session the sole owner of GCP/fit state, and S3 deleted
 * the container: its only remaining production use was this stateless file
 * codec, now exposed as free functions so owners such as
 * RsGeoreferencingSession (via the shell) can read/write .points files
 * without standing up a container as state.
 *
 * The v2 file consists of:
 *   1. `# QGEOS .points v2` marker line
 *   2. Header row `mapX,mapY,pixelX,pixelY,enable,dX,dY,residual,pointType`
 *   3. One data row per GCP
 */

/// Writes \a points to \a filePath in SICNU .points v2 format.
bool rsSaveGcpPointsFile( const QString &filePath, const QVector<QgsGcpPoint> &points );

/**
 * Reads a .points file written by rsSaveGcpPointsFile() OR a legacy v1 file
 * (no marker, 8 columns). On v1, pointType is set to empty string.
 * Each loaded point uses \a destCrs as its destinationPointCrs().
 */
bool rsLoadGcpPointsFile( const QString &filePath, const QgsCoordinateReferenceSystem &destCrs,
                          QVector<QgsGcpPoint> &pointsOut );

#endif
