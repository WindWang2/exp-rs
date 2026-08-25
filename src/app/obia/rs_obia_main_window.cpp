// rs_obia_main_window.cpp — Phase 10B Task 10B.5
#include "rs_obia_main_window.h"
#include "dialogs/dialog_help_catalog.h"
#include "sicnu_logging.h"

#include "rs_obia_task.h"
#include "rs_obia_segmentation.h"
#include "rs_classifier_normalbayes.h"
#include "rs_classifier_svm.h"
#include "rs_classifier_kmeans.h"
#include "rs_classifier_random_forest.h"
#include "rs_classifier_mlp.h"
#include "rs_classifier_backend_factory.h"
#include "rs_object_hierarchy.h"
#include "rs_otb_segmenter.h"
#include "rs_parent_link.h"
#include "rs_hierarchy_features.h"
#include "rs_hierarchy_class_consolidator.h"
#include "rs_object_classify.h"
#include <QInputDialog>
#include <QLineEdit>
#include "rs_class_raster.h"
#include "rs_segmenter_port.h"
#include "rs_accuracy_assessment.h"
#include "classification/rs_accuracy_dialog.h"
#include "shell/rs_session_map_workspace.h"

#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/task_center.h"

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
#include <QGuiApplication>
#include <QFileDialog>
#include <QMenu>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

#include <atomic>
#include <cmath>
#include <memory>
#include <map>
#include <vector>
#include <gdal.h>
#include <gdal_alg.h>
#include <ogr_api.h>
#include <cpl_string.h>

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

RsObiaMainWindow::~RsObiaMainWindow() = default;

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

    // Segmentation params
    mToolbar->addWidget( new QLabel( tr( " Segments:" ) ) );
    auto *kernelSpin = new QSpinBox;
    kernelSpin->setRange( 3, 21 );
    kernelSpin->setValue( 5 );
    SicnuDialogHelp::tip( kernelSpin, tr(
      "平滑核大小 3–21。越大对象边界越粗、碎斑越少。" ) );
    kernelSpin->setObjectName( "kernelSpin" );
    mToolbar->addWidget( kernelSpin );

    auto *binsSpin = new QSpinBox;
    binsSpin->setRange( 2, 128 );
    binsSpin->setValue( 32 );
    SicnuDialogHelp::tip( binsSpin, tr(
      "量化级数（内置分割回退）。级数多则细节多、对象更碎。" ) );
    binsSpin->setObjectName( "binsSpin" );
    mToolbar->addWidget( binsSpin );

    auto *minRegionSpin = new QSpinBox;
    minRegionSpin->setRange( 10, 10000 );
    minRegionSpin->setValue( 100 );
    SicnuDialogHelp::tip( minRegionSpin, tr(
      "最小对象像元数。小于此值的区域会被合并，抑制碎斑。" ) );
    minRegionSpin->setObjectName( "minRegionSpin" );
    mToolbar->addWidget( minRegionSpin );

    auto *segAct = mToolbar->addAction( tr( "Segment" ), this, &RsObiaMainWindow::runSegmentation );
    SicnuDialogHelp::tip( segAct, tr( "运行单层影像分割，生成对象。" ) );

    auto *hierAct = mToolbar->addAction( tr( "Hierarchy" ), this, &RsObiaMainWindow::runHierarchicalSegmentation );
    SicnuDialogHelp::tip( hierAct, tr(
      "两层层次分割：细层 MeanShift + 粗层 Watershed + 父链接（需 OTB）。" ) );

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
    classifierCombo->addItems( { "NormalBayes", "SVM", "RandomForest", "KMeans" } );
    SicnuDialogHelp::tip( classifierCombo, tr(
      "对象级分类器：NormalBayes / SVM / RandomForest / KMeans。" ) );
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
    SicnuDialogHelp::tip( clsAct, tr( "对所选层级的对象进行分类。" ) );

    auto *cfgAct = mToolbar->addAction( tr( "Params" ), this, &RsObiaMainWindow::showClassifierConfigDialog );
    SicnuDialogHelp::tip( cfgAct, tr( "配置所选分类器的超参数（如 Random Forest 树数量、最大深度）。" ) );

    auto *roiAct = mToolbar->addAction( tr( "Import ROI" ), this, &RsObiaMainWindow::importRoiLabels );
    SicnuDialogHelp::tip( roiAct, tr(
      "从训练多边形按多数票标注对象；与点击标注冲突时后写覆盖并提示。" ) );

    auto *consAct = mToolbar->addAction( tr( "Consolidate" ), this, &RsObiaMainWindow::runHierarchyConsolidation );
    SicnuDialogHelp::tip( consAct, tr( "消解多尺度层次之间的分类矛盾（向上多数票投票 / 向下集成）。" ) );

    mToolbar->addSeparator();
    auto *accAct = mToolbar->addAction( tr( "精度评价" ), this, &RsObiaMainWindow::showAccuracyAssessment );
    SicnuDialogHelp::tip( accAct, tr( "查看最近一次对象分类的训练样本精度（混淆矩阵 / OA / Kappa）。" ) );

    auto *toMainAct = mToolbar->addAction( tr( "加载到主图" ), this, &RsObiaMainWindow::loadResultToMainMap );
    SicnuDialogHelp::tip( toMainAct, tr( "将分类结果栅格加载到主窗口地图。" ) );

    mToolbar->addSeparator();
    auto *expAct = mToolbar->addAction( tr( "Export" ), this, &RsObiaMainWindow::exportResult );
    SicnuDialogHelp::tip( expAct, tr( "导出分类栅格（及可选矢量化）。" ) );
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
    } );
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

void RsObiaMainWindow::loadRaster()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr( "Open Raster" ), QString(),
        tr( "Raster files (*.tif *.tiff *.img *.jp2 *.png);;All files (*)" ) );

    if ( path.isEmpty() )
        return;

    SICNU_LOG_INFO( SicnuLogTags::OBIA, QString( "Loading raster: %1" ).arg( path ) );

    auto *layer = new QgsRasterLayer( path, QFileInfo( path ).baseName() );
    if ( !layer->isValid() )
    {
        SICNU_LOG_ERROR( SicnuLogTags::OBIA, QString( "Invalid raster: %1" ).arg( path ) );
        QMessageBox::warning( this, tr( "Error" ), tr( "Cannot open raster: %1" ).arg( path ) );
        delete layer;
        return;
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
            return;
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
        return;
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
    mHierarchyStats.clear();
    mHasHierarchy = false;
    mActiveLevel = 0;
    mClassifyLevel = 0;
    mLastClassRasterPath.clear();
    if ( auto *ls = findChild<QSpinBox *>( "levelSpin" ) )
        ls->setValue( 0 );
    if ( auto *cs = findChild<QSpinBox *>( "classifyLevelSpin" ) )
        cs->setValue( 0 );
    updateSegmentTable();
    updateStatusLabel();

    statusBar()->showMessage( tr( "Loaded: %1 (%2 bands)" ).arg( path ).arg( mBandCount ), 5000 );
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

    QVector<int> bandIndices;
    for ( int b = 1; b <= mBandCount; ++b )
        bandIndices.append( b );

    auto *kernelSpin = findChild<QSpinBox *>( "kernelSpin" );
    auto *binsSpin = findChild<QSpinBox *>( "binsSpin" );
    auto *minRegionSpin = findChild<QSpinBox *>( "minRegionSpin" );

    RsObiaSegmentationConfig segCfg;
    segCfg.rasterPath = mRasterPath;
    segCfg.bandIndices = bandIndices;
    segCfg.preferOtb = true;
    segCfg.smoothKernel = kernelSpin ? kernelSpin->value() : 5;
    segCfg.quantizeBins = binsSpin ? binsSpin->value() : 32;
    segCfg.minRegionSize = minRegionSpin ? minRegionSpin->value() : 100;
    segCfg.spatialRadius = kernelSpin ? kernelSpin->value() : 5;
    segCfg.rangeRadius = binsSpin ? static_cast<double>( binsSpin->value() ) * 0.5 : 15.0;

    if ( startSegmentationTask( segCfg, bandIndices ) < 0 )
    {
        QMessageBox::information( this, tr( "OBIA" ),
                                  tr( "An OBIA task is already running." ) );
    }
}

long RsObiaMainWindow::startSegmentationTask( const RsObiaSegmentationConfig &segCfg,
                                              const QVector<int> &bandIndices )
{
    if ( isBusy() )
        return -1;

    const QString rasterPath = segCfg.rasterPath;
    auto canceled = std::make_shared<std::atomic<bool>>( false );
    auto work = std::make_shared<PendingSegWork>();

    auto *progress = new QProgressDialog(
        RsObiaSegmentation::isOtbAvailable()
            ? tr( "Segmenting with OTB MeanShift..." )
            : tr( "Segmenting with built-in segmenter..." ),
        tr( "Cancel" ), 0, 0, this );
    progress->setWindowModality( Qt::WindowModal );
    progress->setMinimumDuration( 0 );
    progress->setValue( 0 );
    progress->show();

    QGuiApplication::setOverrideCursor( Qt::WaitCursor );
    statusBar()->showMessage( progress->labelText() );

    sicnu::jobs::JobRequest req;
    req.algorithmId = "module:obia:segment";
    req.title = tr( "OBIA segmentation" ).toStdString();
    req.source = "module";
    req.exclusive = true;
    req.params["input"] = rasterPath.toStdString();

    m_pendingOp = PendingOp::Segmentation;
    m_pendingSegWork = work;
    m_pendingCanceled = canceled;
    m_pendingProgress = progress;

    const long taskId = sicnu::TaskCenter::instance().submitJob(
      req,
      [segCfg, bandIndices, rasterPath, canceled, work](
        const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
          ctx.logInfo( "OBIA segmentation" );
          ctx.reportProgress( 0.0, "Segmenting" );
          static bool s_gdalInit = ( GDALAllRegister(), true );
          Q_UNUSED( s_gdalInit );

          work->seg = RsObiaSegmentation::run( segCfg, [canceled, &ctx]() {
              return canceled->load() || ctx.isCancelled();
          } );

          if ( work->seg.ok && !canceled->load() && !ctx.isCancelled() )
          {
              ctx.reportProgress( 0.6, "Extracting features" );
              work->stats = RsSegmentFeatures::extract( rasterPath, work->seg.segMap, bandIndices );
          }
          else if ( work->seg.ok && ( canceled->load() || ctx.isCancelled() ) )
          {
              work->seg.ok = false;
              work->seg.errorMessage = QObject::tr( "Segmentation canceled" );
          }

          if ( ctx.isCancelled() || canceled->load()
               || work->seg.errorMessage.contains( QStringLiteral( "cancel" ), Qt::CaseInsensitive ) )
          {
              throw sicnu::operators::RSOperatorError(
                sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
          }
          if ( !work->seg.ok )
          {
              throw sicnu::operators::RSOperatorError(
                sicnu::operators::ErrorCode::ComputationError,
                work->seg.errorMessage.toStdString() );
          }
          Json::Value result( Json::objectValue );
          result["segmentCount"] = static_cast<int>( work->seg.segMap.segmentCount() );
          result["usedOtb"] = work->seg.usedOtb;
          return result;
      },
      [canceled]() { canceled->store( true ); },
      /*autoLoad=*/false );

    m_pendingTaskId = taskId;
    connect( progress, &QProgressDialog::canceled, this, [this, taskId, canceled]() {
        canceled->store( true );
        sicnu::TaskCenter::instance().cancelTask( taskId );
    } );
    if ( taskId >= 0 )
    {
        const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
        if ( info.status == sicnu::TaskStatus::Completed
             || info.status == sicnu::TaskStatus::Failed
             || info.status == sicnu::TaskStatus::Canceled )
        {
            onObiaTaskUpdated( info );
        }
    }
    return taskId;
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
        m_pendingSegWork.reset();
        m_pendingHierWork.reset();
        m_pendingHierClsWork.reset();
        if ( m_pendingFlatTask )
        {
            m_pendingFlatTask->deleteLater();
            m_pendingFlatTask = nullptr;
        }
        m_pendingFlatOutputPath.clear();
        m_pendingCanceled.reset();
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
    auto segWork = m_pendingSegWork;
    auto hierWork = m_pendingHierWork;
    auto hierClsWork = m_pendingHierClsWork;
    auto levelWork = m_pendingLevelWork;
    RsObiaTask *flatTask = m_pendingFlatTask;
    const QString flatOut = m_pendingFlatOutputPath;

    m_pendingTaskId = -1;
    m_pendingOp = PendingOp::None;
    m_pendingSegWork.reset();
    m_pendingHierWork.reset();
    m_pendingHierClsWork.reset();
    m_pendingLevelWork.reset();
    m_pendingFlatTask = nullptr;
    m_pendingFlatOutputPath.clear();
    m_pendingCanceled.reset();
    finishPendingUi();

    if ( op == PendingOp::Segmentation )
    {
        if ( !segWork )
        {
            updateStatusLabel();
            return;
        }
        if ( info.status == sicnu::TaskStatus::Canceled )
        {
            statusBar()->showMessage( tr( "Segmentation canceled" ), 3000 );
            updateStatusLabel();
            return;
        }
        if ( info.status != sicnu::TaskStatus::Completed || !segWork->seg.ok )
        {
            if ( segWork->seg.errorMessage.contains( QStringLiteral( "cancel" ), Qt::CaseInsensitive ) )
                statusBar()->showMessage( tr( "Segmentation canceled" ), 3000 );
            else
            {
                const QString err = !segWork->seg.errorMessage.isEmpty()
                                      ? segWork->seg.errorMessage
                                      : ( !info.errorMessage.isEmpty() ? info.errorMessage
                                                                       : tr( "Segmentation failed" ) );
                QMessageBox::warning( this, tr( "Error" ), err );
            }
            updateStatusLabel();
            return;
        }
        applySegmentationResult( segWork->seg.segMap, segWork->seg.usedOtb, segWork->stats );
        return;
    }

    if ( op == PendingOp::Hierarchy )
    {
        if ( info.status == sicnu::TaskStatus::Canceled )
        {
            statusBar()->showMessage( tr( "Hierarchy build canceled" ), 3000 );
            updateStatusLabel();
            return;
        }
        if ( info.status != sicnu::TaskStatus::Completed || !hierWork || !hierWork->ok )
        {
            const QString err = ( hierWork && !hierWork->error.isEmpty() )
                                  ? hierWork->error
                                  : ( !info.errorMessage.isEmpty() ? info.errorMessage
                                                                   : tr( "Hierarchy build failed" ) );
            QMessageBox::warning( this, tr( "Error" ), err );
            updateStatusLabel();
            return;
        }
        applyHierarchyResult( std::move( hierWork->hierarchy ), 0, std::move( hierWork->stats ) );
        return;
    }

    if ( op == PendingOp::HierarchyClassify )
    {
        if ( info.status == sicnu::TaskStatus::Canceled )
        {
            statusBar()->showMessage( tr( "Hierarchy classify canceled" ), 3000 );
            return;
        }
        if ( info.status == sicnu::TaskStatus::Completed && hierClsWork && hierClsWork->ok )
        {
            rememberClassification( hierClsWork->outputPath, hierClsWork->accuracy );
            loadClassifiedRaster( hierClsWork->outputPath );
            const QString accLine = mHasAccuracy
                                      ? tr( "\nOA=%1  Kappa=%2 (训练样本)" )
                                            .arg( mLastAccuracy.overallAccuracy, 0, 'f', 3 )
                                            .arg( mLastAccuracy.kappa, 0, 'f', 3 )
                                      : QString();
            QMessageBox box( this );
            box.setIcon( QMessageBox::Information );
            box.setWindowTitle( tr( "OBIA Classification" ) );
            box.setText( tr( "层级 %1 分类完成！\n输出：%2%3" )
                           .arg( hierClsWork->clsLevel )
                           .arg( hierClsWork->outputPath )
                           .arg( accLine ) );
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
        const QString err = ( hierClsWork && !hierClsWork->error.isEmpty() )
                              ? hierClsWork->error
                              : ( !info.errorMessage.isEmpty() ? info.errorMessage
                                                               : tr( "Classification failed" ) );
        QMessageBox::warning( this, tr( "Error" ), err );
        return;
    }

    if ( op == PendingOp::FlatClassify )
    {
        if ( info.status == sicnu::TaskStatus::Canceled )
        {
            statusBar()->showMessage( tr( "OBIA classify cancelled" ), 3000 );
            if ( flatTask )
                flatTask->deleteLater();
            return;
        }
        if ( info.status == sicnu::TaskStatus::Completed && flatTask && flatTask->result().ok )
        {
            populateUncertaintyTable( flatTask->result().segmentUncertainties, flatTask->result().segmentClasses );
            rememberClassification( flatOut, flatTask->result().accuracy );
            loadClassifiedRaster( flatOut );
            const QString accLine = mHasAccuracy
                                      ? tr( "\nOA=%1  Kappa=%2 (训练样本)" )
                                            .arg( mLastAccuracy.overallAccuracy, 0, 'f', 3 )
                                            .arg( mLastAccuracy.kappa, 0, 'f', 3 )
                                      : QString();
            QMessageBox box( this );
            box.setIcon( QMessageBox::Information );
            box.setWindowTitle( tr( "OBIA Classification" ) );
            box.setText( tr( "对象分类完成！\n输出：%1%2" ).arg( flatOut ).arg( accLine ) );
            auto *accBtn = box.addButton( tr( "精度评价" ), QMessageBox::ActionRole );
            auto *mainBtn = box.addButton( tr( "加载到主图" ), QMessageBox::ActionRole );
            box.addButton( QMessageBox::Ok );
            box.exec();
            if ( box.clickedButton() == accBtn )
                showAccuracyAssessment();
            else if ( box.clickedButton() == mainBtn )
                loadResultToMainMap();
        }
        else
        {
            const QString err = ( flatTask && !flatTask->result().errorMessage.isEmpty() )
                                  ? flatTask->result().errorMessage
                                  : ( !info.errorMessage.isEmpty() ? info.errorMessage
                                                                   : tr( "Classification failed" ) );
            QMessageBox::warning( this, tr( "Error" ), err );
        }
        if ( flatTask )
            flatTask->deleteLater();
        return;
    }

    if ( op == PendingOp::LevelFeatures )
    {
        if ( info.status == sicnu::TaskStatus::Canceled )
        {
            statusBar()->showMessage( tr( "Level feature extraction canceled" ), 3000 );
            return;
        }
        if ( info.status != sicnu::TaskStatus::Completed || !levelWork || !levelWork->ok )
        {
            const QString err = ( levelWork && !levelWork->error.isEmpty() )
                                  ? levelWork->error
                                  : ( !info.errorMessage.isEmpty() ? info.errorMessage : tr( "Level feature extraction failed" ) );
            QMessageBox::warning( this, tr( "Error" ), err );
            return;
        }
        applyLevelFeaturesResult( levelWork->level, std::move( levelWork->stats ) );
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
        // Keep placeholder; stats will arrive after current task. Show status.
        statusBar()->showMessage( tr( "Level %1: feature extraction queued…" ).arg( level ), 3000 );
        return;
    }
    startLevelFeaturesTask( level );
}

long RsObiaMainWindow::startLevelFeaturesTask( int level )
{
    if ( isBusy() || !mHasHierarchy || level < 0 || level >= mHierarchy.levelCount() )
        return -1;
    const QString rasterPath = mRasterPath;
    const RsSegmentMap segMap = mHierarchy.level( level );
    const QVector<int> bandIndices = allBandIndices();
    auto canceled = std::make_shared<std::atomic<bool>>( false );
    auto work = std::make_shared<PendingLevelWork>();
    work->level = level;

    auto *progress = new QProgressDialog( tr( "Extracting level %1 features…" ).arg( level ),
                                         tr( "Cancel" ), 0, 0, this );
    progress->setWindowModality( Qt::WindowModal );
    progress->setMinimumDuration( 0 );
    progress->show();
    QGuiApplication::setOverrideCursor( Qt::WaitCursor );
    statusBar()->showMessage( progress->labelText() );

    if ( auto *ls = findChild<QSpinBox *>( "levelSpin" ) )
        ls->setEnabled( false );

    sicnu::jobs::JobRequest req;
    req.algorithmId = "module:obia:level_features";
    req.title = QStringLiteral( "OBIA level %1 features" ).arg( level ).toStdString();
    req.source = "module";
    req.exclusive = true;
    req.params["level"] = level;

    m_pendingOp = PendingOp::LevelFeatures;
    m_pendingLevelWork = work;
    m_pendingCanceled = canceled;
    m_pendingProgress = progress;

    const long taskId = sicnu::TaskCenter::instance().submitJob(
        req,
        [rasterPath, segMap, bandIndices, canceled, work]( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ){
            static bool s_gdalInit = ( GDALAllRegister(), true );
            Q_UNUSED( s_gdalInit );
            ctx.reportProgress( 0.0, "Extracting" );
            if ( canceled->load() || ctx.isCancelled() )
                throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
            work->stats = RsSegmentFeatures::extract( rasterPath, segMap, bandIndices );
            work->ok = true;
            if ( canceled->load() || ctx.isCancelled() )
                throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
            Json::Value result( Json::objectValue );
            result["level"] = work->level;
            result["segments"] = static_cast<int>( work->stats.size() );
            return result;
        },
        [canceled]() { canceled->store( true ); },
        /*autoLoad=*/false );

    m_pendingTaskId = taskId;
    connect( progress, &QProgressDialog::canceled, this, [this, taskId, canceled]() {
        canceled->store( true );
        sicnu::TaskCenter::instance().cancelTask( taskId );
    } );
    if ( taskId >= 0 )
    {
        const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
        if ( info.status == sicnu::TaskStatus::Completed || info.status == sicnu::TaskStatus::Failed || info.status == sicnu::TaskStatus::Canceled )
            onObiaTaskUpdated( info );
    }
    return taskId;
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

    if ( !RsOtbSegmenter::isAvailable() )
    {
        QMessageBox::warning(
            this, tr( "OTB required" ),
            tr( "Hierarchical OBIA requires OTB Segmentation CLI.\n"
                "Set SICNU_OTB_PATH or install OTB.\n"
                "No silent teaching fallback for the hierarchy path." ) );
        return;
    }

    auto *kernelSpin = findChild<QSpinBox *>( "kernelSpin" );
    auto *binsSpin = findChild<QSpinBox *>( "binsSpin" );
    auto *minRegionSpin = findChild<QSpinBox *>( "minRegionSpin" );
    const int spatialRadius = kernelSpin ? kernelSpin->value() : 5;
    const double rangeRadius = binsSpin ? static_cast<double>( binsSpin->value() ) * 0.5 : 15.0;
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

    const QString rasterPath = mRasterPath;
    auto canceled = std::make_shared<std::atomic<bool>>( false );
    auto work = std::make_shared<PendingHierWork>();
    const QVector<int> bandIndices = allBandIndices();

    auto *progress = new QProgressDialog(
        tr( "Building 2-level hierarchy (MeanShift + Watershed)..." ),
        tr( "Cancel" ), 0, 0, this );
    progress->setWindowModality( Qt::WindowModal );
    progress->setMinimumDuration( 0 );
    progress->show();
    QGuiApplication::setOverrideCursor( Qt::WaitCursor );
    statusBar()->showMessage( progress->labelText() );

    sicnu::jobs::JobRequest req;
    req.algorithmId = "module:obia:hierarchy";
    req.title = tr( "OBIA hierarchical segment" ).toStdString();
    req.source = "module";
    req.exclusive = true;
    req.params["input"] = rasterPath.toStdString();

    m_pendingOp = PendingOp::Hierarchy;
    m_pendingHierWork = work;
    m_pendingCanceled = canceled;
    m_pendingProgress = progress;

    const long taskId = sicnu::TaskCenter::instance().submitJob(
        req,
        [rasterPath, bandIndices, canceled, work, spatialRadius, rangeRadius, minRegionSize,
         watershedThreshold](
            const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
            ctx.logInfo( "OBIA hierarchical segmentation" );
            static bool s_gdalInit = ( GDALAllRegister(), true );
            Q_UNUSED( s_gdalInit );

            RsLevelSpec fine;
            fine.filter = RsLevelSpec::Filter::MeanShift;
            fine.name = QStringLiteral( "fine" );
            fine.spatialRadius = spatialRadius;
            fine.rangeRadius = rangeRadius;
            fine.minRegionSize = minRegionSize;
            fine.maxIterations = 100;
            fine.threshold = 0.1;

            RsLevelSpec coarse;
            coarse.filter = RsLevelSpec::Filter::Watershed;
            coarse.name = QStringLiteral( "coarse" );
            coarse.watershedThreshold = watershedThreshold;

            RsOtbSegmenter segmenter;
            RsPixelMajorityParentLink linker;
            QString err;
            ctx.reportProgress( 0.1, "buildLevels" );
            const bool ok = work->hierarchy.buildLevels(
                rasterPath, { fine, coarse }, segmenter, linker, &err,
                [canceled, &ctx]() { return canceled->load() || ctx.isCancelled(); } );

            if ( !ok )
            {
                work->error = err;
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::ComputationError,
                    err.toStdString() );
            }

            ctx.reportProgress( 0.8, "Extracting fine-level features" );
            work->stats = RsSegmentFeatures::extract(
                rasterPath, work->hierarchy.level( 0 ), bandIndices );
            work->ok = true;

            Json::Value result( Json::objectValue );
            result["levels"] = work->hierarchy.levelCount();
            result["fineSegments"] = work->hierarchy.level( 0 ).segmentCount();
            if ( work->hierarchy.levelCount() > 1 )
                result["coarseSegments"] = work->hierarchy.level( 1 ).segmentCount();
            return result;
        },
        [canceled]() { canceled->store( true ); },
        /*autoLoad=*/false );

    m_pendingTaskId = taskId;
    connect( progress, &QProgressDialog::canceled, this, [this, taskId, canceled]() {
        canceled->store( true );
        sicnu::TaskCenter::instance().cancelTask( taskId );
    } );
    if ( taskId >= 0 )
    {
        const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
        if ( info.status == sicnu::TaskStatus::Completed
             || info.status == sicnu::TaskStatus::Failed
             || info.status == sicnu::TaskStatus::Canceled )
        {
            onObiaTaskUpdated( info );
        }
    }
    return taskId;
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

    auto *combo = findChild<QComboBox *>( "classifierCombo" );
    QString algoName = combo ? combo->currentText() : "NormalBayes";

    QSet<int> uniqueClasses;
    for ( auto it = mSegmentLabels.constBegin(); it != mSegmentLabels.constEnd(); ++it )
        uniqueClasses.insert( it.value() );

    std::unique_ptr<RsClassifierBackend> backend;
    if ( algoName == "RandomForest" )
        backend = std::make_unique<RsRandomForestBackend>( mRfNumTrees, mRfMaxDepth, mRfMinSampleCount );
    else if ( algoName == "MLP" || algoName.contains( "Neural", Qt::CaseInsensitive ) )
        backend = std::make_unique<RsMlpBackend>( mMlpHiddenLayerSize, mMlpMaxIter );
    else
        backend = RsClassifierBackendFactory::create( algoName );

    const QVector<int> bandIndices = allBandIndices();
    QHash<int, QColor> classColors;
    for ( const auto &cd : mClassDefs )
        classColors[cd.id] = cd.color;

    const QString defaultOut = QFileInfo( mRasterPath ).path() + QStringLiteral( "/obia_classified.tif" );
    const QString outputPath = QFileDialog::getSaveFileName(
        this, tr( "Save Classified Raster" ), defaultOut,
        tr( "GeoTIFF (*.tif *.tiff);;All files (*)" ) );
    if ( outputPath.isEmpty() )
        return;

    if ( mHasHierarchy && mHierarchy.levelCount() > 0 )
    {
        const int clsLevel = currentClassifyLevel();
        if ( clsLevel < 0 || clsLevel >= mHierarchy.levelCount() )
        {
            QMessageBox::warning( this, tr( "Error" ), tr( "Invalid classify level." ) );
            return;
        }

        auto backendShared = std::shared_ptr<RsClassifierBackend>( std::move( backend ) );
        if ( startHierarchyClassifyTask( clsLevel, outputPath, backendShared, bandIndices,
                                         classColors, mSegmentLabels ) < 0 )
        {
            QMessageBox::information( this, tr( "OBIA" ),
                                      tr( "An OBIA task is already running." ) );
        }
        return;
    }

    RsObiaTask::Config cfg;
    cfg.sourceRaster = mRasterPath;
    cfg.outputRaster = outputPath;
    cfg.bandIndices = bandIndices;
    cfg.existingSegMap = mSegMap;
    cfg.existingStats = mSegStats;
    cfg.backend = std::move( backend );
    cfg.segmentLabels = mSegmentLabels;
    cfg.classColors = classColors;
    cfg.algoName = algoName;
    cfg.featureSelection = featureSelection();

    auto *task = new RsObiaTask( std::move( cfg ) );
    if ( startFlatClassifyTask( task, outputPath, algoName ) < 0 )
    {
        task->deleteLater();
        QMessageBox::information( this, tr( "OBIA" ),
                                  tr( "An OBIA task is already running." ) );
    }
}

long RsObiaMainWindow::startHierarchyClassifyTask(
    int clsLevel, const QString &outputPath,
    std::shared_ptr<RsClassifierBackend> backend,
    const QVector<int> &bandIndices,
    const QHash<int, QColor> &classColors,
    const QMap<quint32, int> &trainLabels )
{
    if ( isBusy() || !mHasHierarchy || clsLevel < 0 || clsLevel >= mHierarchy.levelCount() )
        return -1;

    auto canceled = std::make_shared<std::atomic<bool>>( false );
    auto work = std::make_shared<PendingHierClsWork>();
    work->outputPath = outputPath;
    work->clsLevel = clsLevel;

    auto *progress = new QProgressDialog(
        tr( "Classifying hierarchy level %1..." ).arg( clsLevel ),
        tr( "Cancel" ), 0, 0, this );
    progress->setWindowModality( Qt::WindowModal );
    progress->setMinimumDuration( 0 );
    progress->show();
    QGuiApplication::setOverrideCursor( Qt::WaitCursor );
    statusBar()->showMessage( progress->labelText() );

    const RsObjectHierarchy hierarchy = mHierarchy;
    const QString rasterPath = mRasterPath;

    sicnu::jobs::JobRequest req;
    req.algorithmId = "module:obia:hierarchy_classify";
    req.title = QStringLiteral( "OBIA hierarchy classify L%1" ).arg( clsLevel ).toStdString();
    req.source = "module";
    req.exclusive = true;
    req.params["output"] = outputPath.toStdString();

    m_pendingOp = PendingOp::HierarchyClassify;
    m_pendingHierClsWork = work;
    m_pendingCanceled = canceled;
    m_pendingProgress = progress;

    const long taskId = sicnu::TaskCenter::instance().submitJob(
        req,
        [hierarchy, trainLabels, rasterPath, outputPath, clsLevel, bandIndices, classColors,
         backend, canceled, work](
            const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
            ctx.logInfo( "OBIA hierarchy classify" );
            static bool s_gdalInit = ( GDALAllRegister(), true );
            Q_UNUSED( s_gdalInit );

            auto isCanceled = [canceled, &ctx]() {
                return canceled->load() || ctx.isCancelled();
            };

            ctx.reportProgress( 0.1, "F2a features" );
            if ( isCanceled() )
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::Cancelled, "Cancelled" );

            auto feat = RsHierarchyFeatures::buildFeatureMatrix(
                hierarchy, rasterPath, clsLevel, bandIndices );
            if ( !feat.ok )
            {
                work->error = feat.errorMessage;
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::ComputationError,
                    feat.errorMessage.toStdString() );
            }

            if ( isCanceled() )
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::Cancelled, "Cancelled" );

            ctx.reportProgress( 0.5, "Train/predict" );
            auto cls = RsObjectClassify::classify(
                feat.X, feat.meta.segmentIds, trainLabels, *backend );
            if ( !cls.ok )
            {
                work->error = cls.errorMessage;
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::ComputationError,
                    cls.errorMessage.toStdString() );
            }

            // Training-set accuracy (true labels vs predicted class for labeled objects).
            {
                QVector<int> yTrue;
                QVector<int> yPred;
                for ( auto it = trainLabels.constBegin(); it != trainLabels.constEnd(); ++it )
                {
                    if ( !cls.segmentClasses.contains( it.key() ) )
                        continue;
                    yTrue.append( it.value() );
                    yPred.append( cls.segmentClasses.value( it.key() ) );
                }
                work->accuracy = RsAccuracyAssessment::compute( yTrue, yPred );
            }

            if ( isCanceled() )
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::Cancelled, "Cancelled" );

            ctx.reportProgress( 0.85, "Paint class raster" );
            auto paint = RsClassRaster::paint(
                hierarchy.level( clsLevel ), cls.segmentClasses, rasterPath, outputPath, classColors );
            if ( !paint.ok )
            {
                work->error = paint.errorMessage;
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::GdalError,
                    paint.errorMessage.toStdString() );
            }

            work->ok = true;
            Json::Value result( Json::objectValue );
            result["output"] = outputPath.toStdString();
            result["classifyLevel"] = clsLevel;
            return result;
        },
        [canceled]() { canceled->store( true ); },
        /*autoLoad=*/false );

    m_pendingTaskId = taskId;
    connect( progress, &QProgressDialog::canceled, this, [this, taskId, canceled]() {
        canceled->store( true );
        sicnu::TaskCenter::instance().cancelTask( taskId );
    } );
    if ( taskId >= 0 )
    {
        const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
        if ( info.status == sicnu::TaskStatus::Completed
             || info.status == sicnu::TaskStatus::Failed
             || info.status == sicnu::TaskStatus::Canceled )
        {
            onObiaTaskUpdated( info );
        }
    }
    return taskId;
}

long RsObiaMainWindow::startFlatClassifyTask( RsObiaTask *task, const QString &outputPath,
                                              const QString &algoName )
{
    if ( isBusy() || !task )
        return -1;

    QGuiApplication::setOverrideCursor( Qt::WaitCursor );
    statusBar()->showMessage( tr( "Classifying objects..." ) );

    sicnu::jobs::JobRequest req;
    req.algorithmId = "module:obia:classify";
    req.title = QStringLiteral( "OBIA classify: %1" ).arg( algoName ).toStdString();
    req.source = "module";
    req.exclusive = true;
    req.params["output"] = outputPath.toStdString();

    m_pendingOp = PendingOp::FlatClassify;
    m_pendingFlatTask = task;
    m_pendingFlatOutputPath = outputPath;
    m_pendingProgress = nullptr;

    const long taskId = sicnu::TaskCenter::instance().submitJob(
        req,
        [task]( const sicnu::jobs::JobRequest &request,
                sicnu::operators::RSOperatorContext &ctx ) {
            ctx.logInfo( "OBIA classify" );
            ctx.reportProgress( 0.0, "Classifying objects" );
            const bool ok = task->run();
            if ( ctx.isCancelled()
                 || ( !ok && task->result().errorMessage.contains(
                                 QStringLiteral( "cancel" ), Qt::CaseInsensitive ) ) )
            {
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
            }
            if ( !ok )
            {
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::ComputationError,
                    task->result().errorMessage.toStdString() );
            }
            Json::Value result( Json::objectValue );
            result["output"] = request.params.get( "output", "" ).asString();
            result["durationMs"] = task->result().durationMs;
            return result;
        },
        [task]() { task->cancel(); },
        /*autoLoad=*/false );

    m_pendingTaskId = taskId;
    if ( taskId >= 0 )
    {
        const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
        if ( info.status == sicnu::TaskStatus::Completed
             || info.status == sicnu::TaskStatus::Failed
             || info.status == sicnu::TaskStatus::Canceled )
        {
            onObiaTaskUpdated( info );
        }
    }
    return taskId;
}

void RsObiaMainWindow::importRoiLabels()
{
    if ( mSegMap.isEmpty() )
    {
        QMessageBox::information( this, tr( "Import ROI" ), tr( "Run segmentation or hierarchy first." ) );
        return;
    }

    // Labels apply to classify level geometry
    const int clsLevel = mHasHierarchy ? currentClassifyLevel() : 0;
    const RsSegmentMap &labelMap =
        mHasHierarchy ? mHierarchy.level( clsLevel ) : mSegMap;

    if ( labelMap.isEmpty() )
    {
        QMessageBox::warning( this, tr( "Import ROI" ), tr( "Empty segment map at classify level." ) );
        return;
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

    GDALAllRegister();
    GDALDatasetH vecDs = GDALOpenEx( path.toUtf8().constData(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr );
    if ( !vecDs )
    {
        QMessageBox::warning( this, tr( "Import ROI" ), tr( "Cannot open vector: %1" ).arg( path ) );
        return;
    }

    OGRLayerH layer = GDALDatasetGetLayer( vecDs, 0 );
    if ( !layer )
    {
        GDALClose( vecDs );
        QMessageBox::warning( this, tr( "Import ROI" ), tr( "No layers in vector file." ) );
        return;
    }

    OGRFeatureDefnH defn = OGR_L_GetLayerDefn( layer );
    int fieldIdx = OGR_FD_GetFieldIndex( defn, "class_id" );
    if ( fieldIdx < 0 )
        fieldIdx = OGR_FD_GetFieldIndex( defn, "class" );
    if ( fieldIdx < 0 )
        fieldIdx = OGR_FD_GetFieldIndex( defn, "id" );
    if ( fieldIdx < 0 )
    {
        GDALClose( vecDs );
        QMessageBox::warning( this, tr( "Import ROI" ),
                              tr( "No class_id/class/id field found." ) );
        return;
    }

    double gt[6] = { 0, 1, 0, 0, 0, 1 };
    GDALDatasetH rds = GDALOpen( mRasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( rds )
    {
        GDALGetGeoTransform( rds, gt );
        GDALClose( rds );
    }

    const int w = labelMap.width();
    const int h = labelMap.height();
    const auto &labels = labelMap.labels();

    // votes[segId][classId]
    QHash<quint32, QHash<int, int>> votes;

    OGR_L_ResetReading( layer );
    OGRFeatureH feat = nullptr;
    while ( ( feat = OGR_L_GetNextFeature( layer ) ) != nullptr )
    {
        const int classId = OGR_F_GetFieldAsInteger( feat, fieldIdx );
        OGRGeometryH geom = OGR_F_GetGeometryRef( feat );
        if ( classId <= 0 || !geom )
        {
            OGR_F_Destroy( feat );
            continue;
        }

        double invGt[6];
        if ( !GDALInvGeoTransform( gt, invGt ) )
        {
            OGR_F_Destroy( feat );
            continue;
        }

        // Envelope → pixel range across all 4 corners of the polygon envelope.
        OGREnvelope env;
        OGR_G_GetEnvelope( geom, &env );
        const double xs[4] = { env.MinX, env.MaxX, env.MinX, env.MaxX };
        const double ys[4] = { env.MinY, env.MinY, env.MaxY, env.MaxY };
        double minCol = std::numeric_limits<double>::infinity();
        double maxCol = -std::numeric_limits<double>::infinity();
        double minRow = std::numeric_limits<double>::infinity();
        double maxRow = -std::numeric_limits<double>::infinity();

        for ( int i = 0; i < 4; ++i )
        {
            const double px = invGt[0] + xs[i] * invGt[1] + ys[i] * invGt[2];
            const double py = invGt[3] + xs[i] * invGt[4] + ys[i] * invGt[5];
            minCol = std::min( minCol, px );
            maxCol = std::max( maxCol, px );
            minRow = std::min( minRow, py );
            maxRow = std::max( maxRow, py );
        }

        const int c0 = (std::max)( 0, static_cast<int>( std::floor( minCol ) ) );
        const int c1 = (std::min)( w - 1, static_cast<int>( std::ceil( maxCol ) ) );
        const int r0 = (std::max)( 0, static_cast<int>( std::floor( minRow ) ) );
        const int r1 = (std::min)( h - 1, static_cast<int>( std::ceil( maxRow ) ) );
        if ( c0 > c1 || r0 > r1 )
        {
            OGR_F_Destroy( feat );
            continue;
        }

        // Rasterize polygon to window mask to avoid per-pixel GEOS point-in-poly (SHELLB-3).
        const int winW = c1 - c0 + 1;
        const int winH = r1 - r0 + 1;
        GDALDriverH memDrv = GDALGetDriverByName( "MEM" );
        bool rasterized = false;
        QVector<unsigned char> mask;
        if ( memDrv )
        {
            GDALDatasetH memDs = GDALCreate( memDrv, "", winW, winH, 1, GDT_Byte, nullptr );
            if ( memDs )
            {
                double gtWin[6] = { gt[0] + c0 * gt[1] + r0 * gt[2], gt[1], gt[2],
                                    gt[3] + c0 * gt[4] + r0 * gt[5], gt[4], gt[5] };
                GDALSetGeoTransform( memDs, gtWin );
                // copy projection from source raster if available
                GDALDatasetH srcDsTmp = GDALOpen( mRasterPath.toUtf8().constData(), GA_ReadOnly );
                if ( srcDsTmp )
                {
                    const char *proj = GDALGetProjectionRef( srcDsTmp );
                    if ( proj && proj[0] ) GDALSetProjection( memDs, proj );
                    GDALClose( srcDsTmp );
                }
                GDALRasterBandH memBand = GDALGetRasterBand( memDs, 1 );
                unsigned char zero = 0;
                // initialize to 0
                for ( int rr = 0; rr < winH; ++rr )
                    GDALRasterIO( memBand, GF_Write, 0, rr, winW, 1, &zero, winW, 1, GDT_Byte, 0, 0 );
                char **opts = nullptr;
                // center-pixel semantics: ALL_TOUCHED FALSE matches previous +0.5 rule
                opts = CSLSetNameValue( opts, "ALL_TOUCHED", "FALSE" );
                double burnVal = 1.0;
                OGRGeometryH geomList[1] = { geom };
                int bandList[1] = { 1 };
                CPLErr rErr = GDALRasterizeGeometries( memDs, 1, bandList, 1, geomList, nullptr, nullptr, &burnVal, opts, nullptr, nullptr );
                CSLDestroy( opts );
                if ( rErr == CE_None )
                {
                    mask.resize( winW * winH );
                    if ( GDALRasterIO( memBand, GF_Read, 0, 0, winW, winH, mask.data(), winW, winH, GDT_Byte, 0, 0 ) == CE_None )
                        rasterized = true;
                }
                GDALClose( memDs );
            }
        }
        if ( rasterized )
        {
            for ( int rr = 0; rr < winH; ++rr )
                for ( int cc = 0; cc < winW; ++cc )
                    if ( mask[rr * winW + cc] )
                    {
                        const int r = r0 + rr;
                        const int c = c0 + cc;
                        const quint32 sid = labels[static_cast<size_t>(r) * static_cast<size_t>(w) + static_cast<size_t>(c)];
                        if ( sid != 0 )
                            ++votes[sid][classId];
                    }
        }
        else
        {
            // Fallback to per-pixel GEOS (tiny windows or MEM unavailable) — keeps correctness.
            OGRGeometryH pt = OGR_G_CreateGeometry( wkbPoint );
            for ( int r = r0; r <= r1; ++r )
                for ( int c = c0; c <= c1; ++c )
                {
                    const double x = gt[0] + ( c + 0.5 ) * gt[1] + ( r + 0.5 ) * gt[2];
                    const double y = gt[3] + ( c + 0.5 ) * gt[4] + ( r + 0.5 ) * gt[5];
                    OGR_G_SetPoint_2D( pt, 0, x, y );
                    if ( !OGR_G_Contains( geom, pt ) && !OGR_G_Intersects( geom, pt ) )
                        continue;
                    const quint32 sid = labels[static_cast<size_t>(r) * static_cast<size_t>(w) + static_cast<size_t>(c)];
                    if ( sid != 0 )
                        ++votes[sid][classId];
                }
            OGR_G_DestroyGeometry( pt );
        }
        OGR_F_Destroy( feat );
    }
    GDALClose( vecDs );

    // When viewing a different level than classify level, switch view for assign feedback
    if ( mHasHierarchy && mActiveLevel != clsLevel )
        setActiveLevelMap( clsLevel );

    // Match operator default minLabelPixels (3) for dual-write parity.
    constexpr int minLabelPixels = 3;

    int overwritten = 0;
    int newlyLabeled = 0;
    int skippedSparse = 0;
    for ( auto it = votes.constBegin(); it != votes.constEnd(); ++it )
    {
        int bestClass = 0;
        int bestCount = 0;
        int total = 0;
        for ( auto cit = it.value().constBegin(); cit != it.value().constEnd(); ++cit )
        {
            total += cit.value();
            // Majority; ties → smaller classId (deterministic, mirrors parent-link P1).
            if ( cit.value() > bestCount
                 || ( cit.value() == bestCount && ( bestClass == 0 || cit.key() < bestClass ) ) )
            {
                bestCount = cit.value();
                bestClass = cit.key();
            }
        }
        if ( bestClass <= 0 || total < minLabelPixels )
        {
            if ( total > 0 && total < minLabelPixels )
                ++skippedSparse;
            continue;
        }

        if ( mSegmentLabels.contains( it.key() ) && mSegmentLabels.value( it.key() ) != bestClass )
            ++overwritten;
        else if ( !mSegmentLabels.contains( it.key() ) )
            ++newlyLabeled;

        // Last write wins
        mSegmentLabels[it.key()] = bestClass;
    }

    updateSegmentTable();
    updateStatusLabel();
    statusBar()->showMessage(
        tr( "ROI majority: +%1 new, %2 overwritten (last write wins), %3 skipped (< %4 px) on L%5" )
            .arg( newlyLabeled )
            .arg( overwritten )
            .arg( skippedSparse )
            .arg( minLabelPixels )
            .arg( clsLevel ),
        6000 );
}

void RsObiaMainWindow::exportResult()
{
    if ( !mLastClassRasterPath.isEmpty() && QFileInfo::exists( mLastClassRasterPath ) )
    {
        const QString vecOut = QFileDialog::getSaveFileName(
            this, tr( "Export class polygons (optional)" ),
            QFileInfo( mLastClassRasterPath ).path() + QStringLiteral( "/obia_classes.shp" ),
            tr( "Shapefile (*.shp);;All files (*)" ) );
        if ( !vecOut.isEmpty() )
        {
            auto r = RsClassRaster::polygonize( mLastClassRasterPath, vecOut );
            if ( r.ok )
            {
                QMessageBox::information(
                    this, tr( "Export" ),
                    tr( "Class raster: %1\nPolygons: %2" )
                        .arg( mLastClassRasterPath )
                        .arg( vecOut ) );
                return;
            }
            QMessageBox::warning( this, tr( "Export" ),
                                  tr( "Class raster at:\n%1\nPolygonize failed: %2" )
                                      .arg( mLastClassRasterPath )
                                      .arg( r.errorMessage ) );
            return;
        }
        QMessageBox::information( this, tr( "Export" ),
                                  tr( "Class raster already written:\n%1" ).arg( mLastClassRasterPath ) );
        return;
    }

    QMessageBox::information(
        this, tr( "Export" ),
        tr( "Run Classify first to write a class raster, then Export can polygonize it." ) );
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
    QString algoName = combo ? combo->currentText() : "RandomForest";

    bool ok = false;
    if ( algoName.contains( "MLP", Qt::CaseInsensitive ) || algoName.contains( "Neural", Qt::CaseInsensitive ) )
    {
        int hiddenSize = QInputDialog::getInt(
            this, tr( "MLP Config" ), tr( "Hidden Layer Neuron Count:" ),
            mMlpHiddenLayerSize, 2, 512, 4, &ok );
        if ( ok )
        {
            mMlpHiddenLayerSize = hiddenSize;
            int maxIter = QInputDialog::getInt(
                this, tr( "MLP Config" ), tr( "Maximum Iterations (maxIter):" ),
                mMlpMaxIter, 10, 10000, 50, &ok );
            if ( ok )
                mMlpMaxIter = maxIter;
        }
    }
    else if ( algoName.contains( "RandomForest", Qt::CaseInsensitive ) || algoName.contains( "RF", Qt::CaseInsensitive ) )
    {
        int numTrees = QInputDialog::getInt(
            this, tr( "RandomForest Config" ), tr( "Number of Decision Trees (numTrees):" ),
            mRfNumTrees, 10, 1000, 10, &ok );
        if ( ok )
        {
            mRfNumTrees = numTrees;
            int maxDepth = QInputDialog::getInt(
                this, tr( "RandomForest Config" ), tr( "Max Tree Depth (maxDepth):" ),
                mRfMaxDepth, 2, 100, 1, &ok );
            if ( ok )
            {
                mRfMaxDepth = maxDepth;
                int minSamples = QInputDialog::getInt(
                    this, tr( "RandomForest Config" ), tr( "Min Sample Count per Node (minSampleCount):" ),
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
