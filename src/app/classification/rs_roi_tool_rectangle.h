// rs_roi_tool_rectangle.h — Phase 10A Task 10.4: press-drag-release rectangle.
#pragma once

#include "rs_roi_tool_base.h"
#include "qgspointxy.h"

class RsRoiToolRectangle : public RsRoiToolBase
{
    Q_OBJECT

  public:
    explicit RsRoiToolRectangle( QgsMapCanvas *canvas )
      : RsRoiToolBase( canvas ) {}

    void canvasPressEvent( QgsMapMouseEvent *e ) override;
    void canvasReleaseEvent( QgsMapMouseEvent *e ) override;

  private:
    QgsPointXY mPressed;
    bool mHasPress = false;
};
