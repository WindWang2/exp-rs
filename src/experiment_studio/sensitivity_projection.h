// sensitivity_projection.h — Sensitivity / Uncertainty viz VM (Product D).
// Projects study_analysis curves/envelopes/Pareto; Qt-native charts consume this.
#pragma once

#include "experiment_studio/studio_types.h"
#include "study/study_analysis.h"
#include "study/study_export.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::experiment_studio
{

struct SensitivityViewModel
{
    QString schema = QString::fromUtf8( kSensitivityVmSchema );
    QString studyId;
    QVector<sicnu::study::SensitivityCurve> curves;
    QVector<sicnu::study::UncertaintyEnvelope> envelopes;
    QStringList paretoPointIds;
    QJsonObject declaredBest; ///< passthrough; empty unless objectiveMetric set
    QString selectedPointId;
    QJsonObject selectedPointDetail; ///< PointAggregate-like JSON when available
    QJsonObject spatialDeltaSummary; ///< optional SpatialDifferenceSummary JSON
    bool uncertaintyMissingReplicate = false;
    QStringList issues;

    QJsonObject toJson() const;
};

SensitivityViewModel projectSensitivity( const sicnu::study::StudyReport &report,
                                         const QString &selectedPointId = {} );

} // namespace sicnu::experiment_studio
