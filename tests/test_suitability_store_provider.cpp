// test_suitability_store_provider.cpp — Slice E: StoreDataProvider, the
// read-only projection from a real DatasetStore onto DatasetFacts.
//
// Contract:
//   - the provider is a thin, stateless READ-side adapter: it calls only
//     store read APIs (sampleCount, facetDistribution, facetNames, version
//     manifest, paged samples) and never a write path or a cache;
//   - an absent store or an unknown version fails typed — a projection is
//     never invented around a missing subject;
//   - schema fields come from the version manifest; whatever the manifest
//     does not carry stays unknown (never guessed);
//   - every LIMIT touch (facet tail folded by the store, sample-scan cap)
//     must surface as factsTruncated = true.

#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_ids.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "dataset/sample.h"
#include "suitability/store_data_provider.h"

#include <QDateTime>
#include <QTemporaryDir>

#include <vector>

using sicnu::dataset::DatasetId;
using sicnu::dataset::DatasetManifest;
using sicnu::dataset::DatasetStore;
using sicnu::dataset::DatasetVersionId;
using sicnu::dataset::PointSample;
using sicnu::dataset::SampleId;
using sicnu::dataset::SampleKind;
using sicnu::dataset::SampleRecord;
using sicnu::suitability::DatasetFacts;
using sicnu::suitability::FactsLimits;
using sicnu::suitability::StoreDataProvider;

namespace
{

using FacetEntry = DatasetStore::FacetEntry;

struct StoreFixture
{
    QTemporaryDir dir;
    DatasetStore store;
    DatasetId datasetId = DatasetId::generate();
    DatasetVersionId versionId = DatasetVersionId::generate();

    StoreFixture()
    {
        REQUIRE( store.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
        REQUIRE( store.createDataset( datasetId, QStringLiteral( "suitability-store" ) ).has_value() );

        DatasetManifest manifest;
        manifest.setDatasetId( datasetId.toString() );
        manifest.setVersionId( versionId.toString() );
        manifest.setName( QStringLiteral( "Suitability Store Fixture" ) );
        manifest.setCreatedAtUtc( QDateTime::fromString(
            QStringLiteral( "2026-09-21T00:00:00.000Z" ), Qt::ISODateWithMs ) );
        manifest.schema().modality = QStringLiteral( "optical" );
        manifest.schema().sensor = QStringLiteral( "Sentinel-2/S2MSI2A" );
        manifest.schema().crs = QStringLiteral( "EPSG:32650" );
        manifest.schema().bandRoles = { QStringLiteral( "red" ), QStringLiteral( "green" ),
                                        QStringLiteral( "blue" ), QStringLiteral( "nir" ) };
        manifest.labelSchema().schemaId = QStringLiteral( "schema-landcover" );
        manifest.labelSchema().version = 1;
        manifest.spatialExtent().valid = true;
        manifest.spatialExtent().minimumX = 0.0;
        manifest.spatialExtent().minimumY = 0.0;
        manifest.spatialExtent().maximumX = 100.0;
        manifest.spatialExtent().maximumY = 100.0;
        manifest.temporalExtent().valid = true;
        manifest.temporalExtent().startUtc = QDateTime::fromString(
            QStringLiteral( "2023-01-01T00:00:00Z" ), Qt::ISODate );
        manifest.temporalExtent().endUtc = QDateTime::fromString(
            QStringLiteral( "2024-12-31T00:00:00Z" ), Qt::ISODate );
        REQUIRE( store.createDraftVersion( manifest ).has_value() );
    }

    void addSample( bool withTime, const QVector<FacetEntry> &facets )
    {
        SampleRecord sample;
        sample.setSampleId( SampleId::generate().toString() );
        sample.setDatasetVersionId( versionId.toString() );
        sample.setKind( SampleKind::Point );
        if ( withTime )
            sample.setTimeUtc( QDateTime::fromString(
                QStringLiteral( "2024-05-01T10:00:00Z" ), Qt::ISODate ) );
        PointSample point;
        point.x = 0.0;
        point.y = 0.0;
        sample.payload() = point;
        REQUIRE( store.addSamples( { sample } ).has_value() );
        if ( !facets.isEmpty() )
        {
            REQUIRE( store.setSampleFacets( versionId,
                                            SampleId::fromString( sample.sampleId() ).value(),
                                            facets )
                         .has_value() );
        }
    }
};

} // namespace

TEST_CASE( "store provider refuses a missing store or an unknown version typed",
           "[suitability][store]" )
{
    StoreDataProvider nullProvider( nullptr );
    const auto unavailable = nullProvider.datasetFacts( QStringLiteral( "dv-x" ), {} );
    REQUIRE( !unavailable.has_value() );
    REQUIRE( unavailable.diagnostics().first().code ==
             QStringLiteral( "suitability.provider_unavailable" ) );

    StoreFixture fixture;
    StoreDataProvider provider( &fixture.store );
    const auto unknown = provider.datasetFacts(
        DatasetVersionId::generate().toString(), {} );
    REQUIRE( !unknown.has_value() );
    REQUIRE( unknown.diagnostics().first().code ==
             QStringLiteral( "suitability.dataset_unknown" ) );

    // A text that is not even a version id cannot name a subject either.
    const auto malformed = provider.datasetFacts( QStringLiteral( "not-a-uuid" ), {} );
    REQUIRE( !malformed.has_value() );
    REQUIRE( malformed.diagnostics().first().code ==
             QStringLiteral( "suitability.dataset_unknown" ) );
}

TEST_CASE( "store provider projects manifest, facets and counts into facts",
           "[suitability][store]" )
{
    StoreFixture fixture;
    // 3 forest (two timed spring samples, one untimed), 2 water (summer),
    // 1 urban (untimed autumn facet). One forest sample carries a pseudo
    // label-source facet.
    fixture.addSample( true, { { QStringLiteral( "class" ), QStringLiteral( "forest" ) },
                               { QStringLiteral( "season" ), QStringLiteral( "spring" ) },
                               { QStringLiteral( "year" ), QStringLiteral( "2024" ) } } );
    fixture.addSample( true, { { QStringLiteral( "class" ), QStringLiteral( "forest" ) },
                               { QStringLiteral( "season" ), QStringLiteral( "spring" ) },
                               { QStringLiteral( "year" ), QStringLiteral( "2024" ) },
                               { QStringLiteral( "label_source" ), QStringLiteral( "pseudo" ) } } );
    fixture.addSample( false, { { QStringLiteral( "class" ), QStringLiteral( "forest" ) },
                                { QStringLiteral( "year" ), QStringLiteral( "2024" ) } } );
    fixture.addSample( true, { { QStringLiteral( "class" ), QStringLiteral( "water" ) },
                               { QStringLiteral( "season" ), QStringLiteral( "summer" ) },
                               { QStringLiteral( "year" ), QStringLiteral( "2024" ) } } );
    fixture.addSample( true, { { QStringLiteral( "class" ), QStringLiteral( "water" ) },
                               { QStringLiteral( "season" ), QStringLiteral( "summer" ) },
                               { QStringLiteral( "year" ), QStringLiteral( "2024" ) } } );
    fixture.addSample( false, { { QStringLiteral( "class" ), QStringLiteral( "urban" ) },
                                { QStringLiteral( "season" ), QStringLiteral( "autumn" ) },
                                { QStringLiteral( "year" ), QStringLiteral( "2023" ) } } );

    StoreDataProvider provider( &fixture.store );
    const auto result = provider.datasetFacts( fixture.versionId.toString(), FactsLimits{} );
    REQUIRE( result.has_value() );

    const DatasetFacts facts = result.value();
    REQUIRE( facts.datasetVersionId == fixture.versionId.toString() );
    REQUIRE( !facts.factsTruncated );
    REQUIRE( facts.sampleCount == 6 );
    REQUIRE( facts.hasLabelSchema );
    REQUIRE( facts.labelClasses.size() == 3 );
    REQUIRE( facts.labelClasses.contains( QStringLiteral( "forest" ) ) );
    REQUIRE( facts.labelClasses.contains( QStringLiteral( "water" ) ) );
    REQUIRE( facts.labelClasses.contains( QStringLiteral( "urban" ) ) );
    REQUIRE( facts.samplesByClass.value( QStringLiteral( "forest" ) ) == 3 );
    REQUIRE( facts.samplesByClass.value( QStringLiteral( "water" ) ) == 2 );
    REQUIRE( facts.samplesByClass.value( QStringLiteral( "urban" ) ) == 1 );
    REQUIRE( facts.samplesBySeason.value( QStringLiteral( "spring" ) ) == 2 );
    REQUIRE( facts.samplesBySeason.value( QStringLiteral( "summer" ) ) == 2 );
    REQUIRE( facts.samplesBySeason.value( QStringLiteral( "autumn" ) ) == 1 );
    REQUIRE( facts.samplesByYear.value( QStringLiteral( "2024" ) ) == 5 );
    REQUIRE( facts.samplesByYear.value( QStringLiteral( "2023" ) ) == 1 );
    // Two samples carry no observation time (the store scan measures them).
    REQUIRE( facts.missingTimeCount == 2 );
    // Pseudo evidence comes from the label_source facet, not a guess.
    REQUIRE( facts.pseudoLabelCount == 1 );
    // Schema-derived projections: present in the manifest -> present in facts.
    REQUIRE( facts.bandRoles.size() == 4 );
    REQUIRE( facts.modality == QStringLiteral( "optical" ) );
    REQUIRE( facts.sensor == QStringLiteral( "Sentinel-2/S2MSI2A" ) );
    REQUIRE( facts.crsWkt == QStringLiteral( "EPSG:32650" ) );
    REQUIRE( facts.hasExtent );
    REQUIRE( facts.minX == 0.0 );
    REQUIRE( facts.maxY == 100.0 );
    REQUIRE( facts.hasTemporalExtent );
    REQUIRE( facts.temporalStartUtc.isValid() );
    REQUIRE( facts.temporalEndUtc.isValid() );
}

TEST_CASE( "store provider reports facet-tail truncation", "[suitability][store]" )
{
    StoreFixture fixture;
    // 70 distinct classes: past the default 64-value cap the store folds the
    // tail into its reserved bucket, and that fold must be visible.
    for ( int i = 0; i < 70; ++i )
    {
        fixture.addSample( true,
                           { { QStringLiteral( "class" ), QStringLiteral( "c%1" ).arg( i, 2, 10, QLatin1Char( '0' ) ) } } );
    }

    StoreDataProvider provider( &fixture.store );
    const auto result = provider.datasetFacts( fixture.versionId.toString(), FactsLimits{} );
    REQUIRE( result.has_value() );
    const DatasetFacts facts = result.value();
    REQUIRE( facts.factsTruncated );
    REQUIRE( facts.samplesByClass.size() == FactsLimits{}.maxClassValues );
    // The reserved fold bucket is the store's bookkeeping, not a class.
    REQUIRE( !facts.samplesByClass.contains( QStringLiteral( "(other)" ) ) );
    REQUIRE( facts.labelClasses.size() == FactsLimits{}.maxClassValues );
}

TEST_CASE( "store provider caps the sample scan and reports it", "[suitability][store]" )
{
    StoreFixture fixture;
    for ( int i = 0; i < 5; ++i )
        fixture.addSample( i == 4, {} ); // exactly one untimed sample overall

    StoreDataProvider provider( &fixture.store );
    FactsLimits limits;
    limits.maxSampleScan = 2; // the scan stops before the untimed sample
    const auto result = provider.datasetFacts( fixture.versionId.toString(), limits );
    REQUIRE( result.has_value() );
    const DatasetFacts facts = result.value();
    // A capped scan cannot claim a total: the count stays unknown and only
    // the truncation flag carries the news.
    REQUIRE( facts.factsTruncated );
    REQUIRE( facts.missingTimeCount == -1 );
    // The count path is independent of the scan cap.
    REQUIRE( facts.sampleCount == 5 );
}

TEST_CASE( "store provider leaves label evidence unknown when the manifest has none",
           "[suitability][store]" )
{
    QTemporaryDir dir;
    DatasetStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
    const DatasetId datasetId = DatasetId::generate();
    REQUIRE( store.createDataset( datasetId, QStringLiteral( "bare" ) ).has_value() );
    const DatasetVersionId versionId = DatasetVersionId::generate();
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId.toString() );
    manifest.setVersionId( versionId.toString() );
    manifest.setName( QStringLiteral( "Bare Version" ) );
    manifest.setCreatedAtUtc( QDateTime::fromString(
        QStringLiteral( "2026-09-21T00:00:00.000Z" ), Qt::ISODateWithMs ) );
    REQUIRE( store.createDraftVersion( manifest ).has_value() );

    StoreDataProvider provider( &store );
    const auto result = provider.datasetFacts( versionId.toString(), FactsLimits{} );
    REQUIRE( result.has_value() );
    const DatasetFacts facts = result.value();
    // A zero sample count is a MEASUREMENT (the store counted zero rows);
    // it must not be confused with an unknown count.
    REQUIRE( facts.sampleCount == 0 );
    REQUIRE( !facts.hasLabelSchema );
    REQUIRE( facts.labelClasses.isEmpty() );
    REQUIRE( facts.bandRoles.isEmpty() );
    REQUIRE( facts.modality.isEmpty() );
    REQUIRE( !facts.hasExtent );
    REQUIRE( !facts.hasTemporalExtent );
    REQUIRE( facts.pseudoLabelCount == -1 );
    REQUIRE( facts.missingTimeCount == 0 ); // scanned all zero rows
}
