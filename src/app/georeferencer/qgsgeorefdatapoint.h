/***************************************************************************
     qgsgeorefdatapoint.h
     --------------------------------------
    Date                 : Sun Sep 16 12:02:56 AKDT 2007
    Copyright            : (C) 2007 by Gary E. Sherman
    Email                : sherman at mrcc dot com

    SICNU port (2026-06-02, Task 11.4.5): adapter that references a
    QgsGcpPoint owned by QgsGCPList (non-owning) and holds optional
    source/dest canvas marker items. Canvas marker visuals are deferred
    to Task 11.4.6 — the marker pointers may remain null in Task 11.4.5.
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSGEOREFDATAPOINT_H
#define QGSGEOREFDATAPOINT_H

#include "qgscoordinatereferencesystem.h"
#include "qgsgcppoint.h"
#include "qgspointxy.h"

#include <QObject>
#include <QPointF>

class QgsMapCanvas;
class QgsCoordinateTransformContext;
class QgsGCPCanvasItem;

/**
 * \brief Adapter between a QgsGcpPoint (owned by QgsGCPList) and the GUI.
 *
 * Holds a non-owning reference to a QgsGcpPoint in QgsGCPList plus the
 * source and destination map canvases the point is displayed on, and
 * (optionally) the canvas marker items rendering it. The canvas marker
 * support is deferred until Task 11.4.6 — until then mGCPSourceItem /
 * mGCPDestinationItem are nullptr.
 *
 * Mutating helpers forward to the referenced QgsGcpPoint so the underlying
 * list always observes the change.
 */
class QgsGeorefDataPoint : public QObject
{
    Q_OBJECT

  public:
    /**
     * Constructs a data point referencing an existing \a gcp (must outlive
     * this object). \a srcCanvas / \a dstCanvas may be null.
     */
    QgsGeorefDataPoint( QgsMapCanvas *srcCanvas,
                        QgsMapCanvas *dstCanvas,
                        QgsGcpPoint *gcp );
    ~QgsGeorefDataPoint() override;

    QgsPointXY sourcePoint() const { return mGcpPoint ? mGcpPoint->sourcePoint() : QgsPointXY(); }
    void setSourcePoint( const QgsPointXY &p );

    QgsPointXY destinationPoint() const { return mGcpPoint ? mGcpPoint->destinationPoint() : QgsPointXY(); }
    void setDestinationPoint( const QgsPointXY &p );

    void setDestinationPointCrs( const QgsCoordinateReferenceSystem &crs );

    QgsPointXY transformedDestinationPoint( const QgsCoordinateReferenceSystem &targetCrs,
                                            const QgsCoordinateTransformContext &context ) const;

    bool isEnabled() const { return mGcpPoint && mGcpPoint->isEnabled(); }
    void setEnabled( bool enabled );

    bool isHovered() const { return mHovered; }
    void setHovered( bool hovered );

    int id() const { return mId; }
    void setId( int id );

    /**
     * Rebuilds the SRC + REF canvas item visuals from the current
     * QgsGcpPoint state (id, source/destination point, enabled, residual).
     * Safe to call repeatedly; cheap when nothing has changed.
     */
    void updateMarkers();

    QgsGCPCanvasItem *sourceItem() const { return mGCPSourceItem; }
    QgsGCPCanvasItem *destinationItem() const { return mGCPDestinationItem; }

    void setSelected( bool on );

    /**
     * Tests whether \a p falls within the search radius around this point's
     * graphic on the requested \a type canvas (source or destination).
     * Returns false (and leaves \a distance unmodified) until canvas marker
     * items are wired in Task 11.4.6.
     */
    bool contains( const QgsPointXY &p, QgsGcpPoint::PointType type, double &distance );

    QgsMapCanvas *srcCanvas() const { return mSrcCanvas; }
    QgsMapCanvas *dstCanvas() const { return mDstCanvas; }

    QPointF residual() const { return mResidual; }
    void setResidual( QPointF r );

    QgsCoordinateReferenceSystem destinationPointCrs() const
    {
      return mGcpPoint ? mGcpPoint->destinationPointCrs() : QgsCoordinateReferenceSystem();
    }

    /// Returns a copy of the referenced GCP point. Caller must ensure
    /// gcpPoint() != nullptr (the data point always references one in
    /// normal usage).
    QgsGcpPoint point() const
    {
      return mGcpPoint
               ? *mGcpPoint
               : QgsGcpPoint( QgsPointXY(), QgsPointXY(), QgsCoordinateReferenceSystem(), true );
    }

    /// Returns the underlying (non-owned) GCP point pointer.
    QgsGcpPoint *gcpPoint() const { return mGcpPoint; }

  public slots:
    void moveTo( QgsPointXY p, QgsGcpPoint::PointType type );
    void updateCoords();

  private:
    QgsMapCanvas *mSrcCanvas = nullptr;
    QgsMapCanvas *mDstCanvas = nullptr;
    QgsGCPCanvasItem *mGCPSourceItem = nullptr;
    QgsGCPCanvasItem *mGCPDestinationItem = nullptr;
    bool mHovered = false;

    QgsGcpPoint *mGcpPoint = nullptr; // non-owning reference into QgsGCPList

    int mId = -1;
    QPointF mResidual;

    QgsGeorefDataPoint( const QgsGeorefDataPoint & ) = delete;
    QgsGeorefDataPoint &operator=( const QgsGeorefDataPoint & ) = delete;
};

#endif //QGSGEOREFDATAPOINT_H
