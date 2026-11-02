// Phase 10A Task 10.7 - Magic-wand ROI map tool.
//
// On click, snapshots the click (map coords) + raster path + tolerance on
// the GUI thread, then runs the whole read/fill/geometry pass OFF the GUI
// thread (#1791: the windowed multi-band RasterIO + flood fill used to run
// inside the map-tool release event and froze the canvas on large local or
// any remote source). The worker crosses the thread boundary with plain
// values only (pixels, polygon rings); the landing rebuilds the
// QgsGeometry on the GUI thread and emits:
//   - roiDrawnPixels(geom, classId, pixelIndices) with the exact flooded pixels
//   - roiDrawn(geom, classId) whose geometry is the true region polygon
//     (pixel-corner exact for center-convention rasterization), falling back
//     to the bbox only for degenerate fills (#283).
//   - regionClipped() when the fill reached the edge of the 513x513 search
//     window away from the raster boundary, i.e. the region was truncated (#288).
// A newer click (or tool destruction) supersedes an in-flight fill; the
// stale landing is dropped, never double-applied.
#pragma once

#include "rs_roi_tool_base.h"

#include "qgspointxy.h"

#include <QSet>
#include <QString>
#include <QVector>
#include <cstdint>

class QgsMapMouseEvent;

class RsRoiToolMagicWand : public RsRoiToolBase
{
    Q_OBJECT

  public:
    using RsRoiToolBase::RsRoiToolBase;

    ~RsRoiToolMagicWand() override;

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
    /// One worker result: plain values only (no QGIS object touched off-thread).
    struct WandResult
    {
        bool ok = false;
        int classId = 0;
        /// Flooded pixels as a set (bbox fallback scans it)…
        QSet<quint64> pixels;               // global row*W+col indices
        /// …and as the list roiDrawnPixels emits (stable order after landing).
        QVector<quint64> pixelsList;
        QVector<QVector<QgsPointXY>> rings;  // region polygon rings (outer first)
        bool clipped = false;
        bool useBboxFallback = false;
        double bboxMinX = 0, bboxMinY = 0, bboxMaxX = 0, bboxMaxY = 0;
    };

    /// GUI-thread landing: drops superseded results, rebuilds the geometry,
    /// emits the tool's signals.
    void landWandResult( quint64 requestId, const WandResult &result );

    double mTolerance = 20.0;
    QString mRasterPath;
    /// Async fill state (#1791): every click bump supersedes in-flight work.
    quint64 m_requestEpoch = 0;
    /// RsScanPool generation for the in-flight fill (0 = none); cancelled in
    /// the destructor so a dying tool stops the read.
    quint64 m_scanGeneration = 0;
};
