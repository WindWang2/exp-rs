// sample_catalog.h — bounded sample catalog query (D19 GOAL §12/§27).
//
// Filters logical sample views without requiring every heavy payload in
// memory. Callers page DatasetStore samples and feed flat SampleCatalogRow
// views; the catalog filters/pages those views. Designed for 100k–1M
// logical samples via page/window, never all-rows × all-metadata.
#pragma once

#include "dataset_types.h"
#include "sample.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace sicnu::dataset
{

/// Flat, filterable view of one sample (caller-built from SampleRecord +
/// tip annotation + facets). Heavy geometry/payload stays out of this row.
struct SampleCatalogRow
{
    QString sampleId;
    SampleKind kind = SampleKind::Point;
    QString classCode;
    QString sensor;
    QString region;
    QString modality;
    int year = 0;
    QString splitRole; ///< "train"|"validation"|"test"|"" 
    QString quality;   ///< free-form quality bucket ("high"|"low"|…)
    AnnotationSourceType labelSource = AnnotationSourceType::Human;
    bool hasPseudoLabel = false;
    QString groupId;
    QString crs; ///< per-sample CRS authority string ("" = unspecified; #1007 QA evidence)
};

struct SampleCatalogFilter
{
    QStringList classCodes;   ///< empty = any
    QStringList sensors;
    QStringList regions;
    QStringList modalities;
    QVector<int> years;
    QStringList splitRoles;
    QStringList qualities;
    std::optional<bool> pseudoLabelsOnly; ///< nullopt = any; true/false filter
    SampleKind kind = SampleKind::Point;
    bool filterByKind = false;
};

struct SampleCatalogPage
{
    qint64 totalMatched = 0;
    qint64 offset = 0;
    qint64 limit = 0;
    QVector<SampleCatalogRow> rows;
};

/// Filter @p rows and return a bounded page. Does not allocate beyond the
/// page size for the returned rows (matched count is computed in one pass).
SampleCatalogPage querySampleCatalog( const QVector<SampleCatalogRow> &rows,
                                      const SampleCatalogFilter &filter,
                                      qint64 offset = 0,
                                      qint64 limit = 500 );

/// Summarize class / sensor / year / pseudo counts for agent-facing inspect
/// without returning the full catalog.
struct SampleCatalogSummary
{
    qint64 total = 0;
    qint64 pseudoLabelCount = 0;
    QHash<QString, qint64> byClass;
    QHash<QString, qint64> bySensor;
    QHash<QString, qint64> byRegion;
    QHash<int, qint64> byYear;
    QHash<QString, qint64> bySplitRole;
};

SampleCatalogSummary summarizeSampleCatalog( const QVector<SampleCatalogRow> &rows,
                                             const SampleCatalogFilter &filter = {} );

} // namespace sicnu::dataset
