/***************************************************************************
 * rs_job_panel.cpp
 ***************************************************************************/
#include "rs_job_panel.h"

#include "job_engine_qt_bridge.h"

#include "jobs/job_engine.h"
#include "jobs/job_types.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextCursor>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

using sicnu::jobs::JobEngine;
using sicnu::jobs::JobLogLevel;
using sicnu::jobs::JobRecord;
using sicnu::jobs::JobState;

namespace {

constexpr int RoleJobId = Qt::UserRole;
constexpr int RoleState = Qt::UserRole + 1;

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

} // namespace

RsJobPanel::RsJobPanel( QWidget *parent )
  : QgsDockWidget( tr( "任务" ), parent )
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

  // Toolbar
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

  m_cancelBtn = new QPushButton( tr( "取消" ), toolbar );
  m_cancelBtn->setEnabled( false );
  toolbarLayout->addWidget( m_cancelBtn );

  m_clearFinishedBtn = new QPushButton( tr( "清空已完成" ), toolbar );
  toolbarLayout->addWidget( m_clearFinishedBtn );

  mainLayout->addWidget( toolbar );

  // Split: job list | log
  auto *splitter = new QSplitter( Qt::Horizontal, mainWidget );

  m_jobTree = new QTreeWidget( splitter );
  m_jobTree->setColumnCount( 3 );
  m_jobTree->setHeaderLabels( { tr( "标题" ), tr( "状态" ), tr( "进度" ) } );
  m_jobTree->setRootIsDecorated( false );
  m_jobTree->setSelectionMode( QAbstractItemView::SingleSelection );
  m_jobTree->setUniformRowHeights( true );
  m_jobTree->header()->setStretchLastSection( false );
  m_jobTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
  m_jobTree->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
  m_jobTree->header()->setSectionResizeMode( 2, QHeaderView::ResizeToContents );
  splitter->addWidget( m_jobTree );

  m_logView = new QPlainTextEdit( splitter );
  m_logView->setReadOnly( true );
  m_logView->setPlaceholderText( tr( "选择任务以查看日志…" ) );
  m_logView->setMaximumBlockCount( 20000 );
  splitter->addWidget( m_logView );

  splitter->setStretchFactor( 0, 1 );
  splitter->setStretchFactor( 1, 2 );
  mainLayout->addWidget( splitter, 1 );

  setWidget( mainWidget );

  connect( m_jobTree, &QTreeWidget::itemSelectionChanged, this, &RsJobPanel::onSelectionChanged );
  connect( m_cancelBtn, &QPushButton::clicked, this, &RsJobPanel::onCancelClicked );
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

void RsJobPanel::refreshAll()
{
  const QString keepId = selectedJobId();
  m_jobTree->clear();

  const auto jobs = JobEngine::instance().list();
  // Newest first by createdAtMs
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
    item->setText( 0, title );
    item->setText( 1, state );
    item->setText( 2, formatProgress( rec.progress ) );
    item->setData( 0, RoleJobId, id );
    item->setData( 0, RoleState, static_cast<int>( rec.state ) );
    item->setToolTip( 0, QStringLiteral( "%1\n%2" )
                           .arg( id, QString::fromStdString( rec.request.algorithmId ) ) );
    if ( id == keepId )
      selectItem = item;
  }

  if ( selectItem )
  {
    m_jobTree->setCurrentItem( selectItem );
  }
  else if ( !m_selectedId.isEmpty() )
  {
    m_selectedId.clear();
    m_logView->clear();
    m_cancelBtn->setEnabled( false );
  }
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
    if ( item->data( 0, RoleJobId ).toString() == jobId )
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
      m_logView->clear();
      m_cancelBtn->setEnabled( false );
    }
    return;
  }

  if ( !found )
  {
    found = new QTreeWidgetItem();
    // Insert at top (newest first)
    m_jobTree->insertTopLevelItem( 0, found );
  }

  found->setText( 0, title );
  found->setText( 1, state );
  found->setText( 2, formatProgress( rec.progress ) );
  found->setData( 0, RoleJobId, jobId );
  found->setData( 0, RoleState, static_cast<int>( rec.state ) );
  found->setToolTip( 0, QStringLiteral( "%1\n%2" )
                          .arg( jobId, QString::fromStdString( rec.request.algorithmId ) ) );

  // Follow latest running job when nothing selected
  if ( m_selectedId.isEmpty() && rec.state == JobState::Running )
  {
    m_jobTree->setCurrentItem( found );
  }
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
  lines.reserve( static_cast<int>( snap->logLines.size() ) );
  for ( const auto &line : snap->logLines )
  {
    lines.append( QStringLiteral( "[%1] %2" )
                    .arg( logLevelPrefix( line.level ), QString::fromStdString( line.text ) ) );
  }
  if ( !snap->error.empty() )
    lines.append( QStringLiteral( "[ERROR] %1" ).arg( QString::fromStdString( snap->error ) ) );

  m_logView->setPlainText( lines.join( QLatin1Char( '\n' ) ) );
  // Scroll to end
  auto cursor = m_logView->textCursor();
  cursor.movePosition( QTextCursor::End );
  m_logView->setTextCursor( cursor );
}

QString RsJobPanel::selectedJobId() const
{
  const auto items = m_jobTree->selectedItems();
  if ( items.isEmpty() )
    return {};
  return items.first()->data( 0, RoleJobId ).toString();
}

void RsJobPanel::onJobUpdated( const QString &jobId )
{
  upsertJobRow( jobId );
  if ( jobId == m_selectedId || jobId == selectedJobId() )
  {
    m_selectedId = jobId;
    fillLogForJob( jobId );
    const int state = m_jobTree->currentItem()
                        ? m_jobTree->currentItem()->data( 0, RoleState ).toInt()
                        : -1;
    const bool cancellable = ( state == static_cast<int>( JobState::Queued )
                               || state == static_cast<int>( JobState::Running ) );
    m_cancelBtn->setEnabled( cancellable );
  }
}

void RsJobPanel::onJobFinished( const QString &jobId )
{
  // Row + log already refreshed via jobUpdated; ensure cancel disabled.
  if ( jobId == selectedJobId() )
  {
    m_cancelBtn->setEnabled( false );
    fillLogForJob( jobId );
  }
}

void RsJobPanel::onSelectionChanged()
{
  m_selectedId = selectedJobId();
  if ( m_selectedId.isEmpty() )
  {
    m_logView->clear();
    m_cancelBtn->setEnabled( false );
    return;
  }

  fillLogForJob( m_selectedId );

  const auto items = m_jobTree->selectedItems();
  const int state = items.isEmpty() ? -1 : items.first()->data( 0, RoleState ).toInt();
  const bool cancellable = ( state == static_cast<int>( JobState::Queued )
                             || state == static_cast<int>( JobState::Running ) );
  m_cancelBtn->setEnabled( cancellable );
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
  // UI-only clear: remove terminal rows from the tree (engine keeps history).
  for ( int i = m_jobTree->topLevelItemCount() - 1; i >= 0; --i )
  {
    QTreeWidgetItem *item = m_jobTree->topLevelItem( i );
    const int state = item->data( 0, RoleState ).toInt();
    if ( state == static_cast<int>( JobState::Succeeded )
         || state == static_cast<int>( JobState::Failed )
         || state == static_cast<int>( JobState::Cancelled ) )
    {
      const QString id = item->data( 0, RoleJobId ).toString();
      delete item;
      if ( m_selectedId == id )
      {
        m_selectedId.clear();
        m_logView->clear();
        m_cancelBtn->setEnabled( false );
      }
    }
  }
}

void RsJobPanel::onFilterChanged()
{
  refreshAll();
}
