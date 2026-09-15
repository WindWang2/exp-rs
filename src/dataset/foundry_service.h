// foundry_service.h — headless Dataset Foundry façade (D19 GOAL §26).
//
// Thin service over DatasetStore + QA/catalog/feature helpers for Agent /
// CLI / future D18 Workbench consumption. Does NOT own UI or MissionContext.
#pragma once

#include "dataset_qa_report.h"
#include "dataset_store.h"
#include "feature_table.h"
#include "sample_catalog.h"

#include <QJsonObject>

namespace sicnu::dataset
{

class DatasetFoundryService
{
  public:
    explicit DatasetFoundryService( DatasetStore *store );

    DatasetStore *store() const { return m_store; }

    /// Bounded dataset listing (delegates to store paging).
    sicnu::data::Result<QPair<qint64, QVector<QVariantMap>>> listDatasets(
        qint64 offset = 0, qint64 limit = DatasetStore::kMaxPageSize ) const;

    /// Inspect one version: status, role, fingerprint, sample count, splits.
    sicnu::data::Result<QJsonObject> inspectVersion( const DatasetVersionId &versionId ) const;

    /// Version lineage ancestors (typed failures on cycles / dangling).
    sicnu::data::Result<QVector<DatasetVersionRecord>> versionLineage(
        const DatasetVersionId &versionId ) const;

    /// Build QA report from caller-supplied inputs (pure assembly).
    DatasetQaReport runQa( const DatasetQaInputs &inputs ) const;

    /// Page a flat catalog view (caller supplies rows — typically built from
    /// store samplesPage + facets + annotations).
    SampleCatalogPage querySamples( const QVector<SampleCatalogRow> &rows,
                                    const SampleCatalogFilter &filter, qint64 offset = 0,
                                    qint64 limit = 500 ) const;

    SampleCatalogSummary summarizeSamples( const QVector<SampleCatalogRow> &rows,
                                           const SampleCatalogFilter &filter = {} ) const;

    FeatureJoinResult joinFeatures( const FeatureSet &featureSet,
                                    const QVector<FeatureRow> &rows,
                                    const QStringList &expectedSampleIds,
                                    const QString &expectedInputVersionId = QString() ) const;

  private:
    DatasetStore *m_store = nullptr;
};

} // namespace sicnu::dataset
