// annotation.h — annotation provenance and revision chains (ADR 0135).
//
// An Annotation is an immutable revision chain, not a mutable row: every
// change appends a revision with its parent revision id, source, confidence,
// review status and reason. The current label is the tip; history is
// evidence. Pseudo/model-assisted labels must record the producing model
// (id + digest + threshold) so downstream runs can audit where weak labels
// came from (goal §44).
#pragma once

#include "dataset_types.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace sicnu::dataset
{

/// Serialization schema version of annotation payloads.
inline constexpr int kAnnotationSerializationVersion = 1;

class AnnotationRecord
{
  public:
    AnnotationRecord() = default;

    /// Stable annotation identity across all revisions.
    const QString &annotationId() const { return m_annotationId; }
    void setAnnotationId( const QString &id ) { m_annotationId = id; }
    /// Sample (or entry) this annotation targets.
    const QString &targetSampleId() const { return m_targetSampleId; }
    void setTargetSampleId( const QString &id ) { m_targetSampleId = id; }
    const QString &datasetVersionId() const { return m_datasetVersionId; }
    void setDatasetVersionId( const QString &id ) { m_datasetVersionId = id; }

    /// Monotonic revision number within the annotation (1-based). 0 = unset
    /// (invalid for persistence).
    int revision() const { return m_revision; }
    void setRevision( int revision ) { m_revision = revision; }
    /// Parent revision number (0 for the first revision).
    int parentRevision() const { return m_parentRevision; }
    void setParentRevision( int revision ) { m_parentRevision = revision; }

    /// Label ontology this annotation's class code belongs to.
    const QString &labelSchemaId() const { return m_labelSchemaId; }
    void setLabelSchemaId( const QString &id ) { m_labelSchemaId = id; }
    quint64 labelSchemaVersion() const { return m_labelSchemaVersion; }
    void setLabelSchemaVersion( quint64 version ) { m_labelSchemaVersion = version; }
    /// Class code under that schema (empty for pure geometry annotations).
    const QString &classCode() const { return m_classCode; }
    void setClassCode( const QString &code ) { m_classCode = code; }
    /// Optional geometry payload (WKT) for segmentation/object annotations.
    const QString &geometryWkt() const { return m_geometryWkt; }
    void setGeometryWkt( const QString &wkt ) { m_geometryWkt = wkt; }

    AnnotationSourceType sourceType() const { return m_sourceType; }
    void setSourceType( AnnotationSourceType type ) { m_sourceType = type; }
    /// Structured source detail. For pseudo/model-assisted labels this MUST
    /// carry model identity: {"model_id","model_digest","threshold",
    /// "generation_config"} (goal §44).
    const QJsonObject &sourceDetail() const { return m_sourceDetail; }
    QJsonObject &sourceDetail() { return m_sourceDetail; }
    /// Confidence in [0,1]; -1 = unknown.
    double confidence() const { return m_confidence; }
    void setConfidence( double confidence ) { m_confidence = confidence; }
    AnnotationReviewStatus reviewStatus() const { return m_reviewStatus; }
    void setReviewStatus( AnnotationReviewStatus status ) { m_reviewStatus = status; }
    /// Why this revision was made ("relabel after field visit", …).
    const QString &reason() const { return m_reason; }
    void setReason( const QString &reason ) { m_reason = reason; }
    /// Role/name of the author (no free-form PII by contract).
    const QString &authorRole() const { return m_authorRole; }
    void setAuthorRole( const QString &role ) { m_authorRole = role; }
    const QDateTime &createdAtUtc() const { return m_createdAtUtc; }
    void setCreatedAtUtc( const QDateTime &time ) { m_createdAtUtc = time; }

    QJsonObject toJson() const;
    static sicnu::data::Result<AnnotationRecord> fromJson( const QJsonObject &json );

    friend bool operator==( const AnnotationRecord &, const AnnotationRecord & ) = default;

  private:
    QString m_annotationId;
    QString m_targetSampleId;
    QString m_datasetVersionId;
    int m_revision = 1;
    int m_parentRevision = 0;
    QString m_labelSchemaId;
    quint64 m_labelSchemaVersion = 0;
    QString m_classCode;
    QString m_geometryWkt;
    AnnotationSourceType m_sourceType = AnnotationSourceType::Human;
    QJsonObject m_sourceDetail;
    double m_confidence = -1.0;
    AnnotationReviewStatus m_reviewStatus = AnnotationReviewStatus::Pending;
    QString m_reason;
    QString m_authorRole;
    QDateTime m_createdAtUtc;
};

/// Structural validation: ids present, revision numbering coherent
/// (revision >= 1; parent < revision), confidence in range, and — the
/// provenance rule that matters — pseudo/model-assisted sources must carry
/// model identity in sourceDetail (goal §44: source model, digest,
/// threshold, generation config).
sicnu::data::Result<void> validateAnnotation( const AnnotationRecord &annotation );

/// True when @p candidate continues the chain of @p parent (same annotation
/// id + target, parentRevision == parent.revision, revision ==
/// parent.revision + 1). Chain integrity is checked before append, never
/// repaired after.
bool isContinuationOf( const AnnotationRecord &candidate, const AnnotationRecord &parent );

} // namespace sicnu::dataset
