// pack.cpp — composition and serialization of verifier packs. See pack.h for
// why a conflict is never resolved silently.

#include "verification/pack.h"

#include "verification/failure_codes.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::verification
{

namespace
{

std::string joinQuoted( const std::vector<std::string> &ids )
{
    std::string out;
    for ( const std::string &id : ids )
    {
        if ( !out.empty() )
        {
            out += ", ";
        }
        out += "'" + id + "'";
    }
    return out;
}

void refuse( ComposeOutcome &outcome, const char *code, std::string reason )
{
    outcome.ok = false;
    outcome.failureCode = code;
    outcome.reason = std::move( reason );
    // A refused composition hands back nothing at all: a partially merged
    // check list is exactly what a downstream roll-up would mistake for
    // "everything that survived, passed".
    outcome.checks.clear();
    outcome.overrides.clear();
}

} // namespace

Json::Value VerifierPack::toJson() const
{
    Json::Value json{ Json::objectValue };
    json["schema"] = schema;
    json["id"] = id;
    json["version"] = version;
    json["derived_from"] = derivedFrom;

    Json::Value checkArray{ Json::arrayValue };
    for ( const VerificationCheck &check : checks )
    {
        checkArray.append( check.toJson() );
    }
    json["checks"] = checkArray;
    return json;
}

bool VerifierPack::fromJson( const Json::Value &json, VerifierPack &out, std::string &error )
{
    if ( !json.isObject() )
    {
        error = "verifier pack must be an object";
        return false;
    }

    VerifierPack loaded;

    if ( json.isMember( "schema" ) )
    {
        if ( !json["schema"].isString() )
        {
            error = "member 'schema' must be a string";
            return false;
        }
        // Refuse foreign schemas rather than reinterpret them: a pack written
        // under different rules would otherwise be merged as if it were this
        // version's, and the difference would never be reported.
        if ( json["schema"].asString() != kVerifierPackSchema )
        {
            error = "unsupported pack schema (expected " + std::string( kVerifierPackSchema ) + ")";
            return false;
        }
    }
    loaded.schema = kVerifierPackSchema;

    if ( json.isMember( "id" ) )
    {
        if ( !json["id"].isString() )
        {
            error = "member 'id' must be a string";
            return false;
        }
        loaded.id = json["id"].asString();
    }
    if ( loaded.id.empty() )
    {
        error = "pack id must not be empty";
        return false;
    }
    if ( json.isMember( "version" ) )
    {
        if ( !json["version"].isString() )
        {
            error = "member 'version' must be a string";
            return false;
        }
        loaded.version = json["version"].asString();
    }
    if ( json.isMember( "derived_from" ) )
    {
        if ( !json["derived_from"].isString() )
        {
            error = "member 'derived_from' must be a string";
            return false;
        }
        loaded.derivedFrom = json["derived_from"].asString();
    }

    if ( json.isMember( "checks" ) )
    {
        if ( !json["checks"].isArray() )
        {
            error = "member 'checks' must be an array";
            return false;
        }
        std::set<std::string> seen;
        for ( const Json::Value &entry : json["checks"] )
        {
            VerificationCheck check;
            if ( !VerificationCheck::fromJson( entry, check, error ) )
            {
                return false;
            }
            if ( check.id.empty() )
            {
                // Empty ids collide with each other invisibly: two of them
                // would merge into one with no trace.
                error = "every check in a pack needs a non-empty id";
                return false;
            }
            if ( !seen.insert( check.id ).second )
            {
                error = "duplicate check id '" + check.id + "' in pack '" + loaded.id + "'";
                return false;
            }
            loaded.checks.push_back( std::move( check ) );
        }
    }

    out = std::move( loaded );
    return true;
}

ComposeOutcome composePacks( const std::vector<VerifierPack> &packs, PackConflictPolicy policy )
{
    ComposeOutcome outcome;

    std::vector<const VerifierPack *> ordered;
    ordered.reserve( packs.size() );
    for ( const VerifierPack &pack : packs )
    {
        if ( pack.id.empty() )
        {
            // Without an id a pack has no place in the ordering, so the result
            // would depend on the caller's listing order — which is precisely
            // the non-determinism that makes digests unreproducible.
            refuse( outcome, failure_codes::kSpecInvalid,
                    "every pack needs an id: without one the composition order "
                    "follows the caller's listing instead of pack identity" );
            return outcome;
        }
        ordered.push_back( &pack );
    }
    std::stable_sort( ordered.begin(), ordered.end(),
                      []( const VerifierPack *left, const VerifierPack *right )
                      { return left->id < right->id; } );

    std::map<std::string, std::string> owner;   // check id -> pack id that supplied it
    std::set<std::string> duplicates;
    std::vector<std::string> unnamed;

    for ( const VerifierPack *pack : ordered )
    {
        for ( const VerificationCheck &check : pack->checks )
        {
            if ( check.id.empty() )
            {
                unnamed.push_back( pack->id );
                continue;
            }

            const std::map<std::string, std::string>::iterator existing = owner.find( check.id );
            if ( existing == owner.end() )
            {
                owner.emplace( check.id, pack->id );
                outcome.checks.push_back( check );
                continue;
            }

            duplicates.insert( check.id );
            if ( policy == PackConflictPolicy::Override )
            {
                // The FIRST declaration wins and the drop is recorded. The
                // tempting alternative — overwrite `outcome.checks[i]` — is
                // last-write-wins, and it leaves no trace that an expectation
                // was discarded.
                outcome.overrides.push_back( PackOverride{ check.id, existing->second, pack->id } );
            }
        }
    }

    if ( !unnamed.empty() )
    {
        refuse( outcome, failure_codes::kSpecInvalid,
                "pack(s) " + joinQuoted( unnamed )
                    + " declare a check with an empty id; empty ids collide silently" );
        return outcome;
    }

    outcome.duplicateIds.assign( duplicates.begin(), duplicates.end() );

    if ( !duplicates.empty() && policy == PackConflictPolicy::Reject )
    {
        refuse( outcome, failure_codes::kSpecInvalid,
                "check id(s) " + joinQuoted( outcome.duplicateIds )
                    + " are declared by more than one pack; refusing a silent "
                      "last-write-wins merge" );
        return outcome;
    }

    if ( outcome.checks.empty() )
    {
        // Zero checks is absence of evidence, which the lattice calls
        // Indeterminate and never Pass. Handing back an empty vector would let
        // a caller treat "we have no expectations" as "all expectations held".
        refuse( outcome, failure_codes::kNoChecks,
                std::string( "composition produced no checks; zero checks is " )
                    + failure_codes::kNoChecks + " (Indeterminate), never a pass" );
        return outcome;
    }

    return outcome;
}

} // namespace sicnu::verification
