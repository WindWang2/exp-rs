// georef_dual_window.h — D14 Package G: dual-window linked georeferencing
// workbench (ADR 0159).
//
// Follows the proven rs_dual_viewport_sync_controller pattern: both canvases
// are observed through QPointer, extent mirroring is coalesced by a 16 ms
// throttle timer, and mApplyingSync is the strict reentrancy guard that
// breaks the extentsChanged feedback loop (recursion depth exactly 1).
// Residual vectors are rendered as red arrows (amplified) on the reference
// canvas next to green target crosses.
#pragma once

#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <QTimer>

#include "processing/algorithms/gcp_manager.h"
#include "processing/algorithms/geometric_transform.h"

#include <memory>

class QgsMapCanvas;
class QgsMapTool;
class QgsPointXY;
class QgsRasterLayer;
class QgsRubberBand;
class QComboBox;
class QLabel;
class QTableWidget;

namespace rs::app {

enum class ViewportSyncMode {
    None,
    ExtentSync,          // panning / zooming mirror
    CursorCrosshairSync, // cursor movement mirror
    FullLock             // extent + cursor lock
};

class GeorefDualWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit GeorefDualWindow(QWidget* parent = nullptr);
    ~GeorefDualWindow() override;

    // Project & dataset loaders (invalid rasters are ignored silently).
    void loadSourceImage(const QString& filePath);
    void loadReferenceImage(const QString& filePath);

    // Viewport synchronization.
    void setSyncMode(ViewportSyncMode mode);
    [[nodiscard]] ViewportSyncMode syncMode() const noexcept;

    // Residual arrow visual amplification (10x default, 0 hides arrows).
    void setResidualAmplification(double factor);
    [[nodiscard]] double residualAmplification() const noexcept;

    // Canvas access (link controllers, map tools, tests).
    [[nodiscard]] QgsMapCanvas* sourceCanvas() const noexcept;
    [[nodiscard]] QgsMapCanvas* referenceCanvas() const noexcept;

    // Test inspection seams (offscreen friendly).
    [[nodiscard]] int gcpTableRowCount() const;
    [[nodiscard]] double displayedGlobalRmse() const;
    [[nodiscard]] bool isApplyingSync() const noexcept; // reentrancy probe

  public slots:
    void onAddGcpPoint(const QgsPointXY& srcPt, const QgsPointXY& refPt);
    void onDeleteGcpPoint(int rowIndex);
    void onTransformModelChanged(int modelIndex);
    void onExecuteWarpClicked();
    void onToggleSwipeComparison(bool enabled);

  signals:
    void rectificationFinished(const QString& outputPath, double finalRmse);
    void crosshairPositionChanged(const QgsPointXY& srcPt, const QgsPointXY& refPt);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void buildUi();
    void refreshGcpTable();
    void refitTransform();
    void scheduleExtentSync(bool fromSource);
    void applyPendingExtentSync();
    void updateResidualBand();
    void updateCrosshair(QgsMapCanvas* moved, const QgsPointXY& point);

    QPointer<QgsMapCanvas> mSourceCanvas;
    QPointer<QgsMapCanvas> mReferenceCanvas;
    QTableWidget* mGcpTable = nullptr;
    QLabel* mRmseLabel = nullptr;
    QComboBox* mModelCombo = nullptr;
    QgsRubberBand* mResidualBand = nullptr;

    QPointer<QgsRasterLayer> mSourceLayer;
    QPointer<QgsRasterLayer> mReferenceLayer;
    QPointer<QgsMapTool> mDefaultTool;
    QPointer<QgsMapTool> mSwipeTool;

    rs::core::GcpManager mGcpManager;
    rs::algorithms::TransformModel mModel = rs::algorithms::TransformModel::Affine;
    rs::algorithms::TransformResult mFitted;
    double mDisplayedRmse = 0.0;
    double mResidualAmplification = 10.0;

    ViewportSyncMode mSyncMode = ViewportSyncMode::None;
    bool mApplyingSync = false; // strict reentrancy guard
    QTimer mSyncThrottleTimer;  // 16 ms coalescing (60 fps cap)
    bool mPendingFromSource = true;
};

} // namespace rs::app
