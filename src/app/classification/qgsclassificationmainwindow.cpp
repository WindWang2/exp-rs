#include "qgsclassificationmainwindow.h"
#include "dialogs/dialog_help_catalog.h"

#include "processing/algorithms/math_utils.h"
#include "core/sicnu_logging.h"
#include "rs_accuracy_dialog.h"
#include "rs_accuracy_panel.h"
#include "rs_class_def.h"
#include "rs_class_quick_list.h"
#include "rs_class_table_widget.h"
#include "rs_classification_project.h"
#include "rs_classification_split.h"
#include "rs_classification_task.h"
#include "rs_classifier_kmeans.h"
#include "rs_post_process_dialog.h"
#include "rs_post_process_task.h"
#include "rs_classifier_load_dialog.h"
#include "qgisinterface.h"
#include "rs_classifier_normalbayes.h"
#include "rs_classifier_setup_bar.h"
#include "rs_classifier_svm.h"
#include "rs_classify_session_state.h"
#include "rs_classify_step_host.h"
#include "rs_classify_stepper_bar.h"
#include "rs_classify_workflow_bridge.h"
#include "rs_classify_workflow_controller.h"
#include "rs_cross_validation.h"
#include "rs_cv_task.h"
#include "rs_jm_matrix_widget.h"
#include "rs_jm_separability.h"
#include "rs_pixel_rasterizer.h"
#include "rs_pixel_window.h"
#include "rs_roi.h"
#include "rs_roi_collection.h"
#include "rs_roi_io.h"
#include "rs_roi_tool_base.h"
#include "rs_roi_tool_magicwand.h"
#include "rs_spectral_curve_widget.h"
#include "rs_feature_scaler.h"
#include "qgsadvanceddigitizingdockwidget.h"
#include "qgsapplication.h"
#include "qgscategorizedsymbolrenderer.h"
#include "qgscoordinatereferencesystem.h"
#include "qgsfeature.h"
#include "qgsfeatureid.h"
#include "qgsfeatureiterator.h"
#include "qgsfeaturerequest.h"
#include "qgsfield.h"
#include "qgsfields.h"
#include "qgsfillsymbol.h"
#include "qgsgeometry.h"
#include "qgsmapcanvas.h"
#include "qgsmaplayer.h"
#include "qgsmapmouseevent.h"
#include "qgsmaptooldigitizefeature.h"
#include "qgsmaptoolpan.h"
#include "qgsmemoryproviderutils.h"
#include "qgsmessagelog.h"
#include "qgslayertreemodel.h"
#include "qgslayertreeview.h"
#include "qgsrasterlayer.h"
#include "qgssymbol.h"
#include "qgstaskmanager.h"
#include "qgsvectorlayer.h"
#include "qgis.h"

#include "shell/rs_job_runner.h"
#include "shell/rs_session_map_workspace.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPair>
#include <QPushButton>
#include <QSet>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <opencv2/ml.hpp>

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal_priv.h>

#include <memory>
#include <utility>

namespace
{

/// Click-select features on the sample vector layer (QGIS-like selection).
class SampleSelectTool : public QgsMapTool
{
  public:
    SampleSelectTool( QgsMapCanvas *canvas, QgsVectorLayer **layerRef )
      : QgsMapTool( canvas )
      , mLayerRef( layerRef )
    {
      setCursor( Qt::ArrowCursor );
      mToolName = QStringLiteral( "Select samples" );
    }

    void canvasReleaseEvent( QgsMapMouseEvent *e ) override
    {
      if ( !e || !mLayerRef || !*mLayerRef )
        return;
      QgsVectorLayer *vl = *mLayerRef;
      const QgsPointXY map = toMapCoordinates( e->pos() );
      // ~5 px search radius in map units
      const double mapUnitsPerPixel = canvas() && canvas()->mapUnitsPerPixel() > 0
                                        ? canvas()->mapUnitsPerPixel()
                                        : 1.0;
      const double tol = 5.0 * mapUnitsPerPixel;
      QgsRectangle search( map.x() - tol, map.y() - tol, map.x() + tol, map.y() + tol );
      QgsFeatureRequest req;
      req.setFilterRect( search );
      req.setFlags( Qgis::FeatureRequestFlag::ExactIntersect );
      QgsFeatureIterator it = vl->getFeatures( req );
      QgsFeature f;
      QgsFeatureIds ids;
      while ( it.nextFeature( f ) )
        ids.insert( f.id() );

      if ( e->modifiers() & Qt::ShiftModifier )
      {
        QgsFeatureIds cur = vl->selectedFeatureIds();
        for ( QgsFeatureId id : ids )
          cur.insert( id );
        vl->selectByIds( cur );
      }
      else if ( e->modifiers() & Qt::ControlModifier )
      {
        QgsFeatureIds cur = vl->selectedFeatureIds();
        for ( QgsFeatureId id : ids )
        {
          if ( cur.contains( id ) )
            cur.remove( id );
          else
            cur.insert( id );
        }
        vl->selectByIds( cur );
      }
      else
      {
        vl->selectByIds( ids );
      }
      if ( canvas() )
        canvas()->refresh();
    }

  private:
    QgsVectorLayer **mLayerRef = nullptr;
};

/// Fit a feature scaler on the train split and write scaled matrices +
/// the fitted scaler onto \a cfg. Returns false if fit fails.
bool fitScalerOntoConfig( const RsTrainTestSplit &split,
                          RsClassificationTask::Config &cfg )
{
  RsFeatureScaler scaler;
  if ( !scaler.fit( split.trainX ) )
    return false;
  cfg.trainX = scaler.transform( split.trainX );
  cfg.trainY = split.trainY;
  if ( !split.testX.empty() )
    cfg.testX = scaler.transform( split.testX );
  cfg.testY = split.testY;
  cfg.scaler = std::move( scaler );
  return true;
}

} // namespace

QgsClassificationMainWindow::QgsClassificationMainWindow( QgisInterface *iface, QWidget *parent )
  : QMainWindow( parent )
  , m_iface( iface )
{
  SICNU_LOG_INFO( SicnuLogTags::Classification, QStringLiteral( "Classification window opened" ) );
  setWindowTitle( tr( "Classification · 监督分类" ) );
  setWhatsThis( SicnuDialogHelp::htmlForTool( QStringLiteral( "classification" ), windowTitle() ) );
  setToolTip( SicnuDialogHelp::shortForTool( QStringLiteral( "classification" ), windowTitle() ) );
  resize( 1280, 800 );

  m_rois = new RsRoiCollection( this );

  // Seed default 6 classes per UI/design.html ArtboardClassify spec.
  const QList<QPair<int, QPair<QString, QString>>> defaults = {
    { 1, { tr( "林地" ), QStringLiteral( "#2da44e" ) } },
    { 2, { tr( "草地" ), QStringLiteral( "#a3e635" ) } },
    { 3, { tr( "水体" ), QStringLiteral( "#0969da" ) } },
    { 4, { tr( "建成区" ), QStringLiteral( "#cf222e" ) } },
    { 5, { tr( "耕地" ), QStringLiteral( "#d29922" ) } },
    { 6, { tr( "裸地" ), QStringLiteral( "#8a92a0" ) } },
  };
  for ( const auto &d : defaults )
  {
    m_rois->setClassDef( RsClassDef( d.first, d.second.first, QColor( d.second.second ) ) );
  }

  m_canvas = new QgsMapCanvas( this );
  m_canvas->setObjectName( QStringLiteral( "rsClassifyCanvas" ) );
  m_canvas->setCanvasColor( Qt::white );
  setCentralWidget( m_canvas );

  m_sessionMap = new RsSessionMapWorkspace( m_canvas, this );

  setupLayerManager();
  setupMenus();
  setupToolbars();
  setupDocks();
  setupSampleVectorEditing();
  setupClassifierBar();
  setupWorkflowUi();
  setupStatusBar();

  // Phase 10A review patch — JM matrix recompute throttle. m_rois::changed
  // restarts the timer; on fire, recomputeJmMatrix runs once.
  m_jmRecomputeTimer = new QTimer( this );
  m_jmRecomputeTimer->setSingleShot( true );
  m_jmRecomputeTimer->setInterval( 500 );
  connect( m_jmRecomputeTimer, &QTimer::timeout,
           this, &QgsClassificationMainWindow::recomputeJmMatrix );

  // Phase 10A review patch — feed dead controls.
  if ( m_rois )
  {
    connect( m_rois, &RsRoiCollection::changed,
             this, &QgsClassificationMainWindow::recomputeSpectralCurves );
    connect( m_rois, &RsRoiCollection::changed,
             this, [this]() {
               if ( m_jmRecomputeTimer )
                 m_jmRecomputeTimer->start();
             } );
    // Classification v1.1 — mark session dirty on ROI / class changes.
    // Suppressed during load/save so those paths can clearDirty cleanly.
    connect( m_rois, &RsRoiCollection::changed, this, [this]() {
      if ( !mSuppressDirty )
        mSession.markDirty();
    } );
    // Workflow shell — keep soft-gate stats in sync with ROI / class edits.
    connect( m_rois, &RsRoiCollection::changed,
             this, &QgsClassificationMainWindow::syncWorkflowFromRois );
    connect( m_rois, &RsRoiCollection::classDefChanged,
             this, [this]( int ) { syncWorkflowFromRois(); } );
    connect( m_rois, &RsRoiCollection::changed,
             this, &QgsClassificationMainWindow::updateRoiStatusLabels );
    connect( m_rois, &RsRoiCollection::classDefChanged,
             this, [this]( int ) {
               applySampleLayerRenderer();
               syncWorkflowFromRois();
             } );
  }
  if ( m_classTableWidget && m_spectralCurve )
  {
    connect( m_classTableWidget, &RsClassTableWidget::currentClassChanged,
             m_spectralCurve, &RsSpectralCurveWidget::setSelectedClass );
  }

  // Restore window geometry + last workflow prefs (kind, ratio, paths).
  mSession.restoreWindow( this );
  applyWorkflowSnapshot( mSession.restoreWorkflow() );
  mSession.clearDirty();

  // Seed controller from default classes / empty ROIs after UI is up.
  syncWorkflowFromRois();
  refreshWorkflowUi();

  // Default tool: add polygon (editing already on for sample layer).
  if ( m_addPolygonAction )
    m_addPolygonAction->setChecked( true );
  updateRoiStatusLabels();
}

QgsClassificationMainWindow::~QgsClassificationMainWindow()
{
  if ( m_workflowBridge )
    m_workflowBridge->close();

  // Tear down map interaction while canvas, CAD dock, and session layers are
  // still alive. Sibling docks (e.g. CAD) may be destroyed before the canvas;
  // ~QgsMapCanvas would then deactivate tools with a dangling CAD pointer.
  // Session store (on m_sessionMap) may also outlive or die before the canvas
  // depending on child order — drop canvas layer refs first.
  // Note: setMapTool(nullptr) is a no-op in QgsMapCanvas; use unsetMapTool.
  if ( m_canvas )
  {
    if ( QgsMapTool *tool = m_canvas->mapTool() )
      m_canvas->unsetMapTool( tool );
    m_canvas->setLayers( QList<QgsMapLayer *>() );
    m_canvas->setCurrentLayer( nullptr );
  }
}

RsClassifySessionState::WorkflowSnapshot
QgsClassificationMainWindow::captureWorkflowSnapshot() const
{
  RsClassifySessionState::WorkflowSnapshot s;
  s.lastSourcePath = m_sourceRasterPath;
  s.lastOutputPath = m_classifierBar ? m_classifierBar->outputPath() : QString();
  s.lastRoisPath = mSession.lastRoisPath();
  s.lastModelPath = mLastModelPath;
  s.classifierKind = m_classifierBar
                       ? static_cast<int>( m_classifierBar->currentKind() )
                       : 0;
  s.trainRatio = m_classifierBar ? m_classifierBar->trainRatio() : 0.7;
  s.wandTolerance = m_toolMagicWand ? m_toolMagicWand->tolerance() : 20.0;
  return s;
}

void QgsClassificationMainWindow::applyWorkflowSnapshot(
  const RsClassifySessionState::WorkflowSnapshot &s )
{
  if ( m_classifierBar )
  {
    m_classifierBar->setCurrentKind(
      static_cast<RsClassifierKind>( s.classifierKind ) );
    m_classifierBar->setTrainRatio( s.trainRatio );
    if ( !s.lastOutputPath.isEmpty() )
      m_classifierBar->setOutputPath( s.lastOutputPath );
  }
  if ( m_toolMagicWand )
    m_toolMagicWand->setTolerance( s.wandTolerance );
  if ( !s.lastRoisPath.isEmpty() )
    mSession.setLastRoisPath( s.lastRoisPath );
  mLastModelPath = s.lastModelPath;
  // lastSourcePath is remembered for the next open dialog start; auto-open
  // of the previous raster is intentionally not done (YAGNI / avoid surprise).
}

bool QgsClassificationMainWindow::saveRoisToPath( QString path )
{
  if ( !m_rois || m_rois->size() == 0 )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "无 ROI 可导出" ), 4000 );
    return false;
  }
  if ( path.isEmpty() )
    path = mSession.lastRoisPath();
  if ( path.isEmpty() )
  {
    path = QFileDialog::getSaveFileName(
      this, tr( "Export ROIs" ), QString(),
      tr( "ESRI Shapefile (*.shp)" ) );
  }
  if ( path.isEmpty() )
    return false;
  if ( !path.endsWith( QLatin1String( ".shp" ), Qt::CaseInsensitive ) )
    path += QStringLiteral( ".shp" );

  QgsCoordinateReferenceSystem crs;
  if ( m_sourceLayer )
    crs = m_sourceLayer->crs();

  mSuppressDirty = true;
  const bool ok = RsRoiIO::save( path, *m_rois, crs );
  mSuppressDirty = false;
  if ( ok )
  {
    mSession.setLastRoisPath( path );
    mSession.clearDirty();
    SICNU_LOG_INFO( SicnuLogTags::Classification,
                    QString( "ROIs exported: %1 (%2 ROIs)" )
                      .arg( QFileInfo( path ).fileName() )
                      .arg( m_rois->size() ) );
    if ( statusBar() )
      statusBar()->showMessage(
        tr( "已导出 ROI: %1" ).arg( QFileInfo( path ).fileName() ), 5000 );
  }
  else
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification,
                     QString( "ROI export failed: %1" ).arg( path ) );
    if ( statusBar() )
      statusBar()->showMessage( tr( "ROI 导出失败: %1" ).arg( path ), 5000 );
  }
  return ok;
}

void QgsClassificationMainWindow::closeEvent( QCloseEvent *e )
{
  if ( mSession.isDirty() )
  {
    const auto ans = QMessageBox::question(
      this, tr( "未保存的 ROI" ),
      tr( "ROI / 类别有未保存的更改。是否保存？" ),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save );
    if ( ans == QMessageBox::Cancel )
    {
      e->ignore();
      return;
    }
    if ( ans == QMessageBox::Save )
    {
      if ( !saveRoisToPath( mSession.lastRoisPath() ) )
      {
        // User cancelled dialog or write failed — keep window open.
        e->ignore();
        return;
      }
    }
  }

  mSession.saveWorkflow( captureWorkflowSnapshot() );
  mSession.saveWindow( this );
  e->accept();
}

void QgsClassificationMainWindow::setupMenus()
{
  auto *fileMenu = menuBar()->addMenu( tr( "File" ) );
  m_openRasterAction = fileMenu->addAction(
    tr( "Open source raster..." ), this,
    static_cast<bool ( QgsClassificationMainWindow::* )()>(
      &QgsClassificationMainWindow::openSourceRaster ) );
  m_openRasterAction->setObjectName( QStringLiteral( "rsClassifyOpenRasterAction" ) );
  fileMenu->addAction( tr( "Load classifier model..." ), this,
                       &QgsClassificationMainWindow::loadClassifierModel );
  fileMenu->addAction( tr( "Load ROIs..." ), this,
                       &QgsClassificationMainWindow::loadRois );
  fileMenu->addAction( tr( "Save ROIs..." ), this,
                       &QgsClassificationMainWindow::exportRois );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Save classification project..." ), this, [this]() {
    saveClassificationProject();
  } );
  fileMenu->addAction( tr( "Load classification project..." ), this, [this]() {
    loadProjectFromFile();
  } );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Close" ), this, &QWidget::close );

  menuBar()->addMenu( tr( "Edit" ) );

  auto *viewMenu = menuBar()->addMenu( tr( "View" ) );
  // Layer dock toggle filled after setupLayerManager / setupDocks — connect later if needed.

  auto *procMenu = menuBar()->addMenu( tr( "处理(&P)" ) );
  procMenu->setObjectName( QStringLiteral( "rsClassifyProcessingMenu" ) );

  auto *ppMenu = procMenu->addMenu( tr( "分类后处理" ) );
  ppMenu->setObjectName( QStringLiteral( "rsClassifyPostProcessMenu" ) );
  ppMenu->setToolTip( tr( "每个算法独立对话框；默认加载结果到本窗口图层管理" ) );

  auto addPp = [this, ppMenu]( RsPostProcessDialog::Algorithm a ) {
    auto *act = ppMenu->addAction( RsPostProcessDialog::algorithmTitle( a ), this, [this, a]() {
      openPostProcessDialog( static_cast<int>( a ) );
    } );
    act->setObjectName( QStringLiteral( "rsClassifyPp_%1" ).arg( static_cast<int>( a ) ) );
  };
  addPp( RsPostProcessDialog::Algorithm::Sieve );
  addPp( RsPostProcessDialog::Algorithm::Majority );
  addPp( RsPostProcessDialog::Algorithm::Clump );
  addPp( RsPostProcessDialog::Algorithm::Recode );
  addPp( RsPostProcessDialog::Algorithm::Polygonize );

  procMenu->addSeparator();
  procMenu->addAction( tr( "快速预览" ), this, &QgsClassificationMainWindow::applyPreview );
  procMenu->addAction( tr( "训练并分类…" ), this, &QgsClassificationMainWindow::applyClassification );
  procMenu->addAction( tr( "交叉验证" ), this, &QgsClassificationMainWindow::runCrossValidation );

  menuBar()->addMenu( tr( "Help" ) );
}

void QgsClassificationMainWindow::setupToolbars()
{
  // QGIS-style sample editing toolbar (same model as main-window vector edit).
  auto *roiBar = addToolBar( tr( "样本编辑" ) );
  roiBar->setObjectName( QStringLiteral( "rsClassifyRoiBar" ) );
  roiBar->setMovable( false );
  roiBar->setToolTip( tr(
    "与主窗口矢量编辑一致：切换编辑 → 添加多边形 → 双击结束；"
    "选择后可删除；样本显示在矢量图层上。" ) );

  auto *toolPan = roiBar->addAction( tr( "漫游" ) );
  toolPan->setObjectName( QStringLiteral( "rsToolSamplePan" ) );
  toolPan->setCheckable( true );
  toolPan->setToolTip( tr( "漫游 / 拖动画布" ) );

  auto *toolSelect = roiBar->addAction( tr( "选择要素" ) );
  toolSelect->setObjectName( QStringLiteral( "rsToolSampleSelect" ) );
  toolSelect->setCheckable( true );
  toolSelect->setToolTip( tr( "点击选择样本要素（Shift 加选，Ctrl 切换）" ) );

  roiBar->addSeparator();

  m_toggleEditAction = roiBar->addAction( tr( "切换编辑" ) );
  m_toggleEditAction->setObjectName( QStringLiteral( "rsToolSampleToggleEdit" ) );
  m_toggleEditAction->setCheckable( true );
  m_toggleEditAction->setChecked( true );
  m_toggleEditAction->setToolTip( tr( "开启/关闭样本矢量层编辑（与 QGIS 一致）" ) );

  m_addPolygonAction = roiBar->addAction( tr( "添加多边形" ) );
  m_addPolygonAction->setObjectName( QStringLiteral( "rsToolSampleAddPolygon" ) );
  m_addPolygonAction->setCheckable( true );
  m_addPolygonAction->setToolTip( tr(
    "数字化多边形样本：左键加点，右键/双击结束。"
    "属性 cls_id 自动取当前类别。" ) );

  m_deleteSelectedAction = roiBar->addAction( tr( "删除选中" ) );
  m_deleteSelectedAction->setObjectName( QStringLiteral( "rsToolSampleDelete" ) );
  m_deleteSelectedAction->setToolTip( tr( "删除选中的样本要素" ) );

  auto *toolMagic = roiBar->addAction( tr( "魔棒" ) );
  toolMagic->setObjectName( QStringLiteral( "rsToolRoiMagicWand" ) );
  toolMagic->setCheckable( true );
  toolMagic->setToolTip( tr( "可选：容差生长后写入样本矢量层（非标准 QGIS 编辑）" ) );

  roiBar->addSeparator();
  m_trainRoleAction = roiBar->addAction( tr( "训练样本" ) );
  m_trainRoleAction->setObjectName( QStringLiteral( "rsToolTrainRole" ) );
  m_trainRoleAction->setCheckable( true );
  m_trainRoleAction->setChecked( true );
  m_validRoleAction = roiBar->addAction( tr( "验证样本" ) );
  m_validRoleAction->setObjectName( QStringLiteral( "rsToolValidRole" ) );
  m_validRoleAction->setCheckable( true );
  auto *sampleRoleGroup = new QActionGroup( this );
  sampleRoleGroup->setExclusive( true );
  sampleRoleGroup->addAction( m_trainRoleAction );
  sampleRoleGroup->addAction( m_validRoleAction );
  connect( m_trainRoleAction, &QAction::triggered, this, [this]() {
    setActiveSampleRole( true );
  } );
  connect( m_validRoleAction, &QAction::triggered, this, [this]() {
    setActiveSampleRole( false );
  } );

  roiBar->addSeparator();
  auto *actSpectra = roiBar->addAction( tr( "Spectra" ) );
  actSpectra->setObjectName( QStringLiteral( "rsToolSpectra" ) );
  actSpectra->setCheckable( true );
  auto *actSeparability = roiBar->addAction( tr( "Separability" ) );
  actSeparability->setObjectName( QStringLiteral( "rsToolSeparability" ) );
  actSeparability->setCheckable( true );
  auto *actExport = roiBar->addAction( tr( "Export ROIs" ) );
  actExport->setObjectName( QStringLiteral( "rsToolExportRois" ) );

  auto *spacer = new QWidget( this );
  spacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
  roiBar->addWidget( spacer );

  auto *preview = roiBar->addAction( tr( "Quick preview" ) );
  preview->setObjectName( QStringLiteral( "rsClassifyPreviewAction" ) );
  auto *apply = roiBar->addAction( tr( "Apply classification..." ) );
  apply->setObjectName( QStringLiteral( "rsClassifyApplyAction" ) );
}

void QgsClassificationMainWindow::setupLayerManager()
{
  // Dock + view only; store/tree/model/bridge live on m_sessionMap.
  m_layerTreeDock = new QDockWidget( tr( "图层" ), this );
  m_layerTreeDock->setObjectName( QStringLiteral( "rsClassifyLayerTreeDock" ) );
  m_layerTreeView = new QgsLayerTreeView( m_layerTreeDock );
  m_layerTreeView->setObjectName( QStringLiteral( "rsClassifyLayerTreeView" ) );
  if ( m_sessionMap && m_sessionMap->layerTreeModel() )
  {
    m_layerTreeView->setModel( m_sessionMap->layerTreeModel() );
    m_layerTreeView->setLayerTreeModel( m_sessionMap->layerTreeModel() );
  }
  m_layerTreeDock->setWidget( m_layerTreeView );
  addDockWidget( Qt::LeftDockWidgetArea, m_layerTreeDock );
}

void QgsClassificationMainWindow::addSessionLayer( QgsMapLayer *layer, bool insertOnTop )
{
  if ( !m_sessionMap )
    return;
  m_sessionMap->addLayer( layer, insertOnTop );
  if ( m_layerTreeView )
    m_layerTreeView->expandAll();
}

void QgsClassificationMainWindow::removeSessionLayer( QgsMapLayer *layer )
{
  if ( !m_sessionMap )
    return;
  m_sessionMap->removeLayer( layer );
}

void QgsClassificationMainWindow::setupDocks()
{
  // View menu: layer dock toggle
  if ( QMenu *viewMenu = menuBar()->findChild<QMenu *>( QString(), Qt::FindDirectChildrenOnly ) )
  {
    // Prefer the View menu we just created by title scan
  }
  for ( QAction *a : menuBar()->actions() )
  {
    if ( a->menu() && a->text().contains( tr( "View" ) ) )
    {
      if ( m_layerTreeDock )
        a->menu()->addAction( m_layerTreeDock->toggleViewAction() );
      break;
    }
  }

  m_classListDock = new QDockWidget( tr( "类别管理" ), this );
  m_classListDock->setObjectName( QStringLiteral( "rsClassListDock" ) );
  m_classTableWidget = new RsClassTableWidget( m_classListDock );
  m_classTableWidget->setRoiCollection( m_rois );
  m_classListDock->setWidget( m_classTableWidget );
  addDockWidget( Qt::RightDockWidgetArea, m_classListDock );
  m_classListDock->resize( 380, m_classListDock->height() );

  m_classQuickListDock = new QDockWidget( tr( "类别快览" ), this );
  m_classQuickListDock->setObjectName( QStringLiteral( "rsClassQuickListDock" ) );
  m_classQuickListWidget = new RsClassQuickList( m_classQuickListDock );
  m_classQuickListWidget->setRoiCollection( m_rois );
  m_classQuickListDock->setWidget( m_classQuickListWidget );
  addDockWidget( Qt::LeftDockWidgetArea, m_classQuickListDock );

  m_jmDock = new QDockWidget( tr( "JM 分离度" ), this );
  m_jmDock->setObjectName( QStringLiteral( "rsClassJmDock" ) );
  m_jmMatrix = new RsJmMatrixWidget( m_jmDock );
  m_jmDock->setWidget( m_jmMatrix );
  addDockWidget( Qt::RightDockWidgetArea, m_jmDock );

  m_spectralDock = new QDockWidget( tr( "光谱曲线" ), this );
  m_spectralDock->setObjectName( QStringLiteral( "rsClassSpectralDock" ) );
  m_spectralCurve = new RsSpectralCurveWidget( m_spectralDock );
  m_spectralDock->setWidget( m_spectralCurve );
  addDockWidget( Qt::BottomDockWidgetArea, m_spectralDock );
}

void QgsClassificationMainWindow::setupStatusBar()
{
  auto *crsLabel = new QLabel( tr( "CRS: —" ), this );
  crsLabel->setObjectName( QStringLiteral( "rsClassifyCrsLabel" ) );
  auto *roiCountLabel = new QLabel( tr( "总 ROI: 0, 像元: 0" ), this );
  roiCountLabel->setObjectName( QStringLiteral( "rsClassifyRoiCountLabel" ) );
  statusBar()->addPermanentWidget( crsLabel );
  statusBar()->addPermanentWidget( roiCountLabel );
}

void QgsClassificationMainWindow::setupSampleVectorEditing()
{
  ensureSampleLayer();

  // CAD dock is required by QgsMapToolDigitizeFeature (Q_ASSERT).
  m_cadDock = new QgsAdvancedDigitizingDockWidget( m_canvas, this );
  m_cadDock->setObjectName( QStringLiteral( "rsClassifyCadDock" ) );
  addDockWidget( Qt::LeftDockWidgetArea, m_cadDock );
  m_cadDock->hide();

  m_toolPan = new QgsMapToolPan( m_canvas );
  m_toolSelect = new SampleSelectTool( m_canvas, &m_sampleLayer );
  m_toolAddPolygon = new QgsMapToolDigitizeFeature(
    m_canvas, m_cadDock, QgsMapToolCapture::CapturePolygon );
  m_toolAddPolygon->setLayer( m_sampleLayer );
  connect( m_toolAddPolygon, &QgsMapToolDigitizeFeature::digitizingCompleted,
           this, &QgsClassificationMainWindow::onSampleDigitized );

  m_toolMagicWand = new RsRoiToolMagicWand( m_canvas );
  m_toolMagicWand->setSourceData( m_sourceRasterPath );
  connect( m_toolMagicWand, &RsRoiToolBase::roiDrawn,
           this, &QgsClassificationMainWindow::onMagicWandRoi );

  auto *group = new QActionGroup( this );
  group->setExclusive( true );

  auto bindTool = [this, group]( const QString &objName, QgsMapTool *tool ) {
    QAction *a = findChild<QAction *>( objName );
    if ( !a || !tool )
      return;
    a->setCheckable( true );
    group->addAction( a );
    connect( a, &QAction::toggled, this, [this, tool]( bool on ) {
      if ( !m_canvas )
        return;
      if ( on )
        m_canvas->setMapTool( tool );
      else if ( m_canvas->mapTool() == tool )
        m_canvas->unsetMapTool( tool );
    } );
  };
  bindTool( QStringLiteral( "rsToolSamplePan" ), m_toolPan );
  bindTool( QStringLiteral( "rsToolSampleSelect" ), m_toolSelect );
  bindTool( QStringLiteral( "rsToolSampleAddPolygon" ), m_toolAddPolygon );
  bindTool( QStringLiteral( "rsToolRoiMagicWand" ), m_toolMagicWand );

  if ( m_toggleEditAction )
  {
    connect( m_toggleEditAction, &QAction::toggled,
             this, &QgsClassificationMainWindow::onToggleEditing );
  }
  if ( m_deleteSelectedAction )
  {
    connect( m_deleteSelectedAction, &QAction::triggered,
             this, &QgsClassificationMainWindow::deleteSelectedSamples );
  }

  if ( m_classTableWidget )
  {
    connect( m_classTableWidget, &RsClassTableWidget::currentClassChanged,
             this, &QgsClassificationMainWindow::onCurrentClassChanged );
  }
  if ( m_classQuickListWidget )
  {
    connect( m_classQuickListWidget, &RsClassQuickList::currentClassChanged,
             this, &QgsClassificationMainWindow::onCurrentClassChanged );
  }

  ensureSampleLayerEditing( true );
  if ( m_sessionMap )
    m_sessionMap->setCurrentLayer( m_sampleLayer );
}

void QgsClassificationMainWindow::ensureSampleLayer()
{
  if ( m_sampleLayer )
    return;

  QgsFields fields;
  fields.append( QgsField( QStringLiteral( "cls_id" ), QMetaType::Type::Int ) );
  fields.append( QgsField( QStringLiteral( "cls_name" ), QMetaType::Type::QString ) );
  fields.append( QgsField( QStringLiteral( "px_count" ), QMetaType::Type::LongLong ) );

  QgsCoordinateReferenceSystem crs;
  if ( m_sourceLayer && m_sourceLayer->crs().isValid() )
    crs = m_sourceLayer->crs();
  else
    crs = QgsCoordinateReferenceSystem::fromEpsgId( 4326 );

  m_sampleLayer = QgsMemoryProviderUtils::createMemoryLayer(
    tr( "训练样本" ), fields, Qgis::WkbType::Polygon, crs );
  m_sampleLayer->setObjectName( QStringLiteral( "rsClassifySampleLayer" ) );
  applySampleLayerRenderer();
  addSessionLayer( m_sampleLayer, true );

  connect( m_sampleLayer, &QgsVectorLayer::featureAdded,
           this, [this]( QgsFeatureId ) { onSampleLayerEdited(); } );
  connect( m_sampleLayer, &QgsVectorLayer::featureDeleted,
           this, [this]( QgsFeatureId ) { onSampleLayerEdited(); } );
  connect( m_sampleLayer, &QgsVectorLayer::geometryChanged,
           this, [this]( QgsFeatureId, const QgsGeometry & ) { onSampleLayerEdited(); } );
  connect( m_sampleLayer, &QgsVectorLayer::committedFeaturesAdded,
           this, [this]( const QString &, const QgsFeatureList & ) { onSampleLayerEdited(); } );
  connect( m_sampleLayer, &QgsVectorLayer::committedFeaturesRemoved,
           this, [this]( const QString &, const QgsFeatureIds & ) { onSampleLayerEdited(); } );
  connect( m_sampleLayer, &QgsVectorLayer::editingStopped,
           this, &QgsClassificationMainWindow::onSampleLayerEdited );
}

void QgsClassificationMainWindow::applySampleLayerRenderer()
{
  if ( !m_sampleLayer || !m_rois )
    return;

  QgsCategoryList cats;
  const QHash<int, RsClassDef> defs = m_rois->classDefs();
  QList<int> ids = defs.keys();
  std::sort( ids.begin(), ids.end() );
  for ( int id : ids )
  {
    const RsClassDef d = defs.value( id );
    std::unique_ptr<QgsSymbol> sym(
      QgsSymbol::defaultSymbol( Qgis::GeometryType::Polygon ) );
    if ( !sym )
      continue;
    QColor c = d.color();
    c.setAlpha( 110 );
    sym->setColor( c );
    if ( auto *fill = dynamic_cast<QgsFillSymbol *>( sym.get() ) )
    {
      fill->setColor( c );
    }
    cats.append( QgsRendererCategory( id, sym.release(), d.name() ) );
  }
  auto *renderer = new QgsCategorizedSymbolRenderer( QStringLiteral( "cls_id" ), cats );
  m_sampleLayer->setRenderer( renderer );
  m_sampleLayer->triggerRepaint();
  if ( m_canvas )
    m_canvas->refresh();
}



void QgsClassificationMainWindow::ensureSampleLayerEditing( bool on )
{
  ensureSampleLayer();
  if ( !m_sampleLayer )
    return;
  if ( on )
  {
    if ( !m_sampleLayer->isEditable() )
      m_sampleLayer->startEditing();
  }
  else if ( m_sampleLayer->isEditable() )
  {
    m_sampleLayer->commitChanges();
  }
  if ( m_toggleEditAction )
    m_toggleEditAction->setChecked( m_sampleLayer->isEditable() );
}

void QgsClassificationMainWindow::onToggleEditing( bool on )
{
  ensureSampleLayerEditing( on );
  if ( statusBar() )
  {
    statusBar()->showMessage(
      on ? tr( "样本层编辑已开启" ) : tr( "样本层编辑已关闭（已提交）" ), 2500 );
  }
}

void QgsClassificationMainWindow::deleteSelectedSamples()
{
  ensureSampleLayer();
  if ( !m_sampleLayer )
    return;
  if ( !m_sampleLayer->isEditable() )
    ensureSampleLayerEditing( true );
  if ( m_sampleLayer->selectedFeatureIds().isEmpty() )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "未选中样本要素" ), 2500 );
    return;
  }
  m_sampleLayer->beginEditCommand( tr( "删除样本" ) );
  m_sampleLayer->deleteSelectedFeatures();
  m_sampleLayer->endEditCommand();
  m_sampleLayer->triggerRepaint();
  onSampleLayerEdited();
  if ( m_canvas )
    m_canvas->refresh();
}

void QgsClassificationMainWindow::rebuildRoisFromSampleLayer()
{
  if ( !m_rois || !m_sampleLayer || mSuppressSampleSync )
    return;

  mSuppressSampleSync = true;
  // Keep class defs; replace geometries.
  const QHash<int, RsClassDef> defs = m_rois->classDefs();
  m_rois->clear();
  for ( auto it = defs.constBegin(); it != defs.constEnd(); ++it )
    m_rois->setClassDef( it.value() );

  QgsFeatureRequest req;
  req.setFlags( Qgis::FeatureRequestFlag::NoGeometry );
  // Need geometry for rasterize
  req = QgsFeatureRequest();

  QgsFeatureIterator it = m_sampleLayer->getFeatures( req );
  QgsFeature f;
  while ( it.nextFeature( f ) )
  {
    if ( !f.hasGeometry() || f.geometry().isEmpty() )
      continue;
    const int classId = f.attribute( QStringLiteral( "cls_id" ) ).toInt();
    if ( classId <= 0 )
      continue;
    QSet<quint64> pixels;
    if ( m_sourceWidth > 0 && m_sourceHeight > 0 )
    {
      pixels = RsPixelRasterizer::rasterize(
        f.geometry(), m_sourceGt, m_sourceWidth, m_sourceHeight );
    }
    QVector<quint64> idx( pixels.begin(), pixels.end() );
    m_rois->appendRoi( RsRoi( classId, f.geometry(), idx ) );
  }
  mSuppressSampleSync = false;
  updateRoiStatusLabels();
}

void QgsClassificationMainWindow::syncSampleLayerFromRois()
{
  ensureSampleLayer();
  if ( !m_sampleLayer || !m_rois )
    return;

  mSuppressSampleSync = true;
  const bool wasEditable = m_sampleLayer->isEditable();
  if ( !wasEditable )
    m_sampleLayer->startEditing();

  // Clear all features
  QgsFeatureIds all;
  QgsFeatureIterator it = m_sampleLayer->getFeatures( QgsFeatureRequest().setNoAttributes() );
  QgsFeature f;
  while ( it.nextFeature( f ) )
    all.insert( f.id() );
  if ( !all.isEmpty() )
    m_sampleLayer->deleteFeatures( all );

  for ( const RsRoi &roi : m_rois->rois() )
  {
    if ( roi.geometry().isEmpty() )
      continue;
    QgsFeature nf( m_sampleLayer->fields() );
    nf.setGeometry( roi.geometry() );
    nf.setAttribute( QStringLiteral( "cls_id" ), roi.classId() );
    QString name;
    if ( m_rois->classDefs().contains( roi.classId() ) )
      name = m_rois->classDef( roi.classId() ).name();
    nf.setAttribute( QStringLiteral( "cls_name" ), name );
    nf.setAttribute( QStringLiteral( "px_count" ),
                     static_cast<qint64>( roi.pixelIndices().size() ) );
    m_sampleLayer->addFeature( nf );
  }

  if ( !wasEditable )
    m_sampleLayer->commitChanges();
  else
    m_sampleLayer->triggerRepaint();

  applySampleLayerRenderer();
  mSuppressSampleSync = false;
  if ( m_canvas )
    m_canvas->refresh();
  updateRoiStatusLabels();
}

void QgsClassificationMainWindow::onSampleLayerEdited()
{
  if ( mSuppressSampleSync )
    return;
  rebuildRoisFromSampleLayer();
}

void QgsClassificationMainWindow::onSampleDigitized( const QgsFeature &feature )
{
  ensureSampleLayer();
  if ( !m_sampleLayer )
    return;

  const int classId = resolveActiveClassId();
  if ( classId <= 0 )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "请先在类别表中选一个类别" ), 3000 );
    return;
  }
  if ( !feature.hasGeometry() || feature.geometry().isEmpty() )
    return;

  if ( !m_sampleLayer->isEditable() )
    ensureSampleLayerEditing( true );

  QgsFeature feat( m_sampleLayer->fields() );
  feat.setGeometry( feature.geometry() );
  QString name;
  if ( m_rois && m_rois->classDefs().contains( classId ) )
    name = m_rois->classDef( classId ).name();
  feat.setAttribute( QStringLiteral( "cls_id" ), classId );
  feat.setAttribute( QStringLiteral( "cls_name" ), name );

  QSet<quint64> pixels;
  if ( m_sourceWidth > 0 && m_sourceHeight > 0 )
  {
    pixels = RsPixelRasterizer::rasterize(
      feature.geometry(), m_sourceGt, m_sourceWidth, m_sourceHeight );
  }
  feat.setAttribute( QStringLiteral( "px_count" ),
                     static_cast<qint64>( pixels.size() ) );

  m_sampleLayer->beginEditCommand( tr( "添加训练样本" ) );
  const bool ok = m_sampleLayer->addFeature( feat );
  m_sampleLayer->endEditCommand();
  if ( !ok )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "添加样本失败" ), 3000 );
    return;
  }
  m_sampleLayer->triggerRepaint();
  if ( m_canvas )
    m_canvas->refresh();
  // featureAdded signal rebuilds collection
  if ( statusBar() )
  {
    statusBar()->showMessage(
      tr( "已添加样本 → %1（%2 像元）" ).arg( name ).arg( pixels.size() ), 3000 );
  }
}

void QgsClassificationMainWindow::onMagicWandRoi( const QgsGeometry &geom, int classId )
{
  // Reuse digitize path: inject geometry as a completed feature.
  QgsFeature f;
  f.setGeometry( geom );
  if ( classId > 0 && m_toolMagicWand )
    m_toolMagicWand->setCurrentClassId( classId );
  // Temporarily force class if tool had 0
  if ( classId <= 0 )
    classId = resolveActiveClassId();
  if ( classId > 0 && m_classTableWidget && m_classTableWidget->currentClassId() != classId )
    m_classTableWidget->setCurrentClassId( classId );
  onSampleDigitized( f );
}

void QgsClassificationMainWindow::onCurrentClassChanged( int classId )
{
  if ( classId <= 0 )
    return;
  if ( m_toolMagicWand )
    m_toolMagicWand->setCurrentClassId( classId );
  if ( m_classTableWidget && m_classTableWidget->currentClassId() != classId )
    m_classTableWidget->setCurrentClassId( classId );
  if ( m_classQuickListWidget && m_classQuickListWidget->currentClassId() != classId )
    m_classQuickListWidget->setCurrentClassId( classId );
  if ( m_spectralCurve )
    m_spectralCurve->setSelectedClass( classId );
}

int QgsClassificationMainWindow::resolveActiveClassId( int preferred ) const
{
  if ( preferred > 0 )
    return preferred;
  if ( m_classTableWidget )
  {
    const int id = m_classTableWidget->currentClassId();
    if ( id > 0 )
      return id;
  }
  if ( m_classQuickListWidget )
  {
    const int id = m_classQuickListWidget->currentClassId();
    if ( id > 0 )
      return id;
  }
  return 0;
}

void QgsClassificationMainWindow::updateRoiStatusLabels()
{
  auto *roiCountLabel = findChild<QLabel *>( QStringLiteral( "rsClassifyRoiCountLabel" ) );
  if ( !roiCountLabel )
    return;
  int roiN = 0;
  quint64 pxN = 0;
  if ( m_rois )
  {
    roiN = m_rois->size();
    for ( const RsRoi &r : m_rois->rois() )
      pxN += static_cast<quint64>( r.pixelIndices().size() );
  }
  roiCountLabel->setText( tr( "总样本: %1, 像元: %2" ).arg( roiN ).arg( pxN ) );

  auto *crsLabel = findChild<QLabel *>( QStringLiteral( "rsClassifyCrsLabel" ) );
  if ( crsLabel )
  {
    QString crsText = tr( "CRS: —" );
    if ( m_sampleLayer && m_sampleLayer->crs().isValid() )
      crsText = tr( "CRS: %1" ).arg( m_sampleLayer->crs().authid() );
    else if ( m_sourceLayer && m_sourceLayer->crs().isValid() )
      crsText = tr( "CRS: %1" ).arg( m_sourceLayer->crs().authid() );
    crsLabel->setText( crsText );
  }
}

void QgsClassificationMainWindow::setupClassifierBar()
{
  auto *bar = addToolBar( tr( "Classifier" ) );
  bar->setObjectName( QStringLiteral( "rsClassifierBar" ) );
  bar->setMovable( false );

  m_classifierBar = new RsClassifierSetupBar( bar );
  m_classifierBar->setObjectName( QStringLiteral( "rsClassifierSetupBar" ) );
  bar->addWidget( m_classifierBar );
  addToolBar( Qt::BottomToolBarArea, bar );

  connect( m_classifierBar, &RsClassifierSetupBar::applyRequested,
           this, &QgsClassificationMainWindow::applyClassification );
  // Phase 10A review patch — bar preview / cross-validate signals.
  connect( m_classifierBar, &RsClassifierSetupBar::previewRequested,
           this, &QgsClassificationMainWindow::applyPreview );
  connect( m_classifierBar, &RsClassifierSetupBar::crossValidateRequested,
           this, &QgsClassificationMainWindow::runCrossValidation );

  // The toolbar Apply action (from Task 10.2) routes to the same slot.
  if ( auto *apply = findChild<QAction *>(
         QStringLiteral( "rsClassifyApplyAction" ) ) )
  {
    m_applyAction = apply;
    connect( apply, &QAction::triggered,
             this, &QgsClassificationMainWindow::applyClassification );
  }
  // Phase 10A review patch — toolbar Quick preview routes to applyPreview.
  if ( auto *preview = findChild<QAction *>(
         QStringLiteral( "rsClassifyPreviewAction" ) ) )
  {
    connect( preview, &QAction::triggered,
             this, &QgsClassificationMainWindow::applyPreview );
  }
  // Phase 10A review patch — Spectra/Separability toggle dock visibility,
  // Export ROIs saves to a shapefile via RsRoiIO.
  if ( auto *actSpectra = findChild<QAction *>( QStringLiteral( "rsToolSpectra" ) ) )
  {
    if ( m_spectralDock )
      actSpectra->setChecked( m_spectralDock->isVisible() );
    connect( actSpectra, &QAction::toggled, this, [this]( bool on ) {
      if ( m_spectralDock )
        m_spectralDock->setVisible( on );
    } );
    if ( m_spectralDock )
    {
      connect( m_spectralDock, &QDockWidget::visibilityChanged,
               actSpectra, &QAction::setChecked );
    }
  }
  if ( auto *actSep = findChild<QAction *>( QStringLiteral( "rsToolSeparability" ) ) )
  {
    if ( m_jmDock )
      actSep->setChecked( m_jmDock->isVisible() );
    connect( actSep, &QAction::toggled, this, [this]( bool on ) {
      if ( m_jmDock )
        m_jmDock->setVisible( on );
    } );
    if ( m_jmDock )
    {
      connect( m_jmDock, &QDockWidget::visibilityChanged,
               actSep, &QAction::setChecked );
    }
  }
  if ( auto *actExport = findChild<QAction *>( QStringLiteral( "rsToolExportRois" ) ) )
  {
    connect( actExport, &QAction::triggered,
             this, &QgsClassificationMainWindow::exportRois );
  }
}

void QgsClassificationMainWindow::setupWorkflowUi()
{
  m_workflow = new RsClassifyWorkflowController( this );

  // Runtime session for lab.classify.supervised — mirrors step/complete only.
  // Soft classify-specific gates remain on m_workflow (controller authority).
  m_workflowBridge = std::make_unique<RsClassifyWorkflowBridge>();
  if ( !m_workflowBridge->open() )
  {
    SICNU_LOG_WARN( SicnuLogTags::Classification,
                    QStringLiteral( "Failed to open workflow session lab.classify.supervised" ) );
  }

  m_stepper = new RsClassifyStepperBar( this );
  addToolBarBreak();
  auto *wfBar = addToolBar( tr( "工作流" ) );
  wfBar->setObjectName( QStringLiteral( "rsClassifyWorkflowBar" ) );
  wfBar->setMovable( false );
  wfBar->addWidget( m_stepper );

  m_stepHost = new RsClassifyStepHost( this );
  m_workflowDock = new QDockWidget( tr( "工作流步骤" ), this );
  m_workflowDock->setObjectName( QStringLiteral( "ClassifyWorkflowDock" ) );
  m_workflowDock->setWidget( m_stepHost );
  addDockWidget( Qt::RightDockWidgetArea, m_workflowDock );
  // Prefer the step host as the primary right-side panel.
  if ( m_classListDock )
    tabifyDockWidget( m_classListDock, m_workflowDock );
  m_workflowDock->raise();

  connect( m_stepper, &RsClassifyStepperBar::stepClicked, this,
           [this]( RsClassifyStep s ) {
             if ( m_workflow )
               m_workflow->setCurrentStep( s );
           } );

  connect( m_workflow, &RsClassifyWorkflowController::currentStepChanged, this,
           [this]( RsClassifyStep s ) {
             if ( m_workflowBridge )
               m_workflowBridge->gotoStep( s );
             if ( m_stepper )
               m_stepper->setCurrentStep( s );
             if ( m_stepHost )
               m_stepHost->setCurrentStep( s );
             refreshWorkflowUi();
           } );

  connect( m_stepper, &RsClassifyStepperBar::modeToggled,
           m_workflow, &RsClassifyWorkflowController::setMode );

  connect( m_workflow, &RsClassifyWorkflowController::modeChanged, this,
           [this]( RsClassifyUiMode m ) {
             if ( m_stepper )
               m_stepper->setMode( m );
             refreshWorkflowUi();
           } );

  connect( m_workflow, &RsClassifyWorkflowController::completionChanged, this, [this]() {
    if ( m_workflowBridge && m_workflow )
      m_workflowBridge->syncCompletionsFromController( *m_workflow );
    refreshWorkflowUi();
  } );

  connect( m_stepHost, &RsClassifyStepHost::prevClicked, this, [this]() {
    if ( !m_workflow )
      return;
    const int cur = static_cast<int>( m_workflow->currentStep() );
    if ( cur > 0 )
      m_workflow->setCurrentStep( static_cast<RsClassifyStep>( cur - 1 ) );
  } );
  connect( m_stepHost, &RsClassifyStepHost::nextClicked, this, [this]() {
    if ( !m_workflow )
      return;
    const int cur = static_cast<int>( m_workflow->currentStep() );
    const int last = static_cast<int>( RsClassifyStep::Count ) - 1;
    if ( cur < last )
      m_workflow->setCurrentStep( static_cast<RsClassifyStep>( cur + 1 ) );
  } );

  populateStepPanels();
}

void QgsClassificationMainWindow::populateStepPanels()
{
  if ( !m_stepHost )
    return;

  // --- Step 1: ClassSystem -------------------------------------------------
  if ( QWidget *body = m_stepHost->body( RsClassifyStep::ClassSystem ) )
  {
    auto *lay = qobject_cast<QVBoxLayout *>( body->layout() );
    if ( !lay )
    {
      lay = new QVBoxLayout( body );
      lay->setContentsMargins( 0, 4, 0, 4 );
      lay->setSpacing( 8 );
    }

    auto *btnOpen = new QPushButton( tr( "打开源影像…" ), body );
    btnOpen->setObjectName( QStringLiteral( "classifyStep1OpenRaster" ) );
    connect( btnOpen, &QPushButton::clicked, this,
             static_cast<bool ( QgsClassificationMainWindow::* )()>(
               &QgsClassificationMainWindow::openSourceRaster ) );
    lay->addWidget( btnOpen );

    m_stepClassCountLabel = new QLabel( body );
    m_stepClassCountLabel->setObjectName( QStringLiteral( "classifyStep1ClassCount" ) );
    m_stepClassCountLabel->setWordWrap( true );
    lay->addWidget( m_stepClassCountLabel );

    auto *btnDefaults = new QPushButton( tr( "添加默认 6 类" ), body );
    btnDefaults->setObjectName( QStringLiteral( "classifyStep1DefaultClasses" ) );
    connect( btnDefaults, &QPushButton::clicked, this,
             &QgsClassificationMainWindow::ensureDefaultClasses );
    lay->addWidget( btnDefaults );

    auto *btnClasses = new QPushButton( tr( "打开类别管理" ), body );
    btnClasses->setObjectName( QStringLiteral( "classifyStep1OpenClassTable" ) );
    connect( btnClasses, &QPushButton::clicked, this, [this]() {
      if ( !m_classListDock )
        return;
      m_classListDock->show();
      m_classListDock->raise();
      m_classListDock->activateWindow();
    } );
    lay->addWidget( btnClasses );

    auto *help = new QLabel(
      tr( "在类别管理中编辑名称与颜色；至少 2 个类别后可进入下一步。" ), body );
    help->setWordWrap( true );
    help->setStyleSheet( QStringLiteral( "color: #656d76;" ) );
    lay->addWidget( help );
    lay->addStretch( 1 );
  }

  // --- Step 2: Samples -----------------------------------------------------
  if ( QWidget *body = m_stepHost->body( RsClassifyStep::Samples ) )
  {
    auto *lay = qobject_cast<QVBoxLayout *>( body->layout() );
    if ( !lay )
    {
      lay = new QVBoxLayout( body );
      lay->setContentsMargins( 0, 4, 0, 4 );
      lay->setSpacing( 8 );
    }

    auto *roleRow = new QHBoxLayout;
    m_stepTrainRoleBtn = new QPushButton( tr( "训练样本" ), body );
    m_stepTrainRoleBtn->setObjectName( QStringLiteral( "classifyStep2TrainRole" ) );
    m_stepTrainRoleBtn->setCheckable( true );
    m_stepValidRoleBtn = new QPushButton( tr( "验证样本" ), body );
    m_stepValidRoleBtn->setObjectName( QStringLiteral( "classifyStep2ValidRole" ) );
    m_stepValidRoleBtn->setCheckable( true );
    auto *roleGroup = new QButtonGroup( body );
    roleGroup->setExclusive( true );
    roleGroup->addButton( m_stepTrainRoleBtn );
    roleGroup->addButton( m_stepValidRoleBtn );
    m_stepTrainRoleBtn->setChecked( true );
    connect( m_stepTrainRoleBtn, &QPushButton::clicked, this, [this]() {
      setActiveSampleRole( true );
    } );
    connect( m_stepValidRoleBtn, &QPushButton::clicked, this, [this]() {
      setActiveSampleRole( false );
    } );
    roleRow->addWidget( m_stepTrainRoleBtn );
    roleRow->addWidget( m_stepValidRoleBtn );
    lay->addLayout( roleRow );

    m_stepSampleStatsLabel = new QLabel( body );
    m_stepSampleStatsLabel->setObjectName( QStringLiteral( "classifyStep2Stats" ) );
    m_stepSampleStatsLabel->setWordWrap( true );
    lay->addWidget( m_stepSampleStatsLabel );

    auto *roiRow = new QHBoxLayout;
    auto *btnExport = new QPushButton( tr( "导出 ROI…" ), body );
    btnExport->setObjectName( QStringLiteral( "classifyStep2ExportRois" ) );
    connect( btnExport, &QPushButton::clicked, this,
             &QgsClassificationMainWindow::exportRois );
    auto *btnLoad = new QPushButton( tr( "加载 ROI…" ), body );
    btnLoad->setObjectName( QStringLiteral( "classifyStep2LoadRois" ) );
    connect( btnLoad, &QPushButton::clicked, this,
             &QgsClassificationMainWindow::loadRois );
    roiRow->addWidget( btnExport );
    roiRow->addWidget( btnLoad );
    lay->addLayout( roiRow );

    auto *note = new QLabel(
      tr( "数字化工具（点/矩形/多边形/自由绘/魔棒）在上方工具栏；"
          "先在类别快览中选中类别再勾绘。" ),
      body );
    note->setWordWrap( true );
    note->setStyleSheet( QStringLiteral( "color: #656d76;" ) );
    lay->addWidget( note );
    lay->addStretch( 1 );
  }

  // --- Step 3: Evaluate ----------------------------------------------------
  if ( QWidget *body = m_stepHost->body( RsClassifyStep::Evaluate ) )
  {
    auto *lay = qobject_cast<QVBoxLayout *>( body->layout() );
    if ( !lay )
    {
      lay = new QVBoxLayout( body );
      lay->setContentsMargins( 0, 4, 0, 4 );
      lay->setSpacing( 8 );
    }

    auto *btnSpectral = new QPushButton( tr( "重算光谱曲线" ), body );
    btnSpectral->setObjectName( QStringLiteral( "classifyStep3RecomputeSpectral" ) );
    connect( btnSpectral, &QPushButton::clicked, this, [this]() {
      recomputeSpectralCurves();
      if ( m_spectralDock )
      {
        m_spectralDock->show();
        m_spectralDock->raise();
      }
    } );
    lay->addWidget( btnSpectral );

    auto *btnJm = new QPushButton( tr( "重算 JM 分离度" ), body );
    btnJm->setObjectName( QStringLiteral( "classifyStep3RecomputeJm" ) );
    connect( btnJm, &QPushButton::clicked, this, [this]() {
      recomputeJmMatrix();
      if ( m_jmDock )
      {
        m_jmDock->show();
        m_jmDock->raise();
      }
    } );
    lay->addWidget( btnJm );

    auto *btnRaiseSpectral = new QPushButton( tr( "打开光谱曲线面板" ), body );
    btnRaiseSpectral->setObjectName( QStringLiteral( "classifyStep3RaiseSpectral" ) );
    connect( btnRaiseSpectral, &QPushButton::clicked, this, [this]() {
      if ( m_spectralDock )
      {
        m_spectralDock->show();
        m_spectralDock->raise();
      }
    } );
    lay->addWidget( btnRaiseSpectral );

    auto *btnRaiseJm = new QPushButton( tr( "打开 JM 面板" ), body );
    btnRaiseJm->setObjectName( QStringLiteral( "classifyStep3RaiseJm" ) );
    connect( btnRaiseJm, &QPushButton::clicked, this, [this]() {
      if ( m_jmDock )
      {
        m_jmDock->show();
        m_jmDock->raise();
      }
    } );
    lay->addWidget( btnRaiseJm );

    auto *btnReviewed = new QPushButton( tr( "标记已审阅" ), body );
    btnReviewed->setObjectName( QStringLiteral( "classifyStep3MarkReviewed" ) );
    connect( btnReviewed, &QPushButton::clicked, this, [this]() {
      if ( m_workflow )
        m_workflow->setEvaluateReviewed( true );
      refreshWorkflowUi();
    } );
    lay->addWidget( btnReviewed );

    auto *hint = new QLabel(
      tr( "检查 JM 与光谱可分性后点「标记已审阅」以完成本步。" ), body );
    hint->setWordWrap( true );
    hint->setStyleSheet( QStringLiteral( "color: #656d76;" ) );
    lay->addWidget( hint );
    lay->addStretch( 1 );
  }

  // --- Step 4: TrainClassify -----------------------------------------------
  if ( QWidget *body = m_stepHost->body( RsClassifyStep::TrainClassify ) )
  {
    auto *lay = qobject_cast<QVBoxLayout *>( body->layout() );
    if ( !lay )
    {
      lay = new QVBoxLayout( body );
      lay->setContentsMargins( 0, 4, 0, 4 );
      lay->setSpacing( 8 );
    }

    auto *note = new QLabel(
      tr( "分类器类型、波段与训练比例在底部 Classifier 工具栏设置。" ), body );
    note->setWordWrap( true );
    note->setStyleSheet( QStringLiteral( "color: #656d76;" ) );
    lay->addWidget( note );

    m_stepCvBtn = new QPushButton( tr( "交叉验证 (CV)" ), body );
    m_stepCvBtn->setObjectName( QStringLiteral( "classifyStep4Cv" ) );
    connect( m_stepCvBtn, &QPushButton::clicked, this,
             &QgsClassificationMainWindow::runCrossValidation );
    lay->addWidget( m_stepCvBtn );

    m_stepPreviewBtn = new QPushButton( tr( "快速预览" ), body );
    m_stepPreviewBtn->setObjectName( QStringLiteral( "classifyStep4Preview" ) );
    connect( m_stepPreviewBtn, &QPushButton::clicked, this,
             &QgsClassificationMainWindow::applyPreview );
    lay->addWidget( m_stepPreviewBtn );

    m_stepApplyBtn = new QPushButton( tr( "应用分类…" ), body );
    m_stepApplyBtn->setObjectName( QStringLiteral( "classifyStep4Apply" ) );
    connect( m_stepApplyBtn, &QPushButton::clicked, this,
             &QgsClassificationMainWindow::applyClassification );
    lay->addWidget( m_stepApplyBtn );

    auto *tip = new QLabel(
      tr( "预览仅当前视口，不计入本步完成；全图 Apply 完成后进入精度评定。" ),
      body );
    tip->setWordWrap( true );
    tip->setStyleSheet( QStringLiteral( "color: #656d76;" ) );
    lay->addWidget( tip );
    lay->addStretch( 1 );
  }

  // --- Step 5: Accuracy ----------------------------------------------------
  if ( QWidget *body = m_stepHost->body( RsClassifyStep::Accuracy ) )
  {
    auto *lay = qobject_cast<QVBoxLayout *>( body->layout() );
    if ( !lay )
    {
      lay = new QVBoxLayout( body );
      lay->setContentsMargins( 0, 4, 0, 4 );
      lay->setSpacing( 8 );
    }

    m_accuracyPanel = new RsAccuracyPanel( body );
    m_accuracyPanel->setObjectName( QStringLiteral( "classifyStep5AccuracyPanel" ) );
    lay->addWidget( m_accuracyPanel, 1 );

    m_stepAccuracyPopupBtn = new QPushButton( tr( "弹出完整窗口" ), body );
    m_stepAccuracyPopupBtn->setObjectName( QStringLiteral( "classifyStep5Popup" ) );
    m_stepAccuracyPopupBtn->setEnabled( false );
    connect( m_stepAccuracyPopupBtn, &QPushButton::clicked, this, [this]() {
      if ( !m_accuracyPanel || !m_accuracyPanel->hasResult() )
        return;
      auto *dlg = new RsAccuracyDialog(
        m_accuracyPanel->result(), m_accuracyPanel->classNames(), this );
      dlg->setAttribute( Qt::WA_DeleteOnClose );
      dlg->show();
    } );
    lay->addWidget( m_stepAccuracyPopupBtn );

    auto *hint = new QLabel(
      tr( "精度来自全图 Apply 的 holdout/验证划分；可导出 CSV 或弹出大图查看。" ),
      body );
    hint->setWordWrap( true );
    hint->setStyleSheet( QStringLiteral( "color: #656d76;" ) );
    lay->addWidget( hint );
  }

  // --- Step 6: PostProcess (dialog-driven; panel is a thin entry) ---------
  if ( QWidget *body = m_stepHost->body( RsClassifyStep::PostProcess ) )
  {
    auto *lay = qobject_cast<QVBoxLayout *>( body->layout() );
    if ( !lay )
    {
      lay = new QVBoxLayout( body );
      lay->setContentsMargins( 0, 4, 0, 4 );
      lay->setSpacing( 8 );
    }

    auto *hint = new QLabel(
      tr( "后处理为「一算法一对话框」。请用下方按钮或菜单「处理 → 分类后处理」。\n"
          "默认会将结果加载到本窗口左侧图层管理。也可跳过本步进入输出。" ),
      body );
    hint->setWordWrap( true );
    hint->setStyleSheet( QStringLiteral( "color: #656d76;" ) );
    lay->addWidget( hint );

    auto addAlgoBtn = [this, body, lay]( RsPostProcessDialog::Algorithm a ) {
      auto *btn = new QPushButton( RsPostProcessDialog::algorithmTitle( a ), body );
      btn->setObjectName( QStringLiteral( "classifyStep6Algo_%1" ).arg( static_cast<int>( a ) ) );
      connect( btn, &QPushButton::clicked, this, [this, a]() {
        openPostProcessDialog( static_cast<int>( a ) );
      } );
      lay->addWidget( btn );
    };
    addAlgoBtn( RsPostProcessDialog::Algorithm::Sieve );
    addAlgoBtn( RsPostProcessDialog::Algorithm::Majority );
    addAlgoBtn( RsPostProcessDialog::Algorithm::Clump );
    addAlgoBtn( RsPostProcessDialog::Algorithm::Recode );
    addAlgoBtn( RsPostProcessDialog::Algorithm::Polygonize );

    auto *btnSkip = new QPushButton( tr( "跳过后处理" ), body );
    btnSkip->setObjectName( QStringLiteral( "classifyStep6Skip" ) );
    connect( btnSkip, &QPushButton::clicked, this, [this]() {
      if ( m_workflow )
        m_workflow->setPostProcessSkipped( true );
      if ( statusBar() )
        statusBar()->showMessage( tr( "已跳过后处理" ), 3000 );
      refreshWorkflowUi();
    } );
    lay->addWidget( btnSkip );
    lay->addStretch( 1 );
  }

  // --- Step 7: Export ------------------------------------------------------
  if ( QWidget *body = m_stepHost->body( RsClassifyStep::Export ) )
  {
    auto *lay = qobject_cast<QVBoxLayout *>( body->layout() );
    if ( !lay )
    {
      lay = new QVBoxLayout( body );
      lay->setContentsMargins( 0, 4, 0, 4 );
      lay->setSpacing( 8 );
    }

    auto *listBox = new QGroupBox( tr( "导出所选产物" ), body );
    listBox->setObjectName( QStringLiteral( "classifyStep7ExportBox" ) );
    auto *listLay = new QVBoxLayout( listBox );

    m_exportClassifiedCb = new QCheckBox( tr( "分类 GeoTIFF" ), listBox );
    m_exportClassifiedCb->setObjectName( QStringLiteral( "classifyStep7Classified" ) );
    m_exportClassifiedCb->setChecked( true );
    listLay->addWidget( m_exportClassifiedCb );

    m_exportPostRasterCb = new QCheckBox( tr( "后处理栅格" ), listBox );
    m_exportPostRasterCb->setObjectName( QStringLiteral( "classifyStep7PostRaster" ) );
    listLay->addWidget( m_exportPostRasterCb );

    m_exportPostVectorCb = new QCheckBox( tr( "后处理矢量" ), listBox );
    m_exportPostVectorCb->setObjectName( QStringLiteral( "classifyStep7PostVector" ) );
    listLay->addWidget( m_exportPostVectorCb );

    m_exportRoiCb = new QCheckBox( tr( "ROI" ), listBox );
    m_exportRoiCb->setObjectName( QStringLiteral( "classifyStep7Roi" ) );
    listLay->addWidget( m_exportRoiCb );

    m_exportAccuracyCsvCb = new QCheckBox( tr( "精度 CSV" ), listBox );
    m_exportAccuracyCsvCb->setObjectName( QStringLiteral( "classifyStep7AccuracyCsv" ) );
    listLay->addWidget( m_exportAccuracyCsvCb );

    m_exportProjectCb = new QCheckBox( tr( "分类项目 .rscproj" ), listBox );
    m_exportProjectCb->setObjectName( QStringLiteral( "classifyStep7Project" ) );
    m_exportProjectCb->setChecked( true );
    listLay->addWidget( m_exportProjectCb );

    lay->addWidget( listBox );

    m_exportSelectedBtn = new QPushButton( tr( "导出所选" ), body );
    m_exportSelectedBtn->setObjectName( QStringLiteral( "classifyStep7ExportSelected" ) );
    connect( m_exportSelectedBtn, &QPushButton::clicked, this,
             &QgsClassificationMainWindow::exportSelectedStep7 );
    lay->addWidget( m_exportSelectedBtn );

    m_exportLoadToMainBtn = new QPushButton( tr( "加载分类结果到主窗口" ), body );
    m_exportLoadToMainBtn->setObjectName( QStringLiteral( "classifyStep7LoadToMain" ) );
    connect( m_exportLoadToMainBtn, &QPushButton::clicked, this,
             &QgsClassificationMainWindow::loadClassificationResultToMain );
    lay->addWidget( m_exportLoadToMainBtn );

    auto *hint = new QLabel(
      tr( "勾选产物后点「导出所选」；可将分类/后处理栅格加载到主窗口图层树。"
          "任一成功导出或加载即完成本步。" ),
      body );
    hint->setWordWrap( true );
    hint->setStyleSheet( QStringLiteral( "color: #656d76;" ) );
    lay->addWidget( hint );
    lay->addStretch( 1 );
  }
}

void QgsClassificationMainWindow::ensureDefaultClasses()
{
  if ( !m_rois )
    return;
  if ( !m_rois->classDefs().isEmpty() )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "类别方案已存在，未覆盖" ), 3000 );
    if ( m_classListDock )
    {
      m_classListDock->show();
      m_classListDock->raise();
    }
    return;
  }

  const QList<QPair<int, QPair<QString, QString>>> defaults = {
    { 1, { tr( "林地" ), QStringLiteral( "#2da44e" ) } },
    { 2, { tr( "草地" ), QStringLiteral( "#a3e635" ) } },
    { 3, { tr( "水体" ), QStringLiteral( "#0969da" ) } },
    { 4, { tr( "建成区" ), QStringLiteral( "#cf222e" ) } },
    { 5, { tr( "耕地" ), QStringLiteral( "#d29922" ) } },
    { 6, { tr( "裸地" ), QStringLiteral( "#8a92a0" ) } },
  };
  for ( const auto &d : defaults )
  {
    m_rois->setClassDef( RsClassDef( d.first, d.second.first, QColor( d.second.second ) ) );
  }
  if ( statusBar() )
    statusBar()->showMessage( tr( "已添加默认 6 类" ), 3000 );
  if ( m_classListDock )
  {
    m_classListDock->show();
    m_classListDock->raise();
  }
  syncWorkflowFromRois();
  refreshWorkflowUi();
}

void QgsClassificationMainWindow::setActiveSampleRole( bool trainRole )
{
  m_trainSampleRole = trainRole;
  if ( m_stepTrainRoleBtn )
    m_stepTrainRoleBtn->setChecked( trainRole );
  if ( m_stepValidRoleBtn )
    m_stepValidRoleBtn->setChecked( !trainRole );
  if ( m_trainRoleAction )
    m_trainRoleAction->setChecked( trainRole );
  if ( m_validRoleAction )
    m_validRoleAction->setChecked( !trainRole );
  if ( statusBar() )
  {
    statusBar()->showMessage(
      trainRole ? tr( "当前角色：训练样本（数字化工具写入训练集）" )
                : tr( "当前角色：验证样本（UI 标记；ROI 仍共享集合）" ),
      4000 );
  }
  refreshWorkflowUi();
}

void QgsClassificationMainWindow::setClassifyBusy( bool busy )
{
  if ( m_classifyBusy == busy )
    return;
  m_classifyBusy = busy;
  refreshWorkflowUi();
}

void QgsClassificationMainWindow::syncWorkflowFromRois()
{
  if ( !m_workflow || !m_rois )
    return;

  m_workflow->setClassCount( m_rois->classDefs().size() );

  QSet<int> classesWithPixels;
  int trainPixels = 0;
  for ( const RsRoi &roi : m_rois->rois() )
  {
    const int n = roi.pixelIndices().size();
    if ( n <= 0 )
      continue;
    trainPixels += n;
    if ( roi.classId() > 0 )
      classesWithPixels.insert( roi.classId() );
  }
  m_workflow->setTrainingClassCountWithPixels( classesWithPixels.size() );
  m_workflow->setTrainingPixelCount( trainPixels );
}

void QgsClassificationMainWindow::refreshWorkflowUi()
{
  if ( !m_workflow )
    return;

  // Stepper completion ticks.
  if ( m_stepper )
  {
    for ( int i = 0; i < static_cast<int>( RsClassifyStep::Count ); ++i )
    {
      const auto step = static_cast<RsClassifyStep>( i );
      m_stepper->setStepComplete( step, m_workflow->isStepComplete( step ) );
    }
    m_stepper->setCurrentStep( m_workflow->currentStep() );
    m_stepper->setMode( m_workflow->mode() );
  }

  if ( m_stepHost )
    m_stepHost->setCurrentStep( m_workflow->currentStep() );

  // Gate labels for each panel from missingRequirements.
  if ( m_stepHost )
  {
    for ( int i = 0; i < static_cast<int>( RsClassifyStep::Count ); ++i )
    {
      const auto step = static_cast<RsClassifyStep>( i );
      if ( QLabel *gate = m_stepHost->gateLabel( step ) )
      {
        const QStringList miss = m_workflow->missingRequirements( step );
        if ( miss.isEmpty() )
        {
          if ( m_workflow->isStepComplete( step ) )
            gate->setText( tr( "已完成" ) );
          else
            gate->setText( tr( "可进行主操作" ) );
          gate->setStyleSheet( QStringLiteral( "color: #1a7f37;" ) );
        }
        else
        {
          gate->setText( tr( "还需：%1" ).arg( miss.join( QStringLiteral( "；" ) ) ) );
          gate->setStyleSheet( QStringLiteral( "color: #9a6700;" ) );
        }
      }
    }
  }

  // Step 1: class count label.
  if ( m_stepClassCountLabel )
  {
    const int n = m_rois ? m_rois->classDefs().size() : 0;
    const QString src = m_sourceRasterPath.isEmpty()
                          ? tr( "未打开源影像" )
                          : tr( "源影像：%1" ).arg( QFileInfo( m_sourceRasterPath ).fileName() );
    m_stepClassCountLabel->setText(
      tr( "%1\n类别数：%2" ).arg( src ).arg( n ) );
  }

  // Step 2: ROI / pixel stats.
  if ( m_stepSampleStatsLabel && m_rois )
  {
    int roiCount = m_rois->size();
    int pixelCount = 0;
    QSet<int> classesWithPixels;
    for ( const RsRoi &roi : m_rois->rois() )
    {
      const int n = roi.pixelIndices().size();
      pixelCount += n;
      if ( n > 0 && roi.classId() > 0 )
        classesWithPixels.insert( roi.classId() );
    }
    const QString role = m_trainSampleRole ? tr( "训练" ) : tr( "验证" );
    m_stepSampleStatsLabel->setText(
      tr( "当前角色：%1\nROI 数：%2 · 像元：%3 · 有像元类别：%4" )
        .arg( role )
        .arg( roiCount )
        .arg( pixelCount )
        .arg( classesWithPixels.size() ) );
  }
  else if ( m_stepSampleStatsLabel )
  {
    m_stepSampleStatsLabel->setText( tr( "无样本集合" ) );
  }

  // Step 5: popup enabled only when panel has metrics.
  if ( m_stepAccuracyPopupBtn )
    m_stepAccuracyPopupBtn->setEnabled( m_accuracyPanel && m_accuracyPanel->hasResult() );

  // Soft-gate Apply / Preview from canTrainOrClassify; also block while busy.
  const bool canTrain = m_workflow->canTrainOrClassify() && !m_classifyBusy;
  QString trainTip;
  if ( m_classifyBusy )
    trainTip = tr( "分类任务运行中…" );
  else if ( !m_workflow->canTrainOrClassify() )
    trainTip = tr( "还需：%1" ).arg(
      m_workflow->missingRequirements( RsClassifyStep::TrainClassify )
        .join( QStringLiteral( "；" ) ) );
  if ( m_applyAction )
  {
    m_applyAction->setEnabled( canTrain );
    m_applyAction->setToolTip( trainTip );
  }
  if ( auto *preview = findChild<QAction *>( QStringLiteral( "rsClassifyPreviewAction" ) ) )
  {
    preview->setEnabled( canTrain );
    preview->setToolTip( trainTip );
  }
  if ( m_classifierBar )
  {
    if ( auto *btnApply = m_classifierBar->findChild<QPushButton *>(
           QStringLiteral( "rsClassifierBtnApply" ) ) )
    {
      btnApply->setEnabled( canTrain );
      btnApply->setToolTip( trainTip );
    }
    if ( auto *btnPreview = m_classifierBar->findChild<QPushButton *>(
           QStringLiteral( "rsClassifierBtnPreview" ) ) )
    {
      btnPreview->setEnabled( canTrain );
      btnPreview->setToolTip( trainTip );
    }
    if ( auto *btnCv = m_classifierBar->findChild<QPushButton *>(
           QStringLiteral( "rsClassifierBtnCv" ) ) )
    {
      btnCv->setEnabled( canTrain );
      btnCv->setToolTip( trainTip );
    }
  }
  if ( m_stepApplyBtn )
  {
    m_stepApplyBtn->setEnabled( canTrain );
    m_stepApplyBtn->setToolTip( trainTip );
  }
  if ( m_stepPreviewBtn )
  {
    m_stepPreviewBtn->setEnabled( canTrain );
    m_stepPreviewBtn->setToolTip( trainTip );
  }
  if ( m_stepCvBtn )
  {
    m_stepCvBtn->setEnabled( canTrain );
    m_stepCvBtn->setToolTip( trainTip );
  }
  // Wizard: soft-hide JM / spectral unless Evaluate step; expert: show all.
  const bool expert = m_workflow->mode() == RsClassifyUiMode::Expert;
  const bool onEval = m_workflow->currentStep() == RsClassifyStep::Evaluate;
  if ( expert )
  {
    if ( m_jmDock )
      m_jmDock->show();
    if ( m_spectralDock )
      m_spectralDock->show();
    if ( m_classListDock )
      m_classListDock->show();
    if ( m_classQuickListDock )
      m_classQuickListDock->show();
  }
  else
  {
    if ( m_jmDock )
      m_jmDock->setVisible( onEval );
    if ( m_spectralDock )
      m_spectralDock->setVisible( onEval );
  }

  // Status bar soft-gate hint for the current step (non-sticky).
  if ( statusBar() )
  {
    const QStringList miss =
      m_workflow->missingRequirements( m_workflow->currentStep() );
    if ( !miss.isEmpty() )
    {
      statusBar()->showMessage(
        tr( "软门禁：还需 %1" ).arg( miss.join( QStringLiteral( "；" ) ) ),
        4000 );
    }
  }
}

bool QgsClassificationMainWindow::openSourceRaster()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Open source raster" ), QString(),
    tr( "Raster (*.tif *.tiff *.img *.jp2);;All files (*)" ) );
  if ( path.isEmpty() )
    return false;
  return openSourceRaster( path );
}

bool QgsClassificationMainWindow::openSourceRaster( const QString &path )
{
  ensureGdalInit();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, QString( "Failed to open source raster: %1" ).arg( path ) );
    if ( statusBar() )
      statusBar()->showMessage( tr( "Failed to open raster: %1" ).arg( path ),
                                5000 );
    return false;
  }

  m_sourceRasterPath = path;
  m_sourceWidth = ds->GetRasterXSize();
  m_sourceHeight = ds->GetRasterYSize();
  m_sourceBandCount = ds->GetRasterCount();
  ds->GetGeoTransform( m_sourceGt );
  GDALClose( ds );

  auto *layer = new QgsRasterLayer( path, QFileInfo( path ).baseName(),
                                    QStringLiteral( "gdal" ) );
  if ( layer->isValid() )
  {
    if ( m_sourceLayer )
    {
      // R2 take transfers ownership out of the store — delete on replace.
      removeSessionLayer( m_sourceLayer );
      delete m_sourceLayer;
      m_sourceLayer = nullptr;
    }
    m_sourceLayer = layer;
    // Source under samples: insert at bottom of stack (not top)
    addSessionLayer( layer, /*insertOnTop=*/false );
    ensureSampleLayer();
    if ( m_sampleLayer && layer->crs().isValid() )
      m_sampleLayer->setCrs( layer->crs() );
    applySampleLayerRenderer();
    // Ensure sample sits above source
    if ( m_sampleLayer )
    {
      removeSessionLayer( m_sampleLayer );
      addSessionLayer( m_sampleLayer, true );
    }
    rebuildRoisFromSampleLayer();
    if ( m_sessionMap )
    {
      m_sessionMap->zoomToLayer( layer );
      if ( m_sampleLayer )
        m_sessionMap->setCurrentLayer( m_sampleLayer );
    }
  }
  else
  {
    delete layer;
  }

  if ( m_toolMagicWand )
    m_toolMagicWand->setSourceData( m_sourceRasterPath );
  if ( m_classifierBar )
    m_classifierBar->setSourceBands( m_sourceBandCount );
  if ( m_workflow )
    m_workflow->setHasSourceRaster( true );
  if ( m_workflowBridge )
    m_workflowBridge->setSourceRasterArtifact( path.toStdString() );

  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "Source raster loaded: %1 (%2x%3, %4 bands)" )
    .arg( QFileInfo( path ).fileName() ).arg( m_sourceWidth ).arg( m_sourceHeight ).arg( m_sourceBandCount ) );
  if ( statusBar() )
    statusBar()->showMessage(
      tr( "已加载源影像: %1 (%2×%3, %4 bands)" )
        .arg( QFileInfo( path ).fileName() )
        .arg( m_sourceWidth )
        .arg( m_sourceHeight )
        .arg( m_sourceBandCount ),
      4000 );
  return true;
}

bool QgsClassificationMainWindow::buildTrainingData( const QVector<int> &bands,
                                                     cv::Mat &X,
                                                     cv::Mat &y ) const
{
  if ( !m_rois || bands.isEmpty() || m_sourceRasterPath.isEmpty() )
    return false;
  if ( m_sourceWidth <= 0 || m_sourceHeight <= 0 )
    return false;

  ensureGdalInit();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( m_sourceRasterPath.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
    return false;
  const int W = ds->GetRasterXSize();
  const int H = ds->GetRasterYSize();
  const int nBands = ds->GetRasterCount();
  for ( int b : bands )
  {
    if ( b < 1 || b > nBands )
    {
      GDALClose( ds );
      return false;
    }
  }

  // Collect samples with pixel-index dedup (last class wins on overlapping ROIs).
  QHash<quint64, int> pixelClass;
  for ( const RsRoi &roi : m_rois->rois() )
  {
    if ( roi.classId() <= 0 )
      continue;
    QVector<quint64> idx = roi.pixelIndices();
    if ( idx.isEmpty() )
    {
      const QSet<quint64> px = RsPixelRasterizer::rasterize(
        roi.geometry(), m_sourceGt, W, H );
      idx = QVector<quint64>( px.begin(), px.end() );
    }
    for ( quint64 i : idx )
    {
      if ( i < static_cast<quint64>( W ) * static_cast<quint64>( H ) )
        pixelClass.insert( i, roi.classId() );
    }
  }

  if ( pixelClass.size() < 10 )
  {
    GDALClose( ds );
    return false;
  }

  // Flatten to (classId, pixelIdx) and group sample columns by row so each
  // unique row is read once per band (scanline) instead of 1×1 RasterIO.
  QVector<QPair<int, quint64>> samples;
  samples.reserve( pixelClass.size() );
  for ( auto it = pixelClass.constBegin(); it != pixelClass.constEnd(); ++it )
    samples.push_back( qMakePair( it.value(), it.key() ) );

  // row -> list of (sampleIndex, col)
  QHash<int, QVector<QPair<int, int>>> byRow;
  byRow.reserve( samples.size() );
  for ( int s = 0; s < samples.size(); ++s )
  {
    const quint64 idx = samples[s].second;
    const int r = static_cast<int>( idx / static_cast<quint64>( W ) );
    const int c = static_cast<int>( idx % static_cast<quint64>( W ) );
    byRow[r].append( qMakePair( s, c ) );
  }

  X.create( samples.size(), bands.size(), CV_32F );
  y.create( samples.size(), 1, CV_32S );
  for ( int s = 0; s < samples.size(); ++s )
    y.at<int>( s, 0 ) = samples[s].first;

  std::vector<float> rowBuf( static_cast<size_t>( W ) );
  for ( int bi = 0; bi < bands.size(); ++bi )
  {
    GDALRasterBand *band = ds->GetRasterBand( bands[bi] );
    for ( auto it = byRow.constBegin(); it != byRow.constEnd(); ++it )
    {
      const int r = it.key();
      const CPLErr err = band->RasterIO(
        GF_Read, 0, r, W, 1, rowBuf.data(),
        W, 1, GDT_Float32, 0, 0 );
      if ( err != CE_None )
      {
        GDALClose( ds );
        return false;
      }
      for ( const QPair<int, int> &sc : it.value() )
        X.at<float>( sc.first, bi ) = rowBuf[static_cast<size_t>( sc.second )];
    }
  }

  // Drop ROI samples that fall on NoData / user ignore values (edge/background).
  const RsPixelIgnoreOptions ignore = currentIgnoreOptions();
  const int B = bands.size();
  std::vector<bool> bandHasNodata( static_cast<size_t>( B ), false );
  std::vector<float> bandNodata( static_cast<size_t>( B ), 0.f );
  if ( ignore.useSourceNodata )
  {
    for ( int bi = 0; bi < B; ++bi )
    {
      int success = 0;
      const double nd = ds->GetRasterBand( bands[bi] )->GetNoDataValue( &success );
      if ( success )
      {
        bandHasNodata[static_cast<size_t>( bi )] = true;
        bandNodata[static_cast<size_t>( bi )] = static_cast<float>( nd );
      }
    }
  }

  std::vector<float> feat( static_cast<size_t>( B ) );
  QVector<int> keepRows;
  keepRows.reserve( samples.size() );
  for ( int s = 0; s < samples.size(); ++s )
  {
    for ( int bi = 0; bi < B; ++bi )
      feat[static_cast<size_t>( bi )] = X.at<float>( s, bi );
    if ( !ignore.isIgnorePixel( feat.data(), B, bandHasNodata, bandNodata ) )
      keepRows.push_back( s );
  }

  if ( keepRows.size() < 10 )
  {
    GDALClose( ds );
    return false;
  }

  if ( keepRows.size() != samples.size() )
  {
    cv::Mat X2( keepRows.size(), B, CV_32F );
    cv::Mat y2( keepRows.size(), 1, CV_32S );
    for ( int i = 0; i < keepRows.size(); ++i )
    {
      X.row( keepRows[i] ).copyTo( X2.row( i ) );
      y2.at<int>( i, 0 ) = y.at<int>( keepRows[i], 0 );
    }
    X = X2;
    y = y2;
  }

  GDALClose( ds );
  return true;
}

RsPixelIgnoreOptions QgsClassificationMainWindow::currentIgnoreOptions() const
{
  RsPixelIgnoreOptions opt;
  if ( !m_classifierBar )
    return opt;
  opt.useSourceNodata = m_classifierBar->useSourceNodata();
  opt.setIgnoreValuesFromText( m_classifierBar->ignoreValuesText() );
  opt.mode = ( m_classifierBar->ignoreMatchMode() == 1 )
               ? RsPixelIgnoreOptions::Mode::AllBands
               : RsPixelIgnoreOptions::Mode::AnyBand;
  return opt;
}

void QgsClassificationMainWindow::applyClassification()
{
  if ( m_classifyBusy )
    return;
  if ( m_sourceRasterPath.isEmpty() )
  {
    statusBar()->showMessage( tr( "请先 File → Open source raster..." ), 5000 );
    return;
  }
  if ( !m_classifierBar )
    return;

  QVector<int> bands = m_classifierBar->selectedBands();
  if ( bands.isEmpty() )
  {
    // Default to the first min(3, sourceBands) bands.
    const int n = std::min( 3, m_sourceBandCount );
    for ( int i = 1; i <= n; ++i )
      bands.push_back( i );
  }
  if ( bands.isEmpty() )
  {
    statusBar()->showMessage( tr( "无可用波段" ), 5000 );
    return;
  }

  QString outPath = m_classifierBar->outputPath();
  if ( outPath.isEmpty() )
  {
    outPath = QFileDialog::getSaveFileName(
      this, tr( "Output classified raster" ), QString(),
      tr( "GeoTIFF (*.tif)" ) );
    if ( outPath.isEmpty() )
      return;
    m_classifierBar->setOutputPath( outPath );
  }

  RsClassificationTask::Config cfg;
  cfg.sourceRaster = m_sourceRasterPath;
  cfg.outputRaster = outPath;
  cfg.bandIndices = bands;
  cfg.ignoreOptions = currentIgnoreOptions();

  // Phase 10A.1.3 — class colour table is always built from the current ROI
  // collection so the output GTiff palette stays consistent across both the
  // train-from-ROIs flow and the load-from-disk flow.
  const QHash<int, RsClassDef> classDefs = m_rois ? m_rois->classDefs()
                                                 : QHash<int, RsClassDef>();
  for ( auto it = classDefs.constBegin(); it != classDefs.constEnd(); ++it )
    cfg.classColors[it.key()] = it.value().color();

  if ( m_loadedBackend )
  {
    // Phase 10A.1.3 — a model was loaded from disk via File → "Load
    // classifier model...". Consume it (one-shot) and skip training.
    // testX/testY left empty so RsClassificationTask::run() skips accuracy.
    cfg.algoName = QStringLiteral( "Loaded (%1)" ).arg( m_loadedBackend->name() );
    cfg.backend = std::move( m_loadedBackend );
    // Apply sidecar scaler so tile features match training feature space.
    cfg.scaler = m_loadedScaler;
    m_loadedScaler = RsFeatureScaler();
    if ( statusBar() )
      statusBar()->showMessage( tr( "使用已加载模型 (跳过训练)" ), 3000 );
  }
  else
  {
    cv::Mat X, y;
    if ( !buildTrainingData( bands, X, y ) || X.rows < 10 )
    {
      statusBar()->showMessage(
        tr( "训练样本不足（< 10 像元）— 请先勾画 ROI 或加载已保存样本" ), 6000 );
      return;
    }

    // Phase 10A review patch — stratified 70/30 split (default ratio comes
    // from the train-ratio spinbox). testX/testY are reserved for Task 10.9
    // accuracy assessment.
    const auto split = RsClassificationSplit::stratifiedSplit(
      X, y, m_classifierBar->trainRatio() );
    if ( !fitScalerOntoConfig( split, cfg ) )
    {
      statusBar()->showMessage( tr( "特征标准化失败" ), 5000 );
      return;
    }

    switch ( m_classifierBar->currentKind() )
    {
      case RsClassifierKind::NormalBayes:
        cfg.backend.reset( new RsClassifierNormalBayes );
        cfg.algoName = QStringLiteral( "NormalBayes" );
        break;
      case RsClassifierKind::SvmRbf:
        cfg.backend.reset( new RsClassifierSvm );
        cfg.algoName = QStringLiteral( "SVM_RBF" );
        break;
      case RsClassifierKind::KMeans:
      {
        // k from unique labels that actually have training samples, not empty classDefs.
        QSet<int> uniqueLabels;
        for ( int i = 0; i < y.rows; ++i )
          uniqueLabels.insert( y.at<int>( i, 0 ) );
        cfg.backend.reset( new RsClassifierKMeans(
          std::max( 2, static_cast<int>( uniqueLabels.size() ) ) ) );
        cfg.algoName = QStringLiteral( "KMeans" );
        break;
      }
    }

    // Optional model save (training path only). Cancel leaves modelSavePath
    // empty so the task skips persistence. Sidecar .scale.json is written
    // next to the YAML by RsClassificationTask::run after fit.
    // KMeans has no OpenCV Algorithm serialisation — skip the dialog.
    if ( m_classifierBar->currentKind() != RsClassifierKind::KMeans )
    {
      QString modelPath = QFileDialog::getSaveFileName(
        this, tr( "Save classifier model (optional)" ), QString(),
        tr( "OpenCV YAML (*.yml *.yaml);;All files (*)" ) );
      if ( !modelPath.isEmpty() )
      {
        if ( !modelPath.endsWith( QLatin1String( ".yml" ), Qt::CaseInsensitive )
             && !modelPath.endsWith( QLatin1String( ".yaml" ), Qt::CaseInsensitive )
             && !modelPath.endsWith( QLatin1String( ".xml" ), Qt::CaseInsensitive ) )
        {
          modelPath += QStringLiteral( ".yml" );
        }
        cfg.modelSavePath = modelPath;
        mLastModelPath = modelPath;
      }
    }
  }

  const QString algoForLog = cfg.algoName;
  const QString outForLog = outPath;
  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "Classification started: algo=%1, bands=%2, output=%3" )
    .arg( cfg.algoName ).arg( cfg.bandIndices.size() ).arg( QFileInfo( outPath ).fileName() ) );
  auto *task = new RsClassificationTask( std::move( cfg ) );
  setClassifyBusy( true );

  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:classify:apply";
  req.title = QStringLiteral( "Classification: %1" ).arg( algoForLog ).toStdString();
  req.source = "module";
  req.exclusive = true;
  req.params["output"] = outForLog.toStdString();

  RsJobRunner::run(
    std::move( req ),
    [task]( const sicnu::jobs::JobRequest &request,
            sicnu::operators::RSOperatorContext &ctx ) {
      ctx.logInfo( "Running supervised classification apply" );
      ctx.reportProgress( 0.0, "Classifying" );
      const bool ok = task->run();
      if ( ctx.isCancelled() || ( !ok && task->result().errorMessage
                                    == QStringLiteral( "Cancelled" ) ) )
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
      result["totalPixels"] = task->result().totalPixels;
      result["durationMs"] = task->result().durationMs;
      return result;
    },
    [this, task, algoForLog, outForLog]( const RsJobFinish &fin ) {
      setClassifyBusy( false );
      const auto &r = task->result();

      if ( fin.succeeded() && r.ok )
      {
        m_lastClassifyPath = outForLog;

        if ( m_workflow )
          m_workflow->setHasFullClassifyResult( true );

        auto *classLayer = new QgsRasterLayer(
          outForLog,
          QFileInfo( outForLog ).baseName() + tr( " (分类)" ),
          QStringLiteral( "gdal" ) );
        if ( classLayer->isValid() )
        {
          m_previewLayer = classLayer;
          addSessionLayer( classLayer, true );
        }
        else
        {
          delete classLayer;
        }
        if ( m_workflowBridge )
          m_workflowBridge->setClassifiedOutputArtifact( outForLog.toStdString() );

        QJsonObject obj{
          { QStringLiteral( "event" ), QStringLiteral( "classify_finished" ) },
          { QStringLiteral( "algo" ), algoForLog },
          { QStringLiteral( "total_pixels" ), r.totalPixels },
          { QStringLiteral( "duration_ms" ), r.durationMs },
          { QStringLiteral( "status" ), QStringLiteral( "ok" ) }
        };
        if ( !r.accuracy.classIds.isEmpty() )
        {
          obj.insert( QStringLiteral( "overall_accuracy" ), r.accuracy.overallAccuracy );
          obj.insert( QStringLiteral( "kappa" ), r.accuracy.kappa );
        }
        QgsMessageLog::logMessage(
          QString::fromUtf8( QJsonDocument( obj ).toJson( QJsonDocument::Compact ) ),
          QStringLiteral( "Classification" ),
          Qgis::MessageLevel::Info );
        if ( statusBar() )
          statusBar()->showMessage(
            tr( "分类完成: %1 (%2 ms)" )
              .arg( QFileInfo( outForLog ).fileName() )
              .arg( r.durationMs ),
            6000 );

        if ( !r.accuracy.classIds.isEmpty() )
        {
          QHash<int, QString> classNames;
          if ( m_rois )
          {
            const auto defs = m_rois->classDefs();
            for ( auto it = defs.constBegin(); it != defs.constEnd(); ++it )
              classNames[it.key()] = it.value().name();
          }
          if ( m_accuracyPanel )
            m_accuracyPanel->setResult( r.accuracy, classNames );
          if ( m_stepAccuracyPopupBtn )
            m_stepAccuracyPopupBtn->setEnabled( true );
          m_accuracySource = QStringLiteral( "holdout" );
          if ( m_workflow )
            m_workflow->setHasAccuracyMetrics( true );
          if ( m_workflow )
            m_workflow->setCurrentStep( RsClassifyStep::Accuracy );
        }
        refreshWorkflowUi();
      }
      else if ( fin.cancelled() )
      {
        SICNU_LOG_WARN( SicnuLogTags::Classification,
                        QStringLiteral( "Classification cancelled" ) );
        if ( statusBar() )
          statusBar()->showMessage( tr( "分类已取消" ), 3000 );
      }
      else
      {
        const QString err = !r.errorMessage.isEmpty()
                              ? r.errorMessage
                              : ( !fin.error.isEmpty() ? fin.error : tr( "运行失败" ) );
        SICNU_LOG_ERROR( SicnuLogTags::Classification,
                         QString( "Classification failed: %1" ).arg( err ) );
        if ( statusBar() )
          statusBar()->showMessage( tr( "分类失败: %1" ).arg( err ), 6000 );
      }
      task->deleteLater();
    },
    [task]() { task->cancel(); },
    this );

  if ( statusBar() )
    statusBar()->showMessage( tr( "分类中…" ), 3000 );
}



// ---------------------------------------------------------------------------
// Phase 10A review patch — slot implementations.
// ---------------------------------------------------------------------------

void QgsClassificationMainWindow::applyPreview()
{
  if ( m_classifyBusy )
    return;
  if ( m_sourceRasterPath.isEmpty() )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "请先 File → Open source raster..." ), 5000 );
    return;
  }
  if ( !m_classifierBar )
    return;

  QVector<int> bands = m_classifierBar->selectedBands();
  if ( bands.isEmpty() )
  {
    const int n = std::min( 3, m_sourceBandCount );
    for ( int i = 1; i <= n; ++i )
      bands.push_back( i );
  }
  if ( bands.isEmpty() )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "无可用波段" ), 5000 );
    return;
  }

  cv::Mat X, y;
  if ( !buildTrainingData( bands, X, y ) || X.rows < 10 )
  {
    if ( statusBar() )
      statusBar()->showMessage(
        tr( "训练样本不足（< 10 像元）— 请先勾画 ROI 或加载已保存样本" ), 6000 );
    return;
  }

  // Viewport-cropped preview: only classify the current canvas extent.
  // Reject when the viewport does not intersect the source raster.
  if ( !m_canvas )
    return;
  const RsPixelWindow win = rsMapExtentToPixelWindow(
    m_canvas->extent(), m_sourceGt, m_sourceWidth, m_sourceHeight );
  if ( !win.valid )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "视口不在影像范围内" ), 5000 );
    return;
  }

  // Route output to a temp file, skip the file dialog, and add the result
  // as a temporary canvas layer. Accuracy dialog is intentionally not shown.
  const QString outPath = QDir::temp().filePath(
    QStringLiteral( "classify_preview.tif" ) );

  RsClassificationTask::Config cfg;
  cfg.sourceRaster = m_sourceRasterPath;
  cfg.outputRaster = outPath;
  cfg.bandIndices = bands;
  cfg.ignoreOptions = currentIgnoreOptions();
  cfg.cropToWindow = true;
  cfg.window = win;
  const auto split = RsClassificationSplit::stratifiedSplit(
    X, y, m_classifierBar->trainRatio() );
  if ( !fitScalerOntoConfig( split, cfg ) )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "特征标准化失败" ), 5000 );
    return;
  }

  const QHash<int, RsClassDef> classDefs = m_rois ? m_rois->classDefs()
                                                 : QHash<int, RsClassDef>();
  for ( auto it = classDefs.constBegin(); it != classDefs.constEnd(); ++it )
    cfg.classColors[it.key()] = it.value().color();

  switch ( m_classifierBar->currentKind() )
  {
    case RsClassifierKind::NormalBayes:
      cfg.backend.reset( new RsClassifierNormalBayes );
      cfg.algoName = QStringLiteral( "NormalBayes" );
      break;
    case RsClassifierKind::SvmRbf:
      cfg.backend.reset( new RsClassifierSvm );
      cfg.algoName = QStringLiteral( "SVM_RBF" );
      break;
    case RsClassifierKind::KMeans:
    {
      // k from unique labels that actually have training samples, not empty classDefs.
      QSet<int> uniqueLabels;
      for ( int i = 0; i < y.rows; ++i )
        uniqueLabels.insert( y.at<int>( i, 0 ) );
      cfg.backend.reset( new RsClassifierKMeans(
        std::max( 2, static_cast<int>( uniqueLabels.size() ) ) ) );
      cfg.algoName = QStringLiteral( "KMeans" );
      break;
    }
  }

  auto *task = new RsClassificationTask( std::move( cfg ) );
  const QString outForLog = outPath;
  setClassifyBusy( true );

  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:classify:preview";
  req.title = QStringLiteral( "Classification preview" ).toStdString();
  req.source = "module";
  req.exclusive = false;
  req.params["output"] = outForLog.toStdString();

  RsJobRunner::run(
    std::move( req ),
    [task]( const sicnu::jobs::JobRequest &request,
            sicnu::operators::RSOperatorContext &ctx ) {
      ctx.logInfo( "Running classification preview" );
      const bool ok = task->run();
      if ( ctx.isCancelled() || ( !ok && task->result().errorMessage
                                    == QStringLiteral( "Cancelled" ) ) )
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
    [this, task, outForLog]( const RsJobFinish &fin ) {
      setClassifyBusy( false );
      const auto &r = task->result();

      if ( fin.succeeded() && r.ok )
      {
        auto *previewLayer = new QgsRasterLayer(
          outForLog, QStringLiteral( "classify_preview" ),
          QStringLiteral( "gdal" ) );
        if ( previewLayer->isValid() )
        {
          if ( m_previewLayer )
          {
            // R2 take transfers ownership out of the store — delete on replace.
            removeSessionLayer( m_previewLayer );
            delete m_previewLayer;
            m_previewLayer = nullptr;
          }
          m_previewLayer = previewLayer;
          addSessionLayer( previewLayer, true );
        }
        else
        {
          delete previewLayer;
        }
        if ( statusBar() )
          statusBar()->showMessage(
            tr( "预览完成 (%1 ms)" ).arg( r.durationMs ), 5000 );
      }
      else if ( fin.cancelled() )
      {
        if ( statusBar() )
          statusBar()->showMessage( tr( "预览已取消" ), 3000 );
      }
      else
      {
        const QString err = !r.errorMessage.isEmpty()
                              ? r.errorMessage
                              : ( !fin.error.isEmpty() ? fin.error : tr( "运行失败" ) );
        if ( statusBar() )
          statusBar()->showMessage( tr( "预览失败: %1" ).arg( err ), 6000 );
      }
      task->deleteLater();
    },
    [task]() { task->cancel(); },
    this );

  if ( statusBar() )
    statusBar()->showMessage( tr( "预览中…" ), 3000 );
}

void QgsClassificationMainWindow::openPostProcessDialog( int algorithm )
{
  if ( m_classifyBusy )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "当前有任务进行中，请稍候" ), 3000 );
    return;
  }

  const auto algo = static_cast<RsPostProcessDialog::Algorithm>( algorithm );
  RsPostProcessDialog dlg( algo, this );

  // Prefer last classify result as input; else last post-process raster.
  QString defIn = m_lastClassifyPath;
  if ( defIn.isEmpty() )
    defIn = m_lastPostRasterPath;
  dlg.setDefaultInputPath( defIn );
  if ( !defIn.isEmpty() )
  {
    const QFileInfo fi( defIn );
    QString suffix;
    switch ( algo )
    {
      case RsPostProcessDialog::Algorithm::Sieve:
        suffix = QStringLiteral( "_sieve.tif" );
        break;
      case RsPostProcessDialog::Algorithm::Majority:
        suffix = QStringLiteral( "_majority.tif" );
        break;
      case RsPostProcessDialog::Algorithm::Clump:
        suffix = QStringLiteral( "_clump.tif" );
        break;
      case RsPostProcessDialog::Algorithm::Recode:
        suffix = QStringLiteral( "_recode.tif" );
        break;
      case RsPostProcessDialog::Algorithm::Polygonize:
        suffix = QStringLiteral( "_poly.gpkg" );
        break;
    }
    dlg.setDefaultOutputPath(
      fi.absolutePath() + QLatin1Char( '/' ) + fi.completeBaseName() + suffix );
  }

  if ( dlg.exec() != QDialog::Accepted )
    return;

  RsPostProcessConfig cfg;
  QString err;
  if ( !dlg.buildConfig( cfg, &err ) )
  {
    QMessageBox::warning( this, RsPostProcessDialog::algorithmTitle( algo ), err );
    return;
  }
  runPostProcess( cfg, dlg.loadToLayerTree(),
                  RsPostProcessDialog::algorithmTitle( algo ),
                  RsPostProcessDialog::algorithmId( algo ) );
}

void QgsClassificationMainWindow::runPostProcess( const RsPostProcessConfig &cfgIn,
                                                  bool loadToLayers,
                                                  const QString &jobTitle,
                                                  const QString &algorithmId )
{
  if ( m_classifyBusy )
    return;

  RsPostProcessConfig cfg = cfgIn;
  const QString outRaster = cfg.outputRasterPath;
  const QString outVector = cfg.outputVectorPath;
  const bool doPoly = cfg.runPolygonize;
  const bool rasterOut = !doPoly || !outRaster.isEmpty();
  auto *task = new RsPostProcessTask( std::move( cfg ) );
  setClassifyBusy( true );

  sicnu::jobs::JobRequest req;
  req.algorithmId = algorithmId.isEmpty()
                      ? "module:classify:postprocess"
                      : algorithmId.toStdString();
  req.title = ( jobTitle.isEmpty() ? tr( "分类后处理" ) : jobTitle ).toStdString();
  req.source = "module";
  req.exclusive = true;
  req.params["input"] = cfgIn.inputPath.toStdString();
  if ( !outRaster.isEmpty() )
    req.params["output"] = outRaster.toStdString();
  if ( !outVector.isEmpty() )
    req.params["outputVector"] = outVector.toStdString();
  req.params["loadOutputsToMain"] = loadToLayers;

  RsJobRunner::run(
    std::move( req ),
    [task]( const sicnu::jobs::JobRequest &,
            sicnu::operators::RSOperatorContext &ctx ) {
      ctx.logInfo( "Running classification post-process" );
      ctx.reportProgress( 0.0, "Post-process" );
      const bool ok = task->run();
      if ( ctx.isCancelled()
           || ( !ok && task->result().errorMessage == QStringLiteral( "Cancelled" ) ) )
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
      result["durationMs"] = task->result().durationMs;
      if ( !task->config().outputRasterPath.isEmpty() )
        result["output"] = task->config().outputRasterPath.toStdString();
      if ( !task->config().outputVectorPath.isEmpty() )
        result["outputVector"] = task->config().outputVectorPath.toStdString();
      return result;
    },
    [this, task, outRaster, outVector, doPoly, loadToLayers]( const RsJobFinish &fin ) {
      setClassifyBusy( false );
      const auto &r = task->result();

      if ( fin.succeeded() && r.ok )
      {
        if ( m_workflow )
          m_workflow->setHasPostProcessResult( true );

        if ( !outRaster.isEmpty() )
          m_lastPostRasterPath = outRaster;
        if ( doPoly && !outVector.isEmpty() )
          m_lastPostVectorPath = outVector;

        if ( loadToLayers )
        {
          if ( !doPoly && !outRaster.isEmpty() && QFileInfo::exists( outRaster ) )
          {
            auto *resultLayer = new QgsRasterLayer(
              outRaster,
              QFileInfo( outRaster ).baseName() + tr( " (后处理)" ),
              QStringLiteral( "gdal" ) );
            if ( resultLayer->isValid() )
            {
              m_previewLayer = resultLayer;
              addSessionLayer( resultLayer, true );
            }
            else
            {
              delete resultLayer;
            }
          }
          if ( doPoly && !outVector.isEmpty() && QFileInfo::exists( outVector ) )
          {
            auto *vlayer = new QgsVectorLayer(
              outVector,
              QFileInfo( outVector ).baseName() + tr( " (矢量)" ),
              QStringLiteral( "ogr" ) );
            if ( vlayer->isValid() )
              addSessionLayer( vlayer, true );
            else
              delete vlayer;
          }
        }

        if ( statusBar() )
        {
          QString msg = tr( "后处理完成 (%1 ms)" ).arg( r.durationMs );
          if ( !outRaster.isEmpty() )
            msg += QStringLiteral( ": " ) + QFileInfo( outRaster ).fileName();
          if ( doPoly && !outVector.isEmpty() )
            msg += tr( "；矢量 %1" ).arg( QFileInfo( outVector ).fileName() );
          if ( loadToLayers )
            msg += tr( "（已加载到图层）" );
          statusBar()->showMessage( msg, 6000 );
        }
        refreshWorkflowUi();
      }
      else if ( fin.cancelled() )
      {
        if ( statusBar() )
          statusBar()->showMessage( tr( "后处理已取消" ), 3000 );
      }
      else
      {
        const QString err = !r.errorMessage.isEmpty()
                              ? r.errorMessage
                              : ( !fin.error.isEmpty() ? fin.error : tr( "未知错误" ) );
        SICNU_LOG_ERROR( SicnuLogTags::Classification,
                         QStringLiteral( "Post-process failed: %1" ).arg( err ) );
        if ( statusBar() )
          statusBar()->showMessage( tr( "后处理失败: %1" ).arg( err ), 6000 );
      }
      delete task;
    },
    [task]() { task->cancel(); },
    this );

  if ( statusBar() )
    statusBar()->showMessage( tr( "后处理中…" ), 3000 );
}

void QgsClassificationMainWindow::runCrossValidation()
{
  // Phase 10A.1.2 — stratified 5-fold CV on the current ROIs.
  if ( m_classifyBusy )
    return;
  if ( m_sourceRasterPath.isEmpty() )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "请先 Open source raster…" ), 5000 );
    return;
  }
  if ( !m_classifierBar )
    return;

  QVector<int> bands = m_classifierBar->selectedBands();
  if ( bands.isEmpty() )
  {
    const int n = std::min( 3, m_sourceBandCount );
    for ( int i = 1; i <= n; ++i )
      bands.push_back( i );
  }
  cv::Mat X, y;
  if ( !buildTrainingData( bands, X, y ) || X.rows < 25 )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "CV 需要 ≥ 25 像元" ), 5000 );
    return;
  }

  const auto kind = m_classifierBar->currentKind();
  if ( kind == RsClassifierKind::KMeans )
  {
    QMessageBox::information(
      this, tr( "K-Means CV" ),
      tr( "K-Means 交叉验证不适用 (cluster ↔ class 标签不齐)。\n"
          "请用 NormalBayes 或 SVM。" ) );
    return;
  }
  auto factory = [kind]() -> std::unique_ptr<RsClassifierBackend>
  {
    switch ( kind )
    {
      case RsClassifierKind::NormalBayes:
        return std::make_unique<RsClassifierNormalBayes>();
      case RsClassifierKind::SvmRbf:
        return std::make_unique<RsClassifierSvm>();
      default:
        return nullptr;
    }
  };

  // Run cross-validation via JobEngine (appears in unified task panel)
  auto *task = new RsCvTask( X, y, factory, 5, tr( "5-fold Cross Validation" ) );
  setClassifyBusy( true );

  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:classify:cv";
  req.title = tr( "5-fold 交叉验证" ).toStdString();
  req.source = "module";
  req.exclusive = false;

  RsJobRunner::run(
    std::move( req ),
    [task]( const sicnu::jobs::JobRequest &,
            sicnu::operators::RSOperatorContext &ctx ) {
      ctx.logInfo( "Running 5-fold cross validation" );
      ctx.reportProgress( 0.0, "CV" );
      const bool ok = task->run();
      if ( ctx.isCancelled() || !ok )
      {
        if ( task->result().errorMessage == QStringLiteral( "Cancelled" ) || ctx.isCancelled() )
        {
          throw sicnu::operators::RSOperatorError(
            sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
        }
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::ComputationError,
          task->result().errorMessage.isEmpty()
            ? "Cross validation failed"
            : task->result().errorMessage.toStdString() );
      }
      Json::Value result( Json::objectValue );
      result["meanAccuracy"] = task->result().meanAccuracy;
      result["stdAccuracy"] = task->result().stdAccuracy;
      return result;
    },
    [this, task]( const RsJobFinish &fin ) {
      setClassifyBusy( false );
      const auto res = task->result();
      delete task;

      if ( fin.succeeded() && res.ok() )
      {
        QString perFold;
        for ( int i = 0; i < res.foldAccuracies.size(); ++i )
          perFold += QString( "  fold%1: %2%\n" )
                       .arg( i + 1 )
                       .arg( res.foldAccuracies[i] * 100, 0, 'f', 1 );
        QMessageBox::information(
          this, tr( "5-fold Cross Validation" ),
          tr( "Mean accuracy: %1% ± %2%\n\n%3" )
            .arg( res.meanAccuracy * 100, 0, 'f', 1 )
            .arg( res.stdAccuracy * 100, 0, 'f', 1 )
            .arg( perFold ) );
        if ( statusBar() )
          statusBar()->showMessage( tr( "交叉验证完成" ), 3000 );
      }
      else if ( fin.cancelled() )
      {
        if ( statusBar() )
          statusBar()->showMessage( tr( "交叉验证已取消" ), 3000 );
      }
      else if ( statusBar() )
      {
        statusBar()->showMessage(
          tr( "交叉验证失败: %1" )
            .arg( !fin.error.isEmpty() ? fin.error : tr( "未知错误" ) ),
          5000 );
      }
    },
    [task]() { task->cancel(); },
    this );

  if ( statusBar() )
    statusBar()->showMessage( tr( "5-fold CV 运行中…" ), 3000 );
}

void QgsClassificationMainWindow::recomputeSpectralCurves()
{
  if ( !m_spectralCurve )
    return;
  if ( m_sourceRasterPath.isEmpty() || !m_rois )
  {
    m_spectralCurve->setClassCurves( {} );
    return;
  }
  if ( m_sourceWidth <= 0 || m_sourceHeight <= 0 )
    return;

  // Pick bands: use ClassifierBar selection if available; otherwise the
  // first min(6, m_sourceBandCount) bands.
  QVector<int> bands = m_classifierBar ? m_classifierBar->selectedBands()
                                      : QVector<int>{};
  if ( bands.isEmpty() )
  {
    const int n = std::min( 6, m_sourceBandCount );
    for ( int i = 1; i <= n; ++i )
      bands.push_back( i );
  }
  if ( bands.isEmpty() )
    return;

  // Read bands into memory once. Performance note: this re-reads the raster
  // on every ROI change. Acceptable for v1 (typical training rasters are
  // small / sampling is bounded by ROI pixel set); future optimisation is
  // tile-bounded RasterIO.
  ensureGdalInit();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( m_sourceRasterPath.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
    return;
  const int W = ds->GetRasterXSize();
  const int H = ds->GetRasterYSize();
  const int nBands = ds->GetRasterCount();
  for ( int b : bands )
  {
    if ( b < 1 || b > nBands )
    {
      GDALClose( ds );
      return;
    }
  }
  // Collect all unique ROI pixel indices (no need to read entire raster)
  QHash<int, QVector<quint64>> idxByClass;
  for ( const RsRoi &roi : m_rois->rois() )
  {
    if ( roi.classId() <= 0 )
      continue;
    QVector<quint64> idx = roi.pixelIndices();
    if ( idx.isEmpty() )
    {
      const QSet<quint64> px = RsPixelRasterizer::rasterize(
        roi.geometry(), m_sourceGt, W, H );
      idx = QVector<quint64>( px.begin(), px.end() );
    }
    auto &bucket = idxByClass[roi.classId()];
    for ( quint64 i : idx )
    {
      if ( i < static_cast<quint64>( W ) * H )
        bucket.push_back( i );
    }
  }

  // Read only ROI pixel values per band (much less memory than full raster)
  QHash<int, std::vector<double>> bandSums;   // classId -> per-band sum
  QHash<int, std::vector<double>> bandSumSq;  // classId -> per-band sum of squares
  QHash<int, int> classPixelCount;            // classId -> pixel count

  const QHash<int, RsClassDef> classDefs = m_rois->classDefs();
  for ( auto it = classDefs.constBegin(); it != classDefs.constEnd(); ++it )
  {
    bandSums[it.key()].resize( bands.size(), 0.0 );
    bandSumSq[it.key()].resize( bands.size(), 0.0 );
    classPixelCount[it.key()] = 0;
  }

  for ( int bi = 0; bi < bands.size(); ++bi )
  {
    GDALRasterBandH band = ds->GetRasterBand( bands[bi] );
    if ( !band ) continue;

    for ( auto it = idxByClass.constBegin(); it != idxByClass.constEnd(); ++it )
    {
      int classId = it.key();
      const QVector<quint64> &px = it.value();
      for ( quint64 i : px )
      {
        int row = static_cast<int>( i / W );
        int col = static_cast<int>( i % W );
        float val = 0.0f;
        GDALRasterIO( band, GF_Read, col, row, 1, 1, &val, 1, 1, GDT_Float32, 0, 0 );
        bandSums[classId][bi] += val;
        bandSumSq[classId][bi] += val * val;
      }
      if ( bi == 0 )
        classPixelCount[classId] = px.size();
    }
  }
  GDALClose( ds );

  // Compute curves
  QVector<RsSpectralCurveWidget::Curve> curves;
  curves.reserve( classDefs.size() );
  for ( auto it = classDefs.constBegin(); it != classDefs.constEnd(); ++it )
  {
    int classId = it.key();
    int n = classPixelCount.value( classId, 0 );
    if ( n == 0 ) continue;

    RsSpectralCurveWidget::Curve curve;
    curve.classId = classId;
    curve.color = it.value().color();
    curve.name = it.value().name();
    curve.bandMeans.resize( bands.size() );
    curve.bandStds.resize( bands.size() );

    for ( int bi = 0; bi < bands.size(); ++bi )
    {
      MathUtils::AccumulatorStats accStats;
      accStats.count = n;
      accStats.sum = bandSums[classId][bi];
      accStats.sumSq = bandSumSq[classId][bi];
      accStats.min = 0;
      accStats.max = 0;
      MathUtils::Stats s = MathUtils::computeStatsFromAccumulators(accStats);
      curve.bandMeans[bi] = s.mean;
      curve.bandStds[bi] = s.stddev;
    }
    curves.push_back( curve );
  }
  m_spectralCurve->setClassCurves( curves );
}

void QgsClassificationMainWindow::recomputeJmMatrix()
{
  if ( !m_jmMatrix || !m_rois )
    return;
  if ( m_sourceRasterPath.isEmpty() )
    return;
  if ( m_sourceWidth <= 0 || m_sourceHeight <= 0 )
    return;

  // Same band set as the spectral curve dock for consistency.
  QVector<int> bands = m_classifierBar ? m_classifierBar->selectedBands()
                                      : QVector<int>{};
  if ( bands.isEmpty() )
  {
    const int n = std::min( 6, m_sourceBandCount );
    for ( int i = 1; i <= n; ++i )
      bands.push_back( i );
  }
  if ( bands.isEmpty() )
    return;

  ensureGdalInit();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( m_sourceRasterPath.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
    return;
  const int W = ds->GetRasterXSize();
  const int H = ds->GetRasterYSize();
  const int nBands = ds->GetRasterCount();
  for ( int b : bands )
  {
    if ( b < 1 || b > nBands )
    {
      GDALClose( ds );
      return;
    }
  }
  // Collect ROI pixel indices per class
  QHash<int, QVector<quint64>> idxByClass;
  for ( const RsRoi &roi : m_rois->rois() )
  {
    if ( roi.classId() <= 0 )
      continue;
    QVector<quint64> idx = roi.pixelIndices();
    if ( idx.isEmpty() )
    {
      const QSet<quint64> px = RsPixelRasterizer::rasterize(
        roi.geometry(), m_sourceGt, W, H );
      idx = QVector<quint64>( px.begin(), px.end() );
    }
    auto &bucket = idxByClass[roi.classId()];
    for ( quint64 i : idx )
    {
      if ( i < static_cast<quint64>( W ) * H )
        bucket.push_back( i );
    }
  }

  // Read only ROI pixel values per band (much less memory than full raster)
  QHash<int, cv::Mat> samplesByClass;
  for ( auto it = idxByClass.constBegin(); it != idxByClass.constEnd(); ++it )
  {
    const auto &px = it.value();
    if ( px.size() < 2 )
      continue;
    cv::Mat m( px.size(), bands.size(), CV_32F );
    for ( int bi = 0; bi < bands.size(); ++bi )
    {
      GDALRasterBandH band = ds->GetRasterBand( bands[bi] );
      if ( !band ) continue;
      for ( int r = 0; r < px.size(); ++r )
      {
        int row = static_cast<int>( px[r] / W );
        int col = static_cast<int>( px[r] % W );
        float val = 0.0f;
        GDALRasterIO( band, GF_Read, col, row, 1, 1, &val, 1, 1, GDT_Float32, 0, 0 );
        m.at<float>( r, bi ) = val;
      }
    }
    samplesByClass.insert( it.key(), m );
  }
  GDALClose( ds );

  // Sorted class id list for pairwise iteration.
  QList<int> ids = samplesByClass.keys();
  std::sort( ids.begin(), ids.end() );

  QHash<QPair<int, int>, double> jm;
  for ( int i = 0; i < ids.size(); ++i )
  {
    for ( int j = i + 1; j < ids.size(); ++j )
    {
      const double d = RsJmSeparability::pairJm(
        samplesByClass.value( ids[i] ),
        samplesByClass.value( ids[j] ) );
      jm.insert( qMakePair( ids[i], ids[j] ), d );
    }
  }

  QVector<RsJmMatrixWidget::ClassEntry> classes;
  const QHash<int, RsClassDef> classDefs = m_rois->classDefs();
  QList<int> defIds = classDefs.keys();
  std::sort( defIds.begin(), defIds.end() );
  for ( int id : defIds )
  {
    const RsClassDef d = classDefs.value( id );
    classes.push_back( { d.id(), d.name(), d.color() } );
  }
  m_jmMatrix->setData( classes, jm );
}

void QgsClassificationMainWindow::exportRois()
{
  // Always prompt for path from the menu (last path is for dirty-close Save).
  const QString path = QFileDialog::getSaveFileName(
    this, tr( "Export ROIs" ), mSession.lastRoisPath(),
    tr( "ESRI Shapefile (*.shp)" ) );
  if ( path.isEmpty() )
    return;
  saveRoisToPath( path );
}

void QgsClassificationMainWindow::loadRois()
{
  if ( !m_rois )
  {
    m_rois = new RsRoiCollection( this );
  }

  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Load ROIs" ), mSession.lastRoisPath(),
    tr( "ESRI Shapefile (*.shp)" ) );
  if ( path.isEmpty() )
    return;

  QgsCoordinateReferenceSystem crs;
  if ( m_sourceLayer )
  {
    crs = m_sourceLayer->crs();
  }

  // Stage into a temporary collection so a failed load never wipes the UI.
  RsRoiCollection staging;
  const bool ok = RsRoiIO::load( path, staging, crs );
  if ( ok )
  {
    mSuppressDirty = true;
    m_rois->clear();
    const auto defs = staging.classDefs();
    for ( auto it = defs.constBegin(); it != defs.constEnd(); ++it )
      m_rois->setClassDef( it.value() );
    for ( int i = 0; i < staging.size(); ++i )
    {
      RsRoi roi = staging.at( i );
      if ( m_sourceWidth > 0 && m_sourceHeight > 0 )
      {
        QSet<quint64> pixels = RsPixelRasterizer::rasterize(
          roi.geometry(), m_sourceGt, m_sourceWidth, m_sourceHeight );
        roi.setPixelIndices( QVector<quint64>( pixels.begin(), pixels.end() ) );
      }
      m_rois->appendRoi( roi );
    }
    mSuppressDirty = false;
    syncSampleLayerFromRois();
    applySampleLayerRenderer();
    mSession.setLastRoisPath( path );
    mSession.clearDirty();
    SICNU_LOG_INFO( SicnuLogTags::Classification,
                    QString( "ROIs loaded from %1" ).arg( path ) );
    if ( statusBar() )
      statusBar()->showMessage( tr( "成功加载 %1 个样本" ).arg( m_rois->size() ), 5000 );
  }
  else
  {
    QMessageBox::critical(
      this, tr( "Error" ),
      tr( "Failed to load ROIs from %1" ).arg( path ) );
  }
}

// ---------------------------------------------------------------------------
// Phase 10A.1.3 — Load classifier model from disk.
// ---------------------------------------------------------------------------

void QgsClassificationMainWindow::loadClassifierModel()
{
  RsClassifierLoadDialog dlg( this );
  if ( dlg.exec() != QDialog::Accepted )
    return;

  std::unique_ptr<RsClassifierBackend> backend;
  switch ( dlg.selectedKind() )
  {
    case RsClassifierLoadDialog::BackendKind::NormalBayes:
      backend = std::make_unique<RsClassifierNormalBayes>();
      break;
    case RsClassifierLoadDialog::BackendKind::SvmRbf:
      backend = std::make_unique<RsClassifierSvm>();
      break;
  }

  if ( !backend || !backend->load( dlg.modelPath() ) )
  {
    QMessageBox::warning(
      this, tr( "Load failed" ),
      tr( "无法加载模型：%1" ).arg( dlg.modelPath() ) );
    return;
  }
  m_loadedBackend = std::move( backend );

  // Optional sidecar: <model-stem>.scale.json next to the YAML/XML model.
  m_loadedScaler = RsFeatureScaler();
  const QFileInfo mi( dlg.modelPath() );
  const QString scalePath = mi.absolutePath() + QLatin1Char( '/' )
                            + mi.completeBaseName() + QStringLiteral( ".scale.json" );
  bool scaleMissing = false;
  if ( QFile::exists( scalePath ) )
  {
    if ( !m_loadedScaler.loadJson( scalePath ) )
    {
      m_loadedScaler = RsFeatureScaler();
      SICNU_LOG_WARN( SicnuLogTags::Classification,
                      QString( "scale.json present but failed to load: %1 — predicting without scaling" )
                        .arg( scalePath ) );
      if ( statusBar() )
        statusBar()->showMessage(
          tr( "警告：scale.json 损坏，将不缩放特征" ), 6000 );
    }
    else
    {
      SICNU_LOG_INFO( SicnuLogTags::Classification,
                      QString( "Loaded feature scaler sidecar: %1" ).arg( scalePath ) );
    }
  }
  else
  {
    scaleMissing = true;
    SICNU_LOG_INFO( SicnuLogTags::Classification,
                    QString( "No scale.json sidecar at %1 — predicting without feature scaling (compat with old models)" )
                      .arg( scalePath ) );
  }

  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "Classifier model loaded: %1" ).arg( dlg.modelPath() ) );
  if ( statusBar() )
  {
    if ( scaleMissing )
      statusBar()->showMessage(
        tr( "已加载模型（无 scale.json，将不缩放特征，兼容旧模型）— 下次 Apply 将跳过训练" ), 0 );
    else
      statusBar()->showMessage(
        tr( "已加载模型 — 下次 Apply 将跳过训练，直接 predict" ), 0 );
  }
}

// ---------------------------------------------------------------------------
// Step 7 — export checklist + project persistence.
// ---------------------------------------------------------------------------

bool QgsClassificationMainWindow::copyPathWithDialog( const QString &srcPath,
                                                     const QString &title )
{
  if ( srcPath.isEmpty() || !QFileInfo::exists( srcPath ) )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "源文件不存在: %1" ).arg( srcPath ), 5000 );
    return false;
  }

  const QFileInfo fi( srcPath );
  const QString dest = QFileDialog::getSaveFileName(
    this, title, fi.fileName(),
    tr( "All files (*)" ) );
  if ( dest.isEmpty() )
    return false;

  if ( QFileInfo::exists( dest ) )
    QFile::remove( dest );

  if ( !QFile::copy( srcPath, dest ) )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "复制失败: %1" ).arg( dest ), 5000 );
    return false;
  }
  if ( statusBar() )
    statusBar()->showMessage(
      tr( "已导出: %1" ).arg( QFileInfo( dest ).fileName() ), 4000 );
  return true;
}

void QgsClassificationMainWindow::exportSelectedStep7()
{
  bool anySuccess = false;
  int attempted = 0;

  if ( m_exportClassifiedCb && m_exportClassifiedCb->isChecked() )
  {
    ++attempted;
    const QString src = m_lastClassifyPath;
    if ( copyPathWithDialog( src, tr( "导出分类 GeoTIFF" ) ) )
      anySuccess = true;
  }

  if ( m_exportPostRasterCb && m_exportPostRasterCb->isChecked() )
  {
    ++attempted;
    const QString src = m_lastPostRasterPath;
    if ( copyPathWithDialog( src, tr( "导出后处理栅格" ) ) )
      anySuccess = true;
  }

  if ( m_exportPostVectorCb && m_exportPostVectorCb->isChecked() )
  {
    ++attempted;
    const QString src = m_lastPostVectorPath;
    if ( copyPathWithDialog( src, tr( "导出后处理矢量" ) ) )
      anySuccess = true;
  }

  if ( m_exportRoiCb && m_exportRoiCb->isChecked() )
  {
    ++attempted;
    const QString path = QFileDialog::getSaveFileName(
      this, tr( "Export ROIs" ), mSession.lastRoisPath(),
      tr( "ESRI Shapefile (*.shp)" ) );
    if ( !path.isEmpty() && saveRoisToPath( path ) )
      anySuccess = true;
  }

  if ( m_exportAccuracyCsvCb && m_exportAccuracyCsvCb->isChecked() )
  {
    ++attempted;
    if ( m_accuracyPanel && m_accuracyPanel->hasResult()
         && m_accuracyPanel->exportCsv() )
      anySuccess = true;
    else if ( statusBar() )
      statusBar()->showMessage( tr( "无精度结果可导出" ), 4000 );
  }

  if ( m_exportProjectCb && m_exportProjectCb->isChecked() )
  {
    ++attempted;
    if ( saveClassificationProject() )
      anySuccess = true;
  }

  if ( attempted == 0 )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "请至少勾选一项导出内容" ), 4000 );
    return;
  }

  if ( anySuccess )
  {
    if ( m_workflow )
      m_workflow->setHasExportedOrLoadedToMain( true );
    refreshWorkflowUi();
    if ( statusBar() )
      statusBar()->showMessage( tr( "导出所选完成" ), 4000 );
  }
}

void QgsClassificationMainWindow::loadClassificationResultToMain()
{
  QString path = !m_lastPostRasterPath.isEmpty()
                   ? m_lastPostRasterPath
                   : m_lastClassifyPath;

  if ( path.isEmpty() || !QFileInfo::exists( path ) )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "无可用的分类/后处理栅格路径" ), 5000 );
    return;
  }

  if ( !m_iface )
  {
    if ( statusBar() )
      statusBar()->showMessage(
        tr( "主窗口接口不可用，无法加载图层（请从主程序打开分类窗口）" ), 6000 );
    return;
  }

  const QString baseName = QFileInfo( path ).completeBaseName();
  QgsRasterLayer *layer = m_iface->addRasterLayer( path, baseName );
  if ( !layer || !layer->isValid() )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "加载到主窗口失败: %1" ).arg( path ), 5000 );
    return;
  }

  if ( m_workflow )
    m_workflow->setHasExportedOrLoadedToMain( true );
  refreshWorkflowUi();
  if ( statusBar() )
    statusBar()->showMessage(
      tr( "已加载到主窗口: %1" ).arg( baseName ), 5000 );
}

bool QgsClassificationMainWindow::saveClassificationProject( QString path )
{
  if ( path.isEmpty() )
  {
    path = QFileDialog::getSaveFileName(
      this, tr( "保存分类项目" ), m_projectPath,
      tr( "Classification project (*.rscproj);;All files (*)" ) );
  }
  if ( path.isEmpty() )
    return false;
  if ( !path.endsWith( QLatin1String( ".rscproj" ), Qt::CaseInsensitive ) )
    path += QStringLiteral( ".rscproj" );

  RsClassificationProjectData data;
  data.version = 1;
  if ( m_workflow )
  {
    data.workflowStep = static_cast<int>( m_workflow->currentStep() );
    data.workflowMode =
      ( m_workflow->mode() == RsClassifyUiMode::Expert )
        ? QStringLiteral( "expert" )
        : QStringLiteral( "wizard" );
    data.evaluateReviewed = m_workflow->evaluateReviewed();
  }
  data.sourceRasterPath = m_sourceRasterPath;
  data.roisPath = mSession.lastRoisPath();
  data.classifiedRasterPath = m_lastClassifyPath;
  data.postProcessRasterPath = m_lastPostRasterPath;
  data.postProcessVectorPath = m_lastPostVectorPath;
  data.accuracySource = m_accuracySource;
  if ( m_accuracyPanel && m_accuracyPanel->hasResult() )
  {
    data.overallAccuracy = m_accuracyPanel->result().overallAccuracy;
    data.kappa = m_accuracyPanel->result().kappa;
  }

  if ( !RsClassificationProject::save( path, data ) )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "保存项目失败: %1" ).arg( path ), 5000 );
    return false;
  }

  m_projectPath = path;
  SICNU_LOG_INFO( SicnuLogTags::Classification,
                  QString( "Classification project saved: %1" ).arg( path ) );
  if ( statusBar() )
    statusBar()->showMessage(
      tr( "已保存项目: %1" ).arg( QFileInfo( path ).fileName() ), 5000 );
  return true;
}

bool QgsClassificationMainWindow::loadProjectFromFile( QString path )
{
  if ( path.isEmpty() )
  {
    path = QFileDialog::getOpenFileName(
      this, tr( "加载分类项目" ), m_projectPath,
      tr( "Classification project (*.rscproj);;All files (*)" ) );
  }
  if ( path.isEmpty() )
    return false;

  RsClassificationProjectData data;
  if ( !RsClassificationProject::load( path, data ) )
  {
    QMessageBox::critical( this, tr( "Error" ),
                           tr( "无法加载项目: %1" ).arg( path ) );
    return false;
  }

  m_projectPath = path;

  // Paths first so UI panels reflect them.
  if ( !data.classifiedRasterPath.isEmpty() )
    m_lastClassifyPath = data.classifiedRasterPath;
  if ( !data.postProcessRasterPath.isEmpty() )
    m_lastPostRasterPath = data.postProcessRasterPath;
  if ( !data.postProcessVectorPath.isEmpty() )
    m_lastPostVectorPath = data.postProcessVectorPath;
  m_accuracySource = data.accuracySource;

  // Optionally restore source raster and ROIs when paths are present.
  if ( !data.sourceRasterPath.isEmpty()
       && QFileInfo::exists( data.sourceRasterPath )
       && data.sourceRasterPath != m_sourceRasterPath )
  {
    openSourceRaster( data.sourceRasterPath );
  }
  if ( !data.roisPath.isEmpty() && QFileInfo::exists( data.roisPath ) && m_rois )
  {
    QgsCoordinateReferenceSystem crs;
    if ( m_sourceLayer )
      crs = m_sourceLayer->crs();
    RsRoiCollection staging;
    if ( RsRoiIO::load( data.roisPath, staging, crs ) )
    {
      mSuppressDirty = true;
      m_rois->clear();
      const auto defs = staging.classDefs();
      for ( auto it = defs.constBegin(); it != defs.constEnd(); ++it )
        m_rois->setClassDef( it.value() );
      for ( int i = 0; i < staging.size(); ++i )
      {
        RsRoi roi = staging.at( i );
        if ( m_sourceWidth > 0 && m_sourceHeight > 0 )
        {
          QSet<quint64> pixels = RsPixelRasterizer::rasterize(
            roi.geometry(), m_sourceGt, m_sourceWidth, m_sourceHeight );
          roi.setPixelIndices( QVector<quint64>( pixels.begin(), pixels.end() ) );
        }
        m_rois->appendRoi( roi );
      }
      mSuppressDirty = false;
      syncSampleLayerFromRois();
      applySampleLayerRenderer();
      mSession.setLastRoisPath( data.roisPath );
      mSession.clearDirty();
    }
  }

  if ( m_workflow )
  {
    m_workflow->setEvaluateReviewed( data.evaluateReviewed );
    if ( !data.classifiedRasterPath.isEmpty()
         && QFileInfo::exists( data.classifiedRasterPath ) )
    {
      m_workflow->setHasFullClassifyResult( true );
      if ( m_workflowBridge )
        m_workflowBridge->setClassifiedOutputArtifact(
          data.classifiedRasterPath.toStdString() );
    }
    if ( !data.postProcessRasterPath.isEmpty()
         && QFileInfo::exists( data.postProcessRasterPath ) )
      m_workflow->setHasPostProcessResult( true );

    const RsClassifyUiMode mode =
      ( data.workflowMode == QLatin1String( "expert" ) )
        ? RsClassifyUiMode::Expert
        : RsClassifyUiMode::Wizard;
    m_workflow->setMode( mode );

    int step = data.workflowStep;
    if ( step < 0 )
      step = 0;
    if ( step >= static_cast<int>( RsClassifyStep::Count ) )
      step = static_cast<int>( RsClassifyStep::Count ) - 1;
    m_workflow->setCurrentStep( static_cast<RsClassifyStep>( step ) );
  }
  if ( m_workflowBridge && !m_sourceRasterPath.isEmpty() )
    m_workflowBridge->setSourceRasterArtifact( m_sourceRasterPath.toStdString() );

  syncWorkflowFromRois();
  refreshWorkflowUi();

  SICNU_LOG_INFO( SicnuLogTags::Classification,
                  QString( "Classification project loaded: %1 (step=%2 mode=%3)" )
                    .arg( path )
                    .arg( data.workflowStep )
                    .arg( data.workflowMode ) );
  if ( statusBar() )
    statusBar()->showMessage(
      tr( "已加载项目: %1" ).arg( QFileInfo( path ).fileName() ), 5000 );
  return true;
}
