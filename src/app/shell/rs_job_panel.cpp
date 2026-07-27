/***************************************************************************
 * rs_job_panel.cpp — Task Center projection: 详情 / 右键 / 加载到主图
 ***************************************************************************/
#include "rs_job_panel.h"

#include "main_window.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextCursor>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <json/json.h>

#include <algorithm>
#include <sstream>
#include <vector>

namespace {

constexpr int RoleTaskId = Qt::UserRole;
constexpr int RoleState = Qt::UserRole + 1;
constexpr int ColTitle = 0;
constexpr int ColState = 1;
constexpr int ColProgress = 2;
constexpr int ColLoad = 3;

bool looksLikePathKey( const QString &key )
{
  const QString k = key.toLower();
  return k.contains( QLatin1String( "input" ) )
         || k.contains( QLatin1String( "output" ) )
         || k.contains( QLatin1String( "source" ) )
         || k.contains( QLatin1String( "dest" ) )
         || k.contains( QLatin1String( "path" ) )
         || k.contains( QLatin1String( "file" ) )
         || k.contains( QLatin1String( "raster" ) )
         || k.contains( QLatin1String( "vector" ) )
         || k.contains( QLatin1String( "layer" ) )
         || k == QLatin1String( "in" )
         || k == QLatin1String( "out" );
}

bool isExistingPath( const QString &s )
{
  if ( s.isEmpty() || s.size() < 2 )
    return false;
  if ( s.startsWith( QLatin1Char( '/' ) ) || ( s.size() > 2 && s[1] == QLatin1Char( ':' ) ) )
    return QFileInfo::exists( s );
  return QFileInfo::exists( s );
}

void collectStringPaths( const Json::Value &v, QStringList &out )
{
  if ( v.isString() )
  {
    const QString s = QString::fromStdString( v.asString() );
    if ( isExistingPath( s ) && !out.contains( s ) )
      out.append( s );
    else if ( !s.isEmpty() && ( s.endsWith( QLatin1String( ".tif" ), Qt::CaseInsensitive )
                                || s.endsWith( QLatin1String( ".tiff" ), Qt::CaseInsensitive )
                                || s.endsWith( QLatin1String( ".shp" ), Qt::CaseInsensitive )
                                || s.endsWith( QLatin1String( ".gpkg" ), Qt::CaseInsensitive ) )
              && !out.contains( s ) )
    {
      out.append( s );
    }
  }
  else if ( v.isArray() )
  {
    for ( Json::ArrayIndex i = 0; i < v.size(); ++i )
      collectStringPaths( v[i], out );
  }
  else if ( v.isObject() )
  {
    for ( const auto &name : v.getMemberNames() )
      collectStringPaths( v[name], out );
  }
}

bool isActiveStatus( sicnu::TaskStatus status )
{
  return status == sicnu::TaskStatus::Queued
         || status == sicnu::TaskStatus::Running
         || status == sicnu::TaskStatus::Paused;
}

bool isTerminalStatus( sicnu::TaskStatus status )
{
  return status == sicnu::TaskStatus::Completed
         || status == sicnu::TaskStatus::Failed
         || status == sicnu::TaskStatus::Canceled;
}

QString taskTitle( const sicnu::AlgorithmTaskInfo &info )
{
  if ( info.hasJobRequest && !info.jobRequest.title.empty() )
    return QString::fromStdString( info.jobRequest.title );
  if ( !info.algorithmName.isEmpty() )
    return info.algorithmName;
  return info.algorithmId;
}

} // namespace

RsJobPanel::RsJobPanel( QWidget *parent )
  : QgsDockWidget( tr( "任务中心" ), parent )
{
  setObjectName( QStringLiteral( "rsJobPanelDock" ) );
  setAllowedAreas( Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea );

  setupUi();

  auto &center = sicnu::TaskCenter::instance();
  connect( &center, &sicnu::TaskCenter::taskAdded, this, &RsJobPanel::onTaskAdded, Qt::QueuedConnection );
  connect( &center, &sicnu::TaskCenter::taskUpdated, this, &RsJobPanel::onTaskUpdated, Qt::QueuedConnection );
  connect( &center, &sicnu::TaskCenter::taskLogAdded, this, &RsJobPanel::onTaskLogAdded, Qt::QueuedConnection );

  refreshAll();
}

void RsJobPanel::setupUi()
{
  auto *mainWidget = new QWidget( this );
  auto *mainLayout = new QVBoxLayout( mainWidget );
  mainLayout->setContentsMargins( 4, 4, 4, 4 );
  mainLayout->setSpacing( 4 );

  m_hintLabel = new QLabel(
    tr( "右键任务可查看方法/参数/输入输出，或停止、加载结果到主图。空白处右键也可刷新与查看说明。" ),
    mainWidget );
  m_hintLabel->setObjectName( QStringLiteral( "rsJobPanelHint" ) );
  m_hintLabel->setWordWrap( true );
  m_hintLabel->setStyleSheet( QStringLiteral( "color: #656d76; font-size: 11px;" ) );
  mainLayout->addWidget( m_hintLabel );

  auto *toolbar = new QWidget( mainWidget );
  auto *toolbarLayout = new QHBoxLayout( toolbar );
  toolbarLayout->setContentsMargins( 0, 0, 0, 0 );
  toolbarLayout->setSpacing( 4 );

  m_filterCombo = new QComboBox( toolbar );
  m_filterCombo->addItem( tr( "全部" ), QStringLiteral( "all" ) );
  m_filterCombo->addItem( tr( "运行中" ), QStringLiteral( "active" ) );
  m_filterCombo->addItem( tr( "失败" ), QStringLiteral( "failed" ) );
  m_filterCombo->addItem( tr( "已完成" ), QStringLiteral( "finished" ) );
  toolbarLayout->addWidget( m_filterCombo );
  toolbarLayout->addStretch();

  m_cancelBtn = new QPushButton( tr( "停止" ), toolbar );
  m_cancelBtn->setToolTip( tr( "取消排队或运行中的任务" ) );
  m_cancelBtn->setEnabled( false );
  toolbarLayout->addWidget( m_cancelBtn );

  m_loadBtn = new QPushButton( tr( "加载到主图" ), toolbar );
  m_loadBtn->setToolTip( tr( "将选中任务的输出路径加载到主程序图层" ) );
  m_loadBtn->setEnabled( false );
  toolbarLayout->addWidget( m_loadBtn );

  m_clearFinishedBtn = new QPushButton( tr( "清空已完成" ), toolbar );
  toolbarLayout->addWidget( m_clearFinishedBtn );

  mainLayout->addWidget( toolbar );

  auto *splitter = new QSplitter( Qt::Horizontal, mainWidget );

  m_jobTree = new QTreeWidget( splitter );
  m_jobTree->setColumnCount( 4 );
  m_jobTree->setHeaderLabels( { tr( "标题" ), tr( "状态" ), tr( "进度" ), tr( "加载" ) } );
  m_jobTree->setRootIsDecorated( false );
  m_jobTree->setSelectionMode( QAbstractItemView::SingleSelection );
  m_jobTree->setUniformRowHeights( true );
  m_jobTree->setContextMenuPolicy( Qt::CustomContextMenu );
  m_jobTree->setAlternatingRowColors( true );
  m_jobTree->header()->setStretchLastSection( false );
  m_jobTree->header()->setSectionResizeMode( ColTitle, QHeaderView::Stretch );
  m_jobTree->header()->setSectionResizeMode( ColState, QHeaderView::ResizeToContents );
  m_jobTree->header()->setSectionResizeMode( ColProgress, QHeaderView::ResizeToContents );
  m_jobTree->header()->setSectionResizeMode( ColLoad, QHeaderView::ResizeToContents );
  m_jobTree->headerItem()->setToolTip( ColLoad, tr( "勾选：任务成功后自动将输出加载到主程序" ) );
  splitter->addWidget( m_jobTree );

  m_detailTabs = new QTabWidget( splitter );
  m_detailView = new QPlainTextEdit( m_detailTabs );
  m_detailView->setReadOnly( true );
  m_detailView->setPlaceholderText( tr( "选择任务查看方法、参数、输入输出…" ) );
  m_detailTabs->addTab( m_detailView, tr( "详情" ) );

  m_logView = new QPlainTextEdit( m_detailTabs );
  m_logView->setReadOnly( true );
  m_logView->setPlaceholderText( tr( "选择任务以查看日志…" ) );
  m_logView->setMaximumBlockCount( 20000 );
  m_detailTabs->addTab( m_logView, tr( "日志" ) );

  splitter->addWidget( m_detailTabs );
  splitter->setStretchFactor( 0, 1 );
  splitter->setStretchFactor( 1, 2 );
  mainLayout->addWidget( splitter, 1 );

  setWidget( mainWidget );

  connect( m_jobTree, &QTreeWidget::itemSelectionChanged, this, &RsJobPanel::onSelectionChanged );
  connect( m_jobTree, &QTreeWidget::customContextMenuRequested,
           this, &RsJobPanel::onContextMenuRequested );
  connect( m_jobTree, &QTreeWidget::itemChanged, this, &RsJobPanel::onItemChanged );
  connect( m_cancelBtn, &QPushButton::clicked, this, &RsJobPanel::onCancelClicked );
  connect( m_loadBtn, &QPushButton::clicked, this, [this]() {
    const long id = selectedTaskId();
    if ( id < 0 )
      return;
    const int n = loadPathsToMain( collectOutputPaths( id ) );
    if ( n <= 0 )
      QMessageBox::information( this, tr( "加载到主图" ),
                                tr( "未找到可加载的输出路径（或文件不存在）。" ) );
  } );
  connect( m_clearFinishedBtn, &QPushButton::clicked, this, &RsJobPanel::onClearFinishedClicked );
  connect( m_filterCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &RsJobPanel::onFilterChanged );
}

QString RsJobPanel::statusToString( sicnu::TaskStatus status )
{
  switch ( status )
  {
    case sicnu::TaskStatus::Queued:
      return QObject::tr( "排队" );
    case sicnu::TaskStatus::Running:
      return QObject::tr( "运行中" );
    case sicnu::TaskStatus::Paused:
      return QObject::tr( "已暂停" );
    case sicnu::TaskStatus::Completed:
      return QObject::tr( "成功" );
    case sicnu::TaskStatus::Failed:
      return QObject::tr( "失败" );
    case sicnu::TaskStatus::Canceled:
      return QObject::tr( "已取消" );
  }
  return QObject::tr( "未知" );
}

QString RsJobPanel::formatProgress( double progress )
{
  if ( progress < 0.0 )
    return QStringLiteral( "…" );
  if ( progress > 1.0 )
    progress = 1.0;
  return QStringLiteral( "%1%" ).arg( static_cast<int>( progress * 100.0 + 0.5 ) );
}

QString RsJobPanel::prettyJsonValue( const Json::Value &v )
{
  Json::StreamWriterBuilder b;
  b["indentation"] = "  ";
  b["enableYAMLCompatibility"] = false;
  return QString::fromStdString( Json::writeString( b, v ) );
}

QString RsJobPanel::prettyJson( const std::string &jsonText )
{
  Json::Value root;
  Json::CharReaderBuilder rb;
  std::string errs;
  std::istringstream iss( jsonText );
  if ( !Json::parseFromStream( rb, iss, &root, &errs ) )
    return QString::fromStdString( jsonText );
  return prettyJsonValue( root );
}

bool RsJobPanel::passesFilter( const QString &stateText ) const
{
  const QString key = m_filterCombo
                        ? m_filterCombo->currentData().toString()
                        : QStringLiteral( "all" );
  if ( key == QLatin1String( "all" ) )
    return true;
  if ( key == QLatin1String( "active" ) )
    return stateText == tr( "排队" ) || stateText == tr( "运行中" ) || stateText == tr( "已暂停" );
  if ( key == QLatin1String( "failed" ) )
    return stateText == tr( "失败" );
  if ( key == QLatin1String( "finished" ) )
    return stateText == tr( "成功" ) || stateText == tr( "失败" ) || stateText == tr( "已取消" );
  return true;
}

bool RsJobPanel::loadToMainPreference( long taskId ) const
{
  if ( m_loadToMain.contains( taskId ) )
    return m_loadToMain.value( taskId );
  const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
  if ( info.taskId != taskId )
    return false;
  return info.autoLoadLayer;
}

void RsJobPanel::setLoadToMainPreference( long taskId, bool on )
{
  m_loadToMain.insert( taskId, on );
}

void RsJobPanel::refreshAll()
{
  const long keepId = selectedTaskId();
  m_blockItemChanged = true;
  m_jobTree->clear();

  auto tasks = sicnu::TaskCenter::instance().allTasks();
  std::sort( tasks.begin(), tasks.end(), []( const sicnu::AlgorithmTaskInfo &a,
                                             const sicnu::AlgorithmTaskInfo &b ) {
    return a.taskId > b.taskId;
  } );

  QTreeWidgetItem *selectItem = nullptr;
  for ( const sicnu::AlgorithmTaskInfo &info : tasks )
  {
    const QString state = statusToString( info.status );
    if ( !passesFilter( state ) )
      continue;

    auto *item = new QTreeWidgetItem( m_jobTree );
    item->setText( ColTitle, taskTitle( info ) );
    item->setText( ColState, state );
    item->setText( ColProgress, formatProgress( info.progressPercentage ) );
    item->setData( ColTitle, RoleTaskId, static_cast<qlonglong>( info.taskId ) );
    item->setData( ColTitle, RoleState, static_cast<int>( info.status ) );
    item->setFlags( item->flags() | Qt::ItemIsUserCheckable );
    item->setCheckState( ColLoad, loadToMainPreference( info.taskId ) ? Qt::Checked : Qt::Unchecked );
    item->setToolTip( ColTitle,
                      tr( "任务 ID: %1\n方法: %2\n右键查看详情 / 停止 / 加载" )
                        .arg( info.taskId )
                        .arg( info.algorithmId ) );
    item->setToolTip( ColLoad, tr( "勾选后任务成功时自动加载输出到主程序" ) );
    if ( info.taskId == keepId )
      selectItem = item;
  }
  m_blockItemChanged = false;

  if ( selectItem )
  {
    m_jobTree->setCurrentItem( selectItem );
  }
  else if ( m_selectedId >= 0 )
  {
    m_selectedId = -1;
    m_detailView->clear();
    m_logView->clear();
  }
  updateActionEnabled();
}

void RsJobPanel::upsertTaskRow( const sicnu::AlgorithmTaskInfo &info )
{
  const QString state = statusToString( info.status );
  const bool show = passesFilter( state );
  const long taskId = info.taskId;

  QTreeWidgetItem *found = nullptr;
  for ( int i = 0; i < m_jobTree->topLevelItemCount(); ++i )
  {
    QTreeWidgetItem *item = m_jobTree->topLevelItem( i );
    if ( item->data( ColTitle, RoleTaskId ).toLongLong() == taskId )
    {
      found = item;
      break;
    }
  }

  if ( !show )
  {
    if ( found )
      delete found;
    if ( m_selectedId == taskId )
    {
      m_selectedId = -1;
      m_detailView->clear();
      m_logView->clear();
      updateActionEnabled();
    }
    return;
  }

  m_blockItemChanged = true;
  if ( !found )
  {
    found = new QTreeWidgetItem();
    m_jobTree->insertTopLevelItem( 0, found );
    found->setFlags( found->flags() | Qt::ItemIsUserCheckable );
    found->setCheckState( ColLoad, loadToMainPreference( taskId ) ? Qt::Checked : Qt::Unchecked );
  }

  found->setText( ColTitle, taskTitle( info ) );
  found->setText( ColState, state );
  found->setText( ColProgress, formatProgress( info.progressPercentage ) );
  found->setData( ColTitle, RoleTaskId, static_cast<qlonglong>( taskId ) );
  found->setData( ColTitle, RoleState, static_cast<int>( info.status ) );
  found->setToolTip( ColTitle,
                     tr( "任务 ID: %1\n方法: %2\n右键查看详情 / 停止 / 加载" )
                       .arg( taskId )
                       .arg( info.algorithmId ) );
  m_blockItemChanged = false;

  if ( m_selectedId < 0 && info.status == sicnu::TaskStatus::Running )
    m_jobTree->setCurrentItem( found );
}

void RsJobPanel::fillLogForTask( long taskId )
{
  m_logView->clear();
  const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
  if ( info.taskId != taskId )
  {
    m_logView->setPlainText( tr( "(任务不存在)" ) );
    return;
  }

  QStringList lines;
  lines.reserve( info.logBuffer.size() + 4 );
  lines.append( tr( "—— 任务日志 · %1 ——" ).arg( taskTitle( info ) ) );
  for ( const QString &line : info.logBuffer )
    lines.append( line );
  if ( !info.errorMessage.isEmpty() )
    lines.append( QStringLiteral( "[ERROR] %1" ).arg( info.errorMessage ) );
  if ( lines.size() == 1 )
    lines.append( tr( "(暂无日志)" ) );

  m_logView->setPlainText( lines.join( QLatin1Char( '\n' ) ) );
  auto cursor = m_logView->textCursor();
  cursor.movePosition( QTextCursor::End );
  m_logView->setTextCursor( cursor );
}

void RsJobPanel::fillDetailsForTask( long taskId )
{
  m_detailView->clear();
  const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
  if ( info.taskId != taskId )
  {
    m_detailView->setPlainText( tr( "(任务不存在)" ) );
    return;
  }

  QStringList lines;
  lines << tr( "【基本信息】" );
  lines << tr( "任务 ID：%1" ).arg( taskId );
  lines << tr( "标题：%1" ).arg( taskTitle( info ) );
  lines << tr( "方法 (algorithmId)：%1" ).arg( info.algorithmId );
  if ( info.hasJobRequest && !info.jobRequest.source.empty() )
    lines << tr( "来源：%1" ).arg( QString::fromStdString( info.jobRequest.source ) );
  if ( info.hasJobRequest && !info.jobRequest.clientTag.empty() )
    lines << tr( "客户端标记：%1" ).arg( QString::fromStdString( info.jobRequest.clientTag ) );
  if ( info.hasJobRequest )
    lines << tr( "独占执行：%1" ).arg( info.jobRequest.exclusive ? tr( "是" ) : tr( "否" ) );
  if ( !info.jobId.empty() )
    lines << tr( "内部 jobId：%1" ).arg( QString::fromStdString( info.jobId ) );
  lines << tr( "状态：%1" ).arg( statusToString( info.status ) );
  lines << tr( "进度：%1" ).arg( formatProgress( info.progressPercentage ) );
  lines << tr( "成功后加载到主图：%1" )
             .arg( loadToMainPreference( taskId ) ? tr( "是" ) : tr( "否" ) );
  if ( info.startTime.isValid() )
    lines << tr( "开始时间：%1" ).arg( info.startTime.toString( Qt::ISODate ) );
  if ( info.endTime.isValid() )
    lines << tr( "结束时间：%1" ).arg( info.endTime.toString( Qt::ISODate ) );
  if ( !info.errorMessage.isEmpty() )
    lines << tr( "错误：%1" ).arg( info.errorMessage );

  lines << QString();
  lines << tr( "【方法参数 params】" );
  if ( info.hasJobRequest
       && !( info.jobRequest.params.isNull()
             || ( info.jobRequest.params.isObject() && info.jobRequest.params.empty() ) ) )
  {
    lines << prettyJsonValue( info.jobRequest.params );
  }
  else if ( !info.parameterMap.isEmpty() )
  {
    for ( auto it = info.parameterMap.constBegin(); it != info.parameterMap.constEnd(); ++it )
      lines << QStringLiteral( "  %1 = %2" ).arg( it.key(), it.value().toString() );
  }
  else
  {
    lines << tr( "(无参数)" );
  }

  lines << QString();
  lines << tr( "【输入 / 路径类参数】" );
  bool anyIn = false;
  if ( info.hasJobRequest && info.jobRequest.params.isObject() )
  {
    for ( const auto &name : info.jobRequest.params.getMemberNames() )
    {
      const QString qn = QString::fromStdString( name );
      if ( !looksLikePathKey( qn ) )
        continue;
      anyIn = true;
      const Json::Value &v = info.jobRequest.params[name];
      if ( v.isString() )
        lines << QStringLiteral( "  %1 = %2" ).arg( qn, QString::fromStdString( v.asString() ) );
      else
        lines << QStringLiteral( "  %1 = %2" ).arg( qn, prettyJsonValue( v ).simplified() );
    }
  }
  if ( !anyIn )
  {
    for ( auto it = info.parameterMap.constBegin(); it != info.parameterMap.constEnd(); ++it )
    {
      if ( !looksLikePathKey( it.key() ) )
        continue;
      anyIn = true;
      lines << QStringLiteral( "  %1 = %2" ).arg( it.key(), it.value().toString() );
    }
  }
  if ( !anyIn )
    lines << tr( "  (未识别到路径类输入键)" );

  lines << QString();
  lines << tr( "【结果 / 输出 result】" );
  if ( info.resultPayload.isNull()
       || ( info.resultPayload.isObject() && info.resultPayload.empty() ) )
  {
    if ( !info.outputLayerPath.isEmpty() )
      lines << QStringLiteral( "  output = %1" ).arg( info.outputLayerPath );
    else
      lines << tr( "(尚无结果)" );
  }
  else
  {
    lines << prettyJsonValue( info.resultPayload );
  }

  const QStringList outs = collectOutputPaths( taskId );
  lines << QString();
  lines << tr( "【可加载输出路径】" );
  if ( outs.isEmpty() )
    lines << tr( "  (无)" );
  else
  {
    for ( const QString &p : outs )
    {
      const bool ok = QFileInfo::exists( p );
      lines << QStringLiteral( "  %1  %2" )
                 .arg( p, ok ? tr( "[存在]" ) : tr( "[不存在]" ) );
    }
  }

  m_detailView->setPlainText( lines.join( QLatin1Char( '\n' ) ) );
}

QStringList RsJobPanel::collectOutputPaths( long taskId ) const
{
  QStringList out;
  const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
  if ( info.taskId != taskId )
    return out;

  if ( !info.outputLayerPath.isEmpty() && !out.contains( info.outputLayerPath ) )
    out.append( info.outputLayerPath );

  if ( info.resultPayload.isObject() )
  {
    static const char *kKeys[] = {
      "output", "OUTPUT", "outputPath", "out", "dest", "destination",
      "OUTPUT_RASTER", "OUTPUT_VECTOR", "OUTPUT_LAYER"
    };
    for ( const char *k : kKeys )
    {
      if ( info.resultPayload.isMember( k ) )
        collectStringPaths( info.resultPayload[k], out );
    }
    collectStringPaths( info.resultPayload, out );
  }

  if ( info.hasJobRequest && info.jobRequest.params.isObject() )
  {
    for ( const auto &name : info.jobRequest.params.getMemberNames() )
    {
      const QString qn = QString::fromStdString( name );
      if ( qn.contains( QLatin1String( "output" ), Qt::CaseInsensitive )
           || qn.contains( QLatin1String( "dest" ), Qt::CaseInsensitive )
           || qn.endsWith( QLatin1String( "out" ), Qt::CaseInsensitive ) )
      {
        collectStringPaths( info.jobRequest.params[name], out );
      }
    }
  }
  return out;
}

int RsJobPanel::loadPathsToMain( const QStringList &paths )
{
  auto *mw = qobject_cast<QgisDesktopWindow *>( window() );
  if ( !mw )
  {
    QWidget *w = parentWidget();
    while ( w && !mw )
    {
      mw = qobject_cast<QgisDesktopWindow *>( w );
      w = w->parentWidget();
    }
  }
  if ( !mw )
    return 0;

  int loaded = 0;
  for ( const QString &path : paths )
  {
    if ( path.isEmpty() || !QFileInfo::exists( path ) )
      continue;
    const QString lower = path.toLower();
    const bool preferVector = lower.endsWith( QLatin1String( ".shp" ) )
                              || lower.endsWith( QLatin1String( ".gpkg" ) )
                              || lower.endsWith( QLatin1String( ".geojson" ) )
                              || lower.endsWith( QLatin1String( ".json" ) )
                              || lower.endsWith( QLatin1String( ".kml" ) )
                              || lower.endsWith( QLatin1String( ".gml" ) );
    // Always via main-window Data/Display seam (register Asset + main view).
    if ( preferVector )
      mw->loadVectorLayer( path );
    else
      ( void ) mw->loadDataLayer( path );
    ++loaded;
  }
  return loaded;
}

void RsJobPanel::tryAutoLoadOutputs( const sicnu::AlgorithmTaskInfo &info )
{
  if ( info.status != sicnu::TaskStatus::Completed )
    return;
  if ( !loadToMainPreference( info.taskId ) )
    return;
  loadPathsToMain( collectOutputPaths( info.taskId ) );
}

long RsJobPanel::selectedTaskId() const
{
  const auto items = m_jobTree->selectedItems();
  if ( items.isEmpty() )
    return -1;
  return items.first()->data( ColTitle, RoleTaskId ).toLongLong();
}

void RsJobPanel::updateActionEnabled()
{
  const long id = selectedTaskId();
  if ( id < 0 )
  {
    m_cancelBtn->setEnabled( false );
    m_loadBtn->setEnabled( false );
    return;
  }
  const auto items = m_jobTree->selectedItems();
  const int state = items.isEmpty() ? -1 : items.first()->data( ColTitle, RoleState ).toInt();
  const auto status = static_cast<sicnu::TaskStatus>( state );
  m_cancelBtn->setEnabled( isActiveStatus( status ) );

  const bool canLoad = ( status == sicnu::TaskStatus::Completed )
                       && !collectOutputPaths( id ).isEmpty();
  m_loadBtn->setEnabled( canLoad || status == sicnu::TaskStatus::Completed );
}

void RsJobPanel::onTaskAdded( const sicnu::AlgorithmTaskInfo &info )
{
  upsertTaskRow( info );
}

void RsJobPanel::onTaskUpdated( const sicnu::AlgorithmTaskInfo &info )
{
  upsertTaskRow( info );
  if ( info.taskId == m_selectedId || info.taskId == selectedTaskId() )
  {
    m_selectedId = info.taskId;
    fillLogForTask( info.taskId );
    fillDetailsForTask( info.taskId );
    updateActionEnabled();
  }
  if ( isTerminalStatus( info.status ) )
    tryAutoLoadOutputs( info );
}

void RsJobPanel::onTaskLogAdded( long taskId, const QString & )
{
  if ( taskId == m_selectedId || taskId == selectedTaskId() )
    fillLogForTask( taskId );
}

void RsJobPanel::onSelectionChanged()
{
  m_selectedId = selectedTaskId();
  if ( m_selectedId < 0 )
  {
    m_detailView->clear();
    m_logView->clear();
    updateActionEnabled();
    return;
  }
  fillDetailsForTask( m_selectedId );
  fillLogForTask( m_selectedId );
  updateActionEnabled();
}

void RsJobPanel::onCancelClicked()
{
  const long id = selectedTaskId();
  if ( id < 0 )
    return;
  sicnu::TaskCenter::instance().cancelTask( id );
}

void RsJobPanel::onClearFinishedClicked()
{
  sicnu::TaskCenter::instance().clearCompletedTasks();
  for ( int i = m_jobTree->topLevelItemCount() - 1; i >= 0; --i )
  {
    QTreeWidgetItem *item = m_jobTree->topLevelItem( i );
    const auto status = static_cast<sicnu::TaskStatus>(
      item->data( ColTitle, RoleState ).toInt() );
    if ( !isTerminalStatus( status ) )
      continue;
    const long id = item->data( ColTitle, RoleTaskId ).toLongLong();
    delete item;
    m_loadToMain.remove( id );
    if ( m_selectedId == id )
    {
      m_selectedId = -1;
      m_detailView->clear();
      m_logView->clear();
    }
  }
  updateActionEnabled();
}

void RsJobPanel::onFilterChanged()
{
  refreshAll();
}

void RsJobPanel::onItemChanged( QTreeWidgetItem *item, int column )
{
  if ( m_blockItemChanged || !item || column != ColLoad )
    return;
  const long id = item->data( ColTitle, RoleTaskId ).toLongLong();
  if ( id < 0 )
    return;
  setLoadToMainPreference( id, item->checkState( ColLoad ) == Qt::Checked );
  if ( id == m_selectedId )
    fillDetailsForTask( id );
}

void RsJobPanel::copyText( const QString &text )
{
  if ( QClipboard *cb = QApplication::clipboard() )
    cb->setText( text );
}

void RsJobPanel::showAboutDialog()
{
  QMessageBox::information(
    this, tr( "任务中心" ),
    tr( "任务中心汇总所有经 Task Center 提交的算法任务（JobEngine 为内部执行适配器）。\n\n"
        "• 列表显示标题、状态、进度；「加载」列勾选后，任务成功时自动把输出加载到主图。\n"
        "• 右键任务：查看详情（方法/参数/输入输出）、停止、加载输出、复制信息。\n"
        "• 列表空白处右键：刷新、清空已完成、本说明。\n"
        "• 取消、日志与终态均以 Task Center 为准；本面板为投影，不持有独立生命周期状态。" ) );
}

void RsJobPanel::onContextMenuRequested( const QPoint &pos )
{
  QTreeWidgetItem *item = m_jobTree->itemAt( pos );
  QMenu menu( this );

  if ( !item )
  {
    menu.addAction( tr( "刷新列表" ), this, [this]() { refreshAll(); } );
    menu.addAction( tr( "清空已完成" ), this, &RsJobPanel::onClearFinishedClicked );
    menu.addSeparator();
    menu.addAction( tr( "任务中心说明…" ), this, &RsJobPanel::showAboutDialog );
    const auto tasks = sicnu::TaskCenter::instance().allTasks();
    int active = 0, done = 0;
    for ( const auto &t : tasks )
    {
      if ( isActiveStatus( t.status ) )
        ++active;
      else
        ++done;
    }
    auto *info = menu.addAction(
      tr( "当前：%1 个活动 / %2 个已结束（Task Center 共 %3）" )
        .arg( active )
        .arg( done )
        .arg( tasks.size() ) );
    info->setEnabled( false );
    menu.exec( m_jobTree->viewport()->mapToGlobal( pos ) );
    return;
  }

  m_jobTree->setCurrentItem( item );
  const long taskId = item->data( ColTitle, RoleTaskId ).toLongLong();
  const auto status = static_cast<sicnu::TaskStatus>(
    item->data( ColTitle, RoleState ).toInt() );
  const bool cancellable = isActiveStatus( status );
  const bool succeeded = ( status == sicnu::TaskStatus::Completed );

  menu.addAction( tr( "查看详情" ), this, [this, taskId]() {
    if ( m_detailTabs )
      m_detailTabs->setCurrentWidget( m_detailView );
    fillDetailsForTask( taskId );
  } );
  menu.addAction( tr( "查看日志" ), this, [this, taskId]() {
    if ( m_detailTabs )
      m_detailTabs->setCurrentWidget( m_logView );
    fillLogForTask( taskId );
  } );
  menu.addSeparator();

  QAction *stopAct = menu.addAction( tr( "停止 / 取消" ), this, [taskId]() {
    sicnu::TaskCenter::instance().cancelTask( taskId );
  } );
  stopAct->setEnabled( cancellable );

  QAction *loadAct = menu.addAction( tr( "加载输出到主图" ), this, [this, taskId]() {
    const int n = loadPathsToMain( collectOutputPaths( taskId ) );
    if ( n <= 0 )
      QMessageBox::information( this, tr( "加载到主图" ),
                                tr( "未找到可加载的输出文件。" ) );
  } );
  loadAct->setEnabled( succeeded );

  QAction *autoLoad = menu.addAction( tr( "成功后加载到主图" ) );
  autoLoad->setCheckable( true );
  autoLoad->setChecked( loadToMainPreference( taskId ) );
  connect( autoLoad, &QAction::toggled, this, [this, taskId, item]( bool on ) {
    setLoadToMainPreference( taskId, on );
    m_blockItemChanged = true;
    item->setCheckState( ColLoad, on ? Qt::Checked : Qt::Unchecked );
    m_blockItemChanged = false;
    if ( taskId == m_selectedId )
      fillDetailsForTask( taskId );
  } );

  menu.addSeparator();
  menu.addAction( tr( "复制任务 ID" ), this, [this, taskId]() {
    copyText( QString::number( taskId ) );
  } );
  menu.addAction( tr( "复制方法 ID" ), this, [this, taskId]() {
    const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( info.taskId == taskId )
      copyText( info.algorithmId );
  } );
  menu.addAction( tr( "复制参数 JSON" ), this, [this, taskId]() {
    const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( info.taskId == taskId && info.hasJobRequest )
      copyText( prettyJsonValue( info.jobRequest.params ) );
  } );
  menu.addAction( tr( "复制结果 JSON" ), this, [this, taskId]() {
    const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( info.taskId == taskId )
      copyText( prettyJsonValue( info.resultPayload ) );
  } );
  menu.addAction( tr( "复制详情全文" ), this, [this, taskId]() {
    fillDetailsForTask( taskId );
    copyText( m_detailView->toPlainText() );
  } );

  menu.addSeparator();
  QAction *removeAct = menu.addAction( tr( "从列表移除" ), this, [this, item, taskId]() {
    delete item;
    m_loadToMain.remove( taskId );
    if ( m_selectedId == taskId )
    {
      m_selectedId = -1;
      m_detailView->clear();
      m_logView->clear();
    }
    updateActionEnabled();
  } );
  removeAct->setEnabled( !cancellable );

  menu.addSeparator();
  menu.addAction( tr( "刷新列表" ), this, [this]() { refreshAll(); } );
  menu.addAction( tr( "任务中心说明…" ), this, &RsJobPanel::showAboutDialog );

  menu.exec( m_jobTree->viewport()->mapToGlobal( pos ) );
}
