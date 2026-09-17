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
#include <QStringList>
#include <QVector>

#include <optional>

namespace sicnu::dataset
{

inline constexpr int kDatasetQaReportSerializationVersion = 1;

struct DatasetQaCategory
{
    QString name; ///< "identity"|"composition"|"labels"|"leakage"|"pseudo_labels"|"provenance"|"crs"
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
    /// When false, labels category stays Unknown even if composition is non-empty
    /// (empty findings must not imply "label QA clean").
    bool labelsAudited = false;
    /// Uniqueness evidence window (#1004): identity stays Unknown when the
    /// catalog scan was capped or the version holds more samples than were
    /// scanned — never Pass on partial evidence. Defaults keep the legacy
    /// verdicts for callers that do not report counts.
    qint64 scannedSamples = 0;
    qint64 totalSamples = 0;
    bool scanCapped = false;
    /// CRS evidence (#1007): manifest schema CRS ("" = mixed/unspecified)
    /// plus the distinct non-empty per-sample CRS strings seen in evidence.
    QString schemaCrs;
    QStringList distinctSampleCrs;
};

/// Build a structured QA report from caller-assembled evidence. Does not I/O.
DatasetQaReport buildDatasetQaReport( const DatasetQaInputs &inputs );

/// Map a LeakageReport into PASS/WARN/FAIL/UNKNOWN (no findings + checks ran
/// → Pass; errors → Fail; warnings only → Warn; no checks → Unknown).
AuditVerdict verdictFromLeakageReport( const LeakageReport &report );

} // namespace sicnu::dataset
