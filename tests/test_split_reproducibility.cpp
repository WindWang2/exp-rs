// test_split_reproducibility.cpp — Track 13 R4 WP-B: split reproducibility
// under the pinned random stack.
//
// Independent authorities (never the code-under-test's own logic):
//   - SplitMix64: the public-domain reference algorithm vectors;
//   - Pcg32 + seedFor/hashSeed: vectors computed off-line from the algorithm
//     pinned in deterministic_random.h (the header text IS the contract);
//   - double-run digest: two SplitEngine::generate runs over identical
//     (config, inputs) must produce byte-identical canonical manifests;
//   - the seed contract: "0 is a legal seed, absence is not".
#include <catch2/catch_test_macros.hpp>

#include "dataset/deterministic_random.h"
#include "dataset/dataset_fingerprint.h"
#include "dataset/split.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace sicnu::dataset;

namespace
{

QVector<SplitInput> makeInputs( int count )
{
    QVector<SplitInput> inputs;
    inputs.reserve( count );
    for ( int i = 0; i < count; ++i )
    {
        SplitInput input;
        input.sampleId = QStringLiteral( "sample-%1" ).arg( i, 4, 10, QLatin1Char( '0' ) );
        input.groupId = QStringLiteral( "group-%1" ).arg( i % 7 );
        input.classCode = QStringLiteral( "class-%1" ).arg( i % 3 );
        input.timeMs = 1'700'000'000'000LL + i * 86'400'000LL;
        input.validBounds = true;
        input.minX = 116.0 + ( i % 5 ) * 0.01;
        input.minY = 39.0 + ( i / 5 ) * 0.01;
        input.maxX = input.minX + 0.005;
        input.maxY = input.minY + 0.005;
        input.sceneId = QStringLiteral( "scene-%1" ).arg( i % 4 );
        input.year = 2024 + ( i % 2 );
        inputs.append( input );
    }
    return inputs;
}

SplitConfig randomConfig( quint64 seed )
{
    SplitConfig config;
    config.method = SplitMethod::Random;
    config.seed = seed;
    return config;
}

} // namespace

TEST_CASE( "SplitMix64 matches the public-domain reference vectors",
           "[dataset][split][random][kav]" )
{
    // Reference: seed 0 → 0xe220a8397b1dcdaf, 0x6e789e6aa1b965f4, …
    // (computed independently from the pinned algorithm; these vectors are
    // the classic splitmix64 outputs, stable across every platform).
    SplitMix64 generator( 0 );
    CHECK( generator.next() == 0xe220a8397b1dcdafull );
    CHECK( generator.next() == 0x6e789e6aa1b965f4ull );
    CHECK( generator.next() == 0x06c45d188009454full );
    CHECK( generator.next() == 0xf88bb8a8724c81ecull );

    SplitMix64 seeded( 0xDEADBEEFull );
    CHECK( seeded.next() == 0x4adfb90f68c9eb9bull );
    CHECK( seeded.next() == 0xde586a3141a10922ull );
    CHECK( seeded.next() == 0x021fbc2f8e1cfc1dull );
}

TEST_CASE( "Pcg32 output sequence matches the pinned algorithm vectors",
           "[dataset][split][random][kav]" )
{
    // Off-line vectors from the algorithm text in deterministic_random.h:
    // SplitMix64 seeding, LCG step 6364136223846793005, xorshift output mix.
    Pcg32 generator( 0 );
    CHECK( generator.nextU32() == 775308154u );
    CHECK( generator.nextU32() == 3362502810u );
    CHECK( generator.nextU32() == 1585660908u );
    CHECK( generator.nextU32() == 1026247668u );
    CHECK( generator.nextU32() == 3064539364u );
    CHECK( generator.nextU32() == 2966953711u );

    Pcg32 seeded( 42 );
    CHECK( seeded.nextU32() == 2807823541u );
    CHECK( seeded.nextU32() == 4270804824u );
    CHECK( seeded.nextU32() == 1922010024u );
    CHECK( seeded.nextU32() == 3725733894u );

    // Bounded draws stay in range and consume the same fixed stream.
    Pcg32 bounded( 7 );
    for ( int i = 0; i < 32; ++i )
        CHECK( bounded.nextBounded( 10 ) < 10u );
}

TEST_CASE( "seed derivation vectors: hashSeed/seedFor are pinned and purpose-independent",
           "[dataset][split][random][kav]" )
{
    // Off-line vectors: FNV-1a over UTF-8 + SplitMix64 finalizer, then one
    // SplitMix64 round over (purposeHash ^ rootSeed).
    CHECK( DeterministicRandom::hashSeed( QStringLiteral( "split" ) ) ==
           0xeb20f35369b6fe93ull );
    CHECK( DeterministicRandom::hashSeed( QStringLiteral( "patch" ) ) ==
           0xc6d76581fe23f1bcull );
    CHECK( DeterministicRandom::seedFor( 0, QStringLiteral( "split" ) ) ==
           0xb3de20b6ecdfcef6ull );
    CHECK( DeterministicRandom::seedFor( 0, QStringLiteral( "patch.random" ) ) ==
           0x1cc45c15f308efd5ull );
    CHECK( DeterministicRandom::seedFor( 7, QStringLiteral( "split" ) ) ==
           0xd8ee18c66db7ef35ull );

    // Namespacing discipline: adding "split.x" never perturbs "split" —
    // distinct purposes derive distinct seeds, equal purposes agree.
    CHECK( DeterministicRandom::seedFor( 7, QStringLiteral( "split" ) ) !=
           DeterministicRandom::seedFor( 7, QStringLiteral( "split.x" ) ) );
    CHECK( DeterministicRandom::seedFor( 7, QStringLiteral( "split" ) ) ==
           DeterministicRandom::seedFor( 7, QStringLiteral( "split" ) ) );
}

TEST_CASE( "split generation double-run produces byte-identical manifests",
           "[dataset][split][determinism]" )
{
    const QVector<SplitInput> inputs = makeInputs( 200 );

    // Plain random method: same seed, same inputs → identical manifest,
    // identical canonical fingerprint.
    const auto first = SplitEngine::generate( randomConfig( 42 ), QStringLiteral( "version-a" ),
                                              inputs );
    const auto second = SplitEngine::generate( randomConfig( 42 ), QStringLiteral( "version-a" ),
                                               inputs );
    REQUIRE( first.has_value() );
    REQUIRE( second.has_value() );
    // Whole-manifest equality would carry generate()'s wall-clock
    // createdAtUtc — the deterministic surface is assignments + fingerprint.
    CHECK( first.value().assignments() == second.value().assignments() );
    CHECK( splitManifestFingerprint( first.value() ) ==
           splitManifestFingerprint( second.value() ) );

    // A different seed moves the assignment (sensitivity, not just identity).
    const auto otherSeed = SplitEngine::generate( randomConfig( 43 ), QStringLiteral( "version-a" ),
                                                  inputs );
    REQUIRE( otherSeed.has_value() );
    CHECK( splitManifestFingerprint( first.value() ) !=
           splitManifestFingerprint( otherSeed.value() ) );

    // Fold method: per-fold membership replays exactly, and materialized
    // folds double-run identically.
    SplitConfig foldConfig = randomConfig( 42 );
    foldConfig.method = SplitMethod::KFold;
    foldConfig.foldCount = 5;
    const auto foldA = SplitEngine::generate( foldConfig, QStringLiteral( "version-a" ), inputs );
    const auto foldB = SplitEngine::generate( foldConfig, QStringLiteral( "version-a" ), inputs );
    REQUIRE( foldA.has_value() );
    REQUIRE( foldB.has_value() );
    REQUIRE( foldA.value().assignments().size() == foldB.value().assignments().size() );
    for ( int i = 0; i < foldA.value().assignments().size(); ++i )
    {
        CHECK( foldA.value().assignments().at( i ).sampleId ==
               foldB.value().assignments().at( i ).sampleId );
        CHECK( foldA.value().assignments().at( i ).fold ==
               foldB.value().assignments().at( i ).fold );
    }
    const auto materialA = foldA.value().materializeFold( 2 );
    const auto materialB = foldB.value().materializeFold( 2 );
    REQUIRE( materialA.has_value() );
    REQUIRE( materialB.has_value() );
    CHECK( materialA.value() == materialB.value() );
}

TEST_CASE( "seed 0 is a legal seed and replays like any other",
           "[dataset][split][determinism][seed]" )
{
    const QVector<SplitInput> inputs = makeInputs( 100 );
    const auto zeroA = SplitEngine::generate( randomConfig( 0 ), QStringLiteral( "version-a" ),
                                              inputs );
    const auto zeroB = SplitEngine::generate( randomConfig( 0 ), QStringLiteral( "version-a" ),
                                              inputs );
    REQUIRE( zeroA.has_value() );
    REQUIRE( zeroB.has_value() );
    CHECK( splitManifestFingerprint( zeroA.value() ) ==
           splitManifestFingerprint( zeroB.value() ) );

    // seed_hex "0" round-trips as an explicit legal seed.
    const QJsonObject json = randomConfig( 0 ).toJson();
    REQUIRE( json.value( QStringLiteral( "seed_hex" ) ).toString() ==
             QStringLiteral( "0" ) );
    const auto parsed = SplitConfig::fromJson( json );
    REQUIRE( parsed.has_value() );
    CHECK( parsed.value().seed == 0ull );
}

TEST_CASE( "a config JSON without seed_hex is refused, never silently seeded",
           "[dataset][split][determinism][seed]" )
{
    // Contract (split.h): 0 is a legal seed, ABSENCE is not. A config JSON
    // without the seed key must fail with a typed error — falling back to 0
    // would silently mint degenerate reproducible splits from an operator
    // mistake, and would make seed-0 splits impossible to distinguish from
    // forgotten ones.
    QJsonObject missing = randomConfig( 7 ).toJson();
    missing.remove( QStringLiteral( "seed_hex" ) );
    const auto parsed = SplitConfig::fromJson( missing );
    REQUIRE( !parsed.has_value() );
    REQUIRE( !parsed.diagnostics().isEmpty() );
    CHECK( parsed.diagnostics().first().code ==
           QStringLiteral( "dataset.split_invalid" ) );
    CHECK( parsed.diagnostics().first().message.contains( QStringLiteral( "seed" ) ) );

    // A malformed seed_hex is still refused (existing behavior, now on the
    // same path as absence).
    QJsonObject malformed = randomConfig( 7 ).toJson();
    malformed.insert( QStringLiteral( "seed_hex" ), QStringLiteral( "nothex" ) );
    CHECK( !SplitConfig::fromJson( malformed ).has_value() );

    // Wrong TYPES must refuse as well: a number/null seed_hex would
    // otherwise skip the parse and silently leave the default 0 in place.
    QJsonObject numeric = randomConfig( 7 ).toJson();
    numeric.insert( QStringLiteral( "seed_hex" ), 5 );
    const auto numericParsed = SplitConfig::fromJson( numeric );
    REQUIRE( !numericParsed.has_value() );
    CHECK( numericParsed.diagnostics().first().message.contains(
        QStringLiteral( "seed" ) ) );
    QJsonObject nulled = randomConfig( 7 ).toJson();
    nulled.insert( QStringLiteral( "seed_hex" ), QJsonValue() );
    CHECK( !SplitConfig::fromJson( nulled ).has_value() );

    // Fold manifests deserialize through the same gate: a manifest JSON
    // whose config lacks the seed is refused.
    const QVector<SplitInput> inputs = makeInputs( 40 );
    const auto manifest = SplitEngine::generate( randomConfig( 9 ), QStringLiteral( "version-a" ),
                                                 inputs );
    REQUIRE( manifest.has_value() );
    QJsonObject manifestJson = manifest.value().toJson();
    QJsonObject configJson = manifestJson.value( QStringLiteral( "config" ) ).toObject();
    configJson.remove( QStringLiteral( "seed_hex" ) );
    manifestJson.insert( QStringLiteral( "config" ), configJson );
    const auto reparsed = SplitManifest::fromJson( manifestJson );
    REQUIRE( !reparsed.has_value() );
}

TEST_CASE( "split manifest fingerprint covers content, not the fingerprint field",
           "[dataset][split][determinism]" )
{
    const QVector<SplitInput> inputs = makeInputs( 60 );
    const auto manifest = SplitEngine::generate( randomConfig( 11 ), QStringLiteral( "version-a" ),
                                                 inputs );
    REQUIRE( manifest.has_value() );
    SplitManifest stamped = manifest.value();
    const QString fingerprint = splitManifestFingerprint( stamped );
    CHECK( fingerprint.size() == 64 );

    stamped.setFingerprint( fingerprint );
    QJsonObject json = stamped.toJson();
    REQUIRE( json.value( QStringLiteral( "fingerprint" ) ).toString() == fingerprint );
    CHECK( splitManifestFingerprint( stamped ) == fingerprint );

    // Any content move (a reassignment) moves the fingerprint.
    SplitManifest moved = stamped;
    moved.assignments()[0].sampleId = QStringLiteral( "sample-xxxx" );
    CHECK( splitManifestFingerprint( moved ) != fingerprint );
}
