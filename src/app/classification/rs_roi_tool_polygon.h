// rs_roi_tool_polygon.h — Phase 10A Task 10.4: click vertices, dblclk closes.
#pragma once

#include "rs_roi_tool_base.h"
#include "qgspointxy.h"

#include <QVector>

class RsRoiToolPolygon : public RsRoiToolBase
{
    Q_OBJECT

  public:
    explicit RsRoiToolPolygon( QgsMapCanvas *canvas )
      : RsRoiToolBase( canvas ) {}

    void canvasReleaseEvent( QgsMapMouseEvent *e ) override;
    void canvasDoubleClickEvent( QgsMapMouseEvent *e ) override;

  private:
    QVector<QgsPointXY> mVertices;
};
