// rs_obia_main_window.h — OBIA window (flat + hierarchical), operator client (#663).
//
// Thin client over the rs:obia_* operator seam: every processing flow
// (segmentation, feature extraction, ROI labeling, flat/hierarchy
// classification, vector export) is submitted to the Task Center as a real
// operator id. In-memory state (segment map, stats, labels, hierarchy) is
// presentation cache rehydrated from operator file outputs. The one
// documented exception is the interactive hierarchy-consolidation on label
// maps (RsHierarchyClassConsolidator — no operator yet, ADR 0126 debt).
#pragma once

#include <QMainWindow>

#include "rs_object_hierarchy.h"
#include "rs_segment_map.h"
#include "rs_segment_features.h"
#include "rs_segment_info_dock.h"
#include "rs_segment_select_tool.h"
#include "rs_obia_operator_adapter.h"
#include "rs_accuracy_assessment.h"

#include "processing/framework/task_center.h"

#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>

#include <QDockWidget>
#include <QHash>
#include <QMap>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QToolBar>

#include <memory>

class QgsLayerTree;
class QgsLayerTreeModel;
class QgsLayerTreeView;
class QProgressDialog;
class RsSessionMapWorkspace;

class RsObiaMainWindow : public QMainWindow
{
    Q_OBJECT
  public:
    explicit RsObiaMainWindow( QWidget *parent = nullptr );
    ~RsObiaMainWindow() override;

    /// Session map stack (Wave E secondary Display View host).
    RsSessionMapWorkspace *sessionMap() const { return m_sessionMap; }

    /// Segmentation options collected from the toolbar (schema defaults).
    using SegmentOptions = RsObiaOperatorAdapter::SegmentOptions;

    /// Flat segmentation via the rs:obia_segment operator. Returns the task
    /// id, or -1 if busy. Feature extraction chains automatically.
    long startSegmentationTask( const SegmentOptions &opts );

    /// Load a raster into the session canvas (test-friendly path behind the
    /// Load Raster file dialog).
    bool loadRasterFile( const QString &path );

    enum class PendingOp
    {
      None,
      Segmentation,       // rs:obia_segment in flight
      SegmentFeatures,    // chained rs:obia_features after segmentation
      Hierarchy,          // rs:obia_hierarchy build in flight
      HierarchyFeatures,  // chained / on-demand rs:obia_features (level L)
      HierarchyClassify,  // rs:obia_hierarchy classify leg
      FlatClassify,       // rs:obia_classify
      LabelImport,        // rs:obia_label
      Export,             // gdal:polygonize
    };

    bool isBusy() const { return m_pendingTaskId >= 0; }
    long pendingTaskId() const { return m_pendingTaskId; }
    PendingOp pendingOp() const { return m_pendingOp; }
    void cancelActiveTask();

    int segmentCount() const { return static_cast<int>( mSegMap.segmentCount() ); }

    /// Last classification output path (empty if none).
    QString lastClassRasterPath() const { return mLastClassRasterPath; }
    bool hasAccuracyResult() const { return mHasAccuracy; }
    RsAccuracyAssessment::Result lastAccuracy() const { return mLastAccuracy; }

  signals:
    /// Ask main window to load the classified raster into the project canvas.
    void requestLoadToMainMap( const QString &rasterPath );
    /// Classification finished with optional training-set accuracy.
    void classificationFinished( const QString &rasterPath,
                                 const RsAccuracyAssessment::Result &accuracy );

  private slots:
    void loadRaster();
    void runSegmentation();
    void runHierarchicalSegmentation();
    void runClassification();
    void exportResult();
    void importRoiLabels();
    void onActiveLevelChanged( int level );
    void onClassifyLevelChanged( int level );

    void onSegmentSelected( quint32 segmentId );
    void onSelectionCleared();
    void onAssignClass();
    void onClassTableContextMenu( const QPoint &pos );
    void onSegmentTableContextMenu( const QPoint &pos );
    void onObiaTaskUpdated( const sicnu::AlgorithmTaskInfo &info );
    void showAccuracyAssessment();
    void loadResultToMainMap();
    void runHierarchyConsolidation();
    void showClassifierConfigDialog();
    void onUncertaintySegmentDoubleClicked( int row, int col );

  private:
    void setupUi();
    void setupToolbar();
    void setupDocks();
    void setupMapCanvas();
    void updateSegmentTable();
    void updateSegmentTableRow( quint32 segId );
    void rebuildClassTable();
    void updateStatusLabel();
    void applySegmentationResult( const RsSegmentMap &segMap,
                                  bool usedOtb,
                                  QMap<quint32, RsSegmentFeatures::SegmentStat> stats );
    void applyHierarchyResult( RsObjectHierarchy hierarchy,
                               int activeLevel,
                               QMap<quint32, RsSegmentFeatures::SegmentStat> stats );
    void setActiveLevelMap( int level );
    long startFeaturesTask( int level, bool afterHierarchyBuild );
    void applyLevelFeaturesResult( int level, QMap<quint32, RsSegmentFeatures::SegmentStat> stats );
    QVector<int> allBandIndices() const;
    RsFeatureSelection featureSelection() const;
    void populateUncertaintyTable( const QMap<quint32, double> &uncertainties,
                                   const QMap<quint32, int> &classes );
    int currentClassifyLevel() const;
    void finishPendingUi();
    void loadClassifiedRaster( const QString &outputPath );

    /// Common Task Center submission of an operator job (single-flight gate
    /// is enforced by the callers via isBusy()).
    long submitOperatorTask( const QString &operatorId,
                             const Json::Value &params,
                             const QString &title );
    void watchProgressDialog( QProgressDialog *progress, long taskId );
    void pollIfAlreadyTerminal( long taskId );

    /// Session scratch directory for operator file outputs (label rasters,
    /// feature/label/uncertainty CSVs). Created lazily.
    QString scratchPath( const QString &fileName );
    /// Label raster path for a hierarchy level (or the flat segmentation).
    QString levelLabelsPath( int level ) const;

    // Map canvas + session workspace (not the main project catalog)
    QgsMapCanvas *mCanvas = nullptr;
    RsSessionMapWorkspace *m_sessionMap = nullptr;
    /// Non-owning; owned by session store after addLayer.
    QgsRasterLayer *mRasterLayer = nullptr;
    QgsRasterLayer *mClassifiedLayer = nullptr;

    // Segment data (active level surface)
    RsSegmentMap mSegMap;
    QMap<quint32, RsSegmentFeatures::SegmentStat> mSegStats;
    QMap<quint32, int> mSegmentLabels; // segmentId → classId (classify level)
    RsObjectHierarchy mHierarchy;
    QMap<int, QMap<quint32, RsSegmentFeatures::SegmentStat>> mHierarchyStats;
    int mActiveLevel = 0;
    int mClassifyLevel = 0; // default finest
    bool mHasHierarchy = false;
    QString mLastClassRasterPath;
    RsAccuracyAssessment::Result mLastAccuracy;
    bool mHasAccuracy = false;

    // Operator file outputs backing the session state (#663)
    std::unique_ptr<QTemporaryDir> m_scratch;
    QString m_segLabelsPath;                   // flat segmentation labels
    QString m_hierarchyFinePath;               // hierarchy level 0 labels
    QString m_hierarchyCoarsePath;             // hierarchy level 1 labels
    QString m_hierarchyParentsPath;            // fine→parent CSV
    QString m_pendingFeaturesCsv;              // features CSV being produced
    QString m_pendingUncertaintyCsv;           // entropy CSV being produced
    RsSegmentMap m_pendingSegMap;              // rehydrated during the chain
    bool m_pendingSegUsedOtb = false;
    int m_pendingFeaturesLevel = 0;
    RsObjectHierarchy m_pendingHierarchy;      // rehydrated during the build chain
    QMap<quint32, int> m_pendingRoiLabels;     // rs:obia_label merge input

    void rememberClassification( const QString &outputPath,
                                 const RsAccuracyAssessment::Result &accuracy );
    QHash<int, QString> classNameMap() const;

    /// Classifier choice + hyperparameters (schema defaults via the adapter).
    RsObiaOperatorAdapter::ClassifierOptions classifierOptions() const;

  public:
    struct ClassDef
    {
        int id;
        QString name;
        QColor color;
    };

  private:
    QVector<ClassDef> mClassDefs;
    int mCurrentClassId = 1;
    int mRfNumTrees = 100;
    int mRfMaxDepth = 10;
    int mRfMinSampleCount = 5;
    int mMlpHiddenLayerSize = 16;
    int mMlpMaxIter = 500;

    RsSegmentSelectTool *mSelectTool = nullptr;

    RsSegmentInfoDock *mInfoDock = nullptr;
    QDockWidget *mClassDock = nullptr;
    QTableWidget *mClassTable = nullptr;
    QDockWidget *mSegmentDock = nullptr;
    QTableWidget *mSegmentTable = nullptr;
    QDockWidget *mUncertaintyDock = nullptr;
    QTableWidget *mUncertaintyTable = nullptr;
    QDockWidget *mFeatureDock = nullptr;
    QTreeWidget *mFeatureTree = nullptr;

    QToolBar *mToolbar = nullptr;

    QString mRasterPath;
    int mBandCount = 0;
    int mSelectedClassRow = 0;

    PendingOp m_pendingOp = PendingOp::None;
    long m_pendingTaskId = -1;
    QProgressDialog *m_pendingProgress = nullptr;
};
