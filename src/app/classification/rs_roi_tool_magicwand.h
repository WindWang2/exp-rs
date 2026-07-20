// Phase 10A Task 10.7 — Magic-wand ROI map tool.
//
// On click, opens the configured source raster, converts the click to pixel
// coordinates via the inverse geotransform, performs an L2-tolerance flood
// fill (RsFloodFill), and emits BOTH:
//   - roiDrawnPixels(geom, classId, pixelIndices) with exact flooded pixels
//   - roiDrawn(geom, classId) for base-tool compatibility (geom = bbox for display)
//
// Training MUST use pixelIndices from roiDrawnPixels — re-rasterizing the bbox
// would contaminate samples with background pixels.
#pragma once

#include "rs_roi_tool_base.h"

#include <QString>
#include <QVector>
#include <cstdint>

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

  signals:
    /// Exact flood-fill pixels (row*W+col). Prefer this over re-rasterizing geom.
    void roiDrawnPixels( const QgsGeometry &geom, int classId,
                         const QVector<quint64> &pixelIndices );

  private:
    double mTolerance = 20.0;
    QString mRasterPath;
};
