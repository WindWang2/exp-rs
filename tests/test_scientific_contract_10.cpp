/***************************************************************************
 * test_scientific_contract_10.cpp — Scientific Contract Registry gates
 *
 * Platform 10.0: the registry in src/contracts/scientific_contract.* is the
 * FIRST authority for the scientific dimensions that had none (numeric
 * domain, scale/offset, NoData semantics, categorical encoding, time
 * alignment, wavelength requirements, seed policy, cancellation
 * granularity, atomic publication, provenance). These gates keep it
 * complete, well-formed and honest:
 *
 *   1. completeness — live registry rs: ids == registry table keys
 *  2. well-formedness — every record inside the closed vocabularies with
 *     an evidence anchor
 *   3. round-trip — canonical JSON survives a parse
 *   4. known answers — pinned records for the semantically load-bearing
 *     operators (the values review and fixes depend on)
 *   5. refusal codes — every declared code exists in the live ErrorCode
 *     taxonomy
 *
 * Links the live operator registry (initBuiltinRsOperators), like
 * benchmark_contract9 — a snapshot-based gate would not catch an operator
 * registered without a contract.
 ***************************************************************************/
#include "contracts/scientific_contract.h"

#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace sicnu::contracts;

namespace
{
std::set<std::string> liveRsOperatorIds()
{
    sicnu::operators::rs::initBuiltinRsOperators();
    std::set<std::string> ids;
    for ( const std::string &id : sicnu::operators::RSOperatorRegistry::instance().operatorNames() )
        if ( id.rfind( "rs:", 0 ) == 0 )
            ids.insert( id );
    return ids;
}
} // namespace

TEST_CASE( "scientific contracts cover the live registry exactly", "[contracts][science10]" )
{
    const auto live = liveRsOperatorIds();
    const auto &table = scientificContracts();

    std::set<std::string> declared;
    for ( const auto &[id, contract] : table )
    {
        ( void )contract;
        declared.insert( id );
    }

    CHECK_FALSE( live.empty() );

    std::vector<std::string> undeclared;
    std::set_difference( live.begin(), live.end(), declared.begin(), declared.end(),
                         std::back_inserter( undeclared ) );
    for ( const std::string &id : undeclared )
        FAIL( "rs: operator '" + id
              + "' has no scientific contract — add a record to "
                "src/contracts/scientific_contract.cpp (family template + overrides)" );

    std::vector<std::string> phantom;
    std::set_difference( declared.begin(), declared.end(), live.begin(), live.end(),
                         std::back_inserter( phantom ) );
    for ( const std::string &id : phantom )
        FAIL( "scientific contract '" + id
              + "' names no registered operator — remove it or fix the id" );
}

TEST_CASE( "every scientific contract is inside the closed vocabularies and anchored",
           "[contracts][science10]" )
{
    for ( const auto &[id, contract] : scientificContracts() )
    {
        ( void )id;
        const std::vector<std::string> issues = validateScientificContract( contract );
        if ( !issues.empty() )
        {
            for ( const std::string &issue : issues )
                FAIL( issue );
        }
    }
    // The vocabulary validation must actually reject garbage — the red
    // direction of the gate, proven not assumed.
    ScientificContract broken = scientificContracts().at( "rs:ndvi" );
    broken.inputDomain = "magical_unicorn_units";
    broken.evidence = "";
    const std::vector<std::string> issues = validateScientificContract( broken );
    REQUIRE( issues.size() == 2 );
}

TEST_CASE( "scientific contracts round-trip through canonical JSON", "[contracts][science10]" )
{
    const Json::Value doc = scientificContractsToJson();
    REQUIRE( doc["schema"].asString() == "exp.scientific_contract.v1" );
    const Json::Value &array = doc["contracts"];
    REQUIRE( array.size() == scientificContracts().size() );

    for ( const Json::Value &entry : array )
    {
        ScientificContract parsed;
        std::string error;
        REQUIRE( scientificContractFromJson( entry, parsed, error ) );
        const ScientificContract &original = *findScientificContract( parsed.operatorId );
        CHECK( parsed.inputDomain == original.inputDomain );
        CHECK( parsed.outputDomain == original.outputDomain );
        CHECK( parsed.noDataPolicy == original.noDataPolicy );
        CHECK( parsed.cancellationGranularity == original.cancellationGranularity );
        CHECK( parsed.atomicPublication == original.atomicPublication );
        CHECK( parsed.refusalCodes == original.refusalCodes );
    }

    // Schema mismatch is refused, not defaulted.
    Json::Value garbage( Json::arrayValue );
    ScientificContract parsed;
    std::string error;
    CHECK_FALSE( scientificContractFromJson( garbage, parsed, error ) );
}

TEST_CASE( "semantically load-bearing contracts are pinned (known answers)",
           "[contracts][science10]" )
{
    // F-OPS-3: the quality gate fails closed.
    const ScientificContract *qa = findScientificContract( "rs:qa_mask" );
    REQUIRE( qa );
    CHECK( qa->noDataPolicy == "fail_closed" );
    CHECK( qa->outputDomain == "mask" );
    CHECK( qa->cancellationGranularity == "row_block_level" );

    // Spectral resampling is meaningless without SRF/center wavelengths.
    CHECK( findScientificContract( "rs:spectral_resample" )->wavelengthPolicy == "srf_or_center" );

    // The kmeans record is honest about its nondeterminism: plain kmeans
    // cannot claim a seed policy (cv::kmeans best-of-3 on an advancing
    // thread-local RNG); the deterministic variant is isodata.
    const ScientificContract *kmeans = findScientificContract( "rs:kmeans_classification" );
    REQUIRE( kmeans );
    CHECK( kmeans->seedPolicy == "none" );
    CHECK( kmeans->note.find( "isodata" ) != std::string::npos );

    // SAR calibration declares its radiometric domain and metadata-driven
    // scale/offset.
    const ScientificContract *cal = findScientificContract( "rs:sar_calibrate" );
    REQUIRE( cal );
    CHECK( cal->outputDomain == "sigma0" );
    CHECK( cal->scaleOffset == "product_metadata" );

    // The labels encoding rule pinned by F-OPS-1's fix.
    const ScientificContract *infer = findScientificContract( "rs:infer" );
    REQUIRE( infer );
    CHECK( infer->atomicPublication == "staged_rename" );
    CHECK( infer->cancellationGranularity == "tile_level" );
}

TEST_CASE( "declared refusal codes exist in the live error taxonomy",
           "[contracts][science10]" )
{
    // Every declared code must name a real ErrorCode enum member — a typo
    // would make the refusal contract unmatchable for callers. The name set
    // is built from the enum members via errorCodeToString (the
    // enum<->toString switch itself is enum-drift-guarded by
    // test_diagnostics_contract_9); a member missing from this initializer
    // only weakens this gate, never false-fails it.
    const std::set<std::string> taxonomyNames = [] {
        std::set<std::string> names;
        for ( int value :
              { 0, 1000, 1001, 1002, 1003, 1004, 2000, 2001, 2002, 2003, 2004, 3000, 3001, 3002,
                3003, 3004, 3006, 3007, 4000, 4001, 4002, 4100, 4101, 4102, 9999 } )
            names.insert( sicnu::operators::errorCodeToString(
              static_cast<sicnu::operators::ErrorCode>( value ) ) );
        return names;
    }();

    for ( const auto &[id, contract] : scientificContracts() )
    {
        ( void )id;
        for ( const std::string &code : contract.refusalCodes )
        {
            INFO( code );
            CHECK( taxonomyNames.count( code ) == 1 );
        }
    }
}
