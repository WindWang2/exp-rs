#pragma once

// suitability_types.h — criterion and gap value objects.
//
// A criterion is one machine-checkable suitability dimension with its level,
// evidence and honest gaps. Serialization is strict and versioned per object:
// a foreign schema_version fails typed (never best-effort parsed).

#include "../data/data_result.h"
#include "suitability_level.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::suitability
{

inline constexpr int kSuitabilityCriterionSerializationVersion = 1;
inline constexpr int kSuitabilityGapSerializationVersion = 1;

/// One concrete, machine-readable deficiency (required minus found). Gaps are
/// statements of fact — producing them never triggers a data search.
struct SuitabilityGap
{
    /// Stable machine code, e.g. "band.missing.nir", "samples.below_minimum".
    QString id;
    /// Back-reference to the owning criterion id.
    QString criterionId;
    QString description;
    QJsonObject evidence;

    QJsonObject toJson() const;
    static sicnu::data::Result<SuitabilityGap> fromJson( const QJsonObject &json );
};

struct SuitabilityCriterion
{
    /// Stable machine id, e.g. "spatial.coverage", "temporal.seasonality".
    QString id;
    /// False only when the profile + goal jointly declare the dimension not
    /// applicable (e.g. no model pinned and the profile makes model fit
    /// optional). Missing METADATA is never "not applicable" — that is Unknown.
    bool applicable = true;
    SuitabilityLevel level = SuitabilityLevel::Unknown;
    QString summary;
    QJsonObject evidence;
    /// Honest statements of what could NOT be checked (assumptions made,
    /// sampling performed, CRS skips).
    QStringList notes;
    QVector<SuitabilityGap> gaps;
    QVector<sicnu::data::Diagnostic> diagnostics;

    QJsonObject toJson() const;
    static sicnu::data::Result<SuitabilityCriterion> fromJson( const QJsonObject &json );
};

/// Canonical criterion ordering (by id) for deterministic reports.
bool criterionIdLessThan( const SuitabilityCriterion &a, const SuitabilityCriterion &b );

} // namespace sicnu::suitability
