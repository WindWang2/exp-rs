// src/app/workbench/classification_studio_widget.h — D15 Package G public seam.
//
// Interactive classification & change studio workbench: magic-wand ROI
// extraction over a QgsRasterLayer, a binned density feature-scatter view
// built for 1e5+ points (single blit per paint), and the studio shell that
// wires class palette, algorithm dispatch and swipe comparison.
//
// Threading: UI objects are main-thread only; extraction runs synchronously
// against the raster provider.  The BFS is bounded by maxPixels; the spectral
// planes are materialized once per call (full layer read, all bands).
#pragma once

#include <QWidget>
#include <QPolygonF>
#include <QPoint>
#include <QPointer>
#include <QImage>
#include <QString>

#include <cstdint>
#include <span>
#include <vector>

class QgsRasterLayer;
class QComboBox;
class QTableWidget;
class QSlider;

namespace rs::app
{

/// BFS spectral flood fill (normalized Euclidean distance in raw band
/// units, Scale_k == 1) seeded at a pixel; returns the outline of the
/// connected accepted region as a closed polygon along pixel *edges*
/// (integer lattice), so every accepted pixel centre is strictly inside
/// and every rejected centre strictly outside.  Empty polygon when the
/// seed is out of bounds / the layer is null or invalid.
class RsRoiMagicWandTool : public QObject
{
    Q_OBJECT
  public:
    explicit RsRoiMagicWandTool( QObject *parent = nullptr );
    ~RsRoiMagicWandTool() override = default;

    QPolygonF extractRegion( const QPoint &seedPixel,
                             const QgsRasterLayer *rasterLayer,
                             double spectralTolerance,
                             int maxPixels = 50000,
                             int connectivity = 8 );
};

/// Feature-space scatter with 2D histogram density rendering: setData bins
/// up to 1e6 samples into a <=200x200 grid once; painting blits the cached
/// image (no per-point painter primitives).
class FeatureScatterWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit FeatureScatterWidget( QWidget *parent = nullptr );
    ~FeatureScatterWidget() override = default;

    /// Bins the paired samples (sizes must match; extra samples ignored).
    /// @p labels are stored for class-aware consumers; the density render
    /// itself is label-agnostic.
    void setData( std::span<const float> xFeatures,
                  std::span<const float> yFeatures,
                  std::span<const int> labels,
                  const QString &xLabel,
                  const QString &yLabel );

    void setGridBinning( int binsX = 200, int binsY = 200 );

    /// Log-normalized density image (binsX x binsY).  Empty when no data.
    QImage renderDensityThumbnail() const;

  protected:
    void paintEvent( QPaintEvent *event ) override;

  private:
    void rebuildImage() const;

    std::vector<float> mX, mY;
    std::vector<int> mLabels; // stored for class-aware consumers
    float mXMin = 0.0f, mXMax = 1.0f, mYMin = 0.0f, mYMax = 1.0f;
    int mBinsX = 200, mBinsY = 200;
    mutable QImage mCache;
    mutable bool mDirty = true;
};

/// Studio shell: palette table + algorithm dispatch + swipe slider + the
/// magic wand.  Emits roiExtracted after a successful wand run.
class ClassificationStudioWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit ClassificationStudioWidget( QWidget *parent = nullptr );
    ~ClassificationStudioWidget() override = default;

    void bindInputLayer( QgsRasterLayer *layer );
    void setClassPalette( const std::vector<int> &classIds,
                          const std::vector<QString> &names,
                          const std::vector<uint32_t> &argbColors );

    /// Runs the magic wand on the bound layer and emits roiExtracted with
    /// the currently selected palette class (no-op without a live layer —
    /// QPointer-guarded).
    void runMagicWandAt( const QPoint &seedPixel, double spectralTolerance );

  signals:
    void roiExtracted( int classId, const QPolygonF &polygon );
    void classificationRequested( int algoType );
    void swipeOffsetChanged( float splitRatio );

  private:
    QPointer<QgsRasterLayer> mLayer;
    RsRoiMagicWandTool *mWand = nullptr;
    QTableWidget *mClassTable = nullptr;
    QComboBox *mAlgoCombo = nullptr;
    QSlider *mSwipeSlider = nullptr;
    int mCurrentClassId = 0;
};

} // namespace rs::app
