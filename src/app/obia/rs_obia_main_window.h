// rs_obia_main_window.h — OBIA window (flat + hierarchical V1).
#pragma once

#include <QMainWindow>

#include "rs_object_hierarchy.h"
#include "rs_segment_map.h"
#include "rs_segment_features.h"
#include "rs_segment_info_dock.h"
#include "rs_segment_select_tool.h"
#include "rs_obia_segmentation.h"
#include "rs_obia_task.h"
#include "rs_accuracy_assessment.h"

#include "processing/framework/task_center.h"

#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>

#include <QDockWidget>
#include <QHash>
#include <QMap>
#include <QTableWidget>
#include <QToolBar>

#include <atomic>
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

    /// Flat OBIA segmentation via Task Center. Returns task id, or -1 if busy.
    long startSegmentationTask( const RsObiaSegmentationConfig &segCfg,
                                const QVector<int> &bandIndices );

    /// Two-level hierarchy build via Task Center. Returns task id, or -1 if busy.
    long startHierarchyTask( int spatialRadius, double rangeRadius, int minRegionSize,
                             double watershedThreshold = 0.01 );

    /// Hierarchy-level classify + paint via Task Center.
    long startHierarchyClassifyTask( int clsLevel, const QString &outputPath,
                                     std::shared_ptr<RsClassifierBackend> backend,
                                     const QVector<int> &bandIndices,
                                     const QHash<int, QColor> &classColors,
                                     const QMap<quint32, int> &trainLabels );

    /// Flat classify via Task Center (owns the RsObiaTask until terminal).
    long startFlatClassifyTask( RsObiaTask *task, const QString &outputPath,
                                const QString &algoName );

    long pendingTaskId() const { return m_pendingTaskId; }
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
    void onObiaTaskUpdated( const sicnu::AlgorithmTaskInfo &info );
    void showAccuracyAssessment();
    void loadResultToMainMap();

  private:
    enum class PendingOp
    {
      None,
      Segmentation,
      Hierarchy,
      HierarchyClassify,
      FlatClassify,
    };

    void setupUi();
    void setupToolbar();
    void setupDocks();
    void setupMapCanvas();
    void updateSegmentTable();
    void updateStatusLabel();
    void applySegmentationResult( const RsSegmentMap &segMap,
                                  bool usedOtb,
                                  QMap<quint32, RsSegmentFeatures::SegmentStat> stats );
    void applyHierarchyResult( RsObjectHierarchy hierarchy,
                               int activeLevel,
                               QMap<quint32, RsSegmentFeatures::SegmentStat> stats );
    void setActiveLevelMap( int level );
    QVector<int> allBandIndices() const;
    int currentClassifyLevel() const;
    void finishPendingUi();
    bool isBusy() const { return m_pendingTaskId >= 0; }
    void loadClassifiedRaster( const QString &outputPath );

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
    int mActiveLevel = 0;
    int mClassifyLevel = 0; // default finest
    bool mHasHierarchy = false;
    QString mLastClassRasterPath;
    RsAccuracyAssessment::Result mLastAccuracy;
    bool mHasAccuracy = false;

    void rememberClassification( const QString &outputPath,
                                 const RsAccuracyAssessment::Result &accuracy );
    QHash<int, QString> classNameMap() const;

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

    RsSegmentSelectTool *mSelectTool = nullptr;

    RsSegmentInfoDock *mInfoDock = nullptr;
    QDockWidget *mClassDock = nullptr;
    QTableWidget *mClassTable = nullptr;
    QDockWidget *mSegmentDock = nullptr;
    QTableWidget *mSegmentTable = nullptr;

    QToolBar *mToolbar = nullptr;

    QString mRasterPath;
    int mBandCount = 0;
    int mSelectedClassRow = 0;

    // Shared Task Center pending state (#30 + #31).
    struct PendingSegWork
    {
        RsObiaSegmentationResult seg;
        QMap<quint32, RsSegmentFeatures::SegmentStat> stats;
    };
    struct PendingHierWork
    {
        bool ok = false;
        QString error;
        RsObjectHierarchy hierarchy;
        QMap<quint32, RsSegmentFeatures::SegmentStat> stats;
    };
    struct PendingHierClsWork
    {
        bool ok = false;
        QString error;
        QString outputPath;
        int clsLevel = 0;
        RsAccuracyAssessment::Result accuracy;
    };

    PendingOp m_pendingOp = PendingOp::None;
    long m_pendingTaskId = -1;
    std::shared_ptr<std::atomic<bool>> m_pendingCanceled;
    QProgressDialog *m_pendingProgress = nullptr;

    std::shared_ptr<PendingSegWork> m_pendingSegWork;
    std::shared_ptr<PendingHierWork> m_pendingHierWork;
    std::shared_ptr<PendingHierClsWork> m_pendingHierClsWork;
    RsObiaTask *m_pendingFlatTask = nullptr; // deleteLater after terminal
    QString m_pendingFlatOutputPath;
};
