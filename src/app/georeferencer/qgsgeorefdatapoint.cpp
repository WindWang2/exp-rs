/***************************************************************************
     qgsgeorefdatapoint.cpp
     --------------------------------------
    Date                 : Sun Sep 16 12:02:45 AKDT 2007
    Copyright            : (C) 2007 by Gary E. Sherman
    Email                : sherman at mrcc dot com

    SICNU port (2026-06-02, Task 11.4.5): adapter referencing a QgsGcpPoint
    owned by QgsGCPList; canvas marker items (crosshairs) on SRC + REF/Map.
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "qgsgeorefdatapoint.h"

#include <cmath>

#include "qgis.h"
#include "qgscoordinatereferencesystem.h"
#include "qgsgcpcanvasitem.h"
#include "qgsmapcanvas.h"
#include "qgsmaptool.h"
#include "qgsproject.h"

#include "moc_qgsgeorefdatapoint.cpp"

namespace
{
  /// True once a destination has been assigned (pending SRC-only pick is false).
  bool hasDestinationSide( const QgsGcpPoint *gcp )
  {
    if ( !gcp )
      return false;
    // Dual-pick / dialog always set a destination CRS when dest is committed.
    if ( gcp->destinationPointCrs().isValid() )
      return true;
    // Loaded .points may lack CRS but still have non-zero destination.
    const QgsPointXY d = gcp->destinationPoint();
    return !( qgsDoubleNear( d.x(), 0.0 ) && qgsDoubleNear( d.y(), 0.0 ) );
  }
}

QgsGeorefDataPoint::QgsGeorefDataPoint( QgsMapCanvas *srcCanvas,
                                        QgsMapCanvas *dstCanvas,
                                        QgsGcpPoint *gcp )
  : mSrcCanvas( srcCanvas )
  , mDstCanvas( dstCanvas )
  , mGcpPoint( gcp )
{
  ensureItems();
  updateMarkers();
}

QgsGeorefDataPoint::~QgsGeorefDataPoint()
{
  // QGraphicsItems are owned by their scene; delete is the standard cleanup.
  delete mGCPSourceItem;
  mGCPSourceItem = nullptr;
  delete mGCPDestinationItem;
  mGCPDestinationItem = nullptr;
}

void QgsGeorefDataPoint::ensureItems()
{
  if ( !mGcpPoint )
    return;

  // Always keep a source crosshair on the SRC canvas.
  if ( mSrcCanvas && !mGCPSourceItem )
  {
    mGCPSourceItem = new QgsGCPCanvasItem( mSrcCanvas, mId, mGcpPoint->sourcePoint(), /*isSource=*/true );
  }

  // Always keep a destination crosshair on REF / Map once dest is set.
  // For I2I both images must show GCPs after a pair is completed.
  if ( mDstCanvas && !mGCPDestinationItem )
  {
    mGCPDestinationItem = new QgsGCPCanvasItem(
      mDstCanvas, mId, destinationDisplayPoint(), /*isSource=*/false );
  }
}

QgsPointXY QgsGeorefDataPoint::destinationDisplayPoint() const
{
  if ( !mGcpPoint )
    return QgsPointXY();
  // Prefer the stored destination as picked on the dest canvas. Only reproject
  // when both CRSes are valid and differ (I2M map CRS vs stored dest CRS).
  const QgsCoordinateReferenceSystem destCrs = mGcpPoint->destinationPointCrs();
  const QgsCoordinateReferenceSystem canvasCrs = mDstCanvas
                                                   ? mDstCanvas->mapSettings().destinationCrs()
                                                   : QgsCoordinateReferenceSystem();
  if ( canvasCrs.isValid() && destCrs.isValid() && canvasCrs != destCrs )
  {
    return mGcpPoint->transformedDestinationPoint(
      canvasCrs, QgsProject::instance()->transformContext() );
  }
  return mGcpPoint->destinationPoint();
}

void QgsGeorefDataPoint::updateMarkers()
{
  if ( !mGcpPoint )
    return;

  ensureItems();
  // mResidual is fed by setResidual() (the shell pushes the session's last-fit
  // residuals, ADR 0056); QgsGcpPoint no longer stores residuals.

  if ( mGCPSourceItem )
  {
    mGCPSourceItem->setId( mId );
    mGCPSourceItem->setWorldPos( mGcpPoint->sourcePoint() );
    mGCPSourceItem->setEnabled( mGcpPoint->isEnabled() );
    mGCPSourceItem->setResidual( mResidual );
    mGCPSourceItem->setVisible( true );
    mGCPSourceItem->updatePosition();
  }

  if ( mGCPDestinationItem )
  {
    mGCPDestinationItem->setId( mId );
    mGCPDestinationItem->setWorldPos( destinationDisplayPoint() );
    mGCPDestinationItem->setEnabled( mGcpPoint->isEnabled() );
    // Hide on REF until destination is committed (pending SRC-only pick).
    // After dual-canvas pick / load, always show on reference / map canvas.
    mGCPDestinationItem->setVisible( hasDestinationSide( mGcpPoint ) );
    mGCPDestinationItem->updatePosition();
  }

  // Force repaint so crosshairs appear immediately after pick on both sides.
  if ( mSrcCanvas )
  {
    mSrcCanvas->update();
    if ( mSrcCanvas->scene() )
      mSrcCanvas->scene()->update();
  }
  if ( mDstCanvas )
  {
    mDstCanvas->update();
    if ( mDstCanvas->scene() )
      mDstCanvas->scene()->update();
  }
}

void QgsGeorefDataPoint::setSelected( bool on )
{
  if ( mGCPSourceItem )
    mGCPSourceItem->setSelected( on );
  if ( mGCPDestinationItem )
    mGCPDestinationItem->setSelected( on );
}

void QgsGeorefDataPoint::setSourcePoint( const QgsPointXY &p )
{
  if ( mGcpPoint )
    mGcpPoint->setSourcePoint( p );
  updateCoords();
}

void QgsGeorefDataPoint::setDestinationPoint( const QgsPointXY &p )
{
  if ( mGcpPoint )
    mGcpPoint->setDestinationPoint( p );
  updateCoords();
}

void QgsGeorefDataPoint::setDestinationPointCrs( const QgsCoordinateReferenceSystem &crs )
{
  if ( mGcpPoint )
    mGcpPoint->setDestinationPointCrs( crs );
  updateCoords();
}

QgsPointXY QgsGeorefDataPoint::transformedDestinationPoint( const QgsCoordinateReferenceSystem &targetCrs,
                                                            const QgsCoordinateTransformContext &context ) const
{
  if ( !mGcpPoint )
    return QgsPointXY();
  return mGcpPoint->transformedDestinationPoint( targetCrs, context );
}

void QgsGeorefDataPoint::setEnabled( bool enabled )
{
  if ( mGcpPoint )
    mGcpPoint->setEnabled( enabled );
  if ( mGCPSourceItem )
    mGCPSourceItem->setEnabled( enabled );
  if ( mGCPDestinationItem )
    mGCPDestinationItem->setEnabled( enabled );
}

void QgsGeorefDataPoint::setId( int id )
{
  mId = id;
  if ( mGCPSourceItem )
    mGCPSourceItem->setId( id );
  if ( mGCPDestinationItem )
    mGCPDestinationItem->setId( id );
}

void QgsGeorefDataPoint::setResidual( QPointF r )
{
  mResidual = r;
  if ( mGCPSourceItem )
    mGCPSourceItem->setResidual( r );
}

void QgsGeorefDataPoint::updateCoords()
{
  updateMarkers();
}

void QgsGeorefDataPoint::setHovered( bool hovered )
{
  if ( mHovered == hovered )
    return;
  mHovered = hovered;
  if ( mGCPSourceItem )
    mGCPSourceItem->setHovered( hovered );
  if ( mGCPDestinationItem )
    mGCPDestinationItem->setHovered( hovered );
}

bool QgsGeorefDataPoint::contains( const QgsPointXY &p, QgsGcpPoint::PointType type, double &distance )
{
  QgsGCPCanvasItem *item = nullptr;
  switch ( type )
  {
    case QgsGcpPoint::PointType::Source:
      item = mGCPSourceItem;
      break;
    case QgsGcpPoint::PointType::Destination:
      item = mGCPDestinationItem;
      break;
  }
  if ( !item || !item->canvas() || !item->isVisible() )
    return false;

  const double searchRadiusMM = QgsMapTool::searchRadiusMM();
  const double pixelsPerMM = item->canvas()->logicalDpiX() / 25.4;
  const double searchRadiusPx = searchRadiusMM * pixelsPerMM;

  const QPointF pPos = item->toCanvasCoordinates( p );
  const QPointF itemPos = item->pos();
  const double dx = pPos.x() - itemPos.x();
  const double dy = pPos.y() - itemPos.y();
  const double d = std::hypot( dx, dy );
  if ( d <= searchRadiusPx )
  {
    distance = d;
    return true;
  }
  return false;
}

void QgsGeorefDataPoint::moveTo( QgsPointXY p, QgsGcpPoint::PointType type )
{
  if ( !mGcpPoint )
    return;
  switch ( type )
  {
    case QgsGcpPoint::PointType::Source:
    {
      mGcpPoint->setSourcePoint( p );
      break;
    }
    case QgsGcpPoint::PointType::Destination:
    {
      mGcpPoint->setDestinationPoint( p );
      // Destination is edited on the REF/Map canvas — take CRS from dst canvas.
      if ( mDstCanvas && mDstCanvas->mapSettings().destinationCrs().isValid() )
        mGcpPoint->setDestinationPointCrs( mDstCanvas->mapSettings().destinationCrs() );
      else if ( !mGcpPoint->destinationPointCrs().isValid() )
        mGcpPoint->setDestinationPointCrs( QgsProject::instance()->crs() );
      break;
    }
  }

  updateCoords();
}
