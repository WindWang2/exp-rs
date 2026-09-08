// evaluation.h — typed metric contracts + evaluation protocol
// (goal §25/§26, ADR 0137).
//
// Every metric set is bound to its EvaluationProtocol: dataset version,
// split, subset, ignore labels, mask, thresholds, matching policy,
// aggregation. The same model under two protocols is two NON-comparable
// records — the protocol is part of the metric's identity.
//
// All formulas are implemented exactly once here and pinned by known-answer
// tests (goal §51: fixed confusion matrix → known metric values).
#pragma once

#include "../data/data_result.h"

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::dataset
{
// forward: no dependency; evaluation reuses plain value types only
}

namespace sicnu::experiment
{

using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::Result;

// --- Confusion-matrix family (classification + segmentation pixel metrics) --

/// Row = truth label, column = predicted label; labels are the schema class
/// codes in the recorded order (identity by CODE, never by row index).
class ConfusionMatrix
{
  public:
    ConfusionMatrix() = default;
    ConfusionMatrix( const QStringList &labels, qint64 size )
      : m_labels( labels )
      , m_counts( QVector<qint64>( int( size * size ), 0 ) )
    {
    }

    const QStringList &labels() const { return m_labels; }
    qint64 size() const { return m_labels.size(); }

    qint64 count( qint64 truthRow, qint64 predictedColumn ) const
    {
        return m_counts.at( int( truthRow * size() + predictedColumn ) );
    }
    void increment( qint64 truthRow, qint64 predictedColumn )
    {
        m_counts[int( truthRow * size() + predictedColumn )] += 1;
    }
    void setCount( qint64 truthRow, qint64 predictedColumn, qint64 value )
    {
        m_counts[int( truthRow * size() + predictedColumn )] = value;
    }

    qint64 total() const;
    qint64 truthTotal( qint64 row ) const;
    qint64 predictedTotal( qint64 column ) const;
    qint64 truePositives( qint64 index ) const { return count( index, index ); }

    // -- derived metrics (each named exactly what it is) -------------------
    double overallAccuracy() const;             ///< diagonal / total
    double balancedAccuracy() const;            ///< mean per-class recall
    struct PerClassMetrics
    {
        QString label;
        double precision = 0.0;
        double recall = 0.0;
        double f1 = 0.0;
        double iou = 0.0;
        qint64 support = 0;
    };
    PerClassMetrics perClass( qint64 index ) const;
    QVector<PerClassMetrics> perClassAll() const;
    double macroPrecision() const;
    double macroRecall() const;
    double macroF1() const;
    double macroIoU() const;
    /// Micro precision/recall/F1 all equal overall accuracy in single-label
    /// multiclass; reported as such (documented, not faked as separate math).
    double microF1() const { return overallAccuracy(); }
    /// Frequency-weighted F1 (weights = class support).
    double weightedF1() const;
    /// Cohen's kappa.
    double kappa() const;
    /// Multivariate Matthews correlation (Gorodkin's R_K).
    double mcc() const;

    QJsonObject toJson() const;
    static Result<ConfusionMatrix> fromJson( const QJsonObject &json );

    friend bool operator==( const ConfusionMatrix &, const ConfusionMatrix & ) = default;

  private:
    QStringList m_labels;
    QVector<qint64> m_counts;
};

// --- Regression --------------------------------------------------------------

struct RegressionMetrics
{
    double mae = 0.0;
    double mse = 0.0;
    double rmse = 0.0;
    double r2 = 0.0;
    double bias = 0.0;
    double mape = 0.0;
    qint64 mapeSkippedZeros = 0; ///< truth==0 rows skipped by MAPE policy
    qint64 count = 0;

    QJsonObject toJson() const;
};

/// MAPE policy: zero-truth rows are SKIPPED AND COUNTED (never divided by
/// zero, never silently dropped).
Result<RegressionMetrics> regressionMetrics( const QVector<double> &truth,
                                             const QVector<double> &predicted );

// --- Segmentation boundary F-score ---------------------------------------------

/// Boundary F-score at tolerance @p tolerancePx between two boundary pixel
/// sets (pixel indices flattened row-major within a known width). A truth
/// boundary pixel is matched when a predicted boundary pixel lies within the
/// tolerance radius; matching is greedy with deterministic order (sorted
/// indices). Scores need both directions; unmatched pixels drive FN/FP.
Result<double> boundaryFScore( const QVector<qint64> &truthBoundary,
                               const QVector<qint64> &predictedBoundary, qint64 imageWidth,
                               double tolerancePx );

// --- Detection AP ----------------------------------------------------------------

struct DetectionBox
{
    double x = 0.0; ///< pixel coordinates, top-left origin
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double confidence = 0.0;
    bool isTruePositive = false; ///< set by the caller AFTER IoU matching

    double iouWith( const DetectionBox &other ) const;
};

/// Average precision for ONE class from confidence-sorted detections whose
/// IoU matching already happened (isTruePositive flags). Uses the all-point
/// interpolation (area under the PR curve). N handled per class by caller.
double averagePrecision( const QVector<DetectionBox> &sortedDetections, qint64 groundTruthCount );

/// mAP over classes: pairs of (detections, truth count) per class.
double meanAveragePrecision( const QVector<QVector<DetectionBox>> &perClassDetections,
                             const QVector<qint64> &perClassTruthCounts );

// --- EvaluationProtocol -------------------------------------------------------------

/// The binding contract of a metric record (goal §26). Two records with
/// different protocols are never merged or averaged.
class EvaluationProtocol
{
  public:
    EvaluationProtocol() = default;

    const QString &datasetVersionId() const { return m_datasetVersionId; }
    void setDatasetVersionId( const QString &id ) { m_datasetVersionId = id; }
    const QString &splitManifestId() const { return m_splitManifestId; }
    void setSplitManifestId( const QString &id ) { m_splitManifestId = id; }
    /// Evaluated subset: "train" | "validation" | "test" | "fold:<n>".
    const QString &subset() const { return m_subset; }
    void setSubset( const QString &subset ) { m_subset = subset; }
    /// Ignore-label class codes (excluded from ALL metric denominators).
    const QStringList &ignoreLabels() const { return m_ignoreLabels; }
    QStringList &ignoreLabels() { return m_ignoreLabels; }
    /// Named mask recipe reference (e.g. a QA mask asset), if any.
    const QString &maskRef() const { return m_maskRef; }
    void setMaskRef( const QString &ref ) { m_maskRef = ref; }
    double iouThreshold() const { return m_iouThreshold; }
    void setIouThreshold( double threshold ) { m_iouThreshold = threshold; }
    /// Confidence threshold applied before metric computation.
    double confidenceThreshold() const { return m_confidenceThreshold; }
    void setConfidenceThreshold( double threshold ) { m_confidenceThreshold = threshold; }
    /// "macro" | "micro" | "weighted" aggregation of per-class metrics.
    const QString &aggregation() const { return m_aggregation; }
    void setAggregation( const QString &aggregation ) { m_aggregation = aggregation; }

    sicnu::data::Result<void> validate() const;
    QJsonObject toJson() const;
    static Result<EvaluationProtocol> fromJson( const QJsonObject &json );

    friend bool operator==( const EvaluationProtocol &, const EvaluationProtocol & ) = default;

  private:
    QString m_datasetVersionId;
    QString m_splitManifestId;
    QString m_subset = QStringLiteral( "test" );
    QStringList m_ignoreLabels;
    QString m_maskRef;
    double m_iouThreshold = 0.5;
    double m_confidenceThreshold = 0.0;
    QString m_aggregation = QStringLiteral( "macro" );
};

/// One persisted metric record = protocol + metrics document + identity.
struct MetricRecord
{
    QString runId;
    EvaluationProtocol protocol;
    QJsonObject metrics;   ///< typed metric documents (confusion_matrix, regression, …)
    QString metricsHash;   ///< content hash binding the record

    QJsonObject toJson() const;
    static Result<MetricRecord> fromJson( const QJsonObject &json );
};

} // namespace sicnu::experiment
