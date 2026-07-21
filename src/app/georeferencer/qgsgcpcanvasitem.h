/***************************************************************************
     qgsgcpcanvasitem.h
     --------------------------------------
    Date                 : 14-Feb-2010
    Copyright            : (C) 2010 by Jack R, Maxim Dubinin (GIS-Lab)
    Email                : sim@gis-lab.info

    SICNU port (2026-06-03, Task 11.5.2): rewritten so the canvas item
    holds only its own visual state (id, world position, isSource,
    enabled, selected, residual). Lifecycle is driven by
    QgsGeorefDataPoint, which owns the SRC + REF canvas items.
    2026-07: crosshair (十字丝) visual.
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSGCPCANVASITEM_H
#define QGSGCPCANVASITEM_H

#include "qgsmapcanvasitem.h"
#include "qgspointxy.h"

#include <QBrush>
#include <QColor>
#include <QPen>
#include <QPointF>

class QgsMapCanvas;

/**
 * \brief Per-GCP overlay item rendered on either the SRC or REF/Map canvas.
 *
 * Drawn as a \b crosshair (十字丝) with optional numeric id. SRC is blue,
 * REF/Map is green; selected / hover change color. Residual arrow on SRC only.
 */
class QgsGCPCanvasItem final : public QgsMapCanvasItem
{
  public:
    QgsGCPCanvasItem( QgsMapCanvas *mapCanvas,
                      int id,
                      const QgsPointXY &worldPos,
                      bool isSource );

    using QgsMapCanvasItem::paint;
    void paint( QPainter *p ) override;
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void updatePosition() override;

    QgsMapCanvas *canvas() const { return mMapCanvas; }

    int id() const { return mId; }
    void setId( int id );

    QgsPointXY worldPos() const { return mWorldPos; }
    void setWorldPos( const QgsPointXY &p );

    bool isSource() const { return mIsSource; }

    bool enabled() const { return mEnabled; }
    void setEnabled( bool e );

    bool isSelectedMarker() const { return mSelected; }
    void setSelected( bool s );

    bool isHovered() const { return mHovered; }
    void setHovered( bool h );

    QPointF residual() const { return mResidual; }
    void setResidual( QPointF r );

    /// Calls prepareGeometryChange()
    void checkBoundingRectChange();

  private:
    int mId = -1;
    QgsPointXY mWorldPos;
    bool mIsSource = true;
    bool mEnabled = true;
    bool mSelected = false;
    bool mHovered = false;
    QPointF mResidual;

    QBrush mBadgeBrush;     // retained for color tokens / future use
    QBrush mSelectedBrush;
    QPen mResidualPen;

    void drawResidualArrow( QPainter *p );
    double residualToScreenFactor() const;
};

#endif // QGSGCPCANVASITEM_H
