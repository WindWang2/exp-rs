// leakage_audit.h — spatial/temporal leakage audit (goal §19, ADR 0136).
//
// The auditor consumes the flat AuditSample view (caller-built from samples
// + annotations + provenance) plus the split outcome, and produces typed
// findings. Every check is explicit: a report lists WHICH checks ran, so
// "no leakage" claims are only ever made per audited check — the platform
// never implies an audit that did not happen.
//
// Scale contract: pairwise checks are bucketed (spatial grid, hash maps by
// parent/scene/digest), so 100k-sample audits stay roughly linear.
#pragma once

#include "dataset_types.h"
#include "split.h"

#include "../data/data_result.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sicnu::dataset
{

/// Serialization schema version of leakage reports.
inline constexpr int kLeakageReportSerializationVersion = 1;

/// The audit-facing view of one sample (superset of SplitInput).
struct AuditSample
{
    SplitInput input;        // identity/group/class/time/bounds
    SplitRole role = SplitRole::Unassigned;
    int fold = -1;

    // Spatial evidence
    double windowWidth = 0.0;  ///< pixel-window size (overlap fractions)
    double windowHeight = 0.0;

    // Content identity
    QString contentDigest;      ///< exact-duplicate check (empty = unknown)

    // Lineage keys
    QString parentPolygonId;    ///< annotation/object parent polygon
    QString sourceObjectId;     ///< segmentation source object
    QString sceneId;            ///< source scene
    QString parentSampleId;     ///< augmentation / pseudo-label parent
    bool pseudoDerived = false; ///< sample whose label came from a model

    // Temporal semantics
    QString eventGroup;         ///< pre/post event identity
    QString pairCounterpartId;  ///< the other member of a pair
};

struct LeakageAuditConfig
{
    /// Check names (dataset_types LeakageKind strings). Empty = all checks
    /// applicable to the provided evidence.
    QStringList checks;
    /// Center distance under this (CRS units) is a finding (0 = check off).
    double distanceThreshold = 0.0;
    /// Window-overlap fraction at or above this is a finding (0 = any
    /// positive overlap counts; patches of a grid naturally overlap only if
    /// stride < window).
    double overlapFractionThreshold = 0.0;
    /// Buffer-expansion distance for buffer_overlap (0 = check off).
    double bufferDistance = 0.0;
    /// Report only cross-split collisions (default true). Same-split
    /// duplicates are a data-quality matter, not leakage.
    bool crossSplitOnly = true;

    QJsonObject toJson() const;
    static sicnu::data::Result<LeakageAuditConfig> fromJson( const QJsonObject &json );
};

struct LeakageFinding
{
    LeakageKind kind = LeakageKind::ExactDuplicate;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    QString sampleA;
    QString sampleB;
    QJsonObject evidence; ///< check-specific proof (fold ids, distance, digest…)

    QJsonObject toJson() const;
    static sicnu::data::Result<LeakageFinding> fromJson( const QJsonObject &json );
};

class LeakageReport
{
  public:
    LeakageReport() = default;

    const QString &datasetVersionId() const { return m_datasetVersionId; }
    void setDatasetVersionId( const QString &id ) { m_datasetVersionId = id; }
    const QString &splitManifestId() const { return m_splitManifestId; }
    void setSplitManifestId( const QString &id ) { m_splitManifestId = id; }

    QVector<LeakageFinding> &findings() { return m_findings; }
    const QVector<LeakageFinding> &findings() const { return m_findings; }

    /// The checks actually executed (LeakageKind strings) — a report may
    /// only speak about these (goal §19: no unevidenced "no leakage").
    const QStringList &auditedChecks() const { return m_auditedChecks; }
    void setAuditedChecks( const QStringList &checks ) { m_auditedChecks = checks; }

    bool isClean() const { return m_findings.isEmpty(); }

    /// Counts by kind and severity + audited checks; embedded into split
    /// manifests as the leakage summary.
    QJsonObject summary() const;

    QJsonObject toJson() const;
    static sicnu::data::Result<LeakageReport> fromJson( const QJsonObject &json );

  private:
    QString m_datasetVersionId;
    QString m_splitManifestId;
    QStringList m_auditedChecks;
    QVector<LeakageFinding> m_findings;
};

class LeakageAuditor
{
  public:
    /// @p splitIdFormat: pass the manifest id; samples carry role or fold.
    /// Findings accumulate in stable order (sampleA/sampleB sorted by id)
    /// so reports are reproducible.
    static sicnu::data::Result<LeakageReport> audit( const QString &datasetVersionId,
                                                     const QString &splitManifestId,
                                                     const QVector<AuditSample> &samples,
                                                     const LeakageAuditConfig &config );

    /// True when the two placements can collide across splits: different
    /// roles, or fold-based placements that can meet train-vs-test in some
    /// materialization.
    static bool crossSplit( const AuditSample &a, const AuditSample &b,
                            bool foldBased );
};

} // namespace sicnu::dataset
