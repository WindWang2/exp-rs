// dataset_quality.h — dataset statistics, composition diagnostics and label
// QA (goal §20/§46).
//
// Statistics are computed from flat input rows (caller-built from sample
// pages + annotation tips) so the module never materializes a full dataset
// in memory. The label QA checks use the shared WKT reader and label
// schema; findings are typed with severity and a quality level gate.
#pragma once

#include "dataset_types.h"
#include "label_schema.h"

#include "../data/data_result.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sicnu::dataset
{

/// One flat row of composition evidence (sample-level).
struct CompositionRow
{
    QString classCode;
    QString sensor;
    QString region;
    QString modality;
    double resolution = 0.0;   ///< 0 = unknown
    double quality = -1.0;     ///< label confidence/quality prior
    double weight = 1.0;
    qint64 timeMs = 0;         ///< for season/year balance
    int year = 0;
    QString season;            ///< "spring|summer|autumn|winter" (caller-derived)
};

/// Dataset composition statistics + imbalance diagnostics (goal §46). This
/// is composition reporting for dataset builders — deliberately NOT
/// promoted to a general "fairness" system.
struct DatasetComposition
{
    qint64 sampleCount = 0;
    QHash<QString, qint64> byClass;
    QHash<QString, qint64> bySensor;
    QHash<QString, qint64> byRegion;
    QHash<QString, qint64> bySeason;
    QHash<QString, qint64> byModality;
    QHash<QString, qint64> byResolution; // rounded-resolution key
    QHash<int, qint64> byYear;
    qint64 missingTimeCount = 0;
    qint64 unknownClassCount = 0;
    qint64 zeroWeightCount = 0;

    QJsonObject toJson() const;
};

/// Imbalance finding: one dimension + its distribution.
struct ImbalanceFinding
{
    QString dimension;        ///< "class", "region", "sensor", "season", "resolution"
    QString detail;           ///< dominant vs minority summary
    QJsonObject distribution;
};

/// Computes composition statistics from rows. @p knownClasses (optional)
/// turns unknown-class rows into a counted anomaly instead of a silent
/// bucket.
DatasetComposition computeComposition( const QVector<CompositionRow> &rows,
                                       const LabelSchema *knownClasses = nullptr );

/// Imbalance diagnostics: a dimension is flagged when its max/min non-zero
/// bucket ratio exceeds @p imbalanceRatio (e.g. 10.0). Only dimensions with
/// >= 2 non-zero buckets are considered.
QVector<ImbalanceFinding> imbalanceFindings( const DatasetComposition &composition,
                                             double imbalanceRatio = 10.0 );

// --- Label QA -----------------------------------------------------------------

/// One label quality finding.
struct LabelQualityFinding
{
    QString code;             ///< stable machine code ("label.unknown_class")
    DiagnosticSeverity severity = DiagnosticSeverity::Warning;
    QString sampleId;
    QString annotationId;
    QString message;
    QJsonObject evidence;
};

struct LabelQualityConfig
{
    double tinyPolygonArea = 0.0;  ///< CRS-unit²; 0 = check off
    /// Raster extent for the outside-raster check (CRS units); valid=false
    /// disables the check.
    double rasterMinX = 0.0;
    double rasterMinY = 0.0;
    double rasterMaxX = 0.0;
    double rasterMaxY = 0.0;
    bool hasRasterExtent = false;
};

/// The QA input for one sample: its tip annotations (class + geometry).
struct LabelQaItem
{
    QString sampleId;
    QVector<QString> annotationIds;
    QVector<QString> classCodes;   ///< parallel to annotationIds
    QVector<QString> geometryWkts; ///< parallel; may be empty entries
};

/// Runs the label QA checks (goal §20): empty labels, unknown classes,
/// unparsable geometry, out-of-raster geometry, tiny polygons, conflicting
/// tip labels on one sample, duplicate annotations.
QVector<LabelQualityFinding> labelQualityAudit( const QVector<LabelQaItem> &items,
                                                const LabelSchema &schema,
                                                const LabelQualityConfig &config = {} );

/// Quality gate: maps findings to a level recommendation — errors force
/// Draft; any warning keeps Valid out of Certified; clean content can be
/// Certified. (The human decision stays with the caller; this is the gate
/// arithmetic, goal §20.)
DatasetQualityLevel recommendQualityLevel( const QVector<LabelQualityFinding> &findings );

} // namespace sicnu::dataset
