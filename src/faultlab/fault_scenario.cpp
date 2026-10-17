// fault_scenario.cpp — scenario document loading/validation (the
// `sicnu.lab.faults/1` schema gate). Fail-closed: a foreign schema version,
// an unknown fault family, an unknown relation or a malformed field each
// produce a typed `faultlab.*` diagnostic; nothing is silently defaulted
// into validity. Declarations live in fault_types.h.
#include "fault_types.h"

#include "fault_registry.h"

#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>

#include <limits>

namespace sicnu::faultlab
{

namespace
{

using Json::Value;

/// Hard ceiling for one scenario document (4 MiB). A scenario is a small
/// teaching artifact; anything larger is refused before a byte is parsed.
inline constexpr std::uint64_t kMaxScenarioFileBytes = 4ull * 1024 * 1024;

bool isNonEmptyString( const Value &doc, const char *key )
{
    return doc.isMember( key ) && doc[key].isString() && !doc[key].asString().empty();
}

bool matchesPattern( const std::string &text, const std::string &prefix, bool digitsAfterPrefix )
{
    if ( text.rfind( prefix, 0 ) != 0 || text.size() == prefix.size() )
    {
        return false;
    }
    if ( !digitsAfterPrefix )
    {
        return true;
    }
    for ( std::size_t i = prefix.size(); i < text.size(); ++i )
    {
        const char c = text[i];
        const bool ok = ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_';
        if ( !ok )
        {
            return false;
        }
    }
    return true;
}

bool isLowerSnake( const std::string &text )
{
    if ( text.empty() )
    {
        return false;
    }
    for ( const char c : text )
    {
        const bool ok = ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_';
        if ( !ok )
        {
            return false;
        }
    }
    return true;
}

/// Reads a required uint32 seed; missing/non-uint/negative are field errors.
bool readSeed( const Value &parent, const char *key, std::uint32_t &out, std::string &error )
{
    if ( !parent.isMember( key ) )
    {
        error = std::string( "missing seed field '" ) + key + "'";
        return false;
    }
    const Value &seed = parent[key];
    if ( seed.isUInt() )
    {
        // asUInt64() is exact regardless of jsoncpp version; the bound is
        // re-checked here so the width contract never leans on isUInt()'s
        // bound-checking behavior of a particular vendored jsoncpp.
        const Json::Value::UInt64 wide = seed.asUInt64();
        if ( wide > static_cast<Json::Value::UInt64>( std::numeric_limits<std::uint32_t>::max() ) )
        {
            error = std::string( "seed field '" ) + key
                    + "' must be an unsigned 32-bit integer";
            return false;
        }
        out = static_cast<std::uint32_t>( wide );
        return true;
    }
    // The width bound is the contract: a seed above UINT32_MAX is a field
    // error, never a silent truncation — a truncated seed would replay the
    // lab with a different noise stream while claiming the scenario.
    if ( seed.isIntegral() && seed.isInt64() && seed.asInt64() >= 0
         && seed.asInt64() <= static_cast<std::int64_t>( std::numeric_limits<std::uint32_t>::max() ) )
    {
        out = static_cast<std::uint32_t>( seed.asInt64() );
        return true;
    }
    error = std::string( "seed field '" ) + key + "' must be an unsigned 32-bit integer";
    return false;
}

FaultResult<FaultScenario> fieldError( const std::string &message )
{
    return makeError<FaultScenario>( "faultlab.scenario_field", message );
}

} // namespace

FaultResult<FaultScenario> loadFaultScenario( const Value &doc )
{
    if ( !doc.isObject() )
    {
        return makeError<FaultScenario>( "faultlab.scenario_document",
                                         "scenario document must be a JSON object" );
    }

    // --- schema version gate (fail-closed) ---
    if ( !doc.isMember( "schema_version" ) || !doc["schema_version"].isString() )
    {
        return makeError<FaultScenario>( "faultlab.schema_version",
                                         "scenario document is missing schema_version" );
    }
    if ( doc["schema_version"].asString() != kFaultScenarioSchemaId )
    {
        return makeError<FaultScenario>( "faultlab.schema_version",
                                         "unsupported scenario schema_version '" +
                                             doc["schema_version"].asString() +
                                             "'; expected '" + kFaultScenarioSchemaId + "'" );
    }

    FaultScenario scenario;

    if ( !isNonEmptyString( doc, "scenario_id" ) )
    {
        return fieldError( "scenario_id must be a non-empty string" );
    }
    scenario.scenarioId = doc["scenario_id"].asString();
    if ( !matchesPattern( scenario.scenarioId, "fs_", true ) )
    {
        return fieldError( "scenario_id '" + scenario.scenarioId +
                           "' must match ^fs_[a-z0-9_]+$" );
    }

    if ( !isNonEmptyString( doc, "title" ) )
    {
        return fieldError( "title must be a non-empty string" );
    }
    scenario.title = doc["title"].asString();
    if ( doc.isMember( "title_zh" ) && doc["title_zh"].isString() )
    {
        scenario.titleZh = doc["title_zh"].asString();
    }

    // --- fault block ---
    if ( !doc.isMember( "fault" ) || !doc["fault"].isObject() )
    {
        return fieldError( "fault must be an object" );
    }
    const Value &fault = doc["fault"];
    if ( !isNonEmptyString( fault, "family" ) )
    {
        return fieldError( "fault.family must be a non-empty string" );
    }
    scenario.fault.familyId = fault["family"].asString();
    if ( findFaultFamily( scenario.fault.familyId ) == nullptr )
    {
        return makeError<FaultScenario>( "faultlab.fault_unknown_family",
                                         "unknown fault family '" + scenario.fault.familyId + "'" );
    }
    if ( fault.isMember( "params" ) )
    {
        if ( !fault["params"].isObject() )
        {
            return fieldError( "fault.params must be an object" );
        }
        scenario.fault.params = fault["params"];
    }
    std::string seedError;
    if ( !readSeed( fault, "seed", scenario.fault.seed, seedError ) )
    {
        return fieldError( seedError );
    }

    // --- base fixture block ---
    if ( !doc.isMember( "base_fixture" ) || !doc["base_fixture"].isObject() )
    {
        return fieldError( "base_fixture must be an object" );
    }
    const Value &fixture = doc["base_fixture"];
    if ( !isNonEmptyString( fixture, "fixture_id" ) )
    {
        return fieldError( "base_fixture.fixture_id must be a non-empty string" );
    }
    scenario.fixtureId = fixture["fixture_id"].asString();
    if ( !isLowerSnake( scenario.fixtureId ) )
    {
        return fieldError( "base_fixture.fixture_id '" + scenario.fixtureId +
                           "' must be a lowercase snake identifier" );
    }
    if ( fixture.isMember( "params" ) )
    {
        if ( !fixture["params"].isObject() )
        {
            return fieldError( "base_fixture.params must be an object" );
        }
        // makeFixture documents "v1 fixtures take no parameters" and ignores
        // the object entirely — an author declaring {"width": 64} silently
        // got the stock fixture. That is the #1464 accepted-but-inert shape
        // (the transform-level cell_size was closed in R8); the fixture
        // level refuses the same way. The empty object stays legal for
        // schema symmetry.
        if ( !fixture["params"].empty() )
        {
            return fieldError( "base_fixture.params must be empty: v1 fixtures take no "
                               "parameters, declared params would be silently ignored" );
        }
        scenario.fixtureParams = fixture["params"];
    }
    if ( !readSeed( fixture, "seed", scenario.fixtureSeed, seedError ) )
    {
        return fieldError( seedError );
    }

    // --- sandbox block ---
    if ( doc.isMember( "sandbox" ) )
    {
        const Value &sandbox = doc["sandbox"];
        if ( !sandbox.isObject() )
        {
            return fieldError( "sandbox must be an object" );
        }
        if ( sandbox.isMember( "class" ) )
        {
            if ( !sandbox["class"].isString() || sandbox["class"].asString() != "temp_copy" )
            {
                return fieldError( "sandbox.class must be 'temp_copy'" );
            }
            scenario.sandboxClass = sandbox["class"].asString();
        }
        if ( sandbox.isMember( "max_bytes" ) )
        {
            const Value &maxBytes = sandbox["max_bytes"];
            // isUInt64() (not isIntegral()): jsoncpp asUInt64() throws on
            // negative integrals instead of refusing them.
            if ( !maxBytes.isUInt64() || maxBytes.asUInt64() > kFaultLabMaxBytes )
            {
                return fieldError( "sandbox.max_bytes must be an integer <= 64 MiB" );
            }
            scenario.maxBytes = maxBytes.asUInt64();
        }
        if ( sandbox.isMember( "require_source_unchanged" ) )
        {
            if ( !sandbox["require_source_unchanged"].isBool() ||
                 !sandbox["require_source_unchanged"].asBool() )
            {
                return fieldError( "sandbox.require_source_unchanged may not be waived" );
            }
        }
    }

    // --- expected block ---
    if ( !doc.isMember( "expected" ) || !doc["expected"].isObject() )
    {
        return fieldError( "expected must be an object" );
    }
    const Value &expected = doc["expected"];
    if ( !expected.isMember( "observables" ) || !expected["observables"].isArray() ||
         expected["observables"].empty() )
    {
        return fieldError( "expected.observables must be a non-empty array" );
    }
    for ( const auto &entry : expected["observables"] )
    {
        if ( !entry.isObject() || !isNonEmptyString( entry, "id" ) )
        {
            return fieldError( "each expected observable needs a non-empty id" );
        }
        ObservableExpectation expectation;
        expectation.id = entry["id"].asString();
        if ( !entry.isMember( "relation" ) || !entry["relation"].isString() ||
             !parseExpectationRelation( entry["relation"].asString(), expectation.relation ) )
        {
            FaultResult<FaultScenario> result;
            result.ok = false;
            result.diagnostics.push_back(
                FaultDiagnostic{ "faultlab.scenario_relation",
                                 "unknown expectation relation for observable '" + expectation.id +
                                     "'",
                                 FaultSeverity::Error } );
            return result;
        }
        // Operand honesty (#1464 doctrine at the loader door): a PRESENT
        // operand with the wrong JSON type used to be silently skipped
        // ({"relation":"delta_ge","value":"0.5"} graded as delta >= 0 — a
        // vacuous pass on garbage), and jsoncpp parses "1e999" to ±Infinity
        // with NO parse error on 1.9.8 (older readers fail the text), which
        // carried a non-finite operand all the way into the report digest.
        // Present operands are type- and finite-checked; absent operands
        // keep their legacy defaults (the `changed` relation carries none).
        if ( entry.isMember( "value" ) )
        {
            const Value &operand = entry["value"];
            if ( expectation.relation == ExpectationRelation::TruthIs )
            {
                if ( !operand.isBool() )
                {
                    return fieldError( "truth_is expectation for '" + expectation.id +
                                       "' needs a boolean value" );
                }
                expectation.value = operand.asBool() ? 1.0 : 0.0;
            }
            else if ( !operand.isNumeric() || !std::isfinite( operand.asDouble() ) )
            {
                return fieldError( "expectation '" + expectation.id +
                                   "' value must be a finite number" );
            }
            else
            {
                expectation.value = operand.asDouble();
            }
        }
        else if ( expectation.relation == ExpectationRelation::TruthIs )
        {
            return fieldError( "truth_is expectation for '" + expectation.id +
                               "' needs a boolean value" );
        }
        const auto readRangeBound = [&]( const char *boundName, double &bound ) {
            if ( !entry.isMember( boundName ) )
            {
                return true;
            }
            const Value &operand = entry[boundName];
            if ( !operand.isNumeric() || !std::isfinite( operand.asDouble() ) )
            {
                return false;
            }
            bound = operand.asDouble();
            return true;
        };
        if ( !readRangeBound( "range_lo", expectation.rangeLo ) )
        {
            return fieldError( "expectation '" + expectation.id +
                               "' range_lo must be a finite number" );
        }
        if ( !readRangeBound( "range_hi", expectation.rangeHi ) )
        {
            return fieldError( "expectation '" + expectation.id +
                               "' range_hi must be a finite number" );
        }
        if ( expectation.relation == ExpectationRelation::InRange &&
             expectation.rangeLo > expectation.rangeHi )
        {
            return fieldError( "in_range expectation for '" + expectation.id +
                               "' needs range_lo <= range_hi" );
        }
        if ( entry.isMember( "text" ) )
        {
            if ( !entry["text"].isString() )
            {
                return fieldError( "expectation '" + expectation.id + "' text must be a string" );
            }
            expectation.text = entry["text"].asString();
        }
        scenario.expectations.push_back( expectation );
    }

    if ( !expected.isMember( "diagnosis" ) || !expected["diagnosis"].isObject() ||
         !isNonEmptyString( expected["diagnosis"], "signature" ) )
    {
        return fieldError( "expected.diagnosis.signature must be a non-empty string" );
    }
    scenario.expectedDiagnosisSignature = expected["diagnosis"]["signature"].asString();

    if ( expected.isMember( "verifier" ) )
    {
        if ( !expected["verifier"].isObject() )
        {
            return fieldError( "expected.verifier must be an object" );
        }
        scenario.verifier = expected["verifier"];
    }

    // --- learning objective block ---
    if ( !doc.isMember( "learning_objective" ) || !doc["learning_objective"].isObject() )
    {
        return makeError<FaultScenario>( "faultlab.scenario_objective",
                                         "learning_objective must be an object" );
    }
    const Value &objective = doc["learning_objective"];
    if ( !isNonEmptyString( objective, "id" ) )
    {
        return makeError<FaultScenario>( "faultlab.scenario_objective",
                                         "learning_objective.id must be a non-empty string" );
    }
    scenario.learningObjectiveId = objective["id"].asString();
    // The published schema (data/faultlab/faults.schema.json) and every
    // catalog id pin ^LO-[0-9]{2}$ exactly; the old [a-z0-9_]+ suffix let
    // ids like LO-abc drift silently away from the documented grammar the
    // refusal message already claimed (R14-E audit F3).
    const std::string &loId = scenario.learningObjectiveId;
    const bool loIdWellFormed = loId.size() == 5 && loId.rfind( "LO-", 0 ) == 0
                                && std::all_of( loId.begin() + 3, loId.end(),
                                                []( char c ) { return c >= '0' && c <= '9'; } );
    if ( !loIdWellFormed )
    {
        return makeError<FaultScenario>( "faultlab.scenario_objective",
                                         "learning_objective.id '" + scenario.learningObjectiveId +
                                             "' must match ^LO-[0-9]{2}$" );
    }
    if ( !isNonEmptyString( objective, "statement" ) )
    {
        return makeError<FaultScenario>( "faultlab.scenario_objective",
                                         "learning_objective.statement must be a non-empty string" );
    }
    scenario.learningObjective = objective["statement"].asString();
    if ( objective.isMember( "statement_zh" ) && objective["statement_zh"].isString() )
    {
        scenario.learningObjectiveZh = objective["statement_zh"].asString();
    }

    // --- cleanup block ---
    if ( doc.isMember( "cleanup" ) )
    {
        const Value &cleanup = doc["cleanup"];
        if ( !cleanup.isObject() )
        {
            return fieldError( "cleanup must be an object" );
        }
        if ( cleanup.isMember( "required" ) )
        {
            if ( !cleanup["required"].isBool() || !cleanup["required"].asBool() )
            {
                return fieldError( "cleanup.required may not be waived" );
            }
        }
        if ( cleanup.isMember( "verify_no_residue" ) && !cleanup["verify_no_residue"].isBool() )
        {
            return fieldError( "cleanup.verify_no_residue must be a boolean" );
        }
    }

    return makeOk( std::move( scenario ) );
}

FaultResult<FaultScenario> loadFaultScenarioFile( const std::string &path )
{
    std::ifstream stream( path, std::ios::binary );
    if ( !stream )
    {
        return makeError<FaultScenario>( "faultlab.scenario_unreadable",
                                         "cannot open scenario file: " + path );
    }
    // Bound the read before slurping: a scenario is a small teaching
    // artifact, so an oversized file is refused with the existing
    // unreadable vocabulary instead of an unbounded rdbuf copy. The probe is
    // only an early rejection (seekg/tellg vs. the actual read is a TOCTOU
    // gap): enforcement is on bytes actually read — cap+1 sentinel bytes,
    // mirroring verify_adapters/bounded_io.cpp — so a file that grows
    // between probe and read still hits the cap.
    stream.seekg( 0, std::ios::end );
    const std::streamoff length = stream.tellg();
    if ( length < 0 || static_cast<std::uint64_t>( length ) > kMaxScenarioFileBytes )
    {
        return makeError<FaultScenario>( "faultlab.scenario_unreadable",
                                         "scenario file exceeds the 4 MiB teaching-artifact cap: " +
                                             path );
    }
    stream.seekg( 0, std::ios::beg );

    std::string text;
    text.resize( static_cast<std::size_t>( kMaxScenarioFileBytes ) + 1 );
    stream.read( text.data(), static_cast<std::streamsize>( text.size() ) );
    if ( stream.bad() )
    {
        return makeError<FaultScenario>( "faultlab.scenario_unreadable",
                                         "cannot read scenario file: " + path );
    }
    text.resize( static_cast<std::size_t>( stream.gcount() ) );
    if ( text.size() > kMaxScenarioFileBytes )
    {
        return makeError<FaultScenario>( "faultlab.scenario_unreadable",
                                         "scenario file exceeds the 4 MiB teaching-artifact cap: " +
                                             path );
    }

    Json::Value doc;
    Json::CharReaderBuilder builder;
    // Same safe-reader discipline as grader_json / guidance_store: a scenario
    // file is untrusted content. The reader THROWS on a nesting bomb past
    // the stack limit, trailing garbage used to be silently ignored
    // (failIfExtra defaults to false), and no scenario fixture relies on
    // comments — so the bomb arrives as a typed refusal, extra text fails
    // the parse, and allowComments=false matches the grader doctrine. The
    // module contract is never exceptions across the API boundary.
    builder["stackLimit"] = 128;
    builder["allowComments"] = false;
    builder["collectComments"] = false;
    builder["failIfExtra"] = true;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    try
    {
        if ( !reader->parse( text.data(), text.data() + text.size(), &doc, &errors ) )
        {
            return makeError<FaultScenario>( "faultlab.scenario_malformed",
                                             "cannot parse scenario JSON: " + errors );
        }
    }
    catch ( const Json::Exception &exception )
    {
        return makeError<FaultScenario>( "faultlab.scenario_malformed",
                                         std::string( "cannot parse scenario JSON: " ) +
                                             exception.what() );
    }
    return loadFaultScenario( doc );
}

} // namespace sicnu::faultlab
