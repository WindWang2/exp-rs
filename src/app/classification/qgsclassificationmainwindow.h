#pragma once

#include <QMainWindow>
#include <QMap>
#include <QString>
#include <QVector>

#include <memory>

#include <opencv2/core.hpp>

#include "rs_classifier_backend.h"
#include "rs_classify_session_state.h"
#include "rs_classify_workflow_bridge.h"
#include "rs_classify_workflow_controller.h"
#include "rs_feature_scaler.h"
#include "rs_pixel_ignore_options.h"
#include "rs_post_process_task.h"

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
class QgsAdvancedDigitizingDockWidget;
class QgsFeature;
class QgsGeometry;
class QgsLayerTreeView;
class QgsMapCanvas;
class QgsMapLayer;
class QgsMapTool;
class QgsMapToolDigitizeFeature;
class QgsMapToolPan;
class QgsRasterLayer;
class QgsVectorLayer;
class RsRoiCollection;
class RsSessionMapWorkspace;
class RsClassTableWidget;
class RsClassQuickList;
class RsSpectralCurveWidget;
class RsJmMatrixWidget;
class RsRoiToolMagicWand;
class RsAccuracyPanel;
class RsClassifierSetupBar;
class RsClassifyStepperBar;
class RsClassifyStepHost;

/**
 * \brief Pixel-based supervised classification window.
 *
 * Training samples use a standard QGIS memory vector layer + digitizing
 * tools (add polygon / select / delete / edit toggle), rendered with a
 * categorized style by class id — same interaction model as main-window
 * vector editing.
 */
class QgsClassificationMainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit QgsClassificationMainWindow( QgisInterface *iface, QWidget *parent = nullptr );
    ~QgsClassificationMainWindow() override;

  public slots:
    bool openSourceRaster();
    bool openSourceRaster( const QString &path );
    void applyClassification();
    void applyPreview();
    void runCrossValidation();
    void recomputeSpectralCurves();
    void recomputeJmMatrix();
    void exportRois();
    void loadRois();
    void loadClassifierModel();
    bool saveClassificationProject( QString path = QString() );
    bool loadProjectFromFile( QString path = QString() );

  protected:
    void closeEvent( QCloseEvent *e ) override;

  private:
    void setupMenus();
    void setupToolbars();
    void setupDocks();
    void setupLayerManager();
    void setupStatusBar();
    void setupSampleVectorEditing();
    void setupClassifierBar();
    void setupWorkflowUi();
    void populateStepPanels();
    void refreshWorkflowUi();
    void syncWorkflowFromRois();
    void ensureDefaultClasses();
    void setActiveSampleRole( bool trainRole );
    void setClassifyBusy( bool busy );
    bool buildTrainingData( const QVector<int> &bands,
                            cv::Mat &X,
                            cv::Mat &y ) const;
    RsPixelIgnoreOptions currentIgnoreOptions() const;
    /// Open one post-process algorithm dialog (Sieve / Majority / …).
    void openPostProcessDialog( int algorithm /* RsPostProcessDialog::Algorithm */ );
    /// Run post-process; \a loadToLayers defaults to adding result to session tree.
    void runPostProcess( const RsPostProcessConfig &cfg,
                         bool loadToLayers = true,
                         const QString &jobTitle = QString(),
                         const QString &algorithmId = QString() );
    void exportSelectedStep7();
    void loadClassificationResultToMain();
    bool copyPathWithDialog( const QString &srcPath, const QString &title );
    /// Register layer in the session map (store + tree; source of truth for canvas).
    void addSessionLayer( QgsMapLayer *layer, bool insertOnTop = true );
    /**
     * Remove tree node and takeMapLayer from the session store.
     * Does **not** delete the layer — caller owns it afterward (delete on
     * replace paths; keep alive for z-order shuffle re-add).
     */
    void removeSessionLayer( QgsMapLayer *layer );

    RsClassifySessionState::WorkflowSnapshot captureWorkflowSnapshot() const;
    void applyWorkflowSnapshot( const RsClassifySessionState::WorkflowSnapshot &s );
    bool saveRoisToPath( QString path );

    /// Ensure sample memory layer exists (Polygon, cls_id / cls_name).
    void ensureSampleLayer();
    /// Categorized renderer by cls_id using class colors.
    void applySampleLayerRenderer();
    /// Rebuild RsRoiCollection from sample layer features (training cache).
    void rebuildRoisFromSampleLayer();
    /// Push RsRoiCollection geometries into the sample layer (load path).
    void syncSampleLayerFromRois();
    void updateRoiStatusLabels();
    int resolveActiveClassId( int preferred = 0 ) const;
    void ensureSampleLayerEditing( bool on );
    void deleteSelectedSamples();

    QgisInterface *m_iface = nullptr;
    QgsMapCanvas *m_canvas = nullptr;
    RsRoiCollection *m_rois = nullptr;

    /// Session-local map stack (store + tree + model + bridge).
    RsSessionMapWorkspace *m_sessionMap = nullptr;
    QgsLayerTreeView *m_layerTreeView = nullptr;
    QDockWidget *m_layerTreeDock = nullptr;

    /// Training sample vector layer (memory, owned via session map store).
    QgsVectorLayer *m_sampleLayer = nullptr;
    QgsAdvancedDigitizingDockWidget *m_cadDock = nullptr;
    QgsMapToolPan *m_toolPan = nullptr;
    QgsMapTool *m_toolSelect = nullptr;
    QgsMapToolDigitizeFeature *m_toolAddPolygon = nullptr;
    /// Optional flood-fill helper that still writes into m_sampleLayer.
    RsRoiToolMagicWand *m_toolMagicWand = nullptr;
    QAction *m_toggleEditAction = nullptr;
    QAction *m_addPolygonAction = nullptr;
    QAction *m_deleteSelectedAction = nullptr;
    bool mSuppressSampleSync = false;

    RsClassifyWorkflowController *m_workflow = nullptr;
    std::unique_ptr<RsClassifyWorkflowBridge> m_workflowBridge;
    RsClassifyStepperBar *m_stepper = nullptr;
    RsClassifyStepHost *m_stepHost = nullptr;
    QDockWidget *m_workflowDock = nullptr;

    QLabel *m_stepClassCountLabel = nullptr;
    QLabel *m_stepSampleStatsLabel = nullptr;
    QPushButton *m_stepTrainRoleBtn = nullptr;
    QPushButton *m_stepValidRoleBtn = nullptr;
    QAction *m_trainRoleAction = nullptr;
    QAction *m_validRoleAction = nullptr;
    QPushButton *m_stepCvBtn = nullptr;
    QPushButton *m_stepPreviewBtn = nullptr;
    QPushButton *m_stepApplyBtn = nullptr;
    bool m_trainSampleRole = true;
    bool m_classifyBusy = false;

    RsAccuracyPanel *m_accuracyPanel = nullptr;
    QPushButton *m_stepAccuracyPopupBtn = nullptr;

    QString m_lastClassifyPath;
    QString m_lastPostRasterPath;
    QString m_lastPostVectorPath;

    QCheckBox *m_exportClassifiedCb = nullptr;
    QCheckBox *m_exportPostRasterCb = nullptr;
    QCheckBox *m_exportPostVectorCb = nullptr;
    QCheckBox *m_exportRoiCb = nullptr;
    QCheckBox *m_exportAccuracyCsvCb = nullptr;
    QCheckBox *m_exportProjectCb = nullptr;
    QPushButton *m_exportSelectedBtn = nullptr;
    QPushButton *m_exportLoadToMainBtn = nullptr;

    QString m_accuracySource;
    QString m_projectPath;

    QDockWidget *m_classListDock = nullptr;
    QDockWidget *m_classQuickListDock = nullptr;
    QDockWidget *m_jmDock = nullptr;
    QDockWidget *m_spectralDock = nullptr;

    RsClassTableWidget *m_classTableWidget = nullptr;
    RsClassQuickList *m_classQuickListWidget = nullptr;
    RsSpectralCurveWidget *m_spectralCurve = nullptr;
    RsJmMatrixWidget *m_jmMatrix = nullptr;

    QString m_sourceRasterPath;
    int m_sourceWidth = 0;
    int m_sourceHeight = 0;
    int m_sourceBandCount = 0;
    double m_sourceGt[6] = { 0, 1, 0, 0, 0, -1 };

    RsClassifierSetupBar *m_classifierBar = nullptr;
    QAction *m_applyAction = nullptr;
    QAction *m_openRasterAction = nullptr;
    QgsRasterLayer *m_sourceLayer = nullptr;
    QgsRasterLayer *m_previewLayer = nullptr;

    QTimer *m_jmRecomputeTimer = nullptr;

    std::unique_ptr<RsClassifierBackend> m_loadedBackend;
    RsFeatureScaler m_loadedScaler;

    RsClassifySessionState mSession;
    bool mSuppressDirty = false;
    QString mLastModelPath;

  private slots:
    void onSampleDigitized( const QgsFeature &feature );
    void onMagicWandRoi( const QgsGeometry &geom, int classId );
    void onCurrentClassChanged( int classId );
    void onSampleLayerEdited();
    void onToggleEditing( bool on );
};
