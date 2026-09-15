// benchmark_runner.h — headless Benchmark Runner (D19 GOAL §19).
//
// Definition → pin checks → optional pseudo-label gate → predictions →
// evaluation metrics → BenchmarkResult. Usable from CLI / experiment /
// Agent; does not implement D18 UI. Model execution is supplied by the
// caller (predictions in); the runner never schedules a second engine.
#pragma once

#include "benchmark_definition.h"
#include "evaluation.h"

#include "../dataset/dataset_types.h"
#include "../dataset/sample_catalog.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sicnu::dataset
{
class DatasetStore;
}

namespace sicnu::experiment
{

inline constexpr int kBenchmarkResultSerializationVersion = 1;

enum class BenchmarkRunStatus
{
    Completed,
    Failed,
    Aborted,
};

QString benchmarkRunStatusToString( BenchmarkRunStatus status );

struct BenchmarkPrediction
{
    QString sampleId;
    QString predictedClass; ///< classification / change label code
    double confidence = 1.0;
    double predictedValue = 0.0; ///< regression
};

struct BenchmarkTruth
{
    QString sampleId;
    QString truthClass;
    double truthValue = 0.0;
    sicnu::dataset::AnnotationSourceType labelSource =
        sicnu::dataset::AnnotationSourceType::Human;
};

class BenchmarkResult
{
  public:
    BenchmarkResult() = default;

    const QString &resultId() const { return m_resultId; }
    void setResultId( const QString &id ) { m_resultId = id; }
    const QString &benchmarkId() const { return m_benchmarkId; }
    void setBenchmarkId( const QString &id ) { m_benchmarkId = id; }
    quint64 benchmarkVersion() const { return m_benchmarkVersion; }
    void setBenchmarkVersion( quint64 version ) { m_benchmarkVersion = version; }
    const QString &definitionDigest() const { return m_definitionDigest; }
    void setDefinitionDigest( const QString &digest ) { m_definitionDigest = digest; }

    BenchmarkRunStatus status() const { return m_status; }
    void setStatus( BenchmarkRunStatus status ) { m_status = status; }
    const QString &failureCode() const { return m_failureCode; }
    void setFailureCode( const QString &code ) { m_failureCode = code; }
    const QString &failureMessage() const { return m_failureMessage; }
    void setFailureMessage( const QString &message ) { m_failureMessage = message; }

    const QString &datasetVersionId() const { return m_datasetVersionId; }
    void setDatasetVersionId( const QString &id ) { m_datasetVersionId = id; }
    const QString &splitManifestId() const { return m_splitManifestId; }
    void setSplitManifestId( const QString &id ) { m_splitManifestId = id; }
    const QString &modelId() const { return m_modelId; }
    void setModelId( const QString &id ) { m_modelId = id; }
    const QString &modelDigest() const { return m_modelDigest; }
    void setModelDigest( const QString &digest ) { m_modelDigest = digest; }
    quint64 seed() const { return m_seed; }
    void setSeed( quint64 seed ) { m_seed = seed; }
    const QString &softwareRevision() const { return m_softwareRevision; }
    void setSoftwareRevision( const QString &revision ) { m_softwareRevision = revision; }
    const QString &experimentRunId() const { return m_experimentRunId; }
    void setExperimentRunId( const QString &id ) { m_experimentRunId = id; }

    bool reproducibilityComplete() const { return m_reproducibilityComplete; }
    void setReproducibilityComplete( bool complete ) { m_reproducibilityComplete = complete; }
    const QStringList &reproducibilityGaps() const { return m_reproducibilityGaps; }
    QStringList &reproducibilityGaps() { return m_reproducibilityGaps; }

    QVector<MetricResult> &metrics() { return m_metrics; }
    const QVector<MetricResult> &metrics() const { return m_metrics; }
    const QJsonObject &rawMetrics() const { return m_rawMetrics; }
    void setRawMetrics( const QJsonObject &metrics ) { m_rawMetrics = metrics; }
    const EvaluationProtocol &protocol() const { return m_protocol; }
    void setProtocol( const EvaluationProtocol &protocol ) { m_protocol = protocol; }

    QJsonObject toJson() const;
    static Result<BenchmarkResult> fromJson( const QJsonObject &json );

  private:
    QString m_resultId;
    QString m_benchmarkId;
    quint64 m_benchmarkVersion = 0;
    QString m_definitionDigest;
    BenchmarkRunStatus m_status = BenchmarkRunStatus::Failed;
    QString m_failureCode;
    QString m_failureMessage;
    QString m_datasetVersionId;
    QString m_splitManifestId;
    QString m_modelId;
    QString m_modelDigest;
    quint64 m_seed = 0;
    QString m_softwareRevision;
    QString m_experimentRunId;
    bool m_reproducibilityComplete = false;
    QStringList m_reproducibilityGaps;
    QVector<MetricResult> m_metrics;
    QJsonObject m_rawMetrics;
    EvaluationProtocol m_protocol;
};

struct BenchmarkRunRequest
{
    BenchmarkDefinition definition;
    QVector<BenchmarkTruth> truths;         ///< ground truth for evaluated subset
    QVector<BenchmarkPrediction> predictions;
    QString modelId;
    QString modelDigest;
    quint64 seed = 0;
    QString softwareRevision;
    QString provider;
    QString device;
    QString experimentRunId; ///< optional linkage
    /// When set, runner verifies version/split exist and are frozen.
    sicnu::dataset::DatasetStore *store = nullptr;
};

class BenchmarkRunner
{
  public:
    /// Execute one headless benchmark. Never invents reproducibility.
    static Result<BenchmarkResult> run( const BenchmarkRunRequest &request );
};

/// Extract MetricResult list from a confusion matrix for requested names.
QVector<MetricResult> metricResultsFromConfusion( const ConfusionMatrix &matrix,
                                                  const QStringList &metricNames,
                                                  const QString &scope = QStringLiteral( "overall" ) );

} // namespace sicnu::experiment
