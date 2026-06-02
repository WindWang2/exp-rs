/***************************************************************************
    qgsgcplist.h - SICNU port of QGIS GCP list (canvas-decoupled)
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

#include "qgis.h"
#include "qgscoordinatereferencesystem.h"
#include "qgsgcppoint.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

class QgsPointXY;
class QgsCoordinateTransformContext;
class QgsGeorefTransform;

/**
 * GCP container — owns QgsGcpPoint instances and emits change signals.
 *
 * This is the SICNU canvas-decoupled redesign of the upstream QGIS
 * QgsGCPList (which held QgsGeorefDataPoint* objects coupled to map
 * canvas items). The redesign keeps upstream's residual recompute
 * semantics but replaces canvas-bound storage with bare QgsGcpPoint*,
 * and adds the SICNU .points v2 file format with a pointType column.
 */
class QgsGCPList : public QObject, public QList<QgsGcpPoint *>
{
    Q_OBJECT

  public:
    QgsGCPList() = default;
    ~QgsGCPList() override;

    // Non-copyable: owning raw pointers
    QgsGCPList( const QgsGCPList & ) = delete;
    QgsGCPList &operator=( const QgsGCPList & ) = delete;

    /**
     * Appends a copy of \a point to the list; emits changed().
     * The list takes ownership of the resulting allocation.
     */
    void appendPoint( const QgsGcpPoint &point );

    /**
     * Removes the point at \a row and deletes it; emits changed().
     */
    void removePointAt( int row );

    /**
     * Clears the list and deletes all stored points; emits changed().
     */
    void clearPoints();

    /**
     * Creates parallel vectors of enabled source/destination points,
     * with destinations transformed to \a targetCrs if valid.
     */
    void createGCPVectors( QVector<QgsPointXY> &sourcePoints,
                           QVector<QgsPointXY> &destinationPoints,
                           const QgsCoordinateReferenceSystem &targetCrs,
                           const QgsCoordinateTransformContext &context ) const;

    /// Returns the number of currently enabled points.
    int countEnabledPoints() const;

    /**
     * Recomputes per-point residuals using \a georefTransform.
     *
     * Disabled points are skipped (their residual is not written).
     * \a residualUnit selects whether residuals are in pixels or map units.
     */
    void updateResiduals( QgsGeorefTransform *georefTransform,
                          const QgsCoordinateReferenceSystem &targetCrs,
                          const QgsCoordinateTransformContext &context,
                          Qgis::RenderUnit residualUnit = Qgis::RenderUnit::Pixels );

    /// Convenience overload — matches the simpler test-time API; uses pixel units.
    void updateResiduals( QgsGeorefTransform *georefTransform,
                          const QgsCoordinateReferenceSystem &sourceCrs,
                          const QgsCoordinateReferenceSystem &targetCrs );

    /**
     * Writes the GCP list to \a filePath in SICNU .points v2 format.
     *
     * The file consists of:
     *   1. `# QGEOS .points v2` marker line
     *   2. Header row `mapX,mapY,pixelX,pixelY,enable,dX,dY,residual,pointType`
     *   3. One data row per GCP
     */
    bool saveGcps( const QString &filePath ) const;

    /**
     * Reads a .points file written by saveGcps() OR a legacy v1 file
     * (no marker, 8 columns). On v1, pointType is set to empty string.
     *
     * Existing list contents are cleared first. Each loaded point uses
     * \a destCrs as its destinationPointCrs().
     */
    bool loadGcps( const QString &filePath, const QgsCoordinateReferenceSystem &destCrs );

  signals:
    /// Emitted whenever the list mutates (append, remove, clear, load).
    void changed();

  private:
    /// Deletes all owned points without emitting any signal.
    void deleteAll();
};

#endif
