// rs_roi_tool_point.h — Phase 10A Task 10.4: single-click point ROI tool.
#pragma once

#include "rs_roi_tool_base.h"

class RsRoiToolPoint : public RsRoiToolBase
{
    Q_OBJECT

  public:
    explicit RsRoiToolPoint( QgsMapCanvas *canvas )
      : RsRoiToolBase( canvas ) {}

    void canvasReleaseEvent( QgsMapMouseEvent *e ) override;
};
