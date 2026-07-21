/***************************************************************************
     qgsgcpcanvasitem.cpp
     --------------------------------------
    Date                 : 14-Feb-2010
    Copyright            : (C) 2010 by Jack R, Maxim Dubinin (GIS-Lab)
    Email                : sim@gis-lab.info

    SICNU port (2026-06-03, Task 11.5.2) — refactored to render purely
    from owned visual state; no QgsGeorefDataPoint dependency.
    2026-07: crosshair (十字丝) marker style for SRC / REF canvases.
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsgcpcanvasitem.h"

#include "qgsmapcanvas.h"
#include "qgsrasterlayer.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QString>

namespace
{
  // Design tokens — crosshair colors
  const QColor kSrcColor( 0x1f, 0x6f, 0xeb );     // blue  SRC
  const QColor kRefColor( 0x2d, 0xa4, 0x4e );     // green REF / Map
  const QColor kSelectedColor( 0xbf, 0x87, 0x00 ); // amber selected
  const QColor kHoverColor( 0x39, 0xd0, 0xd8 );   // cyan hover

  constexpr double kArm = 12.0;       // half-length of each crosshair arm (px)
  constexpr double kGap = 2.5;        // gap around center so point is readable
  constexpr double kOutline = 3.0;    // white halo width
  constexpr double kStroke = 1.6;     // main stroke width
  constexpr int kLabelPt = 9;
}

QgsGCPCanvasItem::QgsGCPCanvasItem( QgsMapCanvas *mapCanvas,
                                    int id,
                                    const QgsPointXY &worldPos,
                                    bool isSource )
  : QgsMapCanvasItem( mapCanvas )
  , mId( id )
  , mWorldPos( worldPos )
  , mIsSource( isSource )
  , mBadgeBrush( isSource ? kSrcColor : kRefColor )
  , mSelectedBrush( kSelectedColor )
{
  mResidualPen.setColor( QColor( 255, 0, 0 ) );
  mResidualPen.setWidthF( 2.0 );
  // Sit above map layers / other overlays so GCP crosshairs stay visible.
  setZValue( 2000 );
  setVisible( true );
  setFlag( QGraphicsItem::ItemIgnoresTransformations, false );

  updatePosition();
}

void QgsGCPCanvasItem::paint( QPainter *p )
{
  if ( !p )
    return;

  p->setRenderHint( QPainter::Antialiasing, true );
  p->setOpacity( mEnabled ? 1.0 : 0.4 );

  QColor color = mIsSource ? kSrcColor : kRefColor;
  if ( mSelected )
    color = kSelectedColor;
  if ( mHovered )
    color = kHoverColor;

  const double arm = mSelected || mHovered ? kArm + 2.0 : kArm;
  const double stroke = mSelected || mHovered ? kStroke + 0.6 : kStroke;

  // White halo first (readability on any imagery), then colored crosshair.
  auto drawCross = [&]( const QColor &c, double width ) {
    QPen pen( c );
    pen.setWidthF( width );
    pen.setCapStyle( Qt::SquareCap );
    pen.setJoinStyle( Qt::MiterJoin );
    p->setPen( pen );
    p->setBrush( Qt::NoBrush );
    // Horizontal arms (gap at center)
    p->drawLine( QPointF( -arm, 0 ), QPointF( -kGap, 0 ) );
    p->drawLine( QPointF( kGap, 0 ), QPointF( arm, 0 ) );
    // Vertical arms
    p->drawLine( QPointF( 0, -arm ), QPointF( 0, -kGap ) );
    p->drawLine( QPointF( 0, kGap ), QPointF( 0, arm ) );
    // Small center tick (true crosshair through origin)
    p->drawLine( QPointF( -kGap * 0.4, 0 ), QPointF( kGap * 0.4, 0 ) );
    p->drawLine( QPointF( 0, -kGap * 0.4 ), QPointF( 0, kGap * 0.4 ) );
  };

  drawCross( QColor( 255, 255, 255, 220 ), kOutline );
  drawCross( color, stroke );

  // ID label offset to upper-right of the crosshair
  if ( mId >= 0 )
  {
    QFont f( QStringLiteral( "IBM Plex Mono" ) );
    f.setPointSize( kLabelPt );
    f.setBold( true );
    p->setFont( f );
    const QString s = QString::number( mId );
    const QFontMetricsF fm( f );
    const QRectF textBox = fm.boundingRect( s );
    const QPointF origin( arm + 3.0, -arm + 2.0 );
    // Halo text
    p->setPen( QPen( QColor( 0, 0, 0, 180 ) ) );
    for ( int dx = -1; dx <= 1; ++dx )
      for ( int dy = -1; dy <= 1; ++dy )
        if ( dx || dy )
          p->drawText( origin + QPointF( dx, dy ), s );
    p->setPen( QPen( color ) );
    p->drawText( origin, s );
    Q_UNUSED( textBox )
  }

  if ( mIsSource )
    drawResidualArrow( p );
}

QRectF QgsGCPCanvasItem::boundingRect() const
{
  // Crosshair arms + label + residual arrow extent.
  double half = kArm + 4.0;
  if ( mId >= 0 )
    half = std::max( half, kArm + 20.0 );

  double residualLeft = -half;
  double residualRight = half;
  double residualTop = -half;
  double residualBottom = half;

  if ( mIsSource )
  {
    const double rf = residualToScreenFactor();
    const double rx = mResidual.x() * rf;
    const double ry = mResidual.y() * rf;
    residualLeft = std::min( residualLeft, rx - mResidualPen.widthF() );
    residualRight = std::max( residualRight, rx + mResidualPen.widthF() );
    residualTop = std::min( residualTop, ry - mResidualPen.widthF() );
    residualBottom = std::max( residualBottom, ry + mResidualPen.widthF() );
  }

  return QRectF( QPointF( residualLeft, residualTop ),
                 QPointF( residualRight, residualBottom ) );
}

QPainterPath QgsGCPCanvasItem::shape() const
{
  // Hit area: small circle covering the cross center (for move/delete tools).
  QPainterPath p;
  p.addEllipse( QPointF( 0, 0 ), kArm * 0.55, kArm * 0.55 );
  return p;
}

void QgsGCPCanvasItem::updatePosition()
{
  if ( !mMapCanvas )
    return;
  // Source canvas: pixel/map coords of SRC; dest canvas: REF/Map coords.
  // Callers pass positions already in the canvas destination CRS.
  setPos( toCanvasCoordinates( mWorldPos ) );
}

void QgsGCPCanvasItem::setId( int id )
{
  if ( mId == id )
    return;
  mId = id;
  prepareGeometryChange();
  update();
}

void QgsGCPCanvasItem::setWorldPos( const QgsPointXY &p )
{
  mWorldPos = p;
  updatePosition();
  prepareGeometryChange();
  update();
}

void QgsGCPCanvasItem::setEnabled( bool e )
{
  if ( mEnabled == e )
    return;
  mEnabled = e;
  update();
}

void QgsGCPCanvasItem::setSelected( bool s )
{
  if ( mSelected == s )
    return;
  mSelected = s;
  prepareGeometryChange();
  update();
}

void QgsGCPCanvasItem::setHovered( bool h )
{
  if ( mHovered == h )
    return;
  mHovered = h;
  prepareGeometryChange();
  update();
}

void QgsGCPCanvasItem::setResidual( QPointF r )
{
  mResidual = r;
  prepareGeometryChange();
  update();
}

void QgsGCPCanvasItem::checkBoundingRectChange()
{
  prepareGeometryChange();
}

void QgsGCPCanvasItem::drawResidualArrow( QPainter *p )
{
  if ( !mMapCanvas )
    return;
  if ( mResidual.isNull() )
    return;

  const double rf = residualToScreenFactor();
  p->setPen( mResidualPen );
  p->drawLine( QPointF( 0, 0 ), QPointF( mResidual.x() * rf, mResidual.y() * rf ) );
}

double QgsGCPCanvasItem::residualToScreenFactor() const
{
  if ( !mMapCanvas )
    return 1.0;

  const double mapUnitsPerScreenPixel = mMapCanvas->mapUnitsPerPixel();
  if ( mapUnitsPerScreenPixel <= 0 )
    return 1.0;
  double mapUnitsPerRasterPixel = 1.0;

  const QList<QgsMapLayer *> canvasLayers = mMapCanvas->mapSettings().layers();
  if ( !canvasLayers.isEmpty() )
  {
    if ( QgsRasterLayer *rl = qobject_cast<QgsRasterLayer *>( canvasLayers.first() ) )
      mapUnitsPerRasterPixel = rl->rasterUnitsPerPixelX();
  }

  return mapUnitsPerRasterPixel / mapUnitsPerScreenPixel;
}
