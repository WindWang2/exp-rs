// rs_roi_tool_freehand.h — Phase 10A Task 10.4: press-drag-release freehand.
#pragma once

#include "rs_roi_tool_base.h"
#include "qgspointxy.h"

#include <QVector>

class RsRoiToolFreehand : public RsRoiToolBase
{
    Q_OBJECT

  public:
    explicit RsRoiToolFreehand( QgsMapCanvas *canvas )
      : RsRoiToolBase( canvas ) {}

    void canvasPressEvent( QgsMapMouseEvent *e ) override;
    void canvasMoveEvent( QgsMapMouseEvent *e ) override;
    void canvasReleaseEvent( QgsMapMouseEvent *e ) override;

  private:
    QVector<QgsPointXY> mPath;
    bool mDragging = false;
};
