// tests/test_verifier_packs.cpp
//
// Unified Scientific Verifier (ADR 0172) — Slice E: pack strict serde,
// budgets and deterministic composition.
//
// Light target: links sicnu_verifier + Catch2 + jsoncpp only.

#include <json/json.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "verify/verify_pack.h"
#include "verify/verify_types.h"

using namespace sicnu::verify;

namespace
{

VerificationSpec makeSpec( const std::string &id, const std::string &scope = "node" )
{
    VerificationSpec spec;
    spec.specId = id;
    spec.scope = scope;
    Json::Value params( Json::objectValue );
    params["metric"] = "m_" + id;
    params["max"] = 1.0;
    VerificationCheckSpec check;
    check.checkId = "c_" + id;
    check.kind = "metric.range";
    check.params = params;
    spec.checks.push_back( check );
    return spec;
}

VerifierPack makePack( const std::string &id, std::vector<VerificationSpec> specs )
{
    VerifierPack pack;
    pack.packId = id;
    pack.specs = std::move( specs );
    return pack;
}

} // namespace

TEST_CASE( "packs validate structure and budgets", "[verify][packs][E]" )
{
    VerifierPack pack = makePack( "pack.optical", { makeSpec( "spec.ndvi" ) } );
    REQUIRE( validatePack( pack ).empty() );

    SECTION( "empty pack id" )
    {
        pack.packId = "";
        REQUIRE_FALSE( validatePack( pack ).empty() );
    }
    SECTION( "a pack with no specs is vacuous" )
    {
        pack.specs.clear();
        REQUIRE_FALSE( validatePack( pack ).empty() );
    }
    SECTION( "duplicate spec ids inside one pack" )
    {
        pack.specs.push_back( makeSpec( "spec.ndvi" ) );
        const std::vector<std::string> errors = validatePack( pack );
        bool duplicateNamed = false;
        for ( const std::string &error : errors )
            if ( error.find( "duplicate specId" ) != std::string::npos )
                duplicateNamed = true;
        REQUIRE( duplicateNamed );
    }
    SECTION( "invalid inner spec surfaces its reason" )
    {
        pack.specs[0].scope = "galaxy";
        REQUIRE_FALSE( validatePack( pack ).empty() );
    }
    SECTION( "budget: kMaxSpecsPerPack is refused" )
    {
        std::vector<VerificationSpec> specs;
        for ( std::size_t index = 0; index <= kMaxSpecsPerPack; ++index )
            specs.push_back( makeSpec( "spec." + std::to_string( index ) ) );
        VerifierPack oversized = makePack( "pack.big", std::move( specs ) );
        const std::vector<std::string> errors = validatePack( oversized );
        REQUIRE_FALSE( errors.empty() );
        REQUIRE( errors.front().find( "too many specs" ) != std::string::npos );
    }
}

TEST_CASE( "pack serde is strict and round-trips", "[verify][packs][E]" )
{
    const VerifierPack pack = makePack( "pack.optical", { makeSpec( "spec.ndvi" ), makeSpec( "spec.water", "task" ) } );
    const Json::Value doc = packToJson( pack );
    REQUIRE( doc["schema"].asString() == kPackSchema );

    VerifierPack parsed;
    std::string error;
    REQUIRE( packFromJson( doc, parsed, error ) );
    REQUIRE( parsed.packId == pack.packId );
    REQUIRE( parsed.specs.size() == 2 );
    REQUIRE( packDigest( pack ) == packDigest( parsed ) );

    SECTION( "unknown top-level field refused" )
    {
        Json::Value hostile = doc;
        hostile["extra"] = true;
        REQUIRE_FALSE( packFromJson( hostile, parsed, error ) );
        REQUIRE( error.find( "unknown pack field" ) != std::string::npos );
    }
    SECTION( "wrong schema marker refused" )
    {
        Json::Value hostile = doc;
        hostile["schema"] = "sicnu.verification.pack/2";
        REQUIRE_FALSE( packFromJson( hostile, parsed, error ) );
    }
    SECTION( "duplicate spec ids in the document refused" )
    {
        Json::Value hostile = doc;
        hostile["specs"][1] = hostile["specs"][0];
        REQUIRE_FALSE( packFromJson( hostile, parsed, error ) );
        REQUIRE( error.find( "duplicate specId" ) != std::string::npos );
    }
    SECTION( "hostile inner spec refused with its reason" )
    {
        Json::Value hostile = doc;
        hostile["specs"][0]["params"] = "not-an-object";
        REQUIRE_FALSE( packFromJson( hostile, parsed, error ) );
    }
}

TEST_CASE( "parsePack bounds hostile JSON depth", "[verify][packs][E]" )
{
    // A 20k-deep nesting bomb inside a pack document: the bounded CharReader
    // refuses it as a typed parse error instead of exhausting the stack.
    const std::string bomb = "{\"schema\":\"sicnu.verification.pack/1\",\"packId\":\"p\","
                             "\"specs\":" + std::string( 20000, '[' );
    VerifierPack parsed;
    std::string error;
    std::vector<std::string> validationErrors;
    REQUIRE_FALSE( parsePack( bomb, parsed, error, validationErrors ) );
    REQUIRE( error.find( "invalid JSON" ) != std::string::npos );
}

TEST_CASE( "parsePack separates schema rejection from structural rejection",
           "[verify][packs][E]" )
{
    VerifierPack parsed;
    std::string error;
    std::vector<std::string> validationErrors;

    SECTION( "schema-shaped but structurally empty" )
    {
        const std::string text = R"({"schema":"sicnu.verification.pack/1","packId":"p","specs":[]})";
        REQUIRE_FALSE( parsePack( text, parsed, error, validationErrors ) );
        REQUIRE( error == "pack rejected by structural validation" );
        REQUIRE_FALSE( validationErrors.empty() );
    }
}

TEST_CASE( "composePacks merges deterministically and refuses conflicts",
           "[verify][packs][E]" )
{
    const VerifierPack first = makePack( "pack.a", { makeSpec( "spec.ndvi" ), makeSpec( "spec.b1" ) } );
    const VerifierPack second = makePack( "pack.b", { makeSpec( "spec.water" ), makeSpec( "spec.b1" ) } );
    const VerifierPack third = makePack( "pack.c", { makeSpec( "spec.dem" ) } );

    VerifierPack composed;
    std::string error;

    SECTION( "specId conflict across parts is refused, naming both" )
    {
        REQUIRE_FALSE( composePacks( "pack.composed", { first, second }, composed, error ) );
        REQUIRE( error.find( "spec.b1" ) != std::string::npos );
        REQUIRE( error.find( "pack.a" ) != std::string::npos );
        REQUIRE( error.find( "pack.b" ) != std::string::npos );
    }
    SECTION( "the same pack listed twice conflicts with itself" )
    {
        REQUIRE_FALSE( composePacks( "pack.composed", { third, third }, composed, error ) );
        REQUIRE( error.find( "specId conflict" ) != std::string::npos );
    }
    SECTION( "clean merge is order-independent and sorted" )
    {
        REQUIRE( composePacks( "pack.composed", { first, third }, composed, error ) );
        VerifierPack reversed;
        REQUIRE( composePacks( "pack.composed", { third, first }, reversed, error ) );
        REQUIRE( composed.specs.size() == 3 );
        REQUIRE( packDigest( composed ) == packDigest( reversed ) );
        REQUIRE( composed.specs[0].specId == "spec.b1" );
        REQUIRE( composed.specs[1].specId == "spec.dem" );
        REQUIRE( composed.specs[2].specId == "spec.ndvi" );
    }
    SECTION( "composing zero packs forges nothing" )
    {
        REQUIRE_FALSE( composePacks( "pack.composed", {}, composed, error ) );
    }
    SECTION( "empty composed id refused" )
    {
        REQUIRE_FALSE( composePacks( "", { third }, composed, error ) );
    }
}
