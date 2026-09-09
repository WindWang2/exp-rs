// sample_promotion.cpp — see sample_promotion.h for the contract.
#include "sample_promotion.h"

#include "label_schema.h"
#include <QJsonArray>

#include <QSet>

namespace sicnu::dataset
{

namespace
{

using sicnu::data::Diagnostic;
using VoidResult = sicnu::data::Result<void>;

Diagnostic promotionDiag( const QString &code, const QString &message )
{
    return Diagnostic{ code, message, DiagnosticSeverity::Error };
}

/// Typed failure helpers — `Result` is a template, so each return type
/// builds its own failure explicitly.
template <typename T>
sicnu::data::Result<T> failAs( const QString &code, const QString &message )
{
    return sicnu::data::Result<T>::failure( promotionDiag( code, message ) );
}

template <typename T>
sicnu::data::Result<T> failAs( const QVector<Diagnostic> &diagnostics )
{
    return sicnu::data::Result<T>::failure( diagnostics );
}

VoidResult failure( const QString &code, const QString &message )
{
    return VoidResult::failure( promotionDiag( code, message ) );
}

} // namespace

QJsonObject PromotionReport::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "samples_written" ), samplesWritten );
    json.insert( QStringLiteral( "annotations_written" ), annotationsWritten );
    QJsonArray unmapped;
    for ( const QString &value : unmappedRawValues )
        unmapped.append( value );
    json.insert( QStringLiteral( "unmapped_raw_values" ), unmapped );
    return json;
}

SamplePromoter::SamplePromoter( DatasetStore &store )
    : m_store( &store )
{
}

VoidResult SamplePromoter::requireDraftVersion( const DatasetVersionId &version ) const
{
    const auto record = m_store->versionById( version );
    if ( !record )
        return failure( QStringLiteral( "dataset.not_found" ),
                        QStringLiteral( "version %1 does not exist" ).arg( version.toString() ) );
    if ( !record->isMutable() )
        return failure( QStringLiteral( "dataset.not_draft" ),
                        QStringLiteral( "version %1 is not a draft; promotion only writes"
                                        " into draft versions" )
                            .arg( version.toString() ) );
    return VoidResult::success();
}

VoidResult SamplePromoter::requireKnownClasses( const QSet<QString> &codes,
                                                const LabelSchema *schema ) const
{
    if ( !schema )
        return VoidResult::success();
    for ( const QString &code : codes )
    {
        if ( !schema->classByCode( code ) )
            return failure( QStringLiteral( "dataset.promotion_unknown_class" ),
                            QStringLiteral( "class code '%1' is not in the target label"
                                            " schema" )
                                .arg( code ) );
    }
    return VoidResult::success();
}

SampleRecord SamplePromoter::baseSample( const DatasetVersionId &version, SampleKind kind,
                                         const PromotionSource &source ) const
{
    SampleRecord sample;
    sample.setSampleId( SampleId::generate().toString() );
    sample.setDatasetVersionId( version.toString() );
    sample.setKind( kind );
    QJsonObject provenance;
    provenance.insert( QStringLiteral( "promotion" ), QStringLiteral( "pipeline" ) );
    if ( !source.assetId.isEmpty() )
    {
        QJsonObject assetRef;
        assetRef.insert( QStringLiteral( "asset_id" ), source.assetId );
        assetRef.insert( QStringLiteral( "revision" ), qint64( source.revision ) );
        assetRef.insert( QStringLiteral( "role" ), source.role );
        provenance.insert( QStringLiteral( "producing_asset" ), assetRef );
    }
    if ( !source.workflowProvenance.isEmpty() )
        provenance.insert( QStringLiteral( "workflow" ), source.workflowProvenance );
    if ( !source.sourceDetail.isEmpty() )
        provenance.insert( QStringLiteral( "label_source" ), source.sourceDetail );
    provenance.insert( QStringLiteral( "label_source_type" ),
                       annotationSourceTypeToString( source.annotationSource ) );
    sample.provenance() = provenance;
    return sample;
}

AnnotationRecord SamplePromoter::baseAnnotation( const DatasetVersionId &version,
                                                 const PromotionSource &source ) const
{
    AnnotationRecord annotation;
    annotation.setAnnotationId( AnnotationId::generate().toString() );
    annotation.setDatasetVersionId( version.toString() );
    annotation.setRevision( 1 );
    annotation.setParentRevision( 0 );
    annotation.setSourceType( source.annotationSource );
    annotation.sourceDetail() = source.sourceDetail;
    annotation.setReason( QStringLiteral( "pipeline promotion" ) );
    return annotation;
}

sicnu::data::Result<PromotionReport> SamplePromoter::promoteClassification(
    const DatasetVersionId &version, const PromotionSource &source,
    const QVector<ClassCodeRule> &rules, const QVector<ClassifiedRegion> &regions,
    const LabelSchema *targetSchema ) const
{
    using TypedResult = sicnu::data::Result<PromotionReport>;
    if ( !m_store )
        return failAs<PromotionReport>( QStringLiteral( "dataset.store_closed" ),
                                        QStringLiteral( "store closed" ) );
    if ( !source.isValid() )
        return failAs<PromotionReport>( QStringLiteral( "dataset.promotion_invalid_source" ),
                                        QStringLiteral( "promotion source carries no asset id" ) );
    auto draft = requireDraftVersion( version );
    if ( !draft )
        return failAs<PromotionReport>( draft.diagnostics() );

    QHash<int, QString> ruleMap;
    QSet<QString> mappedCodes;
    for ( const auto &rule : rules )
    {
        if ( rule.classCode.isEmpty() )
            return failAs<PromotionReport>(
                QStringLiteral( "dataset.promotion_invalid_rule" ),
                QStringLiteral( "rule for raw value %1 maps to an empty class code" )
                    .arg( rule.rawValue ) );
        ruleMap.insert( rule.rawValue, rule.classCode );
        mappedCodes.insert( rule.classCode );
    }
    auto known = requireKnownClasses( mappedCodes, targetSchema );
    if ( !known )
        return failAs<PromotionReport>( known.diagnostics() );

    // Every region raw value must have a rule — collect the gaps first so the
    // refusal lists them all (nothing silently dropped).
    QStringList unmapped;
    QSet<int> seenUnmapped;
    for ( const auto &region : regions )
    {
        if ( !ruleMap.contains( region.rawValue ) && !seenUnmapped.contains( region.rawValue ) )
        {
            seenUnmapped.insert( region.rawValue );
            unmapped.append( QString::number( region.rawValue ) );
        }
    }
    if ( !unmapped.isEmpty() )
        return failAs<PromotionReport>(
            QStringLiteral( "dataset.promotion_unmapped_values" ),
            QStringLiteral( "no class rule for raw values: %1" )
                .arg( unmapped.join( QLatin1String( ", " ) ) ) );

    PromotionReport report;
    QVector<SampleRecord> samples;
    QVector<AnnotationRecord> annotations;
    for ( const auto &region : regions )
    {
        SampleRecord sample = baseSample( version, SampleKind::Polygon, source );
        sample.setGroupId( region.groupId );
        sample.setTimeUtc( region.timeUtc );
        sample.setQuality( region.confidence );
        PolygonSample polygon;
        polygon.wkt = region.geometryWkt;
        sample.payload() = polygon;
        SourceAssetRef assetRef;
        assetRef.assetId = source.assetId;
        assetRef.revision = source.revision;
        assetRef.role = QStringLiteral( "label" );
        sample.sourceAssets().append( assetRef );

        AnnotationRecord annotation = baseAnnotation( version, source );
        annotation.setTargetSampleId( sample.sampleId() );
        annotation.setClassCode( ruleMap.value( region.rawValue ) );
        annotation.setGeometryWkt( region.geometryWkt );
        annotation.setConfidence( region.confidence );
        samples.append( sample );
        annotations.append( annotation );
    }

    // Batch-write samples first (one transaction), then the annotations that
    // reference them. A failed addSamples aborts before any annotation exists.
    if ( !samples.isEmpty() )
    {
        auto written = m_store->addSamples( samples );
        if ( !written )
            return failAs<PromotionReport>( written.diagnostics() );
        report.samplesWritten = samples.size();
    }
    for ( auto &annotation : annotations )
    {
        auto written = m_store->addAnnotation( annotation );
        if ( !written )
            return failAs<PromotionReport>( written.diagnostics() );
        ++report.annotationsWritten;
    }
    return TypedResult::success( report );
}

sicnu::data::Result<PromotionReport> SamplePromoter::promoteSegmentation(
    const DatasetVersionId &version, const PromotionSource &source,
    const QVector<SegmentationObject> &objects, const LabelSchema *targetSchema ) const
{
    if ( !m_store )
        return failAs<PromotionReport>( QStringLiteral( "dataset.store_closed" ),
                                        QStringLiteral( "store closed" ) );
    if ( !source.isValid() )
        return failAs<PromotionReport>( QStringLiteral( "dataset.promotion_invalid_source" ),
                                        QStringLiteral( "promotion source carries no asset id" ) );
    auto draft = requireDraftVersion( version );
    if ( !draft )
        return failAs<PromotionReport>( draft.diagnostics() );

    QSet<QString> codes;
    for ( const auto &object : objects )
    {
        if ( !object.classCode.isEmpty() )
            codes.insert( object.classCode );
        if ( object.objectRef.isEmpty() )
            return failAs<PromotionReport>(
                QStringLiteral( "dataset.promotion_invalid_object" ),
                QStringLiteral( "segmentation object with empty objectRef" ) );
    }
    auto known = requireKnownClasses( codes, targetSchema );
    if ( !known )
        return failAs<PromotionReport>( known.diagnostics() );

    PromotionReport report;
    QVector<SampleRecord> samples;
    QVector<AnnotationRecord> annotations;
    for ( const auto &object : objects )
    {
        SampleRecord sample = baseSample( version, SampleKind::Object, source );
        sample.setGroupId( object.groupId );
        sample.setTimeUtc( object.timeUtc );
        sample.setQuality( object.confidence );
        ObjectSample payload;
        payload.assetId = source.assetId;
        payload.objectRef = object.objectRef;
        payload.bounds = object.bounds;
        sample.payload() = payload;

        if ( !object.geometryWkt.isEmpty() )
        {
            SourceAssetRef assetRef;
            assetRef.assetId = source.assetId;
            assetRef.revision = source.revision;
            assetRef.role = QStringLiteral( "segmentation" );
            sample.sourceAssets().append( assetRef );
        }

        samples.append( sample );
        if ( !object.classCode.isEmpty() )
        {
            AnnotationRecord annotation = baseAnnotation( version, source );
            annotation.setTargetSampleId( sample.sampleId() );
            annotation.setClassCode( object.classCode );
            annotation.setGeometryWkt( object.geometryWkt );
            annotation.setConfidence( object.confidence );
            annotations.append( annotation );
        }
    }

    if ( !samples.isEmpty() )
    {
        auto written = m_store->addSamples( samples );
        if ( !written )
            return failAs<PromotionReport>( written.diagnostics() );
        report.samplesWritten = samples.size();
    }
    for ( auto &annotation : annotations )
    {
        auto written = m_store->addAnnotation( annotation );
        if ( !written )
            return failAs<PromotionReport>( written.diagnostics() );
        ++report.annotationsWritten;
    }
    return sicnu::data::Result<PromotionReport>::success( report );
}

sicnu::data::Result<PromotionReport> SamplePromoter::promoteAnnotations(
    const DatasetVersionId &version, const PromotionSource &source,
    const QVector<AnnotationPromotion> &annotationsIn, const LabelSchema *targetSchema ) const
{
    if ( !m_store )
        return failAs<PromotionReport>( QStringLiteral( "dataset.store_closed" ),
                                        QStringLiteral( "store closed" ) );
    auto draft = requireDraftVersion( version );
    if ( !draft )
        return failAs<PromotionReport>( draft.diagnostics() );

    QSet<QString> codes;
    for ( const auto &item : annotationsIn )
    {
        if ( !item.classCode.isEmpty() )
            codes.insert( item.classCode );
        if ( item.targetSampleId.isEmpty() )
            return failAs<PromotionReport>(
                QStringLiteral( "dataset.promotion_invalid_annotation" ),
                QStringLiteral( "annotation with empty target sample" ) );
    }
    auto known = requireKnownClasses( codes, targetSchema );
    if ( !known )
        return failAs<PromotionReport>( known.diagnostics() );

    // Every target sample must already exist in the version.
    for ( const auto &item : annotationsIn )
    {
        const auto sampleId = SampleId::fromString( item.targetSampleId );
        if ( !sampleId || !m_store->sampleById( version, sampleId.value() ) )
            return failAs<PromotionReport>(
                QStringLiteral( "dataset.sample_not_found" ),
                QStringLiteral( "annotation target %1 is not in version %2" )
                    .arg( item.targetSampleId, version.toString() ) );
    }

    PromotionReport report;
    for ( const auto &item : annotationsIn )
    {
        AnnotationRecord annotation = baseAnnotation( version, source );
        annotation.setTargetSampleId( item.targetSampleId );
        annotation.setClassCode( item.classCode );
        annotation.setGeometryWkt( item.geometryWkt );
        annotation.setConfidence( item.confidence );
        annotation.setReviewStatus( item.reviewStatus );
        auto written = m_store->addAnnotation( annotation );
        if ( !written )
            return failAs<PromotionReport>( written.diagnostics() );
        ++report.annotationsWritten;
    }
    return sicnu::data::Result<PromotionReport>::success( report );
}

sicnu::data::Result<QString> SamplePromoter::promotePair( const DatasetVersionId &version,
                                                          const PromotionSource &source,
                                                          const QString &primarySampleId,
                                                          const QString &secondarySampleId,
                                                          const QString &pairRole,
                                                          const QString &eventGroup ) const
{
    using TypedResult = sicnu::data::Result<QString>;
    if ( !m_store )
        return failAs<QString>( QStringLiteral( "dataset.store_closed" ),
                                QStringLiteral( "store closed" ) );
    if ( eventGroup.isEmpty() )
        return failAs<QString>( QStringLiteral( "dataset.promotion_pair_without_event" ),
                                QStringLiteral( "pairs require a shared event group (leakage"
                                                " audit key)" ) );
    if ( primarySampleId == secondarySampleId )
        return failAs<QString>( QStringLiteral( "dataset.promotion_invalid_pair" ),
                                QStringLiteral( "pair members must differ" ) );
    auto draft = requireDraftVersion( version );
    if ( !draft )
        return failAs<QString>( draft.diagnostics() );
    const auto primaryId = SampleId::fromString( primarySampleId );
    const auto secondaryId = SampleId::fromString( secondarySampleId );
    if ( !primaryId || !m_store->sampleById( version, primaryId.value() ) )
        return failAs<QString>( QStringLiteral( "dataset.sample_not_found" ),
                                QStringLiteral( "pair primary %1 is not in the version" )
                                    .arg( primarySampleId ) );
    if ( !secondaryId || !m_store->sampleById( version, secondaryId.value() ) )
        return failAs<QString>( QStringLiteral( "dataset.sample_not_found" ),
                                QStringLiteral( "pair secondary %1 is not in the version" )
                                    .arg( secondarySampleId ) );

    SampleRecord sample = baseSample( version, SampleKind::Pair, source );
    PairSample payload;
    payload.primaryRef = primarySampleId;
    payload.secondaryRef = secondarySampleId;
    payload.pairRole = pairRole;
    sample.payload() = payload;
    // The event group is the leakage key: pre/post members must share it.
    sample.setGroupId( eventGroup );
    QJsonObject provenance = sample.provenance();
    provenance.insert( QStringLiteral( "event_group" ), eventGroup );
    sample.provenance() = provenance;

    auto written = m_store->addSamples( QVector<SampleRecord>{ sample } );
    if ( !written )
        return failAs<QString>( written.diagnostics() );
    return TypedResult::success( sample.sampleId() );
}

sicnu::data::Result<QString> SamplePromoter::promoteTemporal(
    const DatasetVersionId &version, const PromotionSource &source,
    const QVector<TemporalMember> &members, const QDateTime &targetTimeUtc ) const
{
    using TypedResult = sicnu::data::Result<QString>;
    if ( !m_store )
        return failAs<QString>( QStringLiteral( "dataset.store_closed" ),
                                QStringLiteral( "store closed" ) );
    if ( members.isEmpty() )
        return failAs<QString>( QStringLiteral( "dataset.promotion_invalid_temporal" ),
                                QStringLiteral( "temporal sample with no observations" ) );
    auto draft = requireDraftVersion( version );
    if ( !draft )
        return failAs<QString>( draft.diagnostics() );

    TemporalSample payload;
    QString sharedGroup;
    for ( const auto &member : members )
    {
        TemporalObservation observation;
        observation.timeUtc = member.timeUtc;
        observation.assetId = member.assetId;
        observation.revision = member.revision;
        observation.missing = member.missing;
        observation.quality = member.quality;
        payload.observations.append( observation );

        if ( member.missing )
            continue;
        const auto memberId = SampleId::fromString( member.memberSampleId );
        if ( !memberId || !m_store->sampleById( version, memberId.value() ) )
            return failAs<QString>( QStringLiteral( "dataset.sample_not_found" ),
                                    QStringLiteral( "temporal member %1 is not in the version" )
                                        .arg( member.memberSampleId ) );
        if ( sharedGroup.isEmpty() )
        {
            const auto memberRecord = m_store->sampleById( version, memberId.value() );
            sharedGroup = memberRecord->groupId();
        }
    }
    payload.targetTimeUtc = targetTimeUtc;

    SampleRecord sample = baseSample( version, SampleKind::Temporal, source );
    sample.payload() = payload;
    sample.setGroupId( sharedGroup );
    auto written = m_store->addSamples( QVector<SampleRecord>{ sample } );
    if ( !written )
        return failAs<QString>( written.diagnostics() );
    return TypedResult::success( sample.sampleId() );
}

} // namespace sicnu::dataset
