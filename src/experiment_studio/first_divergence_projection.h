// first_divergence_projection.h — First Divergence teaching VM (Product F).
// Projects Experiment Debugger FirstDivergenceReport only.
#pragma once

#include "experiment_studio/studio_types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace sicnu::experiment_studio
{

struct FirstDivergenceViewModel
{
    QString schema = QString::fromUtf8( kFirstDivergenceVmSchema );
    QString referenceRunId;
    QString studentRunId;
    QString verdict; ///< identical|equivalent|divergent|incomplete|non_comparable
    bool hasFirstDivergence = false;
    QString divergenceKind;
    QString causalConfidence;
    QString referenceStepId;
    QString studentStepId;
    QStringList evidence;
    QStringList missingEvidence;
    QStringList evidenceGaps;
    QJsonObject alignment;
    QJsonObject runLevelComparison;
    bool confidenceDowngraded = false; ///< incomplete evidence → lower confidence
    QStringList issues;
    QJsonObject reportJson; ///< full debugger report echo

    QJsonObject toJson() const;
};

/// Project a FirstDivergenceReport JSON (debugger toJson()) into the teaching VM.
FirstDivergenceViewModel projectFirstDivergence( const QJsonObject &debuggerReportJson );

} // namespace sicnu::experiment_studio
