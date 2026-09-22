// artifact_metrics.h — artifact digest + run-metric comparison at and around
// the divergence (RS14-06, ADR 0174). Slice E.
//
// All comparisons are read-only projections over recorded evidence. Digests
// are never computed here; metrics are read through the platform's existing
// dotted-path lookup. Honesty rules:
//   - digests recorded in different modes are IncomparableModes, never equal;
//   - a metric leaf present on one side only is reported one-sided, never
//     zero-filled, never dropped;
//   - both comparisons are bounded (deterministic order, documented cut).
#pragma once

#include "../../data/data_result.h"
#include "first_divergence.h"
#include "run_snapshot.h"
#include "step_aligner.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sicnu::experiment::debugger
{

inline constexpr int kDefaultMaxMetricLeaves = 256;

struct ArtifactComparison
{
    enum class DigestVerdict
    {
        BothAbsent,        ///< neither side recorded a digest
        Equal,
        Different,
        IncomparableModes, ///< both present, different mode tags — cannot mix
        OneSided,          ///< exactly one side recorded a digest
    };

    QString referenceStepId;
    QString studentStepId;
    QString operatorId;
    DigestVerdict digestVerdict = DigestVerdict::BothAbsent;
    QString referenceDigest;
    QString referenceMode;
    QString studentDigest;
    QString studentMode;
    bool sizesComparable = false;
    qint64 referenceSizeBytes = -1;
    qint64 studentSizeBytes = -1;

    QJsonObject toJson() const;
    bool operator==( const ArtifactComparison & ) const = default;
};

struct MetricDeltaFinding
{
    QString path;
    bool referencePresent = false;
    bool studentPresent = false;
    double referenceValue = 0.0;
    double studentValue = 0.0;
    double delta = 0.0;   ///< student − reference (both present only)

    QJsonObject toJson() const;
    bool operator==( const MetricDeltaFinding & ) const = default;
};

class ArtifactMetricComparer
{
  public:
    /// Digest verdicts for every matched step pair that recorded any output
    /// identity, in reference topological order.
    static QVector<ArtifactComparison> compareStepOutputs(
        const RunSnapshot &reference,
        const RunSnapshot &student,
        const AlignmentResult &alignment,
        int maxEntries = kDefaultMaxDivergenceFindings );

    /// Numeric metric-leaf deltas over the union of both runs' metric
    /// documents, deterministic sorted-path order, capped at @p maxLeaves
    /// (the sorted-order cut is part of the contract, not noise).
    static QVector<MetricDeltaFinding> compareRunMetrics(
        const RunSnapshot &reference,
        const RunSnapshot &student,
        int maxLeaves = kDefaultMaxMetricLeaves );
};

} // namespace sicnu::experiment::debugger
