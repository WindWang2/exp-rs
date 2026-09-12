/***************************************************************************
 * rs_job_panel.cpp — Task Center projection: 详情 / 右键 / 加载到主图
 ***************************************************************************/
#include "rs_job_panel.h"

#include "design_tokens.h"
#include "widgets/rs_result_summary.h"

#include "main_window.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextCursor>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include "widgets/rs_empty_state_widget.h"

#include "dialogs/dialog_help_catalog.h"

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
constexpr int ColEta = 4;

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
         || status == sicnu::TaskStatus::WaitingResource
         || status == sicnu::TaskStatus::Dispatching
         || status == sicnu::TaskStatus::Running
         || status == sicnu::TaskStatus::Cancelling
         || status == sicnu::TaskStatus::Paused;
}

static bool isDarkTheme( const QWidget *w )
{
  // Single owner for theme detection (Milestone F): SicnuUi::Tokens.
  return SicnuUi::Tokens::themeIsDark( w );
}

QColor statusColor( sicnu::TaskStatus status, bool isDark )
{
  // Single owner for task-state colors: SicnuUi::Tokens (Milestone F).
  using sicnu::TaskStatus;
  switch ( status )
  {
    case TaskStatus::Running:          return SicnuUi::Tokens::statusRunning( isDark );
    case TaskStatus::Completed:        return SicnuUi::Tokens::statusOk( isDark );
    case TaskStatus::Failed:           return SicnuUi::Tokens::statusError( isDark );
    case TaskStatus::Paused:           return SicnuUi::Tokens::statusWarn( isDark );
    case TaskStatus::WaitingResource:  return SicnuUi::Tokens::statusWaiting( isDark );
    case TaskStatus::Dispatching:      return SicnuUi::Tokens::statusDispatching( isDark );
    case TaskStatus::Cancelling:       return SicnuUi::Tokens::statusCancelling( isDark );
    case TaskStatus::Queued:
    case TaskStatus::Canceled:         return SicnuUi::Tokens::statusIdle( isDark );
  }
  return SicnuUi::Tokens::statusIdle( isDark );
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
  : QgsDockWidget( tr( "Task Center" ), parent )
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
    tr( "Right-click a task for method/parameters/inputs/outputs, to stop it or load its results into the main view. Right-click empty space to refresh and view help." ),
    mainWidget );
  m_hintLabel->setObjectName( QStringLiteral( "rsJobPanelHint" ) );
  m_hintLabel->setWordWrap( true );
  mainLayout->addWidget( m_hintLabel );

  auto *toolbar = new QWidget( mainWidget );
  auto *toolbarLayout = new QHBoxLayout( toolbar );
  toolbarLayout->setContentsMargins( 0, 0, 0, 0 );
  toolbarLayout->setSpacing( 4 );

  m_filterCombo = new QComboBox( toolbar );
  m_filterCombo->setObjectName( QStringLiteral( "rsJobFilterCombo" ) );
  m_filterCombo->addItem( tr( "All" ), QStringLiteral( "all" ) );
  m_filterCombo->addItem( tr( "Running" ), QStringLiteral( "active" ) );
  m_filterCombo->addItem( tr( "Failed" ), QStringLiteral( "failed" ) );
  m_filterCombo->addItem( tr( "Finished" ), QStringLiteral( "finished" ) );
  toolbarLayout->addWidget( m_filterCombo );
  toolbarLayout->addStretch();

  m_cancelBtn = new QPushButton( tr( "Stop" ), toolbar );
  m_cancelBtn->setObjectName( QStringLiteral( "rsJobCancelBtn" ) );
  m_cancelBtn->setProperty( "danger", true );
  m_cancelBtn->setToolTip( tr( "Cancel Queued or Running Tasks" ) );
  m_cancelBtn->setEnabled( false );
  toolbarLayout->addWidget( m_cancelBtn );

  m_loadBtn = new QPushButton( tr( "Load to Main View" ), toolbar );
  m_loadBtn->setObjectName( QStringLiteral( "rsJobLoadBtn" ) );
  m_loadBtn->setProperty( "primary", true );
  m_loadBtn->setToolTip( tr( "Load the selected task's output paths into the main program layers" ) );
  m_loadBtn->setEnabled( false );
  toolbarLayout->addWidget( m_loadBtn );

  m_clearFinishedBtn = new QPushButton( tr( "Clear Finished" ), toolbar );
  m_clearFinishedBtn->setObjectName( QStringLiteral( "rsJobClearBtn" ) );
  m_clearFinishedBtn->setProperty( "ghost", true );
  toolbarLayout->addWidget( m_clearFinishedBtn );

  mainLayout->addWidget( toolbar );

  auto *splitter = new QSplitter( Qt::Horizontal, mainWidget );
  splitter->setObjectName( QStringLiteral( "rsJobSplitter" ) );

  m_treeStack = new QStackedWidget( splitter );
  m_treeStack->setObjectName( QStringLiteral( "rsJobTreeStack" ) );

  m_jobTree = new QTreeWidget( m_treeStack );
  m_jobTree->setObjectName( QStringLiteral( "rsJobTree" ) );
  m_jobTree->setColumnCount( 5 );
  m_jobTree->setHeaderLabels( { tr( "Title" ), tr( "Status" ), tr( "Progress" ), tr( "Load" ), tr( "Estimated Remaining" ) } );
  m_jobTree->setRootIsDecorated( false );
  m_jobTree->setSelectionMode( QAbstractItemView::SingleSelection );
  m_jobTree->setUniformRowHeights( true );
  m_jobTree->setContextMenuPolicy( Qt::CustomContextMenu );
  m_jobTree->setAlternatingRowColors( true );
  m_jobTree->header()->setStretchLastSection( false );
  m_jobTree->header()->setSectionResizeMode( ColTitle, QHeaderView::Stretch );
  m_jobTree->header()->setSectionResizeMode( ColState, QHeaderView::Interactive );
  m_jobTree->header()->setSectionResizeMode( ColProgress, QHeaderView::Interactive );
  m_jobTree->header()->setSectionResizeMode( ColLoad, QHeaderView::Interactive );
  m_jobTree->header()->setSectionResizeMode( ColEta, QHeaderView::Interactive );
  const QFontMetrics fm = m_jobTree->fontMetrics();
  m_jobTree->setColumnWidth( ColState, qMax( 76, fm.horizontalAdvance( tr( "Status" ) ) + 36 ) );
  m_jobTree->setColumnWidth( ColProgress, qMax( 84, fm.horizontalAdvance( QStringLiteral( "100.0%" ) ) + 36 ) );
  m_jobTree->setColumnWidth( ColLoad, qMax( 52, fm.horizontalAdvance( tr( "Load" ) ) + 24 ) );
  m_jobTree->setColumnWidth( ColEta, qMax( 80, fm.horizontalAdvance( QStringLiteral( "99h 59m 59s" ) ) + 24 ) );
  m_jobTree->headerItem()->setToolTip( ColEta, tr( "Estimated from elapsed time and current progress; unavailable at 0% or while paused" ) );
  m_jobTree->headerItem()->setToolTip( ColLoad, tr( "Tick: load outputs into the main program automatically after the task succeeds" ) );
  m_treeStack->addWidget( m_jobTree ); // Index 0: Tree

  m_emptyState = new sicnu::RsEmptyStateWidget(
      QStringLiteral( "check_outline" ),
      tr( "No tasks yet" ),
      tr( "All computation and algorithm tasks are finished or not yet submitted. Start new tasks in the Processing Toolbox or a workflow." ),
      QString(),
      m_treeStack );
  m_treeStack->addWidget( m_emptyState ); // Index 1: Empty State
  m_treeStack->setCurrentIndex( 1 ); // Initially empty

  splitter->addWidget( m_treeStack );

  m_detailTabs = new QTabWidget( splitter );
  m_detailTabs->setObjectName( QStringLiteral( "rsJobDetailTabs" ) );
  m_detailView = new QPlainTextEdit( m_detailTabs );
  m_detailView->setObjectName( QStringLiteral( "rsJobDetailView" ) );
  m_detailView->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
  m_detailView->setReadOnly( true );
  m_detailView->setPlaceholderText( tr( "Select a task to view method, parameters, inputs/outputs..." ) );
  m_detailTabs->addTab( m_detailView, tr( "Details" ) );

  // Structured result view (UX 4.0): operator result JSON rendered as
  // status/metrics/artifacts instead of raw JSON only.
  m_resultSummary = new RsResultSummary( m_detailTabs );
  m_detailTabs->addTab( m_resultSummary, tr( "Result" ) );
  connect( m_resultSummary, &RsResultSummary::openPathRequested,
           this, &RsJobPanel::resultOpenRequested );

  m_logView = new QPlainTextEdit( m_detailTabs );
  m_logView->setObjectName( QStringLiteral( "rsJobLogView" ) );
  m_logView->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
  m_logView->setReadOnly( true );
  m_logView->setPlaceholderText( tr( "Select a task to view its log..." ) );
  m_logView->setMaximumBlockCount( 20000 );
  m_detailTabs->addTab( m_logView, tr( "Log" ) );

  splitter->addWidget( m_detailTabs );
  splitter->setStretchFactor( 0, 1 );
  splitter->setStretchFactor( 1, 2 );
  mainLayout->addWidget( splitter, 1 );

  setWidget( mainWidget );

  connect( m_jobTree, &QTreeWidget::itemSelectionChanged, this, &RsJobPanel::onSelectionChanged );
  connect( m_jobTree, &QTreeWidget::customContextMenuRequested,
           this, &RsJobPanel::onContextMenuRequested );
  connect( m_jobTree, &QTreeWidget::itemChanged, this, &RsJobPanel::onItemChanged );
  connect( m_jobTree, &QTreeWidget::itemDoubleClicked, this, &RsJobPanel::onItemDoubleClicked );
  connect( m_cancelBtn, &QPushButton::clicked, this, &RsJobPanel::onCancelClicked );
  connect( m_loadBtn, &QPushButton::clicked, this, [this]() {
    const long id = selectedTaskId();
    if ( id < 0 )
      return;
    const int n = loadPathsToMain( collectOutputPaths( id ) );
    if ( n <= 0 )
      QMessageBox::information( this, tr( "Load to Main View" ),
                                tr( "No loadable output path found (or the file does not exist)." ) );
  } );
  connect( m_clearFinishedBtn, &QPushButton::clicked, this, &RsJobPanel::onClearFinishedClicked );
  connect( m_filterCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &RsJobPanel::onFilterChanged );

  applyHelpTips();
}

void RsJobPanel::applyHelpTips()
{
  // Dock-level (also powers Shift+F1 "What's This").
  setWhatsThis( SicnuDialogHelp::htmlForTool( QStringLiteral( "obia_task_list" ), windowTitle() ) );
  SicnuDialogHelp::tip( this, tr( "Task Center: aggregates title, status, progress and output loading for all background tasks." ) );

  SicnuDialogHelp::tip( m_filterCombo, tr( "Filters the task list by state: all / running / failed / finished." ) );
  SicnuDialogHelp::tip( m_cancelBtn, tr( "Cancels queued or running tasks (a confirmation pops up)." ) );
  SicnuDialogHelp::tip( m_loadBtn, tr( "Loads the selected task's output paths into the main program layers (successful tasks only)." ) );
  SicnuDialogHelp::tip( m_clearFinishedBtn, tr( "Clear all finished/failed/cancelled tasks from the list (a confirmation pops up)." ) );
  SicnuDialogHelp::tip( m_jobTree, tr( "Task list. Double-click for details; right-click for details/log, stop, pause/resume, retry, load output and copy info." ) );
  SicnuDialogHelp::tip( m_detailView, tr( "Task details: method ID, parameters, inputs/outputs and results." ) );
  SicnuDialogHelp::tip( m_logView, tr( "Task run log (read-only)." ) );
  SicnuDialogHelp::tip( m_hintLabel, tr( "Operation tips." ) );
}

QString RsJobPanel::statusToString( sicnu::TaskStatus status )
{
  switch ( status )
  {
    case sicnu::TaskStatus::Queued:
      return QObject::tr( "Queued" );
    case sicnu::TaskStatus::WaitingResource:
      return QObject::tr( "Waiting for Resources" );
    case sicnu::TaskStatus::Dispatching:
      return QObject::tr( "Scheduling" );
    case sicnu::TaskStatus::Running:
      return QObject::tr( "Running" );
    case sicnu::TaskStatus::Cancelling:
      return QObject::tr( "Cancelling" );
    case sicnu::TaskStatus::Paused:
      return QObject::tr( "Paused" );
    case sicnu::TaskStatus::Completed:
      return QObject::tr( "Succeeded" );
    case sicnu::TaskStatus::Failed:
      return QObject::tr( "Failed" );
    case sicnu::TaskStatus::Canceled:
      return QObject::tr( "Cancelled" );
  }
  return QObject::tr( "Unknown" );
}

QString RsJobPanel::formatProgress( double progress )
{
  if ( progress < 0.0 )
    return QStringLiteral( "…" );
  if ( progress > 1.0 )
    progress = 1.0;
  return QStringLiteral( "%1%" ).arg( static_cast<int>( progress * 100.0 + 0.5 ) );
}

QString RsJobPanel::formatEta( const sicnu::AlgorithmTaskInfo &info )
{
  // Only running tasks with measurable progress can be extrapolated.
  if ( info.status != sicnu::TaskStatus::Running )
    return QStringLiteral( "—" );
  if ( info.progressPercentage <= 0.0 || info.progressPercentage >= 1.0 )
    return QStringLiteral( "—" );
  if ( !info.startTime.isValid() )
    return QStringLiteral( "—" );

  const qint64 elapsedSecs = info.startTime.secsTo( QDateTime::currentDateTime() );
  if ( elapsedSecs <= 0 )
    return QStringLiteral( "—" );

  // eta = elapsed * (1 - progress) / progress
  const double remaining = 1.0 - info.progressPercentage;
  const qint64 etaSecs = static_cast<qint64>( elapsedSecs * remaining / info.progressPercentage );
  if ( etaSecs < 0 )
    return QStringLiteral( "—" );

  if ( etaSecs < 60 )
    return QStringLiteral( "%1s" ).arg( etaSecs );
  if ( etaSecs < 3600 )
    return QStringLiteral( "%1m%2s" ).arg( etaSecs / 60 ).arg( etaSecs % 60 );
  return QStringLiteral( "%1h%2m" ).arg( etaSecs / 3600 ).arg( ( etaSecs % 3600 ) / 60 );
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
    return stateText == tr( "Queued" ) || stateText == tr( "Waiting for Resources" ) || stateText == tr( "Running" ) || stateText == tr( "Cancelling" ) || stateText == tr( "Paused" );
  if ( key == QLatin1String( "failed" ) )
    return stateText == tr( "Failed" );
  if ( key == QLatin1String( "finished" ) )
    return stateText == tr( "Succeeded" ) || stateText == tr( "Failed" ) || stateText == tr( "Cancelled" );
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
  // Grouped view: pipeline children nest under their parent task row.
  QSet<long> parentIds;
  for ( const sicnu::AlgorithmTaskInfo &info : tasks )
  {
    for ( long parent : info.parentTaskIds )
      parentIds.insert( parent );
  }

  QTreeWidgetItem *selectItem = nullptr;
  const bool dark = isDarkTheme( this );
  QList<QTreeWidgetItem *> topRows;

  // Pass B: create parents before children (ascending age), then insert the
  // top rows newest-first below.
  std::sort( tasks.begin(), tasks.end(), []( const sicnu::AlgorithmTaskInfo &a,
                                             const sicnu::AlgorithmTaskInfo &b ) {
    return a.taskId < b.taskId;
  } );

  QHash<long, QTreeWidgetItem *> items;
  for ( const sicnu::AlgorithmTaskInfo &info : tasks )
  {
    const QString state = statusToString( info.status );
    if ( !passesFilter( state ) )
      continue;

    auto *item = new QTreeWidgetItem();
    item->setText( ColTitle, taskTitle( info ) );
    item->setText( ColState, state );
    item->setForeground( ColState, statusColor( info.status, dark ) );
    item->setText( ColProgress, formatProgress( info.progressPercentage ) );
    item->setText( ColEta, formatEta( info ) );
    item->setData( ColTitle, RoleTaskId, static_cast<qlonglong>( info.taskId ) );
    item->setData( ColTitle, RoleState, static_cast<int>( info.status ) );
    item->setFlags( item->flags() | Qt::ItemIsUserCheckable );
    item->setCheckState( ColLoad, loadToMainPreference( info.taskId ) ? Qt::Checked : Qt::Unchecked );
    item->setToolTip( ColTitle,
                      tr( "Task ID: %1\nMethod: %2\nRight-click for details / stop / load" )
                        .arg( info.taskId )
                        .arg( info.algorithmId ) );
    item->setToolTip( ColLoad, tr( "When ticked, outputs are loaded into the main program automatically on task success" ) );
    items.insert( info.taskId, item );

    QTreeWidgetItem *parentItem = nullptr;
    for ( long parent : info.parentTaskIds )
    {
      if ( items.contains( parent ) )
      {
        parentItem = items.value( parent );
        break;
      }
    }
    if ( parentItem )
    {
      parentItem->addChild( item );
    }
    else
    {
      topRows.prepend( item );
    }

    if ( info.taskId == keepId )
      selectItem = item;
  }

  for ( QTreeWidgetItem *row : topRows )
    m_jobTree->addTopLevelItem( row );

  // Expand grouped pipeline parents so children stay visible by default.
  for ( long parentId : parentIds )
  {
    if ( QTreeWidgetItem *parentItem = items.value( parentId ) )
      parentItem->setExpanded( true );
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
  if ( m_treeStack )
    m_treeStack->setCurrentIndex( m_jobTree->topLevelItemCount() > 0 ? 0 : 1 );
  updateActionEnabled();
}

QTreeWidgetItem *RsJobPanel::findTaskItem( long taskId ) const
{
  return findTaskItemRecursive( m_jobTree->invisibleRootItem(), taskId );
}

QTreeWidgetItem *RsJobPanel::findTaskItemRecursive( QTreeWidgetItem *parent, long taskId )
{
  for ( int i = 0; i < parent->childCount(); ++i )
  {
    QTreeWidgetItem *item = parent->child( i );
    if ( item && item->data( ColTitle, RoleTaskId ).toLongLong() == taskId )
      return item;
    if ( item )
    {
      if ( QTreeWidgetItem *nested = findTaskItemRecursive( item, taskId ) )
        return nested;
    }
  }
  return nullptr;
}

void RsJobPanel::upsertTaskRow( const sicnu::AlgorithmTaskInfo &info )
{
  const QString state = statusToString( info.status );
  const bool show = passesFilter( state );
  const long taskId = info.taskId;

  QTreeWidgetItem *found = findTaskItem( taskId );

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
    if ( m_treeStack )
      m_treeStack->setCurrentIndex( m_jobTree->topLevelItemCount() > 0 ? 0 : 1 );
    return;
  }

  m_blockItemChanged = true;
  if ( !found )
  {
    found = new QTreeWidgetItem();
    // Pipeline children nest under their parent when it is already visible;
    // otherwise the row lands top-level until the next full refresh.
    QTreeWidgetItem *parentRow = nullptr;
    for ( long parent : info.parentTaskIds )
    {
      parentRow = findTaskItem( parent );
      if ( parentRow )
        break;
    }
    if ( parentRow )
    {
      parentRow->insertChild( 0, found );
      parentRow->setExpanded( true );
    }
    else
    {
      m_jobTree->insertTopLevelItem( 0, found );
    }
    found->setFlags( found->flags() | Qt::ItemIsUserCheckable );
    found->setCheckState( ColLoad, loadToMainPreference( taskId ) ? Qt::Checked : Qt::Unchecked );
  }

  const bool dark = isDarkTheme( this );
  found->setText( ColTitle, taskTitle( info ) );
  found->setText( ColState, state );
  found->setForeground( ColState, statusColor( info.status, dark ) );
  found->setText( ColProgress, formatProgress( info.progressPercentage ) );
  found->setText( ColEta, formatEta( info ) );
  found->setData( ColTitle, RoleTaskId, static_cast<qlonglong>( taskId ) );
  found->setData( ColTitle, RoleState, static_cast<int>( info.status ) );
  found->setToolTip( ColTitle,
                     tr( "Task ID: %1\nMethod: %2\nRight-click for details / stop / load" )
                       .arg( taskId )
                       .arg( info.algorithmId ) );
  m_blockItemChanged = false;

  if ( m_treeStack )
    m_treeStack->setCurrentIndex( m_jobTree->topLevelItemCount() > 0 ? 0 : 1 );

  if ( m_selectedId < 0 && info.status == sicnu::TaskStatus::Running )
    m_jobTree->setCurrentItem( found );
}

void RsJobPanel::fillLogForTask( long taskId )
{
  const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
  if ( info.taskId != taskId )
  {
    m_logView->setPlainText( tr( "(task not found)" ) );
    m_logTaskId = -1;
    m_logLinesShown = 0;
    return;
  }

  // #704: progress ticks used to clear + re-set the WHOLE log text per tick.
  // Same task with only appended lines → append the new slice; a task switch
  // or a shrunk buffer (task cleared/recreated) falls back to a full refill.
  const int totalLines = info.logBuffer.size();
  if ( taskId == m_logTaskId && totalLines > m_logLinesShown && m_logLinesShown >= 0 )
  {
    QStringList added;
    for ( int i = m_logLinesShown; i < totalLines; ++i )
      added.append( info.logBuffer.at( i ) );
    if ( !info.errorMessage.isEmpty() )
      added.append( QStringLiteral( "[ERROR] %1" ).arg( info.errorMessage ) );
    if ( !added.isEmpty() )
    {
      m_logView->appendPlainText( added.join( QLatin1Char( '\n' ) ) );
      auto cursor = m_logView->textCursor();
      cursor.movePosition( QTextCursor::End );
      m_logView->setTextCursor( cursor );
    }
    m_logLinesShown = totalLines;
    return;
  }

  m_logView->clear();
  m_logTaskId = taskId;

  QStringList lines;
  lines.reserve( totalLines + 4 );
  lines.append( tr( "—— Task Log · %1 ——" ).arg( taskTitle( info ) ) );
  for ( const QString &line : info.logBuffer )
    lines.append( line );
  if ( !info.errorMessage.isEmpty() )
    lines.append( QStringLiteral( "[ERROR] %1" ).arg( info.errorMessage ) );
  if ( lines.size() == 1 )
    lines.append( tr( "(no log yet)" ) );
  m_logLinesShown = totalLines;

  m_logView->setPlainText( lines.join( QLatin1Char( '\n' ) ) );
  auto cursor = m_logView->textCursor();
  cursor.movePosition( QTextCursor::End );
  m_logView->setTextCursor( cursor );
}

void RsJobPanel::fillDetailsForTask( long taskId )
{
  // #704: the detail pane re-prints JSON and re-stats every output path; on
  // chatty progress ticks that is pure GUI-thread churn. Rebuild on status
  // change or at most every 500 ms; other call sites that need a guaranteed
  // refresh pass the task through updateDetailNow.
  const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
  const int statusInt = static_cast<int>( info.status );
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const bool statusChanged = statusInt != m_lastDetailStatus;
  if ( !statusChanged && taskId == m_selectedId
       && nowMs - m_lastDetailRebuildMs < 500 )
    return;
  m_lastDetailStatus = statusInt;
  m_lastDetailRebuildMs = nowMs;

  m_detailView->clear();
  if ( info.taskId != taskId )
  {
    m_detailView->setPlainText( tr( "(task not found)" ) );
    m_resultSummary->clear();
    return;
  }
  if ( m_resultSummary )
  {
    // Surface the operator result document (structured when it matches the
    // known shape; raw JSON remains one click away inside the widget).
    m_resultSummary->setContext( info.algorithmId );
    if ( !info.resultPayload.isNull() && info.resultPayload.isObject() )
      m_resultSummary->setResult( info.resultPayload );
    else
      m_resultSummary->clear();
  }

  QStringList lines;
  lines << tr( "[Basic information]" );
  lines << tr( "Task ID: %1" ).arg( taskId );
  lines << tr( "Title: %1" ).arg( taskTitle( info ) );
  lines << tr( "Method (algorithmId): %1" ).arg( info.algorithmId );
  if ( info.hasJobRequest && !info.jobRequest.source.empty() )
    lines << tr( "Source: %1" ).arg( QString::fromStdString( info.jobRequest.source ) );
  if ( info.hasJobRequest && !info.jobRequest.clientTag.empty() )
    lines << tr( "Client tag: %1" ).arg( QString::fromStdString( info.jobRequest.clientTag ) );
  if ( info.hasJobRequest )
    lines << tr( "Exclusive execution: %1" ).arg( info.jobRequest.exclusive ? tr( "Yes" ) : tr( "No" ) );
  if ( !info.jobId.empty() )
    lines << tr( "Internal jobId: %1" ).arg( QString::fromStdString( info.jobId ) );
  lines << tr( "Status: %1" ).arg( statusToString( info.status ) );
  lines << tr( "Progress: %1" ).arg( formatProgress( info.progressPercentage ) );
  lines << tr( "Loaded to main view on success: %1" )
             .arg( loadToMainPreference( taskId ) ? tr( "Yes" ) : tr( "No" ) );
  if ( info.startTime.isValid() )
    lines << tr( "Start time: %1" ).arg( info.startTime.toString( Qt::ISODate ) );
  if ( info.endTime.isValid() )
    lines << tr( "End time: %1" ).arg( info.endTime.toString( Qt::ISODate ) );
  if ( !info.errorMessage.isEmpty() )
    lines << tr( "Error: %1" ).arg( info.errorMessage );

  lines << QString();
  lines << tr( "[Method parameters (params)]" );
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
    lines << tr( "(no parameters)" );
  }

  lines << QString();
  lines << tr( "[Inputs / path-like parameters]" );
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
    lines << tr( "  (no path-like input parameters found)" );

  lines << QString();
  lines << tr( "[Result / output]" );
  if ( info.resultPayload.isNull()
       || ( info.resultPayload.isObject() && info.resultPayload.empty() ) )
  {
    if ( !info.outputLayerPath.isEmpty() )
      lines << QStringLiteral( "  output = %1" ).arg( info.outputLayerPath );
    else
      lines << tr( "(no results yet)" );
  }
  else
  {
    lines << prettyJsonValue( info.resultPayload );
  }

  const QStringList outs = collectOutputPaths( taskId );
  lines << QString();
  lines << tr( "[Loadable output paths]" );
  if ( outs.isEmpty() )
    lines << tr( "  (None)" );
  else
  {
    for ( const QString &p : outs )
    {
      const bool ok = QFileInfo::exists( p );
      lines << QStringLiteral( "  %1  %2" )
                 .arg( p, ok ? tr( "[present]" ) : tr( "[missing]" ) );
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
  m_lastDetailStatus = -1; // force one detail rebuild for the new selection
  m_logTaskId = -1;        // and a full log refill
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

bool RsJobPanel::confirmDangerous( const QString &title, const QString &body ) const
{
  // Safety-first: default button is No so a stray Enter/Space does not confirm.
  const auto choice = QMessageBox::question(
    const_cast<RsJobPanel *>( this ), title, body,
    QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
  return choice == QMessageBox::Yes;
}

void RsJobPanel::onItemDoubleClicked( QTreeWidgetItem *item, int /*column*/ )
{
  if ( !item )
    return;
  const long taskId = item->data( ColTitle, RoleTaskId ).toLongLong();
  if ( taskId < 0 )
    return;
  if ( m_detailTabs )
    m_detailTabs->setCurrentWidget( m_detailView );
  fillDetailsForTask( taskId );
}

void RsJobPanel::onCancelClicked()
{
  const long id = selectedTaskId();
  if ( id < 0 )
    return;
  const auto info = sicnu::TaskCenter::instance().getTaskInfo( id );
  const QString title = ( info.taskId == id ) ? taskTitle( info ) : QString::number( id );
  if ( !confirmDangerous( tr( "Cancel Task" ),
                          tr( "Cancel task '%1'?\nA running task will be aborted; intermediate results already produced are not rolled back." )
                            .arg( title ) ) )
    return;
  sicnu::TaskCenter::instance().cancelTask( id );
}

void RsJobPanel::onClearFinishedClicked()
{
  // Count terminal rows first so we can skip the dialog when there is nothing to clear.
  int terminalCount = 0;
  for ( int i = 0; i < m_jobTree->topLevelItemCount(); ++i )
  {
    const auto status = static_cast<sicnu::TaskStatus>(
      m_jobTree->topLevelItem( i )->data( ColTitle, RoleState ).toInt() );
    if ( isTerminalStatus( status ) )
      ++terminalCount;
  }
  if ( terminalCount == 0 )
    return;
  if ( !confirmDangerous( tr( "Clear Finished" ),
                          tr( "Clear %1 finished/failed/cancelled tasks from the list?\n(Output files on disk are not deleted.)" )
                            .arg( terminalCount ) ) )
    return;

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
    this, tr( "Task Center" ),
    tr( "The Task Center aggregates all algorithm tasks submitted through the Task Center (JobEngine is the internal execution adapter).\n\n"
        tr("• The list shows title, status and progress; when the 'Load' column is ticked, outputs are loaded into the main view automatically on task success.\n")
        tr("• Right-click a task: view details (method/parameters/inputs/outputs), stop, load outputs, copy info.\n")
        tr("• Right-click empty list space: refresh, clear finished, this help.\n")
        tr("• Cancellation, logs and final states are authoritative in the Task Center; this panel is a projection and holds no independent lifecycle state.") ) );
}

void RsJobPanel::onContextMenuRequested( const QPoint &pos )
{
  QTreeWidgetItem *item = m_jobTree->itemAt( pos );
  QMenu menu( this );

  if ( !item )
  {
    auto *refreshAct = menu.addAction( tr( "Refresh List" ), this, [this]() { refreshAll(); } );
    refreshAct->setToolTip( tr( "Re-fetches all tasks from the Task Center." ) );
    auto *clearAct = menu.addAction( tr( "Clear Finished..." ), this, &RsJobPanel::onClearFinishedClicked );
    clearAct->setToolTip( tr( "Clears all finished/failed/cancelled tasks (a confirmation pops up)." ) );
    menu.addSeparator();
    auto *aboutAct = menu.addAction( tr( "About the Task Center..." ), this, &RsJobPanel::showAboutDialog );
    aboutAct->setToolTip( tr( "View the Task Center feature description." ) );
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
      tr( "Currently: %1 active / %2 finished (Task Center total %3)" )
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
  const bool canRetry = ( status == sicnu::TaskStatus::Failed
                          || status == sicnu::TaskStatus::Canceled );

  auto *detailAct = menu.addAction( tr( "View Details" ), this, [this, taskId]() {
    if ( m_detailTabs )
      m_detailTabs->setCurrentWidget( m_detailView );
    fillDetailsForTask( taskId );
  } );
  detailAct->setToolTip( tr( "Shows method, parameters, inputs/outputs and results on the details page to the right." ) );
  auto *logAct = menu.addAction( tr( "View Log" ), this, [this, taskId]() {
    if ( m_detailTabs )
      m_detailTabs->setCurrentWidget( m_logView );
    fillLogForTask( taskId );
  } );
  logAct->setToolTip( tr( "Shows the run log on the Log page on the right." ) );
  menu.addSeparator();

  QAction *stopAct = menu.addAction( tr( "Stop / Cancel..." ), this, [this, taskId, item]() {
    const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    const QString title = ( info.taskId == taskId ) ? taskTitle( info ) : QString::number( taskId );
    if ( !confirmDangerous( tr( "Cancel Task" ),
                            tr( "Cancel task '%1'?\nA running task will be aborted; intermediate results already produced are not rolled back." )
                              .arg( title ) ) )
      return;
    sicnu::TaskCenter::instance().cancelTask( taskId );
  } );
  stopAct->setEnabled( cancellable );
  stopAct->setToolTip( tr( "Abort queued / running / paused tasks (a confirmation pops up)." ) );

  QAction *pauseAct = menu.addAction( tr( "Pause" ), this, [taskId]() {
    sicnu::TaskCenter::instance().pauseTask( taskId );
  } );
  pauseAct->setEnabled( status == sicnu::TaskStatus::Running );
  pauseAct->setToolTip( tr( "Pauses running tasks; they can be resumed later." ) );

  QAction *resumeAct = menu.addAction( tr( "Resume" ), this, [taskId]() {
    sicnu::TaskCenter::instance().resumeTask( taskId );
  } );
  resumeAct->setEnabled( status == sicnu::TaskStatus::Paused );
  resumeAct->setToolTip( tr( "Resumes paused tasks." ) );

  QAction *retryAct = menu.addAction( tr( "Retry..." ), this, [this, taskId]() {
    const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    const QString title = ( info.taskId == taskId ) ? taskTitle( info ) : QString::number( taskId );
    if ( !confirmDangerous( tr( "Retry Task" ),
                            tr( "Resubmit task '%1'?\nA new task will be created with the same parameters." )
                              .arg( title ) ) )
      return;
    sicnu::TaskCenter::instance().retryTask( taskId );
  } );
  retryAct->setEnabled( canRetry );
  retryAct->setToolTip( tr( "Resubmits failed/cancelled tasks with the same parameters (a confirmation pops up)." ) );

  QAction *loadAct = menu.addAction( tr( "Load Output to Main View" ), this, [this, taskId]() {
    const int n = loadPathsToMain( collectOutputPaths( taskId ) );
    if ( n <= 0 )
      QMessageBox::information( this, tr( "Load to Main View" ),
                                tr( "No loadable output file found." ) );
  } );
  loadAct->setEnabled( succeeded );
  loadAct->setToolTip( tr( "Loads task output paths into the main program layers (successful tasks only)." ) );

  QAction *autoLoad = menu.addAction( tr( "Load to Main View on Success" ) );
  autoLoad->setCheckable( true );
  autoLoad->setToolTip( tr( "When ticked, outputs are loaded into the main program automatically on task success." ) );
  autoLoad->setChecked( loadToMainPreference( taskId ) );
  connect( autoLoad, &QAction::toggled, this, [this, taskId]( bool on ) {
    setLoadToMainPreference( taskId, on );
    m_blockItemChanged = true;
    if ( auto *targetItem = findTaskItem( taskId ) )
      targetItem->setCheckState( ColLoad, on ? Qt::Checked : Qt::Unchecked );
    m_blockItemChanged = false;
    if ( taskId == m_selectedId )
      fillDetailsForTask( taskId );
  } );

  menu.addSeparator();
  auto *copyIdAct = menu.addAction( tr( "Copy Task ID" ), this, [this, taskId]() {
    copyText( QString::number( taskId ) );
  } );
  copyIdAct->setToolTip( tr( "Copies the task ID to the clipboard." ) );
  auto *copyMethodAct = menu.addAction( tr( "Copy Method ID" ), this, [this, taskId]() {
    const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( info.taskId == taskId )
      copyText( info.algorithmId );
  } );
  copyMethodAct->setToolTip( tr( "Copies the method algorithmId to the clipboard." ) );
  auto *copyParamsAct = menu.addAction( tr( "Copy Parameters JSON" ), this, [this, taskId]() {
    const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( info.taskId == taskId && info.hasJobRequest )
      copyText( prettyJsonValue( info.jobRequest.params ) );
  } );
  copyParamsAct->setToolTip( tr( "Copies the formatted parameters JSON to the clipboard." ) );
  auto *copyResultAct = menu.addAction( tr( "Copy Result JSON" ), this, [this, taskId]() {
    const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( info.taskId == taskId )
      copyText( prettyJsonValue( info.resultPayload ) );
  } );
  copyResultAct->setToolTip( tr( "Copies the formatted result JSON to the clipboard." ) );
  auto *copyDetailAct = menu.addAction( tr( "Copy Full Details" ), this, [this, taskId]() {
    fillDetailsForTask( taskId );
    copyText( m_detailView->toPlainText() );
  } );
  copyDetailAct->setToolTip( tr( "Copies the full details page to the clipboard." ) );

  menu.addSeparator();
  QAction *removeAct = menu.addAction( tr( "Remove from List" ), this, [this, taskId]() {
    if ( auto *targetItem = findTaskItem( taskId ) )
      delete targetItem;
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
  removeAct->setToolTip( tr( "Removes the row from this list only (Task Center records are kept; they reappear after refresh)." ) );

  menu.addSeparator();
  auto *refreshAct = menu.addAction( tr( "Refresh List" ), this, [this]() { refreshAll(); } );
  refreshAct->setToolTip( tr( "Re-fetches all tasks from the Task Center." ) );
  auto *aboutAct = menu.addAction( tr( "About the Task Center..." ), this, &RsJobPanel::showAboutDialog );
  aboutAct->setToolTip( tr( "View the Task Center feature description." ) );

  menu.exec( m_jobTree->viewport()->mapToGlobal( pos ) );
}
