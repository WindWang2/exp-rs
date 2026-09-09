/***************************************************************************
 * processing_history_panel.cpp — see processing_history_panel.h
 ***************************************************************************/
#include "processing_history_panel.h"

#include "processing_history_model.h"

#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"

#include "processing/framework/task_center.h"

#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace sicnu::app
{
namespace
{

QString collectOutputPaths( const sicnu::AlgorithmTaskInfo &info )
{
    // Same output-key vocabulary TaskCenter uses to LOCATE destinations:
    // values under output/result-like keys are destinations.
    QStringList paths;
    for ( auto it = info.parameterMap.constBegin(); it != info.parameterMap.constEnd(); ++it )
    {
        if ( !it.key().contains( QLatin1String( "output" ), Qt::CaseInsensitive ) &&
             !it.key().contains( QLatin1String( "result" ), Qt::CaseInsensitive ) )
            continue;
        if ( it.value().typeId() == QMetaType::QString )
        {
            const QString path = it.value().toString();
            if ( !path.isEmpty() )
                paths.append( path );
        }
    }
    if ( paths.isEmpty() && !info.outputLayerPath.isEmpty() )
        paths.append( info.outputLayerPath );
    return paths.join( QLatin1Char( '\n' ) );
}

} // namespace

ProcessingHistoryPanel::ProcessingHistoryPanel( QWidget *parent )
    : QgsDockWidget( parent )
{
    auto *central = new QWidget( this );
    auto *layout = new QVBoxLayout( central );
    layout->setContentsMargins( 4, 4, 4, 4 );

    // ── Filter row (incremental search, goal §H) ────────────────────────
    auto *filterRow = new QHBoxLayout;
    m_search = new QLineEdit( central );
    m_search->setObjectName( QStringLiteral( "rsHistorySearch" ) );
    m_search->setPlaceholderText( tr( "增量搜索：名称 / 来源 / 算法 / 任务 ID" ) );
    m_search->setClearButtonEnabled( true );
    m_stateFilter = new QComboBox( central );
    m_stateFilter->setObjectName( QStringLiteral( "rsHistoryStateFilter" ) );
    m_stateFilter->addItems( { tr( "全部" ), tr( "运行中" ), tr( "排队中" ), tr( "等待资源" ),
                               tr( "已完成" ), tr( "失败" ), tr( "已取消" ),
                               tr( "已中断" ) } );
    filterRow->addWidget( m_search, 1 );
    filterRow->addWidget( m_stateFilter );
    layout->addLayout( filterRow );

    // ── Table (virtual: the model caps + refilters, no widget per row) ──
    m_model = new ProcessingHistoryModel( this );
    m_view = new QTableView( central );
    m_view->setObjectName( QStringLiteral( "rsHistoryView" ) );
    m_view->setModel( m_model );
    m_view->setSelectionBehavior( QAbstractItemView::SelectRows );
    m_view->setSelectionMode( QAbstractItemView::ExtendedSelection );
    m_view->horizontalHeader()->setStretchLastSection( true );
    m_view->horizontalHeader()->resizeSection( ProcessingHistoryModel::Title, 220 );
    m_view->verticalHeader()->setDefaultSectionSize( 24 );
    m_view->setContextMenuPolicy( Qt::CustomContextMenu );
    m_view->setSortingEnabled( false ); // newest-first from the model; sorting stays stable
    layout->addWidget( m_view, 1 );

    // ── Action row ──────────────────────────────────────────────────────
    auto *actionRow = new QHBoxLayout;
    m_cancelBtn = new QPushButton( tr( "取消" ), central );
    m_rerunBtn = new QPushButton( tr( "重跑" ), central );
    m_openBtn = new QPushButton( tr( "打开产物" ), central );
    m_compareBtn = new QPushButton( tr( "对比产物" ), central );
    m_inspectBtn = new QPushButton( tr( "查看溯源" ), central );
    m_resumeBtn = new QPushButton( tr( "恢复运行" ), central );
    for ( QPushButton *btn : { m_cancelBtn, m_rerunBtn, m_openBtn, m_compareBtn, m_inspectBtn, m_resumeBtn } )
        actionRow->addWidget( btn );
    actionRow->addStretch( 1 );
    layout->addLayout( actionRow );

    m_statusLabel = new QLabel( central );
    m_statusLabel->setWordWrap( true );
    layout->addWidget( m_statusLabel );

    setWidget( central );

    // ── Wiring ──────────────────────────────────────────────────────────
    connect( m_search, &QLineEdit::textChanged, this, &ProcessingHistoryPanel::onFilterChanged );
    connect( m_stateFilter, &QComboBox::currentIndexChanged, this,
             &ProcessingHistoryPanel::onFilterChanged );
    connect( m_view, &QTableView::customContextMenuRequested, this,
             &ProcessingHistoryPanel::onContextMenu );
    connect( m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
             [this]( const QItemSelection &, const QItemSelection & ) { updateActionStates(); } );
    connect( m_view, &QTableView::doubleClicked, this,
             [this]( const QModelIndex & ) { openSelectedOutput(); } );

    connect( m_cancelBtn, &QPushButton::clicked, this, &ProcessingHistoryPanel::cancelSelected );
    connect( m_rerunBtn, &QPushButton::clicked, this, &ProcessingHistoryPanel::rerunSelected );
    connect( m_openBtn, &QPushButton::clicked, this, &ProcessingHistoryPanel::openSelectedOutput );
    connect( m_compareBtn, &QPushButton::clicked, this, &ProcessingHistoryPanel::compareSelectedOutputs );
    connect( m_inspectBtn, &QPushButton::clicked, this, &ProcessingHistoryPanel::inspectSelected );
    connect( m_resumeBtn, &QPushButton::clicked, this, &ProcessingHistoryPanel::resumeSelected );
    buildRowActions();

    m_taskCenter = &sicnu::TaskCenter::instance();
    m_coordinator = &sicnu::workflow::WorkflowRunCoordinator::instance();
    connect( m_taskCenter, &sicnu::TaskCenter::taskAdded, this,
             &ProcessingHistoryPanel::scheduleRefresh );
    connect( m_taskCenter, &sicnu::TaskCenter::taskUpdated, this,
             &ProcessingHistoryPanel::scheduleRefresh );
    connect( m_coordinator, &sicnu::workflow::WorkflowRunCoordinator::runStateChanged, this,
             &ProcessingHistoryPanel::scheduleRefresh );
    connect( m_model, &ProcessingHistoryModel::droppedCountChanged, this,
             [this]( long long dropped ) {
                 m_statusLabel->setText( tr( "历史超出显示上限，最早的 %1 条不再展示（执行记录不受影响）。" )
                                             .arg( dropped ) );
             } );

    refreshNow();
}

void ProcessingHistoryPanel::scheduleRefresh()
{
    // Coalesce bursts (progress ticks, step fan-out) into one re-query.
    if ( m_refreshScheduled )
        return;
    m_refreshScheduled = true;
    QTimer::singleShot( 250, this, [this] {
        m_refreshScheduled = false;
        refreshNow();
    } );
}

void ProcessingHistoryPanel::refreshNow()
{
    QVector<HistoryEntry> entries;

    if ( m_taskCenter )
    {
        const QList<sicnu::AlgorithmTaskInfo> tasks = m_taskCenter->allTasks();
        entries.reserve( entries.size() + tasks.size() );
        for ( const sicnu::AlgorithmTaskInfo &task : tasks )
        {
            HistoryEntry entry;
            entry.kind = HistoryEntry::Kind::Task;
            entry.taskId = task.taskId;
            entry.title = !task.algorithmName.isEmpty() ? task.algorithmName : task.algorithmId;
            entry.source = !task.source.isEmpty() ? task.source : tr( "gui" );
            entry.stateText = historyTaskStateText( task.status );
            entry.running = !historyTaskTerminal( task.status );
            entry.failed = task.status == sicnu::TaskStatus::Failed;
            entry.progress = task.progressPercentage;
            entry.started = task.startTime;
            entry.ended = task.endTime;
            const QString paths = collectOutputPaths( task );
            if ( !paths.isEmpty() )
                entry.outputPaths = paths.split( QLatin1Char( '\n' ), Qt::SkipEmptyParts );
            entry.params = task.parameterMap;
            entry.algorithmId = task.algorithmId;
            entries.append( entry );
        }
    }

    if ( m_coordinator )
    {
        const std::vector<std::shared_ptr<sicnu::workflow::WorkflowRun>> runs = m_coordinator->runs();
        entries.reserve( entries.size() + static_cast<int>( runs.size() ) );
        for ( const auto &run : runs )
        {
            if ( !run )
                continue;
            HistoryEntry entry;
            entry.kind = HistoryEntry::Kind::WorkflowRun;
            entry.runId = QString::fromStdString( run->runId() );
            entry.title = tr( "工作流 %1" ).arg( QString::fromStdString( run->workflowId() ) );
            entry.source = QStringLiteral( "workflow" );
            const sicnu::workflow::WorkflowRunState state = run->state();
            entry.stateText = historyRunStateText( state );
            entry.running = !sicnu::workflow::isTerminalRunState( state );
            entry.resumable = historyRunResumable( state );
            entry.failed = state == sicnu::workflow::WorkflowRunState::Failed;
            entry.progress = run->progress() * 100.0;
            const QString created = QString::fromStdString( run->createdAt() );
            entry.started = QDateTime::fromString( created, Qt::ISODate );
            QString errorMessage = QString::fromStdString( run->errorMessage() );
            if ( !errorMessage.isEmpty() )
                entry.stateText += QStringLiteral( " — %1" ).arg( errorMessage );
            // Workflow outputs live in the run's artifact map (name → path).
            for ( const auto &[name, value] : run->artifacts() )
            {
                Q_UNUSED( name );
                const QString path = QString::fromStdString( value );
                if ( QFileInfo::exists( path ) )
                    entry.outputPaths.append( path );
            }
            entries.append( entry );
        }
    }

    m_model->setEntries( entries );
    m_statusLabel->setText( tr( "共 %1 条（本会话投影；上限 %2 条，已截断 %3 条）。" )
                                .arg( m_model->rowCount() )
                                .arg( ProcessingHistoryModel::kMaxRows )
                                .arg( m_model->droppedCount() ) );
    updateActionStates();
}

void ProcessingHistoryPanel::onFilterChanged()
{
    const QString state = m_stateFilter->currentIndex() <= 0
                              ? QString()
                              : m_stateFilter->currentText();
    m_model->setStateFilter( state );
    m_model->setSearchText( m_search->text() );
}

HistoryEntry *ProcessingHistoryPanel::selectedEntry()
{
    // Multi-selection: actions apply to the current row only; compare uses
    // exactly two selected rows (checked by the caller).
    const QModelIndex current = m_view->currentIndex();
    if ( !current.isValid() )
        return nullptr;
    const HistoryEntry *entry = m_model->entryAtRow( current.row() );
    // const-cast is safe: the model owns and outlives the panel's actions.
    return const_cast<HistoryEntry *>( entry );
}

void ProcessingHistoryPanel::updateActionStates()
{
    const QModelIndexList rows = m_view->selectionModel()
                                     ? m_view->selectionModel()->selectedRows()
                                     : QModelIndexList();
    const HistoryEntry *entry = rows.isEmpty() ? nullptr : m_model->entryAtRow( rows.first().row() );
    m_cancelBtn->setEnabled( entry && entry->running && entry->kind == HistoryEntry::Kind::Task );
    m_rerunBtn->setEnabled( entry && entry->kind == HistoryEntry::Kind::Task &&
                            !entry->algorithmId.isEmpty() && !entry->running );
    m_openBtn->setEnabled( entry && entry->outputPaths.size() >= 1 );
    m_compareBtn->setEnabled( rows.size() == 2 );
    m_inspectBtn->setEnabled( entry && entry->outputPaths.size() >= 1 );
    m_resumeBtn->setEnabled( entry && entry->resumable );
}

void ProcessingHistoryPanel::onContextMenu( const QPoint &pos )
{
    QMenu menu( this );
    menu.addAction( tr( "刷新" ), this, &ProcessingHistoryPanel::refreshNow );
    menu.addSeparator();
    menu.addAction( tr( "打开产物" ), this, &ProcessingHistoryPanel::openSelectedOutput );
    menu.addAction( tr( "对比产物" ), this, &ProcessingHistoryPanel::compareSelectedOutputs );
    menu.addAction( tr( "查看溯源" ), this, &ProcessingHistoryPanel::inspectSelected );
    menu.addAction( tr( "重跑" ), this, &ProcessingHistoryPanel::rerunSelected );
    menu.addAction( tr( "取消" ), this, &ProcessingHistoryPanel::cancelSelected );
    menu.addAction( tr( "恢复运行" ), this, &ProcessingHistoryPanel::resumeSelected );
    menu.exec( m_view->viewport()->mapToGlobal( pos ) );
}

void ProcessingHistoryPanel::cancelSelected()
{
    HistoryEntry *entry = selectedEntry();
    if ( !entry || entry->kind != HistoryEntry::Kind::Task || !m_taskCenter )
        return;
    m_taskCenter->cancelTask( entry->taskId );
    refreshNow();
}

void ProcessingHistoryPanel::rerunSelected()
{
    HistoryEntry *entry = selectedEntry();
    if ( !entry || entry->kind != HistoryEntry::Kind::Task || entry->algorithmId.isEmpty() ||
         !m_taskCenter )
        return;
    // Same seam as the original submission (goal §C): a fresh task with the
    // same algorithm + parameter snapshot, GUI-tagged, no auto-load.
    m_taskCenter->enqueueTask( entry->algorithmId, entry->params,
                               /*autoLoad=*/false, sicnu::TaskPriority::Normal,
                               QList<long>(), /*autoDispatch=*/true, 0, QStringLiteral( "gui" ) );
    refreshNow();
}

void ProcessingHistoryPanel::openSelectedOutput()
{
    HistoryEntry *entry = selectedEntry();
    if ( !entry || entry->outputPaths.isEmpty() )
        return;
    emit resultOpenRequested( entry->outputPaths.first() );
}

void ProcessingHistoryPanel::compareSelectedOutputs()
{
    const QModelIndexList rows = m_view->selectionModel()->selectedRows();
    if ( rows.size() != 2 )
        return;
    const HistoryEntry *a = m_model->entryAtRow( rows[0].row() );
    const HistoryEntry *b = m_model->entryAtRow( rows[1].row() );
    if ( !a || !b || a->outputPaths.isEmpty() || b->outputPaths.isEmpty() )
        return;
    emit compareRequested( a->outputPaths.first(), b->outputPaths.first() );
}

void ProcessingHistoryPanel::inspectSelected()
{
    HistoryEntry *entry = selectedEntry();
    if ( !entry || entry->outputPaths.isEmpty() )
        return;
    emit inspectRequested( entry->outputPaths.first() );
}

void ProcessingHistoryPanel::resumeSelected()
{
    HistoryEntry *entry = selectedEntry();
    if ( !entry || !entry->resumable || entry->runId.isEmpty() )
        return;
    emit resumeRunRequested( entry->runId );
}

void ProcessingHistoryPanel::buildRowActions()
{
    // Row-level affordances live in the context menu + action row; nothing
    // else to build per row (goal §H: no widget-per-row).
}

} // namespace sicnu::app
