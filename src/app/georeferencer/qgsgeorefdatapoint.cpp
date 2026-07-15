/***************************************************************************
     qgsgeorefdatapoint.cpp
     --------------------------------------
    Date                 : Sun Sep 16 12:02:45 AKDT 2007
    Copyright            : (C) 2007 by Gary E. Sherman
    Email                : sherman at mrcc dot com

    SICNU port (2026-06-02, Task 11.4.5): adapter referencing a QgsGcpPoint
    owned by QgsGCPList; canvas marker items are deferred to Task 11.4.6
    so any code path that depended on QgsGCPCanvasItem is a no-op here.
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

#include "qgscoordinatereferencesystem.h"
#include "qgsgcpcanvasitem.h"
#include "qgsmapcanvas.h"
#include "qgsmaptool.h"
#include "qgsproject.h"

#include "moc_qgsgeorefdatapoint.cpp"

QgsGeorefDataPoint::QgsGeorefDataPoint( QgsMapCanvas *srcCanvas,
                                        QgsMapCanvas *dstCanvas,
                                        QgsGcpPoint *gcp )
  : mSrcCanvas( srcCanvas )
  , mDstCanvas( dstCanvas )
  , mGcpPoint( gcp )
{
  // Build SRC + REF canvas item visuals if both a canvas and a gcp are wired.
  if ( mGcpPoint )
  {
    if ( mSrcCanvas )
    {
      mGCPSourceItem = new QgsGCPCanvasItem( mSrcCanvas, mId, mGcpPoint->sourcePoint(), /*isSource=*/true );
      mGCPSourceItem->setEnabled( mGcpPoint->isEnabled() );
      mGCPSourceItem->setResidual( mGcpPoint->residual() );
    }
    if ( mDstCanvas )
    {
      mGCPDestinationItem = new QgsGCPCanvasItem( mDstCanvas, mId, mGcpPoint->destinationPoint(), /*isSource=*/false );
      mGCPDestinationItem->setEnabled( mGcpPoint->isEnabled() );
    }
  }
}

QgsGeorefDataPoint::~QgsGeorefDataPoint()
{
  // QGraphicsItems are owned by their scene; delete is the standard cleanup.
  delete mGCPSourceItem;
  mGCPSourceItem = nullptr;
  delete mGCPDestinationItem;
  mGCPDestinationItem = nullptr;
}

void QgsGeorefDataPoint::updateMarkers()
{
  if ( !mGcpPoint )
    return;
  if ( mGCPSourceItem )
  {
    mGCPSourceItem->setId( mId );
    // SRC is pixel space — never reproject.
    mGCPSourceItem->setWorldPos( mGcpPoint->sourcePoint() );
    mGCPSourceItem->setEnabled( mGcpPoint->isEnabled() );
    mGCPSourceItem->setResidual( mGcpPoint->residual() );
  }
  if ( mGCPDestinationItem )
  {
    mGCPDestinationItem->setId( mId );
    // REF may be in a different CRS than the GCP destination CRS.
    // Reproject into the destination canvas CRS so markers don't drift.
    QgsPointXY dest = mGcpPoint->destinationPoint();
    if ( mDstCanvas && mDstCanvas->mapSettings().destinationCrs().isValid() )
    {
      dest = mGcpPoint->transformedDestinationPoint(
               mDstCanvas->mapSettings().destinationCrs(),
               QgsProject::instance()->transformContext() );
    }
    mGCPDestinationItem->setWorldPos( dest );
    mGCPDestinationItem->setEnabled( mGcpPoint->isEnabled() );
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
  mHovered = hovered;
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
  if ( !item || !item->canvas() )
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
      if ( mSrcCanvas && mSrcCanvas->mapSettings().destinationCrs().isValid() )
        mGcpPoint->setDestinationPointCrs( mSrcCanvas->mapSettings().destinationCrs() );
      else if ( mDstCanvas && mDstCanvas->mapSettings().destinationCrs().isValid() )
        mGcpPoint->setDestinationPointCrs( mDstCanvas->mapSettings().destinationCrs() );

      if ( !mGcpPoint->destinationPointCrs().isValid() )
        mGcpPoint->setDestinationPointCrs( QgsProject::instance()->crs() );
      break;
    }
  }

  updateCoords();
}
