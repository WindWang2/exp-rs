// rs_roi_tool_rectangle.h — Phase 10A Task 10.4: press-drag-release rectangle.
#pragma once

#include "rs_roi_tool_base.h"
#include "qgspointxy.h"

class QgsRubberBand;

class RsRoiToolRectangle : public RsRoiToolBase
{
    Q_OBJECT

  public:
    explicit RsRoiToolRectangle( QgsMapCanvas *canvas );
    ~RsRoiToolRectangle() override;

    void canvasPressEvent( QgsMapMouseEvent *e ) override;
    void canvasMoveEvent( QgsMapMouseEvent *e ) override;
    void canvasReleaseEvent( QgsMapMouseEvent *e ) override;
    void deactivate() override;

  private:
    void clearRubber();

    QgsPointXY mPressed;
    bool mHasPress = false;
    QgsRubberBand *mRubber = nullptr;
};
