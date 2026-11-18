// test_r35_t03_dataset_manifest.cpp — R35-T03 WP16: strict manifest
// decoding. A PRESENT field of the wrong JSON type is a typed failure — it
// must not fold into "no entries", "revision 0 (not pinned)" or an empty
// schema. revision 0 itself stays the legal "not pinned" spelling.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_manifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

using namespace sicnu::dataset;

namespace
{

QJsonObject legalManifest()
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kDatasetManifestSerializationVersion );
    json.insert( QStringLiteral( "dataset_id" ), QUuid::createUuid().toString( QUuid::WithoutBraces ) );
    json.insert( QStringLiteral( "version_id" ), QUuid::createUuid().toString( QUuid::WithoutBraces ) );

    QJsonObject sourceAsset;
    sourceAsset.insert( QStringLiteral( "asset_id" ), QUuid::createUuid().toString( QUuid::WithoutBraces ) );
    sourceAsset.insert( QStringLiteral( "revision" ), qint64( 3 ) );
    QJsonArray bands;
    bands.append( QStringLiteral( "B1" ) );
    bands.append( QStringLiteral( "B2" ) );
    sourceAsset.insert( QStringLiteral( "bands" ), bands );
    QJsonArray sourceAssets;
    sourceAssets.append( sourceAsset );
    json.insert( QStringLiteral( "source_assets" ), sourceAssets );

    QJsonObject entry;
    entry.insert( QStringLiteral( "kind" ), QStringLiteral( "asset" ) );
    entry.insert( QStringLiteral( "ref_id" ), QUuid::createUuid().toString( QUuid::WithoutBraces ) );
    entry.insert( QStringLiteral( "revision" ), qint64( 2 ) );
    QJsonArray entries;
    entries.append( entry );
    json.insert( QStringLiteral( "entries" ), entries );

    QJsonObject schema;
    schema.insert( QStringLiteral( "modality" ), QStringLiteral( "optical" ) );
    QJsonObject resolution;
    resolution.insert( QStringLiteral( "x" ), 10.0 );
    resolution.insert( QStringLiteral( "y" ), 10.0 );
    schema.insert( QStringLiteral( "resolution" ), resolution );
    json.insert( QStringLiteral( "schema" ), schema );

    QJsonObject spatial;
    spatial.insert( QStringLiteral( "min_x" ), 8.0 );
    spatial.insert( QStringLiteral( "min_y" ), 50.0 );
    spatial.insert( QStringLiteral( "max_x" ), 9.0 );
    spatial.insert( QStringLiteral( "max_y" ), 51.0 );
    json.insert( QStringLiteral( "spatial_extent" ), spatial );

    QJsonArray tags;
    tags.append( QStringLiteral( "test" ) );
    json.insert( QStringLiteral( "tags" ), tags );
    return json;
}

bool manifestFails( const QJsonObject &json )
{
    return !DatasetManifest::fromJson( json ).has_value();
}

} // namespace

TEST_CASE( "wrong-typed manifest fields fail loudly instead of folding to defaults",
           "[dataset][manifest][r35t03]" )
{
    REQUIRE( DatasetManifest::fromJson( legalManifest() ).has_value() );

    // Structure containers: a string/number in an array/object slot is a
    // failure, never "empty collection".
    QJsonObject entriesString = legalManifest();
    entriesString.insert( QStringLiteral( "entries" ), QStringLiteral( "nope" ) );
    CHECK( manifestFails( entriesString ) );

    QJsonObject entriesElement = legalManifest();
    {
        QJsonArray array;
        array.append( QStringLiteral( "not-an-object" ) );
        entriesElement.insert( QStringLiteral( "entries" ), array );
    }
    CHECK( manifestFails( entriesElement ) );

    QJsonObject sourcesNumber = legalManifest();
    sourcesNumber.insert( QStringLiteral( "source_assets" ), 5 );
    CHECK( manifestFails( sourcesNumber ) );

    QJsonObject schemaString = legalManifest();
    schemaString.insert( QStringLiteral( "schema" ), QStringLiteral( "optical" ) );
    CHECK( manifestFails( schemaString ) );

    QJsonObject resolutionNumber = legalManifest();
    resolutionNumber.insert( QStringLiteral( "schema" ), [] {
        QJsonObject schema;
        schema.insert( QStringLiteral( "resolution" ), 10 );
        return schema;
    }() );
    CHECK( manifestFails( resolutionNumber ) );

    QJsonObject resolutionXString = legalManifest();
    {
        QJsonObject schema = resolutionXString.value( QStringLiteral( "schema" ) ).toObject();
        QJsonObject resolution = schema.value( QStringLiteral( "resolution" ) ).toObject();
        resolution.insert( QStringLiteral( "x" ), QStringLiteral( "ten" ) );
        schema.insert( QStringLiteral( "resolution" ), resolution );
        resolutionXString.insert( QStringLiteral( "schema" ), schema );
    }
    CHECK( manifestFails( resolutionXString ) );

    QJsonObject extentArray = legalManifest();
    {
        QJsonArray bbox;
        bbox.append( 8.0 );
        bbox.append( 50.0 );
        bbox.append( 9.0 );
        bbox.append( 51.0 );
        extentArray.insert( QStringLiteral( "spatial_extent" ), bbox );
    }
    CHECK( manifestFails( extentArray ) );

    QJsonObject labelSchemaString = legalManifest();
    labelSchemaString.insert( QStringLiteral( "label_schema" ), QStringLiteral( "x" ) );
    CHECK( manifestFails( labelSchemaString ) );

    QJsonObject tagsString = legalManifest();
    tagsString.insert( QStringLiteral( "tags" ), QStringLiteral( "test" ) );
    CHECK( manifestFails( tagsString ) );

    QJsonObject tagsMixed = legalManifest();
    {
        QJsonArray array;
        array.append( QStringLiteral( "ok" ) );
        array.append( 3 );
        tagsMixed.insert( QStringLiteral( "tags" ), array );
    }
    CHECK( manifestFails( tagsMixed ) );

    QJsonObject provenanceString = legalManifest();
    provenanceString.insert( QStringLiteral( "provenance" ), QStringLiteral( "p" ) );
    CHECK( manifestFails( provenanceString ) );

    // Bands must be strings, not numbers.
    QJsonObject bandsNumeric = legalManifest();
    {
        QJsonObject sourceAsset = bandsNumeric.value( QStringLiteral( "source_assets" ) )
                                      .toArray()
                                      .at( 0 )
                                      .toObject();
        QJsonArray bands;
        bands.append( 1 );
        sourceAsset.insert( QStringLiteral( "bands" ), bands );
        QJsonArray sourceAssets;
        sourceAssets.append( sourceAsset );
        bandsNumeric.insert( QStringLiteral( "source_assets" ), sourceAssets );
    }
    CHECK( manifestFails( bandsNumeric ) );
}

TEST_CASE( "manifest revisions decode strictly: 0 stays legal, wrong types fail",
           "[dataset][manifest][r35t03]" )
{
    // revision 0 is the legal "not pinned" spelling and must round-trip.
    QJsonObject zero = legalManifest();
    {
        QJsonArray sourceAssets = zero.value( QStringLiteral( "source_assets" ) ).toArray();
        QJsonObject sourceAsset = sourceAssets.at( 0 ).toObject();
        sourceAsset.insert( QStringLiteral( "revision" ), qint64( 0 ) );
        sourceAssets.replace( 0, sourceAsset );
        zero.insert( QStringLiteral( "source_assets" ), sourceAssets );
    }
    const auto unpinned = DatasetManifest::fromJson( zero );
    REQUIRE( unpinned.has_value() );
    CHECK( unpinned.value().sourceAssets().first().revision == 0 );

    // A negative revision is an explicit error, not a silent clamp to 0.
    for ( const qint64 bad : { qint64( -1 ), qint64( -100 ) } )
    {
        QJsonObject negative = legalManifest();
        QJsonArray sourceAssets = negative.value( QStringLiteral( "source_assets" ) ).toArray();
        QJsonObject sourceAsset = sourceAssets.at( 0 ).toObject();
        sourceAsset.insert( QStringLiteral( "revision" ), bad );
        sourceAssets.replace( 0, sourceAsset );
        negative.insert( QStringLiteral( "source_assets" ), sourceAssets );
        const auto refused = DatasetManifest::fromJson( negative );
        CHECK( !refused.has_value() );
    }

    // String and fractional revisions are type errors, not 0.
    QJsonObject revisionString = legalManifest();
    {
        QJsonArray sourceAssets = revisionString.value( QStringLiteral( "source_assets" ) ).toArray();
        QJsonObject sourceAsset = sourceAssets.at( 0 ).toObject();
        sourceAsset.insert( QStringLiteral( "revision" ), QStringLiteral( "3" ) );
        sourceAssets.replace( 0, sourceAsset );
        revisionString.insert( QStringLiteral( "source_assets" ), sourceAssets );
    }
    CHECK( manifestFails( revisionString ) );

    QJsonObject revisionFraction = legalManifest();
    {
        QJsonArray sourceAssets = revisionFraction.value( QStringLiteral( "source_assets" ) ).toArray();
        QJsonObject sourceAsset = sourceAssets.at( 0 ).toObject();
        sourceAsset.insert( QStringLiteral( "revision" ), 2.5 );
        sourceAssets.replace( 0, sourceAsset );
        revisionFraction.insert( QStringLiteral( "source_assets" ), sourceAssets );
    }
    CHECK( manifestFails( revisionFraction ) );

    // Entry revisions follow the same rule.
    QJsonObject entryNegative = legalManifest();
    {
        QJsonArray entries = entryNegative.value( QStringLiteral( "entries" ) ).toArray();
        QJsonObject entry = entries.at( 0 ).toObject();
        entry.insert( QStringLiteral( "revision" ), qint64( -2 ) );
        entries.replace( 0, entry );
        entryNegative.insert( QStringLiteral( "entries" ), entries );
    }
    CHECK( manifestFails( entryNegative ) );

    // An ABSENT revision stays the 0 default (optional field, absent legal).
    QJsonObject revisionAbsent = legalManifest();
    {
        QJsonArray sourceAssets = revisionAbsent.value( QStringLiteral( "source_assets" ) ).toArray();
        QJsonObject sourceAsset = sourceAssets.at( 0 ).toObject();
        sourceAsset.remove( QStringLiteral( "revision" ) );
        sourceAssets.replace( 0, sourceAsset );
        revisionAbsent.insert( QStringLiteral( "source_assets" ), sourceAssets );
    }
    const auto absent = DatasetManifest::fromJson( revisionAbsent );
    REQUIRE( absent.has_value() );
    CHECK( absent.value().sourceAssets().first().revision == 0 );
}

TEST_CASE( "schema_version must be present and integral",
           "[dataset][manifest][r35t03]" )
{
    QJsonObject missing = legalManifest();
    missing.remove( QStringLiteral( "schema_version" ) );
    const auto missingResult = DatasetManifest::fromJson( missing );
    REQUIRE( !missingResult.has_value() );
    CHECK( missingResult.diagnostics().first().code == QStringLiteral( "dataset.manifest_version" ) );

    QJsonObject typed = legalManifest();
    typed.insert( QStringLiteral( "schema_version" ), QStringLiteral( "1" ) );
    const auto typedResult = DatasetManifest::fromJson( typed );
    REQUIRE( !typedResult.has_value() );
    CHECK( typedResult.diagnostics().first().code == QStringLiteral( "dataset.manifest_version" ) );

    // Optional structures may be absent entirely — the minimal manifest
    // (ids + version only) still decodes.
    QJsonObject minimal;
    minimal.insert( QStringLiteral( "schema_version" ), kDatasetManifestSerializationVersion );
    minimal.insert( QStringLiteral( "dataset_id" ), QUuid::createUuid().toString( QUuid::WithoutBraces ) );
    minimal.insert( QStringLiteral( "version_id" ), QUuid::createUuid().toString( QUuid::WithoutBraces ) );
    const auto minimalParsed = DatasetManifest::fromJson( minimal );
    REQUIRE( minimalParsed.has_value() );
    CHECK( minimalParsed.value().entries().isEmpty() );
    CHECK( minimalParsed.value().sourceAssets().isEmpty() );
}
