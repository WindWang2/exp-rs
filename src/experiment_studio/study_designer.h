// study_designer.h — Study Designer projection (Product A).
//
// Builds a ParameterStudySpec from an operator parameter schema document
// (RSOperator::schema() JSON shape, or a simplified designer input). Does NOT
// define a second sweep — validate() + sampleStudyPoints remain the authority.
#pragma once

#include "experiment_studio/studio_errors.h"
#include "experiment_studio/studio_types.h"
#include "study/study_sampling.h"
#include "study/study_spec.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::experiment_studio
{

/// One sweepable numeric parameter projected from operator schema.
struct DesignerParameterOption
{
    QString parameterPath;
    QString description;
    double schemaMin = 0.0;
    double schemaMax = 0.0;
    bool hasRange = false;
    double defaultValue = 0.0;
    bool sweepable = true; ///< false for string/raster/output/boolean/enum
    QString refuseReason;  ///< set when !sweepable

    QJsonObject toJson() const;
};

/// Machine-readable designer view model (future Agent use).
struct StudyDesignerViewModel
{
    QString schema = QString::fromUtf8( kStudyDesignerVmSchema );
    QString algorithmId;
    QString determinismGrade; ///< "bit_exact" | "tolerance" | "unknown"
    bool operatorSuitable = true;
    QStringList suitabilityIssues;
    QVector<DesignerParameterOption> parameters;
    sicnu::study::ParameterStudySpec draftSpec;
    qint64 estimatedPoints = 0;
    bool draftValid = false;
    QStringList draftIssues; ///< typed codes + messages
    StudioResourcePolicy policy;

    QJsonObject toJson() const;
};

/// Extract sweepable numeric parameters from an operator schema root
/// (properties map or params array). Non-numeric keys become non-sweepable
/// options with refuseReason.
Result<QVector<DesignerParameterOption>> projectOperatorParameters( const QJsonObject &operatorSchema );

/// Assemble a ParameterStudySpec + estimate points. Applies Studio resource
/// policy and core validate()/sampleStudyPoints guardrails. Never truncates.
Result<StudyDesignerViewModel> buildStudyDesignerViewModel(
    const QString &algorithmId,
    const QJsonObject &operatorSchema,
    const QJsonObject &baseParameters,
    sicnu::study::SamplingStrategy strategy,
    const QVector<sicnu::study::ParameterDimension> &dimensions,
    const sicnu::study::StudyBudget &budget,
    const QStringList &metricNames,
    const QString &studyId,
    const QString &experimentId,
    const StudioResourcePolicy &policy = defaultResourcePolicy(),
    const QString &determinismGrade = QStringLiteral( "unknown" ),
    bool spatialComparison = false,
    const QString &objectiveMetric = {},
    const QVector<sicnu::study::StudyMetricSpec> &objectiveMetrics = {} );

/// Convenience: estimate point count without running (uses sampleStudyPoints
/// when draft validates; otherwise combinatorial estimate for messaging).
Result<qint64> estimateStudyPointCount( const sicnu::study::ParameterStudySpec &spec );

} // namespace sicnu::experiment_studio
