// evidence_source.cpp — evidence providers (RS14-06, ADR 0174).
//
// Slice A GREEN: InMemory + Directory sources and the two shared
// normalizers. Recorded wire shapes are interpreted ONLY here and in
// snapshot normalization — never re-implemented by callers.

#include "evidence_source.h"

#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_provenance.h"
#include "workflow/workflow_run.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>

namespace sicnu::experiment::debugger
{

using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::Result;

namespace
{

constexpr qint64 kMaxProvenanceFileBytes = 16 * 1024 * 1024; // platform-written docs are far smaller
constexpr int kMaxAttemptDirectories = 32;

Diagnostic typedFailure( const char *code, const QString &message,
                         DiagnosticSeverity severity = DiagnosticSeverity::Error )
{
    return { QLatin1String( code ), message, severity };
}

/// Strips a provenance node-id prefix ("node:<id>" / "artifact:<path>").
QString stripNodePrefix( const QString &id )
{
    const int colon = id.indexOf( QLatin1Char( ':' ) );
    return colon >= 0 ? id.mid( colon + 1 ) : id;
}

/// Deterministic topological order (Kahn, lexicographic tiebreak).
/// Fails (returns false) on a cycle — recorded pipelines are acyclic; a
/// cycle in evidence means the record is corrupt, not that order is a
/// free choice.
bool orderTopologically( QVector<StepSnapshot> &steps )
{
    QHash<QString, int> indexById;
    for ( int i = 0; i < steps.size(); ++i )
        indexById.insert( steps.at( i ).stepId, i );

    QVector<int> inDegree( steps.size(), 0 );
    QVector<QVector<int>> dependents( steps.size() );
    for ( int i = 0; i < steps.size(); ++i )
    {
        for ( const QString &dep : steps.at( i ).dependencies )
        {
            const auto it = indexById.constFind( dep );
            if ( it == indexById.constEnd() )
                continue; // unknown dep handled by the caller (strict modes reject)
            // dep is an upstream of i: edge dep -> i
            dependents[ *it ].append( i );
            ++inDegree[ i ];
        }
    }

    auto byId = [ & ]( int a, int b ) { return steps.at( a ).stepId < steps.at( b ).stepId; };
    QVector<int> ready;
    for ( int i = 0; i < steps.size(); ++i )
        if ( inDegree.at( i ) == 0 )
            ready.append( i );
    std::sort( ready.begin(), ready.end(), byId );

    QVector<StepSnapshot> ordered;
    ordered.reserve( steps.size() );
    while ( !ready.isEmpty() )
    {
        const int current = ready.takeFirst();
        ordered.append( steps.at( current ) );
        for ( int dependent : dependents.at( current ) )
            if ( --inDegree[ dependent ] == 0 )
            {
                auto at = std::upper_bound( ready.begin(), ready.end(), dependent,
                                            [ & ]( int lhs, int rhs ) {
                                                return byId( lhs, rhs );
                                            } );
                ready.insert( at, dependent );
            }
    }
    if ( ordered.size() != steps.size() )
        return false; // cycle or dangling edges — corrupt evidence
    steps = ordered;
    return true;
}

/// jsoncpp → QJson for checkpoint step plans (resolved params). Platform
/// produced values only; depth comes from the writer's own schema.
QJsonObject jsonCppToQJson( const Json::Value &value )
{
    QJsonObject out;
    for ( const auto &key : value.getMemberNames() )
    {
        const Json::Value &member = value[ key ];
        switch ( member.type() )
        {
            case Json::objectValue:
                out.insert( QString::fromStdString( key ), jsonCppToQJson( member ) );
                break;
            case Json::stringValue:
                out.insert( QString::fromStdString( key ),
                            QString::fromStdString( member.asString() ) );
                break;
            case Json::intValue:
                // QJsonValue has no int64 slot; |value| <= 2^53 round-trips
                // exactly through double, which covers every recorded
                // parameter magnitude the platform writes.
                out.insert( QString::fromStdString( key ),
                            static_cast<double>( member.asInt64() ) );
                break;
            case Json::uintValue:
                out.insert( QString::fromStdString( key ),
                            static_cast<double>( member.asUInt64() ) );
                break;
            case Json::realValue:
                out.insert( QString::fromStdString( key ), member.asDouble() );
                break;
            case Json::booleanValue:
                out.insert( QString::fromStdString( key ), member.asBool() );
                break;
            case Json::nullValue:
            default:
                break;
        }
    }
    return out;
}

/// Checkpoint aggregate → StepEvidence (CheckpointSteps mode). Fails typed
/// on dangling dependencies: a checkpoint that references an unknown parent
/// step is corrupt evidence, not a quirk to normalize away.
Result<StepEvidence> stepEvidenceFromCheckpointRun( const workflow::WorkflowRun &run )
{
    StepEvidence evidence;
    evidence.mode = StepEvidenceMode::CheckpointSteps;

    QSet<QString> knownStepIds;
    for ( const workflow::StepPlan &plan : run.stepPlans() )
        knownStepIds.insert( QString::fromStdString( plan.stepId ) );

    for ( const workflow::StepPlan &plan : run.stepPlans() )
    {
        StepSnapshot step;
        step.stepId = QString::fromStdString( plan.stepId );
        step.operatorId = QString::fromStdString( plan.operatorId );
        step.parameters = jsonCppToQJson( plan.resolvedParams );
        step.lineageSignature = QString::fromStdString( plan.fingerprint );
        step.status = QString::fromStdString( plan.status );
        step.cacheHit = plan.cacheHit;
        step.cacheHitKnown = true;
        for ( const std::string &dependency : plan.dependencies )
            step.dependencies << QString::fromStdString( dependency );
        step.dependencies.removeDuplicates();
        step.dependencies.sort();
        for ( const QString &dependency : step.dependencies )
            if ( !knownStepIds.contains( dependency ) )
                return Result<StepEvidence>::failure( typedFailure(
                    kCodeMalformedEvidence,
                    QStringLiteral( "checkpoint step '%1' depends on unknown step '%2'" )
                        .arg( step.stepId, dependency ) ) );
        const DigestRecord digest =
            classifyRecordedDigest( QString::fromStdString( plan.outputDigest ) );
        step.outputDigest = digest.digest;
        step.digestMode = digest.mode;
        step.outputSizeBytes = plan.outputSizeBytes > 0 ? plan.outputSizeBytes : -1;
        step.errorMessage = QString::fromStdString( plan.errorMessage );
        evidence.steps.append( step );

        if ( !step.outputDigest.isEmpty() || !plan.outputLayerPath.empty() )
        {
            ArtifactSnapshot artifact;
            artifact.artifactId = QString::fromStdString( plan.outputLayerPath );
            if ( artifact.artifactId.isEmpty() )
                artifact.artifactId = QStringLiteral( "%1@output" ).arg( step.stepId );
            artifact.digest = digest.digest;
            artifact.digestMode = digest.mode;
            artifact.sizeBytes = step.outputSizeBytes;
            artifact.rootInput = false;
            artifact.producerStepId = step.stepId;
            evidence.artifacts.append( artifact );
        }
    }

    if ( !orderTopologically( evidence.steps ) )
        return Result<StepEvidence>::failure( typedFailure(
            kCodeMalformedEvidence,
            QStringLiteral( "checkpoint step plans form a cyclic dependency graph" ) ) );
    return Result<StepEvidence>::success( evidence );
}

} // namespace

// --- Normalizers (recorded wire shapes are interpreted ONLY here) -------------

Result<StepEvidence> stepEvidenceFromProvenanceDoc( const QJsonObject &doc )
{
    auto graphResult = workflow::ProvenanceGraph::fromJson( doc );
    if ( !graphResult.isSuccess() )
        return Result<StepEvidence>::failure( typedFailure(
            kCodeMalformedEvidence,
            QStringLiteral( "provenance document rejected: %1" ).arg( graphResult.error() ) ) );
    const workflow::ProvenanceGraph &graph = graphResult.value();

    StepEvidence evidence;
    evidence.mode = StepEvidenceMode::ProvenanceGraph;

    QHash<QString, int> stepIndexById;   // plain node id → index into evidence.steps
    QHash<QString, int> artifactIndexById;
    QHash<QString, QString> producerOfArtifact; // artifact node id → producing node id
    QHash<QString, QString> producedByExec;     // exec node id → produced/reused artifact node id

    for ( const workflow::ProvenanceNode &node : graph.nodes() )
    {
        if ( node.kind == QLatin1String( "run" ) )
        {
            evidence.planSignature = node.attributes.value( QLatin1String( "planSignature" ) ).toString();
        }
        else if ( node.kind == QLatin1String( "nodeExec" ) )
        {
            StepSnapshot step;
            step.stepId = node.attributes.value( QLatin1String( "nodeId" ) ).toString();
            if ( step.stepId.isEmpty() )
                step.stepId = stripNodePrefix( node.id );
            step.operatorId = node.attributes.value( QLatin1String( "operatorId" ) ).toString();
            step.status = node.attributes.value( QLatin1String( "state" ) ).toString();
            step.lineageSignature =
                node.attributes.value( QLatin1String( "lineageSignature" ) ).toString();
            step.cacheHit = node.attributes.value( QLatin1String( "isCacheHit" ) ).toBool( false );
            step.cacheHitKnown = true;
            step.errorMessage = node.attributes.value( QLatin1String( "errorMessage" ) ).toString();
            // Provenance graphs record no parameters: parameters and
            // paramsHash stay honestly empty (never invented here).
            stepIndexById.insert( node.id, evidence.steps.size() );
            evidence.steps.append( step );
        }
        else if ( node.kind == QLatin1String( "artifact" ) )
        {
            ArtifactSnapshot artifact;
            artifact.artifactId = node.attributes.value( QLatin1String( "path" ) ).toString();
            if ( artifact.artifactId.isEmpty() )
                artifact.artifactId = stripNodePrefix( node.id );
            const DigestRecord digest = classifyRecordedDigest(
                node.attributes.value( QLatin1String( "fingerprint" ) ).toString() );
            artifact.digest = digest.digest;
            artifact.digestMode = digest.mode;
            artifact.sizeBytes = static_cast<qint64>(
                node.attributes.value( QLatin1String( "sizeBytes" ) ).toDouble( -1 ) );
            artifactIndexById.insert( node.id, evidence.artifacts.size() );
            evidence.artifacts.append( artifact );
        }
    }

    if ( evidence.steps.size() > kMaxSnapshotSteps )
        return Result<StepEvidence>::failure( typedFailure(
            kCodeEvidenceTooLarge,
            QStringLiteral( "%1 nodeExecs exceed the snapshot cap of %2" )
                .arg( evidence.steps.size() ).arg( kMaxSnapshotSteps ) ) );
    if ( evidence.artifacts.size() > kMaxSnapshotArtifacts )
        return Result<StepEvidence>::failure( typedFailure(
            kCodeEvidenceTooLarge,
            QStringLiteral( "%1 artifacts exceed the snapshot cap of %2" )
                .arg( evidence.artifacts.size() ).arg( kMaxSnapshotArtifacts ) ) );

    const QString producedKind = QStringLiteral( "produced" );
    const QString consumedKind = QStringLiteral( "consumed" );
    const QString reusedKind = QStringLiteral( "reusedFrom" );
    for ( const workflow::ProvenanceEdge &edge : graph.edges() )
    {
        if ( edge.kind == producedKind || edge.kind == reusedKind )
        {
            producerOfArtifact.insert( edge.toId, edge.fromId );
            producedByExec.insert( edge.fromId, edge.toId );
        }
        else if ( edge.kind == consumedKind )
        {
            const auto consumer = stepIndexById.constFind( edge.fromId );
            const auto artifact = artifactIndexById.constFind( edge.toId );
            if ( consumer == stepIndexById.constEnd() || artifact == artifactIndexById.constEnd() )
                continue; // strict parse already refused dangling edges
            const QString producer = producerOfArtifact.value( edge.toId );
            const auto producerIndex = producer.isEmpty()
                                           ? stepIndexById.end()
                                           : stepIndexById.constFind( producer );
            if ( producerIndex != stepIndexById.constEnd() )
            {
                StepSnapshot &step = evidence.steps[ *consumer ];
                if ( !step.dependencies.contains( evidence.steps.at( *producerIndex ).stepId ) )
                    step.dependencies.append( evidence.steps.at( *producerIndex ).stepId );
            }
            else
            {
                // Consumed with no in-run producer: external input state.
                evidence.artifacts[ *artifact ].rootInput = true;
            }
        }
    }

    // Produced/reused artifact identity flows onto the producing step.
    for ( auto it = producedByExec.constBegin(); it != producedByExec.constEnd(); ++it )
    {
        const auto exec = stepIndexById.constFind( it.key() );
        const auto artifact = artifactIndexById.constFind( it.value() );
        if ( exec == stepIndexById.constEnd() || artifact == artifactIndexById.constEnd() )
            continue;
        StepSnapshot &step = evidence.steps[ *exec ];
        step.outputDigest = evidence.artifacts.at( *artifact ).digest;
        step.digestMode = evidence.artifacts.at( *artifact ).digestMode;
        step.outputSizeBytes = evidence.artifacts.at( *artifact ).sizeBytes;
        evidence.artifacts[ *artifact ].producerStepId = step.stepId;
    }

    for ( StepSnapshot &step : evidence.steps )
        step.dependencies.sort();

    if ( !orderTopologically( evidence.steps ) )
        return Result<StepEvidence>::failure( typedFailure(
            kCodeMalformedEvidence,
            QStringLiteral( "provenance nodeExec graph is cyclic" ) ) );
    return Result<StepEvidence>::success( evidence );
}

Result<StepEvidence> stepEvidenceFromBridgeWorkflowMetrics( const QJsonObject &workflow )
{
    StepEvidence evidence;
    evidence.mode = StepEvidenceMode::StepsEvidence;
    const QJsonArray steps = workflow.value( QLatin1String( "steps" ) ).toArray();
    for ( const QJsonValue &value : steps )
    {
        const QJsonObject entry = value.toObject();
        StepSnapshot step;
        step.stepId = entry.value( QLatin1String( "id" ) ).toString();
        if ( step.stepId.isEmpty() )
            return Result<StepEvidence>::failure( typedFailure(
                kCodeMalformedEvidence,
                QStringLiteral( "bridge step summary carries no id" ) ) );
        step.operatorId = entry.value( QLatin1String( "operator" ) ).toString();
        step.status = entry.value( QLatin1String( "status" ) ).toString();
        step.errorMessage = entry.value( QLatin1String( "error" ) ).toString();
        const QJsonObject output = entry.value( QLatin1String( "output" ) ).toObject();
        const DigestRecord digest =
            classifyRecordedDigest( output.value( QLatin1String( "digest" ) ).toString() );
        step.outputDigest = digest.digest;
        step.digestMode = digest.mode;
        step.outputSizeBytes = static_cast<qint64>(
            output.value( QLatin1String( "size" ) ).toDouble( -1 ) );
        // Bridge summaries record neither parameters nor topology: those
        // stay empty; the mode tells downstream analysis what is knowable.
        evidence.steps.append( step );
    }
    // Recorded order is the bridge's execution order — keep, never reorder.
    return Result<StepEvidence>::success( evidence );
}

// --- InMemoryEvidenceSource ---------------------------------------------------

void InMemoryEvidenceSource::insertRun( const ExperimentRun &run )
{
    m_runs.insert( run.runId(), run.toJson() );
}

void InMemoryEvidenceSource::insertRunJson( const QString &runId, const QJsonObject &runJson )
{
    m_runs.insert( runId, runJson );
}

void InMemoryEvidenceSource::insertStepEvidence( const QString &runId, const StepEvidence &evidence )
{
    m_steps.insert( runId, evidence );
    m_provenanceDocs.remove( runId );
}

void InMemoryEvidenceSource::insertProvenanceDoc( const QString &runId, const QJsonObject &doc )
{
    m_provenanceDocs.insert( runId, doc );
    m_steps.remove( runId );
}

Result<ExperimentRun> InMemoryEvidenceSource::run( const QString &runId )
{
    const auto it = m_runs.constFind( runId );
    if ( it == m_runs.constEnd() )
        return Result<ExperimentRun>::failure(
            typedFailure( kCodeUnknownRun, QStringLiteral( "no recorded run '%1'" ).arg( runId ) ) );
    auto parsed = ExperimentRun::fromJson( it.value() );
    if ( !parsed )
        return Result<ExperimentRun>::failure( typedFailure(
            kCodeMalformedEvidence,
            QStringLiteral( "stored run '%1' does not parse: %2" )
                .arg( runId, parsed.diagnostics().front().message ) ) );
    return parsed;
}

Result<StepEvidence> InMemoryEvidenceSource::steps( const QString &runId )
{
    const auto prov = m_provenanceDocs.constFind( runId );
    if ( prov != m_provenanceDocs.constEnd() )
        return stepEvidenceFromProvenanceDoc( prov.value() );
    const auto it = m_steps.constFind( runId );
    if ( it == m_steps.constEnd() )
        return Result<StepEvidence>::failure(
            typedFailure( kCodeEvidenceAbsent,
                          QStringLiteral( "no step evidence recorded for run '%1'" ).arg( runId ) ) );
    return Result<StepEvidence>::success( it.value() );
}

// --- DirectoryEvidenceSource ----------------------------------------------------

DirectoryEvidenceSource::DirectoryEvidenceSource( ExperimentStore *store, const QString &runDirectory )
    : m_store( store )
    , m_runDirectory( runDirectory )
{
}

Result<ExperimentRun> DirectoryEvidenceSource::run( const QString &runId )
{
    if ( !m_store )
        return Result<ExperimentRun>::failure(
            typedFailure( kCodeUnknownRun, QStringLiteral( "no recorded run '%1'" ).arg( runId ) ) );
    auto stored = m_store->runById( runId );
    if ( !stored )
        return Result<ExperimentRun>::failure(
            typedFailure( kCodeUnknownRun, QStringLiteral( "no recorded run '%1'" ).arg( runId ) ) );
    return Result<ExperimentRun>::success( *stored );
}

namespace
{

/// Candidate files for one record name: attempt-<N> subdirectories (highest
/// attempt = latest state) first, then the run directory root. Bounded scan.
QStringList candidatePaths( const QString &runDirectory, const QString &fileName )
{
    QStringList candidates;
    QDir dir( runDirectory );
    if ( !dir.exists() )
        return candidates;
    const QRegularExpression attemptDir( QStringLiteral( "^attempt-(\\d+)$" ) );
    QVector<QPair<int, QString>> attempts;
    const QStringList entries = dir.entryList( QDir::Dirs | QDir::NoDotAndDotDot );
    for ( const QString &entry : entries )
    {
        const auto match = attemptDir.match( entry );
        if ( match.hasMatch() )
            attempts.append( { match.captured( 1 ).toInt(), entry } );
    }
    std::sort( attempts.begin(), attempts.end(),
               []( const QPair<int, QString> &a, const QPair<int, QString> &b ) {
                   return a.first > b.first;
               } );
    int considered = 0;
    for ( const auto &attempt : attempts )
    {
        if ( ++considered > kMaxAttemptDirectories )
            break;
        const QString candidate = dir.filePath(
            QStringLiteral( "%1/%2" ).arg( attempt.second, fileName ) );
        if ( QFileInfo::exists( candidate ) )
            candidates << candidate;
    }
    const QString root = dir.filePath( fileName );
    if ( QFileInfo::exists( root ) )
        candidates << root;
    return candidates;
}

/// Bounded read of a platform-written JSON object document.
Result<QJsonObject> readBoundedJsonDocument( const QString &path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
        return Result<QJsonObject>::failure( typedFailure(
            kCodeMalformedEvidence,
            QStringLiteral( "evidence file '%1' cannot be opened" ).arg( path ) ) );
    if ( file.size() > kMaxProvenanceFileBytes )
        return Result<QJsonObject>::failure( typedFailure(
            kCodeEvidenceTooLarge,
            QStringLiteral( "evidence file '%1' exceeds the %2-byte read cap" )
                .arg( path ).arg( kMaxProvenanceFileBytes ) ) );
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson( file.readAll(), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isObject() )
        return Result<QJsonObject>::failure( typedFailure(
            kCodeMalformedEvidence,
            QStringLiteral( "evidence file '%1' is not a JSON object: %2" )
                .arg( path, parseError.errorString() ) ) );
    return Result<QJsonObject>::success( document.object() );
}

} // namespace

Result<StepEvidence> DirectoryEvidenceSource::steps( const QString &runId )
{
    QVector<Diagnostic> warnings;

    // 1. Checkpoint step plans — the richest recorded per-step evidence.
    const QStringList checkpoints =
        candidatePaths( m_runDirectory, QStringLiteral( "checkpoint_%1.json" ).arg( runId ) );
    if ( !checkpoints.isEmpty() )
    {
        workflow::WorkflowCheckpointManager manager;
        QString error;
        auto run = manager.loadCheckpoint( checkpoints.front(), &error );
        if ( run )
        {
            auto evidence = stepEvidenceFromCheckpointRun( *run );
            if ( evidence.has_value() )
                return evidence;
            // Fall through recorded-evidence ladder; keep the typed reason.
            warnings << evidence.diagnostics().front();
        }
        else
        {
            warnings << typedFailure(
                kCodeMalformedEvidence,
                QStringLiteral( "checkpoint '%1' unreadable: %2" ).arg( checkpoints.front(), error ),
                DiagnosticSeverity::Warning );
        }
    }

    // 2. Provenance graph document.
    const QStringList provenanceFiles =
        candidatePaths( m_runDirectory, QStringLiteral( "provenance_%1.json" ).arg( runId ) );
    if ( !provenanceFiles.isEmpty() )
    {
        auto document = readBoundedJsonDocument( provenanceFiles.front() );
        if ( !document )
            return Result<StepEvidence>::failure( document.diagnostics() );
        auto evidence = stepEvidenceFromProvenanceDoc( document.value() );
        if ( !evidence )
            return evidence;
        auto merged = evidence.take();
        // Degrade-in-the-open: a corrupt checkpoint above is surfaced as a
        // warning diagnostic on the successful provenance result.
        auto withWarnings = Result<StepEvidence>::success( merged, warnings );
        return withWarnings;
    }

    // 3. Bridge workflow evidence inside the run record.
    if ( m_store )
    {
        auto stored = m_store->runById( runId );
        if ( stored )
        {
            const QJsonObject workflow =
                stored->metrics().value( QLatin1String( "workflow" ) ).toObject();
            if ( !workflow.isEmpty() )
            {
                auto evidence = stepEvidenceFromBridgeWorkflowMetrics( workflow );
                if ( !evidence )
                    return evidence;
                return Result<StepEvidence>::success( evidence.take(), warnings );
            }
        }
    }

    // 4. Honest absence (with any degrade warnings riding along).
    warnings << typedFailure(
        kCodeEvidenceAbsent,
        QStringLiteral( "no step evidence recorded for run '%1'" ).arg( runId ) );
    return Result<StepEvidence>::failure( warnings );
}

} // namespace sicnu::experiment::debugger
