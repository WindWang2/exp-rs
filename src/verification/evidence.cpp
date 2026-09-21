// evidence.cpp — see evidence.h for why coverage determines trustworthiness.

#include "verification/evidence.h"

#include "verification/canonical_json.h"

#include <string>
#include <vector>

namespace sicnu::verification
{

const char *coverageToWire( EvidenceCoverage coverage )
{
    switch ( coverage )
    {
        case EvidenceCoverage::Full:
            return "full";
        case EvidenceCoverage::Sampled:
            return "sampled";
        case EvidenceCoverage::Unavailable:
            return "unavailable";
    }
    return "unavailable";
}

bool coverageFromWire( const std::string &wire, EvidenceCoverage &out )
{
    if ( wire == "full" )
    {
        out = EvidenceCoverage::Full;
        return true;
    }
    if ( wire == "sampled" )
    {
        out = EvidenceCoverage::Sampled;
        return true;
    }
    if ( wire == "unavailable" )
    {
        out = EvidenceCoverage::Unavailable;
        return true;
    }
    return false;
}

Json::Value VerificationEvidence::toJson() const
{
    Json::Value json{ Json::objectValue };
    json["kind"] = kind;
    json["coverage"] = coverageToWire( coverage );
    json["observed"] = observed;
    json["expected"] = expected;
    json["details"] = details;
    json["source_id"] = sourceId;
    return json;
}

bool VerificationEvidence::fromJson( const Json::Value &json, VerificationEvidence &out,
                                     std::string &error )
{
    if ( !json.isObject() )
    {
        error = "evidence must be an object";
        return false;
    }

    VerificationEvidence loaded;
    if ( json.isMember( "kind" ) )
    {
        if ( !json["kind"].isString() )
        {
            error = "member 'kind' must be a string";
            return false;
        }
        loaded.kind = json["kind"].asString();
    }
    if ( json.isMember( "coverage" ) )
    {
        if ( !json["coverage"].isString() ||
             !coverageFromWire( json["coverage"].asString(), loaded.coverage ) )
        {
            error = "member 'coverage' must be one of full|sampled|unavailable";
            return false;
        }
    }
    if ( json.isMember( "source_id" ) )
    {
        if ( !json["source_id"].isString() )
        {
            error = "member 'source_id' must be a string";
            return false;
        }
        loaded.sourceId = json["source_id"].asString();
    }
    for ( const char *member : { "observed", "expected", "details" } )
    {
        if ( json.isMember( member ) && !json[member].isObject() )
        {
            error = std::string( "member '" ) + member + "' must be an object";
            return false;
        }
    }
    if ( json.isMember( "observed" ) )
    {
        loaded.observed = json["observed"];
    }
    if ( json.isMember( "expected" ) )
    {
        loaded.expected = json["expected"];
    }
    if ( json.isMember( "details" ) )
    {
        loaded.details = json["details"];
    }

    out = std::move( loaded );
    return true;
}

std::vector<std::string> evidenceCompleteness( const VerificationEvidence &evidence )
{
    std::vector<std::string> problems;

    if ( evidence.coverage == EvidenceCoverage::Unavailable && evidence.observed.empty() )
    {
        // Legitimate and expected when a provider returned Missing/Refused; the
        // caller is responsible for turning it into Indeterminate. It is only a
        // PROBLEM if some other part of the record claims otherwise.
        if ( !evidence.expected.empty() )
        {
            problems.emplace_back(
                "evidence is unavailable yet declares an expectation; the check cannot conclude" );
        }
        return problems;
    }

    if ( evidence.coverage == EvidenceCoverage::Sampled )
    {
        // The whole point of Sampled: "1.2% NoData over 1000 pixels" is an
        // estimate, and an estimate presented without its frame is a lie of
        // omission. Both numbers are mandatory.
        if ( !evidence.details.isMember( "sample_size" ) )
        {
            problems.emplace_back( "sampled evidence is missing details.sample_size" );
        }
        if ( !evidence.details.isMember( "population" ) )
        {
            problems.emplace_back( "sampled evidence is missing details.population" );
        }
    }

    if ( evidence.sourceId.empty() )
    {
        problems.emplace_back( "evidence declares no source id" );
    }

    return problems;
}

std::size_t evidenceBytes( const VerificationEvidence &evidence )
{
    std::string error;
    std::string text;
    if ( !canonicalJson( evidence.toJson(), text, error ) )
    {
        // An evidence record that cannot even be measured is, at minimum, as
        // expensive as the largest one we could tolerate. Reporting 0 here
        // would let an uncanonicalizable record claim to be cheap.
        return Json::Value::maxInt;
    }
    return text.size();
}

} // namespace sicnu::verification
