// fault_scenario.cpp — scenario document loading/validation (the
// `sicnu.lab.faults/1` schema gate). Fail-closed: a foreign schema version,
// an unknown fault family, an unknown relation or a malformed field each
// produce a typed `faultlab.*` diagnostic; nothing is silently defaulted
// into validity. Declarations live in fault_types.h.
#include "fault_types.h"

#include "fault_registry.h"

#include <json/json.h>

#include <fstream>
#include <sstream>
#include <string>

#include <limits>

namespace sicnu::faultlab
{

namespace
{

using Json::Value;

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
        out = seed.asUInt();
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
            if ( !maxBytes.isIntegral() || maxBytes.asUInt64() > kFaultLabMaxBytes )
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
        if ( entry.isMember( "value" ) && entry["value"].isNumeric() )
        {
            expectation.value = entry["value"].asDouble();
        }
        if ( expectation.relation == ExpectationRelation::TruthIs )
        {
            if ( !entry.isMember( "value" ) || !entry["value"].isBool() )
            {
                return fieldError( "truth_is expectation for '" + expectation.id +
                                   "' needs a boolean value" );
            }
            expectation.value = entry["value"].asBool() ? 1.0 : 0.0;
        }
        if ( entry.isMember( "range_lo" ) && entry["range_lo"].isNumeric() )
        {
            expectation.rangeLo = entry["range_lo"].asDouble();
        }
        if ( entry.isMember( "range_hi" ) && entry["range_hi"].isNumeric() )
        {
            expectation.rangeHi = entry["range_hi"].asDouble();
        }
        if ( expectation.relation == ExpectationRelation::InRange &&
             expectation.rangeLo > expectation.rangeHi )
        {
            return fieldError( "in_range expectation for '" + expectation.id +
                               "' needs range_lo <= range_hi" );
        }
        if ( entry.isMember( "text" ) && entry["text"].isString() )
        {
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
    if ( !matchesPattern( scenario.learningObjectiveId, "LO-", true ) )
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
    std::ostringstream buffer;
    buffer << stream.rdbuf();

    Json::Value doc;
    Json::CharReaderBuilder builder;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( buffer.str().data(), buffer.str().data() + buffer.str().size(), &doc,
                         &errors ) )
    {
        return makeError<FaultScenario>( "faultlab.scenario_malformed",
                                         "cannot parse scenario JSON: " + errors );
    }
    return loadFaultScenario( doc );
}

} // namespace sicnu::faultlab
