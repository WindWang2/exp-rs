#include "qgsgeoref_shell_window.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaEnum>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStatusBar>
#include <QToolBar>
#include <QVector>
#include <QWhatsThis>
#include <QWidget>
#include <cmath>

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
#include "qgsrpcgcptransformer.h"
#include "qgsmapcanvas.h"
#include "qgsmapcoordsdialog.h"
#include "qgsmaplayerstore.h"
#include "qgsmaptool.h"
#include "qgsmessagelog.h"
#include "qgsproject.h"
#include "qgsrasterlayer.h"
#include "qgsrectangle.h"
#include "qgstaskmanager.h"
#include "rs_georef_task_list.h"
#include "rs_warp_task.h"

namespace
{
  int minimumGcpCountFor( QgsGcpTransformerInterface::TransformMethod m )
  {
    std::unique_ptr<QgsGcpTransformerInterface> t( QgsGcpTransformerInterface::create( m ) );
    return t ? t->minimumGcpCount() : 0;
  }

  double computeEnabledRms( QgsGCPList *gcps )
  {
    if ( !gcps )
      return 0.0;
    double totalSq = 0.0;
    int n = 0;
    for ( const QgsGcpPoint *p : std::as_const( *gcps ) )
    {
      if ( !p || !p->isEnabled() )
        continue;
      const QPointF r = p->residual();
      totalSq += r.x() * r.x() + r.y() * r.y();
      ++n;
    }
    return n > 0 ? std::sqrt( totalSq / n ) : 0.0;
  }
}

QgsGeorefShellWindow::QgsGeorefShellWindow( QgisInterface *iface, QWidget *parent )
  : QMainWindow( parent )
  , mIface( iface )
{
  mGcps = new QgsGCPList();
  mGcps->setParent( this );
  connect( mGcps, &QgsGCPList::changed, this, [this]() {
    if ( !mSuppressDirtyFromList )
      mSession.markDirty();
  } );
  mLayerStore = new QgsMapLayerStore( this );
}

QgsGeorefShellWindow::~QgsGeorefShellWindow()
{
  qDeleteAll( mDataPoints );
  mDataPoints.clear();
}

void QgsGeorefShellWindow::finishCommonSetup( RsGeorefParamsPanel::Profile profile,
                                              const QString &gcpDockObjectName,
                                              const QString &paramDockObjectName )
{
  mGcpDock = new QDockWidget( tr( "GCP 表" ), this );
  mGcpDock->setObjectName( gcpDockObjectName );
  mGcpDock->setAllowedAreas( Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea );
  tipWidget( mGcpDock, tr(
    "地面控制点列表：启用/禁用点、查看残差。点数与分布决定拟合质量。" ) );
  mGcpTable = new QgsGCPListWidget( mGcpDock );
  mGcpTable->setGCPList( mGcps );
  tipWidget( mGcpTable, tr(
    "控制点列表：源/目标坐标与残差。\n"
    "右键：定位、启用/禁用、编辑坐标、删除。\n"
    "Delete 删除选中行；双击 # 或残差列 → 两侧定位。" ) );
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
  tipWidget( mTaskDock, tr(
    "运行历史：点「运行」后任务出现在此，可查看进度、取消或加载结果。" ) );
  mTaskList = new RsGeorefTaskList( mTaskDock );
  mTaskList->setObjectName( QStringLiteral( "rsGeorefTaskList" ) );
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
  tipWidget( mParamDock, tr(
    "变换方法、重采样、目标 CRS 与输出路径。悬停各控件可查看详细说明。" ) );
  mParamsPanel = new RsGeorefParamsPanel( mParamDock );
  mParamsPanel->setProfile( profile );
  if ( profile == RsGeorefParamsPanel::Profile::ImageToImage )
    mParamsPanel->setRpcMode( false );
  mParamDock->setWidget( mParamsPanel );
  addDockWidget( Qt::RightDockWidgetArea, mParamDock );
  resizeDocks( { mParamDock }, { 340 }, Qt::Horizontal );

  connect( mGcps, &QgsGCPList::changed, this, &QgsGeorefShellWindow::recomputeFit );
  connect( mGcps, &QgsGCPList::changed, this, &QgsGeorefShellWindow::onPointsChanged );
  connect( mParamsPanel, &RsGeorefParamsPanel::transformMethodChanged,
           this, &QgsGeorefShellWindow::onTransformMethodChanged );
  connect( mParamsPanel, &RsGeorefParamsPanel::outputPathChanged, this,
           [this]( const QString & ) { recomputeFit(); } );
  connect( mParamsPanel, &RsGeorefParamsPanel::destCrsChanged,
           this, &QgsGeorefShellWindow::recomputeFit );
  connect( mParamsPanel, &RsGeorefParamsPanel::demZOffsetChanged,
           this, &QgsGeorefShellWindow::recomputeFit );

  createMapTools();
  wireMapToolActions();

  mParamsPanel->setActualGcpCount( 0 );
  onTransformMethodChanged();
  recomputeFit();

  mSession.restoreWindow( this );
  applyWorkflowSnapshot( mSession.restoreWorkflow() );
  mParamsPanel->setProfile( profile );
  if ( profile == RsGeorefParamsPanel::Profile::ImageToImage )
    mParamsPanel->setRpcMode( false );
  onTransformMethodChanged();
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

QMenu *QgsGeorefShellWindow::createFileMenu()
{
  auto *fileMenu = menuBar()->addMenu( tr( "&File" ) );
  auto *openSrc = fileMenu->addAction( tr( "Open source raster..." ),
                                       this, &QgsGeorefShellWindow::openSourceRaster );
  tipAction( openSrc, tr(
    "打开待校正源影像（SRC）。影像显示在源画布上，路径用于写出 warp。" ) );
  return fileMenu;
}

void QgsGeorefShellWindow::addStandardMenuBar()
{
  menuBar()->addMenu( tr( "&Edit" ) );
  menuBar()->addMenu( tr( "&View" ) );
  menuBar()->addMenu( tr( "&Settings" ) );
  auto *helpMenu = menuBar()->addMenu( tr( "&Help" ) );
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

void QgsGeorefShellWindow::addGcpEditActions( QToolBar *bar, const QString &objectNamePrefix )
{
  if ( !bar )
    return;

  const QIcon ic( QStringLiteral( ":/icons/r_ster_calc" ) );
  tipWidget( bar, tr( "配准工具栏：加点 / 移动 / 删除 GCP，导入导出控制点，运行校正。" ) );

  mAddPointAction = bar->addAction( ic, tr( "Add GCP" ) );
  mAddPointAction->setObjectName( objectNamePrefix + QStringLiteral( "AddPointAction" ) );
  mAddPointAction->setCheckable( true );
  tipAction( mAddPointAction, tr(
    "添加控制点（双画布点选）：\n"
    "1. 在左侧/上方源影像 (SRC) 点击源点\n"
    "2. 在右侧/下方参考影像或地图上点击同名目标点\n"
    "右键取消当前未完成的源点。均匀分布、覆盖边缘效果更好。" ) );

  mMovePointAction = bar->addAction( ic, tr( "Move GCP" ) );
  mMovePointAction->setObjectName( objectNamePrefix + QStringLiteral( "MovePointAction" ) );
  mMovePointAction->setCheckable( true );
  tipAction( mMovePointAction, tr(
    "移动控制点：在源或目标画布上拖动已有 GCP 标记，微调位置后残差会自动重算。" ) );

  mDeletePointAction = bar->addAction( ic, tr( "Delete GCP" ) );
  mDeletePointAction->setObjectName( objectNamePrefix + QStringLiteral( "DeletePointAction" ) );
  mDeletePointAction->setCheckable( true );
  tipAction( mDeletePointAction, tr(
    "删除控制点：在画布上点击 GCP 标记删除；也可在 GCP 表中选中行删除。" ) );

  auto *group = new QActionGroup( this );
  group->setExclusive( true );
  group->addAction( mAddPointAction );
  group->addAction( mMovePointAction );
  group->addAction( mDeletePointAction );

  auto *loadGcp = bar->addAction( ic, tr( "Load .gcp" ), this, &QgsGeorefShellWindow::loadPoints );
  tipAction( loadGcp, tr( "从 .points / .gcp 文件加载控制点列表。" ) );
  auto *saveGcp = bar->addAction( ic, tr( "Export .gcp" ), this, &QgsGeorefShellWindow::savePoints );
  tipAction( saveGcp, tr( "将当前控制点导出为 .points 文件，便于下次继续。" ) );
}

void QgsGeorefShellWindow::addApplyAction( QToolBar *bar, const QString &objectName )
{
  if ( !bar )
    return;

  auto *spacer = new QWidget( this );
  spacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
  bar->addWidget( spacer );

  mApplyAction = bar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ),
    tr( "运行" ), this, &QgsGeorefShellWindow::applyTransform );
  mApplyAction->setObjectName( objectName );
  tipAction( mApplyAction, tr(
    "运行几何校正：校验 GCP / 输出路径后，将任务加入「校正任务」列表并后台执行 warp。\n"
    "可多次运行形成多条任务；运行中可在任务列表取消。" ) );
  mApplyAction->setEnabled( false );
}

void QgsGeorefShellWindow::createMapTools()
{
  if ( !mSrcCanvas || !mDstCanvas )
    return;

  // Dual-canvas GCP pick: SRC → source, REF/Map → destination (no form).
  // IMPORTANT: do NOT setAction() on these tools. Two tools sharing one QAction
  // causes deactivate() on one canvas to uncheck the action and clear pending
  // source (source coords became 0,0). Toolbar actions are wired only in
  // wireMapToolActions().
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

  mToolMoveSrc = new QgsGeorefToolMovePoint( mSrcCanvas );
  mToolMoveSrc->setParent( this );
  mToolMoveDst = new QgsGeorefToolMovePoint( mDstCanvas );
  mToolMoveDst->setParent( this );

  connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointBeginMove, this, &QgsGeorefShellWindow::selectPoint );
  connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointMoving, this, &QgsGeorefShellWindow::movePoint );
  connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointEndMove, this, &QgsGeorefShellWindow::releasePoint );
  connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointCancelMove, this, &QgsGeorefShellWindow::cancelPoint );
  connect( mToolMoveDst, &QgsGeorefToolMovePoint::pointBeginMove, this, &QgsGeorefShellWindow::selectPoint );
  connect( mToolMoveDst, &QgsGeorefToolMovePoint::pointMoving, this, &QgsGeorefShellWindow::movePoint );
  connect( mToolMoveDst, &QgsGeorefToolMovePoint::pointEndMove, this, &QgsGeorefShellWindow::releasePoint );
  connect( mToolMoveDst, &QgsGeorefToolMovePoint::pointCancelMove, this, &QgsGeorefShellWindow::cancelPoint );

  const auto clearMoveHover = [this]() {
    mMovingPoint = nullptr;
    if ( mHoveredPoint )
    {
      mHoveredPoint->setHovered( false );
      mHoveredPoint = nullptr;
    }
  };
  connect( mToolMoveSrc, &QgsMapTool::deactivated, this, clearMoveHover );
  connect( mToolMoveDst, &QgsMapTool::deactivated, this, clearMoveHover );

  mToolDeleteSrc = new QgsGeorefToolDeletePoint( mSrcCanvas );
  mToolDeleteSrc->setParent( this );
  mToolDeleteDst = new QgsGeorefToolDeletePoint( mDstCanvas );
  mToolDeleteDst->setParent( this );

  connect( mToolDeleteSrc, &QgsGeorefToolDeletePoint::deletePoint, this, &QgsGeorefShellWindow::deletePointAt );
  connect( mToolDeleteSrc, &QgsGeorefToolDeletePoint::hoverPoint, this, &QgsGeorefShellWindow::hoverPoint );
  connect( mToolDeleteDst, &QgsGeorefToolDeletePoint::deletePoint, this, &QgsGeorefShellWindow::deletePointAt );
  connect( mToolDeleteDst, &QgsGeorefToolDeletePoint::hoverPoint, this, &QgsGeorefShellWindow::hoverPoint );

  const auto clearDeleteHover = [this]() {
    if ( mHoveredPoint )
    {
      mHoveredPoint->setHovered( false );
      mHoveredPoint = nullptr;
    }
  };
  connect( mToolDeleteSrc, &QgsMapTool::deactivated, this, clearDeleteHover );
  connect( mToolDeleteDst, &QgsMapTool::deactivated, this, clearDeleteHover );
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
        statusBar()->showMessage(
          tr( "添加 GCP：先在左侧/上方源画布点击源点，再在参考/地图上点击同名位置（右键取消）" ), 8000 );
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
}

void QgsGeorefShellWindow::onTransformMethodChanged()
{
  onTransformMethodChangedExtra();
  recomputeFit();
}

void QgsGeorefShellWindow::onPointsChanged()
{
  if ( !mGcps )
    return;

  QSet<QgsGcpPoint *> live;
  int idCounter = 1; // 1-based labels on canvas badges
  for ( QgsGcpPoint *p : *mGcps )
  {
    if ( !p )
    {
      ++idCounter;
      continue;
    }
    live.insert( p );
    QgsGeorefDataPoint *dp = mDataPoints.value( p, nullptr );
    if ( !dp )
    {
      dp = new QgsGeorefDataPoint( mSrcCanvas, mDstCanvas, p );
      dp->setParent( this );
      mDataPoints.insert( p, dp );
    }
    dp->setId( idCounter );
    dp->updateMarkers();
    ++idCounter;
  }

  QList<QgsGcpPoint *> dead;
  for ( auto it = mDataPoints.cbegin(); it != mDataPoints.cend(); ++it )
  {
    if ( !live.contains( it.key() ) )
      dead.append( it.key() );
  }
  for ( QgsGcpPoint *p : dead )
  {
    QgsGeorefDataPoint *dp = mDataPoints.take( p );
    if ( mMovingPoint == dp )
      mMovingPoint = nullptr;
    if ( mHoveredPoint == dp )
      mHoveredPoint = nullptr;
    delete dp;
  }

  if ( mSrcCanvas )
    mSrcCanvas->refresh();
  if ( mDstCanvas )
    mDstCanvas->refresh();
}

void QgsGeorefShellWindow::syncAllMarkers()
{
  for ( auto it = mDataPoints.begin(); it != mDataPoints.end(); ++it )
  {
    if ( it.value() )
      it.value()->updateMarkers();
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
  if ( !mGcps )
    return;
  for ( int r = 0; r < mGcps->size(); ++r )
  {
    QgsGcpPoint *p = mGcps->at( r );
    QgsGeorefDataPoint *dp = p ? mDataPoints.value( p, nullptr ) : nullptr;
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
  if ( !mGcps || row < 0 || row >= mGcps->size() )
    return;
  QgsGcpPoint *p = mGcps->at( row );
  if ( !p )
    return;
  setSelectedGcpRow( row );
  panCanvasToPoint( mSrcCanvas, p->sourcePoint() );
  if ( statusBar() )
    statusBar()->showMessage( tr( "已定位到源点 #%1" ).arg( row + 1 ), 3000 );
}

void QgsGeorefShellWindow::zoomToGcpDest( int row )
{
  if ( !mGcps || row < 0 || row >= mGcps->size() )
    return;
  QgsGcpPoint *p = mGcps->at( row );
  if ( !p )
    return;
  setSelectedGcpRow( row );
  QgsGeorefDataPoint *dp = mDataPoints.value( p, nullptr );
  const QgsPointXY dest = dp ? dp->destinationDisplayPoint() : p->destinationPoint();
  panCanvasToPoint( mDstCanvas, dest );
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

void QgsGeorefShellWindow::recomputeFit()
{
  if ( !mParamsPanel || !mGcps )
    return;

  const auto method = mParamsPanel->transformMethod();
  const int minN = minimumGcpCountFor( method );
  mParamsPanel->setMinimumGcpCount( minN );

  const QgsCoordinateReferenceSystem dstCrs = mParamsPanel->destCrs();
  const QgsCoordinateTransformContext transformContext;
  QVector<QgsPointXY> src;
  QVector<QgsPointXY> dst;
  mGcps->createGCPVectors( src, dst, dstCrs, transformContext );
  const int enabledCount = src.size();
  mParamsPanel->setActualGcpCount( enabledCount );

  constexpr bool kInvertYAxis = true;
  bool fitOk = false;

  if ( method == QgsGcpTransformerInterface::TransformMethod::RpcPhysical && enabledCount >= 3 )
  {
    double rmsBefore = -1.0;
    double rmsAfter = -1.0;
    {
      auto beforeXf = std::make_unique<QgsGeorefTransform>( method );
      if ( auto *rpc = dynamic_cast<QgsRpcGcpTransformer *>(
             beforeXf ? beforeXf->gcpTransformer() : nullptr ) )
      {
        rpc->setSourceRasterPath( mSourceRasterPath );
        rpc->setRpcOptions( mParamsPanel->demPath(), mParamsPanel->demZOffset(), false );
      }
      try
      {
        if ( beforeXf && beforeXf->updateParametersFromGcps( src, dst, kInvertYAxis ) )
        {
          mGcps->updateResiduals( beforeXf.get(), dstCrs, transformContext );
          rmsBefore = computeEnabledRms( mGcps );
        }
      }
      catch ( ... ) {}
    }
    mTransform.reset( new QgsGeorefTransform( method ) );
    if ( auto *rpc = dynamic_cast<QgsRpcGcpTransformer *>(
           mTransform ? mTransform->gcpTransformer() : nullptr ) )
    {
      rpc->setSourceRasterPath( mSourceRasterPath );
      rpc->setRpcOptions( mParamsPanel->demPath(), mParamsPanel->demZOffset(), true );
    }
    try
    {
      fitOk = mTransform->updateParametersFromGcps( src, dst, kInvertYAxis );
    }
    catch ( ... )
    {
      fitOk = false;
    }
    if ( fitOk )
    {
      mGcps->updateResiduals( mTransform.get(), dstCrs, transformContext );
      rmsAfter = computeEnabledRms( mGcps );
    }
    if ( rmsBefore >= 0.0 && rmsAfter >= 0.0 )
      mParamsPanel->setRefinementRms( rmsBefore, rmsAfter );
    else
      mParamsPanel->clearRefinementRms();
  }
  else
  {
    mParamsPanel->clearRefinementRms();
    mTransform.reset( new QgsGeorefTransform( method ) );
    if ( method == QgsGcpTransformerInterface::TransformMethod::RpcPhysical )
    {
      if ( auto *rpc = dynamic_cast<QgsRpcGcpTransformer *>(
             mTransform ? mTransform->gcpTransformer() : nullptr ) )
      {
        rpc->setSourceRasterPath( mSourceRasterPath );
        rpc->setRpcOptions( mParamsPanel->demPath(), mParamsPanel->demZOffset(), false );
      }
    }
    if ( enabledCount >= minN && minN > 0 )
    {
      try { fitOk = mTransform->updateParametersFromGcps( src, dst, kInvertYAxis ); }
      catch ( ... ) { fitOk = false; }
    }
    else if ( method == QgsGcpTransformerInterface::TransformMethod::RpcPhysical && mTransform )
    {
      try { fitOk = mTransform->updateParametersFromGcps( src, dst, kInvertYAxis ); }
      catch ( ... ) { fitOk = false; }
    }
  }

  double totalSq = 0.0, xSq = 0.0, ySq = 0.0, maxMag = 0.0;
  int maxRow = -1;
  QVector<QPointF> scatter;
  scatter.reserve( enabledCount );

  if ( fitOk )
  {
    mGcps->updateResiduals( mTransform.get(), dstCrs, transformContext );
    int rowId = 0;
    int included = 0;
    for ( const QgsGcpPoint *p : std::as_const( *mGcps ) )
    {
      if ( !p ) { ++rowId; continue; }
      if ( !p->isEnabled() ) { ++rowId; continue; }
      const QPointF r = p->residual();
      scatter.push_back( r );
      const double mag = std::hypot( r.x(), r.y() );
      totalSq += mag * mag;
      xSq += r.x() * r.x();
      ySq += r.y() * r.y();
      if ( mag > maxMag ) { maxMag = mag; maxRow = rowId; }
      ++included;
      ++rowId;
    }
    if ( included > 0 )
    {
      mLastRms = std::sqrt( totalSq / included );
      mParamsPanel->setRmsValues( mGcps->size(), enabledCount, mLastRms,
                                  std::sqrt( xSq / included ), std::sqrt( ySq / included ),
                                  maxMag, maxRow );
    }
    else
    {
      mLastRms = 0.0;
      mParamsPanel->setRmsValues( mGcps->size(), enabledCount, 0, 0, 0, 0, -1 );
    }
  }
  else
  {
    mGcps->clearResiduals();
    mLastRms = 0.0;
    mParamsPanel->setRmsValues( mGcps->size(), enabledCount, 0, 0, 0, 0, -1 );
  }

  // Always push residual / position to canvas badges after fit.
  syncAllMarkers();
  if ( mGcpTable && mGcpTable->gcpModel() )
    mGcpTable->gcpModel()->refreshAll();

  mParamsPanel->setResidualScatter( scatter );
  if ( mRmsLabel )
  {
    if ( fitOk && mLastRms > 0.0 )
      mRmsLabel->setText( tr( "RMS: %1 px" ).arg( QString::number( mLastRms, 'f', 3 ) ) );
    else
      mRmsLabel->setText( tr( "RMS: —" ) );
  }
  if ( mApplyAction )
  {
    mApplyAction->setEnabled( fitOk && enabledCount >= minN
                              && !mParamsPanel->outputPath().isEmpty()
                              && !mWarpInProgress );
  }
}

void QgsGeorefShellWindow::applyTransform()
{
  if ( !mParamsPanel || !mGcps || !mTaskList )
    return;

  const auto method = mParamsPanel->transformMethod();
  const int minN = minimumGcpCountFor( method );
  int enabled = 0;
  for ( const QgsGcpPoint *p : std::as_const( *mGcps ) )
    if ( p && p->isEnabled() ) ++enabled;

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
  if ( !mTransform )
  {
    statusBar()->showMessage( tr( "变换尚未完成拟合" ), 3000 );
    return;
  }

  // Snapshot parameters at enqueue time so later panel edits don't affect this job.
  const QString sourcePath = mSourceRasterPath;
  const QString outputPath = mParamsPanel->outputPath();
  const double rmsAtStart = mLastRms;
  const auto resampling = mParamsPanel->resamplingMethod();
  const auto destCrs = mParamsPanel->destCrs();
  const double pixelSize = mParamsPanel->outputPixelSize();

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

  // Raise task dock so the user sees the new row.
  if ( mTaskDock )
  {
    mTaskDock->show();
    mTaskDock->raise();
  }

  auto *task = new RsWarpTask( sourcePath, outputPath,
                               mTransform.get(), resampling,
                               destCrs, pixelSize );
  mActiveWarpTasks.insert( taskId, task );

  connect( task, &QgsTask::progressChanged, this,
           [this, taskId]( double p ) {
             if ( mTaskList )
               mTaskList->setProgress( taskId, p );
           } );

  auto finalize = [this, task, taskId, outputPath]() {
    mActiveWarpTasks.remove( taskId );
    emitStructuredLog( task->result() );
    const auto &r = task->result();
    if ( r.status == QgsImageWarper::WarpStatus::Ok )
    {
      if ( mTaskList )
        mTaskList->finishSuccess( taskId, r.durationMs, r.outputBytes );
      statusBar()->showMessage(
        tr( "任务 #%1 完成: %2 (%3 字节, %4 ms) — 双击可加载到主工程" )
          .arg( taskId )
          .arg( QFileInfo( outputPath ).fileName() )
          .arg( r.outputBytes )
          .arg( r.durationMs ), 6000 );
    }
    else if ( r.status == QgsImageWarper::WarpStatus::Cancelled )
    {
      if ( mTaskList )
        mTaskList->finishCancelled( taskId, r.durationMs );
      statusBar()->showMessage( tr( "任务 #%1 已取消" ).arg( taskId ), 4000 );
    }
    else
    {
      if ( mTaskList )
        mTaskList->finishFailed( taskId, r.errorMessage, r.durationMs );
      statusBar()->showMessage(
        tr( "任务 #%1 失败: %2" ).arg( taskId ).arg( r.errorMessage ), 6000 );
    }
  };
  connect( task, &QgsTask::taskCompleted, this, finalize );
  connect( task, &QgsTask::taskTerminated, this, finalize );
  QgsApplication::taskManager()->addTask( task );

  statusBar()->showMessage( tr( "已加入任务列表 #%1 并开始运行…" ).arg( taskId ), 3000 );
}

void QgsGeorefShellWindow::cancelWarpTask( int taskId )
{
  const auto it = mActiveWarpTasks.constFind( taskId );
  if ( it == mActiveWarpTasks.constEnd() || !it.value() )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "任务 #%1 已不在运行" ).arg( taskId ), 3000 );
    return;
  }
  it.value()->cancel();
  if ( statusBar() )
    statusBar()->showMessage( tr( "正在取消任务 #%1…" ).arg( taskId ), 3000 );
}

void QgsGeorefShellWindow::loadWarpOutputToProject( const QString &path )
{
  if ( path.isEmpty() )
    return;

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
  if ( statusBar() )
    statusBar()->showMessage( tr( "已加载到主工程: %1" ).arg( layer->name() ), 5000 );
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
  o.insert( QStringLiteral( "rms_px" ), mLastRms );
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
    recomputeFit(); // restores Apply when fit + output path allow
  }
}

bool QgsGeorefShellWindow::isDirtyForTest() const { return mSession.isDirty(); }
void QgsGeorefShellWindow::markDirtyForTest() { mSession.markDirty(); }

int QgsGeorefShellWindow::gcpCountForTest() const
{
  return mGcps ? mGcps->size() : 0;
}

RsGeorefSessionState::WorkflowSnapshot QgsGeorefShellWindow::captureWorkflowSnapshot() const
{
  RsGeorefSessionState::WorkflowSnapshot s;
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
  s.lastPointsPath = mSession.lastPointsPath();
  captureShellSpecific( s );
  return s;
}

void QgsGeorefShellWindow::applyWorkflowSnapshot( const RsGeorefSessionState::WorkflowSnapshot &s )
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
    mSourceRasterPath = s.lastSourcePath;
  applyShellSpecific( s );
  recomputeFit();
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
  QgsCoordinateReferenceSystem destCrs;
  if ( mDstCanvas )
    destCrs = mDstCanvas->mapSettings().destinationCrs();
  if ( !destCrs.isValid() && mParamsPanel )
    destCrs = mParamsPanel->destCrs();

  // Snapshot before clear — never re-read mPendingSource after clearPending.
  const QgsPointXY src = sourceMap;
  const QgsPointXY dst = destMap;

  clearPendingGcpPick();
  if ( !mGcps )
    return;

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

  mGcps->appendPoint( QgsGcpPoint( src, dst, destCrs, true ) );
  rearmAddPointTools();
  if ( statusBar() )
    statusBar()->showMessage(
      tr( "已添加 GCP #%1：源 (%2, %3) → 目标 (%4, %5)" )
        .arg( mGcps->size() )
        .arg( src.x(), 0, 'f', 2 )
        .arg( src.y(), 0, 'f', 2 )
        .arg( dst.x(), 0, 'f', 2 )
        .arg( dst.y(), 0, 'f', 2 ),
      5000 );
}

void QgsGeorefShellWindow::onSourcePointPicked( const QgsPointXY &sourceMap )
{
  // Re-clicking SRC while pending updates the source position.
  beginPendingSourcePick( sourceMap );
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
  // Copy pending source now — commit clears pending state.
  const QgsPointXY src = mPendingSource;
  commitGcpPair( src, destMap );
}

void QgsGeorefShellWindow::showCoordDialog( const QgsPointXY &sourcePixel )
{
  // Advanced / compatibility path: typed destination coordinates.
  // Primary UX is dual-canvas pick (onSourcePointPicked / onDestPointPicked).
  auto *tempGcp = new QgsGcpPoint( sourcePixel, QgsPointXY(), QgsCoordinateReferenceSystem(), true );
  auto *tempDataPoint = new QgsGeorefDataPoint( mSrcCanvas, mDstCanvas, tempGcp );

  QgsCoordinateReferenceSystem rasterCrs = mSrcCanvas
                                             ? mSrcCanvas->mapSettings().destinationCrs()
                                             : QgsCoordinateReferenceSystem();

  auto *dlg = new QgsMapCoordsDialog( mDstCanvas, tempDataPoint, rasterCrs, this );
  dlg->setAttribute( Qt::WA_DeleteOnClose );
  tempDataPoint->setParent( dlg );

  connect( dlg, &QgsMapCoordsDialog::pointAdded, this,
           [this]( const QgsPointXY &srcCoord, const QgsPointXY &dstCoord,
                   const QgsCoordinateReferenceSystem &destCrs ) {
             if ( mGcps )
               mGcps->appendPoint( QgsGcpPoint( srcCoord, dstCoord, destCrs, true ) );
           } );
  connect( dlg, &QObject::destroyed, this, [tempGcp]() { delete tempGcp; } );
  dlg->show();
}

void QgsGeorefShellWindow::openSourceRaster()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Open source raster" ), QString(),
    tr( "Raster (*.tif *.tiff *.img *.jp2);;All files (*)" ) );
  if ( path.isEmpty() )
    return;

  setSourceRasterPath( path );
  auto *layer = new QgsRasterLayer( path, QFileInfo( path ).baseName(), QStringLiteral( "gdal" ) );
  if ( !layer->isValid() )
  {
    delete layer;
    if ( statusBar() )
      statusBar()->showMessage( tr( "Failed to open raster: %1" ).arg( path ), 5000 );
    return;
  }
  if ( mLayerStore )
    mLayerStore->addMapLayer( layer );
  mSrcRaster = layer;
  if ( mSrcCanvas )
  {
    // Keep SRC canvas CRS aligned with the source layer so map picks are in
    // layer/map units (not an unrelated project CRS). Critical for dual pick.
    if ( layer->crs().isValid() )
      mSrcCanvas->setDestinationCrs( layer->crs() );
    mSrcCanvas->setLayers( { layer } );
    mSrcCanvas->setExtent( layer->extent() );
    mSrcCanvas->refresh();
  }
  mSession.saveWorkflow( captureWorkflowSnapshot() );
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
  if ( mSession.isDirty() )
  {
    const auto ans = QMessageBox::question(
      this, tr( "未保存的控制点" ),
      tr( "GCP 列表有未保存的更改。是否保存？" ),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
    if ( ans == QMessageBox::Cancel ) { e->ignore(); return; }
    if ( ans == QMessageBox::Save )
    {
      QString path = mSession.lastPointsPath();
      if ( path.isEmpty() )
      {
        path = QFileDialog::getSaveFileName(
          this, tr( "Save GCP points" ), QString(),
          tr( "GCP Points (*.points *.gcp);;All files (*)" ) );
        if ( path.isEmpty() ) { e->ignore(); return; }
        if ( QFileInfo( path ).suffix().isEmpty() )
          path += QStringLiteral( ".points" );
      }
      if ( !mGcps || !mGcps->saveGcps( path ) )
      {
        QMessageBox::warning( this, tr( "Save GCPs" ), tr( "保存失败，窗口未关闭。" ) );
        e->ignore();
        return;
      }
      mSession.setLastPointsPath( path );
      mSession.clearDirty();
    }
  }
  mSession.saveWorkflow( captureWorkflowSnapshot() );
  mSession.saveWindow( this );
  e->accept();
}

void QgsGeorefShellWindow::loadPoints()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Load GCP points" ), mSession.lastPointsPath(),
    tr( "GCP Points (*.points *.gcp);;All files (*)" ) );
  if ( path.isEmpty() ) return;
  const QgsCoordinateReferenceSystem destCrs =
    mParamsPanel ? mParamsPanel->destCrs() : QgsCoordinateReferenceSystem();
  mSuppressDirtyFromList = true;
  const bool ok = mGcps->loadGcps( path, destCrs );
  mSuppressDirtyFromList = false;
  if ( !ok )
    QMessageBox::warning( this, tr( "Load GCPs" ), tr( "Failed to load GCP points from %1" ).arg( path ) );
  else
  {
    mSession.setLastPointsPath( path );
    mSession.clearDirty();
  }
}

void QgsGeorefShellWindow::savePoints()
{
  const QString path = QFileDialog::getSaveFileName(
    this, tr( "Save GCP points" ), mSession.lastPointsPath(),
    tr( "GCP Points (*.points *.gcp);;All files (*)" ) );
  if ( path.isEmpty() ) return;
  QString finalPath = path;
  if ( QFileInfo( finalPath ).suffix().isEmpty() )
    finalPath += QStringLiteral( ".points" );
  if ( !mGcps->saveGcps( finalPath ) )
    QMessageBox::warning( this, tr( "Save GCPs" ), tr( "Failed to save GCP points to %1" ).arg( finalPath ) );
  else
  {
    mSession.setLastPointsPath( finalPath );
    mSession.clearDirty();
  }
}

void QgsGeorefShellWindow::deleteGcpRows( const QList<int> &rows )
{
  if ( !mGcps || rows.isEmpty() ) return;
  QList<int> sortedRows = rows;
  std::sort( sortedRows.begin(), sortedRows.end(), std::greater<int>() );
  for ( int row : sortedRows )
    if ( row >= 0 && row < mGcps->size() )
      mGcps->removePointAt( row );
}

QgsGeorefDataPoint *QgsGeorefShellWindow::findDataPoint( const QgsPointXY &p, QgsGcpPoint::PointType type )
{
  QgsGeorefDataPoint *nearest = nullptr;
  double bestDistance = -1.0;
  for ( auto it = mDataPoints.cbegin(); it != mDataPoints.cend(); ++it )
  {
    QgsGeorefDataPoint *dp = it.value();
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
    mMovingPoint = nullptr;
    if ( mGcps ) mGcps->notifyPointsMutated();
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
  if ( !dp || !mGcps ) return;
  if ( mHoveredPoint == dp ) { mHoveredPoint->setHovered( false ); mHoveredPoint = nullptr; }
  if ( mMovingPoint == dp ) mMovingPoint = nullptr;
  QgsGcpPoint *gcp = dp->gcpPoint();
  for ( int i = 0; i < mGcps->size(); ++i )
  {
    if ( mGcps->at( i ) == gcp ) { mGcps->removePointAt( i ); break; }
  }
}
