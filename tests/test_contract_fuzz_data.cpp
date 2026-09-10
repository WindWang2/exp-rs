// test_contract_fuzz_data.cpp — bounded property tests over DatasetManifest
// (task D, Verification 7.0).
//
// Contract under fuzz (from dataset_manifest.h): strict-version,
// unknown-field-TOLERANT parse; failures produce a non-empty typed error and
// never a half-filled manifest treated as valid; parse is total on bounded
// mutated JSON (no crash, no hang).
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_manifest.h"
#include "support/bounded_fuzz.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonDocument>

#include <string>
#include <vector>

using sicnu::dataset::DatasetManifest;
using sicnu::testing::BoundedRandom;

namespace
{
/// A minimal VALID manifest payload (the seed for mutations).
QJsonObject validManifest()
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), 1 );
    json.insert( QStringLiteral( "dataset_id" ), QStringLiteral( "ds-fuzz-1" ) );
    json.insert( QStringLiteral( "version_id" ), QStringLiteral( "ver-1" ) );
    json.insert( QStringLiteral( "name" ), QStringLiteral( "Fuzz Fixture" ) );

    QJsonObject schema;
    schema.insert( QStringLiteral( "modality" ), QStringLiteral( "optical" ) );
    schema.insert( QStringLiteral( "crs" ), QStringLiteral( "EPSG:32650" ) );
    json.insert( QStringLiteral( "schema" ), schema );

    QJsonArray entries;
    QJsonObject entry;
    entry.insert( QStringLiteral( "kind" ), QStringLiteral( "asset" ) );
    entry.insert( QStringLiteral( "ref_id" ), QStringLiteral( "asset-1" ) );
    entries.append( entry );
    json.insert( QStringLiteral( "entries" ), entries );

    // Unknown field: the contract says tolerated.
    json.insert( QStringLiteral( "vendor_extension_future" ), QStringLiteral( "ignored" ) );
    return json;
}

QJsonDocument toJsonDoc( const std::string &bytes )
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray( bytes.data(), static_cast<int>( bytes.size() ) ), &error );
    return doc;
}
} // namespace

TEST_CASE( "dataset manifest: valid baseline parses and tolerates unknown "
           "fields",
           "[contract][dataset_manifest]" )
{
    const auto result = DatasetManifest::fromJson( validManifest() );
    REQUIRE( result.has_value() );
    REQUIRE( result->datasetId() == QStringLiteral( "ds-fuzz-1" ) );
    REQUIRE( result->versionId() == QStringLiteral( "ver-1" ) );
}

TEST_CASE( "dataset manifest fuzz: mutated payloads never crash and never "
           "parse ambiguously",
           "[contract][fuzz][dataset_manifest]" )
{
    const QJsonDocument seedDoc{ validManifest() };
    const QByteArray seedBytes = seedDoc.toJson( QJsonDocument::Compact );
    const std::string seed( seedBytes.constData(),
                            static_cast<size_t>( seedBytes.size() ) );

    for ( const uint64_t seedRnd : { 7ull, 777ull } )
    {
        BoundedRandom random( seedRnd );
        for ( int i = 0; i < 500; ++i )
        {
            const std::string mutated = random.mutate( seed, 12 );
            if ( mutated.size() > 8192 )
                continue; // hard cap: fuzz stays bounded
            const QJsonDocument doc = toJsonDoc( mutated );
            if ( doc.isObject() )
            {
                // Total on any bounded JSON object: valid XOR typed failure.
                const auto result = DatasetManifest::fromJson( doc.object() );
                if ( result.has_value() )
                    REQUIRE( result->datasetId().isEmpty() == false );
                else
                    REQUIRE_FALSE( result.diagnostics().isEmpty() );
            }
            // Non-object payloads are outside the parse surface (documented
            // QJsonObject entry) — nothing to assert beyond not crashing.
        }
    }
}

TEST_CASE( "dataset manifest: typed negative cases are refused with reasons",
           "[contract][dataset_manifest]" )
{
    // Missing identity.
    QJsonObject noId = validManifest();
    noId.remove( QStringLiteral( "dataset_id" ) );
    auto result = DatasetManifest::fromJson( noId );
    REQUIRE_FALSE( result.has_value() );
    REQUIRE_FALSE( result.diagnostics().isEmpty() );

    // Missing version identity.
    QJsonObject noVersion = validManifest();
    noVersion.remove( QStringLiteral( "version_id" ) );
    result = DatasetManifest::fromJson( noVersion );
    REQUIRE_FALSE( result.has_value() );
    REQUIRE_FALSE( result.diagnostics().isEmpty() );

    // Wrong entry kind.
    QJsonObject badEntry = validManifest();
    badEntry[QStringLiteral( "entries" )] = QJsonArray{ QJsonObject{
        { QStringLiteral( "kind" ), QStringLiteral( "banana" ) },
        { QStringLiteral( "ref_id" ), QStringLiteral( "x" ) } } };
    result = DatasetManifest::fromJson( badEntry );
    REQUIRE_FALSE( result.has_value() );
    REQUIRE_FALSE( result.diagnostics().isEmpty() );
}
