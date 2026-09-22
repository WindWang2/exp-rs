#include "experiment_studio/fault_teaching_projection.h"

#include <QJsonArray>

namespace sicnu::experiment_studio
{

QString faultTeachingPhaseToString( FaultTeachingPhase phase )
{
    switch ( phase )
    {
    case FaultTeachingPhase::Idle:
        return QStringLiteral( "idle" );
    case FaultTeachingPhase::Predict:
        return QStringLiteral( "predict" );
    case FaultTeachingPhase::Run:
        return QStringLiteral( "run" );
    case FaultTeachingPhase::Evidence:
        return QStringLiteral( "evidence" );
    case FaultTeachingPhase::Diagnose:
        return QStringLiteral( "diagnose" );
    case FaultTeachingPhase::Complete:
        return QStringLiteral( "complete" );
    }
    return QStringLiteral( "idle" );
}

QJsonObject FaultTeachingViewModel::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "schema" ), schema );
    o.insert( QStringLiteral( "phase" ), faultTeachingPhaseToString( phase ) );
    o.insert( QStringLiteral( "scenario_id" ), scenarioId );
    o.insert( QStringLiteral( "title" ), title );
    o.insert( QStringLiteral( "learning_objective" ), learningObjective );
    o.insert( QStringLiteral( "expected_diagnosis_signature" ), expectedDiagnosisSignature );
    o.insert( QStringLiteral( "student_prediction" ), studentPrediction );
    o.insert( QStringLiteral( "system_diagnosis" ), systemDiagnosis );
    o.insert( QStringLiteral( "diagnosis_match" ), diagnosisMatch );
    o.insert( QStringLiteral( "diagnosis_mismatch" ), diagnosisMismatch );
    o.insert( QStringLiteral( "sandbox_unchanged_original" ), sandboxUnchangedOriginal );
    o.insert( QStringLiteral( "sandbox_path" ), sandboxPath );
    o.insert( QStringLiteral( "evidence" ), evidence );
    QJsonArray iss;
    for ( const QString &s : issues )
        iss.append( s );
    o.insert( QStringLiteral( "issues" ), iss );
    o.insert( QStringLiteral( "scenario" ), scenarioJson );
    return o;
}

FaultTeachingViewModel projectFaultScenarioPredict( const QJsonObject &scenarioJson )
{
    FaultTeachingViewModel vm;
    vm.phase = FaultTeachingPhase::Predict;
    vm.scenarioJson = scenarioJson;
    vm.scenarioId = scenarioJson.value( QStringLiteral( "scenario_id" ) ).toString();
    if ( vm.scenarioId.isEmpty() )
        vm.scenarioId = scenarioJson.value( QStringLiteral( "scenarioId" ) ).toString();
    vm.title = scenarioJson.value( QStringLiteral( "title" ) ).toString();
    vm.learningObjective =
        scenarioJson.value( QStringLiteral( "learning_objective" ) ).toString();
    if ( vm.learningObjective.isEmpty() )
        vm.learningObjective =
            scenarioJson.value( QStringLiteral( "learningObjective" ) ).toString();
    vm.expectedDiagnosisSignature =
        scenarioJson.value( QStringLiteral( "expected_diagnosis_signature" ) ).toString();
    if ( vm.expectedDiagnosisSignature.isEmpty() )
        vm.expectedDiagnosisSignature =
            scenarioJson.value( QStringLiteral( "expectedDiagnosisSignature" ) ).toString();
    if ( vm.scenarioId.isEmpty() )
    {
        vm.issues.append(
            QStringLiteral( "experiment_studio.fault_scenario_invalid: missing scenario_id" ) );
    }
    return vm;
}

FaultTeachingViewModel projectFaultDiagnosis(
    const FaultTeachingViewModel &prior,
    const QString &studentPrediction,
    const QString &systemDiagnosis,
    const QJsonObject &evidence,
    const QString &sandboxPath,
    const QString &originalFingerprint,
    const QString &postRunOriginalFingerprint )
{
    FaultTeachingViewModel vm = prior;
    vm.phase = FaultTeachingPhase::Complete;
    vm.studentPrediction = studentPrediction;
    vm.systemDiagnosis = systemDiagnosis;
    vm.evidence = evidence;
    vm.sandboxPath = sandboxPath;
    vm.sandboxUnchangedOriginal = ( originalFingerprint == postRunOriginalFingerprint )
                                  && !originalFingerprint.isEmpty();
    if ( !vm.sandboxUnchangedOriginal )
    {
        vm.issues.append(
            QStringLiteral( "experiment_studio.fault_sandbox_mutated_original:"
                            " original fixture fingerprint changed — contract violation" ) );
    }
    const QString expected = vm.expectedDiagnosisSignature;
    const bool studentOk = !studentPrediction.isEmpty()
                           && ( studentPrediction == systemDiagnosis
                                || ( !expected.isEmpty() && studentPrediction == expected ) );
    const bool systemOk = expected.isEmpty() || systemDiagnosis == expected;
    vm.diagnosisMatch = studentOk && systemOk
                        && studentPrediction == systemDiagnosis;
    vm.diagnosisMismatch = !vm.diagnosisMatch;
    if ( vm.diagnosisMismatch )
    {
        vm.issues.append(
            QStringLiteral( "experiment_studio.fault_diagnosis_mismatch:"
                            " student=%1 system=%2 expected=%3" )
                .arg( studentPrediction, systemDiagnosis, expected ) );
    }
    return vm;
}

} // namespace sicnu::experiment_studio
