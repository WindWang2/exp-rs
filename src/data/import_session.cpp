// src/data/import_session.cpp — see import_session.h for the state machine.
#include "import_session.h"

#include "data_asset.h"
#include "data_manager.h"
#include "import_preflight.h"
#include "source_descriptor.h"

#include "platform/durable_sidecar.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace sicnu::data
{

namespace
{
QString stateName( ImportSessionState state )
{
    switch ( state )
    {
    case ImportSessionState::Checking:
        return QStringLiteral( "checking" );
    case ImportSessionState::ReadyToImport:
        return QStringLiteral( "ready_to_import" );
    case ImportSessionState::Importing:
        return QStringLiteral( "importing" );
    case ImportSessionState::Completed:
        return QStringLiteral( "completed" );
    case ImportSessionState::Cancelled:
        return QStringLiteral( "cancelled" );
    case ImportSessionState::Failed:
        return QStringLiteral( "failed" );
    }
    return QStringLiteral( "unknown" );
}

QString itemName( ImportSessionItemState state )
{
    switch ( state )
    {
    case ImportSessionItemState::Pending:
        return QStringLiteral( "pending" );
    case ImportSessionItemState::Registered:
        return QStringLiteral( "registered" );
    case ImportSessionItemState::Refused:
        return QStringLiteral( "refused" );
    case ImportSessionItemState::Skipped:
        return QStringLiteral( "skipped" );
    case ImportSessionItemState::Error:
        return QStringLiteral( "error" );
    }
    return QStringLiteral( "unknown" );
}
} // namespace

ImportSession::ImportSession( DataManager *manager, QStringList sources, Options options )
    : m_manager( manager ), m_options( std::move( options ) )
{
    m_items.reserve( static_cast<std::size_t>( sources.size() ) );
    for ( const QString &path : sources )
        m_items.push_back( ImportSessionItem{ path, ImportSessionItemState::Pending } );
    if ( !m_options.journalPath.isEmpty() )
        loadJournal();
}

int ImportSessionSummary::registeredCount() const
{
    int n = 0;
    for ( const auto &item : items )
        if ( item.state == ImportSessionItemState::Registered
             || item.state == ImportSessionItemState::Skipped )
            ++n;
    return n;
}

int ImportSessionSummary::refusedCount() const
{
    int n = 0;
    for ( const auto &item : items )
        if ( item.state == ImportSessionItemState::Refused )
            ++n;
    return n;
}

int ImportSessionSummary::pendingCount() const
{
    int n = 0;
    for ( const auto &item : items )
        if ( item.state == ImportSessionItemState::Pending )
            ++n;
    return n;
}

QJsonObject ImportSessionSummary::toJson() const
{
    QJsonObject root;
    root.insert( QStringLiteral( "schema" ), QStringLiteral( "sicnu.import.session/1" ) );
    root.insert( QStringLiteral( "state" ), stateName( state ) );
    root.insert( QStringLiteral( "registered" ), registeredCount() );
    root.insert( QStringLiteral( "refused" ), refusedCount() );
    root.insert( QStringLiteral( "pending" ), pendingCount() );
    QJsonArray array;
    for ( const auto &item : items )
    {
        QJsonObject j;
        j.insert( QStringLiteral( "path" ), item.path );
        j.insert( QStringLiteral( "state" ), itemName( item.state ) );
        if ( !item.verdict.isEmpty() )
            j.insert( QStringLiteral( "preflight_verdict" ), item.verdict );
        if ( !item.assetId.isEmpty() )
            j.insert( QStringLiteral( "asset_id" ), item.assetId );
        if ( !item.message.isEmpty() )
            j.insert( QStringLiteral( "message" ), item.message );
        j.insert( QStringLiteral( "reused_existing" ), item.reusedExisting );
        array.append( j );
    }
    root.insert( QStringLiteral( "items" ), array );
    return root;
}

ImportSessionSummary ImportSession::summary() const
{
    ImportSessionSummary s;
    s.state = m_state;
    s.items = m_items;
    return s;
}

void ImportSession::check()
{
    if ( m_state != ImportSessionState::Checking )
        return; // re-check after a resume re-enters through resume()
    bool anyAdmissible = false;
    for ( auto &item : m_items )
    {
        const ImportPreflightReport report = ImportPreflight::inspect( item.path, m_manager );
        item.verdict = report.verdictString();
        if ( report.alreadyRegistered )
        {
            // Dedup prediction: registering would reuse the existing asset —
            // admissible (registerSource records the reuse honestly).
            anyAdmissible = true;
            continue;
        }
        if ( report.verdict == ImportPreflightVerdict::Importable )
        {
            anyAdmissible = true;
            continue;
        }
        if ( report.verdict == ImportPreflightVerdict::NeedsUserChoice && !m_options.refuseNeedsUserChoice )
        {
            anyAdmissible = true;
            continue;
        }
        item.state = ImportSessionItemState::Refused;
        item.message = report.verdictString();
        for ( const ImportPreflightFinding &finding : report.findings )
        {
            if ( finding.severity == QLatin1String( "error" ) )
            {
                item.message = finding.message;
                break;
            }
        }
    }
    m_state = anyAdmissible ? ImportSessionState::ReadyToImport : ImportSessionState::Completed;
}

void ImportSession::proceed()
{
    if ( m_state != ImportSessionState::ReadyToImport )
        return;
    m_state = ImportSessionState::Importing;
    runPendingItems();
}

void ImportSession::cancel()
{
    m_cancelRequested.store( true );
    if ( m_state == ImportSessionState::Checking || m_state == ImportSessionState::ReadyToImport
         || m_state == ImportSessionState::Importing )
        m_state = ImportSessionState::Cancelled;
}

void ImportSession::resume()
{
    if ( m_state != ImportSessionState::Cancelled && m_state != ImportSessionState::Failed )
        return;
    m_cancelRequested.store( false );
    if ( !m_journalLoaded && !m_options.journalPath.isEmpty() )
        loadJournal();
    // Re-verify journaled-registered items against the catalog: a source
    // that no longer resolves is left Registered in the journal but must be
    // re-attempted here (the journal is a hint, the catalog is the truth).
    m_state = ImportSessionState::Importing;
    runPendingItems();
}

void ImportSession::runPendingItems()
{
    bool anyError = false;
    for ( auto &item : m_items )
    {
        if ( m_cancelRequested.load() )
        {
            m_state = ImportSessionState::Cancelled;
            saveJournal();
            return;
        }
        if ( item.state != ImportSessionItemState::Pending )
            continue;

        if ( m_manager )
        {
            const auto existing = m_manager->findByPath( item.path );
            if ( existing && !existing->id().isNull() )
            {
                // Already in the catalog (prior run's journal, external
                // registration): idempotent skip, asset identity from the
                // ONE authority (the catalog) — never re-derived here.
                item.state = ImportSessionItemState::Skipped;
                item.assetId = existing->id().toString();
                item.message = QStringLiteral( "already registered (dedup)" );
                saveJournal();
                continue;
            }
        }

        RegisterRequest request;
        request.source.canonicalSource = item.path;
        // The provider layer resolves by content; no providerKey guessing.
        if ( !m_manager )
        {
            item.state = ImportSessionItemState::Error;
            item.message = QStringLiteral( "no DataManager attached" );
            anyError = true;
            continue;
        }
        const RegisterResult result = m_manager->registerSource( request );
        item.reusedExisting = result.reusedExisting;
        item.assetId = result.assetId.toString();
        if ( !result.assetId.isNull() )
        {
            item.state = ImportSessionItemState::Registered;
        }
        else
        {
            item.state = ImportSessionItemState::Error;
            item.message = QStringLiteral( "registerSource refused the source" );
            anyError = true;
        }
        saveJournal();
    }
    m_state = anyError ? ImportSessionState::Failed : ImportSessionState::Completed;
    saveJournal();
}

void ImportSession::saveJournal() const
{
    if ( m_options.journalPath.isEmpty() )
        return;
    const QJsonDocument doc( summary().toJson() );
    sicnu::platform::sidecar::WriteRequest write;
    write.targetPath = m_options.journalPath.toStdString();
    write.bytes = doc.toJson( QJsonDocument::Compact ).toStdString();
    write.lastGoodSuffix = ""; // journal must never resurrect a deleted session
    ( void )sicnu::platform::sidecar::write( write ); // flush failures surface via summary state
}

void ImportSession::loadJournal()
{
    m_journalLoaded = true;
    if ( m_options.journalPath.isEmpty() )
        return;
    const auto read = sicnu::platform::sidecar::read( m_options.journalPath.toStdString(), "" );
    if ( read.source == sicnu::platform::sidecar::ReadSource::Missing
         || read.source == sicnu::platform::sidecar::ReadSource::Corrupt )
        return;
    const QJsonDocument doc = QJsonDocument::fromJson( QByteArray( read.bytes.data(),
                                                                   static_cast<int>(
                                                                       read.bytes.size() ) ) );
    if ( !doc.isObject() )
        return;
    const QJsonObject root = doc.object();
    if ( root.value( QStringLiteral( "schema" ) ).toString()
         != QStringLiteral( "sicnu.import.session/1" ) )
        return;
    const QJsonArray items = root.value( QStringLiteral( "items" ) ).toArray();
    std::size_t i = 0;
    for ( const auto &value : items )
    {
        if ( i >= m_items.size() )
            break;
        const QJsonObject j = value.toObject();
        if ( j.value( QStringLiteral( "path" ) ).toString() != m_items[ i ].path )
        {
            ++i;
            continue;
        }
        const QString state = j.value( QStringLiteral( "state" ) ).toString();
        if ( state == QLatin1String( "registered" ) || state == QLatin1String( "skipped" ) )
            m_items[ i ].state = ImportSessionItemState::Skipped;
        else if ( state == QLatin1String( "refused" ) )
            m_items[ i ].state = ImportSessionItemState::Refused;
        m_items[ i ].assetId = j.value( QStringLiteral( "asset_id" ) ).toString();
        m_items[ i ].message = j.value( QStringLiteral( "message" ) ).toString();
        ++i;
    }
    // A loaded journal implies prior progress: the session starts resumable
    // from Cancelled/Failed semantics; resume() re-runs Pending items.
    if ( m_state == ImportSessionState::Checking )
    {
        bool allSettled = true;
        for ( const auto &item : m_items )
            if ( item.state == ImportSessionItemState::Pending )
                allSettled = false;
        m_state = allSettled && !m_items.empty() ? ImportSessionState::Completed
                                                 : ImportSessionState::Failed;
    }
}

} // namespace sicnu::data
