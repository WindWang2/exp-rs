// first_divergence.cpp — first-divergence localization (RS14-06, ADR 0174).
// Slice C GREEN implementation: run-level gate → identity shortcut →
// alignment → single topological walk → typed findings with honest
// confidence.

#include "first_divergence.h"

#include "equivalence.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <algorithm>

namespace sicnu::experiment::debugger
{

using sicnu::data::Diagnostic;
using sicnu::data::Result;

namespace
{

constexpr int kTerminalStudentStepPosition = 1'000'000'000;

} // namespace

QString divergenceKindName( DivergenceKind kind )
{
    switch ( kind )
    {
        case DivergenceKind::None:
            return QStringLiteral( "none" );
        case DivergenceKind::EquivalentAlternativePath:
            return QStringLiteral( "equivalent_alternative_path" );
        case DivergenceKind::DifferentInputState:
            return QStringLiteral( "different_input_state" );
        case DivergenceKind::MissingPreprocessing:
            return QStringLiteral( "missing_preprocessing" );
        case DivergenceKind::ParameterDivergence:
            return QStringLiteral( "parameter_divergence" );
        case DivergenceKind::DataSubsetDivergence:
            return QStringLiteral( "data_subset_divergence" );
        case DivergenceKind::GeometryAlignmentDivergence:
            return QStringLiteral( "geometry_alignment_divergence" );
        case DivergenceKind::ResultDivergenceWithoutProcessDivergence:
            return QStringLiteral( "result_divergence_without_process_divergence" );
        case DivergenceKind::UnknownNonComparable:
            return QStringLiteral( "unknown_non_comparable" );
    }
    return QStringLiteral( "unknown_non_comparable" );
}

QString causalConfidenceName( CausalConfidence confidence )
{
    switch ( confidence )
    {
        case CausalConfidence::None:
            return QStringLiteral( "none" );
        case CausalConfidence::Low:
            return QStringLiteral( "low" );
        case CausalConfidence::Medium:
            return QStringLiteral( "medium" );
        case CausalConfidence::High:
            return QStringLiteral( "high" );
    }
    return QStringLiteral( "none" );
}

namespace
{

/// Tri-state verification of one identity dimension.
enum class Verify
{
    Unknown,     ///< evidence absent or incomparable — never treated as Equal
    Equal,
    Different,
};

struct Position
{
    int order = 0;      ///< reference topological index (student-only terminal = huge)
    int sub = 0;        ///< stable secondary order among findings at one step

    bool operator<( const Position &other ) const
    {
        if ( order != other.order )
            return order < other.order;
        return sub < other.sub;
    }
};

QJsonObject findingToJson( const DivergenceFinding &finding )
{
    QJsonObject json;
    json.insert( QLatin1String( "kind" ), divergenceKindName( finding.kind ) );
    json.insert( QLatin1String( "reference_step_id" ), finding.referenceStepId );
    json.insert( QLatin1String( "student_step_id" ), finding.studentStepId );
    json.insert( QLatin1String( "confidence" ), causalConfidenceName( finding.confidence ) );
    QJsonArray evidence;
    for ( const QString &entry : finding.evidence )
        evidence.append( entry );
    json.insert( QLatin1String( "evidence" ), evidence );
    QJsonArray missing;
    for ( const QString &entry : finding.missingEvidence )
        missing.append( entry );
    json.insert( QLatin1String( "missing_evidence" ), missing );
    if ( !finding.equivalenceRuleId.isEmpty() )
        json.insert( QLatin1String( "equivalence_rule_id" ), finding.equivalenceRuleId );
    return json;
}

/// Sorted (digest, mode) list of this snapshot's root inputs.
QStringList rootInputIdentities( const RunSnapshot &snapshot, bool *allComparable )
{
    QStringList roots;
    *allComparable = true;
    for ( const ArtifactSnapshot &artifact : snapshot.artifacts() )
    {
        if ( !artifact.rootInput )
            continue;
        if ( artifact.digest.isEmpty() )
            *allComparable = false;
        roots << QStringLiteral( "%1|%2" ).arg( artifact.digest, artifact.digestMode );
    }
    roots.sort();
    return roots;
}

/// First matched reference consumer reachable from @p start via the graph's
/// child edges (reference side). Returns "" when none.
QString firstMatchedConsumer( const QString &start,
                              const RunSnapshot &snapshot,
                              const QSet<QString> &matched )
{
    QHash<QString, QStringList> children;
    for ( const StepSnapshot &step : snapshot.steps() )
        for ( const QString &parent : step.dependencies )
            children[ parent ] << step.stepId;

    QStringList queue{ start };
    QSet<QString> visited{ start };
    int guard = 0;
    while ( !queue.isEmpty() && guard++ < 100000 )
    {
        const QString current = queue.takeFirst();
        for ( const QString &child : children.value( current ) )
        {
            if ( matched.contains( child ) )
                return child;
            if ( !visited.contains( child ) )
            {
                visited.insert( child );
                queue << child;
            }
        }
    }
    return {};
}

} // namespace

QJsonObject DivergenceFinding::toJson() const
{
    return findingToJson( *this );
}

QJsonObject FirstDivergenceReport::toJson() const
{
    QJsonObject json;
    json.insert( QLatin1String( "kind" ), QLatin1String( kDivergenceSchemaKind ) );
    json.insert( QLatin1String( "schema_version" ), kDebuggerSchemaVersion );
    json.insert( QLatin1String( "reference_run_id" ), referenceRunId );
    json.insert( QLatin1String( "student_run_id" ), studentRunId );
    json.insert( QLatin1String( "verdict" ), verdict );
    json.insert( QLatin1String( "has_first_divergence" ), hasFirstDivergence );
    json.insert( QLatin1String( "first_divergence" ), firstDivergence.toJson() );

    QJsonArray additional;
    for ( const DivergenceFinding &finding : additionalFindings )
        additional.append( finding.toJson() );
    json.insert( QLatin1String( "additional_findings" ), additional );
    json.insert( QLatin1String( "alignment" ), alignment );
    json.insert( QLatin1String( "run_level_comparison" ), runLevelComparison );
    QJsonArray gaps;
    for ( const QString &gap : evidenceGaps )
        gaps.append( gap );
    json.insert( QLatin1String( "evidence_gaps" ), gaps );
    return json;
}

namespace
{

} // namespace

namespace
{

Result<FirstDivergenceReport> analyzeImpl(
    const ExperimentRun &referenceRun,
    const ExperimentRun &studentRun,
    const RunSnapshot &reference,
    const RunSnapshot &student,
    const EquivalenceProfile *profile,
    const FirstDivergenceOptions &options )
{
    FirstDivergenceReport report;
    report.referenceRunId = reference.runId();
    report.studentRunId = student.runId();

    // ── Run-level comparability gate (existing single truth for pins) ──
    const sicnu::experiment::RunComparison comparison =
        sicnu::experiment::RunComparison::compare( referenceRun, studentRun );
    report.runLevelComparison = comparison.toJson();

    const auto pinDetail = [ & ]( const QString &dimension ) -> QPair<QString, QString> {
        for ( const sicnu::experiment::RunDiffItem &item : comparison.dimensions )
            if ( item.dimension == dimension )
                return { item.dimension, item.detail };
        return {};
    };

    if ( comparison.verdict == sicnu::experiment::RunComparison::Verdict::NotComparable )
    {
        report.verdict = QStringLiteral( "non_comparable" );
        DivergenceFinding finding;
        const auto dataset = pinDetail( QStringLiteral( "dataset" ) );
        const auto split = pinDetail( QStringLiteral( "split" ) );
        // RunComparison details are non-empty exactly when a pin differs:
        // dataset/split identity differences ARE subset divergence by
        // definition; anything else (model identity) stays honestly unknown.
        if ( !dataset.second.isEmpty() || !split.second.isEmpty() )
        {
            finding.kind = DivergenceKind::DataSubsetDivergence;
            finding.confidence = CausalConfidence::High;
        }
        else
        {
            finding.kind = DivergenceKind::UnknownNonComparable;
            finding.confidence = CausalConfidence::None;
        }
        for ( const sicnu::experiment::RunDiffItem &item : comparison.dimensions )
            if ( item.differs )
                finding.evidence
                    << QStringLiteral( "%1: %2" ).arg( item.dimension, item.detail );
        report.firstDivergence = finding;
        report.hasFirstDivergence = true;
        report.evidenceGaps
            << QStringLiteral( "step walk skipped: the runs are not comparable at the identity-pin level" );
        return Result<FirstDivergenceReport>::success( report );
    }

    // ── Step-evidence availability gate ──
    if ( reference.stepEvidence() == StepEvidenceMode::Absent
         || student.stepEvidence() == StepEvidenceMode::Absent )
    {
        report.verdict = QStringLiteral( "incomplete" );
        if ( reference.stepEvidence() == StepEvidenceMode::Absent )
            report.evidenceGaps
                << QStringLiteral( "reference run '%1' carries no step evidence" )
                       .arg( reference.runId() );
        if ( student.stepEvidence() == StepEvidenceMode::Absent )
            report.evidenceGaps
                << QStringLiteral( "student run '%1' carries no step evidence" )
                       .arg( student.runId() );
        return Result<FirstDivergenceReport>::success( report );
    }

    // ── Identity shortcut: identical identity documents are identical runs ──
    if ( reference.identityDocument() == student.identityDocument() )
    {
        report.verdict = QStringLiteral( "identical" );
        return Result<FirstDivergenceReport>::success( report );
    }

    // ── Alignment + single topological walk (profile-aware when given) ──
    auto alignmentResult = profile ? StepAligner::align( reference, student, *profile )
                                   : StepAligner::align( reference, student );
    if ( !alignmentResult.has_value() )
        return Result<FirstDivergenceReport>::failure( alignmentResult.diagnostics() );
    const AlignmentResult alignment = alignmentResult.take();
    report.alignment = alignment.toJson();

    QHash<QString, const StepSnapshot *> refById;
    int index = 0;
    QHash<QString, int> refOrder;
    for ( const StepSnapshot &step : reference.steps() )
    {
        refById.insert( step.stepId, &step );
        refOrder.insert( step.stepId, index++ );
    }
    QHash<QString, const StepSnapshot *> studentById;
    for ( const StepSnapshot &step : student.steps() )
        studentById.insert( step.stepId, &step );

    QHash<QString, QString> refToStudent;
    QHash<QString, QString> studentToRef;
    QSet<QString> matchedRefIds;
    QSet<QString> matchedStudentIds;
    for ( const StepMatch &match : alignment.matches )
    {
        refToStudent.insert( match.referenceStepId, match.studentStepId );
        studentToRef.insert( match.studentStepId, match.referenceStepId );
        matchedRefIds.insert( match.referenceStepId );
        matchedStudentIds.insert( match.studentStepId );
    }

    // Output verification per matched reference step.
    QHash<QString, Verify> outputVerify;
    QHash<QString, bool> parentCoverageComplete;
    for ( const StepMatch &match : alignment.matches )
    {
        parentCoverageComplete.insert( match.referenceStepId, match.parentCoverageComplete );
        const StepSnapshot *refStep = refById.value( match.referenceStepId );
        const StepSnapshot *studentStep = studentById.value( match.studentStepId );
        if ( !refStep || !studentStep )
            continue;
        if ( refStep->outputDigest.isEmpty() || studentStep->outputDigest.isEmpty() )
            outputVerify.insert( refStep->stepId, Verify::Unknown );
        else if ( refStep->digestMode != studentStep->digestMode )
            outputVerify.insert( refStep->stepId, Verify::Unknown ); // incomparable modes
        else
            outputVerify.insert( refStep->stepId,
                                 refStep->outputDigest == studentStep->outputDigest
                                     ? Verify::Equal
                                     : Verify::Different );
    }

    // Upstream verification: a pair's parents are verified when every parent
    // pair verified Equal with complete coverage. Roots count via identity
    // comparison below.
    bool rootInputsComparable = true;
    const QStringList refRoots = rootInputIdentities( reference, &rootInputsComparable );
    bool studentRootsComparable = true;
    const QStringList studentRoots = rootInputIdentities( student, &studentRootsComparable );
    Verify rootVerify =
        ( !rootInputsComparable || !studentRootsComparable || refRoots.isEmpty() != studentRoots.isEmpty() )
            ? Verify::Unknown
            : ( refRoots == studentRoots ? Verify::Equal : Verify::Different );

    QHash<QString, bool> upstreamVerified;   // ref step id → parents fully verified equal
    auto parentsVerified = [ & ]( const StepSnapshot &step ) {
        if ( step.dependencies.isEmpty() )
            return rootVerify == Verify::Equal;
        for ( const QString &parent : step.dependencies )
            if ( upstreamVerified.value( parent, false ) != true )
                return false;
        return true;
    };

    struct Walking
    {
        DivergenceFinding finding;
        Position position;
    };
    QVector<Walking> findings;

    // Per-pair process verification state for the walk.
    for ( const StepSnapshot &refStep : reference.steps() )
    {
        const QString studentId = refToStudent.value( refStep.stepId );
        const bool isMatched = !studentId.isEmpty();

        if ( !isMatched )
        {
            // Reference-only step: missing preprocessing. The finding lands
            // on its first matched consumer; tail steps report themselves.
            continue; // handled after the pair walk
        }

        const StepSnapshot *studentStep = studentById.value( studentId );
        if ( !studentStep )
            continue;

        const int order = refOrder.value( refStep.stepId );
        const bool coverage = parentCoverageComplete.value( refStep.stepId, true );
        const bool upstream = parentsVerified( refStep );

        // Matches accepted by a declared operator-group rule are recorded,
        // never silent — but they are not divergences.
        QString matchRuleId;
        for ( const StepMatch &match : alignment.matches )
            if ( match.referenceStepId == refStep.stepId
                 && match.kind == StepMatch::Kind::EquivalentRule )
            {
                matchRuleId = match.equivalenceRuleId;
                break;
            }
        if ( !matchRuleId.isEmpty() )
        {
            DivergenceFinding finding;
            finding.kind = DivergenceKind::EquivalentAlternativePath;
            finding.referenceStepId = refStep.stepId;
            finding.studentStepId = studentId;
            finding.confidence = CausalConfidence::Medium;
            finding.equivalenceRuleId = matchRuleId;
            finding.evidence
                << QStringLiteral( "step matched through equivalence rule '%1'" )
                       .arg( matchRuleId );
            findings.append( { finding, { order, 2 } } );
        }

        // 1. Missing/extra producers visible through incomplete coverage.
        if ( !coverage )
        {
            QStringList missingRefProducers;
            QStringList extraStudentProducers;
            for ( const QString &parent : refStep.dependencies )
                if ( !matchedRefIds.contains( parent ) )
                    missingRefProducers << parent;
            for ( const QString &parent : studentStep->dependencies )
                if ( !matchedStudentIds.contains( parent ) )
                    extraStudentProducers << parent;

            if ( !missingRefProducers.isEmpty() )
            {
                DivergenceFinding finding;
                finding.kind = DivergenceKind::MissingPreprocessing;
                finding.referenceStepId = refStep.stepId;
                finding.studentStepId = studentId;
                // A missing producer whose consumer output is byte-identical
                // is immaterial for THIS input — say so, at low confidence.
                const bool outputEqual = outputVerify.value( refStep.stepId ) == Verify::Equal;
                finding.confidence = outputEqual ? CausalConfidence::Low : CausalConfidence::High;
                finding.evidence
                    << QStringLiteral( "reference producer(s) absent in student: %1" )
                           .arg( missingRefProducers.join( QLatin1String( ", " ) ) );
                if ( outputEqual )
                    finding.evidence
                        << QStringLiteral( "consumer output digests identical — the missing step appears immaterial for this input" );
                findings.append( { finding, { order, 1 } } );
            }
            if ( !extraStudentProducers.isEmpty() )
            {
                DivergenceFinding finding;
                finding.kind = DivergenceKind::DifferentInputState;
                finding.referenceStepId = refStep.stepId;
                finding.studentStepId = studentId;
                finding.confidence = upstream ? CausalConfidence::High : CausalConfidence::Medium;
                finding.evidence
                    << QStringLiteral( "student producer(s) absent in reference: %1" )
                           .arg( extraStudentProducers.join( QLatin1String( ", " ) ) );
                findings.append( { finding, { order, 2 } } );
            }
        }

        // 2. Root input state at the first root-consuming pair (no parents
        // on either side). Reported once — at the earliest root consumer.
        if ( rootVerify == Verify::Different && refStep.dependencies.isEmpty()
             && studentStep->dependencies.isEmpty() )
        {
            DivergenceFinding finding;
            finding.kind = DivergenceKind::DifferentInputState;
            finding.referenceStepId = refStep.stepId;
            finding.studentStepId = studentId;
            finding.confidence = CausalConfidence::High;
            finding.evidence << QStringLiteral( "root inputs: reference[%1] student[%2]" )
                                    .arg( refRoots.join( QLatin1String( " " ) ),
                                          studentRoots.join( QLatin1String( " " ) ) );
            findings.append( { finding, { order, 0 } } );
            rootVerify = Verify::Equal; // report once — the earliest root consumer
        }

        // 3. Parameter identity.
        const bool paramsPresentBoth =
            !refStep.paramsHash.isEmpty() && !studentStep->paramsHash.isEmpty();
        const bool paramsEqual =
            paramsPresentBoth && refStep.paramsHash == studentStep->paramsHash;
        const bool lineagePresentBoth =
            !refStep.lineageSignature.isEmpty() && !studentStep->lineageSignature.isEmpty();
        const bool lineageEqual =
            lineagePresentBoth
            && refStep.lineageSignature == studentStep->lineageSignature;

        if ( paramsPresentBoth && !paramsEqual )
        {
            bool reportedAtParamsPosition = false;
            if ( profile && !refStep.parameters.isEmpty() && !studentStep->parameters.isEmpty() )
            {
                QStringList differingKeys;
                QString acceptedRuleId;
                const bool accepted = profile->paramsEquivalent(
                    refStep.operatorId, refStep.parameters, studentStep->parameters,
                    &differingKeys, &acceptedRuleId );
                if ( accepted )
                {
                    // Accepted by a declared rule — recorded, never silent.
                    DivergenceFinding finding;
                    finding.kind = DivergenceKind::EquivalentAlternativePath;
                    finding.referenceStepId = refStep.stepId;
                    finding.studentStepId = studentId;
                    finding.confidence = CausalConfidence::Medium;
                    finding.equivalenceRuleId = acceptedRuleId;
                    finding.evidence
                        << QStringLiteral( "parameter difference accepted by rule '%1'" )
                               .arg( acceptedRuleId );
                    findings.append( { finding, { order, 3 } } );
                    reportedAtParamsPosition = true;
                }
                else if ( !differingKeys.isEmpty() )
                {
                    const QStringList geometryKeys = profile->geometryKeysFor( refStep.operatorId );
                    bool allGeometry = !geometryKeys.isEmpty();
                    for ( const QString &key : differingKeys )
                        if ( !geometryKeys.contains( key ) )
                        {
                            allGeometry = false;
                            break;
                        }
                    if ( allGeometry )
                    {
                        DivergenceFinding finding;
                        finding.kind = DivergenceKind::GeometryAlignmentDivergence;
                        finding.referenceStepId = refStep.stepId;
                        finding.studentStepId = studentId;
                        finding.confidence = upstream ? CausalConfidence::High
                                                      : CausalConfidence::Medium;
                        finding.evidence
                            << QStringLiteral( "geometry parameter keys differ: %1" )
                                   .arg( differingKeys.join( QLatin1String( ", " ) ) );
                        findings.append( { finding, { order, 3 } } );
                        reportedAtParamsPosition = true;
                    }
                }
            }

            if ( !reportedAtParamsPosition )
            {
                DivergenceFinding finding;
                finding.kind = DivergenceKind::ParameterDivergence;
                finding.referenceStepId = refStep.stepId;
                finding.studentStepId = studentId;
                finding.confidence = upstream ? CausalConfidence::High : CausalConfidence::Medium;
                finding.evidence
                    << QStringLiteral( "params_hash: reference=%1 student=%2" )
                           .arg( refStep.paramsHash, studentStep->paramsHash );
                findings.append( { finding, { order, 3 } } );
            }
        }
        else if ( !paramsPresentBoth && lineagePresentBoth && !lineageEqual )
        {
            // Process identity differs but recorded evidence cannot name the
            // parameter — a low-confidence divergence with a named gap.
            DivergenceFinding finding;
            finding.kind = DivergenceKind::ParameterDivergence;
            finding.referenceStepId = refStep.stepId;
            finding.studentStepId = studentId;
            finding.confidence = CausalConfidence::Low;
            finding.evidence
                << QStringLiteral( "lineage_signature: reference=%1 student=%2" )
                       .arg( refStep.lineageSignature, studentStep->lineageSignature );
            finding.missingEvidence
                << QStringLiteral( "recorded step parameters unavailable in the evidence mode; add checkpoint evidence to name the differing parameter" );
            findings.append( { finding, { order, 4 } } );
        }
        // params equal + lineage differ ⇒ upstream cascade; the walk has
        // already reported the true origin upstream. Deliberately silent.

        // 4. Output identity under an identical process.
        const Verify outVerify = outputVerify.value( refStep.stepId, Verify::Unknown );
        const bool processIdentical = ( !paramsPresentBoth || paramsEqual )
                                      && ( !lineagePresentBoth || lineageEqual );
        if ( processIdentical && outVerify == Verify::Different )
        {
            DivergenceFinding finding;
            finding.kind = DivergenceKind::ResultDivergenceWithoutProcessDivergence;
            finding.referenceStepId = refStep.stepId;
            finding.studentStepId = studentId;
            const bool cacheComparable =
                refStep.cacheHitKnown && studentStep->cacheHitKnown;
            const bool cacheEqual = !cacheComparable
                                    || refStep.cacheHit == studentStep->cacheHit;
            finding.confidence = cacheEqual ? CausalConfidence::High : CausalConfidence::Medium;
            finding.evidence
                << QStringLiteral( "output_digest: reference=%1 student=%2" )
                       .arg( refStep.outputDigest, studentStep->outputDigest );
            if ( !cacheComparable )
                finding.missingEvidence
                    << QStringLiteral( "cache-hit state unknown on at least one side" );
            else if ( !cacheEqual )
                finding.evidence
                    << QStringLiteral( "cache_hit: reference=%1 student=%2" )
                           .arg( refStep.cacheHit ? QStringLiteral( "true" )
                                                  : QStringLiteral( "false" ),
                                 studentStep->cacheHit ? QStringLiteral( "true" )
                                                       : QStringLiteral( "false" ) );
            findings.append( { finding, { order, 5 } } );
        }
        else if ( processIdentical && outVerify == Verify::Unknown
                  && !refStep.outputDigest.isEmpty() && !studentStep->outputDigest.isEmpty()
                  && refStep.digestMode != studentStep->digestMode )
        {
            DivergenceFinding finding;
            finding.kind = DivergenceKind::UnknownNonComparable;
            finding.referenceStepId = refStep.stepId;
            finding.studentStepId = studentId;
            finding.confidence = CausalConfidence::None;
            finding.evidence
                << QStringLiteral( "digest modes differ: reference=%1 student=%2" )
                       .arg( refStep.digestMode, studentStep->digestMode );
            finding.missingEvidence
                << QStringLiteral( "recompute one side's artifact identity in the other side's mode to compare" );
            findings.append( { finding, { order, 6 } } );
        }

        upstreamVerified.insert( refStep.stepId,
                                 upstream && coverage
                                     && ( !paramsPresentBoth || paramsEqual )
                                     && ( !lineagePresentBoth || lineageEqual )
                                     && outVerify == Verify::Equal );
    }

    // ── Reference-only steps (missing preprocessing) ──
    for ( const StepSnapshot &refStep : reference.steps() )
    {
        if ( matchedRefIds.contains( refStep.stepId ) )
            continue;
        const int order = refOrder.value( refStep.stepId );
        const QString consumer = firstMatchedConsumer( refStep.stepId, reference, matchedRefIds );
        if ( consumer.isEmpty() )
        {
            // Tail step absent in the student — no matched consumer to blame.
            DivergenceFinding finding;
            finding.kind = DivergenceKind::MissingPreprocessing;
            finding.referenceStepId = refStep.stepId;
            finding.confidence = CausalConfidence::High;
            finding.evidence
                << QStringLiteral( "reference step '%1' (%2) has no student counterpart" )
                       .arg( refStep.stepId, refStep.operatorId );
            findings.append( { finding, { order, 0 } } );
        }
        // Steps WITH a matched consumer were reported at the consumer pair
        // through parent-coverage analysis.
    }

    // ── Student-only steps ──
    int studentIndex = 0;
    QHash<QString, int> studentOrder;
    for ( const StepSnapshot &step : student.steps() )
        studentOrder.insert( step.stepId, studentIndex++ );
    for ( const StepSnapshot &studentStep : student.steps() )
    {
        if ( matchedStudentIds.contains( studentStep.stepId ) )
            continue;
        const QString consumer =
            firstMatchedConsumer( studentStep.stepId, student, matchedStudentIds );
        if ( !consumer.isEmpty() )
            continue; // reported at the consumer pair as DifferentInputState
        DivergenceFinding finding;
        finding.kind = DivergenceKind::EquivalentAlternativePath;
        finding.studentStepId = studentStep.stepId;
        finding.confidence = CausalConfidence::None;
        finding.evidence
            << QStringLiteral( "student-only step '%1' (%2) feeds no matched consumer — additive beyond the reference process" )
                   .arg( studentStep.stepId, studentStep.operatorId );
        finding.missingEvidence
            << QStringLiteral( "declare an equivalence rule to accept this step formally" );
        findings.append( { finding, { kTerminalStudentStepPosition + studentOrder.value( studentStep.stepId ), 0 } } );
    }

    // ── Assemble the verdict ──
    std::stable_sort( findings.begin(), findings.end(),
                      []( const Walking &a, const Walking &b ) {
                          return a.position < b.position;
                      } );
    if ( findings.size() > options.maxFindings )
        findings.resize( options.maxFindings );

    int firstSubstantive = -1;
    for ( int i = 0; i < findings.size(); ++i )
    {
        if ( findings.at( i ).finding.kind != DivergenceKind::EquivalentAlternativePath )
        {
            firstSubstantive = i;
            break;
        }
    }

    if ( findings.isEmpty() )
    {
        // Identity documents differ (the shortcut did not fire) but the walk
        // found nothing — comparable-with-differences pins (e.g. run-level
        // config) with equal steps and roots. Still honest: divergent, and
        // the run-level diff in the report names the dimension.
        report.verdict = QStringLiteral( "divergent" );
    }
    else if ( firstSubstantive < 0 )
    {
        // Only additive student-side steps beyond the reference process.
        // They are informational: listed as findings, and hasFirstDivergence
        // stays false — the shared process produced matching outputs.
        report.verdict = QStringLiteral( "equivalent" );
        for ( const Walking &entry : findings )
            report.additionalFindings.append( entry.finding );
    }
    else
    {
        report.verdict = QStringLiteral( "divergent" );
        report.firstDivergence = findings.at( firstSubstantive ).finding;
        for ( int i = 0; i < findings.size(); ++i )
            if ( i != firstSubstantive )
                report.additionalFindings.append( findings.at( i ).finding );
    }
    report.hasFirstDivergence = firstSubstantive >= 0;

    return Result<FirstDivergenceReport>::success( report );
}

} // namespace

Result<FirstDivergenceReport> FirstDivergenceAnalyzer::analyze(
    const ExperimentRun &referenceRun,
    const ExperimentRun &studentRun,
    const RunSnapshot &reference,
    const RunSnapshot &student,
    const FirstDivergenceOptions &options )
{
    return analyzeImpl( referenceRun, studentRun, reference, student, nullptr, options );
}

Result<FirstDivergenceReport> FirstDivergenceAnalyzer::analyze(
    const ExperimentRun &referenceRun,
    const ExperimentRun &studentRun,
    const RunSnapshot &reference,
    const RunSnapshot &student,
    const EquivalenceProfile &profile,
    const FirstDivergenceOptions &options )
{
    return analyzeImpl( referenceRun, studentRun, reference, student, &profile, options );
}

} // namespace sicnu::experiment::debugger
