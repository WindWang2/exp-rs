// dataset_qa_report.h — multi-category Dataset QA with honest verdicts (D19 §16).
//
// Aggregates existing composition/label QA + leakage/pseudo/provenance signals
// into a structured report. Avoids one opaque score: each category carries
// PASS/WARN/FAIL/UNKNOWN plus evidence.
#pragma once

#include "dataset_quality.h"
#include "dataset_types.h"
#include "leakage_audit.h"
#include "sample_catalog.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace sicnu::dataset
{

inline constexpr int kDatasetQaReportSerializationVersion = 1;

struct DatasetQaCategory
{
    QString name; ///< "composition"|"labels"|"leakage"|"pseudo_labels"|"provenance"|"identity"
    AuditVerdict verdict = AuditVerdict::Unknown;
    QString summary;
    QJsonObject evidence;
    QVector<Diagnostic> diagnostics;
};

class DatasetQaReport
{
  public:
    DatasetQaReport() = default;

    const QString &datasetVersionId() const { return m_datasetVersionId; }
    void setDatasetVersionId( const QString &id ) { m_datasetVersionId = id; }
    const QString &splitManifestId() const { return m_splitManifestId; }
    void setSplitManifestId( const QString &id ) { m_splitManifestId = id; }

    QVector<DatasetQaCategory> &categories() { return m_categories; }
    const QVector<DatasetQaCategory> &categories() const { return m_categories; }

    /// Worst category verdict (Fail > Warn > Unknown > Pass). Empty → Unknown.
    AuditVerdict overallVerdict() const;

    QJsonObject toJson() const;

  private:
    QString m_datasetVersionId;
    QString m_splitManifestId;
    QVector<DatasetQaCategory> m_categories;
};

struct DatasetQaInputs
{
    QString datasetVersionId;
    QString splitManifestId;
    DatasetComposition composition;
    QVector<ImbalanceFinding> imbalances;
    QVector<LabelQualityFinding> labelFindings;
    std::optional<LeakageReport> leakage;
    SampleCatalogSummary catalogSummary;
    bool provenanceComplete = false; ///< caller asserts source assets + digests present
    bool versionFrozen = false;      ///< committed/deprecated
    qint64 duplicateSampleIds = 0;
};

/// Build a structured QA report from caller-assembled evidence. Does not I/O.
DatasetQaReport buildDatasetQaReport( const DatasetQaInputs &inputs );

/// Map a LeakageReport into PASS/WARN/FAIL/UNKNOWN (no findings + checks ran
/// → Pass; errors → Fail; warnings only → Warn; no checks → Unknown).
AuditVerdict verdictFromLeakageReport( const LeakageReport &report );

} // namespace sicnu::dataset
