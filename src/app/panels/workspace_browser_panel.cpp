// workspace_browser_panel.cpp — see workspace_browser_panel.h for the contract.
#include "workspace_browser_panel.h"

#include "data/governance/workspace_service.h"
#include "data/governance/workspace_validator.h"

#include <QComboBox>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QTableView>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace sicnu::app
{

using namespace sicnu::workspace;

// ---------------------------------------------------------------------------
// WorkspaceGovernanceModel
// ---------------------------------------------------------------------------

WorkspaceGovernanceModel::WorkspaceGovernanceModel( QObject *parent )
    : QAbstractTableModel( parent )
{
}

void WorkspaceGovernanceModel::setWorkspaceService( WorkspaceService *service )
{
    beginResetModel();
    m_service = service;
    m_rows.clear();
    m_total = 0;
    endResetModel();
}

void WorkspaceGovernanceModel::applyFilters( const QString &text, const QString &entitySet,
                                             const QString &kind, const QString &state,
                                             const QString &sensor )
{
    m_query = WorkspaceQuery();
    m_query.text = text;
    if ( entitySet == QLatin1String( "results" ) ) m_query.set = EntitySet::Results;
    else if ( entitySet == QLatin1String( "runs" ) ) m_query.set = EntitySet::Runs;
    else if ( entitySet == QLatin1String( "datasets" ) ) m_query.set = EntitySet::Datasets;
    else m_query.set = EntitySet::Assets;
    m_query.kind = kind;
    m_query.state = state;
    m_query.sensor = sensor;
    m_query.limit = kPageSize;
    resetQuery();
}

void WorkspaceGovernanceModel::resetQuery()
{
    beginResetModel();
    m_rows.clear();
    m_total = 0;
    endResetModel();
    if ( m_service && m_service->isStoreOpen() )
    {
        const WorkspacePage page = m_service->query( m_query );
        m_total = page.total;
        m_rows = page.items;
    }
}

int WorkspaceGovernanceModel::rowCount( const QModelIndex &parent ) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int WorkspaceGovernanceModel::columnCount( const QModelIndex &parent ) const
{
    return parent.isValid() ? 0 : 6;
}

QVariant WorkspaceGovernanceModel::data( const QModelIndex &index, int role ) const
{
    if ( !index.isValid() || index.row() >= m_rows.size() || role != Qt::DisplayRole )
        return QVariant();
    const QVariantMap &row = m_rows.at( index.row() );
    switch ( index.column() )
    {
        case 0:
        {
            const QString name = row.value( QStringLiteral( "display_name" ) ).toString();
            if ( !name.isEmpty() )
                return name;
            return row.value( QStringLiteral( "name" ) ).toString();
        }
        case 1: return row.value( QStringLiteral( "kind" ) ).toString();
        case 2:
        {
            const QString state = row.value( QStringLiteral( "state" ) ).toString();
            if ( !state.isEmpty() )
                return state;
            return row.value( QStringLiteral( "status" ) ).toString();
        }
        case 3:
        {
            const QString locator = row.value( QStringLiteral( "canonical_source" ) ).toString();
            if ( !locator.isEmpty() )
                return locator;
            const QString run = row.value( QStringLiteral( "run_id" ) ).toString();
            if ( !run.isEmpty() )
                return run;
            return row.value( QStringLiteral( "workflow_id" ) ).toString();
        }
        case 4:
        {
            const QString sensor = row.value( QStringLiteral( "sensor" ) ).toString();
            if ( !sensor.isEmpty() )
                return sensor;
            return row.value( QStringLiteral( "semantic_type" ) ).toString();
        }
        case 5:
        {
            const QVariant updated = row.value( QStringLiteral( "updated_ms" ) );
            bool ok = false;
            const qint64 ms = updated.toLongLong( &ok );
            if ( !ok || ms <= 0 )
                return QVariant();
            return QDateTime::fromMSecsSinceEpoch( ms ).toString( QStringLiteral( "yyyy-MM-dd HH:mm" ) );
        }
    }
    return QVariant();
}

QVariant WorkspaceGovernanceModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
    if ( orientation != Qt::Horizontal || role != Qt::DisplayRole )
        return QAbstractTableModel::headerData( section, orientation, role );
    switch ( section )
    {
        case 0: return tr( "Name" );
        case 1: return tr( "Kind" );
        case 2: return tr( "State" );
        case 3: return tr( "Locator / Run" );
        case 4: return tr( "Sensor" );
        case 5: return tr( "Updated" );
    }
    return QVariant();
}

bool WorkspaceGovernanceModel::canFetchMore( const QModelIndex &parent ) const
{
    if ( parent.isValid() || !m_service )
        return false;
    return m_rows.size() < m_total;
}

void WorkspaceGovernanceModel::fetchMore( const QModelIndex &parent )
{
    if ( parent.isValid() || !m_service || m_rows.size() >= m_total )
        return;
    WorkspaceQuery query = m_query;
    query.offset = m_rows.size();
    query.limit = kPageSize;
    const WorkspacePage page = m_service->query( query );
    if ( page.items.isEmpty() )
    {
        m_total = m_rows.size();
        return;
    }
    const int first = m_rows.size();
    beginInsertRows( QModelIndex(), first, first + page.items.size() - 1 );
    for ( const QVariantMap &row : page.items )
        m_rows.append( row );
    endInsertRows();
}

QString WorkspaceGovernanceModel::entityId( int row ) const
{
    if ( row < 0 || row >= m_rows.size() )
        return QString();
    const QVariantMap &rowMap = m_rows.at( row );
    for ( const QString &key : { QStringLiteral( "asset_id" ), QStringLiteral( "result_id" ),
                                 QStringLiteral( "run_id" ), QStringLiteral( "dataset_id" ) } )
    {
        const QString id = rowMap.value( key ).toString();
        if ( !id.isEmpty() )
            return id;
    }
    return QString();
}

// ---------------------------------------------------------------------------
// WorkspaceBrowserPanel
// ---------------------------------------------------------------------------

WorkspaceBrowserPanel::WorkspaceBrowserPanel( QWidget *parent )
    : QWidget( parent )
{
    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 4, 4, 4, 4 );
    layout->setSpacing( 4 );

    auto *filters = new QGridLayout();
    filters->setHorizontalSpacing( 4 );
    filters->setVerticalSpacing( 4 );

    m_search = new QLineEdit( this );
    m_search->setPlaceholderText( tr( "Search name / locator…" ) );
    m_search->setClearButtonEnabled( true );
    filters->addWidget( m_search, 0, 0, 1, 3 );

    m_entitySet = new QComboBox( this );
    m_entitySet->addItems( { tr( "Assets" ), tr( "Results" ), tr( "Runs" ), tr( "Datasets" ) } );
    filters->addWidget( m_entitySet, 1, 0 );

    m_kind = new QComboBox( this );
    m_kind->addItem( tr( "All kinds" ), QString() );
    for ( const QString &kind :
          { QStringLiteral( "raster" ), QStringLiteral( "vector" ), QStringLiteral( "remote_map" ),
            QStringLiteral( "virtual_raster" ) } )
        m_kind->addItem( kind, kind );
    filters->addWidget( m_kind, 1, 1 );

    m_state = new QComboBox( this );
    m_state->addItem( tr( "All states" ), QString() );
    for ( const QString &state :
          { QStringLiteral( "Ready" ), QStringLiteral( "Missing" ), QStringLiteral( "Error" ),
            QStringLiteral( "Stale" ), QStringLiteral( "Registered" ) } )
        m_state->addItem( state, state );
    filters->addWidget( m_state, 1, 2 );

    m_sensor = new QComboBox( this );
    m_sensor->addItem( tr( "All sensors" ), QString() );
    refreshSensorFacet();
    filters->addWidget( m_sensor, 2, 0, 1, 3 );

    layout->addLayout( filters );

    m_model = new WorkspaceGovernanceModel( this );
    m_table = new QTableView( this );
    m_table->setModel( m_model );
    m_table->horizontalHeader()->setStretchLastSection( true );
    m_table->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::Stretch );
    m_table->setSelectionBehavior( QAbstractItemView::SelectRows );
    m_table->setSelectionMode( QAbstractItemView::SingleSelection );
    m_table->setEditTriggers( QAbstractItemView::NoEditTriggers );
    m_table->verticalHeader()->setVisible( false );
    layout->addWidget( m_table, 3 );

    m_details = new QTextEdit( this );
    m_details->setReadOnly( true );
    m_details->setMaximumHeight( 120 );
    m_details->setPlaceholderText( tr( "Select an entity for governed details…" ) );
    layout->addWidget( m_details );

    auto *footer = new QHBoxLayout();
    m_status = new QLabel( this );
    footer->addWidget( m_status, 1 );
    m_healthButton = new QPushButton( tr( "Health check" ), this );
    footer->addWidget( m_healthButton );
    layout->addLayout( footer );

    // Review P2-17: coalesce store-refresh churn during bulk import instead
    // of one full re-query per asset.
    m_refreshCoalescer = new QTimer( this );
    m_refreshCoalescer->setSingleShot( true );
    m_refreshCoalescer->setInterval( 300 );
    connect( m_refreshCoalescer, &QTimer::timeout, this, &WorkspaceBrowserPanel::refresh );

    connect( m_search, &QLineEdit::textChanged, this, &WorkspaceBrowserPanel::refresh );
    connect( m_entitySet, &QComboBox::currentIndexChanged, this, &WorkspaceBrowserPanel::refresh );
    connect( m_kind, &QComboBox::currentIndexChanged, this, &WorkspaceBrowserPanel::refresh );
    connect( m_state, &QComboBox::currentIndexChanged, this, &WorkspaceBrowserPanel::refresh );
    connect( m_sensor, &QComboBox::currentIndexChanged, this, &WorkspaceBrowserPanel::refresh );
    connect( m_healthButton, &QPushButton::clicked, this, &WorkspaceBrowserPanel::runHealthCheck );
    connect( m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
             &WorkspaceBrowserPanel::showDetails );
}

void WorkspaceBrowserPanel::setWorkspaceService( WorkspaceService *service )
{
    if ( m_service )
        disconnect( m_service, &WorkspaceService::entityChanged, this, &WorkspaceBrowserPanel::refresh );
    m_service = service;
    m_model->setWorkspaceService( service );
    if ( service )
    {
        // Batched refresh on any governed mutation (assets mirrored, tags, …).
        connect( service, &WorkspaceService::entityChanged, m_refreshCoalescer,
                 qOverload<>(&QTimer::start), Qt::QueuedConnection );
    }
    refresh();
}

void WorkspaceBrowserPanel::refresh()
{
    if ( !m_service )
        return;
    const QString sensor = m_sensor->currentData().toString();
    refreshSensorFacet();
    const int sensorIndex = m_sensor->findData( sensor );
    m_sensor->setCurrentIndex( sensorIndex < 0 ? 0 : sensorIndex );

    const int setIndex = m_entitySet->currentIndex();
    const QString entitySet = setIndex <= 0 ? QStringLiteral( "assets" )
                              : setIndex == 1 ? QStringLiteral( "results" )
                              : setIndex == 2 ? QStringLiteral( "runs" ) : QStringLiteral( "datasets" );
    m_model->applyFilters( m_search->text(), entitySet, m_kind->currentData().toString(),
                           m_state->currentData().toString(), m_sensor->currentData().toString() );
    m_status->setText( tr( "%1 entities" ).arg( m_model->rowCount() ) );
}

void WorkspaceBrowserPanel::refreshSensorFacet()
{
    const QString current = m_sensor->currentData().toString();
    QSignalBlocker blocker( m_sensor );
    while ( m_sensor->count() > 1 )
        m_sensor->removeItem( m_sensor->count() - 1 );
    if ( m_service && m_service->isStoreOpen() )
    {
        WorkspaceQuery query;
        query.set = EntitySet::Assets;
        const WorkspacePage page = m_service->query( query, QStringLiteral( "sensor" ) );
        for ( const FacetCount &facet : page.facets )
            m_sensor->addItem( QStringLiteral( "%1 (%2)" ).arg( facet.value ).arg( facet.count ), facet.value );
    }
    const int index = m_sensor->findData( current );
    m_sensor->setCurrentIndex( index < 0 ? 0 : index );
}

void WorkspaceBrowserPanel::runHealthCheck()
{
    if ( !m_service || !m_service->isStoreOpen() )
        return;
    // Validation runs inline (bounded diagnostics); the GUI never renders one
    // widget per finding — findings land in the shared details pane.
    Q_ASSERT( m_service->dataManager() );
    WorkspaceValidator validator( *m_service->dataManager(), *m_service );
    const ValidationReport report = validator.validateProject();
    QStringList lines;
    lines << tr( "Errors: %1  Warnings: %2  Infos: %3" )
                 .arg( report.summary.errors )
                 .arg( report.summary.warnings )
                 .arg( report.summary.infos );
    int shown = 0;
    for ( const GovernanceDiagnostic &d : report.diagnostics )
    {
        if ( ++shown > 50 )
        {
            lines << tr( "… (%1 more findings omitted)" ).arg( report.diagnostics.size() - 50 );
            break;
        }
        lines << QStringLiteral( "[%1] %2 %3 — %4" )
                     .arg( d.severity == DiagnosticSeverity::Error ? QStringLiteral( "E" )
                           : d.severity == DiagnosticSeverity::Warning ? QStringLiteral( "W" )
                                                                       : QStringLiteral( "I" ),
                           d.entityKind, d.entityId, d.message );
        if ( !d.repairSuggestion.isEmpty() )
            lines << QStringLiteral( "      repair: %1" ).arg( d.repairSuggestion );
    }
    m_details->setPlainText( lines.join( QLatin1Char( '\n' ) ) );
}

void WorkspaceBrowserPanel::showDetails( const QModelIndex &index )
{
    if ( !m_service || !m_service->isStoreOpen() || !index.isValid() )
        return;
    const QString id = m_model->entityId( index.row() );
    if ( id.isEmpty() )
        return;
    const std::optional<GovernedAsset> asset = m_service->store().assetById( id );
    if ( !asset )
        return;
    QStringList lines;
    lines << tr( "Asset: %1" ).arg( asset->displayName );
    lines << tr( "Identity: %1 (revision %2)" ).arg( asset->assetId ).arg( asset->revision );
    lines << tr( "Locator: %1" ).arg( asset->canonicalSource );
    lines << tr( "Fingerprint: %1" ).arg( asset->contentFingerprint.isEmpty()
                                              ? tr( "not computed" ) : asset->contentFingerprint );
    lines << tr( "Availability: %1 (verified %2)" )
                 .arg( asset->availability,
                       asset->verifiedMs ? QDateTime::fromMSecsSinceEpoch( asset->verifiedMs ).toString()
                                         : tr( "never" ) );
    const QVector<QVariantMap> upstream = m_service->lineageUpstream( id, 5 );
    lines << tr( "Upstream (≤5): %1" ).arg( upstream.size() );
    const QVector<QVariantMap> downstream = m_service->lineageDownstream( id, 5 );
    lines << tr( "Downstream (≤5): %1" ).arg( downstream.size() );
    m_details->setPlainText( lines.join( QLatin1Char( '\n' ) ) );
}

} // namespace sicnu::app
