// comparison_ext.h — comparability beyond identity pins (goal 7.0 §G):
// metric-protocol compatibility, label-schema compatibility and paired run
// summaries. Honesty contract: where evidence is insufficient the result
// SAYS so ("insufficient_support") — no fabricated significance, no
// p-value theater. Deltas are reported with their denominators.
#pragma once

#include "../dataset/dataset_types.h"
#include "evaluation.h"

namespace sicnu::dataset
{
class LabelSchema;
}

namespace sicnu::experiment
{

/// EvaluationProtocol compatibility: identical semantics (dataset version,
/// split, subset, ignore labels, mask, thresholds, aggregation) are required
/// before two metric records may be subtracted.
struct ProtocolCompatibility
{
    bool compatible = false;
    QStringList differences; ///< one entry per differing dimension

    QJsonObject toJson() const;
};

ProtocolCompatibility compareProtocols( const EvaluationProtocol &a, const EvaluationProtocol &b );

/// Label-schema compatibility for cross-run metric comparison.
struct SchemaCompatibility
{
    enum class Verdict
    {
        Compatible,                 ///< same id + version (or identical code sets)
        CompatibleWithDifferences,  ///< codes added only (metrics per class remain valid)
        NotComparable,              ///< codes removed/changed identity, or different schema ids
    };
    Verdict verdict = Verdict::Compatible;
    QStringList addedCodes;
    QStringList removedCodes;
    QStringList reasons;

    QJsonObject toJson() const;
};

SchemaCompatibility compareLabelSchemas( const sicnu::dataset::LabelSchema &a,
                                         const sicnu::dataset::LabelSchema &b );

/// One metric's paired delta with its evidence.
struct PairedMetricDelta
{
    QString metric;
    double valueA = 0.0;
    double valueB = 0.0;
    double delta = 0.0;          ///< b - a
    qint64 supportA = -1;        ///< denominators when recorded (per-class support / count)
    qint64 supportB = -1;
    bool insufficientSupport = false; ///< support < minimum — delta reported but flagged
    QJsonObject toJson() const;
};

/// Paired summary of two metric records under compatible protocols. Metrics
/// are matched by name over the two documents; scalar numeric leaves are
/// differenced, per-class entries (class code keys) are differenced per code
/// with their supports. Runs whose class supports sum below @p
/// minTestClassSupport get "insufficient_support" on the affected metrics —
/// the summary never invents a significance verdict.
struct PairedRunSummary
{
    bool protocolsCompatible = false;
    bool schemasCompatible = false;
    QVector<PairedMetricDelta> deltas;
    QStringList notes;

    QJsonObject toJson() const;
};

PairedRunSummary pairedRunComparison( const MetricRecord &a, const MetricRecord &b,
                                      const SchemaCompatibility *schemaCompat = nullptr,
                                      qint64 minTestClassSupport = 30 );

} // namespace sicnu::experiment
