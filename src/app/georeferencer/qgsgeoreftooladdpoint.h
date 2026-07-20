/***************************************************************************
     qgsgeoreftooladdpoint.h
     --------------------------------------
    Date                 : 14-Feb-2010
    Copyright            : (C) 2010 by Jack R, Maxim Dubinin (GIS-Lab)
    Email                : sim@gis-lab.info

    SICNU port (2026-06-02, Task 11.4.5): rewritten on top of QgsMapTool
    (the upstream version inherits QgsMapToolCapture which drags in the
    advanced digitizing stack; the GCP add flow only needs a click-emit
    point tool).
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSGEOREFTOOLADDPOINT_H
#define QGSGEOREFTOOLADDPOINT_H

#include "qgsmaptool.h"

class QgsPointXY;
class QgsMapCanvas;
class QgsMapMouseEvent;

/**
 * \brief Click-to-pick map tool for GCP placement on a single canvas.
 *
 * Left click emits \ref pointPicked with canvas map coordinates.
 * Right click emits \ref canceled (e.g. abandon a pending source point).
 */
class QgsGeorefToolAddPoint : public QgsMapTool
{
    Q_OBJECT

  public:
    explicit QgsGeorefToolAddPoint( QgsMapCanvas *canvas );

    Flags flags() const override { return QgsMapTool::AllowZoomRect; }

    void canvasPressEvent( QgsMapMouseEvent *e ) override;

  signals:
    /// Left-button click; coordinate is in this tool's canvas map CRS.
    void pointPicked( const QgsPointXY &mapCoordinates );
    /// Right-button click — cancel pending dual-canvas pick.
    void canceled();
};

#endif // QGSGEOREFTOOLADDPOINT_H
