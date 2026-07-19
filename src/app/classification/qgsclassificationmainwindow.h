#pragma once

#include <QMainWindow>
#include <QMap>
#include <QString>
#include <QVector>

#include <memory>

#include <opencv2/core.hpp>

#include "rs_classifier_backend.h"
#include "rs_classify_session_state.h"
#include "rs_classify_workflow_controller.h"
#include "rs_feature_scaler.h"

class QCheckBox;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

class QgisInterface;
class QAction;
class QDockWidget;
class QgsGeometry;
class QgsMapCanvas;
class QgsMapLayerStore;
class QgsRasterLayer;
class RsRoiCollection;
class RsClassTableWidget;
class RsClassQuickList;
class RsSpectralCurveWidget;
class RsJmMatrixWidget;
class RsRoiToolPoint;
class RsRoiToolRectangle;
class RsRoiToolPolygon;
class RsRoiToolFreehand;
class RsRoiToolMagicWand;
class RsAccuracyPanel;
class RsClassifierSetupBar;
class RsClassifyStepperBar;
class RsClassifyStepHost;

/**
 * \brief Phase 10A — Pixel-Based Classification main window shell.
 *
 * Task 10.2: Provides a QMainWindow with a central QgsMapCanvas, an ROI
 * toolbar (point/rectangle/polygon/freehand/magic wand + spectra /
 * separability / export / preview / apply), 4 dock placeholders for the
 * class table, quick list, JM matrix and spectral curve panels, plus a
 * minimal status bar.
 *
 * Subsequent tasks populate the docks (Task 10.3), wire ROI map tools
 * (Task 10.4), JM dock (Task 10.6), spectral dock (Task 10.5) and the
 * Apply action (Task 10.8).
 */
class QgsClassificationMainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit QgsClassificationMainWindow( QgisInterface *iface, QWidget *parent = nullptr );
    ~QgsClassificationMainWindow() override;

  public slots:
    /// Open the file picker and load a raster as the classification source.
    bool openSourceRaster();
    /// Load a raster from a known path. Sets m_sourceRasterPath / W / H / Gt,
    /// installs the layer in m_canvas, propagates the path to the magic-wand
    /// tool and seeds the ClassifierBar band picker with the band count.
    bool openSourceRaster( const QString &path );

    /// Apply the configured classifier to the open source raster, writing
    /// the result to the path requested in the ClassifierBar (or a file
    /// picker if blank). Schedules an RsClassificationTask on the global
    /// task manager.
    void applyClassification();

    /// Preview slot: trains like applyClassification but classifies only the
    /// current canvas viewport (RsPixelWindow crop), writes a temp GeoTIFF,
    /// skips the accuracy dialog, and adds the result layer to m_canvas.
    void applyPreview();

    /// Phase 10A review patch — 5-fold cross-validation stub. v1 shows an
    /// info dialog ("coming soon"); proper implementation lands in Phase
    /// 10A.1 to avoid a 50+ line accuracy/k-fold body before Task 10.9.
    void runCrossValidation();

    /// Phase 10A review patch — refresh the bottom spectral-curve dock from
    /// the current ROIs + selected bands. Triggered on m_rois::changed and
    /// any external "please refresh" call.
    void recomputeSpectralCurves();

    /// Phase 10A review patch — recompute the JM separability matrix. Slot
    /// is called by m_jmRecomputeTimer (500ms single-shot, restarted on
    /// each m_rois::changed) to throttle expensive pairwise JM evaluation.
    void recomputeJmMatrix();

    /// Phase 10A review patch — toolbar "Export ROIs" action handler.
    /// Pops a save dialog and calls RsRoiIO::save.
    void exportRois();

    /// Phase 10A.1.3 — File → "Load ROIs..." action handler.
    void loadRois();

    /// Phase 10A.1.3 — File → "Load classifier model..." action handler.
    /// Opens RsClassifierLoadDialog; on accept, instantiates the chosen
    /// backend, calls ->load(path), and stashes it in m_loadedBackend so the
    /// next applyClassification() call skips training and predicts directly.
    void loadClassifierModel();

  protected:
    void closeEvent( QCloseEvent *e ) override;

  private:
    void setupMenus();
    void setupToolbars();
    void setupDocks();
    void setupStatusBar();
    void setupRoiTools();
    void setupClassifierBar();
    void setupWorkflowUi();
    /// Fill step-host bodies for steps 1–4 with actions linked to existing slots.
    void populateStepPanels();
    /// Sync stepper completion, gate labels, Apply enable, wizard dock layout.
    void refreshWorkflowUi();
    /// Push ROI / class stats into the workflow controller.
    void syncWorkflowFromRois();
    /// Ensure the default 6 land-cover classes exist when the scheme is empty.
    void ensureDefaultClasses();
    /// Train vs validation sample-digitizing role (UI highlight; ROIs stay shared).
    void setActiveSampleRole( bool trainRole );
    /// Build a CV_32F NxB sample matrix and CV_32S Nx1 label matrix from the
    /// current ROIs + open source raster. Returns true if there are >= 10
    /// total samples, populates `X`, `y` and `bandsOut` accordingly.
    bool buildTrainingData( const QVector<int> &bands,
                            cv::Mat &X,
                            cv::Mat &y ) const;
    /// Start RsPostProcessTask from Step 6 panel settings.
    void runPostProcess();
    /// Collect recode old→new pairs from the Step 6 table (may be empty).
    QMap<int, int> collectRecodeMap() const;

    RsClassifySessionState::WorkflowSnapshot captureWorkflowSnapshot() const;
    void applyWorkflowSnapshot( const RsClassifySessionState::WorkflowSnapshot &s );
    /// Save ROIs to \a path (or last path / file dialog when empty). Returns
    /// true on success. Used by exportRois and dirty close Save.
    bool saveRoisToPath( QString path );

    QgisInterface *m_iface = nullptr;
    QgsMapCanvas *m_canvas = nullptr;
    RsRoiCollection *m_rois = nullptr;

    RsClassifyWorkflowController *m_workflow = nullptr;
    RsClassifyStepperBar *m_stepper = nullptr;
    RsClassifyStepHost *m_stepHost = nullptr;
    QDockWidget *m_workflowDock = nullptr;

    // Step panel widgets (owned by m_stepHost panel bodies).
    QLabel *m_stepClassCountLabel = nullptr;
    QLabel *m_stepSampleStatsLabel = nullptr;
    QPushButton *m_stepTrainRoleBtn = nullptr;
    QPushButton *m_stepValidRoleBtn = nullptr;
    QPushButton *m_stepCvBtn = nullptr;
    QPushButton *m_stepPreviewBtn = nullptr;
    QPushButton *m_stepApplyBtn = nullptr;
    bool m_trainSampleRole = true;

    // Step 5 — accuracy panel (embedded).
    RsAccuracyPanel *m_accuracyPanel = nullptr;
    QPushButton *m_stepAccuracyPopupBtn = nullptr;

    // Step 6 — post-process panel.
    QLineEdit *m_ppInputEdit = nullptr;
    QLineEdit *m_ppOutputEdit = nullptr;
    QLineEdit *m_ppVectorEdit = nullptr;
    QCheckBox *m_ppSieveCb = nullptr;
    QCheckBox *m_ppMajorityCb = nullptr;
    QCheckBox *m_ppClumpCb = nullptr;
    QCheckBox *m_ppRecodeCb = nullptr;
    QCheckBox *m_ppPolygonizeCb = nullptr;
    QSpinBox *m_ppSieveSpin = nullptr;
    QSpinBox *m_ppMajoritySpin = nullptr;
    QTableWidget *m_ppRecodeTable = nullptr;
    QPushButton *m_ppRunBtn = nullptr;
    QPushButton *m_ppSkipBtn = nullptr;
    /// Last successful full-Apply classified raster path (Step 6 input default).
    QString m_lastClassifyPath;

    QDockWidget *m_classListDock = nullptr;
    QDockWidget *m_classQuickListDock = nullptr;
    QDockWidget *m_jmDock = nullptr;
    QDockWidget *m_spectralDock = nullptr;

    RsClassTableWidget *m_classTableWidget = nullptr;
    RsClassQuickList *m_classQuickListWidget = nullptr;
    RsSpectralCurveWidget *m_spectralCurve = nullptr;
    RsJmMatrixWidget *m_jmMatrix = nullptr;

    // Task 10.4 — ROI map tools (point/rectangle/polygon/freehand).
    RsRoiToolPoint *m_toolPoint = nullptr;
    RsRoiToolRectangle *m_toolRect = nullptr;
    RsRoiToolPolygon *m_toolPolygon = nullptr;
    RsRoiToolFreehand *m_toolFreehand = nullptr;

    // Task 10.7 — Magic-wand ROI map tool (L2-tolerance flood fill).
    RsRoiToolMagicWand *m_toolMagicWand = nullptr;

    // Source raster metadata for pixel-index rasterization. Empty path means
    // no raster is loaded yet — onRoiDrawn skips rasterization and stores an
    // RsRoi with empty pixel indices (geometry-only, still useful for later
    // re-rasterization once a raster is set in Task 10.5/10.7/10.8).
    QString m_sourceRasterPath;
    int m_sourceWidth = 0;
    int m_sourceHeight = 0;
    int m_sourceBandCount = 0;
    double m_sourceGt[6] = { 0, 1, 0, 0, 0, -1 };

    // Task 10.8 — ClassifierBar widget mounted in a bottom toolbar.
    RsClassifierSetupBar *m_classifierBar = nullptr;
    QAction *m_applyAction = nullptr;
    QAction *m_openRasterAction = nullptr;
    // Lifetime owner for QgsRasterLayer instances handed to m_canvas.
    QgsMapLayerStore *m_layerStore = nullptr;
    QgsRasterLayer *m_sourceLayer = nullptr; // non-owning

    // Phase 10A review patch — 500ms single-shot throttle for JM matrix
    // recomputation. Restarted on every m_rois::changed; fires once after
    // the user stops dragging in new ROIs.
    QTimer *m_jmRecomputeTimer = nullptr;

    // Phase 10A.1.3 — backend instance loaded from disk via the File menu.
    // Consumed (moved out) on the next applyClassification() call.
    std::unique_ptr<RsClassifierBackend> m_loadedBackend;
    // Optional feature scaler loaded from <model>.scale.json sidecar.
    // Consumed with m_loadedBackend on the next applyClassification().
    RsFeatureScaler m_loadedScaler;

    // Classification v1.1 — dirty close + QSettings workflow continuity.
    RsClassifySessionState mSession;
    bool mSuppressDirty = false;
    QString mLastModelPath;

  private slots:
    void onRoiDrawn( const QgsGeometry &geom, int classId );
    void onCurrentClassChanged( int classId );
};
