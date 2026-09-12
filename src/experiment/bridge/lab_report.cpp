// lab_report.cpp — see lab_report.h. Deterministic assembly of
// `sicnu.labreport.v1` from the experiment governance stack.
#include "lab_report.h"

#include "../dataset/dataset_store.h"
#include "experiment/lineage.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QSet>

#include <algorithm>
#include <tuple>

namespace sicnu::experiment
{

using sicnu::dataset::DatasetStore;

namespace
{

QString isoMs( const QDateTime &time )
{
    return time.isValid() ? time.toString( Qt::ISODateWithMs ) : QString();
}

QDateTime parseUtc( const QString &iso )
{
    if ( iso.isEmpty() )
        return {};
    QDateTime time = QDateTime::fromString( iso, Qt::ISODateWithMs );
    if ( time.isNull() )
        time = QDateTime::fromString( iso, Qt::ISODate );
    return time;
}

/// Stable display fallback for empty identity fields — explicit "unknown",
/// never an invented value.
QString unknownSafe( const QString &value )
{
    return value.isEmpty() ? QStringLiteral( "unknown" ) : value;
}

QJsonObject artifactToJson( const ExperimentRun::Artifact &artifact )
{
    QJsonObject json;
    json.insert( QStringLiteral( "path" ), artifact.path );
    if ( !artifact.role.isEmpty() )
        json.insert( QStringLiteral( "role" ), artifact.role );
    if ( !artifact.digest.isEmpty() )
        json.insert( QStringLiteral( "digest" ), artifact.digest );
    if ( artifact.sizeBytes >= 0 )
        json.insert( QStringLiteral( "sizeBytes" ), static_cast<double>( artifact.sizeBytes ) );
    return json;
}

QJsonObject runToJson( const ExperimentRun &run )
{
    QJsonObject json;
    json.insert( QStringLiteral( "runId" ), run.runId() );
    json.insert( QStringLiteral( "experimentId" ), run.experimentId() );
    json.insert( QStringLiteral( "status" ), sicnu::dataset::runStatusToString( run.status() ) );
    json.insert( QStringLiteral( "executionRef" ), run.executionRef() );
    json.insert( QStringLiteral( "algorithmId" ), run.algorithmId() );
    if ( !run.algorithmVersion().isEmpty() )
        json.insert( QStringLiteral( "algorithmVersion" ), run.algorithmVersion() );
    json.insert( QStringLiteral( "parameters" ), RunEnvironment::redactSecretKeys( run.parameters() ) );
    if ( !run.datasetVersionId().isEmpty() )
        json.insert( QStringLiteral( "datasetVersionId" ), run.datasetVersionId() );
    if ( !run.datasetFingerprint().isEmpty() )
        json.insert( QStringLiteral( "datasetFingerprint" ), run.datasetFingerprint() );
    if ( !run.splitManifestId().isEmpty() )
        json.insert( QStringLiteral( "splitManifestId" ), run.splitManifestId() );
    if ( !run.splitFingerprint().isEmpty() )
        json.insert( QStringLiteral( "splitFingerprint" ), run.splitFingerprint() );
    if ( !run.modelId().isEmpty() )
        json.insert( QStringLiteral( "modelId" ), run.modelId() );
    if ( !run.modelDigest().isEmpty() )
        json.insert( QStringLiteral( "modelDigest" ), run.modelDigest() );
    json.insert( QStringLiteral( "seed" ), static_cast<double>( run.seed() ) );
    json.insert( QStringLiteral( "determinism" ),
                 sicnu::dataset::determinismGradeToString( run.determinism() ) );
    if ( !run.determinismNote().isEmpty() )
        json.insert( QStringLiteral( "determinismNote" ), run.determinismNote() );
    json.insert( QStringLiteral( "startedAtUtc" ), isoMs( run.startedAtUtc() ) );
    json.insert( QStringLiteral( "finishedAtUtc" ), isoMs( run.finishedAtUtc() ) );
    json.insert( QStringLiteral( "createdAtUtc" ), isoMs( run.createdAtUtc() ) );
    json.insert( QStringLiteral( "softwareRevision" ), run.softwareRevision() );
    json.insert( QStringLiteral( "configHash" ), run.configHash() );
    json.insert( QStringLiteral( "executionFingerprint" ),
                 runExecutionFingerprint( run.executionIdentity() ) );
    json.insert( QStringLiteral( "resultFingerprint" ), run.resultFingerprint() );

    QJsonArray artifacts;
    // Deterministic artifact order: path, then role, then digest.
    QVector<ExperimentRun::Artifact> sortedArtifacts = run.artifacts();
    std::sort( sortedArtifacts.begin(), sortedArtifacts.end(),
               []( const ExperimentRun::Artifact &a, const ExperimentRun::Artifact &b ) {
                   return std::tie( a.path, a.role, a.digest )
                          < std::tie( b.path, b.role, b.digest );
               } );
    for ( const auto &artifact : sortedArtifacts )
        artifacts.append( artifactToJson( artifact ) );
    json.insert( QStringLiteral( "artifacts" ), artifacts );

    // Workflow evidence recorded by the bridge (step summaries with output
    // digests, artifact paths) — passed through redacted, bounded already at
    // record time (ADR 0143).
    const QJsonObject workflow = run.metrics().value( QStringLiteral( "workflow" ) ).toObject();
    if ( !workflow.isEmpty() )
        json.insert( QStringLiteral( "workflow" ), RunEnvironment::redactSecretKeys( workflow ) );

    return json;
}

} // namespace

// --- LabGradeEmbedding ---------------------------------------------------------

QJsonObject LabGradeEmbedding::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "status" ), status );
    if ( status == QStringLiteral( "recorded" ) )
    {
        json.insert( QStringLiteral( "gradingRef" ), gradingRef );
        if ( !gradingRefDetails.isEmpty() )
            json.insert( QStringLiteral( "gradingRefDetails" ), gradingRefDetails );
        json.insert( QStringLiteral( "inline" ), inlineResult );
    }
    else
    {
        json.insert( QStringLiteral( "reason" ),
                     reason.isEmpty() ? QString::fromUtf8( kLabGradeUnavailableReason )
                                      : reason );
    }
    return json;
}

// --- LabReportThumbnail --------------------------------------------------------

QJsonObject LabReportThumbnail::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "sourcePath" ), sourcePath );
    if ( sourceSizeBytes >= 0 )
        json.insert( QStringLiteral( "sourceSizeBytes" ),
                     static_cast<double>( sourceSizeBytes ) );
    if ( !error.isEmpty() )
    {
        json.insert( QStringLiteral( "renderError" ), error );
        return json;
    }
    json.insert( QStringLiteral( "dataUrl" ),
                 QStringLiteral( "data:image/png;base64,%1" )
                     .arg( QString::fromLatin1( pngBytes.toBase64() ) ) );
    json.insert( QStringLiteral( "sha256" ),
                 QString::fromLatin1(
                     QCryptographicHash::hash( pngBytes, QCryptographicHash::Sha256 ).toHex() ) );
    return json;
}

// --- LabReportBuilder ----------------------------------------------------------

LabReportBuilder::LabReportBuilder( const ExperimentStore &experimentStore,
                                    const DatasetStore *datasetStore )
    : m_store( &experimentStore )
    , m_datasetStore( datasetStore )
{
}

Result<QJsonObject> LabReportBuilder::build( const LabReportRequest &request ) const
{
    if ( !m_store->isOpen() )
        return Result<QJsonObject>::failure( Diagnostic{
            QStringLiteral( "lab.report_store_closed" ),
            QStringLiteral( "the experiment store is not open" ),
            sicnu::dataset::DiagnosticSeverity::Error } );

    const auto experiment = m_store->experimentById( request.labId );
    if ( !experiment )
    {
        return Result<QJsonObject>::failure( Diagnostic{
            QStringLiteral( "lab.report_no_experiment" ),
            QStringLiteral( "experiment %1 is unknown to the store" ).arg( request.labId ),
            sicnu::dataset::DiagnosticSeverity::Error } );
    }

    // Page through every run of the experiment (bounded page size, loop).
    QVector<ExperimentRun> runs;
    for ( qint64 offset = 0;; offset += m_store->kMaxPageSize )
    {
        auto page = m_store->listRuns( request.labId, QString(), QString(), offset,
                                       m_store->kMaxPageSize );
        if ( !page )
            return Result<QJsonObject>::failure( page.diagnostics().first() );
        runs += page.value().second;
        if ( page.value().second.size() < m_store->kMaxPageSize )
            break;
    }

    if ( runs.isEmpty() && request.operationTrail.isEmpty() )
    {
        return Result<QJsonObject>::failure( Diagnostic{
            QStringLiteral( "lab.report_empty" ),
            QStringLiteral( "no recorded runs and no operation trail for experiment %1" )
                .arg( request.labId ),
            sicnu::dataset::DiagnosticSeverity::Error } );
    }

    // Sort runs deterministically: finished → started → created, then runId.
    std::sort( runs.begin(), runs.end(), []( const ExperimentRun &a, const ExperimentRun &b )
    {
        const auto key = []( const ExperimentRun &run ) {
            if ( run.finishedAtUtc().isValid() )
                return run.finishedAtUtc();
            if ( run.startedAtUtc().isValid() )
                return run.startedAtUtc();
            return run.createdAtUtc();
        };
        const QDateTime ka = key( a );
        const QDateTime kb = key( b );
        if ( ka != kb )
            return ka < kb;
        return a.runId() < b.runId();
    } );

    auto primary = resolvePrimaryRun( request, runs );
    if ( !primary )
        return Result<QJsonObject>::failure( primary.diagnostics() );

    QJsonArray warnings;
    const QJsonArray steps = buildSteps( request, runs );

    // Statistics: the run's metric record verbatim (protocol + documents),
    // one entry per run in the (already deterministic) run order.
    QJsonArray statistics;
    for ( const ExperimentRun &run : runs )
    {
        if ( const auto record = m_store->metricRecordForRun( run.runId() ) )
            statistics.append( record->toJson() );
    }

    const QJsonArray thumbnails = buildThumbnails( request, warnings );

    const QJsonObject lineage = buildLineage( request, *primary );

    // Replay readiness is THE acceptance bar: assessed with the caller's
    // hooks; blockers are surfaced, never hidden.
    const ReplayReadinessReport readiness =
        ReplayReadiness::assess( *primary, m_datasetStore, request.hooks );
    QJsonObject replay;
    replay.insert( QStringLiteral( "level" ),
                   sicnu::dataset::reproductionLevelToString( readiness.level ) );
    QJsonArray checks;
    for ( const ReplayCheck &check : readiness.checks )
        checks.append( check.toJson() );
    replay.insert( QStringLiteral( "checks" ), checks );
    QJsonArray notes;
    for ( const QString &note : readiness.notes )
        notes.append( note );
    replay.insert( QStringLiteral( "notes" ), notes );
    QJsonArray blockers;
    for ( const QString &blocker : readiness.missingDependencyDiagnostics() )
        blockers.append( blocker );
    replay.insert( QStringLiteral( "blockers" ), blockers );

    // Environment: the run's redacted snapshot, re-redacted at the export
    // boundary (issue #789 defense-in-depth).
    QJsonObject environment;
    const RunEnvironment redactedEnvironment = primary->environment().redacted();
    environment.insert( QStringLiteral( "fields" ), redactedEnvironment.fields() );
    QJsonObject environmentVariables;
    for ( auto it = redactedEnvironment.envVariables().constBegin();
          it != redactedEnvironment.envVariables().constEnd(); ++it )
        environmentVariables.insert( it.key(), it.value() );
    environment.insert( QStringLiteral( "envVariables" ), environmentVariables );

    QJsonObject document;
    document.insert( QStringLiteral( "schema" ), QString::fromUtf8( kLabReportSchemaId ) );

    QJsonObject header;
    header.insert( QStringLiteral( "reportId" ),
                   QStringLiteral( "labreport-%1-%2" )
                       .arg( request.labId, primary->runId() ) );
    header.insert( QStringLiteral( "labId" ), request.labId );
    header.insert( QStringLiteral( "labName" ),
                   request.labName.isEmpty() ? experiment->name() : request.labName );
    header.insert(
        QStringLiteral( "objective" ),
        request.objective.isEmpty() ? experiment->objective() : request.objective );
    header.insert( QStringLiteral( "student" ), request.student );
    header.insert( QStringLiteral( "session" ), request.session );
    const QString generatedAt =
        request.generatedAtUtc.isEmpty()
            ? QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs )
            : request.generatedAtUtc;
    header.insert( QStringLiteral( "generatedAtUtc" ), generatedAt );
    header.insert( QStringLiteral( "softwareRevision" ),
                   request.softwareRevision.isEmpty()
                       ? unknownSafe( primary->softwareRevision() )
                       : request.softwareRevision );
    header.insert( QStringLiteral( "gitSha" ), unknownSafe( request.gitSha ) );
    document.insert( QStringLiteral( "header" ), header );

    QJsonArray runArray;
    for ( const ExperimentRun &run : runs )
        runArray.append( runToJson( run ) );
    document.insert( QStringLiteral( "runs" ), runArray );
    document.insert( QStringLiteral( "steps" ), steps );
    document.insert( QStringLiteral( "statistics" ), statistics );
    document.insert( QStringLiteral( "thumbnails" ), thumbnails );
    document.insert( QStringLiteral( "grade" ), request.grade.toJson() );
    document.insert( QStringLiteral( "lineage" ), lineage );
    document.insert( QStringLiteral( "environment" ), environment );
    document.insert( QStringLiteral( "replay" ), replay );
    if ( !warnings.isEmpty() )
        document.insert( QStringLiteral( "warnings" ), warnings );

    const auto validated = validate( document );
    if ( !validated )
        return Result<QJsonObject>::failure( validated.diagnostics() );
    return Result<QJsonObject>::success( document );
}

Result<ExperimentRun> LabReportBuilder::resolvePrimaryRun(
    const LabReportRequest &request, const QVector<ExperimentRun> &runs ) const
{
    if ( runs.isEmpty() )
    {
        return Result<ExperimentRun>::failure( Diagnostic{
            QStringLiteral( "lab.report_no_runs" ),
            QStringLiteral( "no recorded runs; the operation trail alone has no run to anchor "
                            "lineage and replay to" ),
            sicnu::dataset::DiagnosticSeverity::Error } );
    }
    if ( !request.runId.isEmpty() )
    {
        for ( const ExperimentRun &run : runs )
            if ( run.runId() == request.runId )
                return Result<ExperimentRun>::success( run );
        return Result<ExperimentRun>::failure( Diagnostic{
            QStringLiteral( "lab.report_run_not_found" ),
            QStringLiteral( "run %1 is not a run of experiment %2" )
                .arg( request.runId, request.labId ),
            sicnu::dataset::DiagnosticSeverity::Error } );
    }
    // `runs` is already in deterministic order; the primary is the LAST entry
    // (most recently finished/started/created).
    return Result<ExperimentRun>::success( runs.last() );
}

QJsonArray LabReportBuilder::buildSteps( const LabReportRequest &request,
                                         const QVector<ExperimentRun> &runs ) const
{
    // Operator-trail records, redacted, bounded, and joined to runs by the
    // DECLARED attribution policy. The trail carries no run identity — this
    // join is an attribution (time-window + operator name), and the document
    // says so next to every step.
    struct IndexedStep
    {
        QJsonObject step;
        QString startTimeIso;
        int originalIndex = 0;
    };
    QVector<IndexedStep> indexed;
    indexed.reserve( request.operationTrail.size() );
    int index = 0;
    for ( const QJsonValue &value : request.operationTrail )
    {
        const QJsonObject record = RunEnvironment::redactSecretKeys( value.toObject() );
        QJsonObject step;
        step.insert( QStringLiteral( "index" ), index );
        step.insert( QStringLiteral( "operator" ),
                     record.value( QStringLiteral( "operatorName" ) ).toString() );
        step.insert( QStringLiteral( "params" ),
                     record.value( QStringLiteral( "parameters" ) ) );
        step.insert( QStringLiteral( "result" ), record.value( QStringLiteral( "result" ) ) );
        step.insert( QStringLiteral( "success" ),
                     record.value( QStringLiteral( "success" ) ).toBool( false ) );
        const int errorCode = record.value( QStringLiteral( "errorCode" ) ).toInt( 0 );
        if ( errorCode != 0 )
            step.insert( QStringLiteral( "errorCode" ), errorCode );
        const QString errorMessage =
            record.value( QStringLiteral( "errorMessage" ) ).toString();
        if ( !errorMessage.isEmpty() )
            step.insert( QStringLiteral( "errorMessage" ), errorMessage );
        const QString startTime = record.value( QStringLiteral( "startTimeIso" ) ).toString();
        step.insert( QStringLiteral( "startedAtIso" ), startTime );
        const QString endTime = record.value( QStringLiteral( "endTimeIso" ) ).toString();
        step.insert( QStringLiteral( "endedAtIso" ), endTime );
        step.insert( QStringLiteral( "durationMs" ),
                     record.value( QStringLiteral( "durationMs" ) ).toDouble( 0.0 ) );

        // Attribution: the run whose execution window contains the record's
        // start. Zero or several candidate runs are reported as unattributed
        // / ambiguous — never silently assigned.
        const QDateTime started = parseUtc( startTime );
        QJsonObject attribution;
        attribution.insert( QStringLiteral( "policy" ),
                            QString::fromUtf8( kLabStepAttributionPolicy ) );
        if ( !started.isValid() )
        {
            attribution.insert( QStringLiteral( "runId" ), QJsonValue::Null );
            attribution.insert( QStringLiteral( "quality" ), QStringLiteral( "unparsed-time" ) );
        }
        else
        {
            QStringList candidates;
            for ( const ExperimentRun &run : runs )
            {
                const QDateTime start = run.startedAtUtc();
                const QDateTime finish = run.finishedAtUtc();
                if ( !start.isValid() )
                    continue;
                if ( started < start )
                    continue;
                if ( finish.isValid() && started > finish )
                    continue;
                candidates << run.runId();
            }
            if ( candidates.isEmpty() )
            {
                attribution.insert( QStringLiteral( "runId" ), QJsonValue::Null );
                attribution.insert( QStringLiteral( "quality" ), QStringLiteral( "unattributed" ) );
            }
            else if ( candidates.size() > 1 )
            {
                attribution.insert( QStringLiteral( "runId" ), QJsonValue::Null );
                attribution.insert( QStringLiteral( "quality" ), QStringLiteral( "ambiguous" ) );
                attribution.insert( QStringLiteral( "candidates" ),
                                    QJsonArray::fromStringList( candidates ) );
            }
            else
            {
                attribution.insert( QStringLiteral( "runId" ), candidates.first() );
                attribution.insert( QStringLiteral( "quality" ), QStringLiteral( "exact" ) );
            }
        }
        step.insert( QStringLiteral( "attribution" ), attribution );

        indexed.append( IndexedStep{ step, startTime, index } );
        ++index;
    }

    std::stable_sort( indexed.begin(), indexed.end(),
                      []( const IndexedStep &a, const IndexedStep &b )
                      {
                          if ( a.startTimeIso != b.startTimeIso )
                              return a.startTimeIso < b.startTimeIso;
                          return a.originalIndex < b.originalIndex;
                      } );

    QJsonArray steps;
    for ( const IndexedStep &entry : indexed )
        steps.append( entry.step );
    return steps;
}

QJsonArray LabReportBuilder::buildThumbnails( const LabReportRequest &request,
                                              QJsonArray &warnings ) const
{
    // Bounds enforced HERE (not trusted from the caller): parse the PNG IHDR
    // for the real pixel size; anything over 512 px on the long edge, or not
    // a PNG at all, is refused with a warning — never embedded at full size.
    constexpr int kMaxThumbnailEdge = 512;
    QJsonArray thumbnails;
    for ( const LabReportThumbnail &thumbnail : request.thumbnails )
    {
        QJsonObject json = thumbnail.toJson();
        if ( thumbnail.error.isEmpty() )
        {
            const QByteArray &png = thumbnail.pngBytes;
            static constexpr char kPngMagic[] = { '\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n' };
            const bool isPng = png.size() >= 24
                               && std::equal( png.cbegin(), png.cbegin() + 8, kPngMagic );
            long long width = -1;
            long long height = -1;
            if ( isPng )
            {
                // IHDR: bytes 16..19 big-endian width, 20..23 height.
                for ( int i = 0; i < 4; ++i )
                {
                    width = ( width < 0 ? 0 : width ) << 8
                            | static_cast<unsigned char>( png.at( 16 + i ) );
                    height = ( height < 0 ? 0 : height ) << 8
                             | static_cast<unsigned char>( png.at( 20 + i ) );
                }
            }
            if ( !isPng )
            {
                warnings.append( QStringLiteral( "thumbnail for %1 is not a PNG; not embedded" )
                                     .arg( thumbnail.sourcePath ) );
                continue;
            }
            if ( qMax( width, height ) > kMaxThumbnailEdge )
            {
                warnings.append(
                    QStringLiteral( "thumbnail for %1 is %2x%3 px; the %4 px long-edge bound "
                                    "was violated by the provider and it was NOT embedded" )
                        .arg( thumbnail.sourcePath )
                        .arg( width )
                        .arg( height )
                        .arg( kMaxThumbnailEdge ) );
                continue;
            }
            json.insert( QStringLiteral( "widthPx" ), static_cast<double>( width ) );
            json.insert( QStringLiteral( "heightPx" ), static_cast<double>( height ) );
        }
        thumbnails.append( json );
    }
    return thumbnails;
}

QJsonObject LabReportBuilder::buildLineage( const LabReportRequest &request,
                                            const ExperimentRun &primaryRun ) const
{
    const LineageNodeId start{ QStringLiteral( "run" ), primaryRun.runId() };
    if ( m_datasetStore )
    {
        // Full joined graph over dataset + experiment stores; existence
        // resolution is only as good as the resolver, which this track does
        // not wire — dangling flags are therefore "not checked" (the graph
        // never fabricates tombstones it cannot verify).
        LineageGraph graph( *m_datasetStore, *m_store );
        LineageQueryResult result = graph.ancestors( start, request.lineageMaxDepth,
                                                     request.lineageMaxNodes );
        QJsonObject lineage = result.toJson();
        // The traversal walks hash-container edges: re-sort both arrays so
        // the document does not inherit QHash iteration order. (QJsonArray
        // iterators are non-swappable proxies — sort a QVector copy.)
        auto sortItems = []( QJsonArray &array, const QStringList &keys )
        {
            QVector<QJsonObject> items;
            items.reserve( array.size() );
            for ( const QJsonValue &value : array )
                items.append( value.toObject() );
            std::sort( items.begin(), items.end(),
                       [ &keys ]( const QJsonObject &a, const QJsonObject &b )
                       {
                           for ( const QString &key : keys )
                           {
                               const QString va = a.value( key ).toString();
                               const QString vb = b.value( key ).toString();
                               if ( va != vb )
                                   return va < vb;
                           }
                           return false;
                       } );
            QJsonArray sorted;
            for ( const QJsonObject &item : items )
                sorted.append( item );
            array = sorted;
        };
        QJsonArray nodes = lineage.value( QStringLiteral( "nodes" ) ).toArray();
        sortItems( nodes, { QStringLiteral( "kind" ), QStringLiteral( "id" ) } );
        lineage.insert( QStringLiteral( "nodes" ), nodes );
        QJsonArray edges = lineage.value( QStringLiteral( "edges" ) ).toArray();
        sortItems( edges, { QStringLiteral( "from_kind" ), QStringLiteral( "from_id" ),
                            QStringLiteral( "edge" ), QStringLiteral( "to_kind" ),
                            QStringLiteral( "to_id" ) } );
        lineage.insert( QStringLiteral( "edges" ), edges );
        lineage.insert( QStringLiteral( "startKind" ), start.kind );
        lineage.insert( QStringLiteral( "startId" ), start.id );
        lineage.insert( QStringLiteral( "existence" ), QStringLiteral( "not-checked" ) );
        if ( result.budgetExhausted )
            lineage.insert( QStringLiteral( "budgetExhausted" ), true );
        return lineage;
    }

    // No dataset store wired: the experiment-store edges around the run are
    // still truthful (smaller) — existence explicitly unchecked, never implied.
    QJsonObject lineage;
    lineage.insert( QStringLiteral( "startKind" ), start.kind );
    lineage.insert( QStringLiteral( "startId" ), start.id );
    lineage.insert( QStringLiteral( "existence" ), QStringLiteral( "not-checked" ) );
    lineage.insert( QStringLiteral( "source" ), QStringLiteral( "experiment-edges-only" ) );

    QJsonArray nodeArray;
    QJsonObject startNode;
    startNode.insert( QStringLiteral( "kind" ), start.kind );
    startNode.insert( QStringLiteral( "id" ), start.id );
    startNode.insert( QStringLiteral( "dangling" ), false );
    startNode.insert( QStringLiteral( "depth" ), 0 );
    nodeArray.append( startNode );

    QJsonArray edgeArray;
    auto addEdge = [&edgeArray]( const ExperimentStore::LineageEdge &edge ) {
        QJsonObject item;
        item.insert( QStringLiteral( "from_kind" ), edge.fromKind );
        item.insert( QStringLiteral( "from_id" ), edge.fromId );
        item.insert( QStringLiteral( "edge" ), edge.edgeKind );
        item.insert( QStringLiteral( "to_kind" ), edge.toKind );
        item.insert( QStringLiteral( "to_id" ), edge.toId );
        edgeArray.append( item );
    };

    // Deterministic edge order: outgoing then incoming, each sorted by the
    // endpoint tuple (the store returns rows in insertion order).
    QVector<ExperimentStore::LineageEdge> edges = m_store->outgoingEdges( start.kind, start.id );
    edges += m_store->incomingEdges( start.kind, start.id );
    std::sort( edges.begin(), edges.end(), []( const ExperimentStore::LineageEdge &a,
                                               const ExperimentStore::LineageEdge &b ) {
        const auto key = []( const ExperimentStore::LineageEdge &edge ) {
            return std::make_tuple( edge.fromKind, edge.fromId, edge.edgeKind, edge.toKind,
                                    edge.toId );
        };
        return key( a ) < key( b );
    } );
    QSet<QString> seenEdges;
    for ( const ExperimentStore::LineageEdge &edge : edges )
    {
        const QString key = QStringLiteral( "%1:%2|%3|%4:%5" )
                                .arg( edge.fromKind, edge.fromId, edge.edgeKind, edge.toKind,
                                      edge.toId );
        if ( seenEdges.contains( key ) )
            continue; // incoming scan can repeat an edge the outgoing scan had
        seenEdges.insert( key );
        addEdge( edge );
        for ( const auto &[ kind, id ] :
              { std::pair{ edge.fromKind, edge.fromId }, std::pair{ edge.toKind, edge.toId } } )
        {
            QJsonObject node;
            node.insert( QStringLiteral( "kind" ), kind );
            node.insert( QStringLiteral( "id" ), id );
            node.insert( QStringLiteral( "dangling" ), false );
            node.insert( QStringLiteral( "depth" ), 1 );
            nodeArray.append( node );
        }
    }

    lineage.insert( QStringLiteral( "nodes" ), nodeArray );
    lineage.insert( QStringLiteral( "edges" ), edgeArray );
    return lineage;
}

// --- validate -------------------------------------------------------------------

Result<void> LabReportBuilder::validate( const QJsonObject &document )
{
    const auto fail = []( const QString &message ) {
        return Result<void>::failure( Diagnostic{ QStringLiteral( "lab.report_invalid_schema" ),
                                                  message,
                                                  sicnu::dataset::DiagnosticSeverity::Error } );
    };

    if ( document.value( QStringLiteral( "schema" ) ).toString()
         != QString::fromUtf8( kLabReportSchemaId ) )
        return fail( QStringLiteral( "schema must be %1" ).arg( kLabReportSchemaId ) );

    static const QStringList requiredSections{
        QStringLiteral( "header" ), QStringLiteral( "runs" ),   QStringLiteral( "steps" ),
        QStringLiteral( "statistics" ), QStringLiteral( "thumbnails" ), QStringLiteral( "grade" ),
        QStringLiteral( "lineage" ), QStringLiteral( "environment" ), QStringLiteral( "replay" ),
    };
    for ( const QString &section : requiredSections )
        if ( !document.contains( section ) )
            return fail( QStringLiteral( "missing section %1" ).arg( section ) );

    const QJsonObject header = document.value( QStringLiteral( "header" ) ).toObject();
    static const QStringList requiredHeader{
        QStringLiteral( "reportId" ),    QStringLiteral( "labId" ),
        QStringLiteral( "labName" ),     QStringLiteral( "objective" ),
        QStringLiteral( "student" ),     QStringLiteral( "session" ),
        QStringLiteral( "generatedAtUtc" ), QStringLiteral( "softwareRevision" ),
        QStringLiteral( "gitSha" ),
    };
    for ( const QString &field : requiredHeader )
        if ( !header.contains( field ) )
            return fail( QStringLiteral( "header.%1 missing" ).arg( field ) );
    if ( parseUtc( header.value( QStringLiteral( "generatedAtUtc" ) ).toString() ).isNull() )
        return fail( QStringLiteral( "header.generatedAtUtc is not ISO 8601" ) );

    const QJsonObject grade = document.value( QStringLiteral( "grade" ) ).toObject();
    const QString gradeStatus = grade.value( QStringLiteral( "status" ) ).toString();
    if ( gradeStatus != QStringLiteral( "recorded" )
         && gradeStatus != QStringLiteral( "unavailable" ) )
        return fail( QStringLiteral( "grade.status must be recorded|unavailable" ) );
    if ( gradeStatus == QStringLiteral( "recorded" )
         && grade.value( QStringLiteral( "gradingRef" ) ).toString().isEmpty() )
        return fail( QStringLiteral( "grade.gradingRef is required when recorded" ) );

    const QJsonObject replay = document.value( QStringLiteral( "replay" ) ).toObject();
    static const QStringList replayLevels{
        QStringLiteral( "exact" ),     QStringLiteral( "compatible" ),
        QStringLiteral( "best_effort" ), QStringLiteral( "impossible" ),
    };
    if ( !replayLevels.contains( replay.value( QStringLiteral( "level" ) ).toString() ) )
        return fail( QStringLiteral( "replay.level is not a known level" ) );

    const QJsonArray steps = document.value( QStringLiteral( "steps" ) ).toArray();
    for ( const QJsonValue &value : steps )
    {
        const QJsonObject step = value.toObject();
        if ( step.value( QStringLiteral( "attribution" ) ).toObject()
                 .value( QStringLiteral( "policy" ) )
                 .toString()
             != QString::fromUtf8( kLabStepAttributionPolicy ) )
            return fail( QStringLiteral( "every step must declare the attribution policy" ) );
    }

    const QJsonArray thumbnails = document.value( QStringLiteral( "thumbnails" ) ).toArray();
    for ( const QJsonValue &value : thumbnails )
    {
        const QJsonObject thumbnail = value.toObject();
        if ( thumbnail.contains( QStringLiteral( "renderError" ) ) )
            continue;
        const int width = thumbnail.value( QStringLiteral( "widthPx" ) ).toInt( -1 );
        const int height = thumbnail.value( QStringLiteral( "heightPx" ) ).toInt( -1 );
        if ( width < 0 || height < 0 || qMax( width, height ) > 512 )
            return fail( QStringLiteral( "embedded thumbnails must declare ≤512 px edges" ) );
        if ( !thumbnail.value( QStringLiteral( "dataUrl" ) )
                   .toString()
                   .startsWith( QStringLiteral( "data:image/png;base64," ) ) )
            return fail( QStringLiteral( "thumbnails must be inline PNG data URLs" ) );
    }
    return Result<void>::success();
}

} // namespace sicnu::experiment
