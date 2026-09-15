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
