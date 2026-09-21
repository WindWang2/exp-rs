#pragma once

// suitability_level.h — the four-state suitability lattice.
//
// A suitability claim is never a bool: every criterion resolves to
// suitable / marginal / unsuitable / unknown, where unknown means "the
// evidence needed to answer was absent". Unknown ranks ABOVE suitable in
// severity: a report built on partial evidence must not upgrade to
// "suitable" (same fail-closed posture as DatasetQaReport's
// "never Pass on partial evidence").

#include <optional>

#include <QString>
#include <QVector>

namespace sicnu::suitability
{

enum class SuitabilityLevel
{
    Suitable,
    Marginal,
    Unsuitable,
    Unknown,
};

QString suitabilityLevelToString( SuitabilityLevel level );
std::optional<SuitabilityLevel> suitabilityLevelFromString( const QString &text );

/// Severity rank used for aggregation: Suitable(0) < Unknown(1) < Marginal(2)
/// < Unsuitable(3).
int suitabilitySeverityRank( SuitabilityLevel level );

/// Worst level across @p levels. An empty list claims nothing and resolves to
/// Unknown rather than Suitable.
SuitabilityLevel aggregateSuitabilityLevels( const QVector<SuitabilityLevel> &levels );

} // namespace sicnu::suitability
