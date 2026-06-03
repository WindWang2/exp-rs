// Phase 10A Task 10.7 — Magic-wand ROI map tool.
//
// On click, opens the configured source raster, converts the click to pixel
// coordinates via the inverse geotransform, performs an L2-tolerance flood
// fill (RsFloodFill), and emits the bounding-box geometry of the selected
// pixels as the ROI. The bbox is a v1 approximation (overestimates the
// true outline); a true contour-based geometry is deferred to a later task.
#pragma once

#include "rs_roi_tool_base.h"

#include <QString>

class QgsMapMouseEvent;

class RsRoiToolMagicWand : public RsRoiToolBase
{
    Q_OBJECT

  public:
    using RsRoiToolBase::RsRoiToolBase;

    void setTolerance( double t ) { mTolerance = t; }
    double tolerance() const { return mTolerance; }

    void setSourceData( const QString &rasterPath ) { mRasterPath = rasterPath; }
    QString sourceData() const { return mRasterPath; }

    void canvasReleaseEvent( QgsMapMouseEvent *e ) override;

  private:
    double mTolerance = 20.0;
    QString mRasterPath;
};
