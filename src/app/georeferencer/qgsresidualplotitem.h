/***************************************************************************
    qgsresidualplotitem.h
     --------------------------------------
    Date                 : 10-May-2010
    Copyright            : (c) 2010 by Marco Hugentobler
    Email                : marco at sourcepole dot ch

    SICNU port (2026-06-03, Task 11.5.2): adapted to the project's
    GCP handling. Stores its own snapshot of (id, sourcePoint, residual,
    enabled) so it does not depend on QgsGeorefDataPoint and stays
    decoupled from the live GCP state.
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSRESIDUALPLOTITEM_H
#define QGSRESIDUALPLOTITEM_H

#include "qgslayoutitem.h"
#include "qgspointxy.h"
#include "qgsrectangle.h"

#include <QPointF>
#include <QVector>

/**
 * A composer item to visualise the distribution of georeference residuals. For
 * the visualisation, the length of the residual arrows are scaled to fit within
 * the frame.
 */
class QgsResidualPlotItem : public QgsLayoutItem
{
    Q_OBJECT

  public:
    /// Per-point snapshot used for rendering the residual plot.
    struct Entry
    {
        int id = -1;
        QgsPointXY sourcePoint;
        QPointF residual;
        bool enabled = true;
    };

    explicit QgsResidualPlotItem( QgsLayout *layout );
    ~QgsResidualPlotItem() override;

    QgsLayoutItem::Flags itemFlags() const override;

    //! \brief Reimplementation of QCanvasItem::paint
    void paint( QPainter *painter, const QStyleOptionGraphicsItem *itemStyle, QWidget *pWidget ) override;

    /// Sets a snapshot of the GCP list. Caller-prepared entries decouple us from
    /// QgsGeorefDataPoint and from the live GCP state.
    void setEntries( const QVector<Entry> &entries );
    const QVector<Entry> &entries() const { return mEntries; }

    void setExtent( const QgsRectangle &rect ) { mExtent = rect; }
    QgsRectangle extent() const { return mExtent; }

    void setConvertScaleToMapUnits( bool convert ) { mConvertScaleToMapUnits = convert; }
    bool convertScaleToMapUnits() const { return mConvertScaleToMapUnits; }

    void draw( QgsLayoutItemRenderContext &context ) override;

  private:
    QVector<Entry> mEntries;

    QgsRectangle mExtent;
    //! True if the scale bar units should be converted to map units. This can be done for transformation where the scaling in all directions is the same (helmert)
    bool mConvertScaleToMapUnits = false;

    //! Calculates maximal possible mm to pixel ratio such that the residual arrow is still inside the frame
    double maxMMToPixelRatioForEntry( const Entry &p, double pixelXMM, double pixelYMM );

    //! Returns distance between two points
    double dist( QPointF p1, QPointF p2 ) const;

    /**
     * Draws an arrow head on to a QPainter.
     * \param p destination painter
     * \param x x-coordinate of arrow center
     * \param y y-coordinate of arrow center
     * \param angle angle in degrees which arrow should point toward, measured
     * clockwise from pointing vertical upward
     * \param arrowHeadWidth size of arrow head
     */
    static void drawArrowHead( QPainter *p, double x, double y, double angle, double arrowHeadWidth );

    /**
     * Calculates the angle of the line from p1 to p2 (counter clockwise,
     * starting from a line from north to south)
     * \param p1 start point of line
     * \param p2 end point of line
     * \returns angle in degrees, clockwise from south
     */
    static double angle( QPointF p1, QPointF p2 );
};

#endif // QGSRESIDUALPLOTITEM_H
