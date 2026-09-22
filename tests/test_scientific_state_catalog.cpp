/***************************************************************************
  tests/test_scientific_state_catalog.cpp
  RS14-01 Scientific Data Passport — Slice F4: catalog adapter (Qt lane).

  Exercises the full chain against the REAL catalog types: DataManager
  registers a synthesized GeoTIFF, the adapter projects AssetSnapshot /
  DerivationRecord into facts, and the resolver produces the passport.
  One of the two end-to-end integration lanes for this track.
 ***************************************************************************/

#include "scientific_state/asset_state_resolver.h"
#include "scientific_state/catalog/catalog_state_facts.h"
#include "scientific_state/gdal/gdal_state_facts.h"

#include <data/data_manager.h>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <qgsapplication.h>

namespace
{

void ensureQgisApplication()
{
    if ( QApplication::instance() )
        return;
    static int argc = 1;
    static char applicationName[] = "test_scientific_state_catalog";
    static char *argv[] = { applicationName, nullptr };
    new QgsApplication( argc, argv, true );
    QgsApplication::initQgis();
    GDALAllRegister();
}

sicnu::data::AssetId registerAsset( sicnu::data::DataManager &manager, const QString &path )
{
    sicnu::data::RegisterRequest request;
    request.source.providerKey = QStringLiteral( "gdal" );
    request.source.canonicalSource = path;
    const sicnu::data::RegisterResult registered = manager.registerSource( request );
    REQUIRE_FALSE( registered.assetId.isNull() );
    return registered.assetId;
}

} // namespace

using namespace sicnu;

TEST_CASE( "catalog adapter projects a registered raster snapshot into facts",
           "[scientific_state][slice_f4]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    const QString path = dir.filePath( QStringLiteral( "asset.tif" ) );
    GDALDataset *dataset = driver->Create( path.toUtf8().constData(), 8, 8, 1, GDT_Byte, nullptr );
    REQUIRE( dataset != nullptr );
    GDALClose( dataset );

    sicnu::data::DataManager manager;
    const sicnu::data::AssetId assetId = registerAsset( manager, path );
    const std::optional<sicnu::data::AssetSnapshot> snapshot = manager.asset( assetId );
    REQUIRE( snapshot.has_value() );

    const sicnu::state::CatalogFacts facts = sicnu::state::makeCatalogFacts( *snapshot );
    CHECK( !facts.assetId.empty() );
    CHECK( facts.revision == "1" );
    CHECK( facts.kind == "raster" );
    CHECK( facts.lifecycle == "ready" );
    CHECK( facts.persistence == "project_persistent" );
    CHECK( facts.sourcePath == path.toStdString() );
    CHECK( facts.structureBandCount == 1 );
    REQUIRE( facts.bands.size() == 1 );
    CHECK( facts.bands[0].index == 1 );
    CHECK( facts.bands[0].dataType == "Byte" );

    // Resolver chain on catalog facts alone: identity is known, structure
    // mirrors into the band projection.
    sicnu::state::StateResolutionInput input;
    input.catalog = facts;
    const sicnu::state::RemoteSensingAssetState state = sicnu::state::resolveAssetState( input ).state;
    CHECK( state.assetId == facts.assetId );
    CHECK( state.kind == sicnu::state::AssetKind::Raster );
    CHECK( sicnu::state::claimFor( state, "identity.asset_id" ).kind ==
           sicnu::state::ClaimKind::Known );
    REQUIRE( state.bands.size() == 1 );
    CHECK( state.bands[0].dataType == "Byte" );
}

TEST_CASE( "derivation records project into provenance with workflow references",
           "[scientific_state][slice_f4]" )
{
    sicnu::data::DerivationRecord record;
    record.algorithmId = QStringLiteral( "rs:ndvi" );
    record.algorithmVersion = QStringLiteral( "2.0" );
    sicnu::data::DerivationInput inputRecord;
    const std::optional<sicnu::data::AssetId> parsedId = sicnu::data::AssetId::fromString(
        QStringLiteral( "0b7fd6f2-1111-4f0e-9a31-52d4a1b9c777" ) );
    REQUIRE( parsedId.has_value() );
    inputRecord.assetId = *parsedId;
    inputRecord.revision = sicnu::data::AssetRevision::initial();
    inputRecord.bandReferences = QStringList{ QStringLiteral( "nir" ), QStringLiteral( "red" ) };
    inputRecord.valueDomain = QStringLiteral( "surface_reflectance" );
    record.inputs = { inputRecord };
    record.executionFingerprint = QStringLiteral( "fp-42" );
    record.workflowId = QStringLiteral( "wf-1" );
    record.workflowRunId = QStringLiteral( "run-7" );
    record.stepId = QStringLiteral( "step-2" );
    record.softwareVersion = QStringLiteral( "13.0.0" );

    const sicnu::state::DerivationFacts facts = sicnu::state::makeDerivationFacts( record );
    CHECK( facts.algorithmId == "rs:ndvi" );
    CHECK( facts.workflowRef == "wf-1/run-7/step-2" );
    REQUIRE( facts.inputs.size() == 1 );
    CHECK( facts.inputs[0].valueDomain == "surface_reflectance" );

    sicnu::state::StateResolutionInput input;
    input.derivation = facts;
    const sicnu::state::RemoteSensingAssetState state = sicnu::state::resolveAssetState( input ).state;
    CHECK( state.provenance.isDerived );
    CHECK( state.provenance.algorithmId == "rs:ndvi" );
    CHECK( state.provenance.workflowRef == "wf-1/run-7/step-2" );
    const sicnu::state::ClaimRecord claim =
        sicnu::state::claimFor( state, "provenance.algorithm" );
    CHECK( claim.kind == sicnu::state::ClaimKind::Known );
    REQUIRE( !claim.sources.empty() );
    CHECK( claim.sources.front() == "catalog:DerivationRecord" );
}

TEST_CASE( "catalog and dataset facts merge into one passport",
           "[scientific_state][slice_f4]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    const QString path = dir.filePath( QStringLiteral( "merged.tif" ) );
    GDALDataset *dataset = driver->Create( path.toUtf8().constData(), 4, 4, 1, GDT_Byte, nullptr );
    REQUIRE( dataset != nullptr );
    dataset->GetRasterBand( 1 )->SetMetadataItem( "SICNU_BAND_ROLE", "red", nullptr );
    dataset->SetMetadataItem( "SICNU_MODALITY", "optical", nullptr );
    GDALClose( dataset );

    sicnu::data::DataManager manager;
    const sicnu::data::AssetId assetId = registerAsset( manager, path );
    const std::optional<sicnu::data::AssetSnapshot> snapshot = manager.asset( assetId );
    REQUIRE( snapshot.has_value() );

    std::string collectError;
    const std::optional<sicnu::state::DatasetFacts> datasetFacts =
        sicnu::state::collectDatasetFacts( path.toStdString(), collectError );
    REQUIRE( datasetFacts.has_value() );

    sicnu::state::StateResolutionInput input;
    input.catalog = sicnu::state::makeCatalogFacts( *snapshot );
    input.dataset = datasetFacts;
    const sicnu::state::RemoteSensingAssetState state = sicnu::state::resolveAssetState( input ).state;

    // Identity from catalog, modality from file, one unified passport.
    CHECK( !state.assetId.empty() );
    CHECK( state.sensor.modality == sicnu::state::Modality::Optical );
    const sicnu::state::ClaimRecord roleClaim = sicnu::state::claimFor( state, "bands[1].role" );
    CHECK( roleClaim.kind == sicnu::state::ClaimKind::Known );
    // Both declarative sources agree the role is known: file metadata and the
    // structure snapshot the registration recorded.
    CHECK( roleClaim.sources.size() >= 1 );
    CHECK( !state.bands.empty() );
}
