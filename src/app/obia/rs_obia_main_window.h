// rs_obia_main_window.h — Phase 10B Task 10B.5: OBIA classification main window.
//
// Standalone QMainWindow for object-based image analysis:
//   - Left dock: layer list + segment class assignment
//   - Center: map canvas showing segmentation result
//   - Right dock: classifier setup + run controls
//   - Bottom dock: segment info
//   - Toolbar: load raster, segment, classify, export
#pragma once

#include <QMainWindow>

#include "rs_segment_map.h"
#include "rs_segment_features.h"
#include "rs_segment_info_dock.h"
#include "rs_segment_select_tool.h"

#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>

#include <QDockWidget>
#include <QHash>
#include <QMap>
#include <QTableWidget>
#include <QToolBar>

#include <memory>

class QgsLayerTree;
class QgsLayerTreeModel;
class QgsLayerTreeView;

class RsObiaMainWindow : public QMainWindow
{
    Q_OBJECT
  public:
    explicit RsObiaMainWindow( QWidget *parent = nullptr );
    ~RsObiaMainWindow() override;

  private slots:
    void loadRaster();
    void runSegmentation();
    void runClassification();
    void exportResult();

    void onSegmentSelected( quint32 segmentId );
    void onSelectionCleared();
    void onAssignClass();

  private:
    void setupUi();
    void setupToolbar();
    void setupDocks();
    void setupMapCanvas();
    void updateSegmentTable();
    void updateStatusLabel();
    void applySegmentationResult( const RsSegmentMap &segMap, bool usedOtb, const QVector<int> &bandIndices );

    // Map canvas
    QgsMapCanvas *mCanvas = nullptr;
    std::shared_ptr<QgsRasterLayer> mRasterLayer;

    // Layer tree
    QgsLayerTree *mLayerTree = nullptr;
    QgsLayerTreeModel *mLayerTreeModel = nullptr;
    QgsLayerTreeView *mLayerView = nullptr;

    // Segment data
    RsSegmentMap mSegMap;
    QMap<quint32, RsSegmentFeatures::SegmentStat> mSegStats;
    QMap<quint32, int> mSegmentLabels; // segmentId → classId

public:
    // Class definitions (reuse Phase 10A pattern)
    struct ClassDef
    {
        int id;
        QString name;
        QColor color;
    };

private:
    QVector<ClassDef> mClassDefs;
    int mCurrentClassId = 1;

    // Map tool
    RsSegmentSelectTool *mSelectTool = nullptr;

    // Docks
    RsSegmentInfoDock *mInfoDock = nullptr;
    QDockWidget *mClassDock = nullptr;
    QTableWidget *mClassTable = nullptr;
    QDockWidget *mSegmentDock = nullptr;
    QTableWidget *mSegmentTable = nullptr;

    // Toolbar
    QToolBar *mToolbar = nullptr;

    // State
    QString mRasterPath;
    int mBandCount = 0;
    int mSelectedClassRow = 0;
};
