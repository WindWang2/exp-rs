// test_sample_label_annotation.cpp — Foundation 5.0 sample model, label
// ontology and annotation chain tests (ADR 0135): payload round-trips, kind/
// payload coherence, half-open window + footprint math, schema hierarchy
// validation, explicit mappings, annotation provenance rules (pseudo labels
// must cite their model), revision chains, and draft-only mutation in the
// store.
#include <catch2/catch_test_macros.hpp>

#include "dataset/annotation.h"
#include "dataset/dataset_store.h"
#include "dataset/label_schema.h"
#include "dataset/sample.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <limits>

using namespace sicnu::dataset;

namespace
{

void makeStoreWithDraftVersion( DatasetStore &store, const QTemporaryDir &dir,
                                QString *versionIdOut )
{
    REQUIRE( store.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
    const DatasetId datasetId = DatasetId::generate();
    REQUIRE( store.createDataset( datasetId, QStringLiteral( "samples" ) ).has_value() );
    const DatasetVersionId versionId = DatasetVersionId::generate();
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId.toString() );
    manifest.setVersionId( versionId.toString() );
    REQUIRE( store.createDraftVersion( manifest ).has_value() );
    if ( versionIdOut )
        *versionIdOut = versionId.toString();
}

SampleRecord makePatchSample( const QString &versionId )
{
    SampleRecord sample;
    sample.setSampleId( SampleId::generate().toString() );
    sample.setDatasetVersionId( versionId );
    sample.setKind( SampleKind::Patch );
    sample.setGroupId( QStringLiteral( "scene-1" ) );
    PatchSample payload;
    payload.window = PixelWindow{ 100, 200, 256, 256 };
    payload.borderPolicy = BorderPolicy::Pad;
    payload.noDataMode = NoDataMode::MinValidFraction;
    payload.noDataThreshold = 0.8;
    payload.generatorConfigHash = QStringLiteral( "abc123" );
    sample.payload() = payload;
    return sample;
}

} // namespace

TEST_CASE( "half-open window algebra", "[dataset][sample]" )
{
    const PixelWindow window{ 10, 20, 32, 32 };
    CHECK( window.contains( 10, 20 ) );
    CHECK( window.contains( 41, 51 ) );
    CHECK( !window.contains( 42, 20 ) ); // exclusive right edge
    CHECK( !window.contains( 10, 52 ) ); // exclusive bottom edge

    const PixelWindow overlapping{ 30, 0, 32, 32 };
    CHECK( window.intersects( overlapping ) );
    CHECK( window.overlapFraction( overlapping ) > 0.0 );
    CHECK( window.overlapFraction( overlapping ) < 1.0 );
    const PixelWindow disjoint{ 1000, 1000, 5, 5 };
    CHECK( !window.intersects( disjoint ) );
    CHECK( window.overlapFraction( disjoint ) == 0.0 );
}

TEST_CASE( "ground footprint derives from north-up geotransform and rejects rotated",
           "[dataset][sample][spatial]" )
{
    GeoTransform northUp;
    northUp.values = { 500000.0, 10.0, 0.0, 4000000.0, 0.0, -10.0 };
    const PixelWindow window{ 0, 0, 100, 50 };
    const auto footprint = groundFootprintForWindow( window, northUp );
    REQUIRE( footprint.has_value() );
    // Half-open contract: right/bottom edges map the exclusive edge.
    CHECK( footprint.value().contains( QLatin1String( "500000" ) ) );
    CHECK( footprint.value().startsWith( QLatin1String( "POLYGON((", Qt::CaseInsensitive ) ) );

    GeoTransform rotated;
    rotated.values = { 500000.0, 10.0, 0.5, 4000000.0, 0.5, -10.0 };
    const auto refused = groundFootprintForWindow( window, rotated );
    CHECK( !refused.has_value() );
    CHECK( refused.diagnostics().first().code == QStringLiteral( "dataset.transform_rotated" ) );
}

TEST_CASE( "grid windows enumerate deterministically", "[dataset][sample]" )
{
    const auto windows = gridWindows( 100, 100, 32, 32, 32, 32 );
    CHECK( windows.size() == 16 ); // 4x4, overhang rows/cols included
    CHECK( windows.first() == PixelWindow{ 0, 0, 32, 32 } );
    CHECK( windows.at( 1 ) == PixelWindow{ 32, 0, 32, 32 } );
    CHECK( windows.last().x + windows.last().width > 100 ); // overhang preserved
    CHECK( gridWindows( 0, 0, 32, 32, 32, 32 ).isEmpty() );
    CHECK( gridWindows( 100, 100, 0, 32, 32, 32 ).isEmpty() );
}

TEST_CASE( "sample payloads round-trip and validate", "[dataset][sample]" )
{
    SampleRecord sample = makePatchSample( QStringLiteral( "version-1" ) );
    const auto validated = validateSample( sample );
    REQUIRE( validated.has_value() );

    const QJsonObject json = sample.toJson();
    const auto parsed = SampleRecord::fromJson( json );
    REQUIRE( parsed.has_value() );
    CHECK( parsed.value() == sample );

    // Kind/payload mismatch is invalid.
    SampleRecord corrupted = sample;
    corrupted.setKind( SampleKind::Pair );
    CHECK( !validateSample( corrupted ).has_value() );

    // Non-positive weight is invalid.
    SampleRecord zeroWeight = sample;
    zeroWeight.setWeight( 0.0 );
    CHECK( !validateSample( zeroWeight ).has_value() );

    // A patch without a valid window is invalid.
    SampleRecord badWindow = sample;
    auto badPayload = std::get<PatchSample>( badWindow.payload() );
    badPayload.window = PixelWindow{ 0, 0, 0, 10 };
    badWindow.payload() = badPayload;
    CHECK( !validateSample( badWindow ).has_value() );
}

TEST_CASE( "temporal and multimodal samples keep missing observations explicit",
           "[dataset][sample]" )
{
    SampleRecord temporal;
    temporal.setSampleId( SampleId::generate().toString() );
    temporal.setDatasetVersionId( QStringLiteral( "version-1" ) );
    temporal.setKind( SampleKind::Temporal );
    TemporalSample payload;
    TemporalObservation first;
    first.timeUtc = QDateTime::fromString( QStringLiteral( "2026-01-01T00:00:00.000Z" ),
                                           Qt::ISODateWithMs );
    first.assetId = QStringLiteral( "asset-1" );
    first.quality = 0.9;
    TemporalObservation gap;
    gap.timeUtc = QDateTime::fromString( QStringLiteral( "2026-01-11T00:00:00.000Z" ),
                                         Qt::ISODateWithMs );
    gap.missing = true; // known hole — recorded, never dropped
    payload.observations = { first, gap };
    payload.targetTimeUtc =
        QDateTime::fromString( QStringLiteral( "2026-02-01T00:00:00.000Z" ), Qt::ISODateWithMs );
    temporal.payload() = payload;

    const auto parsed = SampleRecord::fromJson( temporal.toJson() );
    REQUIRE( parsed.has_value() );
    const auto &roundTripped = std::get<TemporalSample>( parsed.value().payload() );
    REQUIRE( roundTripped.observations.size() == 2 );
    CHECK( roundTripped.observations.at( 1 ).missing );
    CHECK( roundTripped.observations.at( 1 ).assetId.isEmpty() );

    // Empty observation lists are invalid.
    SampleRecord empty = temporal;
    auto emptyPayload = std::get<TemporalSample>( empty.payload() );
    emptyPayload.observations.clear();
    empty.payload() = emptyPayload;
    CHECK( !validateSample( empty ).has_value() );

    // Multi-modal members carry modality + missing policy.
    SampleRecord multi;
    multi.setSampleId( SampleId::generate().toString() );
    multi.setDatasetVersionId( QStringLiteral( "version-1" ) );
    multi.setKind( SampleKind::MultiModal );
    MultiModalSample multiPayload;
    MultiModalMember optical{ QStringLiteral( "optical" ),
                              SampleId::generate().toString(), true, QStringLiteral( "error" ) };
    MultiModalMember sar{ QStringLiteral( "sar" ),
                          SampleId::generate().toString(), false, QStringLiteral( "mask" ) };
    multiPayload.members = { optical, sar };
    multi.payload() = multiPayload;
    const auto multiParsed = SampleRecord::fromJson( multi.toJson() );
    REQUIRE( multiParsed.has_value() );
    CHECK( std::get<MultiModalSample>( multiParsed.value().payload() ).members.size() == 2 );
}

TEST_CASE( "label schema validates hierarchy, unique ids and cycles",
           "[dataset][label]" )
{
    LabelSchema schema;
    schema.setSchemaId( LabelSchemaId::generate().toString() );
    schema.setVersion( 1 );
    schema.setName( QStringLiteral( "Land Cover" ) );

    auto makeClass = []( const QString &code, const QString &parent = QString() ) {
        LabelClass labelClass;
        labelClass.setStableId( LabelSchemaId::generate().toString() );
        labelClass.setCode( code );
        labelClass.setParentCode( parent );
        return labelClass;
    };
    schema.classes() = { makeClass( QStringLiteral( "vegetation" ) ),
                         makeClass( QStringLiteral( "cropland" ), QStringLiteral( "vegetation" ) ),
                         makeClass( QStringLiteral( "forest" ), QStringLiteral( "vegetation" ) ),
                         makeClass( QStringLiteral( "water" ) ) };
    LabelClass background = makeClass( QStringLiteral( "background" ) );
    background.setBackground( true );
    background.setIgnore( true );
    schema.classes().append( background );

    REQUIRE( schema.validate().has_value() );
    CHECK( schema.leafCodes().size() == 4 ); // cropland, forest, water, background
    CHECK( schema.ancestorsOf( QStringLiteral( "cropland" ) ) ==
           QStringList{ QStringLiteral( "vegetation" ) } );
    CHECK( schema.descendantsOf( QStringLiteral( "vegetation" ) ).size() == 2 );

    // Duplicate code rejected.
    LabelSchema duplicated = schema;
    duplicated.classes().append( makeClass( QStringLiteral( "water" ) ) );
    CHECK( !duplicated.validate().has_value() );

    // Unknown parent rejected.
    LabelSchema orphan = schema;
    orphan.classes().first().setParentCode( QStringLiteral( "ghost" ) );
    CHECK( !orphan.validate().has_value() );

    // Cycle rejected.
    LabelSchema cyclic = schema;
    cyclic.classes().first().setParentCode( QStringLiteral( "cropland" ) );
    CHECK( !cyclic.validate().has_value() );

    // Stable id survives round-trip; code is the interchange key.
    const auto parsed = LabelSchema::fromJson( schema.toJson() );
    REQUIRE( parsed.has_value() );
    CHECK( parsed.value() == schema );
    REQUIRE( parsed->classByCode( QStringLiteral( "forest" ) ) != nullptr );
    CHECK( !parsed->classByCode( QStringLiteral( "forest" ) )->stableId().isEmpty() );
}

TEST_CASE( "label mappings are explicit, versioned and totality-checked",
           "[dataset][label]" )
{
    LabelSchema from;
    from.setSchemaId( LabelSchemaId::generate().toString() );
    from.setVersion( 2 );
    LabelClass cropland;
    cropland.setStableId( LabelSchemaId::generate().toString() );
    cropland.setCode( QStringLiteral( "cropland" ) );
    cropland.setLegacyIntId( 3 ); // interop alias only
    LabelClass water;
    water.setStableId( LabelSchemaId::generate().toString() );
    water.setCode( QStringLiteral( "water" ) );
    from.classes() = { cropland, water };

    LabelMapping mapping;
    mapping.setName( QStringLiteral( "lc-v2-to-simple" ) );
    mapping.setFromSchemaId( from.schemaId() );
    mapping.setFromSchemaVersion( 2 );
    mapping.setToSchemaId( LabelSchemaId::generate().toString() );
    mapping.setToSchemaVersion( 1 );
    mapping.rules().append( { QStringLiteral( "cropland" ), QStringLiteral( "farmland" ) } );
    mapping.rules().append( { QStringLiteral( "water" ), QStringLiteral( "water" ) } );

    const auto unmapped = mapping.covers( from );
    REQUIRE( unmapped.has_value() );
    CHECK( unmapped.value().isEmpty() );

    CHECK( mapping.map( QStringLiteral( "cropland" ) ).value_or( QString() ) ==
           QStringLiteral( "farmland" ) );
    CHECK( !mapping.map( QStringLiteral( "ghost" ) ).has_value() );

    // A schema mismatch is refused, not applied silently.
    LabelSchema newer = from;
    newer.setVersion( 3 );
    const auto mismatch = mapping.covers( newer );
    CHECK( !mismatch.has_value() );
    CHECK( mismatch.diagnostics().first().code == QStringLiteral( "dataset.label_mapping_mismatch" ) );

    // Round-trip.
    const auto parsed = LabelMapping::fromJson( mapping.toJson() );
    REQUIRE( parsed.has_value() );
    CHECK( parsed.value() == mapping );
}

TEST_CASE( "pseudo annotations must cite their model", "[dataset][annotation]" )
{
    AnnotationRecord annotation;
    annotation.setAnnotationId( AnnotationId::generate().toString() );
    annotation.setTargetSampleId( SampleId::generate().toString() );
    annotation.setDatasetVersionId( QStringLiteral( "version-1" ) );
    annotation.setRevision( 1 );
    annotation.setSourceType( AnnotationSourceType::Pseudo );
    annotation.setClassCode( QStringLiteral( "water" ) );

    // Missing model identity is invalid (goal §44).
    CHECK( !validateAnnotation( annotation ).has_value() );

    QJsonObject detail;
    detail.insert( QStringLiteral( "model_id" ), QStringLiteral( "landcover@v3" ) );
    detail.insert( QStringLiteral( "model_digest" ), QStringLiteral( "deadbeef" ) );
    detail.insert( QStringLiteral( "threshold" ), 0.75 );
    annotation.sourceDetail() = detail;
    REQUIRE( validateAnnotation( annotation ).has_value() );

    const auto parsed = AnnotationRecord::fromJson( annotation.toJson() );
    REQUIRE( parsed.has_value() );
    CHECK( parsed.value() == annotation );

    // Revision numbering must stay coherent.
    AnnotationRecord badRevision = annotation;
    badRevision.setRevision( 2 );
    badRevision.setParentRevision( 3 );
    CHECK( !validateAnnotation( badRevision ).has_value() );
}

TEST_CASE( "annotation chains append-only, store-enforced", "[dataset][annotation][store]" )
{
    QTemporaryDir dir;
    QString versionId;
    DatasetStore store;
    makeStoreWithDraftVersion( store, dir, &versionId );

    // Chain codes live under a real schema: unknown codes are refused at
    // ingest (schema-aware fail-fast), so the chain fixture references one.
    LabelSchema chainSchema;
    chainSchema.setSchemaId( LabelSchemaId::generate().toString() );
    chainSchema.setVersion( 1 );
    auto chainClass = []( const QString &code ) {
        LabelClass labelClass;
        labelClass.setStableId( LabelSchemaId::generate().toString() );
        labelClass.setCode( code );
        return labelClass;
    };
    chainSchema.classes() = { chainClass( QStringLiteral( "cropland" ) ),
                              chainClass( QStringLiteral( "forest" ) ) };
    REQUIRE( store.saveLabelSchema( chainSchema ).has_value() );

    const QString annotationId = AnnotationId::generate().toString();
    AnnotationRecord v1;
    v1.setAnnotationId( annotationId );
    v1.setTargetSampleId( SampleId::generate().toString() );
    v1.setDatasetVersionId( versionId );
    v1.setRevision( 1 );
    v1.setSourceType( AnnotationSourceType::Human );
    v1.setLabelSchemaId( chainSchema.schemaId() );
    v1.setLabelSchemaVersion( 1 );
    v1.setClassCode( QStringLiteral( "cropland" ) );
    v1.setAuthorRole( QStringLiteral( "annotator" ) );
    REQUIRE( store.addAnnotation( v1 ).has_value() );

    // Revision 2 must continue revision 1: same ids, parent=1, revision=2.
    AnnotationRecord v2 = v1;
    v2.setParentRevision( 1 );
    v2.setRevision( 2 );
    v2.setSourceType( AnnotationSourceType::ManualInterpretation );
    v2.setClassCode( QStringLiteral( "forest" ) );
    v2.setReason( QStringLiteral( "rechecked imagery" ) );
    REQUIRE( store.addAnnotation( v2 ).has_value() );

    // A forged revision 2 (wrong parent) is refused.
    AnnotationRecord forged = v2;
    forged.setParentRevision( 5 );
    forged.setRevision( 6 );
    const auto refused = store.addAnnotation( forged );
    CHECK( !refused.has_value() );
    CHECK( refused.diagnostics().first().code ==
           QStringLiteral( "dataset.annotation_chain_broken" ) );

    // History is complete and ordered; tip is revision 2.
    const auto history = store.annotationHistory( annotationId );
    REQUIRE( history.size() == 2 );
    CHECK( history.first().revision() == 1 );
    CHECK( history.last().classCode() == QStringLiteral( "forest" ) );
    const auto tip = store.annotationTip( annotationId );
    REQUIRE( tip.has_value() );
    CHECK( tip->revision() == 2 );
    CHECK( store.annotationsOfSample( v1.targetSampleId() ).size() == 1 );
}

TEST_CASE( "samples mutate only in draft versions", "[dataset][sample][store]" )
{
    QTemporaryDir dir;
    QString versionId;
    DatasetStore store;
    makeStoreWithDraftVersion( store, dir, &versionId );

    QVector<SampleRecord> batch;
    for ( int i = 0; i < 5; ++i )
    {
        SampleRecord sample = makePatchSample( versionId );
        sample.setSampleId( SampleId::generate().toString() );
        sample.setGroupId( QStringLiteral( "scene-%1" ).arg( i % 2 ) );
        batch.append( sample );
    }
    REQUIRE( store.addSamples( batch ).has_value() );
    CHECK( store.sampleCount( DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ) ) == 5 );

    // Duplicate sample id fails the batch (never silently overwrites).
    const auto duplicate = store.addSamples( QVector<SampleRecord>{ batch.first() } );
    CHECK( !duplicate.has_value() );

    // Paged read in insertion order.
    const auto page = store.samplesPage(
        DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ), 0, 3 );
    REQUIRE( page.has_value() );
    CHECK( page.value().first == 5 );
    REQUIRE( page.value().second.size() == 3 );
    CHECK( page.value().second.first().sampleId() == batch.first().sampleId() );

    const auto groups = store.sampleGroupIds(
        DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ) );
    CHECK( groups.size() == 2 );

    // Draft removal works.
    REQUIRE( store
                 .removeSample( DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ),
                                SampleId::fromString( batch.last().sampleId() )
                                    .value_or( SampleId{} ) )
                 .has_value() );
    CHECK( store.sampleCount(
               DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ) ) == 4 );

    // Commit freezes: further sample mutation is refused.
    REQUIRE( store.stageVersion(
                 DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ) )
                 .has_value() );
    REQUIRE( store.commitVersion(
                 DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ) )
                 .has_value() );
    SampleRecord frozen = makePatchSample( versionId );
    frozen.setSampleId( SampleId::generate().toString() );
    const auto refused = store.addSamples( QVector<SampleRecord>{ frozen } );
    CHECK( !refused.has_value() );
    CHECK( refused.diagnostics().first().code == QStringLiteral( "dataset.not_draft" ) );
}

TEST_CASE( "label schemas are immutable per (id, version)", "[dataset][label][store]" )
{
    QTemporaryDir dir;
    QString versionId;
    DatasetStore store;
    makeStoreWithDraftVersion( store, dir, &versionId );

    LabelSchema schema;
    schema.setSchemaId( LabelSchemaId::generate().toString() );
    schema.setVersion( 1 );
    LabelClass labelClass;
    labelClass.setStableId( LabelSchemaId::generate().toString() );
    labelClass.setCode( QStringLiteral( "water" ) );
    schema.classes().append( labelClass );
    REQUIRE( store.saveLabelSchema( schema ).has_value() );

    // Idempotent identical re-save.
    CHECK( store.saveLabelSchema( schema ).has_value() );

    // Differing content at the same (id, version) is a conflict.
    LabelSchema mutated = schema;
    mutated.classes().first().setDisplayName( QStringLiteral( "Water" ) );
    const auto conflict = store.saveLabelSchema( mutated );
    CHECK( !conflict.has_value() );
    CHECK( conflict.diagnostics().first().code == QStringLiteral( "dataset.conflict" ) );

    // A new version is a new document row.
    LabelSchema v2 = mutated;
    v2.setVersion( 2 );
    REQUIRE( store.saveLabelSchema( v2 ).has_value() );
    CHECK( store.labelSchemaVersions( schema.schemaId() ).size() == 2 );

    const auto loaded = store.labelSchema( schema.schemaId(), 1 );
    REQUIRE( loaded.has_value() );
    CHECK( loaded.value() == schema );
}

// --- Track 13 R4 WP-D: ontology ingest is fail-fast -------------------------

namespace
{

SampleRecord makeSample( const QString &versionId, SampleKind kind )
{
    SampleRecord sample;
    sample.setSampleId( SampleId::generate().toString() );
    sample.setDatasetVersionId( versionId );
    sample.setKind( kind );
    return sample;
}

} // namespace

TEST_CASE( "ingest rejects provably illegal sample payloads (fail-fast)",
           "[dataset][sample][validation][r4]" )
{
    QTemporaryDir dir;
    DatasetStore store;
    QString versionId;
    makeStoreWithDraftVersion( store, dir, &versionId );

    // Point coordinates must be finite (weight already is — same discipline).
    SampleRecord nanPoint = makeSample( versionId, SampleKind::Point );
    PointSample nanPayload;
    nanPayload.x = std::numeric_limits<double>::quiet_NaN();
    nanPayload.y = 1.0;
    nanPoint.payload() = nanPayload;
    {
        const auto rejected = store.addSamples( { nanPoint } );
        REQUIRE( !rejected.has_value() );
        CHECK( rejected.diagnostics().first().message.contains(
            QStringLiteral( "point" ) ) );
    }

    SampleRecord infPoint = makeSample( versionId, SampleKind::Point );
    PointSample infPayload;
    infPayload.x = std::numeric_limits<double>::infinity();
    infPayload.y = 1.0;
    infPoint.payload() = infPayload;
    CHECK( !store.addSamples( { infPoint } ).has_value() );

    // Pixel indices are unsigned-domain: negatives are illegal, not "far left".
    SampleRecord negativePixel = makeSample( versionId, SampleKind::Pixel );
    PixelSample negativePayload;
    negativePayload.column = -1;
    negativePayload.row = 5;
    negativePixel.payload() = negativePayload;
    {
        const auto rejected = store.addSamples( { negativePixel } );
        REQUIRE( !rejected.has_value() );
        CHECK( rejected.diagnostics().first().message.contains(
            QStringLiteral( "column" ) ) );
    }

    // Pair samples reference two DISTINCT members by id.
    SampleRecord emptyPair = makeSample( versionId, SampleKind::Pair );
    PairSample emptyPairPayload;
    emptyPairPayload.primaryRef.clear();
    emptyPairPayload.secondaryRef = QStringLiteral( "post" );
    emptyPair.payload() = emptyPairPayload;
    CHECK( !store.addSamples( { emptyPair } ).has_value() );

    SampleRecord selfPair = makeSample( versionId, SampleKind::Pair );
    PairSample selfPairPayload;
    selfPairPayload.primaryRef = QStringLiteral( "same" );
    selfPairPayload.secondaryRef = QStringLiteral( "same" );
    selfPair.payload() = selfPairPayload;
    CHECK( !store.addSamples( { selfPair } ).has_value() );

    // Object samples must name their segment.
    SampleRecord anonymousObject = makeSample( versionId, SampleKind::Object );
    ObjectSample anonymousPayload;
    anonymousPayload.assetId = QStringLiteral( "asset-1" );
    anonymousPayload.objectRef.clear();
    anonymousObject.payload() = anonymousPayload;
    CHECK( !store.addSamples( { anonymousObject } ).has_value() );

    // Polygon geometry must parse (reuses the module's own WKT reader).
    SampleRecord brokenPolygon = makeSample( versionId, SampleKind::Polygon );
    PolygonSample brokenPayload;
    brokenPayload.wkt = QStringLiteral( "POLYGON((not a ring" );
    brokenPolygon.payload() = brokenPayload;
    CHECK( !store.addSamples( { brokenPolygon } ).has_value() );

    // Nothing above reached the store: the version stays empty.
    const auto versionKey = DatasetVersionId::fromString( versionId );
    REQUIRE( versionKey.has_value() );
    CHECK( store.sampleCount( versionKey.value() ) == 0 );
}

TEST_CASE( "validation errors locate the record and the field",
           "[dataset][sample][validation][r4]" )
{
    SampleRecord sample;
    sample.setSampleId( SampleId::generate().toString() );
    sample.setDatasetVersionId( QStringLiteral( "version-x" ) );
    sample.setKind( SampleKind::Point );
    PointSample payload;
    payload.x = std::numeric_limits<double>::quiet_NaN();
    payload.y = 1.0;
    sample.payload() = payload;

    const auto rejected = validateSample( sample );
    REQUIRE( !rejected.has_value() );
    const QString message = rejected.diagnostics().first().message;
    // Machine-parseable locator: the record id and the offending field.
    CHECK( message.contains( sample.sampleId() ) );
    CHECK( message.contains( QStringLiteral( "x" ) ) );

    SampleRecord anonymous;
    anonymous.setKind( SampleKind::Point );
    anonymous.payload() = PointSample{};
    const auto idless = validateSample( anonymous );
    REQUIRE( !idless.has_value() );
    CHECK( idless.diagnostics().first().message.contains(
        QStringLiteral( "id" ) ) );
}

TEST_CASE( "annotations with unknown class codes are refused against their schema",
           "[dataset][annotation][validation][r4]" )
{
    QTemporaryDir dir;
    DatasetStore store;
    QString versionId;
    makeStoreWithDraftVersion( store, dir, &versionId );

    LabelSchema schema;
    schema.setSchemaId( LabelSchemaId::generate().toString() );
    schema.setVersion( 1 );
    LabelClass landCover;
    landCover.setStableId( LabelSchemaId::generate().toString() );
    landCover.setCode( QStringLiteral( "landcover" ) );
    schema.classes() = { landCover };
    REQUIRE( store.saveLabelSchema( schema ).has_value() );

    auto makeAnnotation = [&]( const QString &code ) {
        AnnotationRecord annotation;
        annotation.setAnnotationId( AnnotationId::generate().toString() );
        annotation.setTargetSampleId( SampleId::generate().toString() );
        annotation.setDatasetVersionId( versionId );
        annotation.setRevision( 1 );
        annotation.setLabelSchemaId( schema.schemaId() );
        annotation.setLabelSchemaVersion( 1 );
        annotation.setClassCode( code );
        annotation.setConfidence( 1.0 );
        return annotation;
    };

    // A known code is accepted.
    CHECK( store.addAnnotation( makeAnnotation( QStringLiteral( "landcover" ) ) ).has_value() );

    // An unknown code is refused and the message names the code.
    const auto ghost = store.addAnnotation( makeAnnotation( QStringLiteral( "ghost-code" ) ) );
    REQUIRE( !ghost.has_value() );
    CHECK( ghost.diagnostics().first().message.contains(
        QStringLiteral( "ghost-code" ) ) );

    // A missing schema version is refused too, not silently label-less.
    AnnotationRecord futureSchema = makeAnnotation( QStringLiteral( "landcover" ) );
    futureSchema.setLabelSchemaVersion( 99 );
    CHECK( !store.addAnnotation( futureSchema ).has_value() );

    // Pure geometry annotations (empty class code) stay legal.
    AnnotationRecord geometryOnly = makeAnnotation( QString() );
    geometryOnly.setLabelSchemaId( QString() );
    geometryOnly.setLabelSchemaVersion( 0 );
    CHECK( store.addAnnotation( geometryOnly ).has_value() );
}
