/***************************************************************************
 * mission_timeline_panel.cpp — desktop surface over the mission task space
 ***************************************************************************/

#include "app/workbench/mission_timeline_panel.h"

#include "app/workbench/mission_projection.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

namespace sicnu::app
{

MissionTimelinePanel::MissionTimelinePanel( QWidget *parent )
    : QgsDockWidget( parent )
{
    setObjectName( QStringLiteral( "MissionTimelinePanel" ) );
    setWindowTitle( tr( "Mission Timeline" ) );

    auto *root = new QWidget( this );
    auto *layout = new QVBoxLayout( root );
    layout->setContentsMargins( 4, 4, 4, 4 );

    m_header = new QLabel( tr( "No mission" ), root );
    m_header->setTextInteractionFlags( Qt::TextSelectableByMouse );
    layout->addWidget( m_header );

    m_model = new MissionTimelineModel( root );
    m_table = new QTableView( root );
    m_table->setModel( m_model );
    m_table->setSelectionBehavior( QAbstractItemView::SelectRows );
    m_table->setSelectionMode( QAbstractItemView::SingleSelection );
    m_table->verticalHeader()->setVisible( false );
    m_table->horizontalHeader()->setStretchLastSection( true );
    m_table->horizontalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );
    layout->addWidget( m_table, 1 );

    auto *buttons = new QHBoxLayout();
    m_refreshButton = new QPushButton( tr( "Refresh" ), root );
    m_retryButton = new QPushButton( tr( "Retry" ), root );
    m_resumeButton = new QPushButton( tr( "Resume" ), root );
    m_retryButton->setEnabled( false );
    m_resumeButton->setEnabled( false );
    buttons->addWidget( m_refreshButton );
    buttons->addStretch( 1 );
    buttons->addWidget( m_retryButton );
    buttons->addWidget( m_resumeButton );
    layout->addLayout( buttons );

    setWidget( root );

    connect( m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
             &MissionTimelinePanel::onSelectionChanged );
    // The pushed selection must track the authority, not the click: a model
    // reset (project switch, corrupt-authority reset) silently clears the
    // table selection WITHOUT a selectionChanged signal, and an incremental
    // applyEvents changes a selected row's status behind the selection
    // model's back. Both would leave the SelectionContext snapshot holding a
    // stale task id/status that availability rules then project onto
    // commands. Re-derive and re-push on both.
    connect( m_model, &QAbstractItemModel::modelReset, this,
             &MissionTimelinePanel::repushSelection );
    connect( m_model, &QAbstractItemModel::dataChanged, this,
             &MissionTimelinePanel::repushSelection );
    connect( m_refreshButton, &QPushButton::clicked, this,
             &MissionTimelinePanel::onRefreshClicked );
    connect( m_retryButton, &QPushButton::clicked, this, &MissionTimelinePanel::onRetryClicked );
    connect( m_resumeButton, &QPushButton::clicked, this, &MissionTimelinePanel::onResumeClicked );
}

void MissionTimelinePanel::setTimeline( const MissionTimeline &timeline )
{
    m_model->setTimeline( timeline );
    updateActionStates();
}

void MissionTimelinePanel::applyEvents( const MissionTimeline &timeline, quint64 sinceSeq )
{
    m_model->applyEvents( timeline, sinceSeq );
    updateActionStates();
}

void MissionTimelinePanel::setMissionHeader( const QString &missionId, MissionStage stage,
                                             quint64 revision, quint64 lastEventSeq )
{
    m_header->setText( tr( "Mission %1 · stage %2 · revision %3 · events %4" )
                           .arg( missionId.isEmpty() ? tr( "(none)" ) : missionId.left( 8 ),
                                 QLatin1String( missionStageKey( stage ) ) )
                           .arg( revision )
                           .arg( lastEventSeq ) );
}

QString MissionTimelinePanel::selectedTaskId() const
{
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    if ( rows.isEmpty() )
        return {};
    // The model's own row projection — no task-vector copy per selection
    // change (a 4000-task mission would copy the whole vector twice).
    const QJsonObject projection = m_model->projectionAt( rows.first().row() );
    return projection.value( QStringLiteral( "id" ) ).toString();
}

void MissionTimelinePanel::onSelectionChanged()
{
    repushSelection();
}

void MissionTimelinePanel::repushSelection()
{
    const QString taskId = selectedTaskId();
    if ( taskId.isEmpty() )
    {
        pushSelection( {}, MissionTaskStatus::Pending );
        updateActionStates();
        return;
    }
    pushSelection( taskId, selectedTaskStatus( taskId ) );
    updateActionStates();
}

void MissionTimelinePanel::pushSelection( const QString &taskId, MissionTaskStatus status )
{
    // A batch applyEvents emits one dataChanged per touched row; the derived
    // selection is the same for all of them. Skip the identical re-announce
    // (SelectionContext dedupes anyway) but keep the bookkeeping honest.
    if ( taskId == m_pushedTaskId && status == m_pushedStatus )
        return;
    m_pushedTaskId = taskId;
    m_pushedStatus = status;
    emit taskSelected( taskId, status );
}

void MissionTimelinePanel::onRefreshClicked()
{
    emit refreshRequested();
}

void MissionTimelinePanel::onRetryClicked()
{
    const QString taskId = selectedTaskId();
    if ( !taskId.isEmpty() )
        emit retryRequested( taskId );
}

void MissionTimelinePanel::onResumeClicked()
{
    const QString taskId = selectedTaskId();
    if ( !taskId.isEmpty() )
        emit resumeRequested( taskId );
}

MissionTaskStatus MissionTimelinePanel::selectedTaskStatus( const QString &taskId ) const
{
    if ( taskId.isEmpty() )
        return MissionTaskStatus::Pending;
    const int row = m_model->rowOfTask( taskId );
    if ( row < 0 )
        return MissionTaskStatus::Pending;
    const std::optional<MissionTaskStatus> status = missionTaskStatusFromKey(
        m_model->projectionAt( row ).value( QStringLiteral( "status" ) ).toString() );
    return status.value_or( MissionTaskStatus::Pending );
}

void MissionTimelinePanel::updateActionStates()
{
    const MissionTaskStatus status = selectedTaskStatus( selectedTaskId() );
    m_retryButton->setEnabled( missionTaskStatusIsRetryable( status ) );
    // Resume applies to stale work (references must be re-bound) and to a
    // canceled task; a failed task retries.
    m_resumeButton->setEnabled( status == MissionTaskStatus::Stale
                                || status == MissionTaskStatus::Canceled );
}

} // namespace sicnu::app
