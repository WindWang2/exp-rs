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

#include "qgscoordinatereferencesystem.h"
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
  // QgsGCPCanvasItem will be added in Task 11.4.6.
}

QgsGeorefDataPoint::~QgsGeorefDataPoint()
{
  // Canvas marker items not yet owned (Task 11.4.6).
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
}

void QgsGeorefDataPoint::setResidual( QPointF r )
{
  mResidual = r;
}

void QgsGeorefDataPoint::updateCoords()
{
  // Canvas marker visuals are added in Task 11.4.6; nothing to refresh yet.
}

void QgsGeorefDataPoint::setHovered( bool hovered )
{
  mHovered = hovered;
}

bool QgsGeorefDataPoint::contains( const QgsPointXY &p, QgsGcpPoint::PointType type, double &distance )
{
  Q_UNUSED( p )
  Q_UNUSED( type )
  Q_UNUSED( distance )
  // Canvas marker hit-test is added in Task 11.4.6.
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
