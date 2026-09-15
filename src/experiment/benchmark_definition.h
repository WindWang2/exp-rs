// benchmark_definition.h — formal scientific Benchmark Definition (D19 GOAL §17).
//
// A published definition is immutable content: task family, pinned dataset/
// split/label schema, metric list (resolved through evaluation.* — no
// duplicate formulas), evaluation rules aligned with EvaluationProtocol,
// pseudo-label policy, seed/determinism, and environment pin requirements.
#pragma once

#include "evaluation.h"

#include "../dataset/dataset_types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::experiment
{

inline constexpr int kBenchmarkDefinitionSerializationVersion = 1;

class BenchmarkDefinition
{
  public:
    BenchmarkDefinition() = default;

    const QString &benchmarkId() const { return m_benchmarkId; }
    void setBenchmarkId( const QString &id ) { m_benchmarkId = id; }
    quint64 benchmarkVersion() const { return m_benchmarkVersion; }
    void setBenchmarkVersion( quint64 version ) { m_benchmarkVersion = version; }

    const QString &name() const { return m_name; }
    void setName( const QString &name ) { m_name = name; }
    const QString &description() const { return m_description; }
    void setDescription( const QString &text ) { m_description = text; }

    sicnu::dataset::BenchmarkTaskFamily taskFamily() const { return m_taskFamily; }
    void setTaskFamily( sicnu::dataset::BenchmarkTaskFamily family ) { m_taskFamily = family; }

    const QString &datasetVersionId() const { return m_datasetVersionId; }
    void setDatasetVersionId( const QString &id ) { m_datasetVersionId = id; }
    const QString &splitManifestId() const { return m_splitManifestId; }
    void setSplitManifestId( const QString &id ) { m_splitManifestId = id; }
    const QString &labelSchemaId() const { return m_labelSchemaId; }
    void setLabelSchemaId( const QString &id ) { m_labelSchemaId = id; }
    quint64 labelSchemaVersion() const { return m_labelSchemaVersion; }
    void setLabelSchemaVersion( quint64 version ) { m_labelSchemaVersion = version; }

    /// Metric names requested (e.g. "overall_accuracy", "kappa", "macro_f1").
    /// Formulas live solely in evaluation.* — this list is a selection.
    const QStringList &metricNames() const { return m_metricNames; }
    QStringList &metricNames() { return m_metricNames; }

    /// Evaluation rules (subset / ignore / thresholds) — the protocol skeleton.
    const EvaluationProtocol &protocol() const { return m_protocol; }
    EvaluationProtocol &protocol() { return m_protocol; }

    /// Default true: protected test subsets must not contain pseudo/weak/
    /// model-assisted tip labels (D19 GOAL §15/E2E4).
    bool refusePseudoLabelsInTest() const { return m_refusePseudoLabelsInTest; }
    void setRefusePseudoLabelsInTest( bool refuse ) { m_refusePseudoLabelsInTest = refuse; }

    /// Notes only — manifests are data, never shell scripts (GOAL §29).
    const QStringList &allowedPreprocessing() const { return m_allowedPreprocessing; }
    QStringList &allowedPreprocessing() { return m_allowedPreprocessing; }
    const QStringList &forbiddenLeakage() const { return m_forbiddenLeakage; }
    QStringList &forbiddenLeakage() { return m_forbiddenLeakage; }

    sicnu::dataset::DeterminismGrade determinism() const { return m_determinism; }
    void setDeterminism( sicnu::dataset::DeterminismGrade grade ) { m_determinism = grade; }
    const QString &determinismNote() const { return m_determinismNote; }
    void setDeterminismNote( const QString &note ) { m_determinismNote = note; }
    quint64 seedPolicy() const { return m_seedPolicy; }
    void setSeedPolicy( quint64 seed ) { m_seedPolicy = seed; }

    /// Required environment field names (e.g. "software_revision", "device").
    const QStringList &requiredEnvironmentPins() const { return m_requiredEnvironmentPins; }
    QStringList &requiredEnvironmentPins() { return m_requiredEnvironmentPins; }

    const QJsonObject &metadata() const { return m_metadata; }
    QJsonObject &metadata() { return m_metadata; }

    /// Align protocol pins with definition pins; validate required fields.
    sicnu::data::Result<void> validate() const;
    /// Content digest (canonical JSON; metadata excluded).
    QString contentDigest() const;

    QJsonObject toJson() const;
    static Result<BenchmarkDefinition> fromJson( const QJsonObject &json );

    friend bool operator==( const BenchmarkDefinition &, const BenchmarkDefinition & ) = default;

  private:
    QString m_benchmarkId;
    quint64 m_benchmarkVersion = 1;
    QString m_name;
    QString m_description;
    sicnu::dataset::BenchmarkTaskFamily m_taskFamily =
        sicnu::dataset::BenchmarkTaskFamily::Classification;
    QString m_datasetVersionId;
    QString m_splitManifestId;
    QString m_labelSchemaId;
    quint64 m_labelSchemaVersion = 0;
    QStringList m_metricNames;
    EvaluationProtocol m_protocol;
    bool m_refusePseudoLabelsInTest = true;
    QStringList m_allowedPreprocessing;
    QStringList m_forbiddenLeakage;
    sicnu::dataset::DeterminismGrade m_determinism = sicnu::dataset::DeterminismGrade::Strict;
    QString m_determinismNote;
    quint64 m_seedPolicy = 0;
    QStringList m_requiredEnvironmentPins;
    QJsonObject m_metadata;
};

/// Normalized metric envelope for agent/Workbench consumption (GOAL §18).
struct MetricResult
{
    QString name;
    QString definitionVersion; ///< e.g. "exp-rs.evaluation/1"
    double value = 0.0;
    QString scope;   ///< "overall" | "class:<code>" | "subset:test"
    QString classCode;
    qint64 support = -1;
    QStringList warnings;

    QJsonObject toJson() const;
};

} // namespace sicnu::experiment
