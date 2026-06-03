// rs_roi_tool_base.h — Phase 10A Task 10.4: base map tool for ROI digitizing.
//
// All 4 manual ROI tools (Point/Rectangle/Polygon/Freehand) subclass
// RsRoiToolBase. When the user completes a shape, the tool emits
// roiDrawn(geometry, classId). The main window catches the signal,
// rasterizes the geometry against the source raster (RsPixelRasterizer)
// and appends an RsRoi to the collection.
//
// Header-only — no .cpp required.
#pragma once

#include "qgsgeometry.h"
#include "qgsmaptool.h"

class QgsMapCanvas;

class RsRoiToolBase : public QgsMapTool
{
    Q_OBJECT

  public:
    explicit RsRoiToolBase( QgsMapCanvas *canvas )
      : QgsMapTool( canvas ) {}

    void setCurrentClassId( int id ) { mClassId = id; }
    int currentClassId() const { return mClassId; }

  signals:
    /// Emitted when the user completes a shape. The receiver is responsible
    /// for rasterizing the geometry to pixel indices and persisting an RsRoi.
    void roiDrawn( const QgsGeometry &geom, int classId );

  protected:
    int mClassId = 0;
};
