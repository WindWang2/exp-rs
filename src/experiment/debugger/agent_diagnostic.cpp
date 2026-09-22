// agent_diagnostic.cpp — exp.diag.v1 adapter (RS14-06, ADR 0174).
// Slice F GREEN implementation.

#include "agent_diagnostic.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace sicnu::experiment::debugger
{

namespace
{

QJsonObject diagnosticToJson( const AgentDiagnostic &diagnostic )
{
    QJsonObject json;
    json.insert( QLatin1String( "schema" ), QStringLiteral( "exp.diag.v1" ) );
    json.insert( QLatin1String( "code" ), diagnostic.code );
    json.insert( QLatin1String( "component" ), diagnostic.component );
    json.insert( QLatin1String( "student_run_id" ), diagnostic.studentRunId );
    json.insert( QLatin1String( "reference_run_id" ), diagnostic.referenceRunId );
    json.insert( QLatin1String( "recoverability" ), diagnostic.recoverability );
    if ( !diagnostic.suggestedAction.isEmpty() )
        json.insert( QLatin1String( "suggested_action" ), diagnostic.suggestedAction );
    QJsonArray causeChain;
    for ( const QString &entry : diagnostic.causeChain )
        causeChain.append( entry );
    if ( !diagnostic.causeChain.isEmpty() )
        json.insert( QLatin1String( "cause_chain" ), causeChain );
    if ( !diagnostic.details.isEmpty() )
        json.insert( QLatin1String( "details" ), diagnostic.details );
    return json;
}

AgentDiagnostic baseDiagnostic( const QString &studentRunId, const QString &referenceRunId )
{
    AgentDiagnostic diagnostic;
    diagnostic.studentRunId = studentRunId;
    diagnostic.referenceRunId = referenceRunId;
    return diagnostic;
}

/// The one place verdict/kind → code/recoverability/action mapping lives.
void populateFromReport( AgentDiagnostic &diagnostic, const FirstDivergenceReport &report )
{
    QJsonObject details;
    details.insert( QLatin1String( "verdict" ), report.verdict );
    diagnostic.details = details;

    if ( report.verdict == QLatin1String( "identical" ) )
    {
        diagnostic.code = QLatin1String( kDiagNoDivergence );
        diagnostic.recoverability = QStringLiteral( "none" );
        diagnostic.suggestedAction = QStringLiteral( "no action — the runs agree" );
        return;
    }
    if ( report.verdict == QLatin1String( "equivalent" ) )
    {
        diagnostic.code = QLatin1String( kDiagAlternativePathOnly );
        diagnostic.recoverability = QStringLiteral( "none" );
        diagnostic.suggestedAction =
            QStringLiteral( "review the accepted differences and declare formal equivalence rules for recurring ones" );
        return;
    }
    if ( report.verdict == QLatin1String( "incomplete" ) )
    {
        diagnostic.code = QLatin1String( kDiagInsufficientEvidence );
        diagnostic.recoverability = QStringLiteral( "manual" );
        diagnostic.suggestedAction =
            QStringLiteral( "re-record the run with workflow step evidence (checkpoint or bridge) and re-run the analysis" );
        return;
    }
    if ( report.verdict == QLatin1String( "non_comparable" ) )
    {
        diagnostic.code = QLatin1String( kDiagNonComparable );
        diagnostic.recoverability = QStringLiteral( "manual" );
        diagnostic.suggestedAction =
            QStringLiteral( "align dataset/split/model identity pins before comparing these runs" );
        return;
    }

    // divergent
    diagnostic.code = QLatin1String( kDiagFirstDivergence );
    diagnostic.recoverability = QStringLiteral( "manual" );
    const DivergenceFinding &finding = report.firstDivergence;
    details.insert( QLatin1String( "divergence_kind" ), divergenceKindName( finding.kind ) );
    details.insert( QLatin1String( "confidence" ), causalConfidenceName( finding.confidence ) );
    if ( !finding.referenceStepId.isEmpty() )
        details.insert( QLatin1String( "reference_step_id" ), finding.referenceStepId );
    if ( !finding.studentStepId.isEmpty() )
        details.insert( QLatin1String( "student_step_id" ), finding.studentStepId );
    if ( !finding.equivalenceRuleId.isEmpty() )
        details.insert( QLatin1String( "equivalence_rule_id" ), finding.equivalenceRuleId );

    switch ( finding.kind )
    {
        case DivergenceKind::ParameterDivergence:
            diagnostic.suggestedAction =
                QStringLiteral( "reconcile the differing parameters at step '%1' with the reference, or declare a tolerance rule if the difference is intended" )
                    .arg( finding.studentStepId.isEmpty() ? finding.referenceStepId
                                                          : finding.studentStepId );
            break;
        case DivergenceKind::GeometryAlignmentDivergence:
            diagnostic.suggestedAction =
                QStringLiteral( "align geometry parameters (crs/grid/resampling) at step '%1' with the reference" )
                    .arg( finding.studentStepId.isEmpty() ? finding.referenceStepId
                                                          : finding.studentStepId );
            break;
        case DivergenceKind::MissingPreprocessing:
            diagnostic.suggestedAction =
                QStringLiteral( "add the reference preprocessing step(s) before '%1' — the student run skipped a producer the reference performs" )
                    .arg( finding.studentStepId.isEmpty() ? finding.referenceStepId
                                                          : finding.studentStepId );
            break;
        case DivergenceKind::DifferentInputState:
            diagnostic.suggestedAction =
                QStringLiteral( "verify the input data at '%1': it differs from the reference before any processing difference" )
                    .arg( finding.studentStepId.isEmpty() ? finding.referenceStepId
                                                          : finding.studentStepId );
            break;
        case DivergenceKind::ResultDivergenceWithoutProcessDivergence:
            diagnostic.suggestedAction =
                QStringLiteral( "the process is identical but outputs differ — check the operator's determinism grade, seed and cache state at '%1'" )
                    .arg( finding.studentStepId );
            break;
        case DivergenceKind::DataSubsetDivergence:
            diagnostic.suggestedAction =
                QStringLiteral( "re-run on the reference dataset/split before comparing results" );
            break;
        case DivergenceKind::EquivalentAlternativePath:
        case DivergenceKind::UnknownNonComparable:
        case DivergenceKind::None:
            diagnostic.suggestedAction =
                QStringLiteral( "inspect the divergence report for the named evidence gaps" );
            break;
    }

    // Cause chain carries the analysis's named evidence gaps (what could not
    // be verified), outermost first.
    for ( const QString &gap : report.evidenceGaps )
        diagnostic.causeChain << gap;
    diagnostic.details = details;
}

} // namespace

QJsonObject AgentDiagnostic::toJson() const
{
    return diagnosticToJson( *this );
}

AgentDiagnostic AgentDiagnosticAdapter::forReplan( const FirstDivergenceReport &report )
{
    AgentDiagnostic diagnostic =
        baseDiagnostic( report.studentRunId, report.referenceRunId );
    populateFromReport( diagnostic, report );
    return diagnostic;
}

AgentDiagnostic AgentDiagnosticAdapter::forFailure(
    const QVector<sicnu::data::Diagnostic> &diagnostics, const QString &studentRunId )
{
    AgentDiagnostic diagnostic = baseDiagnostic( studentRunId, QString() );
    const QString code = diagnostics.isEmpty()
                             ? QLatin1String( kDiagMalformedEvidence )
                             : diagnostics.constLast().code;
    if ( code == QLatin1String( kCodeUnknownRun ) )
        diagnostic.code = QLatin1String( kDiagUnknownRun );
    else if ( code == QLatin1String( kCodeEvidenceTooLarge ) )
        diagnostic.code = QLatin1String( kDiagEvidenceTooLarge );
    else if ( code == QLatin1String( kCodeEvidenceAbsent ) )
        diagnostic.code = QLatin1String( kDiagInsufficientEvidence );
    else
        diagnostic.code = QLatin1String( kDiagMalformedEvidence );
    diagnostic.recoverability =
        code == QLatin1String( kCodeEvidenceTooLarge )
                || code == QLatin1String( kCodeEvidenceAbsent )
            ? QStringLiteral( "manual" )
            : QStringLiteral( "unknown" );
    for ( const sicnu::data::Diagnostic &entry : diagnostics )
        diagnostic.causeChain << QStringLiteral( "%1: %2" ).arg( entry.code, entry.message );
    diagnostic.details.insert( QLatin1String( "origin_code" ), code );
    if ( diagnostic.code == QLatin1String( kDiagUnknownRun ) )
        diagnostic.suggestedAction =
            QStringLiteral( "check the run id and the evidence source wiring; the run is not recorded" );
    else if ( diagnostic.code == QLatin1String( kDiagEvidenceTooLarge ) )
        diagnostic.suggestedAction =
            QStringLiteral( "the recorded evidence exceeds the analysis cap; export a narrower reference or raise the budget explicitly" );
    else if ( diagnostic.code == QLatin1String( kDiagInsufficientEvidence ) )
        diagnostic.suggestedAction =
            QStringLiteral( "no step evidence was recorded for this run; re-record it with workflow evidence (checkpoint or bridge) and re-run" );
    else
        diagnostic.suggestedAction =
            QStringLiteral( "the recorded evidence is unreadable; re-export the run record" );
    return diagnostic;
}

} // namespace sicnu::experiment::debugger
