// src/data/import_session.cpp — see import_session.h for the state machine.
#include "import_session.h"

#include "data_asset.h"
#include "data_manager.h"
#include "import_preflight.h"
#include "source_descriptor.h"

#include "platform/durable_sidecar.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <limits>

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

ImportSession::ImportSession( DataManager *manager, QStringList sources )
    : ImportSession( manager, std::move( sources ), Options{} )
{
}

ImportSession::ImportSession( DataManager *manager, QStringList sources, const Options &options )
    : m_manager( manager ), m_options( options )
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
    root.insert( QStringLiteral( "schema" ),
                 QStringLiteral( "sicnu.import.session/%1" ).arg( kSchemaVersion ) );
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
        if ( !item.identityToken.isEmpty() )
            j.insert( QStringLiteral( "identity_token" ), item.identityToken );
        j.insert( QStringLiteral( "reused_existing" ), item.reusedExisting );
        array.append( j );
    }
    if ( !journalError.isEmpty() )
    {
        root.insert( QStringLiteral( "journal_write_error" ), journalError );
        root.insert( QStringLiteral( "journal_durable" ), false );
    }
    root.insert( QStringLiteral( "items" ), array );
    return root;
}

ImportSessionSummary ImportSession::summary() const
{
    ImportSessionSummary s;
    s.state = m_state;
    s.items = m_items;
    s.journalError = m_journalError;
    s.journalDurable = m_journalDurable;
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
        item.identityToken = report.identityToken;
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

int ImportSession::checkSteps( std::size_t maxItems )
{
    if ( m_state != ImportSessionState::Checking )
        return 0;
    if ( m_cancelRequested.load() )
    {
        m_state = ImportSessionState::Cancelled;
        saveJournal();
        return 0;
    }
    std::size_t inspected = 0;
    std::size_t withVerdict = 0;
    for ( auto &item : m_items )
    {
        if ( !item.verdict.isEmpty() )
        {
            ++withVerdict;
            continue;
        }
        if ( inspected >= maxItems )
            continue; // budget spent; keep scanning only to count verdicts
        const ImportPreflightReport report = ImportPreflight::inspect( item.path, m_manager );
        item.verdict = report.verdictString();
        item.identityToken = report.identityToken;
        ++inspected;
        ++withVerdict;
        if ( report.alreadyRegistered || report.verdict == ImportPreflightVerdict::Importable
             || ( report.verdict == ImportPreflightVerdict::NeedsUserChoice
                  && !m_options.refuseNeedsUserChoice ) )
        {
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
    if ( withVerdict == m_items.size() )
    {
        // `checkSteps` only inspects a bounded slice on each call. Decide the
        // terminal state from the WHOLE batch, not from the last slice: a
        // final chunk made only of Refused items must not silently mark
        // admissible items from earlier chunks as "Completed".
        m_state = hasPendingItems() ? ImportSessionState::ReadyToImport
                                    : ImportSessionState::Completed;
    }
    return static_cast<int>( inspected );
}

void ImportSession::proceed()
{
    if ( m_state != ImportSessionState::ReadyToImport )
        return;
    m_state = ImportSessionState::Importing;
    runPendingItemsBounded( std::numeric_limits<std::size_t>::max() );
}

int ImportSession::proceedSteps( std::size_t maxItems )
{
    if ( m_state != ImportSessionState::ReadyToImport
         && m_state != ImportSessionState::Importing )
        return 0;
    if ( m_state == ImportSessionState::ReadyToImport )
        m_state = ImportSessionState::Importing;
    return runPendingItemsBounded( maxItems );
}

bool ImportSession::hasPendingItems() const
{
    for ( const auto &item : m_items )
        if ( item.state == ImportSessionItemState::Pending )
            return true;
    return false;
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
    // The journal is a hint; the catalog is the truth. A journaled
    // "registered" whose source no longer resolves in the catalog (asset
    // removed, journal restored onto a fresh catalog) goes back to Pending
    // and is re-attempted — never silently reported done (R1-2).
    if ( m_manager )
    {
        for ( auto &item : m_items )
        {
            if ( item.state != ImportSessionItemState::Skipped )
                continue;
            const auto existing = m_manager->findByPath( item.path );
            if ( !existing || existing->id().isNull() )
                item.state = ImportSessionItemState::Pending;
        }
    }
    // Facts about the files behind the journaled-settled items (replaced /
    // moved sources) are surfaced before new work starts (WP03).
    verifyFacts();
    // Items that never went through check() in THIS process (cancel during
    // Checking, cross-restart journal) must not reach registerSource
    // unexamined — the session never guesses a NeedsUserChoice source
    // (R1-3).
    admitPendingItems();
    m_state = ImportSessionState::Importing;
    runPendingItemsBounded( std::numeric_limits<std::size_t>::max() );
}

void ImportSession::verifyFacts()
{
    // The fact notes THIS pass computes — stripped from a previous pass's
    // message first so re-verification is idempotent (a Data Doctor shape
    // polling verifyFacts() must never accumulate "; missing; missing; …").
    const QString noteMissing = QStringLiteral( "source file missing on disk" );
    const QString noteReplaced =
        QStringLiteral( "source file identity changed since check (replaced on disk)" );
    auto stripFactNotes = [&]( QString message ) {
        for ( const QString &note : { noteMissing, noteReplaced } )
        {
            const QString withSep = QLatin1String( "; " ) + note;
            if ( message.endsWith( withSep ) )
                message.chop( withSep.size() );
            else if ( message == note )
                message.clear();
        }
        return message;
    };
    for ( auto &item : m_items )
    {
        if ( item.state != ImportSessionItemState::Skipped )
            continue;
        QString base = stripFactNotes( item.message );
        if ( !QFile::exists( item.path ) )
        {
            item.message = base.isEmpty() ? noteMissing
                                          : base + QLatin1String( "; " ) + noteMissing;
            continue;
        }
        if ( item.identityToken.isEmpty() )
        {
            item.message = base; // heal any stale missing-note; no identity to compare
            continue;
        }
        ImportPreflight::Options probeOptions;
        probeOptions.probeIdentity = true;
        probeOptions.probeDedup = false;
        const ImportPreflightReport fresh =
            ImportPreflight::inspect( item.path, nullptr, probeOptions );
        if ( !fresh.identityToken.isEmpty() && fresh.identityToken != item.identityToken )
        {
            item.message = base.isEmpty()
                               ? noteReplaced
                               : base + QLatin1String( "; " ) + noteReplaced;
        }
        else
        {
            item.message = base; // file back / identity matches: stale note heals away
        }
    }
}

/// Preflight admission over still-Pending items that have no verdict: the
/// same gate check() applies, factored so resume() cannot bypass it.
void ImportSession::admitPendingItems()
{
    bool anyAdmissible = false;
    for ( auto &item : m_items )
    {
        if ( item.state != ImportSessionItemState::Pending || !item.verdict.isEmpty() )
            continue;
        const ImportPreflightReport report = ImportPreflight::inspect( item.path, m_manager );
        item.verdict = report.verdictString();
        item.identityToken = report.identityToken;
        if ( report.verdict == ImportPreflightVerdict::Importable
             || report.alreadyRegistered
             || ( report.verdict == ImportPreflightVerdict::NeedsUserChoice
                  && !m_options.refuseNeedsUserChoice ) )
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
    ( void )anyAdmissible; // admission only REFUSES here; Pending stays Pending
}

int ImportSession::runPendingItemsBounded( std::size_t maxItems )
{
    bool anyError = false;
    std::size_t settled = 0;
    for ( auto &item : m_items )
    {
        if ( m_cancelRequested.load() )
        {
            m_state = ImportSessionState::Cancelled;
            saveJournal();
            return static_cast<int>( settled );
        }
        if ( settled >= maxItems )
            break; // budget spent: leave the session Importing with work left
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
                ++settled;
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
            ++settled;
            saveJournal();
            continue;
        }
        RegisterResult result;
        try
        {
            result = m_manager->registerSource( request );
        }
        catch ( const std::exception &e )
        {
            // A throwing registration is a per-item failure: record it and
            // keep the batch moving — the session must never strand in
            // Importing without a terminal state (R1-10).
            item.state = ImportSessionItemState::Error;
            item.message = QString::fromUtf8( e.what() );
            anyError = true;
            ++settled;
            saveJournal();
            continue;
        }
        catch ( ... )
        {
            item.state = ImportSessionItemState::Error;
            item.message = QStringLiteral( "registerSource threw an unknown exception" );
            anyError = true;
            ++settled;
            saveJournal();
            continue;
        }
        item.reusedExisting = result.reusedExisting;
        item.assetId = result.assetId.toString();
        if ( !result.assetId.isNull() )
        {
            item.state = ImportSessionItemState::Registered;
            // A fresh registration retires any stale note the item carried
            // (dedup hint from a journal round, fact-check warning from a
            // previous verifyFacts pass) — the summary must not show a
            // warning for work that just succeeded (R1 P3).
            item.message.clear();
        }
        else
        {
            item.state = ImportSessionItemState::Error;
            item.message = QStringLiteral( "registerSource refused the source" );
            anyError = true;
        }
        ++settled;
        saveJournal();
    }
    // Terminal transition only when this call finished the batch: a bounded
    // call that hit its budget leaves the session Importing with pending
    // work (the stepwise driver keeps its event loop alive between calls).
    if ( !hasPendingItems() )
    {
        // A bounded call only sees the current slice. An Error settled in an
        // EARLIER call is still in m_items, so scan the whole batch instead
        // of trusting this call's local anyError. Otherwise the last slice
        // succeeding turns a partially failed batch into Completed.
        bool anyItemFailed = anyError;
        for ( const auto &candidate : m_items )
        {
            if ( candidate.state == ImportSessionItemState::Error )
            {
                anyItemFailed = true;
                break;
            }
        }
        m_state = anyItemFailed ? ImportSessionState::Failed : ImportSessionState::Completed;
        saveJournal();
    }
    return static_cast<int>( settled );
}

void ImportSession::saveJournal()
{
    if ( m_options.journalPath.isEmpty() )
        return;
    const QJsonDocument doc( summary().toJson() );
    sicnu::platform::sidecar::WriteRequest write;
    write.targetPath = m_options.journalPath.toStdString();
    write.bytes = doc.toJson( QJsonDocument::Compact ).toStdString();
    write.lastGoodSuffix = ""; // journal must never resurrect a deleted session
    const sicnu::platform::sidecar::WriteResult result =
        sicnu::platform::sidecar::write( write );
    if ( !result )
    {
        // R1-4: a journal that cannot be persisted means resume-after-crash
        // is NOT durable — record it (summary exposes journal_write_error)
        // instead of pretending a resume point exists. The import itself
        // keeps going: the catalog remains the truth for idempotent resume.
        m_journalError = QString::fromUtf8( result.error.c_str() );
        m_journalDurable = false;
    }
    else
    {
        // A later successful publish HEALS the error (R2-F7): one transient
        // failure must not brand every subsequent summary non-durable.
        m_journalError.clear();
        m_journalDurable = true;
    }
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
    // Path-keyed matching (R2-F7): a reordered or extended source list must
    // not silently drop the resume state of every item after the first
    // positional mismatch.
    QHash<QString, QJsonObject> journaled;
    const QJsonArray items = root.value( QStringLiteral( "items" ) ).toArray();
    for ( const auto &value : items )
    {
        const QJsonObject j = value.toObject();
        journaled.insert( j.value( QStringLiteral( "path" ) ).toString(), j );
    }
    for ( auto &item : m_items )
    {
        const auto it = journaled.constFind( item.path );
        if ( it == journaled.constEnd() )
            continue;
        const QJsonObject &j = *it;
        const QString state = j.value( QStringLiteral( "state" ) ).toString();
        if ( state == QLatin1String( "registered" ) || state == QLatin1String( "skipped" ) )
            item.state = ImportSessionItemState::Skipped;
        else if ( state == QLatin1String( "refused" ) )
            item.state = ImportSessionItemState::Refused;
        item.assetId = j.value( QStringLiteral( "asset_id" ) ).toString();
        item.message = j.value( QStringLiteral( "message" ) ).toString();
        item.identityToken = j.value( QStringLiteral( "identity_token" ) ).toString();
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
