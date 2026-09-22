// rubric_builder.h — Rubric Builder for process grader + lab artifact rules.
// Only kinds the existing graders support; no hidden scoring formulas.
#pragma once

#include "admin_types.h"

#include <QJsonObject>
#include <QSet>
#include <QString>

namespace sicnu::teaching_admin {

/// Artifact assertion kinds from lab_rules.schema.json / kKnownKinds.
QSet<QString> supportedLabRuleKinds();

/// Process criterion kinds from sicnu.grader.rubric/1.
QSet<QString> supportedProcessCriterionKinds();

/// Validate lab artifact rules document (sicnu.lab.rules/1 shape).
ValidationResult validateLabRules( const QJsonObject &rules );

/// Validate process GradingRubric document (sicnu.grader.rubric/1 shape, structural).
ValidationResult validateProcessRubric( const QJsonObject &rubric );

/// Live weight sum check: returns sum and whether it matches expected (default 1.0 ± eps).
struct WeightSumCheck
{
    double sum = 0.0;
    bool matchesExpected = false;
    double expected = 1.0;
};

WeightSumCheck checkWeightSum( const QJsonArray &weightedItems, const QString &weightKey = QStringLiteral( "weight" ),
                               double expected = 1.0, double eps = 1e-6 );

} // namespace sicnu::teaching_admin
