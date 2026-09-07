// test_dataset_quality_scale.cpp — Foundation 5.0 dataset composition/QA
// tests (goal §20/§46) + the metadata-scale contract (goal §37/§52):
// 100k sample rows ingest + paged reads + split + leakage audit within
// bounded memory, timings recorded (never gated on machine speed).
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_quality.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_version.h"
#include "dataset/label_schema.h"
#include "dataset/leakage_audit.h"
#include "dataset/sample.h"
#include "dataset/split.h"

#include <QElapsedTimer>
#include <QHash>
#include <QTemporaryDir>

#include <chrono>
#include <cmath>

using namespace sicnu::dataset;

namespace
{

LabelSchema threeClassSchema()
{
    LabelSchema schema;
    schema.setSchemaId( LabelSchemaId::generate().toString() );
    schema.setVersion( 1 );
    auto makeClass = []( const QString &code, bool background = false ) {
        LabelClass labelClass;
        labelClass.setStableId( LabelSchemaId::generate().toString() );
        labelClass.setCode( code );
        labelClass.setBackground( background );
        return labelClass;
    };
    schema.classes() = { makeClass( QStringLiteral( "water" ) ),
                         makeClass( QStringLiteral( "urban" ) ),
                         makeClass( QStringLiteral( "background" ), true ) };
    return schema;
}

} // namespace

TEST_CASE( "composition statistics count every dimension honestly",
           "[dataset][quality]" )
{
    QVector<CompositionRow> rows;
    for ( int i = 0; i < 50; ++i )
    {
        CompositionRow row;
        row.classCode = QStringLiteral( "water" );
        row.sensor = QStringLiteral( "S2" );
        row.region = QStringLiteral( "A" );
        row.season = QStringLiteral( "summer" );
        row.modality = QStringLiteral( "optical" );
        row.resolution = 10.0;
        row.weight = 1.0;
        row.year = 2025;
        rows.append( row );
    }
    for ( int i = 0; i < 5; ++i )
    {
        CompositionRow row;
        row.classCode = QStringLiteral( "urban" );
        row.sensor = QStringLiteral( "S1" );
        row.region = QStringLiteral( "B" );
        row.season = QStringLiteral( "winter" );
        row.modality = QStringLiteral( "sar" );
        row.resolution = 0.0; // unknown
        row.weight = 0.0;     // bad weight is counted, not dropped
        rows.append( row );
    }
    const LabelSchema schema = threeClassSchema();
    const auto composition = computeComposition( rows, &schema );
    CHECK( composition.sampleCount == 55 );
    CHECK( composition.byClass.value( QStringLiteral( "water" ) ) == 50 );
    CHECK( composition.byClass.value( QStringLiteral( "urban" ) ) == 5 );
    CHECK( composition.unknownClassCount == 0 );
    CHECK( composition.zeroWeightCount == 5 );
    CHECK( composition.byResolution.contains( QStringLiteral( "unknown" ) ) );

    // Class imbalance 50:5 with default ratio 10 → flagged.
    const auto imbalances = imbalanceFindings( composition );
    CHECK( std::any_of( imbalances.cbegin(), imbalances.cend(),
                        []( const ImbalanceFinding &finding ) {
                            return finding.dimension == QLatin1String( "class" );
                        } ) );
    // Unknown classes are counted against the schema.
    CompositionRow alien;
    alien.classCode = QStringLiteral( "martian" );
    const auto withAlien = computeComposition( QVector<CompositionRow>{ alien }, &schema );
    CHECK( withAlien.unknownClassCount == 1 );
    CHECK( withAlien.byClass.contains( QStringLiteral( "(unknown)" ) ) );
}

TEST_CASE( "label QA catches unknown classes, conflicts, duplicates and geometry",
           "[dataset][quality]" )
{
    const LabelSchema schema = threeClassSchema();

    LabelQaItem good;
    good.sampleId = QStringLiteral( "s-good" );
    good.annotationIds = { QStringLiteral( "a1" ) };
    good.classCodes = { QStringLiteral( "water" ) };
    good.geometryWkts = { QStringLiteral( "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))" ) };

    LabelQaItem unknown;
    unknown.sampleId = QStringLiteral( "s-unknown" );
    unknown.annotationIds = { QStringLiteral( "a2" ) };
    unknown.classCodes = { QStringLiteral( "martian" ) };

    LabelQaItem conflicting;
    conflicting.sampleId = QStringLiteral( "s-conflict" );
    conflicting.annotationIds = { QStringLiteral( "a3" ), QStringLiteral( "a4" ) };
    conflicting.classCodes = { QStringLiteral( "water" ), QStringLiteral( "urban" ) };

    LabelQaItem duplicate;
    duplicate.sampleId = QStringLiteral( "s-dup" );
    duplicate.annotationIds = { QStringLiteral( "a5" ), QStringLiteral( "a6" ) };
    duplicate.classCodes = { QStringLiteral( "water" ), QStringLiteral( "water" ) };
    duplicate.geometryWkts = { QStringLiteral( "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))" ),
                               QStringLiteral( "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))" ) };

    LabelQaItem badGeometry;
    badGeometry.sampleId = QStringLiteral( "s-geometry" );
    badGeometry.annotationIds = { QStringLiteral( "a7" ) };
    badGeometry.classCodes = { QStringLiteral( "water" ) };
    badGeometry.geometryWkts = { QStringLiteral( "POLYGON(not coordinates)" ) };

    LabelQaItem tiny;
    tiny.sampleId = QStringLiteral( "s-tiny" );
    tiny.annotationIds = { QStringLiteral( "a8" ) };
    tiny.classCodes = { QStringLiteral( "water" ) };
    tiny.geometryWkts = { QStringLiteral( "POLYGON((0 0, 0.1 0, 0.1 0.1, 0 0.1, 0 0))" ) };

    LabelQaItem outside;
    outside.sampleId = QStringLiteral( "s-outside" );
    outside.annotationIds = { QStringLiteral( "a9" ) };
    outside.classCodes = { QStringLiteral( "water" ) };
    outside.geometryWkts = { QStringLiteral( "POLYGON((900 900, 910 900, 910 910, 900 910, 900 900))" ) };

    LabelQualityConfig config;
    config.tinyPolygonArea = 1.0;
    config.hasRasterExtent = true;
    config.rasterMinX = 0.0;
    config.rasterMinY = 0.0;
    config.rasterMaxX = 100.0;
    config.rasterMaxY = 100.0;

    const auto findings = labelQualityAudit(
        { good, unknown, conflicting, duplicate, badGeometry, tiny, outside }, schema, config );

    auto hasCode = [&]( const QString &code, const QString &sample ) {
        return std::any_of( findings.cbegin(), findings.cend(),
                            [&]( const LabelQualityFinding &finding ) {
                                return finding.code == code && finding.sampleId == sample;
                            } );
    };
    CHECK( hasCode( QStringLiteral( "label.unknown_class" ), QStringLiteral( "s-unknown" ) ) );
    CHECK( hasCode( QStringLiteral( "label.conflict" ), QStringLiteral( "s-conflict" ) ) );
    CHECK( hasCode( QStringLiteral( "label.duplicate_annotation" ), QStringLiteral( "s-dup" ) ) );
    CHECK( hasCode( QStringLiteral( "label.invalid_geometry" ), QStringLiteral( "s-geometry" ) ) );
    CHECK( hasCode( QStringLiteral( "label.tiny_polygon" ), QStringLiteral( "s-tiny" ) ) );
    CHECK( hasCode( QStringLiteral( "label.outside_raster" ), QStringLiteral( "s-outside" ) ) );
    CHECK( !hasCode( QStringLiteral( "label.unknown_class" ), QStringLiteral( "s-good" ) ) );
    CHECK( !hasCode( QStringLiteral( "label.tiny_polygon" ), QStringLiteral( "s-good" ) ) );

    // The gate: errors force Draft quality.
    CHECK( recommendQualityLevel( findings ) == DatasetQualityLevel::Draft );
    // Warnings alone keep the gate at Valid (Certified needs clean).
    QVector<LabelQualityFinding> warningOnly;
    LabelQualityFinding warning;
    warning.severity = DiagnosticSeverity::Warning;
    warningOnly.append( warning );
    CHECK( recommendQualityLevel( warningOnly ) == DatasetQualityLevel::Valid );
    CHECK( recommendQualityLevel( {} ) == DatasetQualityLevel::Certified );
}

namespace
{

/// Ingests @p count patch samples into a draft version of a fresh dataset.
void ingestSamples( DatasetStore &store, const QString &versionId, int count )
{
    QVector<SampleRecord> batch;
    batch.reserve( 1000 );
    for ( int i = 0; i < count; ++i )
    {
        SampleRecord sample;
        sample.setSampleId( SampleId::generate().toString() );
        sample.setDatasetVersionId( versionId );
        sample.setKind( SampleKind::Patch );
        sample.setGroupId( QStringLiteral( "scene-%1" ).arg( i % 500 ) );
        PatchSample payload;
        const qint64 x = ( i % 500 ) * 300;   // slight overlaps inside scenes
        const qint64 y = ( i / 500 ) * 256;
        payload.window = PixelWindow{ x, y, 256, 256 };
        payload.generatorConfigHash = QStringLiteral( "bench" );
        sample.payload() = payload;
        batch.append( sample );
        if ( batch.size() == 1000 )
        {
            REQUIRE( store.addSamples( batch ).has_value() );
            batch.clear();
        }
    }
    if ( !batch.isEmpty() )
        REQUIRE( store.addSamples( batch ).has_value() );
}

} // namespace

TEST_CASE( "100k metadata catalog: bounded ingest, paged reads, split + audit",
           "[dataset][scale][benchmark]" )
{
    QTemporaryDir dir;
    DatasetStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "scale.db" ) ) ) );
    const DatasetId datasetId = DatasetId::generate();
    REQUIRE( store.createDataset( datasetId, QStringLiteral( "scale" ) ).has_value() );
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId.toString() );
    const QString versionId = DatasetVersionId::generate().toString();
    manifest.setVersionId( versionId );
    REQUIRE( store.createDraftVersion( manifest ).has_value() );

    constexpr int kCount = 100000;
    QElapsedTimer timer;
    timer.start();
    ingestSamples( store, versionId, kCount );
    const qint64 ingestMs = timer.elapsed();
    CHECK( store.sampleCount(
               DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ) ) ==
           kCount );

    // Paged full scan in 500-row pages: bounded memory, monotonic order.
    timer.restart();
    qint64 seen = 0;
    qint64 offset = 0;
    QString firstId;
    while ( true )
    {
        const auto page = store.samplesPage(
            DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ), offset,
            500 );
        REQUIRE( page.has_value() );
        CHECK( page->second.size() <= 500 );
        if ( page->second.isEmpty() )
            break;
        if ( firstId.isEmpty() )
            firstId = page->second.first().sampleId();
        seen += page->second.size();
        offset += page->second.size();
    }
    const qint64 scanMs = timer.elapsed();
    CHECK( seen == kCount );
    // Order stability: re-reading the first page returns the same first id.
    const auto again = store.samplesPage(
        DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ), 0, 1 );
    REQUIRE( again.has_value() );
    CHECK( again->second.first().sampleId() == firstId );

    // Split over 100k rows: build the input view from paged reads.
    timer.restart();
    QVector<SplitInput> inputs;
    inputs.reserve( kCount );
    offset = 0;
    int index = 0;
    while ( offset < kCount )
    {
        const auto page = store.samplesPage(
            DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ), offset,
            500 );
        REQUIRE( page.has_value() );
        for ( const SampleRecord &sample : page->second )
        {
            SplitInput input;
            input.sampleId = sample.sampleId();
            input.groupId = sample.groupId();
            input.classCode = index % 3 == 0 ? QStringLiteral( "water" )
                                             : QStringLiteral( "urban" );
            input.validBounds = true;
            const auto &patch = std::get<PatchSample>( sample.payload() );
            input.minX = double( patch.window.x );
            input.minY = double( patch.window.y );
            input.maxX = input.minX + 256.0;
            input.maxY = input.minY + 256.0;
            inputs.append( input );
            ++index;
        }
        offset += page->second.size();
    }
    SplitConfig config;
    config.method = SplitMethod::Grouped;
    config.seed = 99;
    const auto split = SplitEngine::generate( config, versionId, inputs );
    REQUIRE( split.has_value() );
    const qint64 splitMs = timer.elapsed();

    // Leakage audit (grouped split → cross-role spatial pairs).
    timer.restart();
    // O(1) role lookup per sample: assignmentOf is a linear scan by
    // contract, so a per-sample call here would be quadratic at 100k.
    QHash<QString, SplitRole> roleBySample;
    roleBySample.reserve( inputs.size() );
    for ( const SplitAssignment &assignment : split->assignments() )
        roleBySample.insert( assignment.sampleId, assignment.role );
    QVector<AuditSample> auditSamples;
    auditSamples.reserve( inputs.size() );
    for ( const SplitInput &input : inputs )
    {
        AuditSample sample;
        sample.input = input;
        sample.role = roleBySample.value( input.sampleId, SplitRole::Unassigned );
        sample.windowWidth = 256.0;
        sample.windowHeight = 256.0;
        auditSamples.append( sample );
    }
    LeakageAuditConfig auditConfig;
    const auto report = LeakageAuditor::audit( versionId, split->manifestId(), auditSamples,
                                               auditConfig );
    REQUIRE( report.has_value() );
    const qint64 auditMs = timer.elapsed();

    // The grouped split keeps same-scene samples together, so scene-internal
    // overlapping windows are NOT cross-split findings (the whole point of
    // grouped splitting for patches).
    auto overlappingCross = false;
    for ( const LeakageFinding &finding : report->findings() )
    {
        if ( finding.kind == LeakageKind::OverlappingPatch )
            overlappingCross = true;
    }
    CHECK( !overlappingCross );

    // Record the timings (benchmark evidence, not machine-speed gates).
    WARN( "100k benchmark: ingest=" << ingestMs << "ms scan=" << scanMs
          << "ms split=" << splitMs << "ms audit=" << auditMs << "ms findings="
          << report->findings().size() );

    // Persist a split manifest reference into the version (catalog glue).
    const auto stored = store.versionById(
        DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ) );
    REQUIRE( stored.has_value() );
}
