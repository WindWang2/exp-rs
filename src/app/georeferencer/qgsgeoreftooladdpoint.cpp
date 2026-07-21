/***************************************************************************
     qgsgeoreftooladdpoint.cpp
     --------------------------------------
    Date                 : 14-Feb-2010
    Copyright            : (C) 2010 by Jack R, Maxim Dubinin (GIS-Lab)
    Email                : sim@gis-lab.info

    SICNU port (2026-06-02, Task 11.4.5).
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsgeoreftooladdpoint.h"

#include "qgsmapcanvas.h"
#include "qgsmapmouseevent.h"

#include "moc_qgsgeoreftooladdpoint.cpp"

QgsGeorefToolAddPoint::QgsGeorefToolAddPoint( QgsMapCanvas *canvas )
  : QgsMapTool( canvas )
{
  setCursor( Qt::CrossCursor );
}

void QgsGeorefToolAddPoint::canvasPressEvent( QgsMapMouseEvent *e )
{
  if ( !e )
    return;
  if ( Qt::LeftButton == e->button() )
  {
    // Prefer the precomputed map point from the event (handles HiDPI / canvas
    // transform consistently). Fall back to tool helper if needed.
    QgsPointXY mapPt = e->mapPoint();
    if ( mapPt.isEmpty() )
      mapPt = toMapCoordinates( e->pos() );
    emit pointPicked( mapPt );
  }
  else if ( Qt::RightButton == e->button() )
  {
    emit canceled();
  }
}
