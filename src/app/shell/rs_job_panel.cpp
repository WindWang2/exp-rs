/***************************************************************************
 * rs_job_panel.cpp — 任务中心：详情 / 右键 / 加载到主图
 ***************************************************************************/
#include "rs_job_panel.h"

#include "job_engine_qt_bridge.h"
#include "main_window.h"

#include "jobs/job_engine.h"
#include "jobs/job_types.h"

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

using sicnu::jobs::JobEngine;
using sicnu::jobs::JobLogLevel;
using sicnu::jobs::JobRecord;
using sicnu::jobs::JobState;

namespace {

constexpr int RoleJobId = Qt::UserRole;
constexpr int RoleState = Qt::UserRole + 1;
constexpr int ColTitle = 0;
constexpr int ColState = 1;
constexpr int ColProgress = 2;
constexpr int ColLoad = 3;

QString logLevelPrefix( JobLogLevel level )
{
  switch ( level )
  {
    case JobLogLevel::Warning:
      return QStringLiteral( "WARN" );
    case JobLogLevel::Error:
      return QStringLiteral( "ERROR" );
    case JobLogLevel::Info:
    default:
      return QStringLiteral( "INFO" );
  }
}

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
  // Absolute path or Windows drive / UNC-ish
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
                                || s.endsWith( QLatin1String( ".gpkg" ), Qt::CaseInsensitive )
                                || s.endsWith( QLatin1String( ".img" ), Qt::CaseInsensitive )
                                || s.endsWith( QLatin1String( ".vrt" ), Qt::CaseInsensitive ) )
              && !out.contains( s ) )
    {
      // Include likely outputs even if not yet on disk (failed jobs) — load will skip missing.
      out.append( s );
    }
  }
  else if ( v.isArray() )
  {
    for ( const auto &e : v )
      collectStringPaths( e, out );
  }
  else if ( v.isObject() )
  {
    for ( const auto &name : v.getMemberNames() )
      collectStringPaths( v[name], out );
  }
}

} // namespace

RsJobPanel::RsJobPanel( QWidget *parent )
  : QgsDockWidget( tr( "任务中心" ), parent )
{
  setObjectName( QStringLiteral( "rsJobPanelDock" ) );
  setAllowedAreas( Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea );

  setupUi();

  auto *bridge = JobEngineQtBridge::instance();
  connect( bridge, &JobEngineQtBridge::jobUpdated, this, &RsJobPanel::onJobUpdated );
  connect( bridge, &JobEngineQtBridge::jobFinished, this, &RsJobPanel::onJobFinished );

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
    const QString id = selectedJobId();
    if ( id.isEmpty() )
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

QString RsJobPanel::stateToString( int state )
{
  switch ( static_cast<JobState>( state ) )
  {
    case JobState::Queued:
      return tr( "排队" );
    case JobState::Running:
      return tr( "运行中" );
    case JobState::Succeeded:
      return tr( "成功" );
    case JobState::Failed:
      return tr( "失败" );
    case JobState::Cancelled:
      return tr( "已取消" );
  }
  return tr( "未知" );
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
    return stateText == tr( "排队" ) || stateText == tr( "运行中" );
  if ( key == QLatin1String( "failed" ) )
    return stateText == tr( "失败" );
  if ( key == QLatin1String( "finished" ) )
    return stateText == tr( "成功" ) || stateText == tr( "失败" ) || stateText == tr( "已取消" );
  return true;
}

bool RsJobPanel::loadToMainPreference( const QString &jobId ) const
{
  if ( m_loadToMain.contains( jobId ) )
    return m_loadToMain.value( jobId );
  auto snap = JobEngine::instance().snapshot( jobId.toStdString() );
  if ( !snap )
    return false;
  // Optional request.params.loadOutputsToMain boolean
  const auto &p = snap->request.params;
  if ( p.isObject() && p.isMember( "loadOutputsToMain" ) && p["loadOutputsToMain"].isBool() )
    return p["loadOutputsToMain"].asBool();
  if ( p.isObject() && p.isMember( "loadToMain" ) && p["loadToMain"].isBool() )
    return p["loadToMain"].asBool();
  return false;
}

void RsJobPanel::setLoadToMainPreference( const QString &jobId, bool on )
{
  m_loadToMain.insert( jobId, on );
}

void RsJobPanel::refreshAll()
{
  const QString keepId = selectedJobId();
  m_blockItemChanged = true;
  m_jobTree->clear();

  const auto jobs = JobEngine::instance().list();
  std::vector<JobRecord> sorted = jobs;
  std::sort( sorted.begin(), sorted.end(), []( const JobRecord &a, const JobRecord &b ) {
    return a.createdAtMs > b.createdAtMs;
  } );

  QTreeWidgetItem *selectItem = nullptr;
  for ( const JobRecord &rec : sorted )
  {
    const QString id = QString::fromStdString( rec.id );
    const QString title = rec.request.title.empty()
                            ? QString::fromStdString( rec.request.algorithmId )
                            : QString::fromStdString( rec.request.title );
    const QString state = stateToString( static_cast<int>( rec.state ) );
    if ( !passesFilter( state ) )
      continue;

    auto *item = new QTreeWidgetItem( m_jobTree );
    item->setText( ColTitle, title );
    item->setText( ColState, state );
    item->setText( ColProgress, formatProgress( rec.progress ) );
    item->setData( ColTitle, RoleJobId, id );
    item->setData( ColTitle, RoleState, static_cast<int>( rec.state ) );
    item->setFlags( item->flags() | Qt::ItemIsUserCheckable );
    item->setCheckState( ColLoad, loadToMainPreference( id ) ? Qt::Checked : Qt::Unchecked );
    item->setToolTip( ColTitle,
                      tr( "ID: %1\n方法: %2\n来源: %3\n右键查看详情 / 停止 / 加载" )
                        .arg( id,
                              QString::fromStdString( rec.request.algorithmId ),
                              QString::fromStdString( rec.request.source ) ) );
    item->setToolTip( ColLoad, tr( "勾选后任务成功时自动加载输出到主程序" ) );
    if ( id == keepId )
      selectItem = item;
  }
  m_blockItemChanged = false;

  if ( selectItem )
  {
    m_jobTree->setCurrentItem( selectItem );
  }
  else if ( !m_selectedId.isEmpty() )
  {
    m_selectedId.clear();
    m_detailView->clear();
    m_logView->clear();
  }
  updateActionEnabled();
}

void RsJobPanel::upsertJobRow( const QString &jobId )
{
  auto snap = JobEngine::instance().snapshot( jobId.toStdString() );
  if ( !snap )
    return;

  const JobRecord &rec = *snap;
  const QString title = rec.request.title.empty()
                          ? QString::fromStdString( rec.request.algorithmId )
                          : QString::fromStdString( rec.request.title );
  const QString state = stateToString( static_cast<int>( rec.state ) );
  const bool show = passesFilter( state );

  QTreeWidgetItem *found = nullptr;
  for ( int i = 0; i < m_jobTree->topLevelItemCount(); ++i )
  {
    QTreeWidgetItem *item = m_jobTree->topLevelItem( i );
    if ( item->data( ColTitle, RoleJobId ).toString() == jobId )
    {
      found = item;
      break;
    }
  }

  if ( !show )
  {
    if ( found )
      delete found;
    if ( m_selectedId == jobId )
    {
      m_selectedId.clear();
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
    found->setCheckState( ColLoad, loadToMainPreference( jobId ) ? Qt::Checked : Qt::Unchecked );
  }

  found->setText( ColTitle, title );
  found->setText( ColState, state );
  found->setText( ColProgress, formatProgress( rec.progress ) );
  found->setData( ColTitle, RoleJobId, jobId );
  found->setData( ColTitle, RoleState, static_cast<int>( rec.state ) );
  found->setToolTip( ColTitle,
                     tr( "ID: %1\n方法: %2\n来源: %3\n右键查看详情 / 停止 / 加载" )
                       .arg( jobId,
                             QString::fromStdString( rec.request.algorithmId ),
                             QString::fromStdString( rec.request.source ) ) );
  m_blockItemChanged = false;

  if ( m_selectedId.isEmpty() && rec.state == JobState::Running )
    m_jobTree->setCurrentItem( found );
}

void RsJobPanel::fillLogForJob( const QString &jobId )
{
  m_logView->clear();
  auto snap = JobEngine::instance().snapshot( jobId.toStdString() );
  if ( !snap )
  {
    m_logView->setPlainText( tr( "(任务不存在)" ) );
    return;
  }

  QStringList lines;
  lines.reserve( static_cast<int>( snap->logLines.size() ) + 4 );
  lines.append( tr( "—— 任务日志 · %1 ——" )
                  .arg( snap->request.title.empty()
                          ? QString::fromStdString( snap->request.algorithmId )
                          : QString::fromStdString( snap->request.title ) ) );
  for ( const auto &line : snap->logLines )
  {
    lines.append( QStringLiteral( "[%1] %2" )
                    .arg( logLevelPrefix( line.level ), QString::fromStdString( line.text ) ) );
  }
  if ( !snap->error.empty() )
    lines.append( QStringLiteral( "[ERROR] %1" ).arg( QString::fromStdString( snap->error ) ) );
  if ( lines.size() == 1 )
    lines.append( tr( "(暂无日志)" ) );

  m_logView->setPlainText( lines.join( QLatin1Char( '\n' ) ) );
  auto cursor = m_logView->textCursor();
  cursor.movePosition( QTextCursor::End );
  m_logView->setTextCursor( cursor );
}

void RsJobPanel::fillDetailsForJob( const QString &jobId )
{
  m_detailView->clear();
  auto snap = JobEngine::instance().snapshot( jobId.toStdString() );
  if ( !snap )
  {
    m_detailView->setPlainText( tr( "(任务不存在)" ) );
    return;
  }

  const JobRecord &rec = *snap;
  QStringList lines;
  lines << tr( "【基本信息】" );
  lines << tr( "任务 ID：%1" ).arg( jobId );
  lines << tr( "标题：%1" )
             .arg( rec.request.title.empty()
                     ? QString::fromStdString( rec.request.algorithmId )
                     : QString::fromStdString( rec.request.title ) );
  lines << tr( "方法 (algorithmId)：%1" )
             .arg( QString::fromStdString( rec.request.algorithmId ) );
  lines << tr( "来源：%1" ).arg( QString::fromStdString( rec.request.source ) );
  if ( !rec.request.clientTag.empty() )
    lines << tr( "客户端标记：%1" ).arg( QString::fromStdString( rec.request.clientTag ) );
  lines << tr( "独占执行：%1" ).arg( rec.request.exclusive ? tr( "是" ) : tr( "否" ) );
  lines << tr( "状态：%1" ).arg( stateToString( static_cast<int>( rec.state ) ) );
  lines << tr( "进度：%1" ).arg( formatProgress( rec.progress ) );
  if ( !rec.statusMessage.empty() )
    lines << tr( "状态消息：%1" ).arg( QString::fromStdString( rec.statusMessage ) );
  lines << tr( "成功后加载到主图：%1" )
             .arg( loadToMainPreference( jobId ) ? tr( "是" ) : tr( "否" ) );
  lines << tr( "创建时间 (ms)：%1" ).arg( rec.createdAtMs );
  if ( rec.startedAtMs > 0 )
    lines << tr( "开始时间 (ms)：%1" ).arg( rec.startedAtMs );
  if ( rec.finishedAtMs > 0 )
    lines << tr( "结束时间 (ms)：%1" ).arg( rec.finishedAtMs );
  if ( !rec.error.empty() )
    lines << tr( "错误：%1" ).arg( QString::fromStdString( rec.error ) );

  lines << QString();
  lines << tr( "【方法参数 params】" );
  if ( rec.request.params.isNull() || ( rec.request.params.isObject() && rec.request.params.empty() ) )
    lines << tr( "(无参数)" );
  else
    lines << prettyJsonValue( rec.request.params );

  // Highlight input-like keys from params
  lines << QString();
  lines << tr( "【输入 / 路径类参数】" );
  bool anyIn = false;
  if ( rec.request.params.isObject() )
  {
    for ( const auto &name : rec.request.params.getMemberNames() )
    {
      const QString qn = QString::fromStdString( name );
      if ( !looksLikePathKey( qn ) )
        continue;
      anyIn = true;
      const Json::Value &v = rec.request.params[name];
      if ( v.isString() )
        lines << QStringLiteral( "  %1 = %2" ).arg( qn, QString::fromStdString( v.asString() ) );
      else
        lines << QStringLiteral( "  %1 = %2" ).arg( qn, prettyJsonValue( v ).simplified() );
    }
  }
  if ( !anyIn )
    lines << tr( "  (未识别到路径类输入键)" );

  lines << QString();
  lines << tr( "【结果 / 输出 result】" );
  if ( rec.result.isNull() || ( rec.result.isObject() && rec.result.empty() ) )
    lines << tr( "(尚无结果)" );
  else
    lines << prettyJsonValue( rec.result );

  const QStringList outs = collectOutputPaths( jobId );
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

QStringList RsJobPanel::collectOutputPaths( const QString &jobId ) const
{
  QStringList out;
  auto snap = JobEngine::instance().snapshot( jobId.toStdString() );
  if ( !snap )
    return out;

  // Prefer result.output / known keys
  if ( snap->result.isObject() )
  {
    static const char *kKeys[] = {
      "output", "OUTPUT", "outputPath", "out", "dest", "destination",
      "OUTPUT_RASTER", "OUTPUT_VECTOR", "OUTPUT_LAYER"
    };
    for ( const char *k : kKeys )
    {
      if ( snap->result.isMember( k ) )
        collectStringPaths( snap->result[k], out );
    }
    // Also scan all result strings
    collectStringPaths( snap->result, out );
  }

  // Fallback: output-like params (some jobs only echo paths in params)
  if ( snap->request.params.isObject() )
  {
    for ( const auto &name : snap->request.params.getMemberNames() )
    {
      const QString qn = QString::fromStdString( name );
      if ( qn.contains( QLatin1String( "output" ), Qt::CaseInsensitive )
           || qn.contains( QLatin1String( "dest" ), Qt::CaseInsensitive )
           || qn.endsWith( QLatin1String( "out" ), Qt::CaseInsensitive ) )
      {
        collectStringPaths( snap->request.params[name], out );
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
    if ( preferVector )
      mw->loadVectorLayer( path );
    else
      mw->loadRasterLayer( path );
    ++loaded;
  }
  return loaded;
}

void RsJobPanel::tryAutoLoadOutputs( const QString &jobId )
{
  if ( !loadToMainPreference( jobId ) )
    return;
  auto snap = JobEngine::instance().snapshot( jobId.toStdString() );
  if ( !snap || snap->state != JobState::Succeeded )
    return;
  loadPathsToMain( collectOutputPaths( jobId ) );
}

QString RsJobPanel::selectedJobId() const
{
  const auto items = m_jobTree->selectedItems();
  if ( items.isEmpty() )
    return {};
  return items.first()->data( ColTitle, RoleJobId ).toString();
}

void RsJobPanel::updateActionEnabled()
{
  const QString id = selectedJobId();
  if ( id.isEmpty() )
  {
    m_cancelBtn->setEnabled( false );
    m_loadBtn->setEnabled( false );
    return;
  }
  const auto items = m_jobTree->selectedItems();
  const int state = items.isEmpty() ? -1 : items.first()->data( ColTitle, RoleState ).toInt();
  const bool cancellable = ( state == static_cast<int>( JobState::Queued )
                             || state == static_cast<int>( JobState::Running ) );
  m_cancelBtn->setEnabled( cancellable );

  const bool canLoad = ( state == static_cast<int>( JobState::Succeeded ) )
                       && !collectOutputPaths( id ).isEmpty();
  m_loadBtn->setEnabled( canLoad || state == static_cast<int>( JobState::Succeeded ) );
}

void RsJobPanel::onJobUpdated( const QString &jobId )
{
  upsertJobRow( jobId );
  if ( jobId == m_selectedId || jobId == selectedJobId() )
  {
    m_selectedId = jobId;
    fillLogForJob( jobId );
    fillDetailsForJob( jobId );
    updateActionEnabled();
  }
}

void RsJobPanel::onJobFinished( const QString &jobId )
{
  upsertJobRow( jobId );
  if ( jobId == selectedJobId() || jobId == m_selectedId )
  {
    fillLogForJob( jobId );
    fillDetailsForJob( jobId );
    updateActionEnabled();
  }
  tryAutoLoadOutputs( jobId );
}

void RsJobPanel::onSelectionChanged()
{
  m_selectedId = selectedJobId();
  if ( m_selectedId.isEmpty() )
  {
    m_detailView->clear();
    m_logView->clear();
    updateActionEnabled();
    return;
  }
  fillDetailsForJob( m_selectedId );
  fillLogForJob( m_selectedId );
  updateActionEnabled();
}

void RsJobPanel::onCancelClicked()
{
  const QString id = selectedJobId();
  if ( id.isEmpty() )
    return;
  JobEngine::instance().cancel( id.toStdString() );
}

void RsJobPanel::onClearFinishedClicked()
{
  for ( int i = m_jobTree->topLevelItemCount() - 1; i >= 0; --i )
  {
    QTreeWidgetItem *item = m_jobTree->topLevelItem( i );
    const int state = item->data( ColTitle, RoleState ).toInt();
    if ( state == static_cast<int>( JobState::Succeeded )
         || state == static_cast<int>( JobState::Failed )
         || state == static_cast<int>( JobState::Cancelled ) )
    {
      const QString id = item->data( ColTitle, RoleJobId ).toString();
      delete item;
      m_loadToMain.remove( id );
      if ( m_selectedId == id )
      {
        m_selectedId.clear();
        m_detailView->clear();
        m_logView->clear();
      }
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
  const QString id = item->data( ColTitle, RoleJobId ).toString();
  if ( id.isEmpty() )
    return;
  setLoadToMainPreference( id, item->checkState( ColLoad ) == Qt::Checked );
  if ( id == m_selectedId )
    fillDetailsForJob( id );
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
    tr( "任务中心汇总所有经 JobEngine 提交的算法任务。\n\n"
        "• 列表显示标题、状态、进度；「加载」列勾选后，任务成功时自动把输出加载到主图。\n"
        "• 右键任务：查看详情（方法/参数/输入输出）、停止、加载输出、复制信息。\n"
        "• 列表空白处右键：刷新、清空已完成、本说明。\n"
        "• 右侧「详情」页显示 algorithmId、params、result；「日志」页为任务绑定日志。" ) );
}

void RsJobPanel::onContextMenuRequested( const QPoint &pos )
{
  QTreeWidgetItem *item = m_jobTree->itemAt( pos );
  QMenu menu( this );

  if ( !item )
  {
    // Empty area — still offer useful actions
    menu.addAction( tr( "刷新列表" ), this, [this]() { refreshAll(); } );
    menu.addAction( tr( "清空已完成" ), this, &RsJobPanel::onClearFinishedClicked );
    menu.addSeparator();
    menu.addAction( tr( "任务中心说明…" ), this, &RsJobPanel::showAboutDialog );
    // Show engine stats
    const auto jobs = JobEngine::instance().list();
    int active = 0, done = 0;
    for ( const auto &j : jobs )
    {
      if ( j.state == JobState::Queued || j.state == JobState::Running )
        ++active;
      else
        ++done;
    }
    auto *info = menu.addAction(
      tr( "当前：%1 个活动 / %2 个已结束（引擎共 %3）" )
        .arg( active )
        .arg( done )
        .arg( static_cast<int>( jobs.size() ) ) );
    info->setEnabled( false );
    menu.exec( m_jobTree->viewport()->mapToGlobal( pos ) );
    return;
  }

  m_jobTree->setCurrentItem( item );
  const QString jobId = item->data( ColTitle, RoleJobId ).toString();
  const int state = item->data( ColTitle, RoleState ).toInt();
  const bool cancellable = ( state == static_cast<int>( JobState::Queued )
                             || state == static_cast<int>( JobState::Running ) );
  const bool succeeded = ( state == static_cast<int>( JobState::Succeeded ) );

  menu.addAction( tr( "查看详情" ), this, [this, jobId]() {
    if ( m_detailTabs )
      m_detailTabs->setCurrentWidget( m_detailView );
    fillDetailsForJob( jobId );
  } );
  menu.addAction( tr( "查看日志" ), this, [this, jobId]() {
    if ( m_detailTabs )
      m_detailTabs->setCurrentWidget( m_logView );
    fillLogForJob( jobId );
  } );
  menu.addSeparator();

  QAction *stopAct = menu.addAction( tr( "停止 / 取消" ), this, [jobId]() {
    JobEngine::instance().cancel( jobId.toStdString() );
  } );
  stopAct->setEnabled( cancellable );

  QAction *loadAct = menu.addAction( tr( "加载输出到主图" ), this, [this, jobId]() {
    const int n = loadPathsToMain( collectOutputPaths( jobId ) );
    if ( n <= 0 )
      QMessageBox::information( this, tr( "加载到主图" ),
                                tr( "未找到可加载的输出文件。" ) );
  } );
  loadAct->setEnabled( succeeded );

  QAction *autoLoad = menu.addAction( tr( "成功后加载到主图" ) );
  autoLoad->setCheckable( true );
  autoLoad->setChecked( loadToMainPreference( jobId ) );
  connect( autoLoad, &QAction::toggled, this, [this, jobId, item]( bool on ) {
    setLoadToMainPreference( jobId, on );
    m_blockItemChanged = true;
    item->setCheckState( ColLoad, on ? Qt::Checked : Qt::Unchecked );
    m_blockItemChanged = false;
    if ( jobId == m_selectedId )
      fillDetailsForJob( jobId );
  } );

  menu.addSeparator();
  menu.addAction( tr( "复制任务 ID" ), this, [this, jobId]() { copyText( jobId ); } );
  menu.addAction( tr( "复制方法 ID" ), this, [this, jobId]() {
    auto snap = JobEngine::instance().snapshot( jobId.toStdString() );
    if ( snap )
      copyText( QString::fromStdString( snap->request.algorithmId ) );
  } );
  menu.addAction( tr( "复制参数 JSON" ), this, [this, jobId]() {
    auto snap = JobEngine::instance().snapshot( jobId.toStdString() );
    if ( snap )
      copyText( prettyJsonValue( snap->request.params ) );
  } );
  menu.addAction( tr( "复制结果 JSON" ), this, [this, jobId]() {
    auto snap = JobEngine::instance().snapshot( jobId.toStdString() );
    if ( snap )
      copyText( prettyJsonValue( snap->result ) );
  } );
  menu.addAction( tr( "复制详情全文" ), this, [this, jobId]() {
    fillDetailsForJob( jobId );
    copyText( m_detailView->toPlainText() );
  } );

  menu.addSeparator();
  QAction *removeAct = menu.addAction( tr( "从列表移除" ), this, [this, item, jobId]() {
    delete item;
    m_loadToMain.remove( jobId );
    if ( m_selectedId == jobId )
    {
      m_selectedId.clear();
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
