// verification_types.cpp — serialization for the declarative value objects.
//
// Two rules shape everything here:
//
//   1. `kind` round-trips as a STRING and is never validated against the known
//      CheckKind set. An unknown kind must reach the runner so it can become an
//      Indeterminate check with VERIFY.UNSUPPORTED_CHECK_KIND rather than
//      disappearing between load and evaluation — a silently dropped check is
//      indistinguishable from a passing one.
//   2. Every rejection names the offending field. A caller whose spec is
//      refused needs to know which member to fix, not just "invalid spec".

#include "verification/verification_types.h"

#include <algorithm>
#include <string>
#include <vector>

namespace sicnu::verification
{

namespace
{

const Json::Value *optionalMember( const Json::Value &json, const char *name )
{
    return json.isMember( name ) ? &json[name] : nullptr;
}

bool requireString( const Json::Value &json, const char *name, bool required,
                    std::string &out, std::string &error )
{
    const Json::Value *member = optionalMember( json, name );
    if ( member == nullptr )
    {
        if ( required )
        {
            error = std::string( "missing required string member '" ) + name + "'";
            return false;
        }
        out.clear();
        return true;
    }
    if ( !member->isString() )
    {
        error = std::string( "member '" ) + name + "' must be a string";
        return false;
    }
    out = member->asString();
    return true;
}

bool requireObject( const Json::Value &json, const char *name, Json::Value &out,
                    std::string &error )
{
    const Json::Value *member = optionalMember( json, name );
    if ( member == nullptr )
    {
        out = Json::Value{ Json::objectValue };
        return true;
    }
    if ( !member->isObject() )
    {
        error = std::string( "member '" ) + name + "' must be an object";
        return false;
    }
    out = *member;
    return true;
}

bool requireCount( const Json::Value &json, const char *name, std::size_t fallback,
                   std::size_t &out, std::string &error )
{
    const Json::Value *member = optionalMember( json, name );
    if ( member == nullptr )
    {
        out = fallback;
        return true;
    }
    if ( !member->isIntegral() && !member->isUInt() )
    {
        error = std::string( "member '" ) + name + "' must be a non-negative integer";
        return false;
    }
    const Json::Int64 value = member->asInt64();
    if ( value < 0 )
    {
        error = std::string( "member '" ) + name + "' must not be negative";
        return false;
    }
    out = static_cast<std::size_t>( value );
    return true;
}

struct CheckKindRow
{
    CheckKind kind;
    const char *wire;
};

// Ordered to match CheckKind; iteration order carries no meaning.
const CheckKindRow kCheckKindTable[] = {
    { CheckKind::StateInvariant, "state_invariant" },
    { CheckKind::ArtifactShape, "artifact_shape" },
    { CheckKind::NumericRange, "numeric_range" },
    { CheckKind::RelationalConsistency, "relational_consistency" },
    { CheckKind::ProvenanceCompleteness, "provenance_completeness" },
    { CheckKind::ReproducibilityDigest, "reproducibility_digest" },
    { CheckKind::CrossOutputConsistency, "cross_output_consistency" },
};

} // namespace

const char *checkKindToWire( CheckKind kind )
{
    for ( const CheckKindRow &row : kCheckKindTable )
    {
        if ( row.kind == kind )
        {
            return row.wire;
        }
    }
    return "unknown";
}

bool checkKindFromWire( const std::string &wire, CheckKind &out )
{
    for ( const CheckKindRow &row : kCheckKindTable )
    {
        if ( wire == row.wire )
        {
            out = row.kind;
            return true;
        }
    }
    return false;
}

std::vector<std::string> allCheckKinds()
{
    std::vector<std::string> kinds;
    kinds.reserve( sizeof( kCheckKindTable ) / sizeof( kCheckKindTable[0] ) );
    for ( const CheckKindRow &row : kCheckKindTable )
    {
        kinds.emplace_back( row.wire );
    }
    std::sort( kinds.begin(), kinds.end() );
    return kinds;
}

bool checkKindOf( const VerificationCheck &check, CheckKind &out )
{
    return checkKindFromWire( check.kind, out );
}

Json::Value Budget::toJson() const
{
    Json::Value json{ Json::objectValue };
    json["max_checks"] = static_cast<Json::Int64>( maxChecks );
    json["max_nodes"] = static_cast<Json::Int64>( maxNodes );
    json["max_evidence_bytes"] = static_cast<Json::Int64>( maxEvidenceBytes );
    json["max_depth"] = maxDepth;
    json["max_string_chars"] = static_cast<Json::Int64>( maxStringChars );
    json["max_witness_elements"] = static_cast<Json::Int64>( maxWitnessElements );
    return json;
}

bool Budget::fromJson( const Json::Value &json, Budget &out, std::string &error )
{
    Budget loaded;
    const Json::Value *maxDepthMember = optionalMember( json, "max_depth" );
    if ( maxDepthMember != nullptr )
    {
        if ( !maxDepthMember->isIntegral() )
        {
            error = "member 'max_depth' must be an integer";
            return false;
        }
        loaded.maxDepth = maxDepthMember->asInt();
    }

    // Falls back to the struct's own defaults, so a partially-spelled budget
    // stays safe: an absent cap is never read as "unlimited".
    if ( !requireCount( json, "max_checks", loaded.maxChecks, loaded.maxChecks, error ) ||
         !requireCount( json, "max_nodes", loaded.maxNodes, loaded.maxNodes, error ) ||
         !requireCount( json, "max_evidence_bytes", loaded.maxEvidenceBytes, loaded.maxEvidenceBytes, error ) ||
         !requireCount( json, "max_string_chars", loaded.maxStringChars, loaded.maxStringChars, error ) ||
         !requireCount( json, "max_witness_elements", loaded.maxWitnessElements, loaded.maxWitnessElements, error ) )
    {
        return false;
    }

    out = loaded;
    return true;
}

Json::Value SubjectRef::toJson() const
{
    Json::Value json{ Json::objectValue };
    json["kind"] = kind;
    json["id"] = id;
    return json;
}

bool SubjectRef::fromJson( const Json::Value &json, SubjectRef &out, std::string &error )
{
    if ( !json.isObject() )
    {
        error = "subject must be an object";
        return false;
    }
    SubjectRef loaded;
    if ( !requireString( json, "kind", true, loaded.kind, error ) ||
         !requireString( json, "id", false, loaded.id, error ) )
    {
        return false;
    }
    out = loaded;
    return true;
}

Json::Value VerificationCheck::toJson() const
{
    Json::Value json{ Json::objectValue };
    json["id"] = id;
    json["kind"] = kind;
    json["title"] = title;
    json["subject"] = subject.toJson();
    json["params"] = params.isObject() ? params : Json::Value{ Json::objectValue };
    json["required"] = required;
    json["failure_code"] = failureCode;
    json["hints"] = hints.isObject() ? hints : Json::Value{ Json::objectValue };
    return json;
}

bool VerificationCheck::fromJson( const Json::Value &json, VerificationCheck &out,
                                  std::string &error )
{
    if ( !json.isObject() )
    {
        error = "each check must be an object";
        return false;
    }

    VerificationCheck loaded;
    if ( !requireString( json, "id", true, loaded.id, error ) ||
         !requireString( json, "kind", true, loaded.kind, error ) ||
         !requireString( json, "title", false, loaded.title, error ) ||
         !requireString( json, "failure_code", false, loaded.failureCode, error ) ||
         !requireObject( json, "params", loaded.params, error ) ||
         !requireObject( json, "hints", loaded.hints, error ) )
    {
        return false;
    }

    const Json::Value *subjectMember = optionalMember( json, "subject" );
    if ( subjectMember != nullptr && !SubjectRef::fromJson( *subjectMember, loaded.subject, error ) )
    {
        return false;
    }

    const Json::Value *requiredMember = optionalMember( json, "required" );
    if ( requiredMember != nullptr )
    {
        if ( !requiredMember->isBool() )
        {
            error = "member 'required' must be a boolean";
            return false;
        }
        loaded.required = requiredMember->asBool();
    }

    if ( loaded.id.empty() )
    {
        error = "check id must not be empty";
        return false;
    }
    if ( loaded.kind.empty() )
    {
        error = "check '" + loaded.id + "' has an empty kind";
        return false;
    }

    out = loaded;
    return true;
}

} // namespace sicnu::verification
