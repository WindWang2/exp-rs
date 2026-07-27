// rs_obia_main_window.h — OBIA window (flat + hierarchical V1).
#pragma once

#include <QMainWindow>

#include "rs_object_hierarchy.h"
#include "rs_segment_map.h"
#include "rs_segment_features.h"
#include "rs_segment_info_dock.h"
#include "rs_segment_select_tool.h"
#include "rs_obia_segmentation.h"

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

class RsObiaMainWindow : public QMainWindow
{
    Q_OBJECT
  public:
    explicit RsObiaMainWindow( QWidget *parent = nullptr );
    ~RsObiaMainWindow() override;

    /// Submit flat OBIA segmentation through Task Center. Returns the task id,
    /// or -1 if a segmentation is already pending. Production UI entry and
    /// tests both use this seam.
    long startSegmentationTask( const RsObiaSegmentationConfig &segCfg,
                                const QVector<int> &bandIndices );

    long pendingSegmentationTaskId() const { return m_pendingSegmentationTaskId; }
    int segmentCount() const { return static_cast<int>( mSegMap.segmentCount() ); }

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
    void onSegmentationTaskUpdated( const sicnu::AlgorithmTaskInfo &info );

  private:
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
    void finishPendingSegmentationUi();

    // Map canvas
    QgsMapCanvas *mCanvas = nullptr;
    std::shared_ptr<QgsRasterLayer> mRasterLayer;

    // Layer tree
    QgsLayerTree *mLayerTree = nullptr;
    QgsLayerTreeModel *mLayerTreeModel = nullptr;
    QgsLayerTreeView *mLayerView = nullptr;

    // Segment data (active level surface)
    RsSegmentMap mSegMap;
    QMap<quint32, RsSegmentFeatures::SegmentStat> mSegStats;
    QMap<quint32, int> mSegmentLabels; // segmentId → classId (classify level)
    RsObjectHierarchy mHierarchy;
    int mActiveLevel = 0;
    int mClassifyLevel = 0; // default finest
    bool mHasHierarchy = false;
    QString mLastClassRasterPath;

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

    // Flat segmentation Task Center pending state (#30).
    struct PendingSegWork
    {
        RsObiaSegmentationResult seg;
        QMap<quint32, RsSegmentFeatures::SegmentStat> stats;
    };
    long m_pendingSegmentationTaskId = -1;
    std::shared_ptr<PendingSegWork> m_pendingSegWork;
    std::shared_ptr<std::atomic<bool>> m_pendingSegCanceled;
    QProgressDialog *m_pendingSegProgress = nullptr;
};
