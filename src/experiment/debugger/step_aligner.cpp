// step_aligner.cpp — deterministic step alignment (RS14-06, ADR 0174).
// Slice B GREEN implementation.

#include "step_aligner.h"

#include <QJsonArray>
#include <QSet>
#include <algorithm>

namespace sicnu::experiment::debugger
{

using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::Result;

namespace
{

QJsonObject matchToJson( const StepMatch &match )
{
    QJsonObject json;
    switch ( match.kind )
    {
        case StepMatch::Kind::ExactId:
            json.insert( QLatin1String( "kind" ), QStringLiteral( "exact_id" ) );
            break;
        case StepMatch::Kind::Structural:
            json.insert( QLatin1String( "kind" ), QStringLiteral( "structural" ) );
            break;
        case StepMatch::Kind::EquivalentRule:
            json.insert( QLatin1String( "kind" ), QStringLiteral( "equivalent_rule" ) );
            break;
    }
    json.insert( QLatin1String( "reference_step_id" ), match.referenceStepId );
    json.insert( QLatin1String( "student_step_id" ), match.studentStepId );
    json.insert( QLatin1String( "parent_coverage_complete" ), match.parentCoverageComplete );
    if ( !match.equivalenceRuleId.isEmpty() )
        json.insert( QLatin1String( "equivalence_rule_id" ), match.equivalenceRuleId );
    return json;
}

/// Matched-parent analysis in the step's OWN id space: which of the step's
/// parents already have counterparts, and whether any parent is left
/// unmatched on this side.
struct ParentCoverage
{
    QSet<QString> matchedParents;   // own-space ids whose counterpart exists
    bool hasUnmatchedParent = false;
};

ParentCoverage parentCoverage( const StepSnapshot &step, const QSet<QString> &matchedIds )
{
    ParentCoverage coverage;
    for ( const QString &parent : step.dependencies )
    {
        if ( matchedIds.contains( parent ) )
            coverage.matchedParents.insert( parent );
        else
            coverage.hasUnmatchedParent = true;
    }
    return coverage;
}

} // namespace

QJsonObject StepMatch::toJson() const
{
    return matchToJson( *this );
}

QJsonObject AlignmentResult::toJson() const
{
    QJsonObject json;
    switch ( planRelation )
    {
        case PlanRelation::SamePlanSignature:
            json.insert( QLatin1String( "plan_relation" ), QStringLiteral( "same_plan_signature" ) );
            break;
        case PlanRelation::DifferentPlan:
            json.insert( QLatin1String( "plan_relation" ), QStringLiteral( "different_plan" ) );
            break;
        case PlanRelation::InsufficientEvidence:
            json.insert( QLatin1String( "plan_relation" ),
                         QStringLiteral( "insufficient_evidence" ) );
            break;
    }

    QJsonArray matchesJson;
    for ( const StepMatch &match : matches )
        matchesJson.append( match.toJson() );
    json.insert( QLatin1String( "matches" ), matchesJson );

    QJsonArray unmatchedRef;
    for ( const QString &stepId : unmatchedReference )
        unmatchedRef.append( stepId );
    json.insert( QLatin1String( "unmatched_reference" ), unmatchedRef );

    QJsonArray unmatchedStu;
    for ( const QString &stepId : unmatchedStudent )
        unmatchedStu.append( stepId );
    json.insert( QLatin1String( "unmatched_student" ), unmatchedStu );
    return json;
}

Result<AlignmentResult> StepAligner::align( const RunSnapshot &reference,
                                            const RunSnapshot &student,
                                            const AlignmentBudget &budget )
{
    AlignmentResult result;

    const bool signaturesPresent =
        !reference.planSignature().isEmpty() && !student.planSignature().isEmpty();
    const bool signaturesEqual =
        signaturesPresent && reference.planSignature() == student.planSignature();
    result.planRelation = !signaturesPresent
                              ? AlignmentResult::PlanRelation::InsufficientEvidence
                              : ( signaturesEqual ? AlignmentResult::PlanRelation::SamePlanSignature
                                                  : AlignmentResult::PlanRelation::DifferentPlan );

    QHash<QString, const StepSnapshot *> refById;
    for ( const StepSnapshot &step : reference.steps() )
        refById.insert( step.stepId, &step );
    QHash<QString, const StepSnapshot *> studentById;
    for ( const StepSnapshot &step : student.steps() )
        studentById.insert( step.stepId, &step );

    // Same plan signature: id equality is authoritative — one exact pass.
    // (A step present on one side only — e.g. a node that never executed —
    // stays honestly unmatched.)
    if ( result.planRelation == AlignmentResult::PlanRelation::SamePlanSignature )
    {
        for ( const StepSnapshot &refStep : reference.steps() )
        {
            if ( !studentById.contains( refStep.stepId ) )
            {
                result.unmatchedReference << refStep.stepId;
                continue;
            }
            StepMatch match;
            match.kind = StepMatch::Kind::ExactId;
            match.referenceStepId = refStep.stepId;
            match.studentStepId = refStep.stepId;
            result.matches.append( match );
        }
        for ( const StepSnapshot &studentStep : student.steps() )
            if ( !refById.contains( studentStep.stepId ) )
                result.unmatchedStudent << studentStep.stepId;
        return Result<AlignmentResult>::success( result );
    }

    // Different plans (or missing signatures): structural alignment in
    // topological (stored) order. Snapshots store steps topologically in
    // both rich modes; bridge mode preserves recorded execution order.
    QSet<QString> matchedRefIds;
    QSet<QString> matchedStudentIds;
    QHash<QString, QString> refToStudent;
    QHash<QString, QString> studentToRef;

    qint64 comparisons = 0;

    for ( const StepSnapshot &refStep : reference.steps() )
    {
        // Candidate scan (operator equality), preferring content identity:
        // a step that produced the byte-identical output — same digest and
        // digest mode — is almost surely the same scientific step.
        const StepSnapshot *firstAcceptable = nullptr;
        const StepSnapshot *contentIdentical = nullptr;
        for ( const StepSnapshot &studentStep : student.steps() )
        {
            if ( matchedStudentIds.contains( studentStep.stepId ) )
                continue;
            if ( ++comparisons > budget.maxComparisons )
                return Result<AlignmentResult>::failure( Diagnostic{
                    QLatin1String( kCodeAlignmentBudgetExceeded ),
                    QStringLiteral( "alignment exceeded %1 candidate comparisons" )
                        .arg( budget.maxComparisons ),
                    DiagnosticSeverity::Error } );
            if ( studentStep.operatorId != refStep.operatorId )
                continue;

            const ParentCoverage refCoverage =
                parentCoverage( refStep, matchedRefIds );
            const ParentCoverage studentCoverage =
                parentCoverage( studentStep, matchedStudentIds );

            // Reference-side consistency: every matched reference parent's
            // counterpart must feed the candidate. The student side may
            // carry extra or missing producers — accepted here but flagged
            // via parentCoverageComplete so the divergence walk sees them.
            // (Bidirectional strictness was tried and destroys exactly the
            // missing-preprocessing localization this module exists for:
            // the consumer stops matching the moment its producer set
            // differs, and the report degrades to a shapeless step list.)
            bool correspond = true;
            for ( const QString &refParent : refCoverage.matchedParents )
            {
                const QString counterpart = refToStudent.value( refParent );
                if ( counterpart.isEmpty()
                     || !studentStep.dependencies.contains( counterpart ) )
                {
                    correspond = false;
                    break;
                }
            }
            if ( !correspond )
                continue;

            if ( firstAcceptable == nullptr )
                firstAcceptable = &studentStep;
            const bool contentIdenticalPair =
                !refStep.outputDigest.isEmpty()
                && refStep.outputDigest == studentStep.outputDigest
                && refStep.digestMode == studentStep.digestMode;
            if ( contentIdenticalPair && contentIdentical == nullptr )
                contentIdentical = &studentStep;
            if ( contentIdentical != nullptr )
                break; // cannot do better than byte-identical content
        }

        const StepSnapshot *chosen = contentIdentical ? contentIdentical : firstAcceptable;
        if ( chosen == nullptr )
            continue; // recorded by the single-writer leftover pass below

        const ParentCoverage refCoverage = parentCoverage( refStep, matchedRefIds );
        const ParentCoverage studentCoverage = parentCoverage( *chosen, matchedStudentIds );
        const bool coverageComplete = !refCoverage.hasUnmatchedParent
                                      && !studentCoverage.hasUnmatchedParent;

        matchedRefIds.insert( refStep.stepId );
        matchedStudentIds.insert( chosen->stepId );
        refToStudent.insert( refStep.stepId, chosen->stepId );
        studentToRef.insert( chosen->stepId, refStep.stepId );

        StepMatch match;
        match.kind = StepMatch::Kind::Structural;
        match.referenceStepId = refStep.stepId;
        match.studentStepId = chosen->stepId;
        match.parentCoverageComplete = coverageComplete;
        result.matches.append( match );
    }

    for ( const StepSnapshot &studentStep : student.steps() )
        if ( !matchedStudentIds.contains( studentStep.stepId ) )
            result.unmatchedStudent << studentStep.stepId;
    for ( const StepSnapshot &refStep : reference.steps() )
        if ( !matchedRefIds.contains( refStep.stepId ) )
            result.unmatchedReference << refStep.stepId;

    return Result<AlignmentResult>::success( result );
}

} // namespace sicnu::experiment::debugger
