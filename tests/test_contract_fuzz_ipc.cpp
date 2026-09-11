// test_contract_fuzz_ipc.cpp — bounded property tests over the worker/plugin
// IPC envelope + split manifest contracts (task D, Verification Platform 8.0).
//
// Contracts under fuzz:
//
//   worker_protocol (v1 frames, header-only, Qt-free):
//     1. parseFrame is TOTAL: never throws for any bounded input; malformed
//        JSON and wrong versions are `false`, never a crash or a partial
//        parse treated as success.
//     2. Frame builders (makeResultFrame/makeErrorFrame/makeReadyFrame)
//        always produce frames their own parser accepts — builder/parser
//        round-trip identity.
//     3. Accessor totality: frameHasCapability/frameErrorCode answer for any
//        frame shape (missing/wrong-typed members included).
//
//   PluginManifest::fromJson (plugin IPC/extension contract):
//     4. Totality: any bounded mutated JSON yields true+filled OR
//        false+diagnostic — never a crash, never a half-filled manifest
//        reported as success.
//
//   SplitConfig/SplitManifest (dataset contract):
//     5. fromJson is total on bounded mutated payloads; validate() is
//        truthful (accepts only the documented ratio/seed rules); a valid
//        config round-trips through toJson/fromJson.
//
// Inputs are deterministic (fixed seeds) and hard-capped (≤ 512 bytes,
// bounded iterations), so runs are reproducible and memory-safe.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_types.h"
#include "dataset/split.h"
#include "runtime/worker/worker_protocol.h"
#include "exprs/plugin_manifest.h"
#include "exprs/plugin_diagnostics.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "support/bounded_fuzz.h"

#include <json/json.h>

#include <string>
#include <vector>

using namespace sicnu::runtime::worker;
using exprs::PluginManifest;
using exprs::PluginDiagnostic;
using sicnu::testing::BoundedRandom;

namespace
{
constexpr size_t kMaxInputBytes = 512;
constexpr int kIterationsPerSeed = 300;

const std::vector<char> kJsonAlphabet = [] {
    std::vector<char> chars;
    for ( char c = 'a'; c <= 'z'; ++c )
        chars.push_back( c );
    for ( char c = 'A'; c <= 'Z'; ++c )
        chars.push_back( c );
    for ( char c = '0'; c <= '9'; ++c )
        chars.push_back( c );
    for ( const char c : std::string_view( "{}[]\"':,.0123456789-e+ \t\n" ) )
        chars.push_back( c );
    return chars;
}();

const std::vector<std::string> kFrameFragments = {
    R"({"v":1,"op":"ready")",  R"({"v":2,"op":"result")",
    R"("jobId":"j-1")",        R"("caps":["structuredErrors"])",
    R"("code":"worker_crash")", R"("payload":{"x":1})",
    "}",                       "null",
    "true",                    "1e999",
};

std::string fuzzJsonInput( BoundedRandom &random )
{
    if ( random.chance( 0.5 ) )
    {
        // Stitch directed frame fragments — hits protocol shapes fast.
        std::string out;
        const int parts = 1 + random.below( 5 );
        for ( int i = 0; i < parts && out.size() < kMaxInputBytes; ++i )
            out += random.pick( kFrameFragments );
        return out;
    }
    return random.mutate( R"({"v":1,"op":"result","jobId":"job-7","payload":{}})",
                          12 );
}

Json::Value parseJsonOr( const std::string &text, bool *ok )
{
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    *ok = reader->parse( text.data(), text.data() + text.size(), &value, &errors );
    return value;
}
} // namespace

TEST_CASE( "worker protocol fuzz: parseFrame is total and version-strict",
           "[contract8][fuzz][ipc]" )
{
    for ( const uint64_t seed : { 1ull, 0xB00B5ull, 0xDEC0DEull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const std::string input = fuzzJsonInput( random );
            REQUIRE( input.size() <= kMaxInputBytes );
            Json::Value frame;
            bool parsed = false;
            REQUIRE_NOTHROW( parsed = parseFrame( input, frame ) );
            if ( parsed )
            {
                // parseFrame == true implies: valid JSON, v==1, op member.
                bool jsonOk = false;
                const Json::Value check = parseJsonOr( input, &jsonOk );
                REQUIRE( jsonOk );
                REQUIRE( check.isMember( "op" ) );
                REQUIRE( check["v"].asInt() == 1 );
            }
        }
    }
}

TEST_CASE( "worker protocol: builders round-trip through their own parser",
           "[contract8][ipc]" )
{
    // Known-answer leg of the same contract: the builders' output is a
    // v=1 frame with the documented members, regardless of payload weirdness
    // (empty strings, control bytes, huge ids stay bounded by caller).
    const std::vector<std::string> weirdIds = {
        "", "normal-id", "with\"quote", "with\\backslash", "line\nbreak",
    };
    for ( const std::string &id : weirdIds )
    {
        Json::Value frame;
        REQUIRE( parseFrame( makeResultFrame( id, "ok", Json::Value( Json::objectValue ) ),
                             frame ) );
        CHECK( frame["op"].asString() == "result" );
        CHECK( frame["v"].asInt() == 1 );
        CHECK( frame["jobId"].asString() == id );

        Json::Value errorFrame;
        REQUIRE( parseFrame( makeErrorFrame( id, "boom" ), errorFrame ) );
        CHECK( errorFrame["op"].asString() == "error" );
        CHECK( frameErrorCode( errorFrame ).empty() ); // no code ⇒ legacy ""
    }

    // Capability accessor totality: non-array caps are simply "absent".
    Json::Value ready;
    REQUIRE( parseFrame( makeReadyFrame( { "structuredErrors" } ), ready ) );
    CHECK( frameHasCapability( ready, "structuredErrors" ) );
    CHECK_FALSE( frameHasCapability( ready, "nonexistent" ) );
    Json::Value objectCaps;
    objectCaps["caps"] = Json::Value( "structuredErrors" ); // string, not array
    CHECK_FALSE( frameHasCapability( objectCaps, "structuredErrors" ) );
}

TEST_CASE( "plugin manifest fuzz: fromJson is total and never half-succeeds",
           "[contract8][fuzz][ipc]" )
{
    const std::string baseline = R"({
        "id": "com.example.plugin",
        "name": "Example",
        "version": "1.2.3",
        "sdk": "8.0",
        "entry": "plugin.js"
    })";

    for ( const uint64_t seed : { 3ull, 777ull, 0xFEEDull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const std::string input = random.mutate( baseline, 10 );
            REQUIRE( input.size() <= kMaxInputBytes );
            bool jsonOk = false;
            const Json::Value json = parseJsonOr( input, &jsonOk );
            if ( !jsonOk )
                continue; // JSON-level garbage is parseFrame's domain, tested above
            PluginManifest manifest;
            PluginDiagnostic diagnostic;
            bool ok = false;
            REQUIRE_NOTHROW( ok = PluginManifest::fromJson( json, manifest, diagnostic ) );
            if ( ok )
            {
                // fromJson is STRUCTURAL (identity validation happens in the
                // plugin validator, not here): success must survive a
                // serialization round-trip coherently.
                PluginManifest reparsed;
                PluginDiagnostic reparseError;
                const Json::Value serialized = manifest.toJson();
                bool ok2 = false;
                REQUIRE_NOTHROW(
                    ok2 = PluginManifest::fromJson( serialized, reparsed, reparseError ) );
                CHECK( ok2 );
                CHECK( reparsed.id == manifest.id );
            }
            else
            {
                // Failure must say why (truthful diagnostics, non-empty).
                CHECK_FALSE( diagnostic.message.empty() );
            }
        }
    }
}

TEST_CASE( "split config fuzz: fromJson/validate are total and truthful",
           "[contract8][fuzz][data]" )
{
    using sicnu::dataset::SplitConfig;

    const char *baselineJson = R"({
        "method": "random",
        "trainRatio": 0.7,
        "validationRatio": 0.15,
        "testRatio": 0.15,
        "seed": 9001
    })";

    for ( const uint64_t seed : { 11ull, 0xC0FFEEull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const std::string input = random.mutate( baselineJson, 8 );
            REQUIRE( input.size() <= kMaxInputBytes );
            // Qt-JSON parse (the config reader's own document family).
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(
                QByteArray::fromStdString( input ), &parseError );
            if ( parseError.error != QJsonParseError::NoError || !document.isObject() )
                continue;

            // Round A: the config reader is total over bounded JSON objects.
            REQUIRE_NOTHROW( ( void ) SplitConfig::fromJson( document.object() ) );
        }
    }

    // Truthful validation (fromJson validates internally): ratios must sum
    // to ~1, a spatial block needs block sizes — with the REAL persisted
    // snake_case keys (train_ratio / seed_hex / block_size_x, …).
    {
        QJsonObject object;
        object.insert( "method", QStringLiteral( "random" ) );
        object.insert( "train_ratio", 0.6 );
        object.insert( "validation_ratio", 0.3 );
        object.insert( "test_ratio", 0.3 ); // sums to 1.2 → refused at parse
        const auto parsed = SplitConfig::fromJson( object );
        CHECK_FALSE( parsed.has_value() );
        CHECK_FALSE( parsed.diagnostics().isEmpty() );
    }
    {
        QJsonObject object;
        object.insert( "method", QStringLiteral( "spatial_block" ) );
        object.insert( "train_ratio", 0.7 );
        object.insert( "validation_ratio", 0.15 );
        object.insert( "test_ratio", 0.15 );
        // No block_size_x/Y → the method-specific requirement is refused.
        const auto parsed = SplitConfig::fromJson( object );
        CHECK_FALSE( parsed.has_value() );
    }
    {
        // The valid baseline round-trips through toJson/fromJson unchanged
        // (documents the persisted contract other tracks consume).
        QJsonObject object;
        object.insert( "method", QStringLiteral( "random" ) );
        object.insert( "train_ratio", 0.7 );
        object.insert( "validation_ratio", 0.15 );
        object.insert( "test_ratio", 0.15 );
        object.insert( "seed_hex", QStringLiteral( "2317" ) );
        const auto parsed = SplitConfig::fromJson( object );
        REQUIRE( parsed.has_value() );
        CHECK( parsed.value().validate().has_value() );
        CHECK( parsed.value().seed == 0x2317 );
        const QJsonDocument document( parsed.value().toJson() );
        const auto reparsed = SplitConfig::fromJson( document.object() );
        REQUIRE( reparsed.has_value() );
        CHECK( reparsed.value() == parsed.value() );
    }
}
