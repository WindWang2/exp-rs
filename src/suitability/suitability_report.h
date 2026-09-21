#pragma once

// suitability_report.h — the top-level suitability assessment report.
//
// The report is a pure value object: criteria arrive pre-computed and are kept
// in canonical id order so the serialized form (and its content digest) is
// deterministic for identical evidence regardless of evaluation order. The
// digest is the persistence/provenance handle for downstream consumers.

#include "../data/data_result.h"
#include "suitability_types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::suitability
{

inline constexpr int kSuitabilityReportSerializationVersion = 1;

class SuitabilityReport
{
    public:
        SuitabilityReport() = default;

        /// The assessed dataset version, when the subject is a dataset.
        const QString &datasetVersionId() const { return m_datasetVersionId; }
        void setDatasetVersionId( QString id ) { m_datasetVersionId = std::move( id ); }

        /// The assessed scene/asset ids, when the subject is a scene set.
        const QStringList &sceneIds() const { return m_sceneIds; }
        void setSceneIds( QStringList ids ) { m_sceneIds = std::move( ids ); }

        /// Digest of the goal the assessment was run against. Pairing goal and
        /// report digests is what makes a persisted report replayable.
        const QString &goalDigest() const { return m_goalDigest; }
        void setGoalDigest( QString digest ) { m_goalDigest = std::move( digest ); }

        /// Criteria in canonical (id-ordered) sequence. addCriterion inserts
        /// in order; the report never carries duplicates by id (a later add
        /// for an existing id replaces the earlier entry in place).
        const QVector<SuitabilityCriterion> &criteria() const { return m_criteria; }
        QVector<SuitabilityCriterion> &criteria() { return m_criteria; }
        void addCriterion( SuitabilityCriterion criterion );

        /// Worst level across applicable criteria. Not-applicable criteria are
        /// excluded; with nothing applicable (or an empty report) the overall
        /// level is Unknown — a report with no applicable evidence never
        /// claims "suitable".
        SuitabilityLevel overallLevel() const;

        /// Every gap from every criterion, sorted by gap id.
        QVector<SuitabilityGap> allGaps() const;

        /// SHA-256 over the canonical compact JSON. Never serializes the
        /// digest itself (no circularity).
        QString contentDigest() const;

        QJsonObject toJson() const;
        static sicnu::data::Result<SuitabilityReport> fromJson( const QJsonObject &json );

    private:
        QString m_datasetVersionId;
        QStringList m_sceneIds;
        QString m_goalDigest;
        QVector<SuitabilityCriterion> m_criteria;
};

} // namespace sicnu::suitability
