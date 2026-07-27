// rs_obia_main_window.cpp — Phase 10B Task 10B.5
#include "rs_obia_main_window.h"
#include "dialogs/dialog_help_catalog.h"
#include "sicnu_logging.h"

#include "rs_obia_task.h"
#include "rs_obia_segmentation.h"
#include "rs_classifier_normalbayes.h"
#include "rs_classifier_svm.h"
#include "rs_classifier_kmeans.h"
#include "rs_object_hierarchy.h"
#include "rs_otb_segmenter.h"
#include "rs_parent_link.h"
#include "rs_hierarchy_features.h"
#include "rs_object_classify.h"
#include "rs_class_raster.h"
#include "rs_segmenter_port.h"
#include "rs_accuracy_assessment.h"
#include "classification/rs_accuracy_dialog.h"

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
#include <QComboBox>
#include <QGuiApplication>
#include <QFileDialog>
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
#include <ogr_api.h>

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
    classifierCombo->addItems( { "NormalBayes", "SVM", "KMeans" } );
    SicnuDialogHelp::tip( classifierCombo, tr(
      "对象级分类器：NormalBayes / SVM / KMeans。" ) );
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

    auto *roiAct = mToolbar->addAction( tr( "Import ROI" ), this, &RsObiaMainWindow::importRoiLabels );
    SicnuDialogHelp::tip( roiAct, tr(
      "从训练多边形按多数票标注对象；与点击标注冲突时后写覆盖并提示。" ) );

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
    SicnuDialogHelp::tip( mClassTable, tr( "类别定义。ID 对应分类栅格像元值。" ) );
    mClassTable->horizontalHeader()->setStretchLastSection( true );
    mClassTable->setSelectionBehavior( QAbstractItemView::SelectRows );
    mClassTable->setSelectionMode( QAbstractItemView::SingleSelection );
    mClassTable->setMinimumWidth( 180 );

    // Populate class table
    mClassTable->setRowCount( mClassDefs.size() );
    for ( int i = 0; i < mClassDefs.size(); ++i )
    {
        mClassTable->setItem( i, 0, new QTableWidgetItem( QString::number( mClassDefs[i].id ) ) );
        mClassTable->setItem( i, 1, new QTableWidgetItem( mClassDefs[i].name ) );
        auto *colorItem = new QTableWidgetItem;
        colorItem->setBackground( mClassDefs[i].color );
        mClassTable->setItem( i, 2, colorItem );
    }
    connect( mClassTable, &QTableWidget::cellClicked, this, [this]( int row, int ) {
        if ( row >= 0 && row < mClassDefs.size() )
            mCurrentClassId = mClassDefs[row].id;
    } );

    // Assign button
    auto *assignBtn = new QPushButton( tr( "Assign to Selected Segment" ) );
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
    addDockWidget( Qt::BottomDockWidgetArea, mInfoDock );

    // Right dock: segment list
    mSegmentDock = new QDockWidget( tr( "Segments" ), this );
    mSegmentDock->setObjectName( "obiaSegmentDock" );
    mSegmentTable = new QTableWidget;
    mSegmentTable->setColumnCount( 3 );
    mSegmentTable->setHorizontalHeaderLabels( { tr( "ID" ), tr( "Pixels" ), tr( "Class" ) } );
    mSegmentTable->horizontalHeader()->setStretchLastSection( true );
    mSegmentTable->setSelectionBehavior( QAbstractItemView::SelectRows );
    mSegmentDock->setWidget( mSegmentTable );
    addDockWidget( Qt::RightDockWidgetArea, mSegmentDock );
}

void RsObiaMainWindow::setupMapCanvas()
{
    // HIGH #4 fix: QgsLayerTree has no QObject parent, so we manage it manually.
    // mLayerTreeModel takes ownership of mLayerTree. mLayerView parented to this.
    mLayerTree = new QgsLayerTree;
    mLayerTreeModel = new QgsLayerTreeModel( mLayerTree ); // takes ownership
    mLayerView = new QgsLayerTreeView( this );
    mLayerView->setModel( mLayerTreeModel );

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

    auto layer = std::make_shared<QgsRasterLayer>( path, QFileInfo( path ).baseName() );
    if ( !layer->isValid() )
    {
        SICNU_LOG_ERROR( SicnuLogTags::OBIA, QString( "Invalid raster: %1" ).arg( path ) );
        QMessageBox::warning( this, tr( "Error" ), tr( "Cannot open raster: %1" ).arg( path ) );
        return;
    }

    mRasterLayer = layer;
    mRasterPath = path;
    mBandCount = layer->bandCount();

    // Update canvas
    mCanvas->setLayers( { mRasterLayer.get() } );
    mCanvas->setExtent( layer->extent() );
    mCanvas->refresh();

    // Reset segmentation / hierarchy
    mSegMap = RsSegmentMap();
    mSegStats.clear();
    mSegmentLabels.clear();
    mHierarchy.clear();
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
}

void RsObiaMainWindow::loadClassifiedRaster( const QString &outputPath )
{
    mLastClassRasterPath = outputPath;
    // Session canvas only — do not inject into main QgsProject catalog (ADR 0010).
    // Main map load is explicit via requestLoadToMainMap → loadDataLayer.
    auto resultLayer = std::make_shared<QgsRasterLayer>(
        outputPath, QFileInfo( outputPath ).baseName(), QStringLiteral( "gdal" ) );
    if ( !resultLayer->isValid() )
    {
        statusBar()->showMessage( tr( "Invalid classification raster: %1" ).arg( outputPath ), 5000 );
        return;
    }
    mClassifiedLayer = resultLayer;
    QList<QgsMapLayer *> layers;
    if ( mRasterLayer )
        layers << mRasterLayer.get();
    layers << mClassifiedLayer.get();
    mCanvas->setLayers( layers );
    mCanvas->refresh();
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
    RsObiaTask *flatTask = m_pendingFlatTask;
    const QString flatOut = m_pendingFlatOutputPath;

    m_pendingTaskId = -1;
    m_pendingOp = PendingOp::None;
    m_pendingSegWork.reset();
    m_pendingHierWork.reset();
    m_pendingHierClsWork.reset();
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

    mActiveLevel = level;
    mSegMap = mHierarchy.level( level );
    mSegStats = RsSegmentFeatures::extract( mRasterPath, mSegMap, allBandIndices() );

    // Labels are stored for classify level; when viewing another level, table class col may be empty.
    if ( mSelectTool )
        mSelectTool->setSegmentMap( mSegMap );

    updateSegmentTable();
    updateStatusLabel();
}

void RsObiaMainWindow::applyHierarchyResult( RsObjectHierarchy hierarchy,
                                             int activeLevel,
                                             QMap<quint32, RsSegmentFeatures::SegmentStat> stats )
{
    mHierarchy = std::move( hierarchy );
    mHasHierarchy = true;
    mActiveLevel = activeLevel;
    mClassifyLevel = 0;
    mSegMap = mHierarchy.level( activeLevel );
    mSegStats = std::move( stats );
    mSegmentLabels.clear();

    if ( auto *ls = findChild<QSpinBox *>( "levelSpin" ) )
    {
        ls->setMaximum( std::max( 0, mHierarchy.levelCount() - 1 ) );
        ls->setValue( activeLevel );
    }
    if ( auto *cs = findChild<QSpinBox *>( "classifyLevelSpin" ) )
    {
        cs->setMaximum( std::max( 0, mHierarchy.levelCount() - 1 ) );
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
    if ( algoName == "SVM" )
        backend = std::make_unique<RsClassifierSvm>();
    else if ( algoName == "KMeans" )
        backend = std::make_unique<RsClassifierKMeans>( uniqueClasses.size() );
    else
        backend = std::make_unique<RsClassifierNormalBayes>();

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

        if ( std::abs( gt[1] ) < 1e-12 || std::abs( gt[5] ) < 1e-12 )
        {
            OGR_F_Destroy( feat );
            continue;
        }

        // Envelope → pixel range; normalize for north-up or south-up geotransforms.
        OGREnvelope env;
        OGR_G_GetEnvelope( geom, &env );
        int cA = static_cast<int>( std::floor( ( env.MinX - gt[0] ) / gt[1] ) );
        int cB = static_cast<int>( std::ceil( ( env.MaxX - gt[0] ) / gt[1] ) );
        int rA = static_cast<int>( std::floor( ( env.MinY - gt[3] ) / gt[5] ) );
        int rB = static_cast<int>( std::ceil( ( env.MaxY - gt[3] ) / gt[5] ) );
        const int c0 = std::max( 0, std::min( cA, cB ) );
        const int c1 = std::min( w - 1, std::max( cA, cB ) );
        const int r0 = std::max( 0, std::min( rA, rB ) );
        const int r1 = std::min( h - 1, std::max( rA, rB ) );
        if ( c0 > c1 || r0 > r1 )
        {
            OGR_F_Destroy( feat );
            continue;
        }

        OGRGeometryH pt = OGR_G_CreateGeometry( wkbPoint );
        for ( int r = r0; r <= r1; ++r )
        {
            for ( int c = c0; c <= c1; ++c )
            {
                const double x = gt[0] + ( c + 0.5 ) * gt[1] + ( r + 0.5 ) * gt[2];
                const double y = gt[3] + ( c + 0.5 ) * gt[4] + ( r + 0.5 ) * gt[5];
                OGR_G_SetPoint_2D( pt, 0, x, y );
                if ( !OGR_G_Contains( geom, pt ) && !OGR_G_Intersects( geom, pt ) )
                    continue;
                const quint32 sid = labels[r * w + c];
                if ( sid != 0 )
                    ++votes[sid][classId];
            }
        }
        OGR_G_DestroyGeometry( pt );
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

// ---------------------------------------------------------------------------
// UI updates
// ---------------------------------------------------------------------------

void RsObiaMainWindow::updateSegmentTable()
{
    auto segIds = mSegMap.uniqueLabels().values();
    std::sort( segIds.begin(), segIds.end() );

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
