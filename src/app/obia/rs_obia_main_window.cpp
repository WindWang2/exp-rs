// rs_obia_main_window.cpp — OBIA window: thin operator client (#663).
//
// Every processing flow dispatches a real operator id through the Task
// Center seam (rs:obia_segment / rs:obia_features / rs:obia_label /
// rs:obia_classify / rs:obia_hierarchy / gdal:polygonize). In-memory state
// is presentation cache rehydrated from operator file outputs. The old
// the legacy GUI executor lambdas (direct RsOtbSegmenter /
// RsSimpleSegmenter / RsSegmentFeatures / RsObjectClassify / RsClassRaster
// calls under pseudo algorithm ids) are gone — see ADR 0126.
#include "rs_obia_main_window.h"
#include "dialogs/dialog_help_catalog.h"
#include "sicnu_logging.h"

#include "rs_hierarchy_class_consolidator.h"
#include "classification/rs_accuracy_dialog.h"
#include "shell/rs_session_map_workspace.h"

#include "jobs/job_types.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/task_center.h"
#include "processing/tools/tool_path_manager.h"

#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgstaskmanager.h>
#include <qgslayertreemodel.h>
#include <qgslayertreeview.h>
#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>
#include <qgsproject.h>
#include <qgsdockwidget.h>

#include <QAction>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QGuiApplication>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QSpinBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

#include <cmath>
#include <memory>
#include <map>
#include <vector>
#include <gdal.h>

namespace
{

/// Operator schema default lookup — the single source of truth for widget
/// initial values (GUI defaults = operator defaults, #663).
int schemaIntDefault( const char *operatorId, const char *param, int fallback )
{
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( operatorId );
    if ( !op )
        return fallback;
    const Json::Value schema = op->schema();
    const Json::Value &value = schema["properties"][param]["default"];
    return value.isIntegral() ? value.asInt() : fallback;
}

double schemaDoubleDefault( const char *operatorId, const char *param, double fallback )
{
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( operatorId );
    if ( !op )
        return fallback;
    const Json::Value schema = op->schema();
    const Json::Value &value = schema["properties"][param]["default"];
    return value.isNumeric() ? value.asDouble() : fallback;
}

} // namespace

// ---------------------------------------------------------------------------
// Default class definitions (same as Phase 10A)
// ---------------------------------------------------------------------------
static QVector<RsObiaMainWindow::ClassDef> defaultClassDefs()
{
    return {
        { 1, QObject::tr( "Forest" ), QColor( 34, 139, 34 ) },
        { 2, QObject::tr( "Grassland" ), QColor( 144, 238, 144 ) },
        { 3, QObject::tr( "Water" ), QColor( 30, 144, 255 ) },
        { 4, QObject::tr( "Built-up" ), QColor( 255, 99, 71 ) },
        { 5, QObject::tr( "Cropland" ), QColor( 255, 215, 0 ) },
        { 6, QObject::tr( "Bare Soil" ), QColor( 210, 180, 140 ) },
    };
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

RsObiaMainWindow::RsObiaMainWindow( QWidget *parent )
    : QMainWindow( parent )
    , mClassDefs( defaultClassDefs() )
{
    setWindowTitle( tr( "OBIA — Object-Based Classification" ) );
  setWhatsThis( SicnuDialogHelp::htmlForTool( QStringLiteral( "obia" ), windowTitle() ) );
  setToolTip( SicnuDialogHelp::shortForTool( QStringLiteral( "obia" ), windowTitle() ) );
    resize( 1200, 800 );

    setupUi();
    setupToolbar();
    setupDocks();
    setupMapCanvas();
    updateStatusLabel();

    connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated,
             this, &RsObiaMainWindow::onObiaTaskUpdated );
}

RsObiaMainWindow::~RsObiaMainWindow()
{
  // #650: the app-global WaitCursor override installed by the task launchers
  // was only restored from finishPendingUi() (via the taskUpdated signal).
  // Closing this non-modal window mid-run dropped that connection and left
  // the WaitCursor overridden for the whole application session while the
  // task silently ran to completion. cancelActiveTask() cancels the task and
  // restores the cursor before member teardown destroys the progress dialog.
  cancelActiveTask();
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void RsObiaMainWindow::setupUi()
{
    // Central widget: map canvas in a frame
    mCanvas = new QgsMapCanvas( this );
    SicnuDialogHelp::tip( mCanvas, tr( "地图画布：显示影像、分割边界与分类结果。用「选择对象」工具点选对象以查看/赋类。" ) );
    setCentralWidget( mCanvas );
}

void RsObiaMainWindow::setupToolbar()
{
    mToolbar = addToolBar( tr( "OBIA" ) );
    mToolbar->setObjectName( "obiaToolbar" );

    auto *loadAct = mToolbar->addAction( tr( "Load Raster" ), this, &RsObiaMainWindow::loadRaster );
    SicnuDialogHelp::tip( loadAct, tr( "加载待分割/分类的栅格影像。" ) );

    mToolbar->addSeparator();

    // Segmentation params — initial values come from the rs:obia_segment
    // schema (single source of truth with the operator contract, #663).
    mToolbar->addWidget( new QLabel( tr( " Segments:" ) ) );
    auto *kernelSpin = new QSpinBox;
    kernelSpin->setRange( 3, 21 );
    kernelSpin->setSingleStep( 2 ); // smoothKernel must be odd (simple engine)
    kernelSpin->setValue( schemaIntDefault( "rs:obia_segment", "smoothKernel", 5 ) );
    SicnuDialogHelp::tip( kernelSpin, tr(
      "平滑核大小（rs:obia_segment.smoothKernel，奇数；同时用作 OTB spatialRadius）。越大对象边界越粗、碎斑越少。" ) );
    kernelSpin->setObjectName( "kernelSpin" );
    mToolbar->addWidget( kernelSpin );

    auto *binsSpin = new QSpinBox;
    binsSpin->setRange( 2, 128 );
    binsSpin->setValue( schemaIntDefault( "rs:obia_segment", "quantizeBins", 32 ) );
    SicnuDialogHelp::tip( binsSpin, tr(
      "量化级数（rs:obia_segment.quantizeBins，内置分割回退）。级数多则细节多、对象更碎。" ) );
    binsSpin->setObjectName( "binsSpin" );
    mToolbar->addWidget( binsSpin );

    auto *rangeSpin = new QDoubleSpinBox;
    rangeSpin->setRange( 0.1, 1000.0 );
    rangeSpin->setDecimals( 1 );
    rangeSpin->setSingleStep( 0.5 );
    rangeSpin->setValue( schemaDoubleDefault( "rs:obia_segment", "rangeRadius", 15.0 ) );
    SicnuDialogHelp::tip( rangeSpin, tr(
      "OTB MeanShift 光谱半径（rs:obia_segment.rangeRadius，米）。" ) );
    rangeSpin->setObjectName( "rangeSpin" );
    mToolbar->addWidget( rangeSpin );

    auto *minRegionSpin = new QSpinBox;
    minRegionSpin->setRange( 10, 10000 );
    minRegionSpin->setValue( schemaIntDefault( "rs:obia_segment", "minRegionSize", 50 ) );
    SicnuDialogHelp::tip( minRegionSpin, tr(
      "最小对象像元数（rs:obia_segment.minRegionSize）。小于此值的区域会被合并，抑制碎斑。" ) );
    minRegionSpin->setObjectName( "minRegionSpin" );
    mToolbar->addWidget( minRegionSpin );

    auto *segAct = mToolbar->addAction( tr( "Segment" ), this, &RsObiaMainWindow::runSegmentation );
    SicnuDialogHelp::tip( segAct, tr( "运行单层影像分割（rs:obia_segment，优先 OTB MeanShift，缺失时内置分割回退）。" ) );

    auto *hierAct = mToolbar->addAction( tr( "Hierarchy" ), this, &RsObiaMainWindow::runHierarchicalSegmentation );
    SicnuDialogHelp::tip( hierAct, tr(
      "两层层次分割：细层 MeanShift + 粗层 Watershed + 父链接（rs:obia_hierarchy，需 OTB）。" ) );

    mToolbar->addWidget( new QLabel( tr( " View L:" ) ) );
    auto *levelSpin = new QSpinBox;
    levelSpin->setRange( 0, 1 );
    levelSpin->setValue( 0 );
    levelSpin->setObjectName( "levelSpin" );
    SicnuDialogHelp::tip( levelSpin, tr( "活动显示层级（0=最细）。对象 id 按层独立编号。" ) );
    mToolbar->addWidget( levelSpin );
    connect( levelSpin, QOverload<int>::of( &QSpinBox::valueChanged ),
             this, &RsObiaMainWindow::onActiveLevelChanged );

    mToolbar->addSeparator();

    // Classifier selection
    mToolbar->addWidget( new QLabel( tr( " Classifier:" ) ) );
    auto *classifierCombo = new QComboBox;
    classifierCombo->addItems( { "NormalBayes", "SVM", "RandomForest", "KMeans", "MLP" } );
    SicnuDialogHelp::tip( classifierCombo, tr(
      "对象级分类器（rs:obia_classify.method）：NormalBayes / SVM / RandomForest / KMeans / MLP。" ) );
    classifierCombo->setObjectName( "classifierCombo" );
    mToolbar->addWidget( classifierCombo );

    mToolbar->addWidget( new QLabel( tr( " Cls L:" ) ) );
    auto *classifyLevelSpin = new QSpinBox;
    classifyLevelSpin->setRange( 0, 1 );
    classifyLevelSpin->setValue( 0 );
    classifyLevelSpin->setObjectName( "classifyLevelSpin" );
    SicnuDialogHelp::tip( classifyLevelSpin, tr(
      "分类层级，默认 0（最细）。训练标签绑定该层对象。" ) );
    mToolbar->addWidget( classifyLevelSpin );
    connect( classifyLevelSpin, QOverload<int>::of( &QSpinBox::valueChanged ),
             this, &RsObiaMainWindow::onClassifyLevelChanged );

    auto *clsAct = mToolbar->addAction( tr( "Classify" ), this, &RsObiaMainWindow::runClassification );
    SicnuDialogHelp::tip( clsAct, tr( "对所选层级的对象进行分类（rs:obia_classify / rs:obia_hierarchy）。" ) );

    auto *cfgAct = mToolbar->addAction( tr( "Params" ), this, &RsObiaMainWindow::showClassifierConfigDialog );
    SicnuDialogHelp::tip( cfgAct, tr( "配置所选分类器的超参数（rs:obia_classify 的 rfNumTrees / mlpHiddenLayerSize 等）。" ) );

    auto *roiAct = mToolbar->addAction( tr( "Import ROI" ), this, &RsObiaMainWindow::importRoiLabels );
    SicnuDialogHelp::tip( roiAct, tr(
      "从训练多边形按多数票标注对象（rs:obia_label）；与点击标注冲突时后写覆盖并提示。" ) );

    auto *consAct = mToolbar->addAction( tr( "Consolidate" ), this, &RsObiaMainWindow::runHierarchyConsolidation );
    SicnuDialogHelp::tip( consAct, tr( "消解多尺度层次之间的分类矛盾（向上多数票投票 / 向下集成）。" ) );

    mToolbar->addSeparator();
    auto *accAct = mToolbar->addAction( tr( "精度评价" ), this, &RsObiaMainWindow::showAccuracyAssessment );
    SicnuDialogHelp::tip( accAct, tr( "查看最近一次对象分类的训练样本精度（混淆矩阵 / OA / Kappa）。" ) );

    auto *toMainAct = mToolbar->addAction( tr( "加载到主图" ), this, &RsObiaMainWindow::loadResultToMainMap );
    SicnuDialogHelp::tip( toMainAct, tr( "将分类结果栅格加载到主窗口地图。" ) );

    mToolbar->addSeparator();
    auto *expAct = mToolbar->addAction( tr( "Export" ), this, &RsObiaMainWindow::exportResult );
    SicnuDialogHelp::tip( expAct, tr( "导出分类栅格（及可选矢量化 gdal:polygonize）。" ) );
}

void RsObiaMainWindow::setupDocks()
{
    // Left dock: class assignment
    mClassDock = new QDockWidget( tr( "Classes" ), this );
    mClassDock->setObjectName( "obiaClassDock" );
    SicnuDialogHelp::tip( mClassDock, tr( "类别表：ID、名称、显示颜色。用于对象分类图例与标注。" ) );
    mClassTable = new QTableWidget;
    mClassTable->setColumnCount( 3 );
    mClassTable->setHorizontalHeaderLabels( { tr( "ID" ), tr( "Name" ), tr( "Color" ) } );
    SicnuDialogHelp::tip( mClassTable, tr( "类别定义。ID 对应分类栅格像元值。右键可编辑名称/颜色、插入/删除类别。" ) );
    mClassTable->horizontalHeader()->setStretchLastSection( true );
    mClassTable->setSelectionBehavior( QAbstractItemView::SelectRows );
    mClassTable->setSelectionMode( QAbstractItemView::SingleSelection );
    mClassTable->setContextMenuPolicy( Qt::CustomContextMenu );
    mClassTable->setMinimumWidth( 180 );

    // Populate class table
    mClassTable->setRowCount( mClassDefs.size() );
    for ( int i = 0; i < mClassDefs.size(); ++i )
    {
        auto *idItem = new QTableWidgetItem( QString::number( mClassDefs[i].id ) );
        idItem->setFlags( idItem->flags() & ~Qt::ItemIsEditable ); // ID maps to pixel value; keep read-only
        mClassTable->setItem( i, 0, idItem );
        mClassTable->setItem( i, 1, new QTableWidgetItem( mClassDefs[i].name ) );
        auto *colorItem = new QTableWidgetItem;
        colorItem->setFlags( colorItem->flags() & ~Qt::ItemIsEditable ); // edited via context menu color picker
        colorItem->setBackground( mClassDefs[i].color );
        mClassTable->setItem( i, 2, colorItem );
    }
    connect( mClassTable, &QTableWidget::cellClicked, this, [this]( int row, int ) {
        if ( row >= 0 && row < mClassDefs.size() )
            mCurrentClassId = mClassDefs[row].id;
    });
    connect( mClassTable, &QTableWidget::customContextMenuRequested,
             this, &RsObiaMainWindow::onClassTableContextMenu );

    // Assign button
    auto *assignBtn = new QPushButton( tr( "Assign to Selected Segment" ) );
    SicnuDialogHelp::tip( assignBtn, tr( "把当前类别（选中行）赋给画布上选中的对象。" ) );
    connect( assignBtn, &QPushButton::clicked, this, &RsObiaMainWindow::onAssignClass );

    auto *classWidget = new QWidget;
    auto *classLayout = new QVBoxLayout( classWidget );
    classLayout->addWidget( mClassTable );
    classLayout->addWidget( assignBtn );
    classLayout->setContentsMargins( 0, 0, 0, 0 );
    mClassDock->setWidget( classWidget );
    addDockWidget( Qt::LeftDockWidgetArea, mClassDock );

    // Bottom dock: segment info
    mInfoDock = new RsSegmentInfoDock( this );
    SicnuDialogHelp::tip( mInfoDock, tr( "对象信息：选中对象的形状、光谱与层级统计（只读）。" ) );
    mInfoDock->setWhatsThis( SicnuDialogHelp::htmlForTool(
        QStringLiteral( "obia_segment_info" ), mInfoDock->windowTitle() ) );
    addDockWidget( Qt::BottomDockWidgetArea, mInfoDock );

    // Right dock: segment list
    mSegmentDock = new QDockWidget( tr( "Segments" ), this );
    mSegmentDock->setObjectName( "obiaSegmentDock" );
    SicnuDialogHelp::tip( mSegmentDock, tr( "对象列表：当前层级所有分割对象。右键可赋类、查看信息、复制 ID。" ) );
    mSegmentTable = new QTableWidget;
    mSegmentTable->setColumnCount( 3 );
    mSegmentTable->setHorizontalHeaderLabels( { tr( "ID" ), tr( "Pixels" ), tr( "Class" ) } );
    mSegmentTable->horizontalHeader()->setStretchLastSection( true );
    mSegmentTable->setSelectionBehavior( QAbstractItemView::SelectRows );
    mSegmentTable->setSelectionMode( QAbstractItemView::SingleSelection );
    mSegmentTable->setContextMenuPolicy( Qt::CustomContextMenu );
    mSegmentTable->setEditTriggers( QAbstractItemView::NoEditTriggers ); // filled programmatically
    SicnuDialogHelp::tip( mSegmentTable, tr( "对象列表（ID/像元数/类别）。右键赋为当前类别、查看信息或复制 ID。" ) );
    mSegmentDock->setWidget( mSegmentTable );
    addDockWidget( Qt::RightDockWidgetArea, mSegmentDock );
    connect( mSegmentTable, &QTableWidget::customContextMenuRequested,
             this, &RsObiaMainWindow::onSegmentTableContextMenu );

    // Active learning uncertainty dock
    mUncertaintyDock = new QDockWidget( tr( "Uncertainty Candidates (Active Learning)" ), this );
    mUncertaintyDock->setObjectName( "obiaUncertaintyDock" );
    mUncertaintyTable = new QTableWidget;
    mUncertaintyTable->setColumnCount( 3 );
    mUncertaintyTable->setHorizontalHeaderLabels( { tr( "Seg ID" ), tr( "Entropy (H)" ), tr( "Predicted Class" ) } );
    mUncertaintyTable->horizontalHeader()->setStretchLastSection( true );
    mUncertaintyTable->setSelectionBehavior( QAbstractItemView::SelectRows );
    mUncertaintyTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
    connect( mUncertaintyTable, &QTableWidget::cellDoubleClicked,
             this, &RsObiaMainWindow::onUncertaintySegmentDoubleClicked );
    mUncertaintyDock->setWidget( mUncertaintyTable );
    addDockWidget( Qt::RightDockWidgetArea, mUncertaintyDock );
    mUncertaintyDock->hide();

    // Left dock: Feature selection tree
    mFeatureDock = new QDockWidget( tr( "Feature Tree (特征树选择)" ), this );
    mFeatureDock->setObjectName( "obiaFeatureDock" );
    mFeatureTree = new QTreeWidget;
    mFeatureTree->setHeaderHidden( true );

    auto *spectralRoot = new QTreeWidgetItem( mFeatureTree, { tr( "Spectral Features (光谱特征)" ) } );
    spectralRoot->setFlags( spectralRoot->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate );
    spectralRoot->setCheckState( 0, Qt::Checked );

    auto *itemMean = new QTreeWidgetItem( spectralRoot, { tr( "Band Mean (波段均值)" ) } );
    itemMean->setFlags( itemMean->flags() | Qt::ItemIsUserCheckable );
    itemMean->setCheckState( 0, Qt::Checked );
    itemMean->setData( 0, Qt::UserRole, "mean" );

    auto *itemStd = new QTreeWidgetItem( spectralRoot, { tr( "Band StdDev (标准差)" ) } );
    itemStd->setFlags( itemStd->flags() | Qt::ItemIsUserCheckable );
    itemStd->setCheckState( 0, Qt::Checked );
    itemStd->setData( 0, Qt::UserRole, "stddev" );

    auto *itemMin = new QTreeWidgetItem( spectralRoot, { tr( "Band Min (最小值)" ) } );
    itemMin->setFlags( itemMin->flags() | Qt::ItemIsUserCheckable );
    itemMin->setCheckState( 0, Qt::Checked );
    itemMin->setData( 0, Qt::UserRole, "min" );

    auto *itemMax = new QTreeWidgetItem( spectralRoot, { tr( "Band Max (最大值)" ) } );
    itemMax->setFlags( itemMax->flags() | Qt::ItemIsUserCheckable );
    itemMax->setCheckState( 0, Qt::Checked );
    itemMax->setData( 0, Qt::UserRole, "max" );

    auto *textureRoot = new QTreeWidgetItem( mFeatureTree, { tr( "Texture Features (GLCM 纹理特征)" ) } );
    textureRoot->setFlags( textureRoot->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate );
    textureRoot->setCheckState( 0, Qt::Checked );

    auto *itemContrast = new QTreeWidgetItem( textureRoot, { tr( "GLCM Contrast (对比度)" ) } );
    itemContrast->setFlags( itemContrast->flags() | Qt::ItemIsUserCheckable );
    itemContrast->setCheckState( 0, Qt::Checked );
    itemContrast->setData( 0, Qt::UserRole, "contrast" );

    auto *itemCorr = new QTreeWidgetItem( textureRoot, { tr( "GLCM Correlation (相关性)" ) } );
    itemCorr->setFlags( itemCorr->flags() | Qt::ItemIsUserCheckable );
    itemCorr->setCheckState( 0, Qt::Checked );
    itemCorr->setData( 0, Qt::UserRole, "correlation" );

    auto *itemEnergy = new QTreeWidgetItem( textureRoot, { tr( "GLCM Energy (能量)" ) } );
    itemEnergy->setFlags( itemEnergy->flags() | Qt::ItemIsUserCheckable );
    itemEnergy->setCheckState( 0, Qt::Checked );
    itemEnergy->setData( 0, Qt::UserRole, "energy" );

    auto *itemHomog = new QTreeWidgetItem( textureRoot, { tr( "GLCM Homogeneity (同质性)" ) } );
    itemHomog->setFlags( itemHomog->flags() | Qt::ItemIsUserCheckable );
    itemHomog->setCheckState( 0, Qt::Checked );
    itemHomog->setData( 0, Qt::UserRole, "homogeneity" );

    auto *shapeRoot = new QTreeWidgetItem( mFeatureTree, { tr( "Shape Features (几何与形状特征)" ) } );
    shapeRoot->setFlags( shapeRoot->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate );
    shapeRoot->setCheckState( 0, Qt::Checked );

    auto *itemArea = new QTreeWidgetItem( shapeRoot, { tr( "Area (像素面积)" ) } );
    itemArea->setFlags( itemArea->flags() | Qt::ItemIsUserCheckable );
    itemArea->setCheckState( 0, Qt::Checked );
    itemArea->setData( 0, Qt::UserRole, "area" );

    auto *itemPerimeter = new QTreeWidgetItem( shapeRoot, { tr( "Perimeter (周长)" ) } );
    itemPerimeter->setFlags( itemPerimeter->flags() | Qt::ItemIsUserCheckable );
    itemPerimeter->setCheckState( 0, Qt::Checked );
    itemPerimeter->setData( 0, Qt::UserRole, "perimeter" );

    auto *itemShapeIdx = new QTreeWidgetItem( shapeRoot, { tr( "Shape Index (形状指数)" ) } );
    itemShapeIdx->setFlags( itemShapeIdx->flags() | Qt::ItemIsUserCheckable );
    itemShapeIdx->setCheckState( 0, Qt::Checked );
    itemShapeIdx->setData( 0, Qt::UserRole, "shapeIndex" );

    auto *itemCompactness = new QTreeWidgetItem( shapeRoot, { tr( "Compactness (紧凑度)" ) } );
    itemCompactness->setFlags( itemCompactness->flags() | Qt::ItemIsUserCheckable );
    itemCompactness->setCheckState( 0, Qt::Checked );
    itemCompactness->setData( 0, Qt::UserRole, "compactness" );

    auto *itemRect = new QTreeWidgetItem( shapeRoot, { tr( "Rectangularity (矩形度)" ) } );
    itemRect->setFlags( itemRect->flags() | Qt::ItemIsUserCheckable );
    itemRect->setCheckState( 0, Qt::Checked );
    itemRect->setData( 0, Qt::UserRole, "rectangularity" );

    auto *itemAspect = new QTreeWidgetItem( shapeRoot, { tr( "Aspect Ratio (长宽比)" ) } );
    itemAspect->setFlags( itemAspect->flags() | Qt::ItemIsUserCheckable );
    itemAspect->setCheckState( 0, Qt::Checked );
    itemAspect->setData( 0, Qt::UserRole, "aspectRatio" );

    mFeatureTree->expandAll();
    mFeatureDock->setWidget( mFeatureTree );
    addDockWidget( Qt::LeftDockWidgetArea, mFeatureDock );
}

void RsObiaMainWindow::setupMapCanvas()
{
    // Wave E: store + tree + bridge (local until ProjectContext registers a view).
    m_sessionMap = new RsSessionMapWorkspace( mCanvas, this );

    mCanvas->setLayers( {} );
    mCanvas->setDestinationCrs( QgsCoordinateReferenceSystem() );
    mCanvas->refresh();
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

bool RsObiaMainWindow::loadRasterFile( const QString &path )
{
    if ( path.isEmpty() )
        return false;

    SICNU_LOG_INFO( SicnuLogTags::OBIA, QString( "Loading raster: %1" ).arg( path ) );

    auto *layer = new QgsRasterLayer( path, QFileInfo( path ).baseName() );
    if ( !layer->isValid() )
    {
        SICNU_LOG_ERROR( SicnuLogTags::OBIA, QString( "Invalid raster: %1" ).arg( path ) );
        QMessageBox::warning( this, tr( "Error" ), tr( "Cannot open raster: %1" ).arg( path ) );
        delete layer;
        return false;
    }

    // Safety-first: warn that existing segmentation/classification results will be lost.
    const bool hasPriorWork = mRasterLayer || mClassifiedLayer
                              || !mSegMap.isEmpty() || mHasHierarchy;
    if ( hasPriorWork )
    {
        const auto choice = QMessageBox::question(
            this, tr( "OBIA" ),
            tr( "加载新影像将清除当前分割与分类结果，是否继续？" ),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
        if ( choice != QMessageBox::Yes )
        {
            delete layer;
            return false;
        }
    }

    // Clear previous session display layers (store takes ownership after add).
    if ( m_sessionMap )
    {
        if ( mClassifiedLayer )
        {
            m_sessionMap->removeLayer( mClassifiedLayer );
            delete mClassifiedLayer;
            mClassifiedLayer = nullptr;
        }
        if ( mRasterLayer )
        {
            m_sessionMap->removeLayer( mRasterLayer );
            delete mRasterLayer;
            mRasterLayer = nullptr;
        }
        m_sessionMap->addLayer( layer, /*insertOnTop=*/false );
    }
    else
    {
        delete layer;
        return false;
    }

    mRasterLayer = layer;
    mRasterPath = path;
    mBandCount = layer->bandCount();
    m_sessionMap->zoomToLayer( layer );

    // Reset segmentation / hierarchy
    mSegMap = RsSegmentMap();
    mSegStats.clear();
    mSegmentLabels.clear();
    mHierarchy.clear();
    m_pendingHierarchy.clear();
    mHierarchyStats.clear();
    mHasHierarchy = false;
    mActiveLevel = 0;
    mClassifyLevel = 0;
    mLastClassRasterPath.clear();
    m_segLabelsPath.clear();
    m_hierarchyFinePath.clear();
    m_hierarchyCoarsePath.clear();
    m_hierarchyParentsPath.clear();
    if ( auto *ls = findChild<QSpinBox *>( "levelSpin" ) )
        ls->setValue( 0 );
    if ( auto *cs = findChild<QSpinBox *>( "classifyLevelSpin" ) )
        cs->setValue( 0 );
    updateSegmentTable();
    updateStatusLabel();

    statusBar()->showMessage( tr( "Loaded: %1 (%2 bands)" ).arg( path ).arg( mBandCount ), 5000 );
    return true;
}

void RsObiaMainWindow::loadRaster()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr( "Open Raster" ), QString(),
        tr( "Raster files (*.tif *.tiff *.img *.jp2 *.png);;All files (*)" ) );

    loadRasterFile( path );
}

void RsObiaMainWindow::runSegmentation()
{
    if ( mRasterPath.isEmpty() )
    {
        SICNU_LOG_WARN( SicnuLogTags::OBIA, "Segmentation requested with no raster loaded" );
        QMessageBox::information( this, tr( "OBIA" ), tr( "Load a raster first." ) );
        return;
    }

    SICNU_LOG_INFO( SicnuLogTags::OBIA, QString( "Starting segmentation: %1" ).arg( mRasterPath ) );

    SegmentOptions opts;
    opts.rasterPath = mRasterPath;
    opts.engine = QStringLiteral( "auto" ); // ADR 0058 policy, owned by the operator
    auto *kernelSpin = findChild<QSpinBox *>( "kernelSpin" );
    auto *binsSpin = findChild<QSpinBox *>( "binsSpin" );
    auto *rangeSpin = findChild<QDoubleSpinBox *>( "rangeSpin" );
    auto *minRegionSpin = findChild<QSpinBox *>( "minRegionSpin" );
    // QSpinBox singleStep does not constrain keyboard entry: snap an even
    // kernel to the odd value below it (the operator rejects even kernels,
    // and even engine=auto runs may take the teaching path).
    int smoothKernel = kernelSpin ? kernelSpin->value() : 5;
    if ( smoothKernel % 2 == 0 )
        smoothKernel -= 1;
    opts.smoothKernel = smoothKernel;
    opts.quantizeBins = binsSpin ? binsSpin->value() : opts.quantizeBins;
    opts.minRegionSize = minRegionSpin ? minRegionSpin->value() : opts.minRegionSize;
    // One spin feeds two engine params (existing UX): smoothKernel (odd) for
    // the teaching engine, spatialRadius for OTB — both default to 5.
    opts.spatialRadius = kernelSpin ? kernelSpin->value() : opts.spatialRadius;
    opts.rangeRadius = rangeSpin ? rangeSpin->value() : opts.rangeRadius;

    if ( startSegmentationTask( opts ) < 0 )
    {
        QMessageBox::information( this, tr( "OBIA" ),
                                  tr( "An OBIA task is already running." ) );
    }
}

long RsObiaMainWindow::startSegmentationTask( const SegmentOptions &opts )
{
    if ( isBusy() )
        return -1;

    m_segLabelsPath = scratchPath( QStringLiteral( "seg_labels.tif" ) );
    Json::Value params = RsObiaOperatorAdapter::buildSegmentParams( opts );
    params["output"] = m_segLabelsPath.toStdString();

    auto *progress = new QProgressDialog(
        tr( "Segmenting (rs:obia_segment, engine=%1)..." ).arg( opts.engine ),
        tr( "Cancel" ), 0, 0, this );
    progress->setWindowModality( Qt::WindowModal );
    progress->setMinimumDuration( 0 );
    progress->setValue( 0 );
    progress->show();

    QGuiApplication::setOverrideCursor( Qt::WaitCursor );
    statusBar()->showMessage( progress->labelText() );

    m_pendingOp = PendingOp::Segmentation;
    m_pendingProgress = progress;

    const long taskId = submitOperatorTask(
        QStringLiteral( "rs:obia_segment" ), params, tr( "OBIA segmentation" ) );
    return taskId;
}

long RsObiaMainWindow::submitOperatorTask( const QString &operatorId,
                                           const Json::Value &params,
                                           const QString &title )
{
    sicnu::jobs::JobRequest req;
    req.algorithmId = operatorId.toStdString();
    req.title = title.toStdString();
    req.source = "module";
    req.exclusive = true;
    req.params = params;

    // No executor: JobEngine resolves the real operator from the registry
    // (ADR 0062). autoLoad=false — session outputs load into the session
    // canvas explicitly, never the main project catalog (ADR 0010).
    const long taskId = sicnu::TaskCenter::instance().submitJob(
        req, {}, {}, /*autoLoad=*/false );

    if ( taskId < 0 )
    {
        // Admission failure: release the pending slot so the UI recovers.
        m_pendingOp = PendingOp::None;
        finishPendingUi();
        statusBar()->showMessage( tr( "Task rejected by the Task Center (%1)" ).arg( operatorId ), 5000 );
        return -1;
    }

    m_pendingTaskId = taskId;
    if ( m_pendingProgress )
        watchProgressDialog( m_pendingProgress, taskId );
    pollIfAlreadyTerminal( taskId );
    return taskId;
}

void RsObiaMainWindow::watchProgressDialog( QProgressDialog *progress, long taskId )
{
    connect( progress, &QProgressDialog::canceled, this, [this, taskId]() {
        sicnu::TaskCenter::instance().cancelTask( taskId );
    } );
}

void RsObiaMainWindow::pollIfAlreadyTerminal( long taskId )
{
    const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( info.status == sicnu::TaskStatus::Completed
         || info.status == sicnu::TaskStatus::Failed
         || info.status == sicnu::TaskStatus::Canceled )
    {
        onObiaTaskUpdated( info );
    }
}

QString RsObiaMainWindow::scratchPath( const QString &fileName )
{
    if ( !m_scratch )
        m_scratch = std::make_unique<QTemporaryDir>(
            QDir::temp().absoluteFilePath( QStringLiteral( "obia_session_XXXXXX" ) ) );
    return QDir( m_scratch->path() ).absoluteFilePath( fileName );
}

QString RsObiaMainWindow::levelLabelsPath( int level ) const
{
    if ( mHasHierarchy )
    {
        if ( level <= 0 )
            return m_hierarchyFinePath;
        return m_hierarchyCoarsePath;
    }
    return m_segLabelsPath;
}

void RsObiaMainWindow::finishPendingUi()
{
    QGuiApplication::restoreOverrideCursor();
    if ( m_pendingProgress )
    {
        m_pendingProgress->reset();
        m_pendingProgress->deleteLater();
        m_pendingProgress = nullptr;
    }
    if ( auto *ls = findChild<QSpinBox *>( "levelSpin" ) )
        ls->setEnabled( true );
}

void RsObiaMainWindow::loadClassifiedRaster( const QString &outputPath )
{
    mLastClassRasterPath = outputPath;
    // Session canvas only — do not inject into main QgsProject catalog (ADR 0010).
    // Main map load is explicit via requestLoadToMainMap → loadDataLayer.
    auto *resultLayer = new QgsRasterLayer(
        outputPath, QFileInfo( outputPath ).baseName(), QStringLiteral( "gdal" ) );
    if ( !resultLayer->isValid() )
    {
        delete resultLayer;
        statusBar()->showMessage( tr( "Invalid classification raster: %1" ).arg( outputPath ), 5000 );
        return;
    }
    if ( !m_sessionMap )
    {
        delete resultLayer;
        return;
    }
    if ( mClassifiedLayer )
    {
        m_sessionMap->removeLayer( mClassifiedLayer );
        delete mClassifiedLayer;
        mClassifiedLayer = nullptr;
    }
    m_sessionMap->addLayer( resultLayer, /*insertOnTop=*/true );
    mClassifiedLayer = resultLayer;
}

void RsObiaMainWindow::rememberClassification( const QString &outputPath,
                                               const RsAccuracyAssessment::Result &accuracy )
{
    mLastClassRasterPath = outputPath;
    mLastAccuracy = accuracy;
    mHasAccuracy = !accuracy.classIds.isEmpty();
    emit classificationFinished( outputPath, accuracy );
}

QHash<int, QString> RsObiaMainWindow::classNameMap() const
{
    QHash<int, QString> names;
    for ( const ClassDef &c : mClassDefs )
        names.insert( c.id, c.name );
    return names;
}

void RsObiaMainWindow::showAccuracyAssessment()
{
    if ( !mHasAccuracy )
    {
        QMessageBox::information(
            this, tr( "精度评价" ),
            tr( "尚无精度结果。请先完成对象分类（基于已标注对象计算训练精度）。" ) );
        return;
    }
    auto *dlg = new RsAccuracyDialog( mLastAccuracy, classNameMap(), this );
    dlg->setAttribute( Qt::WA_DeleteOnClose );
    dlg->setWindowTitle( tr( "OBIA 精度评价（训练样本）" ) );
    dlg->show();
}

void RsObiaMainWindow::loadResultToMainMap()
{
    if ( mLastClassRasterPath.isEmpty() || !QFileInfo::exists( mLastClassRasterPath ) )
    {
        QMessageBox::information(
            this, tr( "加载到主图" ),
            tr( "尚无分类结果。请先运行 Classify。" ) );
        return;
    }
    emit requestLoadToMainMap( mLastClassRasterPath );
    statusBar()->showMessage( tr( "已请求将结果加载到主图：%1" ).arg( mLastClassRasterPath ), 4000 );
}

void RsObiaMainWindow::cancelActiveTask()
{
    if ( m_pendingTaskId >= 0 )
    {
        sicnu::TaskCenter::instance().cancelTask( m_pendingTaskId );
        m_pendingTaskId = -1;
        m_pendingOp = PendingOp::None;
        m_pendingSegMap = RsSegmentMap();
        m_pendingSegUsedOtb = false;
        m_pendingHierarchy.clear();
        m_pendingFeaturesCsv.clear();
        m_pendingUncertaintyCsv.clear();
        m_pendingRoiLabels.clear();
        finishPendingUi();
        statusBar()->showMessage( tr( "OBIA 任务已取消" ), 3000 );
    }
}

void RsObiaMainWindow::onObiaTaskUpdated( const sicnu::AlgorithmTaskInfo &info )
{
    if ( info.taskId != m_pendingTaskId || m_pendingTaskId < 0 )
        return;
    if ( info.status != sicnu::TaskStatus::Completed
         && info.status != sicnu::TaskStatus::Failed
         && info.status != sicnu::TaskStatus::Canceled )
        return;

    const PendingOp op = m_pendingOp;
    const Json::Value payload = info.resultPayload;

    m_pendingTaskId = -1;
    m_pendingOp = PendingOp::None;
    if ( info.status != sicnu::TaskStatus::Completed )
    {
        // A canceled or failed chained step invalidates the staged
        // intermediate state it was going to consume/produce.
        m_pendingSegMap = RsSegmentMap();
        m_pendingSegUsedOtb = false;
        m_pendingHierarchy.clear();
        m_pendingFeaturesCsv.clear();
        m_pendingUncertaintyCsv.clear();
    }
    finishPendingUi();

    if ( info.status == sicnu::TaskStatus::Canceled )
    {
        statusBar()->showMessage( tr( "OBIA task canceled" ), 3000 );
        updateStatusLabel();
        return;
    }
    if ( info.status != sicnu::TaskStatus::Completed )
    {
        const QString err = !info.errorMessage.isEmpty()
                              ? info.errorMessage
                              : tr( "Operator task failed (%1)" ).arg( info.algorithmId );
        SICNU_LOG_ERROR( SicnuLogTags::OBIA, err );
        QMessageBox::warning( this, tr( "Error" ), err );
        updateStatusLabel();
        return;
    }

    switch ( op )
    {
        case PendingOp::Segmentation:
        {
            // Rehydrate the segment map from the operator's label raster,
            // then chain feature extraction (rs:obia_features).
            m_pendingSegMap = RsSegmentMap::fromGeoTIFF( m_segLabelsPath );
            if ( m_pendingSegMap.isEmpty() )
            {
                QMessageBox::warning( this, tr( "Error" ),
                                      tr( "Segmentation output is unreadable: %1" ).arg( m_segLabelsPath ) );
                updateStatusLabel();
                return;
            }
            m_pendingSegUsedOtb = payload.get( "engine", "" ).asString() == "otb";
            m_pendingFeaturesLevel = 0;
            startFeaturesTask( -1, /*afterHierarchyBuild=*/false );
            return;
        }

        case PendingOp::SegmentFeatures:
        {
            QMap<quint32, RsSegmentFeatures::SegmentStat> stats;
            QString parseError;
            if ( !RsObiaOperatorAdapter::parseFeaturesCsv( m_pendingFeaturesCsv, stats, &parseError ) )
            {
                QMessageBox::warning( this, tr( "Error" ), parseError );
                updateStatusLabel();
                return;
            }
            applySegmentationResult( m_pendingSegMap, m_pendingSegUsedOtb, std::move( stats ) );
            m_pendingSegMap = RsSegmentMap();
            m_pendingFeaturesCsv.clear();
            return;
        }

        case PendingOp::Hierarchy:
        {
            RsObjectHierarchy hierarchy;
            QString rehydrateError;
            if ( !RsObiaOperatorAdapter::rehydrateHierarchy(
                     m_hierarchyFinePath, m_hierarchyCoarsePath, m_hierarchyParentsPath,
                     hierarchy, &rehydrateError ) )
            {
                QMessageBox::warning( this, tr( "Error" ), rehydrateError );
                updateStatusLabel();
                return;
            }
            m_pendingHierarchy = std::move( hierarchy );
            m_pendingFeaturesLevel = 0;
            if ( startFeaturesTask( 0, /*afterHierarchyBuild=*/true ) < 0 )
            {
                const QString err = tr( "Could not start object-feature extraction on the hierarchy labels (%1)." )
                                        .arg( m_hierarchyFinePath.isEmpty()
                                                  ? tr( "no fine labels raster" )
                                                  : m_hierarchyFinePath );
                SICNU_LOG_ERROR( SicnuLogTags::OBIA, err );
                QMessageBox::warning( this, tr( "Error" ), err );
                m_pendingHierarchy.clear();
                updateStatusLabel();
            }
            return;
        }

        case PendingOp::HierarchyFeatures:
        {
            QMap<quint32, RsSegmentFeatures::SegmentStat> stats;
            QString parseError;
            if ( !RsObiaOperatorAdapter::parseFeaturesCsv( m_pendingFeaturesCsv, stats, &parseError ) )
            {
                QMessageBox::warning( this, tr( "Error" ), parseError );
                updateStatusLabel();
                return;
            }
            const int level = m_pendingFeaturesLevel;
            const bool afterBuild = !m_pendingHierarchy.isEmpty();
            m_pendingFeaturesCsv.clear();
            if ( afterBuild )
            {
                applyHierarchyResult( std::move( m_pendingHierarchy ), 0, std::move( stats ) );
                m_pendingHierarchy.clear();
            }
            else
            {
                applyLevelFeaturesResult( level, std::move( stats ) );
            }
            return;
        }

        case PendingOp::FlatClassify:
        case PendingOp::HierarchyClassify:
        {
            const char *outputKey = op == PendingOp::HierarchyClassify ? "outputClass" : "output";
            const QString outputPath = QString::fromStdString( payload.get( outputKey, "" ).asString() );
            if ( outputPath.isEmpty() || !QFileInfo::exists( outputPath ) )
            {
                QMessageBox::warning( this, tr( "Error" ),
                                      tr( "Classification produced no output raster." ) );
                return;
            }

            RsAccuracyAssessment::Result accuracy;
            RsObiaOperatorAdapter::parseAccuracyJson( payload["accuracy"], accuracy );

            QMap<quint32, double> uncertainties;
            QMap<quint32, int> segmentClasses;
            if ( !m_pendingUncertaintyCsv.isEmpty() )
            {
                RsObiaOperatorAdapter::parseUncertaintyCsv( m_pendingUncertaintyCsv,
                                                            uncertainties, segmentClasses );
            }
            else
            {
                statusBar()->showMessage( tr( "No uncertainty sidecar (unsupervised method)" ), 4000 );
            }
            populateUncertaintyTable( uncertainties, segmentClasses );
            m_pendingUncertaintyCsv.clear();

            rememberClassification( outputPath, accuracy );
            loadClassifiedRaster( outputPath );

            const QString accLine = mHasAccuracy
                                      ? tr( "\nOA=%1  Kappa=%2 (训练样本)" )
                                            .arg( mLastAccuracy.overallAccuracy, 0, 'f', 3 )
                                            .arg( mLastAccuracy.kappa, 0, 'f', 3 )
                                      : QString();
            QMessageBox box( this );
            box.setIcon( QMessageBox::Information );
            box.setWindowTitle( tr( "OBIA Classification" ) );
            if ( op == PendingOp::HierarchyClassify )
                box.setText( tr( "层级 %1 分类完成！\n输出：%2%3" )
                                 .arg( payload.get( "classifyLevel", 0 ).asInt() )
                                 .arg( outputPath )
                                 .arg( accLine ) );
            else
                box.setText( tr( "对象分类完成！\n输出：%1%2" ).arg( outputPath ).arg( accLine ) );
            auto *accBtn = box.addButton( tr( "精度评价" ), QMessageBox::ActionRole );
            auto *mainBtn = box.addButton( tr( "加载到主图" ), QMessageBox::ActionRole );
            box.addButton( QMessageBox::Ok );
            box.exec();
            if ( box.clickedButton() == accBtn )
                showAccuracyAssessment();
            else if ( box.clickedButton() == mainBtn )
                loadResultToMainMap();
            return;
        }

        case PendingOp::LabelImport:
        {
            QMap<quint32, int> imported;
            QString parseError;
            if ( !RsObiaOperatorAdapter::parseSegmentClassesCsv( m_pendingFeaturesCsv, imported, &parseError ) )
            {
                QMessageBox::warning( this, tr( "Import ROI" ), parseError );
                return;
            }
            m_pendingFeaturesCsv.clear();

            if ( imported.isEmpty() )
            {
                SICNU_LOG_WARN( SicnuLogTags::OBIA,
                                QStringLiteral( "rs:obia_label produced no labels (CRS overlap? class field?)" ) );
                statusBar()->showMessage(
                    tr( "ROI 标注完成，但没有对象获得标签（请检查 CRS 覆盖与类别字段）。" ), 6000 );
                updateStatusLabel();
                return;
            }
            int overwritten = 0;
            int newlyLabeled = 0;
            for ( auto it = imported.constBegin(); it != imported.constEnd(); ++it )
            {
                if ( mSegmentLabels.contains( it.key() ) && mSegmentLabels.value( it.key() ) != it.value() )
                    ++overwritten;
                else if ( !mSegmentLabels.contains( it.key() ) )
                    ++newlyLabeled;
                mSegmentLabels[it.key()] = it.value(); // last write wins
            }
            updateSegmentTable();
            updateStatusLabel();
            statusBar()->showMessage(
                tr( "ROI majority (rs:obia_label): +%1 new, %2 overwritten (last write wins)" )
                    .arg( newlyLabeled )
                    .arg( overwritten ),
                6000 );
            return;
        }

        case PendingOp::Export:
        {
            const QString vecOut = QString::fromStdString( payload.get( "output", "" ).asString() );
            QMessageBox::information(
                this, tr( "Export" ),
                tr( "Class raster: %1\nPolygons: %2" ).arg( mLastClassRasterPath ).arg( vecOut ) );
            return;
        }

        case PendingOp::None:
            return;
    }
}

void RsObiaMainWindow::applySegmentationResult(
    const RsSegmentMap &segMap,
    bool usedOtb,
    QMap<quint32, RsSegmentFeatures::SegmentStat> stats )
{
    mSegMap = segMap;
    mSegStats = std::move( stats );
    mSegmentLabels.clear();

    if ( !mSelectTool )
    {
        mSelectTool = new RsSegmentSelectTool( mCanvas );
        connect( mSelectTool, &RsSegmentSelectTool::segmentSelected,
                 this, &RsObiaMainWindow::onSegmentSelected );
        connect( mSelectTool, &RsSegmentSelectTool::selectionCleared,
                 this, &RsObiaMainWindow::onSelectionCleared );
    }
    mSelectTool->setSegmentMap( mSegMap );

    double gt[6] = { 0, 1, 0, 0, 0, 1 };
    // #655: read the geotransform once here (the rasterize loop used to
    // re-open the dataset PER FEATURE just to copy the projection).
    GDALDatasetH rds = GDALOpen( mRasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( rds )
    {
        GDALGetGeoTransform( rds, gt );
        GDALClose( rds );
    }
    mSelectTool->setGeoTransform( gt );
    mCanvas->setMapTool( mSelectTool );

    updateSegmentTable();
    updateStatusLabel();

    mHasHierarchy = false;
    mHierarchy.clear();

    const QString method = usedOtb ? tr( "OTB MeanShift" ) : tr( "built-in segmenter" );
    statusBar()->showMessage(
        tr( "Segmentation complete (%1): %2 segments" ).arg( method ).arg( mSegMap.segmentCount() ), 5000 );

    QMessageBox::information( this, tr( "Segmentation" ),
                              tr( "Segmentation complete using %1: %2 segments" )
                                  .arg( method )
                                  .arg( mSegMap.segmentCount() ) );
}

QVector<int> RsObiaMainWindow::allBandIndices() const
{
    QVector<int> bandIndices;
    for ( int b = 1; b <= mBandCount; ++b )
        bandIndices.append( b );
    return bandIndices;
}

int RsObiaMainWindow::currentClassifyLevel() const
{
    if ( auto *cs = findChild<QSpinBox *>( "classifyLevelSpin" ) )
        return cs->value();
    return mClassifyLevel;
}

void RsObiaMainWindow::setActiveLevelMap( int level )
{
    if ( !mHasHierarchy || level < 0 || level >= mHierarchy.levelCount() )
        return;

    // If we have cached stats, apply immediately without blocking
    if ( mHierarchyStats.contains( level ) )
    {
        mActiveLevel = level;
        mSegMap = mHierarchy.level( level );
        mSegStats = mHierarchyStats.value( level );
        if ( mSelectTool )
            mSelectTool->setSegmentMap( mSegMap );
        updateSegmentTable();
        updateStatusLabel();
        return;
    }

    // Not cached: switch geometry immediately and dispatch async feature extraction
    mActiveLevel = level;
    mSegMap = mHierarchy.level( level );
    if ( mSelectTool )
        mSelectTool->setSegmentMap( mSegMap );
    updateSegmentTable();
    updateStatusLabel();
    // Async path — reuse TaskCenter, keep UI responsive. Cancellation reuses isBusy guard.
    if ( isBusy() )
    {
        // Nothing is queued here (no job is submitted): the user must re-select
        // this level once the current task finishes — keep the message honest.
        statusBar()->showMessage( tr( "当前任务结束后请重新选择层级 %1 以提取特征。" ).arg( level ), 4000 );
        return;
    }
    startFeaturesTask( level, /*afterHierarchyBuild=*/false );
}

long RsObiaMainWindow::startFeaturesTask( int level, bool afterHierarchyBuild )
{
    if ( isBusy() )
        return -1;
    // level < 0 = flat segmentation labels. The hierarchy-build chain
    // calls this with afterHierarchyBuild=true before applyHierarchyResult
    // sets mHasHierarchy, so levelLabelsPath(0) would still return the
    // (possibly empty) flat seg_labels path — use the fine raster directly.
    const QString labelsPath = afterHierarchyBuild
                                   ? m_hierarchyFinePath
                                   : ( level < 0 ? m_segLabelsPath : levelLabelsPath( level ) );
    if ( labelsPath.isEmpty() || mRasterPath.isEmpty() )
        return -1;

    m_pendingFeaturesCsv = scratchPath( QStringLiteral( "features_L%1.csv" ).arg( qMax( level, 0 ) ) );
    m_pendingFeaturesLevel = qMax( level, 0 );
    m_pendingOp = afterHierarchyBuild || level >= 0 ? PendingOp::HierarchyFeatures
                                                    : PendingOp::SegmentFeatures;

    auto *progress = new QProgressDialog(
        tr( "Extracting object features (rs:obia_features)…" ), tr( "Cancel" ), 0, 0, this );
    progress->setWindowModality( Qt::WindowModal );
    progress->setMinimumDuration( 0 );
    progress->show();
    QGuiApplication::setOverrideCursor( Qt::WaitCursor );
    statusBar()->showMessage( progress->labelText() );

    if ( afterHierarchyBuild )
    {
        if ( auto *ls = findChild<QSpinBox *>( "levelSpin" ) )
            ls->setEnabled( false );
    }

    const Json::Value params = RsObiaOperatorAdapter::buildFeaturesParams(
        mRasterPath, labelsPath, m_pendingFeaturesCsv );
    return submitOperatorTask( QStringLiteral( "rs:obia_features" ), params,
                               tr( "OBIA object features" ) );
}

void RsObiaMainWindow::applyLevelFeaturesResult( int level, QMap<quint32, RsSegmentFeatures::SegmentStat> stats )
{
    if ( !mHasHierarchy || level < 0 || level >= mHierarchy.levelCount() )
        return;
    mHierarchyStats[level] = stats;
    if ( mActiveLevel == level )
    {
        mSegStats = stats;
        updateSegmentTable();
        updateStatusLabel();
    }
    if ( auto *ls = findChild<QSpinBox *>( "levelSpin" ) )
        ls->setEnabled( true );
    statusBar()->showMessage( tr( "Level %1 features ready (%2 segments)" ).arg( level ).arg( stats.size() ), 4000 );
}

void RsObiaMainWindow::applyHierarchyResult( RsObjectHierarchy hierarchy,
                                             int activeLevel,
                                             QMap<quint32, RsSegmentFeatures::SegmentStat> stats )
{
    mHierarchy = std::move( hierarchy );
    mHierarchyStats.clear();
    mHierarchyStats[activeLevel] = stats;
    mHasHierarchy = true;
    mActiveLevel = activeLevel;
    mClassifyLevel = 0;
    mSegMap = mHierarchy.level( activeLevel );
    mSegStats = std::move( stats );
    mSegmentLabels.clear();

    if ( auto *ls = findChild<QSpinBox *>( "levelSpin" ) )
    {
        ls->setMaximum( (std::max)( 0, mHierarchy.levelCount() - 1 ) );
        ls->setValue( activeLevel );
        ls->setEnabled( true );
    }
    if ( auto *cs = findChild<QSpinBox *>( "classifyLevelSpin" ) )
    {
        cs->setMaximum( (std::max)( 0, mHierarchy.levelCount() - 1 ) );
        cs->setValue( 0 );
    }

    if ( !mSelectTool )
    {
        mSelectTool = new RsSegmentSelectTool( mCanvas );
        connect( mSelectTool, &RsSegmentSelectTool::segmentSelected,
                 this, &RsObiaMainWindow::onSegmentSelected );
        connect( mSelectTool, &RsSegmentSelectTool::selectionCleared,
                 this, &RsObiaMainWindow::onSelectionCleared );
    }
    mSelectTool->setSegmentMap( mSegMap );

    double gt[6] = { 0, 1, 0, 0, 0, 1 };
    // #655: read the geotransform once here (the rasterize loop used to
    // re-open the dataset PER FEATURE just to copy the projection).
    GDALDatasetH rds = GDALOpen( mRasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( rds )
    {
        GDALGetGeoTransform( rds, gt );
        GDALClose( rds );
    }
    mSelectTool->setGeoTransform( gt );
    mCanvas->setMapTool( mSelectTool );

    updateSegmentTable();
    updateStatusLabel();

    statusBar()->showMessage(
        tr( "Hierarchy ready: %1 levels | viewing L%2 (%3 objects)" )
            .arg( mHierarchy.levelCount() )
            .arg( mActiveLevel )
            .arg( mSegMap.segmentCount() ),
        5000 );

    QMessageBox::information(
        this, tr( "Hierarchical Segmentation" ),
        tr( "Two-level hierarchy built.\nLevel 0 (fine): %1 objects\nLevel 1 (coarse): %2 objects\n"
            "Select an object to inspect parent / childCount / areaRatio.\n"
            "Segment ids are local per level." )
            .arg( mHierarchy.level( 0 ).segmentCount() )
            .arg( mHierarchy.levelCount() > 1 ? mHierarchy.level( 1 ).segmentCount() : 0 ) );
}

void RsObiaMainWindow::runHierarchicalSegmentation()
{
    if ( mRasterPath.isEmpty() )
    {
        QMessageBox::information( this, tr( "OBIA" ), tr( "Load a raster first." ) );
        return;
    }

    if ( ToolPathManager::instance().otbToolPath( QStringLiteral( "Segmentation" ) ).isEmpty() )
    {
        QMessageBox::warning(
            this, tr( "OTB required" ),
            tr( "Hierarchical OBIA requires OTB Segmentation CLI.\n"
                "Set SICNU_OTB_PATH or install OTB.\n"
                "No silent teaching fallback for the hierarchy path." ) );
        return;
    }

    auto *kernelSpin = findChild<QSpinBox *>( "kernelSpin" );
    auto *rangeSpin = findChild<QDoubleSpinBox *>( "rangeSpin" );
    auto *minRegionSpin = findChild<QSpinBox *>( "minRegionSpin" );
    const int spatialRadius = kernelSpin ? kernelSpin->value() : 5;
    const double rangeRadius = rangeSpin ? rangeSpin->value() : 15.0;
    const int minRegionSize = minRegionSpin ? minRegionSpin->value() : 100;

    if ( startHierarchyTask( spatialRadius, rangeRadius, minRegionSize ) < 0 )
    {
        QMessageBox::information( this, tr( "OBIA" ),
                                  tr( "An OBIA task is already running." ) );
    }
}

long RsObiaMainWindow::startHierarchyTask( int spatialRadius, double rangeRadius, int minRegionSize,
                                           double watershedThreshold )
{
    if ( isBusy() || mRasterPath.isEmpty() )
        return -1;

    m_hierarchyFinePath = scratchPath( QStringLiteral( "hier_fine.tif" ) );
    m_hierarchyCoarsePath = scratchPath( QStringLiteral( "hier_coarse.tif" ) );
    m_hierarchyParentsPath = scratchPath( QStringLiteral( "hier_parents.csv" ) );

    auto *progress = new QProgressDialog(
        tr( "Building 2-level hierarchy (rs:obia_hierarchy: MeanShift + Watershed)..." ),
        tr( "Cancel" ), 0, 0, this );
    progress->setWindowModality( Qt::WindowModal );
    progress->setMinimumDuration( 0 );
    progress->show();
    QGuiApplication::setOverrideCursor( Qt::WaitCursor );
    statusBar()->showMessage( progress->labelText() );

    m_pendingOp = PendingOp::Hierarchy;
    m_pendingProgress = progress;

    const Json::Value params = RsObiaOperatorAdapter::buildHierarchyBuildParams(
        mRasterPath, m_hierarchyFinePath, m_hierarchyCoarsePath, m_hierarchyParentsPath,
        spatialRadius, rangeRadius, minRegionSize, watershedThreshold );
    return submitOperatorTask( QStringLiteral( "rs:obia_hierarchy" ), params,
                               tr( "OBIA hierarchical segment" ) );
}

void RsObiaMainWindow::onActiveLevelChanged( int level )
{
    if ( !mHasHierarchy )
        return;
    if ( level == mActiveLevel )
        return;
    setActiveLevelMap( level );
    statusBar()->showMessage( tr( "Viewing level %1 (%2 objects, ids are level-local)" )
                                  .arg( level )
                                  .arg( mSegMap.segmentCount() ),
                              4000 );
}

void RsObiaMainWindow::onClassifyLevelChanged( int level )
{
    mClassifyLevel = level;
    // Training labels are per classify-level object ids — clear on level switch to avoid id confusion.
    mSegmentLabels.clear();
    updateSegmentTable();
    statusBar()->showMessage(
        tr( "Classify level set to %1 — labels cleared (ids are level-local)" ).arg( level ), 4000 );
}

RsObiaOperatorAdapter::ClassifierOptions RsObiaMainWindow::classifierOptions() const
{
    RsObiaOperatorAdapter::ClassifierOptions opts;
    auto *combo = findChild<QComboBox *>( "classifierCombo" );
    const QString label = combo ? combo->currentText() : QStringLiteral( "NormalBayes" );
    opts.method = RsObiaOperatorAdapter::methodForClassifierLabel( label );
    opts.rfNumTrees = mRfNumTrees;
    opts.rfMaxDepth = mRfMaxDepth;
    opts.rfMinSampleCount = mRfMinSampleCount;
    opts.mlpHiddenLayerSize = mMlpHiddenLayerSize;
    opts.mlpMaxIter = mMlpMaxIter;
    return opts;
}

void RsObiaMainWindow::runClassification()
{
    if ( mSegMap.isEmpty() )
    {
        SICNU_LOG_WARN( SicnuLogTags::OBIA, "Classification requested without segmentation" );
        QMessageBox::information( this, tr( "OBIA" ), tr( "Run segmentation or hierarchy first." ) );
        return;
    }

    if ( mSegmentLabels.size() < 2 )
    {
        QMessageBox::information( this, tr( "OBIA" ),
                                  tr( "Label at least 2 segments (click assign and/or Import ROI)." ) );
        return;
    }

    if ( isBusy() )
    {
        QMessageBox::information( this, tr( "OBIA" ),
                                  tr( "An OBIA task is already running." ) );
        return;
    }

    const RsObiaOperatorAdapter::ClassifierOptions classifier = classifierOptions();
    QHash<int, QColor> classColors;
    for ( const auto &cd : mClassDefs )
        classColors[cd.id] = cd.color;

    const QString defaultOut = QFileInfo( mRasterPath ).path() + QStringLiteral( "/obia_classified.tif" );
    const QString outputPath = QFileDialog::getSaveFileName(
        this, tr( "Save Classified Raster" ), defaultOut,
        tr( "GeoTIFF (*.tif *.tiff);;All files (*)" ) );
    if ( outputPath.isEmpty() )
        return;

    m_pendingUncertaintyCsv = scratchPath( QStringLiteral( "uncertainty.csv" ) );
    const int clsLevel = currentClassifyLevel();

    Json::Value params;
    QString operatorId;
    if ( mHasHierarchy && mHierarchy.levelCount() > 0 )
    {
        if ( clsLevel < 0 || clsLevel >= mHierarchy.levelCount() )
        {
            QMessageBox::warning( this, tr( "Error" ), tr( "Invalid classify level." ) );
            return;
        }
        operatorId = QStringLiteral( "rs:obia_hierarchy" );
        params = RsObiaOperatorAdapter::buildHierarchyClassifyParams(
            mRasterPath, m_hierarchyFinePath, m_hierarchyCoarsePath, m_hierarchyParentsPath,
            outputPath, clsLevel, mSegmentLabels, classifier, classColors,
            m_pendingUncertaintyCsv );
        m_pendingOp = PendingOp::HierarchyClassify;
    }
    else
    {
        if ( m_segLabelsPath.isEmpty() )
        {
            QMessageBox::warning( this, tr( "Error" ),
                                  tr( "No segment label raster in this session (re-run Segment)." ) );
            return;
        }
        operatorId = QStringLiteral( "rs:obia_classify" );
        params = RsObiaOperatorAdapter::buildFlatClassifyParams(
            mRasterPath, m_segLabelsPath, outputPath, mSegmentLabels, classifier,
            featureSelection(), classColors, m_pendingUncertaintyCsv );
        m_pendingOp = PendingOp::FlatClassify;
    }

    statusBar()->showMessage( tr( "Classifying objects (%1)..." ).arg( operatorId ) );
    QGuiApplication::setOverrideCursor( Qt::WaitCursor );

    auto *progress = new QProgressDialog(
        tr( "Classifying objects (%1)..." ).arg( operatorId ), tr( "Cancel" ), 0, 0, this );
    progress->setWindowModality( Qt::WindowModal );
    progress->setMinimumDuration( 0 );
    progress->show();
    m_pendingProgress = progress;

    submitOperatorTask( operatorId, params,
                        tr( "OBIA classify: %1" ).arg( classifier.method ) );
}

void RsObiaMainWindow::importRoiLabels()
{
    if ( mSegMap.isEmpty() )
    {
        QMessageBox::information( this, tr( "Import ROI" ), tr( "Run segmentation or hierarchy first." ) );
        return;
    }
    if ( isBusy() )
    {
        QMessageBox::information( this, tr( "Import ROI" ), tr( "An OBIA task is already running." ) );
        return;
    }

    const int clsLevel = mHasHierarchy ? currentClassifyLevel() : 0;
    const QString labelsPath = levelLabelsPath( clsLevel );
    if ( labelsPath.isEmpty() || !QFileInfo::exists( labelsPath ) )
    {
        QMessageBox::warning( this, tr( "Import ROI" ),
                              tr( "No segment label raster for level %1 (re-run segmentation)." ).arg( clsLevel ) );
        return;
    }

    // When viewing a different level than classify level, switch view for
    // assign feedback BEFORE anything is submitted: the switch itself may
    // dispatch a level-features task, and the single-flight gate must never
    // be overwritten here (single submit per gate).
    if ( mHasHierarchy && mActiveLevel != clsLevel )
    {
        setActiveLevelMap( clsLevel );
        if ( isBusy() )
        {
            QMessageBox::information(
                this, tr( "Import ROI" ),
                tr( "Level %1 特征仍在提取，完成后请重新执行 Import ROI。" ).arg( clsLevel ) );
            return;
        }
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr( "Open training polygons" ), QString(),
        tr( "Vector (*.shp *.gpkg *.geojson);;All files (*)" ) );
    if ( path.isEmpty() )
        return;

    // Safety-first: warn that existing segment labels will be replaced.
    if ( !mSegmentLabels.isEmpty() )
    {
        const auto choice = QMessageBox::question(
            this, tr( "Import ROI" ),
            tr( "导入新 ROI 将替换当前 %1 个已赋标签的对象，是否继续？" ).arg( mSegmentLabels.size() ),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
        if ( choice != QMessageBox::Yes )
            return;
    }

    m_pendingFeaturesCsv = scratchPath( QStringLiteral( "roi_labels.csv" ) );
    const Json::Value params = RsObiaOperatorAdapter::buildLabelParams(
        mRasterPath, labelsPath, path, /*classField=*/QString(),
        /*minLabelPixels=*/3 /* operator default, ADR 0060 */ );
    m_pendingOp = PendingOp::LabelImport;

    auto *progress = new QProgressDialog(
        tr( "Labeling objects from ROI (rs:obia_label)…" ), tr( "Cancel" ), 0, 0, this );
    progress->setWindowModality( Qt::WindowModal );
    progress->setMinimumDuration( 0 );
    progress->show();
    QGuiApplication::setOverrideCursor( Qt::WaitCursor );
    statusBar()->showMessage( progress->labelText() );

    submitOperatorTask( QStringLiteral( "rs:obia_label" ), params, tr( "OBIA ROI labeling" ) );
}

void RsObiaMainWindow::exportResult()
{
    if ( mLastClassRasterPath.isEmpty() || !QFileInfo::exists( mLastClassRasterPath ) )
    {
        QMessageBox::information(
            this, tr( "Export" ),
            tr( "Run Classify first to write a class raster, then Export can polygonize it." ) );
        return;
    }
    if ( isBusy() )
    {
        QMessageBox::information( this, tr( "Export" ), tr( "An OBIA task is already running." ) );
        return;
    }

    const QString vecOut = QFileDialog::getSaveFileName(
        this, tr( "Export class polygons" ),
        QFileInfo( mLastClassRasterPath ).path() + QStringLiteral( "/obia_classes.shp" ),
        tr( "Shapefile (*.shp);;All files (*)" ) );
    if ( vecOut.isEmpty() )
        return;

    const Json::Value params = RsObiaOperatorAdapter::buildPolygonizeParams(
        mLastClassRasterPath, vecOut );
    m_pendingOp = PendingOp::Export;
    submitOperatorTask( QStringLiteral( "gdal:polygonize" ), params, tr( "OBIA export polygons" ) );
}

void RsObiaMainWindow::onSegmentSelected( quint32 segmentId )
{
    auto it = mSegStats.find( segmentId );
    if ( it != mSegStats.end() )
    {
        const int classId = ( mActiveLevel == currentClassifyLevel() )
                              ? mSegmentLabels.value( segmentId, 0 )
                              : 0;
        if ( mHasHierarchy )
        {
            const quint32 parentId = mHierarchy.parentOf( mActiveLevel, segmentId );
            const int childCount = mHierarchy.childCount( mActiveLevel, segmentId );
            const double areaRatio = mHierarchy.areaRatioToParent( mActiveLevel, segmentId );
            mInfoDock->showSegmentInfo( segmentId, it.value(), classId,
                                        mActiveLevel, parentId, childCount, areaRatio );
        }
        else
        {
            mInfoDock->showSegmentInfo( segmentId, it.value(), classId );
        }
    }

    statusBar()->showMessage( tr( "Selected L%1 segment %2 (pixels: %3)" )
                                  .arg( mActiveLevel )
                                  .arg( segmentId )
                                  .arg( mSegMap.pixelCount( segmentId ) ) );
}

void RsObiaMainWindow::onSelectionCleared()
{
    mInfoDock->clearInfo();
    statusBar()->showMessage( tr( "Ready" ) );
}

void RsObiaMainWindow::onAssignClass()
{
    // Click-assign operates on classify level objects
    if ( mHasHierarchy && mActiveLevel != currentClassifyLevel() )
    {
        QMessageBox::information(
            this, tr( "OBIA" ),
            tr( "Switch View L to the classify level (%1) before assigning labels, "
                "or change Cls L to the active view level." )
                .arg( currentClassifyLevel() ) );
        return;
    }

    if ( mSelectTool && mSelectTool->selectedSegmentId() != 0 )
    {
        quint32 segId = mSelectTool->selectedSegmentId();
        const bool overwritten = mSegmentLabels.contains( segId )
                                 && mSegmentLabels.value( segId ) != mCurrentClassId;
        mSegmentLabels[segId] = mCurrentClassId; // last write wins
        // Incremental update avoids full N-row rebuild on 100k+ segments (O2)
        if ( mSegmentTable->rowCount() == static_cast<int>( mSegMap.segmentCount() ) )
            updateSegmentTableRow( segId );
        else
            updateSegmentTable();

        auto it = mSegStats.find( segId );
        if ( it != mSegStats.end() )
        {
            if ( mHasHierarchy )
            {
                mInfoDock->showSegmentInfo(
                    segId, it.value(), mCurrentClassId, mActiveLevel,
                    mHierarchy.parentOf( mActiveLevel, segId ),
                    mHierarchy.childCount( mActiveLevel, segId ),
                    mHierarchy.areaRatioToParent( mActiveLevel, segId ) );
            }
            else
            {
                mInfoDock->showSegmentInfo( segId, it.value(), mCurrentClassId );
            }
        }

        statusBar()->showMessage(
            overwritten
                ? tr( "Segment %1 → Class %2 (overwrote previous label)" ).arg( segId ).arg( mCurrentClassId )
                : tr( "Segment %1 → Class %2" ).arg( segId ).arg( mCurrentClassId ),
            3000 );
    }
    else
    {
        QMessageBox::information( this, tr( "OBIA" ), tr( "Click a segment on the map first." ) );
    }
}

void RsObiaMainWindow::rebuildClassTable()
{
    mClassTable->setRowCount( mClassDefs.size() );
    for ( int i = 0; i < mClassDefs.size(); ++i )
    {
        auto *idItem = new QTableWidgetItem( QString::number( mClassDefs[i].id ) );
        idItem->setFlags( idItem->flags() & ~Qt::ItemIsEditable );
        mClassTable->setItem( i, 0, idItem );
        mClassTable->setItem( i, 1, new QTableWidgetItem( mClassDefs[i].name ) );
        auto *colorItem = new QTableWidgetItem;
        colorItem->setFlags( colorItem->flags() & ~Qt::ItemIsEditable );
        colorItem->setBackground( mClassDefs[i].color );
        mClassTable->setItem( i, 2, colorItem );
    }
}

void RsObiaMainWindow::onClassTableContextMenu( const QPoint &pos )
{
    const int row = mClassTable->rowAt( pos.y() );
    QMenu menu( this );

    QAction *editNameAct = menu.addAction( tr( "编辑名称…" ) );
    editNameAct->setToolTip( tr( "修改该类别的显示名称。" ) );
    editNameAct->setEnabled( row >= 0 && row < mClassDefs.size() );

    QAction *editColorAct = menu.addAction( tr( "更改颜色…" ) );
    editColorAct->setToolTip( tr( "打开调色板选择新的类别颜色。" ) );
    editColorAct->setEnabled( row >= 0 && row < mClassDefs.size() );

    menu.addSeparator();

    QAction *insertAct = menu.addAction( tr( "在此之后插入类别…" ) );
    insertAct->setToolTip( tr( "追加一个新类别（ID 自动取当前最大值 +1）。" ) );
    insertAct->setEnabled( row >= 0 );

    QAction *deleteAct = menu.addAction( tr( "删除类别…" ) );
    deleteAct->setToolTip( tr( "删除该类别（会弹出确认）。已赋该类的对象标签不会自动清除。" ) );
    deleteAct->setEnabled( row >= 0 && row < mClassDefs.size() && mClassDefs.size() > 1 );

    menu.addSeparator();
    QAction *copyIdAct = menu.addAction( tr( "复制类别 ID" ) );
    copyIdAct->setToolTip( tr( "把该类别的 ID 复制到剪贴板。" ) );
    copyIdAct->setEnabled( row >= 0 && row < mClassDefs.size() );

    QAction *chosen = menu.exec( mClassTable->viewport()->mapToGlobal( pos ) );
    if ( !chosen )
        return;

    if ( chosen == editNameAct && row >= 0 && row < mClassDefs.size() )
    {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr( "编辑类别名称" ), tr( "名称：" ), QLineEdit::Normal,
            mClassDefs[row].name, &ok );
        if ( ok && !name.trimmed().isEmpty() )
        {
            mClassDefs[row].name = name.trimmed();
            mClassTable->item( row, 1 )->setText( mClassDefs[row].name );
            statusBar()->showMessage( tr( "类别 %1 已更名为「%2」" )
                                          .arg( mClassDefs[row].id )
                                          .arg( mClassDefs[row].name ), 3000 );
        }
    }
    else if ( chosen == editColorAct && row >= 0 && row < mClassDefs.size() )
    {
        const QColor c = QColorDialog::getColor( mClassDefs[row].color, this, tr( "选择类别颜色" ) );
        if ( c.isValid() )
        {
            mClassDefs[row].color = c;
            mClassTable->item( row, 2 )->setBackground( c );
            statusBar()->showMessage( tr( "类别 %1 颜色已更新" ).arg( mClassDefs[row].id ), 3000 );
        }
    }
    else if ( chosen == insertAct && row >= 0 )
    {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr( "新类别名称" ), tr( "名称：" ), QLineEdit::Normal,
            tr( "New class" ), &ok );
        if ( !ok || name.trimmed().isEmpty() )
            return;
        int maxId = 0;
        for ( const ClassDef &cd : mClassDefs )
            maxId = std::max( maxId, cd.id );
        const int newId = maxId + 1;
        // Insert right after the current row in the definition vector.
        mClassDefs.insert( row + 1, ClassDef{ newId, name.trimmed(), QColor::fromHsv( ( newId * 67 ) % 360, 200, 220 ) } );
        rebuildClassTable();
        statusBar()->showMessage( tr( "已插入类别 %1「%2」" ).arg( newId ).arg( name.trimmed() ), 3000 );
    }
    else if ( chosen == deleteAct && row >= 0 && row < mClassDefs.size() && mClassDefs.size() > 1 )
    {
        const ClassDef cd = mClassDefs[row];
        // Safety-first: default button is No.
        const auto choice = QMessageBox::question(
            this, tr( "删除类别" ),
            tr( "确定删除类别 %1「%2」？\n已赋该类的对象标签不会被自动清除，可重新赋类。" )
                .arg( cd.id )
                .arg( cd.name ),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
        if ( choice != QMessageBox::Yes )
            return;
        mClassDefs.removeAt( row );
        if ( mCurrentClassId == cd.id )
            mCurrentClassId = mClassDefs.isEmpty() ? 0 : mClassDefs.first().id;
        rebuildClassTable();
        statusBar()->showMessage( tr( "已删除类别 %1" ).arg( cd.id ), 3000 );
    }
    else if ( chosen == copyIdAct && row >= 0 && row < mClassDefs.size() )
    {
        if ( QClipboard *cb = QGuiApplication::clipboard() )
            cb->setText( QString::number( mClassDefs[row].id ) );
    }
}

void RsObiaMainWindow::onSegmentTableContextMenu( const QPoint &pos )
{
    const int row = mSegmentTable->rowAt( pos.y() );
    QMenu menu( this );

    const bool hasData = mSegmentTable->rowCount() > 0;
    const bool canAssign = hasData && row >= 0 && row < mSegmentTable->rowCount();

    QAction *assignAct = menu.addAction( tr( "赋为当前类别" ) );
    assignAct->setToolTip( tr( "把当前选中类别（Classes 表选中行）赋给该对象。" ) );
    assignAct->setEnabled( canAssign );

    QAction *infoAct = menu.addAction( tr( "查看对象信息" ) );
    infoAct->setToolTip( tr( "在下方信息面板显示该对象的形状/光谱/层级统计。" ) );
    infoAct->setEnabled( canAssign );

    menu.addSeparator();
    QAction *copyIdAct = menu.addAction( tr( "复制对象 ID" ) );
    copyIdAct->setToolTip( tr( "把该对象 ID 复制到剪贴板。" ) );
    copyIdAct->setEnabled( canAssign );

    if ( !hasData )
    {
        auto *hint = menu.addAction( tr( "（先完成分割后才有对象可操作）" ) );
        hint->setEnabled( false );
    }

    QAction *chosen = menu.exec( mSegmentTable->viewport()->mapToGlobal( pos ) );
    if ( !chosen || !canAssign )
        return;

    bool conv = false;
    const quint32 segId = mSegmentTable->item( row, 0 )->text().toUInt( &conv );
    if ( !conv )
        return;

    if ( chosen == assignAct )
    {
        // Mirror onAssignClass semantics (level-bound labels).
        if ( mHasHierarchy && mActiveLevel != currentClassifyLevel() )
        {
            QMessageBox::information(
                this, tr( "OBIA" ),
                tr( "切换 View L 到分类层级（%1）后再赋标签，或将 Cls L 设为当前视图层级。" )
                    .arg( currentClassifyLevel() ) );
            return;
        }
        mSegmentLabels[segId] = mCurrentClassId;
        if ( mSegmentTable->rowCount() == static_cast<int>( mSegMap.segmentCount() ) )
            updateSegmentTableRow( segId );
        else
            updateSegmentTable();
        statusBar()->showMessage( tr( "对象 %1 → 类别 %2" ).arg( segId ).arg( mCurrentClassId ), 3000 );
    }
    else if ( chosen == infoAct )
    {
        onSegmentSelected( segId );
    }
    else if ( chosen == copyIdAct )
    {
        if ( QClipboard *cb = QGuiApplication::clipboard() )
            cb->setText( QString::number( segId ) );
    }
}

// ---------------------------------------------------------------------------
// UI updates
// ---------------------------------------------------------------------------

void RsObiaMainWindow::updateSegmentTable()
{
    auto segIds = mSegMap.uniqueLabels().values();
    std::sort( segIds.begin(), segIds.end() );

    mSegmentTable->setUpdatesEnabled( false );
    mSegmentTable->blockSignals( true );
    mSegmentTable->setRowCount( segIds.size() );
    for ( int i = 0; i < segIds.size(); ++i )
    {
        quint32 segId = segIds[i];
        mSegmentTable->setItem( i, 0, new QTableWidgetItem( QString::number( segId ) ) );
        mSegmentTable->setItem( i, 1, new QTableWidgetItem( QString::number( mSegMap.pixelCount( segId ) ) ) );

        // Labels are level-local and bind to classify level only — never show
        // classify-level class against a different view level's segment ids.
        const bool labelsApplyToView =
            !mHasHierarchy || ( mActiveLevel == currentClassifyLevel() );
        QString classText = tr( "-" );
        if ( labelsApplyToView )
        {
            const int classId = mSegmentLabels.value( segId, 0 );
            if ( classId > 0 )
                classText = QString::number( classId );
        }
        mSegmentTable->setItem( i, 2, new QTableWidgetItem( classText ) );
    }
    mSegmentTable->blockSignals( false );
    mSegmentTable->setUpdatesEnabled( true );
}

void RsObiaMainWindow::updateSegmentTableRow( quint32 segId )
{
    if ( !mSegmentTable ) return;
    const bool labelsApplyToView = !mHasHierarchy || ( mActiveLevel == currentClassifyLevel() );
    QString classText = tr( "-" );
    if ( labelsApplyToView )
    {
        const int classId = mSegmentLabels.value( segId, 0 );
        if ( classId > 0 )
            classText = QString::number( classId );
    }
    for ( int r = 0; r < mSegmentTable->rowCount(); ++r )
    {
        auto *idItem = mSegmentTable->item( r, 0 );
        if ( !idItem ) continue;
        bool ok = false;
        quint32 rowId = idItem->text().toUInt( &ok );
        if ( ok && rowId == segId )
        {
            auto *clsItem = mSegmentTable->item( r, 2 );
            if ( clsItem )
                clsItem->setText( classText );
            else
                mSegmentTable->setItem( r, 2, new QTableWidgetItem( classText ) );
            return;
        }
    }
    // Row not found (e.g. new segment after hierarchy switch) — full rebuild fallback
    updateSegmentTable();
}

void RsObiaMainWindow::updateStatusLabel()
{
    if ( !statusBar() )
        return;

    if ( mSegMap.isEmpty() )
        statusBar()->showMessage( tr( "Ready — Load a raster to begin" ) );
    else if ( mHasHierarchy )
        statusBar()->showMessage(
            tr( "Hierarchy L%1/%2 | %3 objects | %4 labeled | classify L%5" )
                .arg( mActiveLevel )
                .arg( mHierarchy.levelCount() - 1 )
                .arg( mSegMap.segmentCount() )
                .arg( mSegmentLabels.size() )
                .arg( currentClassifyLevel() ) );
    else
        statusBar()->showMessage( tr( "%1 segments | %2 labeled" )
                                      .arg( mSegMap.segmentCount() )
                                      .arg( mSegmentLabels.size() ) );
}

void RsObiaMainWindow::showClassifierConfigDialog()
{
    auto *combo = findChild<QComboBox *>( "classifierCombo" );
    QString algoName = combo ? combo->currentText() : QStringLiteral( "RandomForest" );

    bool ok = false;
    if ( algoName.contains( "MLP", Qt::CaseInsensitive ) || algoName.contains( "Neural", Qt::CaseInsensitive ) )
    {
        int hiddenSize = QInputDialog::getInt(
            this, tr( "MLP Config" ), tr( "Hidden Layer Neuron Count (mlpHiddenLayerSize):" ),
            mMlpHiddenLayerSize, 2, 512, 4, &ok );
        if ( ok )
        {
            mMlpHiddenLayerSize = hiddenSize;
            int maxIter = QInputDialog::getInt(
                this, tr( "MLP Config" ), tr( "Maximum Iterations (mlpMaxIter):" ),
                mMlpMaxIter, 10, 10000, 50, &ok );
            if ( ok )
                mMlpMaxIter = maxIter;
        }
    }
    else if ( algoName.contains( "RandomForest", Qt::CaseInsensitive ) || algoName.contains( "RF", Qt::CaseInsensitive ) )
    {
        int numTrees = QInputDialog::getInt(
            this, tr( "RandomForest Config" ), tr( "Number of Decision Trees (rfNumTrees):" ),
            mRfNumTrees, 10, 1000, 10, &ok );
        if ( ok )
        {
            mRfNumTrees = numTrees;
            int maxDepth = QInputDialog::getInt(
                this, tr( "RandomForest Config" ), tr( "Max Tree Depth (rfMaxDepth):" ),
                mRfMaxDepth, 2, 100, 1, &ok );
            if ( ok )
            {
                mRfMaxDepth = maxDepth;
                int minSamples = QInputDialog::getInt(
                    this, tr( "RandomForest Config" ), tr( "Min Sample Count per Node (rfMinSampleCount):" ),
                    mRfMinSampleCount, 1, 100, 1, &ok );
                if ( ok )
                    mRfMinSampleCount = minSamples;
            }
        }
    }
    else
    {
        QMessageBox::information(
            this, tr( "Classifier Config" ),
            tr( "No configurable hyperparameters for selected classifier backend." ) );
    }
}

void RsObiaMainWindow::runHierarchyConsolidation()
{
    if ( !mHasHierarchy || mHierarchy.levelCount() <= 1 )
    {
        QMessageBox::information(
            this, tr( "Hierarchy Consolidation" ),
            tr( "Hierarchy consolidation requires a multi-level RsObjectHierarchy structure." ) );
        return;
    }

    QStringList options = { tr( "Bottom-Up Majority Vote (子级多数票投票决定父级)" ),
                            tr( "Area-Weighted Vote (子级像素面积加权投票决定父级)" ),
                            tr( "Top-Down Inheritance (父级类别直接向下继承)" ) };
    bool ok = false;
    QString choice = QInputDialog::getItem(
        this, tr( "Hierarchy Class Consolidator" ),
        tr( "Select Consolidation Strategy:" ), options, 0, false, &ok );
    if ( !ok )
        return;

    RsConsolidationMode mode = RsConsolidationMode::BottomUpMajorityVote;
    if ( choice.startsWith( tr( "Area-Weighted" ) ) )
        mode = RsConsolidationMode::ProbabilityWeightedVote;
    else if ( choice.startsWith( tr( "Top-Down" ) ) )
        mode = RsConsolidationMode::TopDownInheritance;

    QMap<int, QMap<quint32, int>> levelClasses;
    levelClasses[mClassifyLevel] = mSegmentLabels;

    // Interactive-only map operation (no operator equivalent yet — see ADR
    // 0126 "remaining debt"); operates on session label maps, no raster I/O.
    auto consolidated = RsHierarchyClassConsolidator::consolidate( mHierarchy, levelClasses, mode );
    if ( consolidated.contains( mClassifyLevel ) )
    {
        mSegmentLabels = consolidated[mClassifyLevel];
        updateSegmentTable();
        updateStatusLabel();
        QMessageBox::information(
            this, tr( "Hierarchy Consolidation" ),
            tr( "Consolidation finished successfully. Class labels updated across hierarchy levels." ) );
    }
}

void RsObiaMainWindow::onUncertaintySegmentDoubleClicked( int row, int )
{
    if ( !mUncertaintyTable || row < 0 || row >= mUncertaintyTable->rowCount() )
        return;

    auto *item = mUncertaintyTable->item( row, 0 );
    if ( !item )
        return;

    bool ok = false;
    quint32 segId = item->text().toUInt( &ok );
    if ( ok && segId > 0 )
    {
        onSegmentSelected( segId );
    }
}

RsFeatureSelection RsObiaMainWindow::featureSelection() const
{
    RsFeatureSelection sel;
    if ( !mFeatureTree )
        return sel;

    QList<QTreeWidgetItem *> items;
    for ( int i = 0; i < mFeatureTree->topLevelItemCount(); ++i )
    {
        auto *parent = mFeatureTree->topLevelItem( i );
        for ( int j = 0; j < parent->childCount(); ++j )
            items.append( parent->child( j ) );
    }

    for ( auto *item : items )
    {
        const QString key = item->data( 0, Qt::UserRole ).toString();
        const bool checked = ( item->checkState( 0 ) == Qt::Checked );
        if ( key == "mean" ) sel.useMean = checked;
        else if ( key == "stddev" ) sel.useStdDev = checked;
        else if ( key == "min" ) sel.useMin = checked;
        else if ( key == "max" ) sel.useMax = checked;
        else if ( key == "contrast" ) sel.useGlcmContrast = checked;
        else if ( key == "correlation" ) sel.useGlcmCorrelation = checked;
        else if ( key == "energy" ) sel.useGlcmEnergy = checked;
        else if ( key == "homogeneity" ) sel.useGlcmHomogeneity = checked;
        else if ( key == "area" ) sel.useArea = checked;
        else if ( key == "perimeter" ) sel.usePerimeter = checked;
        else if ( key == "shapeIndex" ) sel.useShapeIndex = checked;
        else if ( key == "compactness" ) sel.useCompactness = checked;
        else if ( key == "rectangularity" ) sel.useRectangularity = checked;
        else if ( key == "aspectRatio" ) sel.useAspectRatio = checked;
    }
    return sel;
}

void RsObiaMainWindow::populateUncertaintyTable( const QMap<quint32, double> &uncertainties,
                                                 const QMap<quint32, int> &classes )
{
    if ( !mUncertaintyTable || !mUncertaintyDock )
        return;

    struct Candidate
    {
        quint32 segId;
        double entropy;
        int predictedClass;
    };
    QList<Candidate> candidates;
    candidates.reserve( uncertainties.size() );
    for ( auto it = uncertainties.constBegin(); it != uncertainties.constEnd(); ++it )
    {
        const quint32 segId = it.key();
        const double entropy = it.value();
        const int predClass = classes.value( segId, 0 );
        candidates.append( { segId, entropy, predClass } );
    }

    std::sort( candidates.begin(), candidates.end(), []( const Candidate &a, const Candidate &b ) {
        return a.entropy > b.entropy;
    } );

    // Cap to top 100 candidates to keep UI responsive on 100k+ segments (O2)
    constexpr int kMaxUncertaintyRows = 100;
    const int rows = std::min<int>( candidates.size(), kMaxUncertaintyRows );
    mUncertaintyTable->setRowCount( rows );
    for ( int i = 0; i < rows; ++i )
    {
        const Candidate &c = candidates[i];
        auto *idItem = new QTableWidgetItem( QString::number( c.segId ) );
        auto *entropyItem = new QTableWidgetItem( QString::number( c.entropy, 'f', 4 ) );
        auto *classItem = new QTableWidgetItem( c.predictedClass > 0 ? QString::number( c.predictedClass ) : tr( "-" ) );

        mUncertaintyTable->setItem( i, 0, idItem );
        mUncertaintyTable->setItem( i, 1, entropyItem );
        mUncertaintyTable->setItem( i, 2, classItem );
    }

    if ( !candidates.isEmpty() )
        mUncertaintyDock->show();
}
