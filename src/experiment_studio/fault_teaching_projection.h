// fault_teaching_projection.h — Fault Injection teaching mode (Product E).
// Consumes faultlab scenarios; sandbox only; never mutates originals.
#pragma once

#include "experiment_studio/studio_errors.h"
#include "experiment_studio/studio_types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace sicnu::experiment_studio
{

enum class FaultTeachingPhase
{
    Idle,
    Predict,
    Run,
    Evidence,
    Diagnose,
    Complete,
};

QString faultTeachingPhaseToString( FaultTeachingPhase phase );

struct FaultTeachingViewModel
{
    QString schema = QString::fromUtf8( kFaultTeachingVmSchema );
    FaultTeachingPhase phase = FaultTeachingPhase::Idle;
    QString scenarioId;
    QString title;
    QString learningObjective;
    QString expectedDiagnosisSignature;
    QString studentPrediction;     ///< student-entered signature guess
    QString systemDiagnosis;       ///< diagnoseTransition / runner result
    bool diagnosisMatch = false;
    bool diagnosisMismatch = false;
    bool sandboxUnchangedOriginal = true; ///< contract: originals never mutated
    QString sandboxPath;
    QJsonObject evidence; ///< observables / report slice
    QStringList issues;
    QJsonObject scenarioJson; ///< echo for export

    QJsonObject toJson() const;
};

/// Project a loaded scenario into Predict phase (no run yet).
FaultTeachingViewModel projectFaultScenarioPredict( const QJsonObject &scenarioJson );

/// Advance to evidence+diagnose after a sandbox run. @p originalFingerprint
/// and @p postFingerprint must match for sandboxUnchangedOriginal.
FaultTeachingViewModel projectFaultDiagnosis(
    const FaultTeachingViewModel &prior,
    const QString &studentPrediction,
    const QString &systemDiagnosis,
    const QJsonObject &evidence,
    const QString &sandboxPath,
    const QString &originalFingerprint,
    const QString &postRunOriginalFingerprint );

} // namespace sicnu::experiment_studio
