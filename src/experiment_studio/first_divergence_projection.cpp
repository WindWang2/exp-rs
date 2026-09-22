#include "experiment_studio/first_divergence_projection.h"

#include <QJsonArray>

namespace sicnu::experiment_studio
{

QJsonObject FirstDivergenceViewModel::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "schema" ), schema );
    o.insert( QStringLiteral( "reference_run_id" ), referenceRunId );
    o.insert( QStringLiteral( "student_run_id" ), studentRunId );
    o.insert( QStringLiteral( "verdict" ), verdict );
    o.insert( QStringLiteral( "has_first_divergence" ), hasFirstDivergence );
    o.insert( QStringLiteral( "divergence_kind" ), divergenceKind );
    o.insert( QStringLiteral( "causal_confidence" ), causalConfidence );
    o.insert( QStringLiteral( "reference_step_id" ), referenceStepId );
    o.insert( QStringLiteral( "student_step_id" ), studentStepId );
    QJsonArray ev;
    for ( const QString &e : evidence )
        ev.append( e );
    o.insert( QStringLiteral( "evidence" ), ev );
    QJsonArray me;
    for ( const QString &e : missingEvidence )
        me.append( e );
    o.insert( QStringLiteral( "missing_evidence" ), me );
    QJsonArray gaps;
    for ( const QString &e : evidenceGaps )
        gaps.append( e );
    o.insert( QStringLiteral( "evidence_gaps" ), gaps );
    o.insert( QStringLiteral( "alignment" ), alignment );
    o.insert( QStringLiteral( "run_level_comparison" ), runLevelComparison );
    o.insert( QStringLiteral( "confidence_downgraded" ), confidenceDowngraded );
    QJsonArray iss;
    for ( const QString &s : issues )
        iss.append( s );
    o.insert( QStringLiteral( "issues" ), iss );
    o.insert( QStringLiteral( "report" ), reportJson );
    return o;
}

namespace
{

QString confidenceNameFromJson( const QJsonValue &v )
{
    if ( v.isString() )
        return v.toString();
    if ( v.isDouble() )
    {
        // numeric enum fallback
        const int n = static_cast<int>( v.toDouble() );
        switch ( n )
        {
        case 0:
            return QStringLiteral( "none" );
        case 1:
            return QStringLiteral( "low" );
        case 2:
            return QStringLiteral( "medium" );
        case 3:
            return QStringLiteral( "high" );
        default:
            break;
        }
    }
    return QStringLiteral( "none" );
}

int confidenceRank( const QString &name )
{
    if ( name == QStringLiteral( "high" ) )
        return 3;
    if ( name == QStringLiteral( "medium" ) )
        return 2;
    if ( name == QStringLiteral( "low" ) )
        return 1;
    return 0;
}

} // namespace

FirstDivergenceViewModel projectFirstDivergence( const QJsonObject &debuggerReportJson )
{
    FirstDivergenceViewModel vm;
    vm.reportJson = debuggerReportJson;
    vm.referenceRunId = debuggerReportJson.value( QStringLiteral( "reference_run_id" ) ).toString();
    if ( vm.referenceRunId.isEmpty() )
        vm.referenceRunId = debuggerReportJson.value( QStringLiteral( "referenceRunId" ) ).toString();
    vm.studentRunId = debuggerReportJson.value( QStringLiteral( "student_run_id" ) ).toString();
    if ( vm.studentRunId.isEmpty() )
        vm.studentRunId = debuggerReportJson.value( QStringLiteral( "studentRunId" ) ).toString();
    vm.verdict = debuggerReportJson.value( QStringLiteral( "verdict" ) ).toString();
    vm.hasFirstDivergence =
        debuggerReportJson.value( QStringLiteral( "has_first_divergence" ) ).toBool(
            debuggerReportJson.value( QStringLiteral( "hasFirstDivergence" ) ).toBool() );
    vm.alignment = debuggerReportJson.value( QStringLiteral( "alignment" ) ).toObject();
    vm.runLevelComparison =
        debuggerReportJson.value( QStringLiteral( "run_level_comparison" ) ).toObject();
    if ( vm.runLevelComparison.isEmpty() )
        vm.runLevelComparison =
            debuggerReportJson.value( QStringLiteral( "runLevelComparison" ) ).toObject();

    QJsonObject first = debuggerReportJson.value( QStringLiteral( "first_divergence" ) ).toObject();
    if ( first.isEmpty() )
        first = debuggerReportJson.value( QStringLiteral( "firstDivergence" ) ).toObject();
    if ( !first.isEmpty() )
    {
        vm.divergenceKind = first.value( QStringLiteral( "kind" ) ).toString();
        if ( vm.divergenceKind.isEmpty() )
            vm.divergenceKind = first.value( QStringLiteral( "divergence_kind" ) ).toString();
        vm.causalConfidence = confidenceNameFromJson( first.value( QStringLiteral( "confidence" ) ) );
        if ( vm.causalConfidence == QStringLiteral( "none" ) )
            vm.causalConfidence =
                confidenceNameFromJson( first.value( QStringLiteral( "causal_confidence" ) ) );
        vm.referenceStepId = first.value( QStringLiteral( "reference_step_id" ) ).toString();
        if ( vm.referenceStepId.isEmpty() )
            vm.referenceStepId = first.value( QStringLiteral( "referenceStepId" ) ).toString();
        vm.studentStepId = first.value( QStringLiteral( "student_step_id" ) ).toString();
        if ( vm.studentStepId.isEmpty() )
            vm.studentStepId = first.value( QStringLiteral( "studentStepId" ) ).toString();
        const QJsonArray ev = first.value( QStringLiteral( "evidence" ) ).toArray();
        for ( const QJsonValue &v : ev )
            vm.evidence.append( v.toString() );
        const QJsonArray me = first.value( QStringLiteral( "missing_evidence" ) ).toArray();
        for ( const QJsonValue &v : me )
            vm.missingEvidence.append( v.toString() );
        if ( me.isEmpty() )
        {
            const QJsonArray me2 = first.value( QStringLiteral( "missingEvidence" ) ).toArray();
            for ( const QJsonValue &v : me2 )
                vm.missingEvidence.append( v.toString() );
        }
    }

    const QJsonArray gaps = debuggerReportJson.value( QStringLiteral( "evidence_gaps" ) ).toArray();
    for ( const QJsonValue &v : gaps )
        vm.evidenceGaps.append( v.toString() );
    if ( gaps.isEmpty() )
    {
        const QJsonArray gaps2 = debuggerReportJson.value( QStringLiteral( "evidenceGaps" ) ).toArray();
        for ( const QJsonValue &v : gaps2 )
            vm.evidenceGaps.append( v.toString() );
    }

    // Incomplete evidence → confidence downgrade signal for teaching UI.
    if ( vm.verdict == QStringLiteral( "incomplete" )
         || vm.verdict == QStringLiteral( "non_comparable" )
         || !vm.evidenceGaps.isEmpty() || !vm.missingEvidence.isEmpty() )
    {
        if ( confidenceRank( vm.causalConfidence ) >= 2 )
        {
            // Surface that UI must not present high confidence when gaps exist.
            vm.confidenceDowngraded = true;
            vm.causalConfidence = QStringLiteral( "low" );
            vm.issues.append(
                QStringLiteral( "experiment_studio.confidence_downgraded:"
                                " incomplete evidence forbids high/medium presentation" ) );
        }
        else if ( confidenceRank( vm.causalConfidence ) == 0
                  && ( !vm.evidenceGaps.isEmpty() || !vm.missingEvidence.isEmpty() ) )
        {
            vm.confidenceDowngraded = true;
            vm.issues.append(
                QStringLiteral( "experiment_studio.confidence_downgraded:"
                                " evidence gaps present; confidence stays none/low" ) );
        }
    }

    if ( vm.verdict.isEmpty() )
    {
        vm.issues.append(
            QStringLiteral( "experiment_studio.debugger_report_incomplete: missing verdict" ) );
    }
    return vm;
}

} // namespace sicnu::experiment_studio
