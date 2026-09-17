// test_d19_dataset_foundry.cpp — D19 Dataset Foundry unit tests:
// DatasetRole vocabulary/manifest, FeatureSet identity join, sample catalog
// paging, QA report verdicts, FoundryService inspect.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_manifest.h"
#include "dataset/dataset_qa_report.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "dataset/feature_table.h"
#include "dataset/foundry_service.h"
#include "dataset/leakage_audit.h"
#include "dataset/sample_catalog.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace sicnu::dataset;

namespace
{

DatasetManifest makeManifest( const QString &datasetId, const QString &versionId )
{
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId );
    manifest.setVersionId( versionId );
    manifest.setName( QStringLiteral( "D19 Foundry Fixture" ) );
    manifest.setRole( DatasetRole::Training );
    manifest.setCreatedAtUtc( QDateTime::fromString(
        QStringLiteral( "2026-09-15T00:00:00.000Z" ), Qt::ISODateWithMs ) );
    SourceAssetRef source;
    source.assetId = QStringLiteral( "0a0a0a0a-1111-4222-8333-444444444444" );
    source.revision = 1;
    source.role = QStringLiteral( "image" );
    manifest.sourceAssets().append( source );
    DatasetEntry entry;
    entry.kind = QStringLiteral( "asset" );
    entry.refId = source.assetId;
    entry.role = QStringLiteral( "image" );
    manifest.entries().append( entry );
    return manifest;
}

} // namespace

TEST_CASE( "D19 dataset role vocabulary and manifest round-trip", "[d19][foundry][role]" )
{
    CHECK( datasetRoleFromString( datasetRoleToString( DatasetRole::Benchmark ) ) ==
           DatasetRole::Benchmark );
    CHECK( datasetRoleFromString( QStringLiteral( "not-a-role" ) ) == std::nullopt );
    CHECK( auditVerdictFromString( auditVerdictToString( AuditVerdict::Fail ) ) ==
           AuditVerdict::Fail );

    DatasetManifest manifest = makeManifest( DatasetId::generate().toString(),
                                             DatasetVersionId::generate().toString() );
    const QJsonObject json = manifest.toJson();
    CHECK( json.value( QStringLiteral( "role" ) ).toString() == QStringLiteral( "training" ) );
    const auto parsed = DatasetManifest::fromJson( json );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->role() == DatasetRole::Training );

    // Unspecified role omitted → fingerprint-stable for pre-D19 manifests.
    DatasetManifest bare;
    bare.setDatasetId( DatasetId::generate().toString() );
    bare.setVersionId( DatasetVersionId::generate().toString() );
    bare.setName( QStringLiteral( "bare" ) );
    bare.setCreatedAtUtc( QDateTime::fromString(
        QStringLiteral( "2026-09-15T00:00:00.000Z" ), Qt::ISODateWithMs ) );
    SourceAssetRef source;
    source.assetId = QStringLiteral( "0a0a0a0a-1111-4222-8333-444444444444" );
    source.revision = 1;
    bare.sourceAssets().append( source );
    DatasetEntry entry;
    entry.kind = QStringLiteral( "asset" );
    entry.refId = source.assetId;
    bare.entries().append( entry );
    const QJsonObject bareJson = bare.toJson();
    CHECK( !bareJson.contains( QStringLiteral( "role" ) ) );
}

TEST_CASE( "D19 feature set join refuses ambiguous and stale pins", "[d19][foundry][features]" )
{
    FeatureSet set;
    set.setFeatureSetId( QStringLiteral( "fs-spectral-1" ) );
    set.setSchemaVersion( 1 );
    set.setSampleKey( QStringLiteral( "sample_id" ) );
    set.setInputDatasetVersionId( QStringLiteral( "version-a" ) );
    set.setProducer( QStringLiteral( "rs:temporal_region_features" ) );
    FeatureColumn ndvi;
    ndvi.name = QStringLiteral( "ndvi_mean" );
    ndvi.dtype = QStringLiteral( "float64" );
    ndvi.unit = QStringLiteral( "1" );
    set.columns().append( ndvi );
    REQUIRE( set.validate().has_value() );
    CHECK( !set.schemaDigest().isEmpty() );

    QVector<FeatureRow> rows;
    FeatureRow r1;
    r1.sampleId = QStringLiteral( "s1" );
    r1.values.insert( QStringLiteral( "ndvi_mean" ), 0.4 );
    rows.append( r1 );
    FeatureRow r2a;
    r2a.sampleId = QStringLiteral( "s2" );
    r2a.values.insert( QStringLiteral( "ndvi_mean" ), 0.1 );
    rows.append( r2a );
    FeatureRow r2b = r2a;
    r2b.values.insert( QStringLiteral( "ndvi_mean" ), 0.2 );
    rows.append( r2b ); // duplicate key for s2

    const auto join = joinFeaturesBySampleId(
        set, rows, QStringList{ QStringLiteral( "s1" ), QStringLiteral( "s2" ),
                                QStringLiteral( "s3" ) },
        QStringLiteral( "version-a" ) );
    CHECK( join.matched == 1 );
    CHECK( join.ambiguous == 1 );
    CHECK( join.missing == 1 );
    CHECK( join.verdict == AuditVerdict::Fail );

    const auto stale = joinFeaturesBySampleId( set, rows, QStringList{ QStringLiteral( "s1" ) },
                                               QStringLiteral( "version-b" ) );
    CHECK( stale.verdict == AuditVerdict::Fail );
    REQUIRE( !stale.findings.isEmpty() );
    CHECK( stale.findings.first().status == FeatureJoinStatus::StaleInputVersion );
}

TEST_CASE( "D19 FeatureSet MissingRequiredColumn yields Fail not Unknown",
           "[d19][foundry][features][995]" )
{
    FeatureSet set;
    set.setFeatureSetId( QStringLiteral( "fs-req" ) );
    set.setInputDatasetVersionId( QStringLiteral( "version-a" ) );
    set.setProducer( QStringLiteral( "rs:test" ) );
    FeatureColumn ndvi;
    ndvi.name = QStringLiteral( "ndvi_mean" );
    ndvi.dtype = QStringLiteral( "float64" );
    ndvi.required = true;
    set.columns().append( ndvi );
    REQUIRE( set.validate().has_value() );

    FeatureRow row;
    row.sampleId = QStringLiteral( "s1" );
    // deliberately omit required ndvi_mean
    row.values.insert( QStringLiteral( "other" ), 1.0 );

    const auto join = joinFeaturesBySampleId( set, QVector<FeatureRow>{ row },
                                              QStringList{ QStringLiteral( "s1" ) },
                                              QStringLiteral( "version-a" ) );
    CHECK( join.matched == 0 );
    CHECK( join.missingRequiredColumns == 1 );
    CHECK( join.verdict == AuditVerdict::Fail );
    REQUIRE( !join.findings.isEmpty() );
    CHECK( join.findings.first().status == FeatureJoinStatus::MissingRequiredColumn );

    // #1003 residual: a present-but-null required value is as absent as a
    // missing key — external FeatureSets commonly encode missing as JSON null.
    FeatureRow nullRow;
    nullRow.sampleId = QStringLiteral( "s1" );
    nullRow.values.insert( QStringLiteral( "ndvi_mean" ), QJsonValue() ); // JSON null
    const auto nullJoin = joinFeaturesBySampleId( set, QVector<FeatureRow>{ nullRow },
                                                  QStringList{ QStringLiteral( "s1" ) },
                                                  QStringLiteral( "version-a" ) );
    CHECK( nullJoin.matched == 0 );
    CHECK( nullJoin.missingRequiredColumns == 1 );
    CHECK( nullJoin.verdict == AuditVerdict::Fail );
    REQUIRE( !nullJoin.findings.isEmpty() );
    CHECK( nullJoin.findings.first().status == FeatureJoinStatus::MissingRequiredColumn );
}

TEST_CASE( "D19 sample catalog pages and summaries stay bounded", "[d19][foundry][catalog]" )
{
    QVector<SampleCatalogRow> rows;
    for ( int i = 0; i < 1000; ++i )
    {
        SampleCatalogRow row;
        row.sampleId = QStringLiteral( "s%1" ).arg( i );
        row.classCode = ( i % 2 == 0 ) ? QStringLiteral( "water" ) : QStringLiteral( "land" );
        row.sensor = ( i % 3 == 0 ) ? QStringLiteral( "S2" ) : QStringLiteral( "GF" );
        row.region = QStringLiteral( "A" );
        row.year = 2024 + ( i % 2 );
        row.splitRole = ( i % 10 == 0 ) ? QStringLiteral( "test" ) : QStringLiteral( "train" );
        row.hasPseudoLabel = ( i % 7 == 0 );
        rows.append( row );
    }

    SampleCatalogFilter filter;
    filter.classCodes = QStringList{ QStringLiteral( "water" ) };
    filter.pseudoLabelsOnly = false;
    const SampleCatalogPage page = querySampleCatalog( rows, filter, 0, 50 );
    CHECK( page.limit == 50 );
    CHECK( page.rows.size() == 50 );
    CHECK( page.totalMatched > 50 );
    for ( const SampleCatalogRow &row : page.rows )
    {
        CHECK( row.classCode == QStringLiteral( "water" ) );
        CHECK( !row.hasPseudoLabel );
    }

    const SampleCatalogSummary summary = summarizeSampleCatalog( rows );
    CHECK( summary.total == 1000 );
    CHECK( summary.pseudoLabelCount > 0 );
    CHECK( summary.byClass.value( QStringLiteral( "water" ) ) == 500 );
}

TEST_CASE( "D19 QA labels stay Unknown unless labelsAudited",
           "[d19][foundry][qa][996]" )
{
    DatasetQaInputs inputs;
    inputs.datasetVersionId = QStringLiteral( "v-labels" );
    inputs.versionFrozen = true;
    inputs.provenanceComplete = true;
    inputs.composition.sampleCount = 5;
    inputs.catalogSummary.total = 5;
    inputs.duplicateSampleIds = 0;
    // labelsAudited defaults false — must not Pass as "label QA clean".
    const DatasetQaReport report = buildDatasetQaReport( inputs );
    AuditVerdict labels = AuditVerdict::Pass;
    for ( const DatasetQaCategory &category : report.categories() )
    {
        if ( category.name == QLatin1String( "labels" ) )
            labels = category.verdict;
    }
    CHECK( labels == AuditVerdict::Unknown );

    inputs.labelsAudited = true;
    const DatasetQaReport audited = buildDatasetQaReport( inputs );
    for ( const DatasetQaCategory &category : audited.categories() )
    {
        if ( category.name == QLatin1String( "labels" ) )
            labels = category.verdict;
    }
    CHECK( labels == AuditVerdict::Pass );
}

TEST_CASE( "D19 QA identity stays Unknown when uniqueness evidence is incomplete",
           "[d19][foundry][qa][1004]" )
{
    auto identityVerdict = []( const DatasetQaInputs &inputs ) {
        const DatasetQaReport report = buildDatasetQaReport( inputs );
        for ( const DatasetQaCategory &category : report.categories() )
            if ( category.name == QLatin1String( "identity" ) )
                return category.verdict;
        return AuditVerdict::Unknown;
    };

    DatasetQaInputs inputs;
    inputs.datasetVersionId = QStringLiteral( "v-cap" );
    inputs.versionFrozen = true;
    inputs.duplicateSampleIds = 0;

    // Complete evidence: uniqueness over the full version claims Pass.
    inputs.scannedSamples = 10;
    inputs.totalSamples = 10;
    inputs.scanCapped = false;
    CHECK( identityVerdict( inputs ) == AuditVerdict::Pass );

    // #1004: scan window capped — duplicates may live beyond it, never Pass.
    inputs.scannedSamples = 10000;
    inputs.totalSamples = 25000;
    inputs.scanCapped = true;
    CHECK( identityVerdict( inputs ) == AuditVerdict::Unknown );

    // Uncapped, but the version holds more samples than were scanned.
    inputs.scannedSamples = 100;
    inputs.totalSamples = 150;
    inputs.scanCapped = false;
    CHECK( identityVerdict( inputs ) == AuditVerdict::Unknown );
}

TEST_CASE( "D19 QA carries a CRS category for schema/sample CRS honesty",
           "[d19][foundry][qa][1007]" )
{
    auto crsCategory = []( const DatasetQaInputs &inputs ) {
        const DatasetQaReport report = buildDatasetQaReport( inputs );
        for ( const DatasetQaCategory &category : report.categories() )
            if ( category.name == QLatin1String( "crs" ) )
                return category;
        return DatasetQaCategory{};
    };

    DatasetQaInputs inputs;
    inputs.datasetVersionId = QStringLiteral( "v-crs" );
    inputs.versionFrozen = true;

    // Empty schema CRS means mixed/unspecified — must surface, not stay silent.
    inputs.schemaCrs = QString();
    CHECK( crsCategory( inputs ).verdict == AuditVerdict::Unknown );

    inputs.schemaCrs = QStringLiteral( "EPSG:32650" );
    inputs.distinctSampleCrs = { QStringLiteral( "EPSG:32650" ) };
    CHECK( crsCategory( inputs ).verdict == AuditVerdict::Pass );

    // Sample CRS unspecified under a declared schema stays Pass.
    inputs.distinctSampleCrs.clear();
    CHECK( crsCategory( inputs ).verdict == AuditVerdict::Pass );

    inputs.distinctSampleCrs = { QStringLiteral( "EPSG:4326" ) };
    const DatasetQaCategory conflict = crsCategory( inputs );
    CHECK( conflict.verdict == AuditVerdict::Warn );
    CHECK( conflict.evidence.value( QStringLiteral( "conflicting_sample_crs" ) ).toArray().size()
           == 1 );
}

TEST_CASE( "D19 QA report uses PASS/WARN/FAIL/UNKNOWN categories", "[d19][foundry][qa]" )
{
    DatasetQaInputs inputs;
    inputs.datasetVersionId = QStringLiteral( "v1" );
    inputs.versionFrozen = true;
    inputs.provenanceComplete = true;
    inputs.composition.sampleCount = 10;
    inputs.composition.byClass.insert( QStringLiteral( "a" ), 9 );
    inputs.composition.byClass.insert( QStringLiteral( "b" ), 1 );
    ImbalanceFinding imbalance;
    imbalance.dimension = QStringLiteral( "class" );
    imbalance.detail = QStringLiteral( "9:1" );
    inputs.imbalances.append( imbalance );
    inputs.catalogSummary.total = 10;
    inputs.catalogSummary.pseudoLabelCount = 2;

    LeakageReport leakage;
    leakage.setAuditedChecks( QStringList{ QStringLiteral( "exact_duplicate" ) } );
    inputs.leakage = leakage;

    const DatasetQaReport report = buildDatasetQaReport( inputs );
    CHECK( report.overallVerdict() == AuditVerdict::Warn ); // imbalance + pseudo
    const QJsonObject json = report.toJson();
    CHECK( json.value( QStringLiteral( "overall" ) ).toString() == QStringLiteral( "warn" ) );
    CHECK( json.value( QStringLiteral( "categories" ) ).toArray().size() >= 5 );
}

TEST_CASE( "D19 FoundryService inspects committed versions", "[d19][foundry][service]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    DatasetStore store;
    QString error;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "foundry.sqlite" ) ), &error ) );

    const QString datasetId = DatasetId::generate().toString();
    const QString versionId = DatasetVersionId::generate().toString();
    REQUIRE( store.createDataset( DatasetId::fromString( datasetId ).value(),
                                  QStringLiteral( "D19" ) )
                 .has_value() );
    auto manifest = makeManifest( datasetId, versionId );
    REQUIRE( store.createDraftVersion( manifest ).has_value() );
    REQUIRE( store.stageVersion( DatasetVersionId::fromString( versionId ).value() ).has_value() );
    REQUIRE( store.commitVersion( DatasetVersionId::fromString( versionId ).value() ).has_value() );

    DatasetFoundryService foundry( &store );
    const auto inspect =
        foundry.inspectVersion( DatasetVersionId::fromString( versionId ).value() );
    REQUIRE( inspect.has_value() );
    CHECK( inspect->value( QStringLiteral( "role" ) ).toString() == QStringLiteral( "training" ) );
    CHECK( inspect->value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "committed" ) );
    CHECK( inspect->value( QStringLiteral( "sample_count" ) ).toInteger() == 0 );
}

TEST_CASE( "D19 sample catalog scale stress stays page-bounded (100k)",
           "[d19][foundry][catalog][scale][hermetic]" )
{
    // Target toward 100k logical samples. 1M QString-heavy rows is too heavy
    // for this shared authoring box (~4 GiB available RAM alongside other
    // agents); N=100000 proves paging/filter/summary without materializing
    // an unbounded result page. Page hard-cap remains 500.
    constexpr int kCatalogScaleN = 100000;
    QVector<SampleCatalogRow> rows;
    rows.reserve( kCatalogScaleN );
    for ( int i = 0; i < kCatalogScaleN; ++i )
    {
        SampleCatalogRow row;
        row.sampleId = QStringLiteral( "scale-%1" ).arg( i );
        row.classCode =
            ( i % 4 == 0 ) ? QStringLiteral( "water" ) : QStringLiteral( "land" );
        row.sensor = ( i % 3 == 0 ) ? QStringLiteral( "S2" ) : QStringLiteral( "GF" );
        row.region = ( i % 2 == 0 ) ? QStringLiteral( "A" ) : QStringLiteral( "B" );
        row.modality = QStringLiteral( "optical" );
        row.year = 2020 + ( i % 5 );
        row.splitRole =
            ( i % 10 == 0 ) ? QStringLiteral( "test" ) : QStringLiteral( "train" );
        row.hasPseudoLabel = ( i % 17 == 0 );
        rows.append( row );
    }
    REQUIRE( rows.size() == kCatalogScaleN );

    SampleCatalogFilter filter;
    filter.classCodes = QStringList{ QStringLiteral( "water" ) };
    filter.regions = QStringList{ QStringLiteral( "A" ) };
    filter.pseudoLabelsOnly = false;

    // Oversize limit must clamp to hard bound 500.
    const SampleCatalogPage page = querySampleCatalog( rows, filter, 0, 10000 );
    CHECK( page.limit == 500 );
    CHECK( page.rows.size() <= 500 );
    CHECK( page.rows.size() == page.limit );
    CHECK( page.totalMatched > page.limit );
    for ( const SampleCatalogRow &row : page.rows )
    {
        CHECK( row.classCode == QStringLiteral( "water" ) );
        CHECK( row.region == QStringLiteral( "A" ) );
        CHECK( !row.hasPseudoLabel );
    }

    // Deep page still bounded; offset past end yields empty rows with count.
    const SampleCatalogPage deep = querySampleCatalog( rows, filter, 50000, 50 );
    CHECK( deep.limit == 50 );
    CHECK( deep.rows.size() <= 50 );
    CHECK( deep.totalMatched == page.totalMatched );

    const SampleCatalogSummary summary = summarizeSampleCatalog( rows, filter );
    CHECK( summary.total == page.totalMatched );
    CHECK( summary.byRegion.value( QStringLiteral( "A" ) ) == summary.total );
    CHECK( summary.pseudoLabelCount == 0 );
}

TEST_CASE( "D19 version evolution Source→Derived→Benchmark keeps lineage",
           "[d19][foundry][version][hermetic]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    DatasetStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "evolve.sqlite" ) ) ) );

    const QString datasetId = DatasetId::generate().toString();
    REQUIRE( store.createDataset( DatasetId::fromString( datasetId ).value(),
                                  QStringLiteral( "D19 evolve" ) )
                 .has_value() );

    auto commitWithRole = [&]( DatasetRole role, const QString &parentId,
                               const QString &name ) -> QString {
        const QString versionId = DatasetVersionId::generate().toString();
        DatasetManifest manifest = makeManifest( datasetId, versionId );
        manifest.setName( name );
        manifest.setRole( role );
        if ( !parentId.isEmpty() )
            manifest.setParentVersionId( parentId );
        REQUIRE( store.createDraftVersion( manifest ).has_value() );
        REQUIRE( store.stageVersion( DatasetVersionId::fromString( versionId ).value() )
                     .has_value() );
        REQUIRE( store.commitVersion( DatasetVersionId::fromString( versionId ).value() )
                     .has_value() );
        return versionId;
    };

    const QString v1 = commitWithRole( DatasetRole::Source, QString(),
                                       QStringLiteral( "v1-source" ) );
    const QString v2 = commitWithRole( DatasetRole::Derived, v1,
                                       QStringLiteral( "v2-derived" ) );
    const QString v3 = commitWithRole( DatasetRole::Benchmark, v2,
                                       QStringLiteral( "v3-benchmark" ) );

    // createDerivedVersion from frozen v1 still works (role inherited until
    // a role-retargeted draft is used for scientific evolution).
    const auto forked =
        store.createDerivedVersion( DatasetVersionId::fromString( v1 ).value(),
                                    QStringLiteral( "fork" ) );
    REQUIRE( forked.has_value() );
    CHECK( forked->parentVersionId() == v1 );
    CHECK( forked->status() == DatasetVersionStatus::Draft );

    DatasetFoundryService foundry( &store );
    const auto lineage =
        foundry.versionLineage( DatasetVersionId::fromString( v3 ).value() );
    REQUIRE( lineage.has_value() );
    REQUIRE( lineage->size() == 3 );
    CHECK( lineage->at( 0 ).versionId() == v3 );
    CHECK( lineage->at( 1 ).versionId() == v2 );
    CHECK( lineage->at( 2 ).versionId() == v1 );

    const auto i1 =
        foundry.inspectVersion( DatasetVersionId::fromString( v1 ).value() );
    const auto i2 =
        foundry.inspectVersion( DatasetVersionId::fromString( v2 ).value() );
    const auto i3 =
        foundry.inspectVersion( DatasetVersionId::fromString( v3 ).value() );
    REQUIRE( i1.has_value() );
    REQUIRE( i2.has_value() );
    REQUIRE( i3.has_value() );
    CHECK( i1->value( QStringLiteral( "role" ) ).toString() == QStringLiteral( "source" ) );
    CHECK( i2->value( QStringLiteral( "role" ) ).toString() == QStringLiteral( "derived" ) );
    CHECK( i3->value( QStringLiteral( "role" ) ).toString() ==
           QStringLiteral( "benchmark" ) );
    CHECK( i1->value( QStringLiteral( "status" ) ).toString() ==
           QStringLiteral( "committed" ) );
}

TEST_CASE( "D19 QA refuses clean claim when leakage findings are errors",
           "[d19][foundry][leakage][hermetic]" )
{
    DatasetQaInputs inputs;
    inputs.datasetVersionId = QStringLiteral( "v-leak" );
    inputs.versionFrozen = true;
    inputs.provenanceComplete = true;
    inputs.composition.sampleCount = 4;
    inputs.catalogSummary.total = 4;
    inputs.catalogSummary.pseudoLabelCount = 0;

    LeakageReport dirty;
    dirty.setAuditedChecks( QStringList{ leakageKindToString( LeakageKind::ExactDuplicate ),
                                         leakageKindToString( LeakageKind::OverlappingPatch ) } );
    LeakageFinding finding;
    finding.kind = LeakageKind::ExactDuplicate;
    finding.severity = DiagnosticSeverity::Error;
    finding.sampleA = QStringLiteral( "train-1" );
    finding.sampleB = QStringLiteral( "test-9" );
    dirty.findings().append( finding );
    inputs.leakage = dirty;

    const DatasetQaReport failReport = buildDatasetQaReport( inputs );
    CHECK( failReport.overallVerdict() == AuditVerdict::Fail );
    bool sawLeakFail = false;
    for ( const DatasetQaCategory &category : failReport.categories() )
    {
        if ( category.name == QLatin1String( "leakage" ) )
        {
            sawLeakFail = true;
            CHECK( category.verdict == AuditVerdict::Fail );
        }
    }
    CHECK( sawLeakFail );

    // Audited + empty findings → Pass on leakage (honest clean claim).
    LeakageReport clean;
    clean.setAuditedChecks( dirty.auditedChecks() );
    inputs.leakage = clean;
    const DatasetQaReport passReport = buildDatasetQaReport( inputs );
    AuditVerdict leakVerdict = AuditVerdict::Unknown;
    for ( const DatasetQaCategory &category : passReport.categories() )
    {
        if ( category.name == QLatin1String( "leakage" ) )
            leakVerdict = category.verdict;
    }
    CHECK( leakVerdict == AuditVerdict::Pass );
    CHECK( passReport.overallVerdict() != AuditVerdict::Fail );
}
