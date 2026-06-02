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
 * \brief Source-canvas map tool: clicking the canvas emits a source
 * coordinate, which the main window uses to open the MapCoords dialog
 * for entering the matching destination coordinate.
 */
class QgsGeorefToolAddPoint : public QgsMapTool
{
    Q_OBJECT

  public:
    explicit QgsGeorefToolAddPoint( QgsMapCanvas *canvas );

    Flags flags() const override { return QgsMapTool::AllowZoomRect; }

    void canvasPressEvent( QgsMapMouseEvent *e ) override;

  signals:
    /// Emitted on a left-button click on the canvas. Coordinate is in
    /// source-canvas map coords (pixels for a non-referenced raster).
    void showCoordDialog( const QgsPointXY &sourceCoordinates );
};

#endif // QGSGEOREFTOOLADDPOINT_H
