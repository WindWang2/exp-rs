/***************************************************************************
 * processing_history_panel.cpp — see processing_history_panel.h
 ***************************************************************************/
#include "processing_history_panel.h"

#include "processing_history_model.h"

#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"

#include "processing/framework/task_center.h"
#include "data/derivation_record.h"

#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelection>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace sicnu::app
{
namespace
{

QString collectOutputPaths( const sicnu::AlgorithmTaskInfo &info )
{
    // The ONE output-key vocabulary shared with TaskCenter (#726): values
    // under output/result-like keys are destinations, never sources.
    QStringList paths;
    for ( auto it = info.parameterMap.constBegin(); it != info.parameterMap.constEnd(); ++it )
    {
        if ( !sicnu::data::isOutputVocabularyKey( it.key() ) )
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

QString artifactKindLabel( ProcessingHistoryPanel::ArtifactKind kind )
{
    using Kind = ProcessingHistoryPanel::ArtifactKind;
    switch ( kind )
    {
        case Kind::Raster:
            return QObject::tr( "raster" );
        case Kind::Report:
            return QObject::tr( "report / non-raster" );
        case Kind::Other:
            return QObject::tr( "unknown type" );
    }
    return QObject::tr( "unknown type" );
}

} // namespace

ProcessingHistoryPanel::ArtifactKind ProcessingHistoryPanel::artifactKind( const QString &path )
{
    // R30-T06 / F2 (DECISIONS D09): a UI-side eligibility heuristic, NOT a
    // new artifact authority. The shell comparison seam only loadRasterLayer()s
    // its two paths, so a raster/raster pair is the only comparable pair.
    // Unknown suffixes stay "Other" and remain openable — historical products
    // must not be silently disabled by a suffix guess.
    static const QStringList rasterSuffixes = {
        QStringLiteral( "tif" ), QStringLiteral( "tiff" ), QStringLiteral( "img" ),
        QStringLiteral( "vrt" ), QStringLiteral( "dat" ), QStringLiteral( "pix" ),
        QStringLiteral( "nc" ), QStringLiteral( "hdf" ), QStringLiteral( "h5" ),
        QStringLiteral( "png" ), QStringLiteral( "jpg" ), QStringLiteral( "jpeg" ),
        QStringLiteral( "bil" ), QStringLiteral( "bip" ), QStringLiteral( "bsq" ),
        QStringLiteral( "asc" ), QStringLiteral( "grd" ), QStringLiteral( "envi" ) };
    static const QStringList reportSuffixes = {
        QStringLiteral( "json" ), QStringLiteral( "pdf" ), QStringLiteral( "svg" ),
        QStringLiteral( "txt" ), QStringLiteral( "xml" ), QStringLiteral( "csv" ),
        QStringLiteral( "md" ), QStringLiteral( "log" ), QStringLiteral( "html" ),
        QStringLiteral( "htm" ), QStringLiteral( "gpkg" ), QStringLiteral( "shp" ),
        QStringLiteral( "geojson" ), QStringLiteral( "yml" ), QStringLiteral( "yaml" ) };
    const QString suffix = QFileInfo( path ).suffix().toLower();
    if ( suffix.isEmpty() )
        return ArtifactKind::Other;
    if ( rasterSuffixes.contains( suffix ) )
        return ArtifactKind::Raster;
    if ( reportSuffixes.contains( suffix ) )
        return ArtifactKind::Report;
    return ArtifactKind::Other;
}

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
    m_search->setPlaceholderText( tr( "Incremental search: name / source / algorithm / task ID" ) );
    m_search->setClearButtonEnabled( true );
    m_stateFilter = new QComboBox( central );
    m_stateFilter->setObjectName( QStringLiteral( "rsHistoryStateFilter" ) );
    m_stateFilter->addItems( { tr( "All" ), tr( "Running" ), tr( "Queued" ), tr( "Waiting for Resources" ),
                               tr( "Finished" ), tr( "Failed" ), tr( "Cancelled" ),
                               tr( "Interrupted" ) } );
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
    m_cancelBtn = new QPushButton( tr( "Cancel" ), central );
    m_rerunBtn = new QPushButton( tr( "Re-run" ), central );
    m_openBtn = new QPushButton( tr( "Open Artifacts" ), central );
    m_compareBtn = new QPushButton( tr( "Comparison Artifacts" ), central );
    m_inspectBtn = new QPushButton( tr( "View Provenance" ), central );
    m_resumeBtn = new QPushButton( tr( "Resume Run" ), central );
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
             [this]( const QItemSelection &, const QItemSelection & )
             {
                 updateActionStates();
                 // Workbench 10.0 unified selection: workflow-run rows feed the
                 // SelectionContext (selectedWorkflowRunIds); task rows clear it.
                 QStringList runIds;
                 const QModelIndexList rows = m_view->selectionModel()
                                                  ? m_view->selectionModel()->selectedRows()
                                                  : QModelIndexList();
                 for ( const QModelIndex &row : rows )
                 {
                     if ( const auto *entry = m_model->entryAtRow( row.row() );
                          entry && entry->kind == sicnu::app::HistoryEntry::Kind::WorkflowRun )
                         runIds.append( entry->runId );
                 }
                 emit workflowRunSelectionChanged( runIds );
             } );
    connect( m_view, &QTableView::doubleClicked, this,
             [this]( const QModelIndex & ) { openSelectedOutput(); } );

    connect( m_cancelBtn, &QPushButton::clicked, this, &ProcessingHistoryPanel::cancelSelected );
    connect( m_rerunBtn, &QPushButton::clicked, this, &ProcessingHistoryPanel::rerunSelected );
    connect( m_openBtn, &QPushButton::clicked, this, &ProcessingHistoryPanel::openSelectedOutput );
    connect( m_compareBtn, &QPushButton::clicked, this, &ProcessingHistoryPanel::compareSelectedOutputs );
    connect( m_inspectBtn, &QPushButton::clicked, this, &ProcessingHistoryPanel::inspectSelected );
    connect( m_resumeBtn, &QPushButton::clicked, this, &ProcessingHistoryPanel::resumeSelected );

    m_taskCenter = &sicnu::TaskCenter::instance();
    m_coordinator = &sicnu::workflow::WorkflowRunCoordinator::instance();
    connect( m_taskCenter, &sicnu::TaskCenter::taskAdded, this,
             &ProcessingHistoryPanel::scheduleRefresh );
    connect( m_taskCenter, &sicnu::TaskCenter::taskUpdated, this,
             &ProcessingHistoryPanel::scheduleRefresh );
    // Queued: same-thread run-state emissions deliver INSIDE the coordinator
    // mutex; the refresh must never run on that stack (ProjectContext does
    // the same for the same reason).
    connect( m_coordinator, &sicnu::workflow::WorkflowRunCoordinator::runStateChanged, this,
             &ProcessingHistoryPanel::scheduleRefresh, Qt::QueuedConnection );
    // Truncation accounting is rendered by refreshNow()'s own summary line
    // (single writer for the status label).

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
    // D2 (R29-T04): the model reset below drops the view's selection and
    // scroll on every coalesced refresh. Capture the AUTHORITATIVE
    // identities first and re-select them after the reset — a running task
    // ticking progress must not clear what the user selected.
    QStringList identityKeys;
    QString anchorKey;
    if ( m_view->selectionModel() )
    {
        const QModelIndexList selected = m_view->selectionModel()->selectedRows();
        const QModelIndex current = m_view->currentIndex();
        for ( const QModelIndex &row : selected )
        {
            if ( const HistoryEntry *entry = m_model->entryAtRow( row.row() ) )
            {
                const QString key = ProcessingHistoryModel::identityKey( *entry );
                identityKeys.append( key );
                if ( row.row() == current.row() ) // column-agnostic anchor
                    anchorKey = key;
            }
        }
    }
    const int verticalScrollValue =
        m_view->verticalScrollBar() ? m_view->verticalScrollBar()->value() : 0;

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
            entry.title = tr( "Workflow %1" ).arg( QString::fromStdString( run->workflowId() ) );
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
    m_statusLabel->setText( tr( "%1 rows in total (projected in this session; cap %2, truncated %3)." )
                                .arg( m_model->rowCount() )
                                .arg( ProcessingHistoryModel::kMaxRows )
                                .arg( m_model->droppedCount() ) );
    restoreSelection( identityKeys, anchorKey, verticalScrollValue );
    updateActionStates();
}

void ProcessingHistoryPanel::restoreSelection( const QStringList &identityKeys,
                                               const QString &anchorKey, int verticalScrollValue )
{
    if ( !identityKeys.isEmpty() && m_view->selectionModel() )
    {
        QItemSelection restored;
        QModelIndex first;
        QModelIndex anchor;
        for ( const QString &key : identityKeys )
        {
            const int row = m_model->rowForIdentity( key );
            if ( row < 0 )
                continue; // entry gone: identity dropped, never re-aimed
            const QModelIndex index = m_model->index( row, 0 );
            restored.select( index, m_model->index( row, ProcessingHistoryModel::ColumnCount - 1 ) );
            if ( key == anchorKey )
                anchor = index;
            if ( !first.isValid() )
                first = index;
        }
        if ( !restored.isEmpty() )
        {
            m_view->selectionModel()->select( restored,
                                              QItemSelectionModel::ClearAndSelect
                                                  | QItemSelectionModel::Rows );
            // Keyboard/current target: the row the user was on when it
            // survived, else the first surviving selected row. NoUpdate —
            // the view-level setCurrentIndex default (ClearAndSelect) would
            // collapse the restored multi-selection to the anchor alone.
            m_view->selectionModel()->setCurrentIndex( anchor.isValid() ? anchor : first,
                                                       QItemSelectionModel::NoUpdate );
        }
    }
    if ( m_view->verticalScrollBar() )
        m_view->verticalScrollBar()->setValue( verticalScrollValue );
}

void ProcessingHistoryPanel::onFilterChanged()
{
    const QString state = m_stateFilter->currentIndex() <= 0
                              ? QString()
                              : m_stateFilter->currentText();
    m_model->setStateFilter( state );
    m_model->setSearchText( m_search->text() );
}

QModelIndex ProcessingHistoryPanel::actionTargetRow() const
{
    const QModelIndexList rows =
        m_view->selectionModel() ? m_view->selectionModel()->selectedRows() : QModelIndexList();
    if ( rows.isEmpty() )
        return {};
    // The current row wins when it belongs to the selection (keyboard /
    // right-click context); otherwise the first selected row. This is the
    // single target rule for BOTH enable checks and actions.
    const QModelIndex current = m_view->currentIndex();
    if ( current.isValid() && current.row() >= 0 )
        for ( const QModelIndex &row : rows )
            if ( row.row() == current.row() )
                return row;
    return rows.first();
}

HistoryEntry *ProcessingHistoryPanel::selectedEntry()
{
    // D2 (R29-T04): the action target is the row updateActionStates() gates
    // on — the old pair (enable: first selected row, act: currentIndex)
    // could enable a button against task A while firing it at task B.
    const QModelIndex row = actionTargetRow();
    if ( !row.isValid() )
        return nullptr;
    const HistoryEntry *entry = m_model->entryAtRow( row.row() );
    // const-cast is safe: the model owns and outlives the panel's actions.
    return const_cast<HistoryEntry *>( entry );
}

ProcessingHistoryPanel::ActionPlan ProcessingHistoryPanel::actionPlan() const
{
    // R30-T06 / WP04-05: ONE availability + rejection-reason truth. The
    // buttons, the context menu, keyboard/double-click and every slot's
    // trigger-time re-check all read this, so an entry point can never be
    // enabled against a different target than the one it acts on.
    ActionPlan plan;
    const QModelIndexList rows =
        m_view->selectionModel() ? m_view->selectionModel()->selectedRows() : QModelIndexList();
    const QModelIndex target = actionTargetRow();
    const HistoryEntry *entry =
        target.isValid() ? m_model->entryAtRow( target.row() ) : nullptr;

    if ( !m_taskCenter )
        plan.cancelReason = tr( "The task center is unavailable." );
    else if ( !entry )
        plan.cancelReason = tr( "Select a history row first." );
    else if ( entry->kind != HistoryEntry::Kind::Task )
        plan.cancelReason = tr( "Only tasks can be cancelled." );
    else if ( !entry->running )
        plan.cancelReason = tr( "This task is already in a terminal state." );
    else
        plan.cancel = true;

    if ( !m_taskCenter )
        plan.rerunReason = tr( "The task center is unavailable." );
    else if ( !entry || entry->kind != HistoryEntry::Kind::Task )
        plan.rerunReason = tr( "Only finished tasks can be re-run." );
    else if ( entry->algorithmId.isEmpty() )
        plan.rerunReason = tr( "This row has no algorithm id to re-submit." );
    else if ( entry->running )
        plan.rerunReason = tr( "A running task cannot be re-run (cancel it first)." );
    else
        plan.rerun = true;

    bool anyOpenable = false;
    if ( entry )
        for ( const OutputArtifactChoice &choice : outputChoicesFor( *entry ) )
            anyOpenable = anyOpenable || ( choice.exists && choice.supported );
    plan.open = plan.inspect = anyOpenable;
    if ( !anyOpenable )
    {
        plan.openReason = tr( "No existing artifact is available for this row." );
        plan.inspectReason = plan.openReason;
    }

    if ( rows.size() != 2 )
    {
        plan.compareReason = tr( "Select exactly two history rows to compare." );
    }
    else
    {
        const HistoryEntry *a = m_model->entryAtRow( rows[0].row() );
        const HistoryEntry *b = m_model->entryAtRow( rows[1].row() );
        const QVector<OutputArtifactChoice> choicesA = a ? outputChoicesFor( *a ) : QVector<OutputArtifactChoice>();
        const QVector<OutputArtifactChoice> choicesB = b ? outputChoicesFor( *b ) : QVector<OutputArtifactChoice>();
        bool aHas = false;
        bool bHas = false;
        for ( const OutputArtifactChoice &choice : choicesA )
            aHas = aHas || ( choice.exists && choice.supported );
        for ( const OutputArtifactChoice &choice : choicesB )
            bHas = bHas || ( choice.exists && choice.supported );
        if ( !aHas || !bHas )
        {
            plan.compareReason = tr( "One side has no existing artifact; comparison needs a real product from each row." );
        }
        else
        {
            bool comparablePair = false;
            for ( const OutputArtifactChoice &left : choicesA )
            {
                for ( const OutputArtifactChoice &right : choicesB )
                {
                    if ( left.exists && left.supported && right.exists && right.supported &&
                         left.kind == ArtifactKind::Raster && right.kind == ArtifactKind::Raster )
                        comparablePair = true;
                }
            }
            if ( !comparablePair )
                plan.compareReason = tr( "Comparison loads raster layers: both rows must hold a raster product (incompatible or unknown types are refused)." );
            else
                plan.compare = true;
        }
    }

    if ( !entry || !entry->resumable || entry->runId.isEmpty() )
        plan.resumeReason = tr( "Only an interrupted workflow run can be resumed." );
    else
        plan.resume = true;

    return plan;
}

void ProcessingHistoryPanel::updateActionStates()
{
    const ActionPlan plan = actionPlan();
    const auto apply = []( QPushButton *button, bool enabled, const QString &reason ) {
        if ( !button )
            return;
        if ( !button->property( "baseToolTip" ).isValid() )
            button->setProperty( "baseToolTip", button->toolTip() );
        const QString base = button->property( "baseToolTip" ).toString();
        button->setEnabled( enabled );
        button->setToolTip( !enabled && !reason.isEmpty()
                              ? ( base.isEmpty() ? reason : base + QStringLiteral( "\n" ) + reason )
                              : base );
    };
    apply( m_cancelBtn, plan.cancel, plan.cancelReason );
    apply( m_rerunBtn, plan.rerun, plan.rerunReason );
    apply( m_openBtn, plan.open, plan.openReason );
    apply( m_compareBtn, plan.compare, plan.compareReason );
    apply( m_inspectBtn, plan.inspect, plan.inspectReason );
    apply( m_resumeBtn, plan.resume, plan.resumeReason );
}

void ProcessingHistoryPanel::buildContextMenuActions( QMenu *menu )
{
    if ( !menu )
        return;
    const ActionPlan plan = actionPlan();
    const auto add = [this, menu]( const QString &text, bool enabled, const QString &reason,
                                   void ( ProcessingHistoryPanel::*slot )() ) {
        QAction *action = menu->addAction( text );
        action->setEnabled( enabled );
        if ( !enabled && !reason.isEmpty() )
            action->setToolTip( reason );
        connect( action, &QAction::triggered, this, slot );
    };
    QAction *refreshAction = menu->addAction( tr( "Refresh" ), this, &ProcessingHistoryPanel::refreshNow );
    refreshAction->setToolTip( tr( "Re-query the task center and workflow coordinator." ) );
    menu->addSeparator();
    add( tr( "Open Artifacts" ), plan.open, plan.openReason,
         &ProcessingHistoryPanel::openSelectedOutput );
    add( tr( "Comparison Artifacts" ), plan.compare, plan.compareReason,
         &ProcessingHistoryPanel::compareSelectedOutputs );
    add( tr( "View Provenance" ), plan.inspect, plan.inspectReason,
         &ProcessingHistoryPanel::inspectSelected );
    add( tr( "Re-run" ), plan.rerun, plan.rerunReason,
         &ProcessingHistoryPanel::rerunSelected );
    add( tr( "Cancel" ), plan.cancel, plan.cancelReason,
         &ProcessingHistoryPanel::cancelSelected );
    add( tr( "Resume Run" ), plan.resume, plan.resumeReason,
         &ProcessingHistoryPanel::resumeSelected );
}

void ProcessingHistoryPanel::onContextMenu( const QPoint &pos )
{
    QMenu menu( this );
    buildContextMenuActions( &menu );
    menu.exec( m_view->viewport()->mapToGlobal( pos ) );
}

void ProcessingHistoryPanel::cancelSelected()
{
    // Trigger-time re-check: the row's state may have changed while a menu
    // was open or a keyboard shortcut was queued.
    const ActionPlan plan = actionPlan();
    if ( !plan.cancel )
    {
        m_statusLabel->setText( plan.cancelReason );
        return;
    }
    HistoryEntry *entry = selectedEntry();
    if ( !entry || !m_taskCenter )
        return;
    m_taskCenter->cancelTask( entry->taskId );
    refreshNow();
}

QVector<ProcessingHistoryPanel::OutputArtifactChoice>
ProcessingHistoryPanel::outputChoicesFor( const HistoryEntry &entry )
{
    QVector<OutputArtifactChoice> choices;
    choices.reserve( entry.outputPaths.size() );
    for ( const QString &path : entry.outputPaths )
    {
        OutputArtifactChoice choice;
        choice.path = path;
        const QFileInfo info( path );
        choice.exists = !path.isEmpty() && info.exists();
        choice.kind = artifactKind( path );
        choice.sourceDir = info.absolutePath();
        // A vanished path or a directory is shown for context but never
        // enabled. Unknown suffixes deliberately keep supported=true
        // (DECISIONS D09/D11): `kind` is only a label / comparison hint, NOT
        // an openability authority, so a legitimate historical product is
        // never disabled by a suffix guess.
        choice.supported = choice.exists && info.isFile();
        choice.label = ( choice.exists ? QString()
                                       : QObject::tr( "(missing) " ) )
                       + info.fileName()
                       + QStringLiteral( " [%1]" ).arg( artifactKindLabel( choice.kind ) );
        choices.append( choice );
    }
    return choices;
}

QString ProcessingHistoryPanel::pickOutputArtifact( const HistoryEntry &entry )
{
    const QVector<OutputArtifactChoice> choices = outputChoicesFor( entry );
    if ( entry.outputPaths.size() <= 1 )
        return entry.outputPaths.isEmpty() ? QString() : entry.outputPaths.first();
    // Injection seam (same pattern as ActiveViewHost::ConfirmationFn): tests
    // pick a specific artifact deterministically without a modal QMenu.
    if ( m_artifactPickFn )
    {
        const QString injected = m_artifactPickFn( choices );
        // Honour the same eligibility as the real menu: an injected pick of a
        // vanished / unsupported artifact is still a refusal.
        for ( const OutputArtifactChoice &choice : choices )
            if ( choice.path == injected && choice.exists && choice.supported )
                return injected;
        return QString();
    }
    // Multi-artifact rows pick EXPLICITLY (existing menu style): the old
    // "always the first output" opened artifact 0 while the user was looking
    // at a row whose main product was artifact 2. Vanished artifacts stay
    // visible but disabled.
    QMenu menu( this );
    QVector<QPair<QAction *, QString>> picks;
    for ( const OutputArtifactChoice &choice : choices )
    {
        QAction *action = menu.addAction( choice.label );
        action->setEnabled( choice.exists && choice.supported );
        picks.append( { action, choice.path } );
    }
    // Anchored on the view's current row (keyboard invocations would
    // otherwise open the menu at wherever the mouse happens to be parked).
    // Anchored on the ACTION TARGET's row (Ctrl+arrow can park the current
    // row outside the selection — the artifacts listed belong to the
    // target, and the menu must open over that row).
    const QModelIndex anchor = actionTargetRow();
    QAction *chosen = menu.exec( m_view->viewport()->mapToGlobal(
      m_view->visualRect( anchor.isValid() ? anchor : m_view->currentIndex() ).center() ) );
    for ( const auto &pick : picks )
        if ( pick.first == chosen )
            return pick.second;
    return QString();
}

void ProcessingHistoryPanel::rerunSelected()
{
    // Trigger-time re-check (WP05): a running/terminal transition between
    // enable and click must not silently re-submit the wrong target.
    const ActionPlan plan = actionPlan();
    if ( !plan.rerun )
    {
        m_statusLabel->setText( plan.rerunReason );
        return;
    }
    HistoryEntry *entry = selectedEntry();
    if ( !entry || !m_taskCenter )
        return;
    // Snapshot the identity BEFORE refreshNow() resets the model.
    const long originalId = entry->taskId;
    const QString algorithmId = entry->algorithmId;
    const QVariantMap params = entry->params;

    // The documented retry seam first (#685/#702: parent/pipeline-aware);
    // fall back to a plain re-submission with the same parameter snapshot
    // when the original task is no longer retryable. Only a VALID new task
    // id is ever reported as queued (WP05).
    QString statusMessage;
    const long newTaskId = m_taskCenter->retryTask( originalId );
    if ( newTaskId > 0 )
    {
        statusMessage =
            tr( "Task %1 retry queued as new task %2 (keeps pipeline parents)." )
                .arg( originalId )
                .arg( newTaskId );
    }
    else
    {
        const long resubmittedId = m_taskCenter->enqueueTask(
            algorithmId, params,
            /*autoLoad=*/false, sicnu::TaskPriority::Normal,
            QList<long>(), /*autoDispatch=*/true, 0,
            QStringLiteral( "gui" ) );
        if ( resubmittedId > 0 )
        {
            statusMessage =
                tr( "Task %1 no longer retryable — resubmitted as new task %2 (fresh task, no pipeline parents)." )
                    .arg( originalId )
                    .arg( resubmittedId );
        }
        else
        {
            // No typed admission diagnostic is exposed to this panel; the
            // authoritative fact is "no new task id was returned". Never
            // render a failed submission as a queued task.
            statusMessage =
                tr( "Task %1 could not be re-submitted: the task center did not accept a new task. No task was queued." )
                    .arg( originalId );
        }
    }
    // refreshNow writes its own summary line — the retry outcome lands
    // AFTER it so the user actually reads it.
    refreshNow();
    m_statusLabel->setText( statusMessage );
}

void ProcessingHistoryPanel::openSelectedOutput()
{
    const ActionPlan plan = actionPlan();
    if ( !plan.open )
    {
        m_statusLabel->setText( plan.openReason );
        return;
    }
    HistoryEntry *entry = selectedEntry();
    if ( !entry )
        return;
    const QString path = pickOutputArtifact( *entry );
    if ( !path.isEmpty() )
        emit resultOpenRequested( path );
}

void ProcessingHistoryPanel::compareSelectedOutputs()
{
    // Trigger-time re-check (WP04): the two rows and their products may have
    // changed since the menu/button was enabled.
    const ActionPlan plan = actionPlan();
    if ( !plan.compare )
    {
        m_statusLabel->setText( plan.compareReason );
        return;
    }
    const QModelIndexList rows = m_view->selectionModel()->selectedRows();
    if ( rows.size() != 2 )
        return;
    const HistoryEntry *a = m_model->entryAtRow( rows[0].row() );
    const HistoryEntry *b = m_model->entryAtRow( rows[1].row() );
    if ( !a || !b )
        return;
    // R1 (P2-2): the plan promises "a comparable raster pair", so the picker
    // must offer ONLY comparable candidates — otherwise an enabled Compare
    // button could still be refused AFTER the user picked a report product.
    // Reuse the existing picker on filtered copies; do not fork a picker.
    HistoryEntry comparableA = *a;
    comparableA.outputPaths.clear();
    for ( const OutputArtifactChoice &choice : outputChoicesFor( *a ) )
        if ( choice.exists && choice.supported && choice.kind == ArtifactKind::Raster )
            comparableA.outputPaths.append( choice.path );
    HistoryEntry comparableB = *b;
    comparableB.outputPaths.clear();
    for ( const OutputArtifactChoice &choice : outputChoicesFor( *b ) )
        if ( choice.exists && choice.supported && choice.kind == ArtifactKind::Raster )
            comparableB.outputPaths.append( choice.path );
    if ( comparableA.outputPaths.isEmpty() || comparableB.outputPaths.isEmpty() )
    {
        m_statusLabel->setText(
            tr( "Comparison cancelled: no comparable raster artifact remains on one side." ) );
        return;
    }
    // Each side picks ONE REAL artifact explicitly — never index 0 of either row.
    const QString pathA = pickOutputArtifact( comparableA );
    const QString pathB = pickOutputArtifact( comparableB );
    if ( pathA.isEmpty() || pathB.isEmpty() )
    {
        m_statusLabel->setText( tr( "Comparison cancelled: an artifact was not selected on one side." ) );
        return;
    }
    const QFileInfo infoA( pathA );
    const QFileInfo infoB( pathB );
    if ( !infoA.exists() || !infoA.isFile() || !infoB.exists() || !infoB.isFile() )
    {
        m_statusLabel->setText( tr( "Comparison cancelled: one of the selected artifacts is missing." ) );
        return;
    }
    if ( artifactKind( pathA ) != ArtifactKind::Raster ||
         artifactKind( pathB ) != ArtifactKind::Raster )
    {
        m_statusLabel->setText(
            tr( "Comparison cancelled: the comparison view loads raster layers, so both selected artifacts must be raster products." ) );
        return;
    }
    // Neutral "requested" wording: the shell still has to load both raster
    // layers and open a modal comparison dialog, which can fail. Declaring
    // "Comparing X with Y" here would read as a success the panel cannot
    // prove (R1 / S2). Set it BEFORE the synchronous emit.
    m_statusLabel->setText(
        tr( "Comparison requested: %1 / %2 (see the comparison window)." )
            .arg( infoA.fileName(), infoB.fileName() ) );
    emit compareRequested( pathA, pathB );
}

void ProcessingHistoryPanel::inspectSelected()
{
    const ActionPlan plan = actionPlan();
    if ( !plan.inspect )
    {
        m_statusLabel->setText( plan.inspectReason );
        return;
    }
    HistoryEntry *entry = selectedEntry();
    if ( !entry )
        return;
    const QString path = pickOutputArtifact( *entry );
    if ( !path.isEmpty() )
        emit inspectRequested( path );
}

void ProcessingHistoryPanel::resumeSelected()
{
    const ActionPlan plan = actionPlan();
    if ( !plan.resume )
    {
        m_statusLabel->setText( plan.resumeReason );
        return;
    }
    HistoryEntry *entry = selectedEntry();
    if ( !entry )
        return;
    emit resumeRunRequested( entry->runId );
}

} // namespace sicnu::app
