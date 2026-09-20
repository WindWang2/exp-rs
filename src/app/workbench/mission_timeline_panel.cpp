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
    const int row = rows.first().row();
    if ( row < 0 || row >= m_model->timeline().tasks().size() )
        return {};
    return m_model->timeline().tasks().at( row ).id;
}

void MissionTimelinePanel::onSelectionChanged()
{
    const QString taskId = selectedTaskId();
    if ( taskId.isEmpty() )
    {
        emit taskSelected( {}, MissionTaskStatus::Pending );
        updateActionStates();
        return;
    }
    const MissionTask *task = m_model->timeline().task( taskId );
    emit taskSelected( taskId, task ? task->status : MissionTaskStatus::Pending );
    updateActionStates();
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

void MissionTimelinePanel::updateActionStates()
{
    const MissionTask *task = nullptr;
    const QString taskId = selectedTaskId();
    if ( !taskId.isEmpty() )
        task = m_model->timeline().task( taskId );

    m_retryButton->setEnabled( task && missionTaskStatusIsRetryable( task->status ) );
    // Resume applies to stale work (references must be re-bound) and to a
    // canceled task; a failed task retries.
    m_resumeButton->setEnabled(
        task && ( task->status == MissionTaskStatus::Stale
                  || task->status == MissionTaskStatus::Canceled ) );
}

} // namespace sicnu::app
