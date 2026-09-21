// spec.cpp — see spec.h for why specs are content-addressed.

#include "verification/spec.h"

#include "verification/canonical_json.h"
#include "verification/digest.h"
#include "verification/failure_codes.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace sicnu::verification
{

namespace
{

const char *indeterminatePolicyToWire( IndeterminatePolicy policy )
{
    return policy == IndeterminatePolicy::Fail ? "fail" : "keep";
}

bool indeterminatePolicyFromWire( const std::string &wire, IndeterminatePolicy &out,
                                  std::string &error )
{
    if ( wire == "keep" )
    {
        out = IndeterminatePolicy::Keep;
        return true;
    }
    if ( wire == "fail" )
    {
        out = IndeterminatePolicy::Fail;
        return true;
    }
    error = "unknown indeterminate_policy '" + wire + "' (expected 'keep' or 'fail')";
    return false;
}

} // namespace

Json::Value VerificationSpec::toJson() const
{
    Json::Value json{ Json::objectValue };
    json["schema"] = kVerificationSpecSchema;
    json["schema_version"] = schemaVersion;
    json["spec_id"] = specId;
    json["spec_version"] = specVersion;
    json["indeterminate_policy"] = indeterminatePolicyToWire( indeterminatePolicy );
    json["budget"] = budget.toJson();

    Json::Value checkArray{ Json::arrayValue };
    for ( const VerificationCheck &check : checks )
    {
        checkArray.append( check.toJson() );
    }
    json["checks"] = checkArray;

    Json::Value evidenceArray{ Json::arrayValue };
    for ( const std::string &evidence : requiredEvidence )
    {
        evidenceArray.append( evidence );
    }
    json["required_evidence"] = evidenceArray;

    return json;
}

bool VerificationSpec::fromJson( const Json::Value &json, VerificationSpec &out,
                                 std::string &error )
{
    if ( !json.isObject() )
    {
        error = "verification spec must be an object";
        return false;
    }

    VerificationSpec loaded;

    const Json::Value *schemaMember = json.isMember( "schema" ) ? &json["schema"] : nullptr;
    if ( schemaMember != nullptr )
    {
        if ( !schemaMember->isString() || schemaMember->asString() != kVerificationSpecSchema )
        {
            // Refuse foreign versions rather than reinterpret them: a
            // half-understood spec is how a downstream check reads a field that
            // changed meaning.
            error = "unsupported spec schema (expected " + std::string( kVerificationSpecSchema ) + ")";
            return false;
        }
    }

    const Json::Value *versionMember = json.isMember( "schema_version" ) ? &json["schema_version"] : nullptr;
    if ( versionMember != nullptr )
    {
        if ( !versionMember->isIntegral() )
        {
            error = "member 'schema_version' must be an integer";
            return false;
        }
        loaded.schemaVersion = versionMember->asInt();
    }
    if ( loaded.schemaVersion != kVerificationSpecSchemaVersion )
    {
        error = "unsupported spec schema_version";
        return false;
    }

    if ( json.isMember( "spec_id" ) )
    {
        if ( !json["spec_id"].isString() )
        {
            error = "member 'spec_id' must be a string";
            return false;
        }
        loaded.specId = json["spec_id"].asString();
    }
    if ( json.isMember( "spec_version" ) )
    {
        if ( !json["spec_version"].isString() )
        {
            error = "member 'spec_version' must be a string";
            return false;
        }
        loaded.specVersion = json["spec_version"].asString();
    }
    if ( json.isMember( "indeterminate_policy" ) )
    {
        if ( !json["indeterminate_policy"].isString() )
        {
            error = "member 'indeterminate_policy' must be a string";
            return false;
        }
        if ( !indeterminatePolicyFromWire( json["indeterminate_policy"].asString(),
                                           loaded.indeterminatePolicy, error ) )
        {
            return false;
        }
    }
    if ( json.isMember( "budget" ) && !Budget::fromJson( json["budget"], loaded.budget, error ) )
    {
        return false;
    }

    const Json::Value *checksMember = json.isMember( "checks" ) ? &json["checks"] : nullptr;
    if ( checksMember != nullptr )
    {
        if ( !checksMember->isArray() )
        {
            error = "member 'checks' must be an array";
            return false;
        }
        for ( const Json::Value &entry : *checksMember )
        {
            VerificationCheck check;
            if ( !VerificationCheck::fromJson( entry, check, error ) )
            {
                return false;
            }
            loaded.checks.push_back( std::move( check ) );
        }
    }

    const Json::Value *evidenceMember =
        json.isMember( "required_evidence" ) ? &json["required_evidence"] : nullptr;
    if ( evidenceMember != nullptr )
    {
        if ( !evidenceMember->isArray() )
        {
            error = "member 'required_evidence' must be an array";
            return false;
        }
        for ( const Json::Value &entry : *evidenceMember )
        {
            if ( !entry.isString() )
            {
                error = "every entry of 'required_evidence' must be a string";
                return false;
            }
            loaded.requiredEvidence.emplace_back( entry.asString() );
        }
    }

    out = std::move( loaded );
    return true;
}

std::vector<std::string> validateSpec( const VerificationSpec &spec )
{
    std::vector<std::string> problems;

    if ( spec.schemaVersion != kVerificationSpecSchemaVersion )
    {
        problems.emplace_back( failure_codes::kSpecInvalid );
    }
    if ( spec.specId.empty() )
    {
        // Content addressing requires a human-legible name to go with the
        // digest; a nameless spec cannot be referred to in a report.
        problems.emplace_back( "spec id must not be empty" );
    }

    std::set<std::string> seen;
    for ( const VerificationCheck &check : spec.checks )
    {
        if ( check.id.empty() )
        {
            problems.emplace_back( "every check needs an id" );
            continue;
        }
        // Duplicates are reported, not rejected outright: whether an overlap is
        // fatal depends on the composition policy the caller chose (pack.h),
        // which validation cannot see.
        if ( !seen.insert( check.id ).second )
        {
            problems.emplace_back( "duplicate check id '" + check.id + "'" );
        }
        if ( check.kind.empty() )
        {
            // An empty kind cannot even be named as unsupported later.
            problems.emplace_back( "check '" + check.id + "' has an empty kind" );
        }
        if ( !check.failureCode.empty() && !isKnownFailureCode( check.failureCode ) )
        {
            // An unrecognised code would reach an Agent consumer that has no
            // idea what to do with it. Surface it at authoring time instead.
            problems.emplace_back( "check '" + check.id + "' declares unknown failure code '"
                                   + check.failureCode + "'" );
        }
    }

    if ( spec.checks.size() > spec.budget.maxChecks )
    {
        problems.emplace_back( failure_codes::kBudgetExceeded );
    }

    return problems;
}

std::string specDigest( const VerificationSpec &spec )
{
    std::string error;
    return canonicalDigestSha256( spec.toJson(), error );
}

} // namespace sicnu::verification
