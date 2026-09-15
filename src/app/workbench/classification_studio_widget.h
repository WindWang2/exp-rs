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

#include "app/workbench/mission_context.h"

class QgsRasterLayer;
class QComboBox;
class QTableWidget;
class QSlider;
class QLabel;

namespace rs::app
{

// ---------------------------------------------------------------------------
// Classification & Object Intelligence 11.0 — studio diagnostics panels.
//
// All four panels are pure-data-driven painters: they receive POD vectors
// (no analysis-layer types, no OpenCV), so hosts and tests feed them from
// any source with hand-verifiable expectations. Panel data setters clamp
// and ignore malformed input (mismatched lengths, non-finite values) —
// rendering never throws.
// ---------------------------------------------------------------------------

/// Stacked per-class probability bar for one sample (class order = the
/// caller's classIds order, the RsClassOrder contract).
class RsProbabilityPanel : public QWidget
{
    Q_OBJECT
  public:
    explicit RsProbabilityPanel( QWidget *parent = nullptr );
    ~RsProbabilityPanel() override = default;

    void setProbabilityRow( const QVector<int> &classIds,
                            const QVector<double> &probs,
                            int predictedClass = -1 );
    void clear();

  protected:
    void paintEvent( QPaintEvent *event ) override;

  private:
    QVector<int> mClassIds;
    QVector<double> mProbs;
    int mPredictedClass = -1;
};

/// Reliability diagram: per-bin mean confidence vs empirical accuracy.
/// Two series (e.g. uncalibrated + calibrated) share one [0,1]² plot; the
/// diagonal is the perfect-calibration reference.
class RsReliabilityWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit RsReliabilityWidget( QWidget *parent = nullptr );
    ~RsReliabilityWidget() override = default;

    void setSeries( const QVector<double> &binConfidence,
                    const QVector<double> &binAccuracy,
                    const QVector<int> &binCounts,
                    bool calibrated );
    void clear();

  protected:
    void paintEvent( QPaintEvent *event ) override;

  private:
    struct Series
    {
        QVector<double> confidence;
        QVector<double> accuracy;
        QVector<int> counts;
    };
    Series mRaw;
    Series mCalibrated;
};

/// Top-k confusion pairs ("true → predicted") with count-proportional bars.
class RsConfusionPairsWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit RsConfusionPairsWidget( QWidget *parent = nullptr );
    ~RsConfusionPairsWidget() override = default;

    void setPairs( const QVector<int> &trueIds,
                   const QVector<int> &predictedIds,
                   const QVector<int> &counts );
    void clear();

  protected:
    void paintEvent( QPaintEvent *event ) override;

  private:
    QVector<int> mTrueIds;
    QVector<int> mPredictedIds;
    QVector<int> mCounts;
};

/// Horizontal per-feature importance bars (RF-style mean decrease).
class RsFeatureImportanceWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit RsFeatureImportanceWidget( QWidget *parent = nullptr );
    ~RsFeatureImportanceWidget() override = default;

    void setImportances( const QVector<QString> &featureNames,
                         const QVector<double> &importances );
    void clear();

  protected:
    void paintEvent( QPaintEvent *event ) override;

  private:
    QVector<QString> mNames;
    QVector<double> mImportances;
};

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
/// magic wand + F12 intelligence panels (probability / reliability /
/// confusion pairs / feature importance).  Emits roiExtracted after a
/// successful wand run.
class ClassificationStudioWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit ClassificationStudioWidget( QWidget *parent = nullptr );
    ~ClassificationStudioWidget() override = default;

    /// F12 diagnostics panel accessors (owned by this widget; never null
    /// after construction).
    RsProbabilityPanel *probabilityPanel() const { return mProbabilityPanel; }
    RsReliabilityWidget *reliabilityWidget() const { return mReliabilityWidget; }
    RsConfusionPairsWidget *confusionPairsWidget() const { return mConfusionPairsWidget; }
    RsFeatureImportanceWidget *featureImportanceWidget() const { return mFeatureImportanceWidget; }

    /// F12: mean best-class confidence summary line (clamped to [0,1];
    /// non-finite input is ignored).
    void setConfidenceSummary( double meanConfidence );

    void bindInputLayer( QgsRasterLayer *layer );

    /// D18: record the mission-level input as a typed ref (no live ownership).
    /// The live QgsRasterLayer binding remains for interactive tools.
    void setMissionInputRef( const sicnu::app::WorkbenchObjectRef &ref );
    sicnu::app::WorkbenchObjectRef missionInputRef() const { return mMissionInput; }

    /// Last published classification/change result id (Result/Asset), if any.
    void setMissionResultRef( const sicnu::app::WorkbenchObjectRef &ref );
    sicnu::app::WorkbenchObjectRef missionResultRef() const { return mMissionResult; }

    /// When a concrete product path exists (classic classify export, pipeline
    /// output, or user-provided path), accept it so MissionContext can publish
    /// a path-backed Result instead of a provisional request id.
    void acceptProductPath( const QString &path, int algoType = -1 );

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
    /// Emitted when a concrete product path is available (path non-empty).
    void classificationProductReady( const QString &path, int algoType );
    void swipeOffsetChanged( float splitRatio );

  private:
    QPointer<QgsRasterLayer> mLayer;
    sicnu::app::WorkbenchObjectRef mMissionInput;
    sicnu::app::WorkbenchObjectRef mMissionResult;
    RsRoiMagicWandTool *mWand = nullptr;
    QTableWidget *mClassTable = nullptr;
    QComboBox *mAlgoCombo = nullptr;
    QSlider *mSwipeSlider = nullptr;
    RsProbabilityPanel *mProbabilityPanel = nullptr;
    RsReliabilityWidget *mReliabilityWidget = nullptr;
    RsConfusionPairsWidget *mConfusionPairsWidget = nullptr;
    RsFeatureImportanceWidget *mFeatureImportanceWidget = nullptr;
    QLabel *mConfidenceLabel = nullptr;
    int mCurrentClassId = 0;
};

} // namespace rs::app
