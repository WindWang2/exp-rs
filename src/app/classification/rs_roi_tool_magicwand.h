// Phase 10A Task 10.7 - Magic-wand ROI map tool.
//
// On click, opens the configured source raster, converts the click to pixel
// coordinates via the inverse geotransform, performs an L2-tolerance flood
// fill (RsFloodFill), and emits:
//   - roiDrawnPixels(geom, classId, pixelIndices) with the exact flooded pixels
//   - roiDrawn(geom, classId) whose geometry is the true region polygon
//     (pixel-corner exact for center-convention rasterization), falling back
//     to the bbox only for degenerate fills (#283).
//   - regionClipped() when the fill reached the edge of the 513x513 search
//     window away from the raster boundary, i.e. the region was truncated (#288).
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

    /// The fill was clipped by the local search window - the selected region
    /// continues beyond it and the emitted ROI is smaller than the region.
    void regionClipped();

  private:
    double mTolerance = 20.0;
    QString mRasterPath;
};
