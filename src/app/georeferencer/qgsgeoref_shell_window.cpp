#include "qgsgeoref_shell_window.h"
#include <QMainWindow>

#include "shell/rs_session_map_workspace.h"
#include "qgsmaptoolpan.h"
#include "qgsmaptoolzoom.h"
#include "qgsmapcanvas.h"
#include "qgsrectangle.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QKeySequence>
#include <QMenu>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QIcon>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaEnum>
#include <QMetaMethod>
#include <QStringList>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QVector>
#include <QWhatsThis>
#include <QWidget>
#include <cmath>
#include <functional>

namespace
{
  void tipAction( QAction *a, const QString &text )
  {
    if ( !a )
      return;
    a->setToolTip( text );
    a->setStatusTip( text );
    a->setWhatsThis( text );
  }

  void tipWidget( QWidget *w, const QString &text )
  {
    if ( !w )
      return;
    w->setToolTip( text );
    w->setStatusTip( text );
    w->setWhatsThis( text );
  }
}

#include "dialogs/dialog_help_catalog.h"
#include "qgis.h"
#include "qgsapplication.h"
#include "qgscoordinatereferencesystem.h"
#include "qgscoordinatetransformcontext.h"
#include "qgsgcplist.h"
#include "qgsgcplistmodel.h"
#include "qgsgcplistwidget.h"
#include "qgsgcppoint.h"
#include "qgsgeorefdatapoint.h"
#include "qgsgeoreftooladdpoint.h"
#include "qgsgeoreftooldeletepoint.h"
#include "qgsgeoreftoolmovepoint.h"
#include "qgsgeoreftransform.h"
#include "qgscoordinatetransform.h"
#include "qgsmapcanvas.h"
#include "qgsmapcoordsdialog.h"
#include "qgsmaplayerstore.h"
#include "qgisinterface.h"
#include "qgsmaptool.h"
#include "qgsmessagelog.h"
#include "qgsproject.h"
#include "qgsrasterchangecoords.h"
#include "qgsrasterlayer.h"
#include "qgsrectangle.h"
#include "qgstaskmanager.h"
#include "rs_georef_task_list.h"
#include "rs_warp_task.h"

QgsGeorefShellWindow::QgsGeorefShellWindow( QgisInterface *iface, QWidget *parent )
  : QMainWindow( parent )
  , mIface( iface )
{
  // ADR 0020 S2: the Georeferencing Session owns the GCP list; any mutation
  // marks the window dirty (unless suppressed during .points load).
  connect( &mGeorefSession, &RsGeoreferencingSession::gcpsChanged, this, [this]() {
    if ( !mSuppressDirtyFromList )
      mGeorefSession.markDirty();
  } );
  // Prefer session map stores (setupSessionMaps); keep legacy store for early paths.
  mLayerStore = new QgsMapLayerStore( this );
}

QgsGeorefShellWindow::~QgsGeorefShellWindow()
{
  qDeleteAll( mDataPoints );
  mDataPoints.clear();
  qDeleteAll( mGcpViewPoints );
  mGcpViewPoints.clear();
}

void QgsGeorefShellWindow::setupSessionMaps()
{
  if ( mSrcCanvas && !mSrcSession )
    mSrcSession = new RsSessionMapWorkspace( mSrcCanvas, this );
  if ( mDstCanvas && !mDstSession )
    mDstSession = new RsSessionMapWorkspace( mDstCanvas, this );
}

void QgsGeorefShellWindow::finishCommonSetup( RsGeorefParamsPanel::Profile profile,
                                              const QString &gcpDockObjectName,
                                              const QString &paramDockObjectName )
{
  // Subclass has created canvases; wire session stacks before loading layers.
  setupSessionMaps();

  mGcpDock = new QDockWidget( tr( "GCP 表" ), this );
  mGcpDock->setObjectName( gcpDockObjectName );
  mGcpDock->setAllowedAreas( Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea );
  tipWidget( mGcpDock, SicnuDialogHelp::shortForTool(
               QStringLiteral( "georef_gcp_table" ), tr( "GCP 控制点表" ) ) );
  mGcpDock->setWhatsThis( SicnuDialogHelp::htmlForTool(
                            QStringLiteral( "georef_gcp_table" ), tr( "GCP 控制点表" ) ) );
  mGcpTable = new QgsGCPListWidget( mGcpDock );
  mGcpTable->setGcpsSource( &mGeorefSession );
  tipWidget( mGcpTable, SicnuDialogHelp::shortForTool(
               QStringLiteral( "georef_gcp_table" ), tr( "GCP 控制点表" ) )
             + QLatin1Char( '\n' )
             + tr( "右键：定位 / 启用禁用 / 编辑 / 删除。Delete 删行。" ) );
  mGcpTable->setWhatsThis( SicnuDialogHelp::htmlForTool(
    QStringLiteral( "georef_gcp_table" ), tr( "GCP 控制点表" ) ) );
  mGcpDock->setWidget( mGcpTable );
  connect( mGcpTable, &QgsGCPListWidget::deleteRowsRequested,
           this, &QgsGeorefShellWindow::deleteGcpRows );
  connect( mGcpTable, &QgsGCPListWidget::zoomToSourceRequested,
           this, &QgsGeorefShellWindow::zoomToGcpSource );
  connect( mGcpTable, &QgsGCPListWidget::zoomToDestRequested,
           this, &QgsGeorefShellWindow::zoomToGcpDest );
  connect( mGcpTable, &QgsGCPListWidget::zoomToBothRequested,
           this, &QgsGeorefShellWindow::zoomToGcpBoth );
  connect( mGcpTable, &QgsGCPListWidget::currentGcpRowChanged,
           this, &QgsGeorefShellWindow::onGcpTableRowChanged );
  addDockWidget( Qt::BottomDockWidgetArea, mGcpDock );

  // Task list dock — Apply/Run enqueues warp jobs here.
  mTaskDock = new QDockWidget( tr( "校正任务" ), this );
  mTaskDock->setObjectName( gcpDockObjectName + QStringLiteral( "_tasks" ) );
  mTaskDock->setAllowedAreas( Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea );
  tipWidget( mTaskDock, SicnuDialogHelp::shortForTool(
               QStringLiteral( "georef_tasks" ), tr( "校正任务列表" ) ) );
  mTaskDock->setWhatsThis( SicnuDialogHelp::htmlForTool(
                             QStringLiteral( "georef_tasks" ), tr( "校正任务列表" ) ) );
  mTaskList = new RsGeorefTaskList( mTaskDock );
  mTaskList->setObjectName( QStringLiteral( "rsGeorefTaskList" ) );
  mTaskList->setWhatsThis( SicnuDialogHelp::htmlForTool(
    QStringLiteral( "georef_tasks" ), tr( "校正任务列表" ) ) );
  mTaskDock->setWidget( mTaskList );
  addDockWidget( Qt::BottomDockWidgetArea, mTaskDock );
  tabifyDockWidget( mGcpDock, mTaskDock );
  mGcpDock->raise();
  resizeDocks( { mGcpDock, mTaskDock }, { 220, 180 }, Qt::Vertical );

  connect( mTaskList, &RsGeorefTaskList::openOutputRequested, this,
           [this]( const QString &path ) {
             if ( statusBar() )
               statusBar()->showMessage( tr( "输出: %1" ).arg( path ), 5000 );
           } );
  connect( mTaskList, &RsGeorefTaskList::loadOutputRequested, this,
           &QgsGeorefShellWindow::loadWarpOutputToProject );
  connect( mTaskList, &RsGeorefTaskList::cancelTaskRequested, this,
           &QgsGeorefShellWindow::cancelWarpTask );

  mParamDock = new QDockWidget( tr( "校正参数" ), this );
  mParamDock->setObjectName( paramDockObjectName );
  mParamDock->setAllowedAreas( Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea );
  tipWidget( mParamDock, SicnuDialogHelp::shortForTool(
               QStringLiteral( "georef_params" ), tr( "校正参数面板" ) ) );
  mParamDock->setWhatsThis( SicnuDialogHelp::htmlForTool(
                              QStringLiteral( "georef_params" ), tr( "校正参数面板" ) ) );
  mParamsPanel = new RsGeorefParamsPanel( mParamDock );
  mParamsPanel->setProfile( profile );
  if ( profile == RsGeorefParamsPanel::Profile::ImageToImage )
    mParamsPanel->setRpcMode( false );
  mParamDock->setWidget( mParamsPanel );
  addDockWidget( Qt::RightDockWidgetArea, mParamDock );
  resizeDocks( { mParamDock }, { 340 }, Qt::Horizontal );

  connect( &mGeorefSession, &RsGeoreferencingSession::gcpsChanged,
           this, &QgsGeorefShellWindow::onPointsChanged );
  connect( &mGeorefSession, &RsGeoreferencingSession::fitChanged,
           this, &QgsGeorefShellWindow::onSessionFitChanged );
  connect( mParamsPanel, &RsGeorefParamsPanel::transformMethodChanged,
           this, &QgsGeorefShellWindow::onTransformMethodChanged );
  connect( mParamsPanel, &RsGeorefParamsPanel::outputPathChanged, this,
           [this]( const QString & ) { updateApplyEnabled(); } );
  connect( mParamsPanel, &RsGeorefParamsPanel::destCrsChanged,
           this, &QgsGeorefShellWindow::refreshFit );
  connect( mParamsPanel, &RsGeorefParamsPanel::demZOffsetChanged,
           this, &QgsGeorefShellWindow::refreshFit );

  createMapTools();
  wireMapToolActions();

  // Canvas UX: smooth render, wheel zoom, context menus
  for ( QgsMapCanvas *c : { mSrcCanvas, mDstCanvas } )
  {
    if ( !c )
      continue;
    c->enableAntiAliasing( true );
    c->setWheelFactor( 2.0 );
    c->setCachingEnabled( true );
  }
  installCanvasContextMenu( mSrcCanvas, true );
  installCanvasContextMenu( mDstCanvas, false );

  mParamsPanel->setActualGcpCount( 0 );
  onTransformMethodChanged();
  refreshFit();

  mGeorefSession.restoreWindow( this );
  applyWorkflowSnapshot( mGeorefSession.restoreWorkflow() );
  mParamsPanel->setProfile( profile );
  if ( profile == RsGeorefParamsPanel::Profile::ImageToImage )
    mParamsPanel->setRpcMode( false );
  onTransformMethodChanged();
  updateToolAvailability();

  addViewMenu();

  // Default tool: pan (browse first, then switch to Add GCP)
  if ( mPanAction )
    mPanAction->setChecked( true );

  connect( &mGeorefSession, &RsGeoreferencingSession::warpFinished, this,
           [this]( long taskCenterId, bool success, const QString &errorMessage,
                   const QString &outputPath ) {
             Q_UNUSED( taskCenterId );
             // Single in-flight warp (Apply is gated while one runs); the
             // session owns the Task Center id, the shell owns the task-list row.
             if ( mActiveWarpTaskListId < 0 )
               return;
             const int listId = mActiveWarpTaskListId;
             mActiveWarpTaskListId = -1;
             mWarpInProgress = false;
             updateApplyEnabled();

             if ( success )
             {
               if ( mTaskList )
                 mTaskList->finishSuccess( listId, 0, 0 );
               statusBar()->showMessage(
                 tr( "任务 #%1 完成: %2 — 双击可加载到主工程" )
                   .arg( listId )
                   .arg( QFileInfo( outputPath ).fileName() ),
                 6000 );
             }
             else if ( errorMessage.contains( QStringLiteral( "ancel" ), Qt::CaseInsensitive ) )
             {
               if ( mTaskList )
                 mTaskList->finishCancelled( listId, 0 );
               statusBar()->showMessage( tr( "任务 #%1 已取消" ).arg( listId ), 4000 );
             }
             else
             {
               if ( mTaskList )
                 mTaskList->finishFailed( listId, errorMessage, 0 );
               statusBar()->showMessage(
                 tr( "任务 #%1 失败: %2" ).arg( listId ).arg( errorMessage ), 6000 );
             }
           } );
}

void QgsGeorefShellWindow::setupStatusBar( const QString &coordObj, const QString &crsObj, const QString &rmsObj )
{
  mCoordLabel = new QLabel( tr( "—" ), this );
  mCoordLabel->setObjectName( coordObj );
  tipWidget( mCoordLabel, tr( "提示与坐标信息。" ) );
  mCrsLabel = new QLabel( tr( "CRS: —" ), this );
  mCrsLabel->setObjectName( crsObj );
  tipWidget( mCrsLabel, tr( "当前相关坐标系摘要。" ) );
  mRmsLabel = new QLabel( tr( "RMS: —" ), this );
  mRmsLabel->setObjectName( rmsObj );
  tipWidget( mRmsLabel, tr( "当前拟合总 RMS。点数不足或未拟合时显示 —。" ) );
  statusBar()->addWidget( mCoordLabel, 1 );
  statusBar()->addPermanentWidget( mCrsLabel );
  statusBar()->addPermanentWidget( mRmsLabel );
  statusBar()->showMessage( tr( "准备就绪 — 打开源影像并选取 GCP，设置输出后点「运行」" ), 8000 );
}

QWidget *QgsGeorefShellWindow::makeCanvasPanel( QgsMapCanvas *canvas,
                                                QLabel **labelOut,
                                                const QString &roleTitle,
                                                const QString &panelObjectName,
                                                const QString &labelObjectName )
{
  auto *panel = new QWidget( this );
  panel->setObjectName( panelObjectName );
  auto *layout = new QVBoxLayout( panel );
  layout->setContentsMargins( 0, 0, 0, 0 );
  layout->setSpacing( 0 );

  auto *caption = new QLabel( roleTitle + QStringLiteral( ": —" ), panel );
  caption->setObjectName( labelObjectName );
  caption->setMargin( 4 );
  caption->setWordWrap( false );
  caption->setTextInteractionFlags( Qt::TextSelectableByMouse );
  QFont f = caption->font();
  f.setBold( true );
  f.setPointSize( (std::max)( 9, f.pointSize() ) );
  caption->setFont( f );
  caption->setStyleSheet(
    QStringLiteral(
      "QLabel {"
      "  background-color: #f0f3f6;"
      "  color: #24292f;"
      "  border-bottom: 1px solid #d0d7de;"
      "  padding: 4px 8px;"
      "}" ) );
  tipWidget( caption, tr(
    "当前画布对应的图层/文件名。悬停可查看完整路径。" ) );

  layout->addWidget( caption );
  layout->addWidget( canvas, 1 );

  if ( labelOut )
    *labelOut = caption;
  return panel;
}

void QgsGeorefShellWindow::updateSourceLayerCaption()
{
  if ( !mSrcLayerLabel )
    return;

  QString name;
  QString path = mSourceRasterPath;
  if ( mSrcRaster && mSrcRaster->isValid() )
  {
    name = mSrcRaster->name();
    if ( path.isEmpty() )
      path = mSrcRaster->source();
  }
  else if ( !path.isEmpty() )
  {
    name = QFileInfo( path ).fileName();
  }

  if ( name.isEmpty() )
  {
    mSrcLayerLabel->setText( tr( "源 (Warp): —" ) );
    mSrcLayerLabel->setToolTip( tr( "尚未打开源影像（待纠正 / Warp）。File → Open source raster…" ) );
    return;
  }

  mSrcLayerLabel->setText( tr( "源 (Warp): %1" ).arg( name ) );
  mSrcLayerLabel->setToolTip(
    tr( "源影像（待纠正 / Warp）\n图层: %1\n路径: %2" )
      .arg( name, path.isEmpty() ? tr( "—" ) : path ) );
}

void QgsGeorefShellWindow::updateDestLayerCaption( const QString &displayName,
                                                   const QString &fullPathOrTip )
{
  if ( !mDstLayerLabel )
    return;

  if ( displayName.isEmpty() )
  {
    mDstLayerLabel->setText( tr( "基准 (Base): —" ) );
    mDstLayerLabel->setToolTip( tr( "尚未指定基准（参考影像或地图图层）。" ) );
    return;
  }

  mDstLayerLabel->setText( tr( "基准 (Base): %1" ).arg( displayName ) );
  if ( !fullPathOrTip.isEmpty() )
    mDstLayerLabel->setToolTip( fullPathOrTip );
  else
    mDstLayerLabel->setToolTip( tr( "基准图层 / 参考: %1" ).arg( displayName ) );
}

QMenu *QgsGeorefShellWindow::createFileMenu()
{
  auto *fileMenu = menuBar()->addMenu( tr( "文件(&F)" ) );
  mOpenSourceFileAction = fileMenu->addAction(
    QIcon( QStringLiteral( ":/icons/o_en" ) ),
    tr( "从文件打开源影像…" ),
    this, &QgsGeorefShellWindow::openSourceRaster );
  tipAction( mOpenSourceFileAction, tr(
    "从文件打开待校正源影像（SRC / Warp）。显示在源画布，路径用于写出 warp。" ) );
  mOpenSourceLayerAction = fileMenu->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster" ) ),
    tr( "从工程图层打开源影像…" ),
    this, &QgsGeorefShellWindow::openSourceFromProjectLayer );
  tipAction( mOpenSourceLayerAction, tr(
    "从主工程图层列表选择栅格作为源影像（Warp），无需再选文件。" ) );
  return fileMenu;
}

void QgsGeorefShellWindow::addStandardMenuBar()
{
  // View menu is filled by addViewMenu() after toolbar actions exist.
  menuBar()->addMenu( tr( "设置(&S)" ) );
  auto *helpMenu = menuBar()->addMenu( tr( "帮助(&H)" ) );
  auto *about = helpMenu->addAction( tr( "关于本窗口…" ), this, [this]() {
    QMessageBox::information( this, tr( "几何校正帮助" ), windowHelpText() );
  } );
  tipAction( about, tr( "显示本窗口工作流程与各面板说明。" ) );
  auto *whats = helpMenu->addAction( tr( "这是什么？(Shift+F1)" ), this, [this]() {
    QWhatsThis::enterWhatsThisMode();
  } );
  tipAction( whats, tr( "进入「这是什么」模式，再点击控件查看说明。" ) );
}

QString QgsGeorefShellWindow::windowHelpText() const
{
  return tr(
    "<b>影像配准 / 几何校正</b><br><br>"
    "1. 打开源影像（File）<br>"
    "2. 在 SRC 与目标画布上采集 GCP<br>"
    "3. 在右侧设置变换方法、目标 CRS、输出路径<br>"
    "4. 查看残差；点数与方法满足后点工具栏「运行」<br>"
    "5. 在「校正任务」中查看进度；完成后双击可加载结果<br><br>"
    "提示：将鼠标悬停在工具按钮或参数控件上可查看详细说明。" );
}

QActionGroup *QgsGeorefShellWindow::mapToolActionGroup()
{
  if ( !mMapToolActionGroup )
  {
    mMapToolActionGroup = new QActionGroup( this );
    mMapToolActionGroup->setExclusive( true );
  }
  return mMapToolActionGroup;
}

void QgsGeorefShellWindow::addCanvasNavigationActions( QToolBar *bar, const QString &objectNamePrefix )
{
  if ( !bar )
    return;

  QActionGroup *group = mapToolActionGroup();

  mPanAction = bar->addAction( QIcon( QStringLiteral( ":/icons/p_n" ) ), tr( "平移" ) );
  mPanAction->setObjectName( objectNamePrefix + QStringLiteral( "PanAction" ) );
  mPanAction->setCheckable( true );
  mPanAction->setShortcut( QKeySequence( Qt::Key_Space ) );
  mPanAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  tipAction( mPanAction, tr(
    "平移 (Space)：在源与参考/地图画布上拖动浏览。与加点等工具互斥。" ) );
  group->addAction( mPanAction );
  addAction( mPanAction );

  mZoomInAction = bar->addAction( QIcon( QStringLiteral( ":/icons/zoo_in" ) ), tr( "放大" ) );
  mZoomInAction->setObjectName( objectNamePrefix + QStringLiteral( "ZoomInAction" ) );
  mZoomInAction->setCheckable( true );
  mZoomInAction->setShortcut( QKeySequence::ZoomIn );
  mZoomInAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  tipAction( mZoomInAction, tr(
    "放大 (Ctrl++)：框选或点击放大。两侧画布均可用。滚轮也可缩放。" ) );
  group->addAction( mZoomInAction );
  addAction( mZoomInAction );

  mZoomOutAction = bar->addAction( QIcon( QStringLiteral( ":/icons/zoo_out" ) ), tr( "缩小" ) );
  mZoomOutAction->setObjectName( objectNamePrefix + QStringLiteral( "ZoomOutAction" ) );
  mZoomOutAction->setCheckable( true );
  mZoomOutAction->setShortcut( QKeySequence::ZoomOut );
  mZoomOutAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  tipAction( mZoomOutAction, tr(
    "缩小 (Ctrl+-)：框选或点击缩小。两侧画布均可用。" ) );
  group->addAction( mZoomOutAction );
  addAction( mZoomOutAction );

  bar->addSeparator();

  mFitSrcAction = bar->addAction(
    QIcon( QStringLiteral( ":/icons/full_extent" ) ), tr( "适合源" ),
    this, &QgsGeorefShellWindow::fitSourceExtent );
  mFitSrcAction->setObjectName( objectNamePrefix + QStringLiteral( "FitSrcAction" ) );
  mFitSrcAction->setShortcut( QKeySequence( QStringLiteral( "F" ) ) );
  mFitSrcAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  tipAction( mFitSrcAction, tr(
    "适合源 (F)：源影像画布缩放到全图。" ) );
  addAction( mFitSrcAction );

  const QString fitDstLabel = ( shellId() == QLatin1String( "i2i" ) )
                                ? tr( "适合参考" )
                                : tr( "适合地图" );
  mFitDstAction = bar->addAction(
    QIcon( QStringLiteral( ":/icons/full_extent" ) ), fitDstLabel,
    this, &QgsGeorefShellWindow::fitDestExtent );
  mFitDstAction->setObjectName( objectNamePrefix + QStringLiteral( "FitDstAction" ) );
  mFitDstAction->setShortcut( QKeySequence( QStringLiteral( "Shift+F" ) ) );
  mFitDstAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  tipAction( mFitDstAction, tr(
    "适合参考/地图 (Shift+F)：目标画布缩放到全图。" ) );
  addAction( mFitDstAction );

  mFitBothAction = bar->addAction(
    QIcon( QStringLiteral( ":/icons/full_extent" ) ), tr( "适合两侧" ),
    this, &QgsGeorefShellWindow::fitBothExtents );
  mFitBothAction->setObjectName( objectNamePrefix + QStringLiteral( "FitBothAction" ) );
  mFitBothAction->setShortcut( QKeySequence( QStringLiteral( "Ctrl+Shift+F" ) ) );
  mFitBothAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  tipAction( mFitBothAction, tr(
    "适合两侧 (Ctrl+Shift+F)：源与目标画布均缩放到全图。" ) );
  addAction( mFitBothAction );

  bar->addSeparator();

  mZoomPrevAction = bar->addAction(
    QIcon( QStringLiteral( ":/icons/refresh_view" ) ), tr( "上一范围" ),
    this, &QgsGeorefShellWindow::zoomPreviousBoth );
  mZoomPrevAction->setObjectName( objectNamePrefix + QStringLiteral( "ZoomPrevAction" ) );
  mZoomPrevAction->setShortcut( QKeySequence( QStringLiteral( "Alt+Left" ) ) );
  mZoomPrevAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  tipAction( mZoomPrevAction, tr(
    "上一范围 (Alt+←)：两侧画布回退到上一次视图范围。" ) );
  addAction( mZoomPrevAction );

  mZoomNextAction = bar->addAction(
    QIcon( QStringLiteral( ":/icons/refresh_view" ) ), tr( "下一范围" ),
    this, &QgsGeorefShellWindow::zoomNextBoth );
  mZoomNextAction->setObjectName( objectNamePrefix + QStringLiteral( "ZoomNextAction" ) );
  mZoomNextAction->setShortcut( QKeySequence( QStringLiteral( "Alt+Right" ) ) );
  mZoomNextAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  tipAction( mZoomNextAction, tr(
    "下一范围 (Alt+→)：两侧画布前进到下一次视图范围。" ) );
  addAction( mZoomNextAction );
}

void QgsGeorefShellWindow::addGcpEditActions( QToolBar *bar, const QString &objectNamePrefix )
{
  if ( !bar )
    return;

  const QIcon ic( QStringLiteral( ":/icons/r_ster_calc" ) );
  const QIcon icSelect( QStringLiteral( ":/icons/select" ) );
  const QIcon icMove( QStringLiteral( ":/icons/mActionMoveFeature" ) );
  const QIcon icDel( QStringLiteral( ":/icons/mActionDeleteSelectedFeatures" ) );
  tipWidget( bar, tr( "配准工具栏：导航、加点 / 移动 / 删除 GCP，导入导出控制点，运行校正。" ) );

  mAddPointAction = bar->addAction( icSelect.isNull() ? ic : icSelect, tr( "添加控制点" ) );
  mAddPointAction->setObjectName( objectNamePrefix + QStringLiteral( "AddPointAction" ) );
  mAddPointAction->setCheckable( true );
  mAddPointAction->setShortcut( QKeySequence( QStringLiteral( "A" ) ) );
  mAddPointAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  tipAction( mAddPointAction, tr(
    "添加控制点 (A)：\n"
    "1. 在源影像 (SRC) 点击源点\n"
    "2. 在参考/地图上点击同名目标点\n"
    "右键取消未完成的源点。点宜均匀分布。" ) );
  addAction( mAddPointAction );

  mMovePointAction = bar->addAction( icMove.isNull() ? ic : icMove, tr( "移动控制点" ) );
  mMovePointAction->setObjectName( objectNamePrefix + QStringLiteral( "MovePointAction" ) );
  mMovePointAction->setCheckable( true );
  mMovePointAction->setShortcut( QKeySequence( QStringLiteral( "M" ) ) );
  mMovePointAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  tipAction( mMovePointAction, tr(
    "移动控制点 (M)：拖动已有 GCP 标记微调，残差自动重算。" ) );
  addAction( mMovePointAction );

  mDeletePointAction = bar->addAction( icDel.isNull() ? ic : icDel, tr( "删除控制点" ) );
  mDeletePointAction->setObjectName( objectNamePrefix + QStringLiteral( "DeletePointAction" ) );
  mDeletePointAction->setCheckable( true );
  mDeletePointAction->setShortcut( QKeySequence( QStringLiteral( "D" ) ) );
  mDeletePointAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  tipAction( mDeletePointAction, tr(
    "删除控制点 (D)：点击标记删除；或在 GCP 表中删除行。" ) );
  addAction( mDeletePointAction );

  QActionGroup *group = mapToolActionGroup();
  group->addAction( mAddPointAction );
  group->addAction( mMovePointAction );
  group->addAction( mDeletePointAction );

  mLoadGcpAction = bar->addAction( QIcon( QStringLiteral( ":/icons/o_en" ) ), tr( "加载控制点" ),
                                   this, &QgsGeorefShellWindow::loadPoints );
  tipAction( mLoadGcpAction, tr( "从 .points / .gcp 文件加载控制点。" ) );
  mSaveGcpAction = bar->addAction( QIcon( QStringLiteral( ":/icons/s_ve" ) ), tr( "导出控制点" ),
                                   this, &QgsGeorefShellWindow::savePoints );
  tipAction( mSaveGcpAction, tr( "导出控制点为 .points 文件。" ) );
}

void QgsGeorefShellWindow::addApplyAction( QToolBar *bar, const QString &objectName )
{
  if ( !bar )
    return;

  auto *spacer = new QWidget( this );
  spacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
  bar->addWidget( spacer );

  mApplyAction = bar->addAction(
    QIcon( QStringLiteral( ":/icons/quick_run" ) ),
    tr( "运行" ), this, &QgsGeorefShellWindow::applyTransform );
  mApplyAction->setObjectName( objectName );
  tipAction( mApplyAction, tr(
    "运行几何校正：校验 GCP / 输出路径后，将任务加入「校正任务」列表并后台执行 warp。\n"
    "可多次运行形成多条任务；运行中可在任务列表取消。" ) );
  mApplyAction->setEnabled( false );
}

void QgsGeorefShellWindow::createMapTools()
{
  if ( !mSrcCanvas )
    return;

  // Dual-canvas GCP (I2I): SRC → source, REF → destination.
  // Map-coords dialog mode (I2M): only SRC tools; destination via table or main map.
  // IMPORTANT: do NOT setAction() on dual-canvas add tools (shared QAction race).
  mAddPointTool = new QgsGeorefToolAddPoint( mSrcCanvas );
  mAddPointTool->setParent( this );
  connect( mAddPointTool, &QgsGeorefToolAddPoint::pointPicked,
           this, &QgsGeorefShellWindow::onSourcePointPicked );
  connect( mAddPointTool, &QgsGeorefToolAddPoint::canceled, this, [this]() {
    if ( !mHasPendingSource )
      return;
    clearPendingGcpPick();
    if ( statusBar() )
      statusBar()->showMessage( tr( "已取消未完成的源点" ), 3000 );
  } );

  if ( mDstCanvas && !usesMapCoordsDialogForGcp() )
  {
    mAddPointToolDst = new QgsGeorefToolAddPoint( mDstCanvas );
    mAddPointToolDst->setParent( this );
    connect( mAddPointToolDst, &QgsGeorefToolAddPoint::pointPicked,
             this, &QgsGeorefShellWindow::onDestPointPicked );
    connect( mAddPointToolDst, &QgsGeorefToolAddPoint::canceled, this, [this]() {
      if ( !mHasPendingSource )
        return;
      clearPendingGcpPick();
      if ( statusBar() )
        statusBar()->showMessage( tr( "已取消未完成的源点" ), 3000 );
    } );
  }

  mToolMoveSrc = new QgsGeorefToolMovePoint( mSrcCanvas );
  mToolMoveSrc->setParent( this );
  connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointBeginMove, this, &QgsGeorefShellWindow::selectPoint );
  connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointMoving, this, &QgsGeorefShellWindow::movePoint );
  connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointEndMove, this, &QgsGeorefShellWindow::releasePoint );
  connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointCancelMove, this, &QgsGeorefShellWindow::cancelPoint );

  if ( mDstCanvas )
  {
    mToolMoveDst = new QgsGeorefToolMovePoint( mDstCanvas );
    mToolMoveDst->setParent( this );
    connect( mToolMoveDst, &QgsGeorefToolMovePoint::pointBeginMove, this, &QgsGeorefShellWindow::selectPoint );
    connect( mToolMoveDst, &QgsGeorefToolMovePoint::pointMoving, this, &QgsGeorefShellWindow::movePoint );
    connect( mToolMoveDst, &QgsGeorefToolMovePoint::pointEndMove, this, &QgsGeorefShellWindow::releasePoint );
    connect( mToolMoveDst, &QgsGeorefToolMovePoint::pointCancelMove, this, &QgsGeorefShellWindow::cancelPoint );
  }

  const auto clearMoveHover = [this]() {
    mMovingPoint = nullptr;
    if ( mHoveredPoint )
    {
      mHoveredPoint->setHovered( false );
      mHoveredPoint = nullptr;
    }
  };
  connect( mToolMoveSrc, &QgsMapTool::deactivated, this, clearMoveHover );
  if ( mToolMoveDst )
    connect( mToolMoveDst, &QgsMapTool::deactivated, this, clearMoveHover );

  mToolDeleteSrc = new QgsGeorefToolDeletePoint( mSrcCanvas );
  mToolDeleteSrc->setParent( this );
  connect( mToolDeleteSrc, &QgsGeorefToolDeletePoint::deletePoint, this, &QgsGeorefShellWindow::deletePointAt );
  connect( mToolDeleteSrc, &QgsGeorefToolDeletePoint::hoverPoint, this, &QgsGeorefShellWindow::hoverPoint );

  if ( mDstCanvas )
  {
    mToolDeleteDst = new QgsGeorefToolDeletePoint( mDstCanvas );
    mToolDeleteDst->setParent( this );
    connect( mToolDeleteDst, &QgsGeorefToolDeletePoint::deletePoint, this, &QgsGeorefShellWindow::deletePointAt );
    connect( mToolDeleteDst, &QgsGeorefToolDeletePoint::hoverPoint, this, &QgsGeorefShellWindow::hoverPoint );
  }

  const auto clearDeleteHover = [this]() {
    if ( mHoveredPoint )
    {
      mHoveredPoint->setHovered( false );
      mHoveredPoint = nullptr;
    }
  };
  connect( mToolDeleteSrc, &QgsMapTool::deactivated, this, clearDeleteHover );
  if ( mToolDeleteDst )
    connect( mToolDeleteDst, &QgsMapTool::deactivated, this, clearDeleteHover );

  // Navigation tools (SRC always; dest only when dual-canvas)
  mPanSrc = new QgsMapToolPan( mSrcCanvas );
  mPanSrc->setParent( this );
  mZoomInSrc = new QgsMapToolZoom( mSrcCanvas, false /* zoom in */ );
  mZoomInSrc->setParent( this );
  mZoomOutSrc = new QgsMapToolZoom( mSrcCanvas, true /* zoom out */ );
  mZoomOutSrc->setParent( this );

  if ( mDstCanvas )
  {
    mPanDst = new QgsMapToolPan( mDstCanvas );
    mPanDst->setParent( this );
    mZoomInDst = new QgsMapToolZoom( mDstCanvas, false );
    mZoomInDst->setParent( this );
    mZoomOutDst = new QgsMapToolZoom( mDstCanvas, true );
    mZoomOutDst->setParent( this );
  }
}

void QgsGeorefShellWindow::rearmAddPointTools()
{
  if ( !mAddPointAction || !mAddPointAction->isChecked() )
    return;
  if ( mSrcCanvas && mAddPointTool )
    mSrcCanvas->setMapTool( mAddPointTool );
  if ( mDstCanvas && mAddPointToolDst )
    mDstCanvas->setMapTool( mAddPointToolDst );
}

QgsPointXY QgsGeorefShellWindow::mapPickToLayerCrs( QgsMapCanvas *canvas, QgsRasterLayer *layer,
                                                     const QgsPointXY &canvasMapPt ) const
{
  if ( !canvas || !layer || !layer->isValid() )
    return canvasMapPt;

  const QgsCoordinateReferenceSystem canvasCrs = canvas->mapSettings().destinationCrs();
  const QgsCoordinateReferenceSystem layerCrs = layer->crs();
  if ( !canvasCrs.isValid() || !layerCrs.isValid() || canvasCrs == layerCrs )
    return canvasMapPt;

  try
  {
    const QgsCoordinateTransform ct(
      canvasCrs, layerCrs,
      QgsProject::instance() ? QgsProject::instance()->transformContext()
                             : QgsCoordinateTransformContext() );
    return ct.transform( canvasMapPt );
  }
  catch ( ... )
  {
    return canvasMapPt;
  }
}

void QgsGeorefShellWindow::updateGcpTableRasterPaths()
{
  if ( !mGcpTable || !mGcpTable->gcpModel() )
    return;
  // ADR 0020 S3: the GCP table model is presentation-only. The GDAL
  // geotransform reads happen here in the shell and are injected into the
  // model as plain pixel-converter callables.
  std::function<QgsPointXY( const QgsPointXY & )> srcToPixel;
  std::function<QgsPointXY( const QgsPointXY & )> dstToPixel;
  if ( !mSourceRasterPath.isEmpty() )
  {
    QgsRasterChangeCoords srcCoords;
    srcCoords.loadRaster( mSourceRasterPath );
    if ( srcCoords.hasExistingGeoreference() )
      srcToPixel = [srcCoords]( const QgsPointXY &p ) { return srcCoords.toColumnLine( p ); };
  }
  if ( !mDestRasterPath.isEmpty() )
  {
    QgsRasterChangeCoords dstCoords;
    dstCoords.loadRaster( mDestRasterPath );
    if ( dstCoords.hasExistingGeoreference() )
      dstToPixel = [dstCoords]( const QgsPointXY &p ) { return dstCoords.toColumnLine( p ); };
  }
  mGcpTable->gcpModel()->setPixelConverters( srcToPixel, dstToPixel );
}

void QgsGeorefShellWindow::wireMapToolActions()
{
  if ( mAddPointAction )
  {
    connect( mAddPointAction, &QAction::toggled, this, [this]( bool on ) {
      if ( !on )
      {
        clearPendingGcpPick();
        if ( mSrcCanvas && mAddPointTool && mSrcCanvas->mapTool() == mAddPointTool )
          mSrcCanvas->unsetMapTool( mAddPointTool );
        if ( mDstCanvas && mAddPointToolDst && mDstCanvas->mapTool() == mAddPointToolDst )
          mDstCanvas->unsetMapTool( mAddPointToolDst );
        return;
      }
      rearmAddPointTools();
      if ( statusBar() )
      {
        if ( usesMapCoordsDialogForGcp() )
          statusBar()->showMessage(
            tr( "添加 GCP：在源影像上点击像点，然后在对话框中填写地图坐标或从主窗口地图取点" ), 8000 );
        else
          statusBar()->showMessage(
            tr( "添加 GCP：先在源画布点击源点，再在参考影像上点击同名位置（右键取消）" ), 8000 );
      }
    } );
  }
  if ( mMovePointAction )
  {
    connect( mMovePointAction, &QAction::toggled, this, [this]( bool on ) {
      if ( !on )
        return;
      clearPendingGcpPick();
      if ( mSrcCanvas )
        mSrcCanvas->setMapTool( mToolMoveSrc );
      if ( mDstCanvas )
        mDstCanvas->setMapTool( mToolMoveDst );
    } );
  }
  if ( mDeletePointAction )
  {
    connect( mDeletePointAction, &QAction::toggled, this, [this]( bool on ) {
      if ( !on )
        return;
      clearPendingGcpPick();
      if ( mSrcCanvas )
        mSrcCanvas->setMapTool( mToolDeleteSrc );
      if ( mDstCanvas )
        mDstCanvas->setMapTool( mToolDeleteDst );
    } );
  }
  if ( mPanAction )
  {
    connect( mPanAction, &QAction::toggled, this, [this]( bool on ) {
      if ( !on )
        return;
      clearPendingGcpPick();
      if ( mSrcCanvas && mPanSrc )
        mSrcCanvas->setMapTool( mPanSrc );
      if ( mDstCanvas && mPanDst )
        mDstCanvas->setMapTool( mPanDst );
      if ( statusBar() )
        statusBar()->showMessage( tr( "平移：拖动画布浏览。两侧画布均可操作。" ), 4000 );
    } );
  }
  if ( mZoomInAction )
  {
    connect( mZoomInAction, &QAction::toggled, this, [this]( bool on ) {
      if ( !on )
        return;
      clearPendingGcpPick();
      if ( mSrcCanvas && mZoomInSrc )
        mSrcCanvas->setMapTool( mZoomInSrc );
      if ( mDstCanvas && mZoomInDst )
        mDstCanvas->setMapTool( mZoomInDst );
      if ( statusBar() )
        statusBar()->showMessage( tr( "放大：点击或框选放大。" ), 4000 );
    } );
  }
  if ( mZoomOutAction )
  {
    connect( mZoomOutAction, &QAction::toggled, this, [this]( bool on ) {
      if ( !on )
        return;
      clearPendingGcpPick();
      if ( mSrcCanvas && mZoomOutSrc )
        mSrcCanvas->setMapTool( mZoomOutSrc );
      if ( mDstCanvas && mZoomOutDst )
        mDstCanvas->setMapTool( mZoomOutDst );
      if ( statusBar() )
        statusBar()->showMessage( tr( "缩小：点击或框选缩小。" ), 4000 );
    } );
  }
}

void QgsGeorefShellWindow::fitSourceExtent()
{
  if ( !mSrcCanvas )
    return;
  if ( mSrcRaster && mSrcRaster->isValid() )
  {
    QgsRectangle e = mSrcRaster->extent();
    e.scale( 1.02 ); // slight margin
    mSrcCanvas->setExtent( e );
    mSrcCanvas->refresh();
  }
  else
  {
    mSrcCanvas->zoomToFullExtent();
  }
  if ( statusBar() )
    statusBar()->showMessage( tr( "源画布已适合全图" ), 2500 );
}

void QgsGeorefShellWindow::fitDestExtent()
{
  if ( !mDstCanvas )
    return;
  if ( mDstRaster && mDstRaster->isValid() )
  {
    QgsRectangle e = mDstRaster->extent();
    e.scale( 1.02 );
    mDstCanvas->setExtent( e );
    mDstCanvas->refresh();
  }
  else
  {
    // I2M: full extent of currently set map layers
    mDstCanvas->zoomToFullExtent();
  }
  if ( statusBar() )
    statusBar()->showMessage(
      ( shellId() == QLatin1String( "i2i" ) )
        ? tr( "参考画布已适合全图" )
        : tr( "地图画布已适合全图" ),
      2500 );
}

void QgsGeorefShellWindow::fitBothExtents()
{
  fitSourceExtent();
  fitDestExtent();
}

void QgsGeorefShellWindow::zoomSourceIn()
{
  if ( mSrcCanvas )
    mSrcCanvas->zoomIn();
}

void QgsGeorefShellWindow::zoomSourceOut()
{
  if ( mSrcCanvas )
    mSrcCanvas->zoomOut();
}

void QgsGeorefShellWindow::zoomPreviousSource()
{
  if ( mSrcCanvas )
    mSrcCanvas->zoomToPreviousExtent();
}

void QgsGeorefShellWindow::zoomNextSource()
{
  if ( mSrcCanvas )
    mSrcCanvas->zoomToNextExtent();
}

void QgsGeorefShellWindow::zoomPreviousDest()
{
  if ( mDstCanvas )
    mDstCanvas->zoomToPreviousExtent();
}

void QgsGeorefShellWindow::zoomNextDest()
{
  if ( mDstCanvas )
    mDstCanvas->zoomToNextExtent();
}

void QgsGeorefShellWindow::zoomPreviousBoth()
{
  zoomPreviousSource();
  zoomPreviousDest();
  if ( statusBar() )
    statusBar()->showMessage( tr( "已回退上一视图范围" ), 2000 );
}

void QgsGeorefShellWindow::zoomNextBoth()
{
  zoomNextSource();
  zoomNextDest();
  if ( statusBar() )
    statusBar()->showMessage( tr( "已前进下一视图范围" ), 2000 );
}

void QgsGeorefShellWindow::addViewMenu()
{
  // Insert View before Settings/Help if present
  QMenu *viewMenu = nullptr;
  for ( QAction *a : menuBar()->actions() )
  {
    if ( a->menu() && a->text().contains( tr( "视图" ) ) )
    {
      viewMenu = a->menu();
      break;
    }
  }
  if ( !viewMenu )
    viewMenu = menuBar()->addMenu( tr( "视图(&V)" ) );
  viewMenu->clear();
  viewMenu->setToolTipsVisible( true );
  if ( mPanAction )
    viewMenu->addAction( mPanAction );
  if ( mZoomInAction )
    viewMenu->addAction( mZoomInAction );
  if ( mZoomOutAction )
    viewMenu->addAction( mZoomOutAction );
  viewMenu->addSeparator();
  if ( mFitSrcAction )
    viewMenu->addAction( mFitSrcAction );
  if ( mFitDstAction )
    viewMenu->addAction( mFitDstAction );
  if ( mFitBothAction )
    viewMenu->addAction( mFitBothAction );
  viewMenu->addSeparator();
  if ( mZoomPrevAction )
    viewMenu->addAction( mZoomPrevAction );
  if ( mZoomNextAction )
    viewMenu->addAction( mZoomNextAction );

  // One-shot zoom steps (both canvases)
  auto *zin = viewMenu->addAction( tr( "放大一级" ), this, [this]() {
    zoomSourceIn();
    zoomDestIn();
  } );
  zin->setShortcut( QKeySequence( QStringLiteral( "+" ) ) );
  zin->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  addAction( zin );
  auto *zout = viewMenu->addAction( tr( "缩小一级" ), this, [this]() {
    zoomSourceOut();
    zoomDestOut();
  } );
  zout->setShortcut( QKeySequence( QStringLiteral( "-" ) ) );
  zout->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  addAction( zout );
}

void QgsGeorefShellWindow::installCanvasContextMenu( QgsMapCanvas *canvas, bool isSource )
{
  if ( !canvas )
    return;

  canvas->setContextMenuPolicy( Qt::CustomContextMenu );
  connect( canvas, &QWidget::customContextMenuRequested, this,
           [this, canvas, isSource]( const QPoint &pos ) {
             // Don't steal right-click while placing GCP (cancel pending) or delete mode
             if ( mAddPointAction && mAddPointAction->isChecked() )
               return;
             if ( mDeletePointAction && mDeletePointAction->isChecked() )
               return;

             QMenu menu( canvas );
             if ( isSource )
             {
               menu.addAction( tr( "适合源全图" ), this, &QgsGeorefShellWindow::fitSourceExtent );
               menu.addAction( tr( "放大一级" ), this, &QgsGeorefShellWindow::zoomSourceIn );
               menu.addAction( tr( "缩小一级" ), this, &QgsGeorefShellWindow::zoomSourceOut );
               menu.addSeparator();
               menu.addAction( tr( "上一范围" ), this, &QgsGeorefShellWindow::zoomPreviousSource );
               menu.addAction( tr( "下一范围" ), this, &QgsGeorefShellWindow::zoomNextSource );
             }
             else
             {
               menu.addAction( shellId() == QLatin1String( "i2i" )
                                 ? tr( "适合参考全图" )
                                 : tr( "适合地图全图" ),
                               this, &QgsGeorefShellWindow::fitDestExtent );
               menu.addAction( tr( "放大一级" ), this, &QgsGeorefShellWindow::zoomDestIn );
               menu.addAction( tr( "缩小一级" ), this, &QgsGeorefShellWindow::zoomDestOut );
               menu.addSeparator();
               menu.addAction( tr( "上一范围" ), this, &QgsGeorefShellWindow::zoomPreviousDest );
               menu.addAction( tr( "下一范围" ), this, &QgsGeorefShellWindow::zoomNextDest );
             }
             menu.addSeparator();
             menu.addAction( tr( "适合两侧" ), this, &QgsGeorefShellWindow::fitBothExtents );
             if ( mPanAction )
               menu.addAction( mPanAction );
             menu.exec( canvas->mapToGlobal( pos ) );
           } );
}

void QgsGeorefShellWindow::zoomDestIn()
{
  if ( mDstCanvas )
    mDstCanvas->zoomIn();
}

void QgsGeorefShellWindow::zoomDestOut()
{
  if ( mDstCanvas )
    mDstCanvas->zoomOut();
}

void QgsGeorefShellWindow::onTransformMethodChanged()
{
  onTransformMethodChangedExtra();
  refreshFit();
}

void QgsGeorefShellWindow::onPointsChanged()
{
  // Session is the sole owner of the GCP list — reconcile view rows/markers.
  const QVector<QgsGcpPoint> &gcps = mGeorefSession.gcps();

  if ( mSaveGcpAction )
    mSaveGcpAction->setEnabled( hasSourceReady() && !gcps.isEmpty() );

  // Destination CRS for the view models: REF/Map canvas CRS, else panel CRS
  // (mirrors commitGcpPair; used only for marker display heuristics).
  QgsCoordinateReferenceSystem destCrs;
  if ( mDstCanvas )
    destCrs = mDstCanvas->mapSettings().destinationCrs();
  if ( !destCrs.isValid() && mParamsPanel )
    destCrs = mParamsPanel->destCrs();

  // Shrink rows beyond the session list.
  while ( mDataPoints.size() > gcps.size() )
  {
    QgsGeorefDataPoint *dp = mDataPoints.takeLast();
    if ( mMovingPoint == dp )
      mMovingPoint = nullptr;
    if ( mHoveredPoint == dp )
      mHoveredPoint = nullptr;
    delete dp;
    delete mGcpViewPoints.takeLast();
  }
  // Grow new rows; 1-based ids on canvas badges.
  while ( mDataPoints.size() < gcps.size() )
  {
    const QgsGcpPoint &pair = gcps.at( mDataPoints.size() );
    auto *pt = new QgsGcpPoint( pair.sourcePoint(), pair.destinationPoint(),
                                destCrs, pair.isEnabled() );
    pt->setPointType( pair.pointType() );
    auto *dp = new QgsGeorefDataPoint( mSrcCanvas, mDstCanvas, pt );
    dp->setParent( this );
    mGcpViewPoints.append( pt );
    mDataPoints.append( dp );
  }
  // Sync every view row from the session (session → view direction only).
  for ( int i = 0; i < gcps.size(); ++i )
  {
    const QgsGcpPoint &pair = gcps.at( i );
    QgsGcpPoint *pt = mGcpViewPoints.at( i );
    pt->setSourcePoint( pair.sourcePoint() );
    pt->setDestinationPoint( pair.destinationPoint() );
    pt->setEnabled( pair.isEnabled() );
    pt->setPointType( pair.pointType() );
    QgsGeorefDataPoint *dp = mDataPoints.at( i );
    dp->setId( i + 1 );
    dp->updateMarkers();
  }

  if ( mSrcCanvas )
    mSrcCanvas->refresh();
  if ( mDstCanvas )
    mDstCanvas->refresh();
}

void QgsGeorefShellWindow::syncAllMarkers()
{
  for ( QgsGeorefDataPoint *dp : mDataPoints )
  {
    if ( dp )
      dp->updateMarkers();
  }
  if ( mSrcCanvas )
    mSrcCanvas->update();
  if ( mDstCanvas )
    mDstCanvas->update();
}

void QgsGeorefShellWindow::panCanvasToPoint( QgsMapCanvas *canvas, const QgsPointXY &mapPoint )
{
  if ( !canvas )
    return;
  QgsRectangle extent = canvas->extent();
  if ( extent.isEmpty() || !extent.isFinite() )
  {
    constexpr double half = 50.0;
    extent = QgsRectangle( mapPoint.x() - half, mapPoint.y() - half,
                           mapPoint.x() + half, mapPoint.y() + half );
  }
  else
  {
    const double w = extent.width();
    const double h = extent.height();
    extent = QgsRectangle( mapPoint.x() - w / 2.0, mapPoint.y() - h / 2.0,
                           mapPoint.x() + w / 2.0, mapPoint.y() + h / 2.0 );
  }
  canvas->setExtent( extent );
  canvas->refresh();
}

void QgsGeorefShellWindow::setSelectedGcpRow( int row )
{
  for ( int r = 0; r < mDataPoints.size(); ++r )
  {
    QgsGeorefDataPoint *dp = mDataPoints.at( r );
    if ( dp )
      dp->setSelected( r == row );
  }
  if ( mSrcCanvas )
    mSrcCanvas->update();
  if ( mDstCanvas )
    mDstCanvas->update();
}

void QgsGeorefShellWindow::zoomToGcpSource( int row )
{
  if ( row < 0 || row >= mGeorefSession.gcps().size() )
    return;
  setSelectedGcpRow( row );
  panCanvasToPoint( mSrcCanvas, mGeorefSession.gcps().at( row ).sourcePoint() );
  if ( statusBar() )
    statusBar()->showMessage( tr( "已定位到源点 #%1" ).arg( row + 1 ), 3000 );
}

void QgsGeorefShellWindow::zoomToGcpDest( int row )
{
  if ( row < 0 || row >= mGeorefSession.gcps().size() )
    return;
  setSelectedGcpRow( row );
  QgsGeorefDataPoint *dp = mDataPoints.value( row, nullptr );
  const QgsPointXY dest = dp ? dp->destinationDisplayPoint()
                             : mGeorefSession.gcps().at( row ).destinationPoint();
  if ( mDstCanvas )
  {
    panCanvasToPoint( mDstCanvas, dest );
  }
  else if ( usesMapCoordsDialogForGcp() )
  {
    // I2M: locate destination on the main application map.
    QgsMapCanvas *mainMap = mainApplicationMapCanvas();
    if ( mainMap )
    {
      panCanvasToPoint( mainMap, dest );
      if ( QWidget *w = mainMap->window() )
      {
        w->raise();
        w->activateWindow();
      }
    }
  }
  if ( statusBar() )
    statusBar()->showMessage( tr( "已定位到目标点 #%1" ).arg( row + 1 ), 3000 );
}

void QgsGeorefShellWindow::zoomToGcpBoth( int row )
{
  zoomToGcpSource( row );
  zoomToGcpDest( row );
  if ( statusBar() )
    statusBar()->showMessage( tr( "已两侧定位 GCP #%1" ).arg( row + 1 ), 3000 );
}

void QgsGeorefShellWindow::onGcpTableRowChanged( int row )
{
  setSelectedGcpRow( row );
}

void QgsGeorefShellWindow::refreshFit()
{
  if ( !mParamsPanel )
    return;

  // Push panel config into the session, then refit — the session is the only
  // place a fit is computed (ADR 0020 S2). fitChanged drives the UI update.
  mGeorefSession.setSourceRasterPath( mSourceRasterPath );
  mGeorefSession.setTransformMethod( mParamsPanel->transformMethod() );
  mGeorefSession.setDemPath( mParamsPanel->demPath() );
  mGeorefSession.setDemZOffset( mParamsPanel->demZOffset() );
  mGeorefSession.refit();
}

void QgsGeorefShellWindow::onSessionFitChanged( const RsGeorefFitResult &fit )
{
  if ( !mParamsPanel )
    return;

  const QVector<QgsGcpPoint> &gcps = mGeorefSession.gcps();

  mParamsPanel->setMinimumGcpCount( fit.minimumGcpCount );
  mParamsPanel->setActualGcpCount( fit.enabledGcpCount );

  // Prefer dest canvas CRS for residual display (REF/Map image coords).
  QgsCoordinateReferenceSystem dstCrs = mParamsPanel->destCrs();
  if ( mDstCanvas && mDstCanvas->mapSettings().destinationCrs().isValid() )
  {
    if ( !dstCrs.isValid() )
      dstCrs = mDstCanvas->mapSettings().destinationCrs();
  }
  const QgsCoordinateTransformContext transformContext =
    QgsProject::instance() ? QgsProject::instance()->transformContext()
                           : QgsCoordinateTransformContext();

  // Keep GCP table in sync: col/row display mode + residual unit labels.
  if ( mGcpTable && mGcpTable->gcpModel() )
  {
    mGcpTable->gcpModel()->setCoordinateDisplayMode(
      mGcpTable->gcpModel()->sourceHasExistingGeoreference(),
      /*residualIsMap=*/false,
      dstCrs.isValid() ? dstCrs.authid() : QString() );
    mGcpTable->gcpModel()->setTargetCrs( dstCrs, transformContext );
  }

  // RPC refinement diagnostic (ported from recomputeFit): show before/after
  // only when the session produced both values.
  if ( fit.ready && fit.refinementRmsBefore >= 0.0 && fit.rms >= 0.0 )
    mParamsPanel->setRefinementRms( fit.refinementRmsBefore, fit.rms );
  else
    mParamsPanel->clearRefinementRms();

  double xSq = 0.0, ySq = 0.0, maxMag = 0.0;
  int maxRow = -1;
  int included = 0;
  QVector<QPointF> scatter;

  if ( fit.ready )
  {
    scatter.reserve( fit.enabledGcpCount );
    for ( int i = 0; i < gcps.size() && i < fit.residuals.size(); ++i )
    {
      if ( !gcps.at( i ).isEnabled() )
        continue;
      const QPointF r = fit.residuals.at( i );
      if ( !rsGeorefResidualIsValid( r ) )
        continue;
      scatter.push_back( r );
      const double mag = std::hypot( r.x(), r.y() );
      xSq += r.x() * r.x();
      ySq += r.y() * r.y();
      if ( mag > maxMag ) { maxMag = mag; maxRow = i; }
      ++included;
    }
  }

  if ( fit.ready && included > 0 )
  {
    mParamsPanel->setRmsValues( gcps.size(), fit.enabledGcpCount, fit.rms,
                                std::sqrt( xSq / included ), std::sqrt( ySq / included ),
                                maxMag, maxRow );
  }
  else
  {
    mParamsPanel->setRmsValues( gcps.size(), fit.enabledGcpCount, 0, 0, 0, 0, -1 );
  }

  // Push residuals into the per-row view models (markers). Mirrors the old
  // updateResiduals/clearResiduals semantics: on a failed fit every row goes
  // to (0,0); on a good fit disabled rows keep their previous residual.
  // Residuals now live on QgsGeorefDataPoint (ADR 0056) — QgsGcpPoint no
  // longer carries them.
  for ( int i = 0; i < mDataPoints.size(); ++i )
  {
    QgsGeorefDataPoint *dp = mDataPoints.at( i );
    if ( !dp )
      continue;
    if ( !fit.ready )
    {
      dp->setResidual( QPointF() );
    }
    else if ( i < gcps.size() && gcps.at( i ).isEnabled()
              && i < fit.residuals.size() && rsGeorefResidualIsValid( fit.residuals.at( i ) ) )
    {
      dp->setResidual( fit.residuals.at( i ) );
    }
  }

  // Always push residual / position to canvas badges after fit.
  syncAllMarkers();
  if ( mGcpTable && mGcpTable->gcpModel() )
    mGcpTable->gcpModel()->refreshAll();

  mParamsPanel->setResidualScatter( scatter );
  if ( mRmsLabel )
  {
    if ( fit.ready && fit.rms > 0.0 )
      mRmsLabel->setText( tr( "RMS: %1 px" ).arg( QString::number( fit.rms, 'f', 3 ) ) );
    else
      mRmsLabel->setText( tr( "RMS: —" ) );
  }
  updateApplyEnabled();
}

void QgsGeorefShellWindow::updateApplyEnabled()
{
  if ( !mApplyAction || !mParamsPanel )
    return;
  const RsGeorefFitResult &fit = mGeorefSession.lastFit();
  mApplyAction->setEnabled( fit.ready && fit.enabledGcpCount >= fit.minimumGcpCount
                            && !mParamsPanel->outputPath().isEmpty()
                            && !mWarpInProgress
                            && hasSourceReady() && hasDestReady() );
}

void QgsGeorefShellWindow::applyTransform()
{
  if ( !mParamsPanel || !mTaskList )
    return;

  const auto method = mParamsPanel->transformMethod();
  // Counts via the fit engine (ADR 0057) — same seam createWarpSnapshot uses.
  const int minN = QgsGeorefTransform::minimumGcpCountFor( method );
  const int enabled = QgsGeorefTransform::enabledGcpCount( mGeorefSession.gcps() );

  if ( enabled < minN )
  {
    statusBar()->showMessage( tr( "GCP 数量不足（需要 %1，实际 %2）" ).arg( minN ).arg( enabled ), 3000 );
    return;
  }
  if ( mParamsPanel->outputPath().isEmpty() )
  {
    statusBar()->showMessage( tr( "请填写输出路径" ), 3000 );
    return;
  }
  if ( mSourceRasterPath.isEmpty() )
  {
    statusBar()->showMessage( tr( "未指定源栅格路径" ), 3000 );
    return;
  }
  if ( !mGeorefSession.lastFit().ready )
  {
    statusBar()->showMessage( tr( "变换尚未完成拟合" ), 3000 );
    return;
  }

  // Freeze GCP + method + paths into an immutable session snapshot (#32),
  // then run the warp through the session's Task Center executor so later
  // panel edits cannot affect it. The session already holds the live GCP
  // list and fit (ADR 0020 S2) — no re-push needed here.
  const QString sourcePath = mSourceRasterPath;
  const QString outputPath = mParamsPanel->outputPath();
  const double rmsAtStart = mGeorefSession.lastFit().rms;
  const auto resampling = mParamsPanel->resamplingMethod();
  const auto destCrs = mParamsPanel->destCrs();
  const double pixelSize = mParamsPanel->outputPixelSize();

  const auto snapOpt = mGeorefSession.createWarpSnapshot(
    outputPath, resampling, destCrs, pixelSize );
  if ( !snapOpt.has_value() )
  {
    statusBar()->showMessage( tr( "无法创建校正快照" ), 3000 );
    return;
  }

  QString methodLabel;
  {
    const QMetaEnum me = QMetaEnum::fromType<QgsGcpTransformerInterface::TransformMethod>();
    const char *key = me.valueToKey( static_cast<int>( method ) );
    methodLabel = QString::fromUtf8( key ? key : "?" );
  }

  const RsGeorefTaskList::Kind kind =
    ( shellId() == QLatin1String( "i2m" ) )
      ? RsGeorefTaskList::Kind::WarpI2M
      : RsGeorefTaskList::Kind::WarpI2I;

  const QString title = tr( "%1 → %2" )
                          .arg( QFileInfo( sourcePath ).fileName() )
                          .arg( QFileInfo( outputPath ).fileName() );

  const int taskId = mTaskList->beginTask( kind, title, methodLabel,
                                           sourcePath, outputPath,
                                           enabled, rmsAtStart );

  if ( mTaskDock )
  {
    mTaskDock->show();
    mTaskDock->raise();
  }

  const long tcId = mGeorefSession.startWarpTask( *snapOpt );
  if ( tcId < 0 )
  {
    if ( mTaskList )
      mTaskList->finishFailed( taskId, tr( "Task Center 提交失败" ), 0 );
    statusBar()->showMessage( tr( "无法提交校正任务" ), 3000 );
    return;
  }

  mActiveWarpTaskListId = taskId;
  mWarpInProgress = true;
  updateApplyEnabled();
  statusBar()->showMessage( tr( "已加入任务列表 #%1 并开始运行…" ).arg( taskId ), 3000 );
}

void QgsGeorefShellWindow::cancelWarpTask( int taskId )
{
  if ( taskId < 0 || taskId != mActiveWarpTaskListId )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "任务 #%1 已不在运行" ).arg( taskId ), 3000 );
    return;
  }
  mGeorefSession.cancelWarpTask( mGeorefSession.pendingWarpTaskId() );
  if ( statusBar() )
    statusBar()->showMessage( tr( "正在取消任务 #%1…" ).arg( taskId ), 3000 );
}

void QgsGeorefShellWindow::loadWarpOutputToProject( const QString &path )
{
  if ( path.isEmpty() )
    return;

  // Wave E: prefer signal so main can route through loadDataLayer once.
  const QMetaMethod loadSig =
      QMetaMethod::fromSignal( &QgsGeorefShellWindow::requestLoadToMainMap );
  if ( isSignalConnected( loadSig ) )
  {
    emit requestLoadToMainMap( path );
  }
  else
  {
    // No shell host (tests / standalone): try parent slot, then QgsProject.
    bool loaded = false;
    QWidget *w = parentWidget();
    while ( w )
    {
      if ( QMetaObject::invokeMethod( w, "loadRasterLayer", Q_ARG( QString, path ) ) ||
           QMetaObject::invokeMethod( w, "loadDataLayer", Q_ARG( QString, path ) ) )
      {
        loaded = true;
        break;
      }
      w = w->parentWidget();
    }
    if ( !loaded )
    {
      auto *layer = new QgsRasterLayer( path, QFileInfo( path ).baseName(),
                                        QStringLiteral( "gdal" ) );
      if ( !layer->isValid() )
      {
        delete layer;
        if ( statusBar() )
          statusBar()->showMessage( tr( "无法加载结果: %1" ).arg( path ), 5000 );
        return;
      }
      QgsProject::instance()->addMapLayer( layer );
    }
  }

  if ( statusBar() )
    statusBar()->showMessage(
      tr( "已请求加载到主工程: %1" ).arg( QFileInfo( path ).fileName() ), 5000 );
}

void QgsGeorefShellWindow::emitStructuredLog( const QgsImageWarper::WarpResult &r )
{
  QJsonObject o;
  o.insert( QStringLiteral( "event" ), QStringLiteral( "warp_finished" ) );
  o.insert( QStringLiteral( "shell" ), shellId() );
  if ( mParamsPanel )
  {
    const QMetaEnum me = QMetaEnum::fromType<QgsGcpTransformerInterface::TransformMethod>();
    const char *key = me.valueToKey( static_cast<int>( mParamsPanel->transformMethod() ) );
    o.insert( QStringLiteral( "method" ), QString::fromUtf8( key ? key : "" ) );
    o.insert( QStringLiteral( "output" ), mParamsPanel->outputPath() );
  }
  o.insert( QStringLiteral( "output_bytes" ), static_cast<double>( r.outputBytes ) );
  o.insert( QStringLiteral( "duration_ms" ), r.durationMs );
  o.insert( QStringLiteral( "rms_px" ), mGeorefSession.lastFit().rms );
  o.insert( QStringLiteral( "status" ),
            r.status == QgsImageWarper::WarpStatus::Ok ? QStringLiteral( "ok" )
                                                       : QStringLiteral( "failed" ) );
  if ( r.status != QgsImageWarper::WarpStatus::Ok )
    o.insert( QStringLiteral( "error_msg" ), r.errorMessage );
  QgsMessageLog::logMessage(
    QString::fromUtf8( QJsonDocument( o ).toJson( QJsonDocument::Compact ) ),
    QStringLiteral( "Georeferencer" ), Qgis::Info );
}

void QgsGeorefShellWindow::setWarpInProgressForTest( bool on )
{
  mWarpInProgress = on;
  if ( mGcpTable )
    mGcpTable->setEnabled( !on );
  if ( on )
  {
    if ( mApplyAction )
      mApplyAction->setEnabled( false );
  }
  else
  {
    updateApplyEnabled(); // restores Apply when fit + output path allow
  }
}

bool QgsGeorefShellWindow::isDirtyForTest() const { return mGeorefSession.isDirty(); }
void QgsGeorefShellWindow::markDirtyForTest() { mGeorefSession.markDirty(); }

void QgsGeorefShellWindow::setSourceRasterPath( const QString &p )
{
  mSourceRasterPath = p;
  mGeorefSession.setSourceRasterPath( p );
}

int QgsGeorefShellWindow::gcpCountForTest() const
{
  return mGeorefSession.gcps().size();
}

RsGeoreferencingSession::WorkflowSnapshot QgsGeorefShellWindow::captureWorkflowSnapshot() const
{
  RsGeoreferencingSession::WorkflowSnapshot s;
  if ( mParamsPanel )
  {
    s.transformMethod = static_cast<int>( mParamsPanel->transformMethod() );
    s.resamplingMethod = static_cast<int>( mParamsPanel->resamplingMethod() );
    s.lastOutputPath = mParamsPanel->outputPath();
    s.lastDemPath = mParamsPanel->demPath();
    s.lastDestCrsAuthId = mParamsPanel->destCrs().authid();
    s.demZOffset = mParamsPanel->demZOffset();
  }
  s.lastSourcePath = mSourceRasterPath;
  s.lastPointsPath = mGeorefSession.lastPointsPath();
  captureShellSpecific( s );
  return s;
}

void QgsGeorefShellWindow::applyWorkflowSnapshot( const RsGeoreferencingSession::WorkflowSnapshot &s )
{
  if ( mParamsPanel )
  {
    const QSignalBlocker panelBlocker( mParamsPanel );
    mParamsPanel->setTransformMethod(
      static_cast<QgsGcpTransformerInterface::TransformMethod>( s.transformMethod ) );
    mParamsPanel->setResamplingMethod(
      static_cast<QgsImageWarper::ResamplingMethod>( s.resamplingMethod ) );
    if ( !s.lastOutputPath.isEmpty() )
      mParamsPanel->setOutputPath( s.lastOutputPath );
    if ( !s.lastDemPath.isEmpty() )
      mParamsPanel->setDemPath( s.lastDemPath );
    if ( !s.lastDestCrsAuthId.isEmpty() )
    {
      const QgsCoordinateReferenceSystem crs( s.lastDestCrsAuthId );
      if ( crs.isValid() )
        mParamsPanel->setDestCrs( crs );
    }
    mParamsPanel->setDemZOffset( s.demZOffset );
  }
  if ( !s.lastSourcePath.isEmpty() )
    setSourceRasterPath( s.lastSourcePath );
  applyShellSpecific( s );
  updateSourceLayerCaption();
  updateGcpTableRasterPaths();
  refreshFit();
}

void QgsGeorefShellWindow::clearPendingGcpPick()
{
  mHasPendingSource = false;
  mPendingSource = QgsPointXY();
  delete mPendingDataPoint;
  mPendingDataPoint = nullptr;
  delete mPendingGcp;
  mPendingGcp = nullptr;
}

void QgsGeorefShellWindow::beginPendingSourcePick( const QgsPointXY &sourceMap )
{
  clearPendingGcpPick();
  mPendingSource = sourceMap;
  mHasPendingSource = true;
  mPendingGcp = new QgsGcpPoint( sourceMap, QgsPointXY(), QgsCoordinateReferenceSystem(), true );
  mPendingDataPoint = new QgsGeorefDataPoint( mSrcCanvas, mDstCanvas, mPendingGcp );
  mPendingDataPoint->setParent( this );
  // Keep both tools armed after the click (canvas may steal focus / tool).
  rearmAddPointTools();
  if ( statusBar() )
    statusBar()->showMessage(
      tr( "已选源点 (%1, %2) — 请在参考/地图画布上点击同名位置（右键取消）" )
        .arg( sourceMap.x(), 0, 'f', 3 )
        .arg( sourceMap.y(), 0, 'f', 3 ),
      0 );
}

void QgsGeorefShellWindow::commitGcpPair( const QgsPointXY &sourceMap, const QgsPointXY &destMap )
{
  // Snapshot before clear — never re-read mPendingSource after clearPending.
  const QgsPointXY src = sourceMap;
  const QgsPointXY dst = destMap;

  clearPendingGcpPick();

  // Guard against the (0,0)/(0,0) bug that left SRC column all zeros.
  if ( qgsDoubleNear( src.x(), 0.0 ) && qgsDoubleNear( src.y(), 0.0 )
       && !( qgsDoubleNear( dst.x(), 0.0 ) && qgsDoubleNear( dst.y(), 0.0 ) ) )
  {
    if ( statusBar() )
      statusBar()->showMessage(
        tr( "源点坐标无效 (0,0)。请重新在源画布上取点，再点目标位置。" ), 6000 );
    rearmAddPointTools();
    return;
  }

  // I2I map-map sanity: same-name features in one projected CRS should be close.
  // Huge separation usually means Sync zoom or mismatched CRS on the two canvases.
  QString warn;
  const double srcMag = std::hypot( src.x(), src.y() );
  const double dstMag = std::hypot( dst.x(), dst.y() );
  if ( srcMag > 1000.0 && dstMag > 1000.0 )
  {
    const double sep = std::hypot( src.x() - dst.x(), src.y() - dst.y() );
    if ( sep > 50000.0 )
    {
      warn = tr( " ⚠ 源/参相距约 %1 km，请关闭 Sync zoom 并确认两侧 CRS 一致后重采。" )
               .arg( sep / 1000.0, 0, 'f', 1 );
    }
  }

  // Session GCPs are QgsGcpPoint values; capture the destination CRS at add
  // time so .points saves round-trip it (ADR 0056).
  const QgsCoordinateReferenceSystem destCrs =
    mParamsPanel ? mParamsPanel->destCrs() : QgsCoordinateReferenceSystem();
  mGeorefSession.addGcp( QgsGcpPoint( src, dst, destCrs, true ) );
  rearmAddPointTools();
  if ( statusBar() )
    statusBar()->showMessage(
      tr( "已添加 GCP #%1：源 (%2, %3) → 目标 (%4, %5)%6" )
        .arg( mGeorefSession.gcps().size() )
        .arg( src.x(), 0, 'f', 2 )
        .arg( src.y(), 0, 'f', 2 )
        .arg( dst.x(), 0, 'f', 2 )
        .arg( dst.y(), 0, 'f', 2 )
        .arg( warn ),
      warn.isEmpty() ? 5000 : 10000 );
}

void QgsGeorefShellWindow::onSourcePointPicked( const QgsPointXY &sourceMap )
{
  // Normalize into the source layer CRS so stored map coords match the image.
  const QgsPointXY layerMap = mapPickToLayerCrs( mSrcCanvas, mSrcRaster, sourceMap );
  if ( usesMapCoordsDialogForGcp() )
  {
    // QGIS-style I2M: dialog for typed map X/Y or pick from main window map.
    showCoordDialog( layerMap );
    rearmAddPointTools();
    return;
  }
  beginPendingSourcePick( layerMap );
}

void QgsGeorefShellWindow::onDestPointPicked( const QgsPointXY &destMap )
{
  if ( !mHasPendingSource )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "请先在源影像画布上点击源点" ), 4000 );
    rearmAddPointTools();
    return;
  }
  // Normalize REF/Map pick into the destination raster CRS when available.
  const QgsPointXY layerMap = mapPickToLayerCrs( mDstCanvas, mDstRaster, destMap );
  const QgsPointXY src = mPendingSource;
  commitGcpPair( src, layerMap );
}

QgsMapCanvas *QgsGeorefShellWindow::mainApplicationMapCanvas() const
{
  if ( mIface )
  {
    if ( QgsMapCanvas *c = mIface->mapCanvas() )
      return c;
  }
  // Walk parents for the main window canvas (iface is often null in this app).
  for ( QWidget *w = parentWidget(); w; w = w->parentWidget() )
  {
    const auto canvases = w->findChildren<QgsMapCanvas *>();
    for ( QgsMapCanvas *c : canvases )
    {
      if ( c && c != mSrcCanvas && c != mDstCanvas )
        return c;
    }
  }
  return nullptr;
}

void QgsGeorefShellWindow::showCoordDialog( const QgsPointXY &sourcePixel )
{
  // QGIS-style destination entry: type X/Y or pick from the main map canvas.
  QgsMapCanvas *mainMap = mainApplicationMapCanvas();

  if ( !mainMap )
  {
    // Fallback: seed a row so the user can fill dest X/Y in the GCP table.
    if ( statusBar() )
      statusBar()->showMessage(
        tr( "无法连接主地图画布：请在 GCP 表中直接填写目标 X/Y，或先打开主窗口。" ), 6000 );
    const QgsCoordinateReferenceSystem destCrs =
      mParamsPanel ? mParamsPanel->destCrs() : QgsCoordinateReferenceSystem();
    mGeorefSession.addGcp( QgsGcpPoint( sourcePixel, QgsPointXY(), destCrs, true ) );
    return;
  }

  auto *tempGcp = new QgsGcpPoint( sourcePixel, QgsPointXY(), QgsCoordinateReferenceSystem(), true );
  auto *tempDataPoint = new QgsGeorefDataPoint( mSrcCanvas, nullptr, tempGcp );

  QgsCoordinateReferenceSystem rasterCrs = mParamsPanel
                                             ? mParamsPanel->destCrs()
                                             : mainMap->mapSettings().destinationCrs();
  if ( !rasterCrs.isValid() )
    rasterCrs = mainMap->mapSettings().destinationCrs();

  auto *dlg = new QgsMapCoordsDialog( mainMap, tempDataPoint, rasterCrs, this );
  dlg->setAttribute( Qt::WA_DeleteOnClose );
  tempDataPoint->setParent( dlg );
  dlg->updateSourceCoordinates( sourcePixel );

  connect( dlg, &QgsMapCoordsDialog::pointAdded, this,
           [this]( const QgsPointXY &srcCoord, const QgsPointXY &dstCoord,
                   const QgsCoordinateReferenceSystem &destCrs ) {
             mGeorefSession.addGcp( QgsGcpPoint( srcCoord, dstCoord, destCrs, true ) );
             if ( statusBar() )
               statusBar()->showMessage( tr( "已添加 GCP（源像点 + 地图坐标）" ), 4000 );
             rearmAddPointTools();
           } );
  connect( dlg, &QObject::destroyed, this, [tempGcp]() { delete tempGcp; } );
  dlg->show();
  dlg->raise();
  dlg->activateWindow();
}

void QgsGeorefShellWindow::openSourceRaster()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Open source raster" ), QString(),
    tr( "Raster (*.tif *.tiff *.img *.jp2);;All files (*)" ) );
  if ( path.isEmpty() )
    return;
  loadSourceRaster( path );
}

void QgsGeorefShellWindow::openSourceFromProjectLayer()
{
  QgsRasterLayer *picked = pickProjectRasterLayer(
    tr( "从主工程选择源影像 (Warp)" ) );
  if ( !picked )
    return;
  loadSourceRaster( picked->source(), picked->name() );
}

bool QgsGeorefShellWindow::loadSourceRaster( const QString &path, const QString &displayName )
{
  if ( path.isEmpty() )
    return false;

  const QString name = displayName.isEmpty()
                         ? QFileInfo( path ).completeBaseName()
                         : displayName;
  auto *layer = new QgsRasterLayer( path, name, QStringLiteral( "gdal" ) );
  if ( !layer->isValid() )
  {
    // Retry with default provider key empty for non-file sources (e.g. memory).
    delete layer;
    layer = new QgsRasterLayer( path, name );
  }
  if ( !layer->isValid() )
  {
    delete layer;
    if ( statusBar() )
      statusBar()->showMessage( tr( "无法打开源影像: %1" ).arg( path ), 5000 );
    return false;
  }

  setSourceRasterPath( path );
  if ( mSrcSession )
  {
    if ( mSrcRaster )
    {
      mSrcSession->removeLayer( mSrcRaster );
      delete mSrcRaster;
      mSrcRaster = nullptr;
    }
    mSrcSession->addLayer( layer, true );
  }
  else if ( mLayerStore )
    mLayerStore->addMapLayer( layer );
  else
    layer->setParent( this );
  mSrcRaster = layer;
  if ( mSrcCanvas )
  {
    // Keep SRC canvas CRS aligned with the source layer so map picks are in
    // layer/map units (not an unrelated project CRS). Critical for dual pick.
    if ( layer->crs().isValid() )
      mSrcCanvas->setDestinationCrs( layer->crs() );
    if ( mSrcSession )
      mSrcSession->zoomToLayer( layer );
    else
    {
      mSrcCanvas->setLayers( { layer } );
      mSrcCanvas->setExtent( layer->extent() );
      mSrcCanvas->refresh();
    }
  }
  updateSourceLayerCaption();
  updateGcpTableRasterPaths();
  updateToolAvailability();
  refreshFit();
  mGeorefSession.saveWorkflow( captureWorkflowSnapshot() );
  if ( statusBar() )
    statusBar()->showMessage( tr( "已加载源影像 (Warp): %1" ).arg( layer->name() ), 4000 );
  return true;
}

bool QgsGeorefShellWindow::hasSourceReady() const
{
  return mSrcRaster && mSrcRaster->isValid();
}

bool QgsGeorefShellWindow::hasDestReady() const
{
  // I2M: map canvas is always usable (even empty); I2I overrides for REF.
  return true;
}

void QgsGeorefShellWindow::updateToolAvailability()
{
  const bool srcOk = hasSourceReady();
  const bool destOk = hasDestReady();
  const bool gcpOk = srcOk && destOk;

  if ( mAddPointAction )
  {
    mAddPointAction->setEnabled( gcpOk );
    if ( !gcpOk && mAddPointAction->isChecked() )
      mAddPointAction->setChecked( false );
  }
  if ( mMovePointAction )
  {
    mMovePointAction->setEnabled( gcpOk );
    if ( !gcpOk && mMovePointAction->isChecked() )
      mMovePointAction->setChecked( false );
  }
  if ( mDeletePointAction )
  {
    mDeletePointAction->setEnabled( gcpOk );
    if ( !gcpOk && mDeletePointAction->isChecked() )
      mDeletePointAction->setChecked( false );
  }
  if ( mLoadGcpAction )
    mLoadGcpAction->setEnabled( srcOk );
  if ( mSaveGcpAction )
    mSaveGcpAction->setEnabled( srcOk && !mGeorefSession.gcps().isEmpty() );
  if ( mGcpTable )
    mGcpTable->setEnabled( gcpOk || !mGeorefSession.gcps().isEmpty() );

  if ( !gcpOk )
    clearPendingGcpPick();

  // Apply remains gated by the session fit (GCP count + output path + tools).
  refreshFit();
  if ( mApplyAction && !gcpOk )
    mApplyAction->setEnabled( false );
}

// Keep Export .gcp enabled state in sync when GCP list changes.
// (Hooked via existing gcpsChanged → onPointsChanged; also refresh save.)

QgsRasterLayer *QgsGeorefShellWindow::pickProjectRasterLayer( const QString &dialogTitle )
{
  QList<QgsRasterLayer *> rasters;
  const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
  for ( auto it = layers.constBegin(); it != layers.constEnd(); ++it )
  {
    if ( auto *rl = qobject_cast<QgsRasterLayer *>( it.value() ) )
    {
      if ( rl->isValid() )
        rasters.append( rl );
    }
  }

  if ( rasters.isEmpty() )
  {
    QMessageBox::information(
      this, dialogTitle,
      tr( "主工程中没有可用的栅格图层。\n请先在主窗口加载影像，或改用「从文件打开」。" ) );
    return nullptr;
  }

  QStringList labels;
  labels.reserve( rasters.size() );
  for ( QgsRasterLayer *rl : rasters )
  {
    const QString src = rl->source();
    const QString shortSrc = src.size() > 60
                               ? ( QStringLiteral( "…" ) + src.right( 59 ) )
                               : src;
    labels << QStringLiteral( "%1  [%2]" ).arg( rl->name() ).arg( shortSrc );
  }

  bool ok = false;
  const QString chosen = QInputDialog::getItem(
    this, dialogTitle,
    tr( "选择栅格图层:" ),
    labels, 0, false, &ok );
  if ( !ok || chosen.isEmpty() )
    return nullptr;

  const int idx = labels.indexOf( chosen );
  if ( idx < 0 || idx >= rasters.size() )
    return nullptr;
  return rasters.at( idx );
}

void QgsGeorefShellWindow::closeEvent( QCloseEvent *e )
{
  const bool busy = mWarpInProgress || ( mTaskList && mTaskList->hasRunning() );
  if ( busy )
  {
    const auto ans = QMessageBox::question(
      this, tr( "几何校正" ), tr( "校正任务仍在运行，仍要关闭？" ),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
    if ( ans != QMessageBox::Yes ) { e->ignore(); return; }
  }
  if ( mGeorefSession.isDirty() )
  {
    const auto ans = QMessageBox::question(
      this, tr( "未保存的控制点" ),
      tr( "GCP 列表有未保存的更改。是否保存？" ),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
    if ( ans == QMessageBox::Cancel ) { e->ignore(); return; }
    if ( ans == QMessageBox::Save )
    {
      QString path = mGeorefSession.lastPointsPath();
      if ( path.isEmpty() )
      {
        path = QFileDialog::getSaveFileName(
          this, tr( "Save GCP points" ), QString(),
          tr( "GCP Points (*.points *.gcp);;All files (*)" ) );
        if ( path.isEmpty() ) { e->ignore(); return; }
        if ( QFileInfo( path ).suffix().isEmpty() )
          path += QStringLiteral( ".points" );
      }
      if ( !rsSaveGcpPointsFile( path, mGeorefSession.gcps() ) )
      {
        QMessageBox::warning( this, tr( "Save GCPs" ), tr( "保存失败，窗口未关闭。" ) );
        e->ignore();
        return;
      }
      mGeorefSession.setLastPointsPath( path );
      mGeorefSession.clearDirty();
    }
  }
  mGeorefSession.saveWorkflow( captureWorkflowSnapshot() );
  mGeorefSession.saveWindow( this );
  e->accept();
}

void QgsGeorefShellWindow::loadPoints()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Load GCP points" ), mGeorefSession.lastPointsPath(),
    tr( "GCP Points (*.points *.gcp);;All files (*)" ) );
  if ( path.isEmpty() ) return;
  const QgsCoordinateReferenceSystem destCrs =
    mParamsPanel ? mParamsPanel->destCrs() : QgsCoordinateReferenceSystem();
  QVector<QgsGcpPoint> loaded;
  if ( !rsLoadGcpPointsFile( path, destCrs, loaded ) )
  {
    QMessageBox::warning( this, tr( "Load GCPs" ), tr( "Failed to load GCP points from %1" ).arg( path ) );
    return;
  }

  // Persistence codec only — the loaded rows become the session's GCP list.
  // Each point keeps its file-stored destination CRS (ADR 0056) with the
  // panel CRS as fallback for pre-CRS files.
  mSuppressDirtyFromList = true;
  mGeorefSession.setGcps( loaded );
  mSuppressDirtyFromList = false;
  mGeorefSession.refit();

  mGeorefSession.setLastPointsPath( path );
  mGeorefSession.clearDirty();
}

void QgsGeorefShellWindow::savePoints()
{
  const QString path = QFileDialog::getSaveFileName(
    this, tr( "Save GCP points" ), mGeorefSession.lastPointsPath(),
    tr( "GCP Points (*.points *.gcp);;All files (*)" ) );
  if ( path.isEmpty() ) return;
  QString finalPath = path;
  if ( QFileInfo( finalPath ).suffix().isEmpty() )
    finalPath += QStringLiteral( ".points" );
  // Session GCPs are already QgsGcpPoint values carrying their destination
  // CRS (ADR 0056); the codec serializes it back into the file.
  if ( !rsSaveGcpPointsFile( finalPath, mGeorefSession.gcps() ) )
    QMessageBox::warning( this, tr( "Save GCPs" ), tr( "Failed to save GCP points to %1" ).arg( finalPath ) );
  else
  {
    mGeorefSession.setLastPointsPath( finalPath );
    mGeorefSession.clearDirty();
  }
}

void QgsGeorefShellWindow::deleteGcpRows( const QList<int> &rows )
{
  if ( rows.isEmpty() ) return;
  QList<int> sortedRows = rows;
  std::sort( sortedRows.begin(), sortedRows.end(), std::greater<int>() );
  for ( int row : sortedRows )
    mGeorefSession.removeGcpAt( row );
}

QgsGeorefDataPoint *QgsGeorefShellWindow::findDataPoint( const QgsPointXY &p, QgsGcpPoint::PointType type )
{
  QgsGeorefDataPoint *nearest = nullptr;
  double bestDistance = -1.0;
  for ( QgsGeorefDataPoint *dp : mDataPoints )
  {
    if ( !dp ) continue;
    double distance = 0.0;
    if ( dp->contains( p, type, distance ) )
    {
      if ( bestDistance < 0.0 || distance < bestDistance )
      {
        bestDistance = distance;
        nearest = dp;
      }
    }
  }
  return nearest;
}

void QgsGeorefShellWindow::selectPoint( const QgsPointXY &p )
{
  const bool isSrc = ( sender() == mToolMoveSrc );
  const auto type = isSrc ? QgsGcpPoint::PointType::Source : QgsGcpPoint::PointType::Destination;
  QgsGeorefToolMovePoint *tool = isSrc ? mToolMoveSrc : mToolMoveDst;
  mMovingPoint = findDataPoint( p, type );
  if ( !mMovingPoint || !tool ) return;
  if ( mHoveredPoint ) { mHoveredPoint->setHovered( false ); mHoveredPoint = nullptr; }
  mMoveOrigin = ( type == QgsGcpPoint::PointType::Source )
                  ? mMovingPoint->sourcePoint() : mMovingPoint->destinationPoint();
  tool->setStartPoint( mMoveOrigin );
}

void QgsGeorefShellWindow::movePoint( const QgsPointXY &p )
{
  const bool isSrc = ( sender() == mToolMoveSrc );
  const auto type = isSrc ? QgsGcpPoint::PointType::Source : QgsGcpPoint::PointType::Destination;
  if ( mMovingPoint ) { mMovingPoint->moveTo( p, type ); return; }
  QgsGeorefDataPoint *point = findDataPoint( p, type );
  if ( point ) point->setHovered( true );
  if ( mHoveredPoint && point != mHoveredPoint ) mHoveredPoint->setHovered( false );
  mHoveredPoint = point;
}

void QgsGeorefShellWindow::releasePoint( const QgsPointXY &p )
{
  const bool isSrc = ( sender() == mToolMoveSrc );
  const auto type = isSrc ? QgsGcpPoint::PointType::Source : QgsGcpPoint::PointType::Destination;
  QgsGeorefToolMovePoint *tool = isSrc ? mToolMoveSrc : mToolMoveDst;
  if ( tool ) tool->setStartPoint( QgsPointXY() );
  if ( mMovingPoint )
  {
    // Push the dragged row's new coordinates back to the session (owner).
    const int row = mDataPoints.indexOf( mMovingPoint );
    if ( row >= 0 && row < mGcpViewPoints.size() )
    {
      if ( type == QgsGcpPoint::PointType::Source )
        mGeorefSession.setGcpSource( row, mGcpViewPoints.at( row )->sourcePoint() );
      else
        mGeorefSession.setGcpDestination( row, mGcpViewPoints.at( row )->destinationPoint() );
    }
    mMovingPoint = nullptr;
  }
  QgsGeorefDataPoint *point = findDataPoint( p, type );
  if ( point ) point->setHovered( true );
  if ( mHoveredPoint && point != mHoveredPoint ) mHoveredPoint->setHovered( false );
  mHoveredPoint = point;
}

void QgsGeorefShellWindow::cancelPoint( const QgsPointXY &p )
{
  const bool isSrc = ( sender() == mToolMoveSrc );
  const auto type = isSrc ? QgsGcpPoint::PointType::Source : QgsGcpPoint::PointType::Destination;
  QgsGeorefToolMovePoint *tool = isSrc ? mToolMoveSrc : mToolMoveDst;
  if ( mMovingPoint )
  {
    const QgsPointXY origin = ( tool && !tool->startPoint().isEmpty() ) ? tool->startPoint() : mMoveOrigin;
    if ( type == QgsGcpPoint::PointType::Source ) mMovingPoint->setSourcePoint( origin );
    else mMovingPoint->setDestinationPoint( origin );
  }
  if ( tool ) tool->setStartPoint( QgsPointXY() );
  mMovingPoint = nullptr;
  QgsGeorefDataPoint *point = findDataPoint( p, type );
  if ( point ) point->setHovered( true );
  if ( mHoveredPoint && point != mHoveredPoint ) mHoveredPoint->setHovered( false );
  mHoveredPoint = point;
}

void QgsGeorefShellWindow::hoverPoint( const QgsPointXY &p )
{
  const auto type = ( sender() == mToolDeleteSrc )
                      ? QgsGcpPoint::PointType::Source : QgsGcpPoint::PointType::Destination;
  QgsGeorefDataPoint *point = findDataPoint( p, type );
  if ( point ) point->setHovered( true );
  if ( mHoveredPoint && point != mHoveredPoint ) mHoveredPoint->setHovered( false );
  mHoveredPoint = point;
}

void QgsGeorefShellWindow::deletePointAt( const QgsPointXY &p )
{
  const auto type = ( sender() == mToolDeleteSrc )
                      ? QgsGcpPoint::PointType::Source : QgsGcpPoint::PointType::Destination;
  QgsGeorefDataPoint *dp = findDataPoint( p, type );
  if ( !dp ) return;
  if ( mHoveredPoint == dp ) { mHoveredPoint->setHovered( false ); mHoveredPoint = nullptr; }
  if ( mMovingPoint == dp ) mMovingPoint = nullptr;
  mGeorefSession.removeGcpAt( mDataPoints.indexOf( dp ) );
}
