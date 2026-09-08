// annotation.cpp — annotation serialization + validation.
#include "annotation.h"

#include <cmath>

namespace sicnu::dataset
{

QJsonObject AnnotationRecord::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kAnnotationSerializationVersion );
    json.insert( QStringLiteral( "annotation_id" ), m_annotationId );
    json.insert( QStringLiteral( "target_sample_id" ), m_targetSampleId );
    json.insert( QStringLiteral( "dataset_version_id" ), m_datasetVersionId );
    json.insert( QStringLiteral( "revision" ), m_revision );
    json.insert( QStringLiteral( "parent_revision" ), m_parentRevision );
    if ( !m_labelSchemaId.isEmpty() )
    {
        json.insert( QStringLiteral( "label_schema_id" ), m_labelSchemaId );
        json.insert( QStringLiteral( "label_schema_version" ), qint64( m_labelSchemaVersion ) );
    }
    if ( !m_classCode.isEmpty() )
        json.insert( QStringLiteral( "class_code" ), m_classCode );
    if ( !m_geometryWkt.isEmpty() )
        json.insert( QStringLiteral( "geometry_wkt" ), m_geometryWkt );
    json.insert( QStringLiteral( "source_type" ), annotationSourceTypeToString( m_sourceType ) );
    json.insert( QStringLiteral( "source_detail" ), m_sourceDetail );
    json.insert( QStringLiteral( "confidence" ), m_confidence );
    json.insert( QStringLiteral( "review_status" ),
                 annotationReviewStatusToString( m_reviewStatus ) );
    if ( !m_reason.isEmpty() )
        json.insert( QStringLiteral( "reason" ), m_reason );
    if ( !m_authorRole.isEmpty() )
        json.insert( QStringLiteral( "author_role" ), m_authorRole );
    if ( m_createdAtUtc.isValid() )
        json.insert( QStringLiteral( "created_at_utc" ), m_createdAtUtc.toString( Qt::ISODateWithMs ) );
    return json;
}

sicnu::data::Result<AnnotationRecord> AnnotationRecord::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<AnnotationRecord>;
    const qint64 schemaVersion = json.value( QStringLiteral( "schema_version" ) ).toInteger();
    if ( schemaVersion != kAnnotationSerializationVersion )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.annotation_version" ),
            QStringLiteral( "annotation schema version %1 not supported (expected %2)" )
                .arg( schemaVersion )
                .arg( kAnnotationSerializationVersion ),
            DiagnosticSeverity::Error,
        } );
    }
    AnnotationRecord annotation;
    annotation.m_annotationId = json.value( QStringLiteral( "annotation_id" ) ).toString();
    annotation.m_targetSampleId = json.value( QStringLiteral( "target_sample_id" ) ).toString();
    annotation.m_datasetVersionId = json.value( QStringLiteral( "dataset_version_id" ) ).toString();
    if ( annotation.m_annotationId.isEmpty() || annotation.m_targetSampleId.isEmpty() ||
         annotation.m_datasetVersionId.isEmpty() )
    {
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.annotation_invalid" ),
                                            QStringLiteral( "annotation requires annotation_id, target and version" ),
                                            DiagnosticSeverity::Error } );
    }
    annotation.m_revision =
        json.value( QStringLiteral( "revision" ) ).toInt( 1 );
    annotation.m_parentRevision =
        json.value( QStringLiteral( "parent_revision" ) ).toInt( 0 );
    annotation.m_labelSchemaId = json.value( QStringLiteral( "label_schema_id" ) ).toString();
    annotation.m_labelSchemaVersion = quint64( qMax<qint64>(
        0, json.value( QStringLiteral( "label_schema_version" ) ).toInteger() ) );
    annotation.m_classCode = json.value( QStringLiteral( "class_code" ) ).toString();
    annotation.m_geometryWkt = json.value( QStringLiteral( "geometry_wkt" ) ).toString();
    const auto sourceType = annotationSourceTypeFromString(
        json.value( QStringLiteral( "source_type" ) ).toString() );
    if ( !sourceType )
    {
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.annotation_invalid" ),
                                            QStringLiteral( "annotation source_type unknown" ),
                                            DiagnosticSeverity::Error } );
    }
    annotation.m_sourceType = *sourceType;
    annotation.m_sourceDetail = json.value( QStringLiteral( "source_detail" ) ).toObject();
    annotation.m_confidence = json.value( QStringLiteral( "confidence" ) ).toDouble( -1.0 );
    const auto review = annotationReviewStatusFromString(
        json.value( QStringLiteral( "review_status" ) ).toString() );
    if ( !review )
    {
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.annotation_invalid" ),
                                            QStringLiteral( "annotation review_status unknown" ),
                                            DiagnosticSeverity::Error } );
    }
    annotation.m_reviewStatus = *review;
    annotation.m_reason = json.value( QStringLiteral( "reason" ) ).toString();
    annotation.m_authorRole = json.value( QStringLiteral( "author_role" ) ).toString();
    annotation.m_createdAtUtc = QDateTime::fromString(
        json.value( QStringLiteral( "created_at_utc" ) ).toString(), Qt::ISODateWithMs );

    const auto validated = validateAnnotation( annotation );
    if ( !validated )
        return Result::failure( validated.diagnostics() );
    return Result::success( annotation );
}

sicnu::data::Result<void> validateAnnotation( const AnnotationRecord &annotation )
{
    using Result = sicnu::data::Result<void>;
    auto fail = []( const QString &message ) {
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.annotation_invalid" ),
                                            message, DiagnosticSeverity::Error } );
    };
    if ( annotation.annotationId().isEmpty() || annotation.targetSampleId().isEmpty() ||
         annotation.datasetVersionId().isEmpty() )
        return fail( QStringLiteral( "annotation requires ids" ) );
    if ( annotation.revision() < 1 )
        return fail( QStringLiteral( "annotation revision must be >= 1" ) );
    if ( annotation.parentRevision() < 0 || annotation.parentRevision() >= annotation.revision() )
        return fail( QStringLiteral( "annotation parent revision must be < revision" ) );
    if ( !std::isfinite( annotation.confidence() ) )
        return fail( QStringLiteral( "annotation confidence must be finite" ) );
    if ( annotation.confidence() != -1.0 &&
         ( annotation.confidence() < 0.0 || annotation.confidence() > 1.0 ) )
        return fail( QStringLiteral( "annotation confidence must be in [0,1] (or -1 unknown)" ) );

    const bool modelGenerated = annotation.sourceType() == AnnotationSourceType::Pseudo ||
                                annotation.sourceType() == AnnotationSourceType::ModelAssisted;
    if ( modelGenerated )
    {
        const QJsonObject detail = annotation.sourceDetail();
        const QString modelId = detail.value( QStringLiteral( "model_id" ) ).toString();
        const QString modelDigest = detail.value( QStringLiteral( "model_digest" ) ).toString();
        if ( modelId.isEmpty() || modelDigest.isEmpty() )
        {
            return fail( QStringLiteral(
                "pseudo/model-assisted annotations must record model_id and model_digest" ) );
        }
    }
    if ( !annotation.labelSchemaId().isEmpty() && annotation.labelSchemaVersion() == 0 )
        return fail( QStringLiteral( "annotation with a label schema requires a schema version" ) );
    return Result::success();
}

bool isContinuationOf( const AnnotationRecord &candidate, const AnnotationRecord &parent )
{
    return candidate.annotationId() == parent.annotationId() &&
           candidate.targetSampleId() == parent.targetSampleId() &&
           candidate.datasetVersionId() == parent.datasetVersionId() &&
           candidate.parentRevision() == parent.revision() &&
           candidate.revision() == parent.revision() + 1;
}

} // namespace sicnu::dataset
