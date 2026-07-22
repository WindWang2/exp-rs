// rs_roi_tool_polygon.h — Phase 10A Task 10.4: click vertices, dblclk closes.
#pragma once

#include "rs_roi_tool_base.h"
#include "qgspointxy.h"

#include <QVector>

class QgsRubberBand;

class RsRoiToolPolygon : public RsRoiToolBase
{
    Q_OBJECT

  public:
    explicit RsRoiToolPolygon( QgsMapCanvas *canvas );
    ~RsRoiToolPolygon() override;

    void canvasReleaseEvent( QgsMapMouseEvent *e ) override;
    void canvasDoubleClickEvent( QgsMapMouseEvent *e ) override;
    void canvasMoveEvent( QgsMapMouseEvent *e ) override;
    void deactivate() override;

  private:
    void updateRubber( const QgsPointXY *cursor = nullptr );
    void clearRubber();

    QVector<QgsPointXY> mVertices;
    QgsRubberBand *mRubber = nullptr;
};
