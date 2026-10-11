// checks_artifact.cpp — see checks_artifact.h for the fixed ordering and for
// why "no CRS" may never be reported as "wrong CRS".

#include "verification/checks_artifact.h"

#include "verification/availability.h"
#include "verification/canonical_json.h"
#include "verification/checks_numeric.h"
#include "verification/evidence.h"
#include "verification/failure_codes.h"

#include <algorithm>
#include <string>
#include <vector>

namespace sicnu::verification
{

// ---------------------------------------------------------------------------
// The closed fact vocabulary, mirrored
// ---------------------------------------------------------------------------
//
// kFactKeys lives in an anonymous namespace in src/agent/harness/workflow_ir.cpp
// and is therefore not linkable from here; this track must not edit that file.
// The names below are mirrored EXACTLY, and `mirroredFactKeys()` is exported
// precisely so a drift-guard test can later diff the two lists.

std::vector<std::string> mirroredFactKeys()
{
    return {
        "kind",
        "numeric_domain",
        "dtype",
        "wavelengths_nm",
        "temporal",
        "calibration",
        "polarizations",
        "class_count",
        "crs",
        "crs_authid",
        "size",
        "pixel_size",
        "extent",
        "band_roles",
        "band_count",
        "bands",
        "modality",
        "sensor",
        "nodata",
        "quality_masks",
        "radiometric_state",
        "acquisition_time",
        "processing_level",
        "product_type",
        "product_id",
        "product_metadata",
        "temporal_facts",
        "dates",
        "feature_count",
        "geometry_type",
    };
}

bool isMirroredFactKey( const std::string &key )
{
    const std::vector<std::string> keys = mirroredFactKeys();
    return std::find( keys.begin(), keys.end(), key ) != keys.end();
}

namespace
{

struct GridSize
{
    double width = 0.0;
    double height = 0.0;
};

struct Expectation
{
    bool hasKind = false;
    std::string kind;
    bool hasSize = false;
    GridSize size;
    bool hasCrs = false;
    std::string crsAuthId;
};

const Json::Value *optionalMember( const Json::Value &json, const char *name )
{
    return json.isMember( name ) ? &json[name] : nullptr;
}

/// Reads size.px from facts, accepting both shapes the inspect tools produce
/// ({ "width": .., "height": .. } and [width, height]) — see the "either" type
/// note on `size` in the authoritative kFactKeys table.
bool readGridSize( const Json::Value &node, GridSize &out )
{
    if ( node.isObject() )
    {
        const Json::Value *widthMember = optionalMember( node, "width" );
        const Json::Value *heightMember = optionalMember( node, "height" );
        if ( widthMember == nullptr || heightMember == nullptr ||
             !widthMember->isNumeric() || !heightMember->isNumeric() )
        {
            return false;
        }
        out.width = widthMember->asDouble();
        out.height = heightMember->asDouble();
        return true;
    }
    if ( node.isArray() && node.size() >= 2 && node[0].isNumeric() && node[1].isNumeric() )
    {
        out.width = node[0].asDouble();
        out.height = node[1].asDouble();
        return true;
    }
    return false;
}

bool readExpectation( const Json::Value &params, Expectation &expectation,
                      std::vector<std::string> &requiredKeys, std::string &problem,
                      std::string &unexpectedKey )
{
    Json::Value expect{ Json::objectValue };
    const Json::Value *expectMember = optionalMember( params, "expect" );
    if ( expectMember != nullptr )
    {
        if ( !expectMember->isObject() )
        {
            problem = "member 'expect' must be an object";
            return false;
        }
        expect = *expectMember;
    }

    for ( const std::string &key : expect.getMemberNames() )
    {
        if ( key != "kind" && key != "size" && key != "crs_authid" )
        {
            unexpectedKey = key;
            problem = "this family cannot evaluate expectation member '" + key + "'";
            return false;
        }
    }

    const Json::Value *kindMember = optionalMember( expect, "kind" );
    if ( kindMember != nullptr )
    {
        if ( !kindMember->isString() || kindMember->asString().empty() )
        {
            problem = "expectation member 'kind' must be a non-empty string";
            return false;
        }
        expectation.hasKind = true;
        expectation.kind = kindMember->asString();
    }

    const Json::Value *sizeMember = optionalMember( expect, "size" );
    if ( sizeMember != nullptr )
    {
        if ( !sizeMember->isObject() )
        {
            problem = "expectation member 'size' must be an object with width and height";
            return false;
        }
        if ( !optionalMember( *sizeMember, "width" ) || !optionalMember( *sizeMember, "height" ) ||
             !(*sizeMember)["width"].isNumeric() || !(*sizeMember)["height"].isNumeric() )
        {
            problem = "expectation member 'size' must carry numeric width and height";
            return false;
        }
        expectation.hasSize = true;
        expectation.size.width = ( *sizeMember )["width"].asDouble();
        expectation.size.height = ( *sizeMember )["height"].asDouble();
    }

    const Json::Value *crsMember = optionalMember( expect, "crs_authid" );
    if ( crsMember != nullptr )
    {
        if ( !crsMember->isString() || crsMember->asString().empty() )
        {
            problem = "expectation member 'crs_authid' must be a non-empty string";
            return false;
        }
        expectation.hasCrs = true;
        expectation.crsAuthId = crsMember->asString();
    }

    const Json::Value *keysMember = optionalMember( params, "required_keys" );
    if ( keysMember != nullptr )
    {
        if ( !keysMember->isArray() )
        {
            problem = "member 'required_keys' must be an array of fact keys";
            return false;
        }
        for ( const Json::Value &entry : *keysMember )
        {
            if ( !entry.isString() )
            {
                problem = "every entry of 'required_keys' must be a string";
                return false;
            }
            const std::string key = entry.asString();
            if ( !isMirroredFactKey( key ) )
            {
                unexpectedKey = key;
                problem = "'" + key + "' is not part of the closed fact vocabulary, so this "
                                      "check cannot require it";
                return false;
            }
            requiredKeys.push_back( key );
        }
    }

    if ( !expectation.hasKind && !expectation.hasSize && !expectation.hasCrs &&
         requiredKeys.empty() )
    {
        problem = "no expectation was declared; an artifact shape check needs at least one of "
                  "expect.kind, expect.size, expect.crs_authid or required_keys";
        return false;
    }

    return true;
}

/// A check that says anything about an artifact had better be able to say what
/// the artifact "is"; these keys join `required_keys` for that reason.
///
/// `facts` is consulted because kind and grid are mutually exclusive claims: a
/// vector has no grid, so demanding `size` of one would report a fabricated
/// schema defect instead of the true "this is not the kind you asked for".
std::vector<std::string> requiredKeySet( const std::vector<std::string> &declared,
                                         const Expectation &expectation,
                                         const Json::Value &facts )
{
    const bool describesGrid = !facts.isMember( "kind" ) || facts["kind"].asString() == "raster";

    std::vector<std::string> keys = declared;
    if ( expectation.hasKind )
    {
        keys.emplace_back( "kind" );
    }
    if ( expectation.hasSize && describesGrid )
    {
        keys.emplace_back( "size" );
    }
    // De-duplicated WITHOUT reordering: the order below is the order a report
    // lists them in, and that has to be stable across runs.
    std::vector<std::string> unique = keys;
    unique.clear();
    for ( const std::string &key : keys )
    {
        if ( std::find( unique.begin(), unique.end(), key ) == unique.end() )
        {
            unique.push_back( key );
        }
    }
    return unique;
}

CheckResult unavailableResult( CheckResult result, Availability availability,
                              const std::string &reason, const VerificationCheck &check )
{
    result.evidence.details["availability"] = availabilityToWire( availability );
    if ( !reason.empty() )
    {
        result.evidence.details["reason"] = reason;
    }
    if ( availability == Availability::Refused )
    {
        finishResult( result, CheckStatus::Indeterminate, failure_codes::kEvidenceRefused,
                      titleScopedMessage( check,
                                          std::string( "the artifact source refused to describe " ) +
                                              check.subject.id + ": " + reason +
                                              "; that is an operational problem to clear, not "
                                              "scientific evidence about the artifact" ) );
        return result;
    }
    finishResult( result, CheckStatus::Indeterminate, failure_codes::kEvidenceUnavailable,
                  titleScopedMessage( check,
                                      std::string( "no source could describe " ) +
                                          check.subject.id +
                                          ", so nothing is known about its kind, grid or frame; "
                                          "absence of evidence is neither a confirmation nor a "
                                          "refutation, and this stays unverified" ) );
    return result;
}

} // namespace

CheckResult runArtifactShapeCheck( const VerificationCheck &check, const VerificationInputs &inputs )
{
    CheckResult result = beginResult( check, inputs );
    result.evidence.details["artifact_ref"] = check.subject.id;

    if ( check.subject.id.empty() )
    {
        finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                      titleScopedMessage( check, "no artifact was named, so there is no "
                                                 "description to compare" ) );
        return result;
    }

    Expectation expectation;
    std::vector<std::string> declaredKeys;
    std::string problem;
    std::string unexpectedKey;
    if ( !readExpectation( check.params, expectation, declaredKeys, problem, unexpectedKey ) )
    {
        if ( !unexpectedKey.empty() )
        {
            result.evidence.details["unexpected_key"] = unexpectedKey;
        }
        else
        {
            result.evidence.details["spec_problem"] = problem;
        }
        finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                      titleScopedMessage( check, problem ) );
        return result;
    }

    if ( expectation.hasKind )
    {
        result.evidence.expected["kind"] = expectation.kind;
    }
    if ( expectation.hasSize )
    {
        // Numeric, not canonicalised text. These are the DECLARED expectation
        // echoed back so a reader can diff expected against observed without
        // reparsing prose; they are compared as numbers, so they are carried as
        // numbers. (canonicalNumber() exists for values that reach a digest,
        // where the 12-significant-digit text IS the value.)
        result.evidence.expected["size"]["width"] = expectation.size.width;
        result.evidence.expected["size"]["height"] = expectation.size.height;
    }
    if ( expectation.hasCrs )
    {
        result.evidence.expected["crs_authid"] = expectation.crsAuthId;
    }

    if ( inputs.artifact == nullptr )
    {
        return unavailableResult( std::move( result ), Availability::Missing,
                                  "no artifact provider is wired", check );
    }

    Json::Value facts{ Json::objectValue };
    std::string reason;
    const Availability answer = inputs.artifact->describe( check.subject.id, facts, reason );
    if ( answer != Availability::Found )
    {
        return unavailableResult( std::move( result ), answer, reason, check );
    }

    if ( !facts.isObject() || facts.empty() )
    {
        // Answered "found" and then described nothing: the artifact simply is
        // not there as far as this source is concerned.
        result.evidence.coverage = EvidenceCoverage::Unavailable;
        finishResult( result, CheckStatus::Indeterminate, failure_codes::kArtifactMissing,
                      titleScopedMessage( check,
                                          std::string( "the source described " ) +
                                              check.subject.id +
                                              " as carrying no facts at all, so there is no "
                                              "artifact here to judge" ) );
        return result;
    }

    // Coverage: by default everything the provider returned describes the whole
    // artifact. `sampling` demotes what follows to an ESTIMATE.
    const Json::Value *samplingMember = optionalMember( check.params, "sampling" );
    if ( samplingMember != nullptr )
    {
        if ( !samplingMember->isObject() )
        {
            result.evidence.details["spec_problem"] = "member 'sampling' must be an object";
            finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                          titleScopedMessage( check, "member 'sampling' must be an object with "
                                                     "sample_size and population" ) );
            return result;
        }
        result.evidence.coverage = EvidenceCoverage::Sampled;
        const Json::Value *samplesMember = optionalMember( *samplingMember, "sample_size" );
        if ( samplesMember != nullptr && samplesMember->isNumeric() )
        {
            result.evidence.details["sample_size"] = canonicalNumber( samplesMember->asDouble() );
        }
        const Json::Value *populationMember = optionalMember( *samplingMember, "population" );
        if ( populationMember != nullptr && populationMember->isNumeric() )
        {
            result.evidence.details["population"] = canonicalNumber( populationMember->asDouble() );
        }
    }
    else
    {
        result.evidence.coverage = EvidenceCoverage::Full;
    }

    // Sampled observations are recorded with what was seen, as given.
    result.evidence.observed = facts;

    // A sampled record that forgets its frame — how many were looked at, out of
    // how many — is not a measurement of anything. Judge nothing from it.
    if ( result.evidence.coverage == EvidenceCoverage::Sampled )
    {
        const std::vector<std::string> problems = evidenceCompleteness( result.evidence );
        if ( !problems.empty() )
        {
            Json::Value listed{ Json::arrayValue };
            for ( const std::string &entry : problems )
            {
                listed.append( entry );
            }
            result.evidence.details["evidence_problems"] = listed;
            finishResult( result, CheckStatus::Indeterminate, failure_codes::kEvidenceUnavailable,
                          titleScopedMessage( check, problems.front() +
                                                         "; an estimate without its frame must "
                                                         "never be promoted to a fact" ) );
            return result;
        }
    }

    // 4. Kind FIRST, before any schema completeness check.
    //
    // Ordering here is a semantic decision, not an optimisation. A vector
    // legitimately has no `size` key — it has no grid — so if a check asks "a
    // 512x512 raster?" of a vector and the schema test ran first, the report
    // would say "the description omits required fact key(s) size", which reads
    // as "the description was malformed". It was not malformed; it described a
    // vector accurately. The real answer is that a vector is not a raster, and
    // a caller acting on the report needs THAT sentence, because "malformed
    // description" sends them to fix the producer while "wrong kind" sends them
    // to fix the plan.
    if ( expectation.hasKind )
    {
        const Json::Value *observedKindMember = optionalMember( facts, "kind" );
        const std::string observedKind =
            observedKindMember != nullptr && observedKindMember->isString()
                ? observedKindMember->asString()
                : std::string();
        if ( observedKind.empty() )
        {
            result.evidence.details["failing_key"] = "kind";
            result.evidence.details["unreadable_key"] = "kind";
            finishResult( result, CheckStatus::Indeterminate, failure_codes::kArtifactSchemaMismatch,
                          titleScopedMessage( check,
                                              std::string( "the description of " ) +
                                                  check.subject.id +
                                                  " declares no readable kind, so it cannot be "
                                                  "told whether it is the '" + expectation.kind +
                                                  "' the step contracted" ) );
            return result;
        }
        if ( observedKind != expectation.kind )
        {
            result.evidence.details["failing_key"] = "kind";
            finishResult( result, CheckStatus::Fail, failure_codes::kArtifactKindMismatch,
                          titleScopedMessage( check,
                                              std::string( "the step contracted a '" ) +
                                                  expectation.kind + "' but " +
                                                  check.subject.id + " is a '" + observedKind +
                                                  "'; gridded operators — band arithmetic, "
                                                  "kernel windows, per-pixel statistics — are "
                                                  "undefined on the delivered kind, so nothing "
                                                  "computed from it describes the same object" ) );
            return result;
        }
    }

    // 5. Schema: every fact the plan relies on must be present, but only for
    // keys that are meaningful GIVEN the kind we just established. Requiring
    // `size` of a vector would fabricate a problem the artifact does not have.
    Json::Value missing{ Json::arrayValue };
    for ( const std::string &key : requiredKeySet( declaredKeys, expectation, facts ) )
    {
        if ( !facts.isMember( key ) )
        {
            missing.append( key );
        }
    }
    if ( !missing.empty() )
    {
        result.evidence.details["missing_keys"] = missing;
        std::string listed;
        for ( Json::ArrayIndex index = 0; index < missing.size(); ++index )
        {
            if ( index != 0 )
            {
                listed += ", ";
            }
            listed += missing[index].asString();
        }
        finishResult( result, CheckStatus::Fail, failure_codes::kArtifactSchemaMismatch,
                      titleScopedMessage( check,
                                          std::string( "the description of " ) +
                                              check.subject.id + " omits required fact key(s) " +
                                              listed +
                                              "; whatever else the artifact claims cannot be "
                                              "checked against expectations about something it "
                                              "never described" ) );
        return result;
    }

    // 6. Grid.
    if ( expectation.hasSize )
    {
        GridSize observedSize;
        if ( !readGridSize( facts["size"], observedSize ) )
        {
            result.evidence.details["failing_key"] = "size";
            finishResult( result, CheckStatus::Indeterminate,
                          failure_codes::kArtifactSchemaMismatch,
                          titleScopedMessage( check,
                                              std::string( "the description of " ) +
                                                  check.subject.id +
                                                  " carries no readable width and height, so the "
                                                  "declared grid cannot be compared" ) );
            return result;
        }

        const double *declaredSizes[2] = { &expectation.size.width, &expectation.size.height };
        const double observedSizes[2] = { observedSize.width, observedSize.height };
        const char *names[2] = { "size.width", "size.height" };
        for ( int axis = 0; axis < 2; ++axis )
        {
            if ( numericSame( observedSizes[axis], *declaredSizes[axis] ) )
            {
                continue;
            }
            const double delta = observedSizes[axis] - *declaredSizes[axis];
            result.evidence.details["failing_key"] = names[axis];
            result.evidence.details["observed"] = canonicalNumber( observedSizes[axis] );
            result.evidence.details["expected"] = canonicalNumber( *declaredSizes[axis] );
            result.evidence.details["delta"] = canonicalNumber( delta );
            finishResult( result, CheckStatus::Fail, failure_codes::kArtifactGridMismatch,
                          titleScopedMessage( check,
                                              std::string( "declared grid is " ) +
                                                  roundSignificant( expectation.size.width ) +
                                                  " x " + roundSignificant( expectation.size.height ) +
                                                  " but " + names[axis] + " is " +
                                                  roundSignificant( observedSizes[axis] ) +
                                                  " (delta " + roundSignificant( delta ) +
                                                  "); one axis off by even one pixel shifts every "
                                                  "sample by one ground sample distance and "
                                                  "silently misaligns any overlay, differencing or "
                                                  "zonal statistic built on this grid" ) );
            return result;
        }
    }

    // 7. Spatial frame. The two outcomes below MUST stay apart: absent evidence
    // versus contradicting evidence. Reporting the first as the second is the
    // defect this whole track exists to remove.
    if ( expectation.hasCrs )
    {
        // `crs_authid` is deliberately NOT in requiredKeySet. Demanding its
        // presence would route "the producer never recorded a CRS" through the
        // generic schema check, i.e. through FAIL — which is exactly the bug
        // this track exists to remove. Absence is its own verdict below.
        if ( !facts.isMember( "crs_authid" ) || !facts["crs_authid"].isString() ||
             facts["crs_authid"].asString().empty() )
        {
            result.evidence.details["failing_key"] = "crs_authid";
            result.evidence.details["absence"] = true;
            result.evidence.expected["crs_authid"] = expectation.crsAuthId;
            finishResult( result, CheckStatus::Indeterminate,
                          failure_codes::kEvidenceUnavailable,
                          titleScopedMessage( check,
                                              std::string( "no CRS was described for " ) +
                                                  check.subject.id +
                                                  ", so the contracted frame cannot be compared; "
                                                  "this is absence of evidence, not evidence of a "
                                                  "different frame, and no reprojection is implied" ) );
            return result;
        }

        const std::string observedCrs = facts["crs_authid"].asString();
        if ( observedCrs != expectation.crsAuthId )
        {
            result.evidence.details["failing_key"] = "crs_authid";
            result.evidence.details["observed"] = observedCrs;
            result.evidence.details["expected"] = expectation.crsAuthId;
            finishResult( result, CheckStatus::Fail, failure_codes::kArtifactGridMismatch,
                          titleScopedMessage( check,
                                              std::string( "the step contracted frame " ) +
                                                  expectation.crsAuthId + " but " +
                                                  check.subject.id + " carries " + observedCrs +
                                                  "; comparing positions across these frames "
                                                  "mis-registers pixels by up to hundreds of "
                                                  "metres and every area or distance derived from "
                                                  "it is wrong" ) );
            return result;
        }
    }

    finishResult( result, CheckStatus::Pass, {},
                  titleScopedMessage( check,
                                      std::string( "the description of " ) + check.subject.id +
                                          " satisfies every declared shape expectation" ) );
    return result;
}

} // namespace sicnu::verification
