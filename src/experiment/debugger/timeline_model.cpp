// timeline_model.cpp — Qt-free dual-run timeline (RS14-06, ADR 0174).
// Slice F GREEN implementation.

#include "timeline_model.h"

#include <QHash>
#include <QSet>

namespace sicnu::experiment::debugger
{

namespace
{

QJsonObject entryToJson( const TimelineEntry &entry )
{
    QJsonObject json;
    json.insert( QLatin1String( "reference_step_id" ), entry.referenceStepId );
    json.insert( QLatin1String( "student_step_id" ), entry.studentStepId );
    json.insert( QLatin1String( "operator_id" ), entry.operatorId );
    json.insert( QLatin1String( "status" ), entry.status );
    json.insert( QLatin1String( "divergence_kind" ), entry.divergenceKind );
    json.insert( QLatin1String( "confidence" ), causalConfidenceName( entry.confidence ) );
    if ( !entry.equivalenceRuleId.isEmpty() )
        json.insert( QLatin1String( "equivalence_rule_id" ), entry.equivalenceRuleId );
    json.insert( QLatin1String( "is_first_divergence" ), entry.isFirstDivergence );
    return json;
}

} // namespace

QJsonObject TimelineEntry::toJson() const
{
    return entryToJson( *this );
}

QVector<TimelineEntry> TimelineDiffModel::build( const RunSnapshot &reference,
                                                 const RunSnapshot &student,
                                                 const AlignmentResult &alignment,
                                                 const FirstDivergenceReport &report )
{
    QHash<QString, const StepSnapshot *> studentById;
    for ( const StepSnapshot &step : student.steps() )
        studentById.insert( step.stepId, &step );
    QHash<QString, const StepSnapshot *> refById;
    for ( const StepSnapshot &step : reference.steps() )
        refById.insert( step.stepId, &step );

    // Divergence lookup per matched pair.
    struct PairDiff
    {
        QString kind;
        CausalConfidence confidence = CausalConfidence::None;
        QString ruleId;
    };
    QHash<QString, PairDiff> divergentPairs;   // by reference step id
    QSet<QString> incomparablePairs;
    auto registerFinding = [ & ]( const DivergenceFinding &finding ) {
        if ( finding.referenceStepId.isEmpty() )
            return;
        if ( finding.kind == DivergenceKind::UnknownNonComparable )
            incomparablePairs.insert( finding.referenceStepId );
        else if ( finding.kind != DivergenceKind::EquivalentAlternativePath )
            divergentPairs.insert( finding.referenceStepId,
                                   { divergenceKindName( finding.kind ),
                                     finding.confidence, finding.equivalenceRuleId } );
    };
    registerFinding( report.firstDivergence );
    for ( const DivergenceFinding &finding : report.additionalFindings )
        registerFinding( finding );

    QHash<QString, QString> refToStudent;
    QSet<QString> matchedStudent;
    for ( const StepMatch &match : alignment.matches )
    {
        refToStudent.insert( match.referenceStepId, match.studentStepId );
        matchedStudent.insert( match.studentStepId );
    }

    const QString firstRefStep = report.hasFirstDivergence
                                     ? report.firstDivergence.referenceStepId
                                     : QString();

    QVector<TimelineEntry> entries;
    for ( const StepSnapshot &refStep : reference.steps() )
    {
        const QString studentId = refToStudent.value( refStep.stepId );
        TimelineEntry entry;
        entry.referenceStepId = refStep.stepId;
        entry.operatorId = refStep.operatorId;
        if ( studentId.isEmpty() )
        {
            entry.status = QStringLiteral( "missing_in_student" );
        }
        else
        {
            entry.studentStepId = studentId;
            if ( incomparablePairs.contains( refStep.stepId ) )
                entry.status = QStringLiteral( "matched_incomparable" );
            else if ( divergentPairs.contains( refStep.stepId ) )
            {
                entry.status = QStringLiteral( "matched_divergent" );
                const PairDiff diff = divergentPairs.value( refStep.stepId );
                entry.divergenceKind = diff.kind;
                entry.confidence = diff.confidence;
                entry.equivalenceRuleId = diff.ruleId;
            }
            else
            {
                entry.status = QStringLiteral( "matched_identical" );
            }
        }
        entry.isFirstDivergence = report.hasFirstDivergence
                                  && refStep.stepId == firstRefStep;
        entries.append( entry );
    }

    // Student-only steps after the shared timeline.
    QSet<QString> matchedRef;
    for ( const StepMatch &match : alignment.matches )
        matchedRef.insert( match.referenceStepId );
    for ( const StepSnapshot &studentStep : student.steps() )
    {
        if ( matchedStudent.contains( studentStep.stepId ) )
            continue;
        TimelineEntry entry;
        entry.studentStepId = studentStep.stepId;
        entry.operatorId = studentStep.operatorId;
        entry.status = QStringLiteral( "extra_in_student" );
        entries.append( entry );
    }
    return entries;
}

} // namespace sicnu::experiment::debugger
