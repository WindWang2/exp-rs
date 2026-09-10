// sample_promotion.h — promotion of transient pipeline outputs into scientific
// samples (goal 7.0 §B).
//
// Classification / segmentation / annotation workflows produce transient
// data (label rasters, segment tables, annotation layers). Promotion turns
// that data into governed SampleRecords + AnnotationRevision chains under a
// DRAFT dataset version. Contracts enforced here:
//
//   - Class identity is ALWAYS a LabelSchema class code. A pipeline's raw
//     value (e.g. a cv::Mat label integer) may appear only as a
//     promotion-time rule input (`ClassCodeRule`); it is never persisted as
//     identity, and a raw value with no rule fails the promotion listing the
//     missing values (nothing is silently dropped).
//   - Provenance is mandatory: every promoted sample carries the producing
//     asset ref + workflow provenance; every promoted annotation carries its
//     AnnotationSourceType (human/field/pseudo/model-assisted…) and, for
//     model-derived labels, the model identity block.
//   - Writes go only into DRAFT versions (the store refuses otherwise;
//     committed versions stay immutable).
//   - The promoter writes; it never executes anything and never resamples
//     geometry (footprint derivation stays in sample.h's ground contract).
#pragma once

#include "annotation.h"
#include "dataset_store.h"
#include "sample.h"

#include <QSet>

namespace sicnu::dataset
{

/// Maps one pipeline-raw class value to its governing class code. Raw values
/// are promotion-time inputs only — see the file contract above.
struct ClassCodeRule
{
    int rawValue = 0;
    QString classCode;

    friend bool operator==( const ClassCodeRule &, const ClassCodeRule & ) = default;
};

/// Where the promoted data came from. Stamped into every sample's provenance
/// and every annotation's source block.
struct PromotionSource
{
    QString assetId;   ///< producing asset (classified raster, segmentation…)
    quint64 revision = 0;
    QString role = QStringLiteral( "image" );
    /// What kind of evidence produced the labels.
    AnnotationSourceType annotationSource = AnnotationSourceType::ModelAssisted;
    /// For ModelAssisted/Weak/Pseudo labels this MUST carry the model block
    /// ("model_id", "model_digest", "threshold") — validateAnnotation enforces.
    QJsonObject sourceDetail;
    /// Workflow/operator identity + parameter hash + software revision.
    QJsonObject workflowProvenance;

    bool isValid() const { return !assetId.isEmpty(); }
};

/// One classified region (a connected classified area or labeled pixel
/// cluster) of the pipeline output.
struct ClassifiedRegion
{
    int rawValue = 0;      ///< pipeline class value (rule input only)
    QString geometryWkt;   ///< ground geometry (POLYGON or POINT) in version CRS
    QString groupId;       ///< leakage group (scene/object id), empty = ungrouped
    QDateTime timeUtc;
    double confidence = -1.0; ///< label confidence prior, -1 = unknown

    friend bool operator==( const ClassifiedRegion &, const ClassifiedRegion & ) = default;
};

/// One segmentation object (e.g. an OBIA segment id within its source asset).
struct SegmentationObject
{
    QString objectRef;     ///< object/segment id within the source asset
    QString geometryWkt;   ///< optional object polygon
    PixelWindow bounds;    ///< optional bounding window in the source raster
    QString classCode;     ///< optional class assignment (validated when set)
    QString groupId;
    QDateTime timeUtc;
    double confidence = -1.0;

    friend bool operator==( const SegmentationObject &, const SegmentationObject & ) = default;
};

/// One tip annotation to append onto an EXISTING sample.
struct AnnotationPromotion
{
    QString targetSampleId;
    QString classCode;     ///< empty = pure-geometry annotation
    QString geometryWkt;
    double confidence = -1.0;
    AnnotationReviewStatus reviewStatus = AnnotationReviewStatus::Pending;

    friend bool operator==( const AnnotationPromotion &, const AnnotationPromotion & ) = default;
};

/// One temporal member observation of the sample being assembled.
struct TemporalMember
{
    QString memberSampleId;
    QDateTime timeUtc;
    QString assetId;
    quint64 revision = 0;
    bool missing = false;
    double quality = -1.0;

    friend bool operator==( const TemporalMember &, const TemporalMember & ) = default;
};

/// What a promotion did — returned even on partial rejection paths so the
/// caller can log exact counts; write failures carry typed diagnostics.
struct PromotionReport
{
    int samplesWritten = 0;
    int annotationsWritten = 0;
    QStringList unmappedRawValues; ///< raw values with no rule (promotion refused)
    QJsonObject toJson() const;
};

class SamplePromoter
{
  public:
    explicit SamplePromoter( DatasetStore &store );

    /// Classification regions → polygon/point samples + polygon annotations.
    /// @p targetSchema (recommended) validates every mapped class code.
    sicnu::data::Result<PromotionReport> promoteClassification(
        const DatasetVersionId &version, const PromotionSource &source,
        const QVector<ClassCodeRule> &rules, const QVector<ClassifiedRegion> &regions,
        const LabelSchema *targetSchema = nullptr ) const;

    /// Segmentation objects → object samples (optionally with polygon
    /// annotations when a class was assigned).
    sicnu::data::Result<PromotionReport> promoteSegmentation(
        const DatasetVersionId &version, const PromotionSource &source,
        const QVector<SegmentationObject> &objects,
        const LabelSchema *targetSchema = nullptr ) const;

    /// Annotation-only promotion: new revision-1 chains on existing samples.
    sicnu::data::Result<PromotionReport> promoteAnnotations(
        const DatasetVersionId &version, const PromotionSource &source,
        const QVector<AnnotationPromotion> &annotations,
        const LabelSchema *targetSchema = nullptr ) const;

    /// Assembles a pre/post (or bi-temporal) pair sample. @p eventGroup is
    /// REQUIRED (pairs without a shared event key cannot be leakage-audited);
    /// both member ids must already exist in the version.
    sicnu::data::Result<QString> promotePair(
        const DatasetVersionId &version, const PromotionSource &source,
        const QString &primarySampleId, const QString &secondarySampleId,
        const QString &pairRole, const QString &eventGroup ) const;

    /// Assembles a temporal sample from member observations. Missing
    /// observations are preserved (a known hole is information).
    sicnu::data::Result<QString> promoteTemporal(
        const DatasetVersionId &version, const PromotionSource &source,
        const QVector<TemporalMember> &members, const QDateTime &targetTimeUtc ) const;

  private:
    sicnu::data::Result<void> requireDraftVersion( const DatasetVersionId &version ) const;
    sicnu::data::Result<void> requireKnownClasses( const QSet<QString> &codes,
                                                   const LabelSchema *schema ) const;
    SampleRecord baseSample( const DatasetVersionId &version, SampleKind kind,
                            const PromotionSource &source ) const;
    AnnotationRecord baseAnnotation( const DatasetVersionId &version,
                                     const PromotionSource &source ) const;

    DatasetStore *m_store = nullptr;
};

} // namespace sicnu::dataset
