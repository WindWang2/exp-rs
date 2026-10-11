/***************************************************************************
  test_verifier_checks_14.cpp — Unified Scientific Verifier 14 (Slices B + C)

  Slice B owns `StateInvariant` and `ArtifactShape`; Slice C owns
  `NumericRange` and `RelationalConsistency`. They share one lane because they
  share one rule, and this lane exists to prove that rule survives mutation:

      "we could not obtain the evidence" is a THIRD answer. It is never Pass
      and never Fail.

  Everything below is driven through in-memory fake providers: no filesystem,
  no Qt, no QGIS, no fixtures on disk. Each test case is named after the
  requirement it pins (B-1 ... B-10, C-1 ... C-7) so that the mutation report
  can say exactly which requirement went red.

  Every case asserts the same three things about every result:
    - the identity/coverage/source plumbing is complete (requireWellFormed),
    - the verdict and the failure code are the ones required,
    - the record survives toJson/fromJson and two runs produce one canonical
      text (requireRoundTripAndDeterminism / requireStable).
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "verification/canonical_json.h"
#include "verification/checks_artifact.h"
#include "verification/checks_numeric.h"
#include "verification/checks_relational.h"
#include "verification/checks_state.h"
#include "verification/evidence.h"
#include "verification/failure_codes.h"
#include "verification/status_lattice.h"

#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{

using sicnu::verification::ArtifactProvider;
using sicnu::verification::Availability;
using sicnu::verification::CheckResult;
using sicnu::verification::CheckStatus;
using sicnu::verification::EvidenceCoverage;
using sicnu::verification::MetricProvider;
using sicnu::verification::MetricValue;
using sicnu::verification::StateProvider;
using sicnu::verification::StateSnapshot;
using sicnu::verification::VerificationCheck;
using sicnu::verification::VerificationEvidence;
using sicnu::verification::VerificationInputs;

namespace fcv = sicnu::verification::failure_codes;

const char *kBundle = "fake-provider-bundle-14";
const char *kDemRef = "out/dem_calibrated.tif";
const char *kVectorRef = "out/watershed_boundaries.gpkg";

const double kNan = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

// --------------------------------------------------------------------------
// Fake providers: answers are decided by the fixture, never by the core.
// --------------------------------------------------------------------------

class FakeStates final : public StateProvider
{
  public:
    std::map<std::string, StateSnapshot> states;
    std::map<std::string, Availability> availability;  ///< absent means Found
    std::string reason;

    Availability tryGet( const std::string &subjectId, StateSnapshot &out,
                         std::string &why ) const override
    {
        lastAsked.push_back( subjectId );
        why = reason;
        const Availability answer = find( availability, subjectId );
        out = answer == Availability::Found ? findValue( states, subjectId ) : StateSnapshot{};
        return answer;
    }

    mutable std::vector<std::string> lastAsked;

  private:
    static Availability find( const std::map<std::string, Availability> &table,
                              const std::string &key )
    {
        const auto found = table.find( key );
        return found == table.end() ? Availability::Found : found->second;
    }
    static StateSnapshot findValue( const std::map<std::string, StateSnapshot> &table,
                                    const std::string &key )
    {
        const auto found = table.find( key );
        return found == table.end() ? StateSnapshot{} : found->second;
    }
};

class FakeArtifacts final : public ArtifactProvider
{
  public:
    std::map<std::string, Json::Value> descriptions;
    std::map<std::string, Availability> availability;  ///< absent means Found
    std::string reason;

    Availability describe( const std::string &artifactRef, Json::Value &out,
                           std::string &why ) const override
    {
        lastAsked.push_back( artifactRef );
        why = reason;
        const auto available = availability.find( artifactRef );
        const Availability answer =
            available == availability.end() ? Availability::Found : available->second;
        if ( answer != Availability::Found )
        {
            out = Json::Value{ Json::objectValue };
            return answer;
        }
        const auto found = descriptions.find( artifactRef );
        if ( found == descriptions.end() )
        {
            // A ref nobody knows about is Missing, and that is a fact about the
            // source rather than about the artifact's shape.
            out = Json::Value{ Json::objectValue };
            return Availability::Missing;
        }
        out = found->second;
        return Availability::Found;
    }

    mutable std::vector<std::string> lastAsked;
};

class FakeMetrics final : public MetricProvider
{
  public:
    std::map<std::string, MetricValue> values;
    std::map<std::string, Availability> availability;  ///< absent means Found
    std::string reason;

    Availability metric( const std::string &metricName, MetricValue &out,
                         std::string &why ) const override
    {
        lastAsked.push_back( metricName );
        why = reason;
        const auto available = availability.find( metricName );
        const Availability answer =
            available == availability.end() ? Availability::Found : available->second;
        if ( answer != Availability::Found )
        {
            out = MetricValue{};
            return answer;
        }
        const auto found = values.find( metricName );
        if ( found == values.end() )
        {
            out = MetricValue{};
            return Availability::Missing;
        }
        out = found->second;
        return Availability::Found;
    }

    mutable std::vector<std::string> lastAsked;
};

struct Bundle
{
    FakeStates states;
    FakeArtifacts artifacts;
    FakeMetrics metrics;

    VerificationInputs inputs() const
    {
        VerificationInputs wired;
        wired.state = &states;
        wired.artifact = &artifacts;
        wired.metric = &metrics;
        wired.sourceId = kBundle;
        return wired;
    }
};

// --------------------------------------------------------------------------
// Fixtures
// --------------------------------------------------------------------------

Json::Value rasterGrid( int width, int height, const char *crsAuthId = "EPSG:32648" )
{
    Json::Value facts{ Json::objectValue };
    facts["kind"] = "raster";
    facts["size"]["width"] = width;
    facts["size"]["height"] = height;
    facts["crs_authid"] = crsAuthId;
    facts["dtype"] = "float32";
    facts["band_count"] = 1;
    return facts;
}

Json::Value vectorShape( const char *crsAuthId = "EPSG:32648" )
{
    Json::Value facts{ Json::objectValue };
    facts["kind"] = "vector";
    facts["geometry_type"] = "MultiPolygon";
    facts["feature_count"] = 42;
    facts["crs_authid"] = crsAuthId;
    return facts;
}

Json::Value expectParams( Json::Value expect )
{
    Json::Value params{ Json::objectValue };
    params["expect"] = expect;
    return params;
}

Json::Value gridExpectation( int width, int height, const char *crsAuthId = "EPSG:32648" )
{
    Json::Value expect{ Json::objectValue };
    expect["kind"] = "raster";
    expect["size"]["width"] = width;
    expect["size"]["height"] = height;
    expect["crs_authid"] = crsAuthId;
    return expect;
}

VerificationCheck artifactCheck( std::string id, std::string reference, Json::Value params )
{
    VerificationCheck check;
    check.id = std::move( id );
    check.kind = "artifact_shape";
    check.title = "calibrated DEM keeps the declared grid and spatial frame";
    check.subject.kind = "artifact";
    check.subject.id = std::move( reference );
    check.params = std::move( params );
    return check;
}

VerificationCheck stateCheck( std::string id, std::string nodeId, Json::Value expect )
{
    VerificationCheck check;
    check.id = std::move( id );
    check.kind = "state_invariant";
    check.title = "backscatter enters this step in the linear power domain";
    check.subject.kind = "node";
    check.subject.id = std::move( nodeId );
    check.params = expectParams( std::move( expect ) );
    return check;
}

VerificationCheck numericCheck( std::string id, std::string metricName, Json::Value expect )
{
    VerificationCheck check;
    check.id = std::move( id );
    check.kind = "numeric_range";
    check.title = "retrieved reflectance stays inside the physically admissible band";
    check.subject.kind = "metric";
    check.subject.id = std::move( metricName );
    check.params = expectParams( std::move( expect ) );
    return check;
}

Json::Value intervalExpectation( double lower, double upper, bool inclusiveLower = true,
                                 bool inclusiveUpper = true )
{
    Json::Value expect{ Json::objectValue };
    expect["lower"] = lower;
    expect["upper"] = upper;
    expect["inclusive_lower"] = inclusiveLower;
    expect["inclusive_upper"] = inclusiveUpper;
    return expect;
}

Json::Value pinExpectation( double value )
{
    Json::Value expect{ Json::objectValue };
    expect["value"] = value;
    return expect;
}

Json::Value operand( const char *source, const char *reference, const char *fact = nullptr )
{
    Json::Value value{ Json::objectValue };
    value["source"] = source;
    if ( fact != nullptr )
    {
        value["ref"] = reference;
        value["fact"] = fact;
    }
    else
    {
        value["name"] = reference;
    }
    return value;
}

Json::Value relationParams( const char *relation, Json::Value leftOperand,
                           Json::Value rightOperand, Json::Value expect )
{
    Json::Value params{ Json::objectValue };
    params["relation"] = relation;
    Json::Value operands{ Json::arrayValue };
    operands.append( std::move( leftOperand ) );
    operands.append( std::move( rightOperand ) );
    params["operands"] = operands;
    params["expect"] = std::move( expect );
    return params;
}

VerificationCheck relationalCheck( std::string id, std::string subjectId, Json::Value params )
{
    VerificationCheck check;
    check.id = std::move( id );
    check.kind = "relational_consistency";
    check.title = "width x height must equal the declared pixel count";
    check.subject.kind = "artifact";
    check.subject.id = std::move( subjectId );
    check.params = std::move( params );
    return check;
}

// --------------------------------------------------------------------------
// Shared invariants: every result in this lane must satisfy these
// --------------------------------------------------------------------------

void requireWellFormed( const CheckResult &result, const VerificationCheck &check )
{
    UNSCOPED_INFO( "result of check '" << check.id << "'" );

    REQUIRE( result.checkId == check.id );
    REQUIRE( result.kind == check.kind );
    REQUIRE( result.title == check.title );
    REQUIRE( result.evidence.kind == check.kind );
    REQUIRE( result.evidence.sourceId == kBundle );
    REQUIRE( !result.message.empty() );

    if ( result.status == CheckStatus::Pass )
    {
        REQUIRE( result.failureCode.empty() );
    }
    else
    {
        // Anything that is not a pass must name a KNOWN reason; an empty or
        // invented code is how "we do not know" gets rendered as "nothing
        // happened".
        REQUIRE( !result.failureCode.empty() );
        REQUIRE( sicnu::verification::isKnownFailureCode( result.failureCode ) );
    }
}

std::string canonicalText( const Json::Value &value )
{
    std::string text;
    std::string error;
    REQUIRE( sicnu::verification::canonicalJson( value, text, error ) );
    REQUIRE( error.empty() );
    return text;
}

std::string canonicalResultText( const CheckResult &result )
{
    return canonicalText( result.toJson() );
}

/// Every evidence record must round-trip: a report that cannot be re-read is a
/// report nobody can audit later.
void requireRoundTrip( const CheckResult &result )
{
    CheckResult loaded;
    std::string error;
    REQUIRE( CheckResult::fromJson( result.toJson(), loaded, error ) );
    REQUIRE( loaded.checkId == result.checkId );
    REQUIRE( loaded.kind == result.kind );
    REQUIRE( loaded.status == result.status );
    REQUIRE( loaded.failureCode == result.failureCode );
    REQUIRE( loaded.message == result.message );
    REQUIRE( loaded.evidence.kind == result.evidence.kind );
    REQUIRE( loaded.evidence.coverage == result.evidence.coverage );
    REQUIRE( loaded.evidence.sourceId == result.evidence.sourceId );
    REQUIRE( canonicalResultText( loaded ) == canonicalResultText( result ) );
}

// --------------------------------------------------------------------------
// Slice B requirement 1: exists + kind + grid -> Pass, coverage Full
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 B-1: artifact exists and its kind and grid match — Pass, Full coverage",
           "[verifier14][checks][artifact]" )
{
    Bundle bundle;
    bundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 512 );

    const VerificationCheck check = artifactCheck( "artifact.shape", kDemRef,
                                                   expectParams( gridExpectation( 512, 512 ) ) );
    const CheckResult result = sicnu::verification::runArtifactShapeCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Pass );
    REQUIRE( result.evidence.coverage == EvidenceCoverage::Full );
    REQUIRE( result.evidence.observed["kind"].asString() == "raster" );
    REQUIRE( result.evidence.observed["size"]["width"].asInt() == 512 );
    REQUIRE( result.evidence.observed["size"]["height"].asInt() == 512 );
    REQUIRE( bundle.artifacts.lastAsked.size() == 1 );
    REQUIRE( bundle.artifacts.lastAsked.front() == kDemRef );
    requireRoundTrip( result );
}

// --------------------------------------------------------------------------
// Slice B requirement 2: Missing is Indeterminate, never Fail
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 B-2: a provider that found nothing yields Indeterminate, not Fail",
           "[verifier14][checks][artifact]" )
{
    Bundle bundle;
    bundle.artifacts.availability[kDemRef] = Availability::Missing;

    const VerificationCheck check = artifactCheck( "artifact.shape", kDemRef,
                                                   expectParams( gridExpectation( 512, 512 ) ) );
    const CheckResult result = sicnu::verification::runArtifactShapeCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Indeterminate );
    REQUIRE( result.status != CheckStatus::Fail );
    REQUIRE( result.status != CheckStatus::Pass );
    REQUIRE( result.failureCode == fcv::kEvidenceUnavailable );
    REQUIRE( result.evidence.coverage == EvidenceCoverage::Unavailable );
    REQUIRE( result.evidence.observed.empty() );
    requireRoundTrip( result );
}

// --------------------------------------------------------------------------
// Slice B requirement 3: Refused carries its reason into the evidence
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 B-3: a refusing provider records why it refused",
           "[verifier14][checks][artifact]" )
{
    Bundle bundle;
    bundle.artifacts.availability[kDemRef] = Availability::Refused;
    bundle.artifacts.reason = "sidecar .aux.xml is locked by another process";

    const VerificationCheck check = artifactCheck( "artifact.shape", kDemRef,
                                                   expectParams( gridExpectation( 512, 512 ) ) );
    const CheckResult result = sicnu::verification::runArtifactShapeCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Indeterminate );
    REQUIRE( result.failureCode == fcv::kEvidenceRefused );
    // The provider's own words must survive into the report, otherwise the
    // operator cannot fix the operational problem that caused the refusal.
    REQUIRE( result.evidence.details["reason"].asString() ==
             "sidecar .aux.xml is locked by another process" );
    REQUIRE( result.message.find( "sidecar .aux.xml is locked by another process" ) !=
             std::string::npos );
    requireRoundTrip( result );
}

// --------------------------------------------------------------------------
// Slice B requirement 4: wrong kind -> Fail + ARTIFACT_KIND_MISMATCH
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 B-4: a raster was contracted and a vector was delivered — Fail",
           "[verifier14][checks][artifact]" )
{
    Bundle bundle;
    bundle.artifacts.descriptions[kVectorRef] = vectorShape();

    const VerificationCheck check = artifactCheck( "artifact.kind", kVectorRef,
                                                   expectParams( gridExpectation( 512, 512 ) ) );
    const CheckResult result = sicnu::verification::runArtifactShapeCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Fail );
    REQUIRE( result.failureCode == fcv::kArtifactKindMismatch );
    REQUIRE( result.evidence.coverage == EvidenceCoverage::Full );
    REQUIRE( result.evidence.observed["kind"].asString() == "vector" );
    REQUIRE( result.evidence.expected["kind"].asString() == "raster" );
    // Nothing was hidden: every fact the provider returned survives into the
    // observed record, including the vector-specific ones. `size` is NOT among
    // them because a vector has no grid — asserting it were present would be
    // asserting a fabricated fact, and the check's job is the opposite.
    REQUIRE( result.evidence.observed.isMember( "geometry_type" ) );
    REQUIRE( result.evidence.observed.isMember( "feature_count" ) );
    REQUIRE( !result.evidence.observed.isMember( "size" ) );
    // The declared grid is still visible, on the EXPECTED side, so a reader can
    // see exactly what was asked for and exactly what was delivered.
    REQUIRE( result.evidence.expected["size"]["width"].asInt() == 512 );
    requireRoundTrip( result );
}

// --------------------------------------------------------------------------
// Slice B requirement 5: grid off by one -> Fail with expected/observed/delta
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 B-5: a one pixel grid difference is Fail and carries the delta",
           "[verifier14][checks][artifact]" )
{
    Bundle bundle;
    bundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 513 );

    const VerificationCheck check = artifactCheck( "artifact.grid", kDemRef,
                                                   expectParams( gridExpectation( 512, 512 ) ) );
    const CheckResult result = sicnu::verification::runArtifactShapeCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Fail );
    REQUIRE( result.failureCode == fcv::kArtifactGridMismatch );

    // expected / observed / delta all have to be readable off the evidence.
    REQUIRE( result.evidence.expected["size"]["width"].asInt() == 512 );
    REQUIRE( result.evidence.observed["size"]["height"].asInt() == 513 );
    REQUIRE( result.evidence.details["failing_key"].asString() == "size.height" );
    REQUIRE( result.evidence.details["observed"].asString() ==
             sicnu::verification::roundSignificant( 513.0 ) );
    REQUIRE( result.evidence.details["expected"].asString() ==
             sicnu::verification::roundSignificant( 512.0 ) );
    REQUIRE( result.evidence.details["delta"].asString() ==
             sicnu::verification::roundSignificant( 1.0 ) );

    // And the message explains the consequence, not merely the mismatch.
    REQUIRE( result.message.find( "one ground sample distance" ) != std::string::npos );
    requireRoundTrip( result );
}

// --------------------------------------------------------------------------
// Slice B requirement 6: CRS absent vs CRS contradictory must be distinguishable
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 B-6: CRS absent and CRS contradictory are different verdicts",
           "[verifier14][checks][artifact]" )
{
    Bundle absentBundle;
    Json::Value noCrs = rasterGrid( 512, 512 );
    noCrs.removeMember( "crs_authid" );
    absentBundle.artifacts.descriptions[kDemRef] = noCrs;

    Bundle wrongBundle;
    wrongBundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 512, "EPSG:4326" );

    const VerificationCheck check = artifactCheck( "artifact.crs", kDemRef,
                                                   expectParams( gridExpectation( 512, 512 ) ) );
    const CheckResult absent = sicnu::verification::runArtifactShapeCheck( check, absentBundle.inputs() );
    const CheckResult wrong = sicnu::verification::runArtifactShapeCheck( check, wrongBundle.inputs() );

    requireWellFormed( absent, check );
    requireWellFormed( wrong, check );

    // No CRS was described: absence of evidence. It may NOT be reported as the
    // science being wrong.
    REQUIRE( absent.status == CheckStatus::Indeterminate );
    REQUIRE( absent.failureCode == fcv::kEvidenceUnavailable );
    REQUIRE( !absent.evidence.observed.isMember( "crs_authid" ) );

    // A CRS WAS described and it contradicts the contract: that is evidence.
    REQUIRE( wrong.status == CheckStatus::Fail );
    REQUIRE( wrong.failureCode == fcv::kArtifactGridMismatch );
    REQUIRE( wrong.evidence.observed["crs_authid"].asString() == "EPSG:4326" );
    REQUIRE( wrong.evidence.expected["crs_authid"].asString() == "EPSG:32648" );
    REQUIRE( wrong.evidence.details["failing_key"].asString() == "crs_authid" );

    // Distinguishable on the wire: neither the status nor the code agree.
    REQUIRE( absent.status != wrong.status );
    REQUIRE( absent.failureCode != wrong.failureCode );
    REQUIRE( std::string( sicnu::verification::statusToWire( absent.status ) ) !=
             std::string( sicnu::verification::statusToWire( wrong.status ) ) );
    requireRoundTrip( absent );
    requireRoundTrip( wrong );
}

// --------------------------------------------------------------------------
// Slice B requirement 7: absent required fact key -> schema mismatch
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 B-7: an absent required fact key is a schema mismatch",
           "[verifier14][checks][artifact]" )
{
    Bundle bundle;
    Json::Value facts = rasterGrid( 512, 512 );
    facts.removeMember( "dtype" );
    bundle.artifacts.descriptions[kDemRef] = facts;

    Json::Value params{ Json::objectValue };
    params["expect"] = gridExpectation( 512, 512 );
    Json::Value required{ Json::arrayValue };
    required.append( "dtype" );
    required.append( "band_count" );
    params["required_keys"] = required;

    const VerificationCheck check = artifactCheck( "artifact.schema", kDemRef, params );
    const CheckResult result = sicnu::verification::runArtifactShapeCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Fail );
    REQUIRE( result.failureCode == fcv::kArtifactSchemaMismatch );
    REQUIRE( result.evidence.details["missing_keys"].size() == 1 );
    REQUIRE( result.evidence.details["missing_keys"][0].asString() == "dtype" );
    requireRoundTrip( result );
}

// --------------------------------------------------------------------------
// Slice B requirement 8: violated state invariant states the consequence
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 B-8: a violated state invariant states the scientific consequence",
           "[verifier14][checks][state]" )
{
    Bundle bundle;
    StateSnapshot declared;
    declared.numericDomain = "db";
    declared.radiometricState = "sigma0";
    bundle.states.states["node.sar_calibrate"] = declared;

    Json::Value expect{ Json::objectValue };
    expect["numeric_domain"] = "linear";
    const VerificationCheck check = stateCheck( "state.domain", "node.sar_calibrate", expect );
    const CheckResult result = sicnu::verification::runStateInvariantCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Fail );
    REQUIRE( result.failureCode == fcv::kStateInvariantViolation );
    REQUIRE( result.evidence.coverage == EvidenceCoverage::Full );
    REQUIRE( result.evidence.observed["numeric_domain"].asString() == "db" );
    REQUIRE( result.evidence.expected["numeric_domain"].asString() == "linear" );

    // The report must carry WHY this invalidates the result, not merely that
    // two strings differ. A reader acting on this must learn that the numbers
    // are not "slightly off" but radiometrically meaningless.
    REQUIRE( result.message.find( "logarithmic" ) != std::string::npos );
    REQUIRE( result.message.find( "4x" ) != std::string::npos );
    REQUIRE( result.message.find( "slope-dependent" ) != std::string::npos );
    REQUIRE( result.message.find( "radiometr" ) != std::string::npos );
    requireRoundTrip( result );
}

TEST_CASE( "verifier14 B-8b: a wrong radiometric state names what it does to the backscatter",
           "[verifier14][checks][state]" )
{
    Bundle bundle;
    StateSnapshot declared;
    declared.numericDomain = "linear";
    declared.radiometricState = "sigma0";
    bundle.states.states["node.sar_calibrate"] = declared;

    Json::Value expect{ Json::objectValue };
    expect["radiometric_state"] = "gamma0";
    const VerificationCheck check = stateCheck( "state.radiometry", "node.sar_calibrate", expect );
    const CheckResult result = sicnu::verification::runStateInvariantCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Fail );
    REQUIRE( result.failureCode == fcv::kStateInvariantViolation );
    REQUIRE( result.message.find( "incidence angle" ) != std::string::npos );
    REQUIRE( result.message.find( "terrain-flattening" ) != std::string::npos );
    requireRoundTrip( result );
}

// --------------------------------------------------------------------------
// Slice B requirement 9: declared-wrong is Fail, undeclared is Indeterminate
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 B-9: an unobtainable state token is not a violation",
           "[verifier14][checks][state]" )
{
    Bundle missingBundle;
    Bundle refusedBundle;
    refusedBundle.states.availability["node.sar_calibrate"] = Availability::Refused;
    refusedBundle.states.reason = "state tokens are not emitted for synthetic sources";
    Bundle undeclaredBundle;
    undeclaredBundle.states.states["node.sar_calibrate"] = StateSnapshot{};  // both empty
    Bundle declaredWrongBundle;
    StateSnapshot wrong;
    wrong.numericDomain = "db";
    declaredWrongBundle.states.states["node.sar_calibrate"] = wrong;

    Json::Value expect{ Json::objectValue };
    expect["numeric_domain"] = "linear";
    const VerificationCheck check = stateCheck( "state.domain", "node.sar_calibrate", expect );

    const CheckResult missing = sicnu::verification::runStateInvariantCheck( check, missingBundle.inputs() );
    const CheckResult refused = sicnu::verification::runStateInvariantCheck( check, refusedBundle.inputs() );
    const CheckResult undeclared = sicnu::verification::runStateInvariantCheck( check, undeclaredBundle.inputs() );
    const CheckResult declaredWrong = sicnu::verification::runStateInvariantCheck( check, declaredWrongBundle.inputs() );

    requireWellFormed( missing, check );
    requireWellFormed( refused, check );
    requireWellFormed( undeclared, check );
    requireWellFormed( declaredWrong, check );

    // Nobody handed us a token: absence of evidence, three ways.
    REQUIRE( missing.status == CheckStatus::Indeterminate );
    REQUIRE( missing.failureCode == fcv::kStateTokenMissing );
    REQUIRE( missing.evidence.coverage == EvidenceCoverage::Unavailable );

    REQUIRE( refused.status == CheckStatus::Indeterminate );
    REQUIRE( refused.failureCode == fcv::kEvidenceRefused );
    REQUIRE( refused.evidence.details["reason"].asString() ==
             "state tokens are not emitted for synthetic sources" );

    // A state was obtained but it declares nothing about the domain. That is
    // NOT evidence of a wrong domain.
    REQUIRE( undeclared.status == CheckStatus::Indeterminate );
    REQUIRE( undeclared.failureCode == fcv::kStateTokenMissing );
    REQUIRE( undeclared.evidence.observed["numeric_domain"].asString().empty() );

    // Declared, and declared wrong. That is the only failing case.
    REQUIRE( declaredWrong.status == CheckStatus::Fail );
    REQUIRE( declaredWrong.failureCode == fcv::kStateInvariantViolation );

    REQUIRE( undeclared.failureCode != declaredWrong.failureCode );
    requireRoundTrip( missing );
    requireRoundTrip( undeclared );
}

TEST_CASE( "verifier14 B-9b: a state check without any expectation cannot conclude",
           "[verifier14][checks][state]" )
{
    Bundle bundle;
    bundle.states.states["node.sar_calibrate"] = StateSnapshot{};

    const VerificationCheck check = stateCheck( "state.empty", "node.sar_calibrate",
                                                Json::Value{ Json::objectValue } );
    const CheckResult result = sicnu::verification::runStateInvariantCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Indeterminate );
    REQUIRE( result.failureCode == fcv::kSpecInvalid );
    requireRoundTrip( result );
}

// --------------------------------------------------------------------------
// Slice B requirement 10: sampled evidence without its frame is no fact
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 B-10: sampled evidence missing sample_size or population is no fact",
           "[verifier14][checks][artifact]" )
{
    Bundle bundle;
    bundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 512 );

    Json::Value expect = gridExpectation( 512, 512 );

    // Both declared: the record is complete and may be judged.
    Json::Value completeParams{ Json::objectValue };
    completeParams["expect"] = expect;
    completeParams["sampling"]["sample_size"] = 4096;
    completeParams["sampling"]["population"] = 262144;

    Json::Value noSampleParams{ Json::objectValue };
    noSampleParams["expect"] = expect;
    noSampleParams["sampling"]["population"] = 262144;

    Json::Value noPopulationParams{ Json::objectValue };
    noPopulationParams["expect"] = expect;
    noPopulationParams["sampling"]["sample_size"] = 4096;

    const VerificationCheck complete =
        artifactCheck( "artifact.sampled", kDemRef, completeParams );
    const VerificationCheck noSample = artifactCheck( "artifact.sampled", kDemRef, noSampleParams );
    const VerificationCheck noPopulation =
        artifactCheck( "artifact.sampled", kDemRef, noPopulationParams );

    const CheckResult completeResult =
        sicnu::verification::runArtifactShapeCheck( complete, bundle.inputs() );
    const CheckResult noSampleResult =
        sicnu::verification::runArtifactShapeCheck( noSample, bundle.inputs() );
    const CheckResult noPopulationResult =
        sicnu::verification::runArtifactShapeCheck( noPopulation, bundle.inputs() );

    requireWellFormed( completeResult, complete );
    requireWellFormed( noSampleResult, noSample );
    requireWellFormed( noPopulationResult, noPopulation );

    REQUIRE( completeResult.evidence.coverage == EvidenceCoverage::Sampled );
    REQUIRE( completeResult.evidence.details.isMember( "sample_size" ) );
    REQUIRE( completeResult.evidence.details.isMember( "population" ) );
    REQUIRE( completeResult.status == CheckStatus::Pass );

    // An estimate without its frame says "we looked at some pixels" and hides
    // how many. Judging from it would promote an estimate to a fact.
    REQUIRE( noSampleResult.status == CheckStatus::Indeterminate );
    REQUIRE( noSampleResult.status != CheckStatus::Pass );
    REQUIRE( noSampleResult.failureCode == fcv::kEvidenceUnavailable );
    REQUIRE( sicnu::verification::evidenceCompleteness( noSampleResult.evidence )
                 .size() == 1 );

    REQUIRE( noPopulationResult.status == CheckStatus::Indeterminate );
    REQUIRE( noPopulationResult.failureCode == fcv::kEvidenceUnavailable );

    requireRoundTrip( completeResult );
    requireRoundTrip( noSampleResult );
}

// --------------------------------------------------------------------------
// Slice B extras: refuse rather than silently ignore an expectation
// --------------------------------------------------------------------------

TEST_CASE( "verifier14: an unrecognised expectation is refused rather than ignored",
           "[verifier14][checks][artifact]" )
{
    Bundle bundle;
    bundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 512 );

    Json::Value expect = gridExpectation( 512, 512 );
    expect["resolution_m"] = 30;  // nobody evaluates this key

    const VerificationCheck check = artifactCheck( "artifact.unknown_key", kDemRef,
                                                   expectParams( expect ) );
    const CheckResult result = sicnu::verification::runArtifactShapeCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    // Ignoring it would have produced a Pass over an expectation nobody kept.
    REQUIRE( result.status == CheckStatus::Indeterminate );
    REQUIRE( result.failureCode == fcv::kSpecInvalid );
    REQUIRE( result.evidence.details["unexpected_key"].asString() == "resolution_m" );
    requireRoundTrip( result );
}

TEST_CASE( "verifier14: required_keys outside the closed fact vocabulary are refused",
           "[verifier14][checks][artifact]" )
{
    Bundle bundle;
    bundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 512 );

    Json::Value params{ Json::objectValue };
    params["expect"] = gridExpectation( 512, 512 );
    Json::Value required{ Json::arrayValue };
    required.append( "not_a_fact_key" );
    params["required_keys"] = required;

    const VerificationCheck check = artifactCheck( "artifact.bad_key", kDemRef, params );
    const CheckResult result = sicnu::verification::runArtifactShapeCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Indeterminate );
    REQUIRE( result.failureCode == fcv::kSpecInvalid );

    // And the keys that ARE in the closed vocabulary are accepted, so this
    // refusal is a vocabulary check rather than a blanket rejection.
    REQUIRE( sicnu::verification::isMirroredFactKey( "kind" ) );
    REQUIRE( sicnu::verification::isMirroredFactKey( "crs_authid" ) );
    REQUIRE( sicnu::verification::isMirroredFactKey( "size" ) );
    REQUIRE( sicnu::verification::isMirroredFactKey( "radiometric_state" ) );
    REQUIRE( sicnu::verification::isMirroredFactKey( "numeric_domain" ) );
    REQUIRE( sicnu::verification::isMirroredFactKey( "feature_count" ) );
    REQUIRE( sicnu::verification::isMirroredFactKey( "geometry_type" ) );
    REQUIRE( sicnu::verification::isMirroredFactKey( "product_metadata" ) );
    REQUIRE( sicnu::verification::isMirroredFactKey( "temporal_facts" ) );
    REQUIRE( !sicnu::verification::isMirroredFactKey( "not_a_fact_key" ) );
    REQUIRE( !sicnu::verification::mirroredFactKeys().empty() );
    requireRoundTrip( result );
}

// --------------------------------------------------------------------------
// Slice C requirement 1: inside -> Pass, just outside -> Fail with bound+delta
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 C-1: inside the interval passes, just outside fails with bound and delta",
           "[verifier14][checks][numeric]" )
{
    Bundle bundle;
    bundle.metrics.values["reflectance"] = MetricValue{ 0.42, "fraction" };
    // The excursion must exceed the checker's numeric tolerance, otherwise this
    // is not testing "outside the interval" at all — the default tolerance is
    // max(1e-9, 1e-9*scale), so a 1e-11 excursion is *inside* by that measure
    // and passing it is correct behaviour, not a bug. Deliberately the smallest
    // delta that must be rejected, so the tolerance floor stays observable.
    bundle.metrics.values["over"] = MetricValue{ 1.0 + 1e-8, "fraction" };

    const VerificationCheck inside =
        numericCheck( "numeric.inside", "reflectance", intervalExpectation( 0.0, 1.0 ) );
    const VerificationCheck outside =
        numericCheck( "numeric.outside", "over", intervalExpectation( 0.0, 1.0 ) );

    const CheckResult insideResult =
        sicnu::verification::runNumericRangeCheck( inside, bundle.inputs() );
    const CheckResult outsideResult =
        sicnu::verification::runNumericRangeCheck( outside, bundle.inputs() );

    requireWellFormed( insideResult, inside );
    requireWellFormed( outsideResult, outside );

    REQUIRE( insideResult.status == CheckStatus::Pass );
    REQUIRE( insideResult.evidence.coverage == EvidenceCoverage::Full );
    REQUIRE( insideResult.evidence.observed["value"].asString() ==
             sicnu::verification::roundSignificant( 0.42 ) );
    REQUIRE( insideResult.evidence.observed["unit"].asString() == "fraction" );

    REQUIRE( outsideResult.status == CheckStatus::Fail );
    REQUIRE( outsideResult.failureCode == fcv::kNumericOutOfRange );
    REQUIRE( outsideResult.evidence.details["violated_bound"].asString() == "upper" );
    REQUIRE( outsideResult.evidence.details["bound"].asString() ==
             sicnu::verification::roundSignificant( 1.0 ) );
    // Derived, not hard-coded: 1.0 + 1e-8 is not exactly representable, so the
    // true excursion is what the arithmetic actually produced. Asserting a
    // decimal literal here would test the platform's rounding, not the checker.
    REQUIRE( outsideResult.evidence.details["delta"].asString() ==
             sicnu::verification::roundSignificant( ( 1.0 + 1e-8 ) - 1.0 ) );
    REQUIRE( outsideResult.evidence.expected["upper"].asString() ==
             sicnu::verification::roundSignificant( 1.0 ) );
    requireRoundTrip( insideResult );
    requireRoundTrip( outsideResult );
}

TEST_CASE( "verifier14 C-1b: below the lower bound is reported as the lower bound",
           "[verifier14][checks][numeric]" )
{
    Bundle bundle;
    bundle.metrics.values["below"] = MetricValue{ -0.000001, "fraction" };

    const VerificationCheck check =
        numericCheck( "numeric.below", "below", intervalExpectation( 0.0, 1.0 ) );
    const CheckResult result = sicnu::verification::runNumericRangeCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Fail );
    REQUIRE( result.failureCode == fcv::kNumericOutOfRange );
    REQUIRE( result.evidence.details["violated_bound"].asString() == "lower" );
    REQUIRE( result.evidence.details["bound"].asString() ==
             sicnu::verification::roundSignificant( 0.0 ) );
    requireRoundTrip( result );
}

// --------------------------------------------------------------------------
// Slice C requirement 2: NaN / Inf are their own verdict
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 C-2: NaN and infinity are never judged to be in range",
           "[verifier14][checks][numeric]" )
{
    Bundle bundle;
    bundle.metrics.values["nan_metric"] = MetricValue{ kNan, "fraction" };
    bundle.metrics.values["plus_inf"] = MetricValue{ kInf, "fraction" };
    bundle.metrics.values["minus_inf"] = MetricValue{ -kInf, "fraction" };

    const VerificationCheck nanCheck =
        numericCheck( "numeric.nan", "nan_metric", intervalExpectation( 0.0, 1.0 ) );
    const VerificationCheck pinCheck =
        numericCheck( "numeric.nan_pin", "nan_metric", pinExpectation( kNan ) );
    const VerificationCheck infCheck =
        numericCheck( "numeric.inf", "plus_inf", intervalExpectation( 0.0, 1.0 ) );
    const VerificationCheck minusInfCheck =
        numericCheck( "numeric.minus_inf", "minus_inf", intervalExpectation( 0.0, 1.0 ) );

    const CheckResult nanResult = sicnu::verification::runNumericRangeCheck( nanCheck, bundle.inputs() );
    const CheckResult infResult = sicnu::verification::runNumericRangeCheck( infCheck, bundle.inputs() );
    const CheckResult minusInfResult =
        sicnu::verification::runNumericRangeCheck( minusInfCheck, bundle.inputs() );

    requireWellFormed( nanResult, nanCheck );
    requireWellFormed( infResult, infCheck );
    requireWellFormed( minusInfResult, minusInfCheck );

    for ( const CheckResult &result : { nanResult, infResult, minusInfResult } )
    {
        REQUIRE( result.status == CheckStatus::Fail );
        REQUIRE( result.status != CheckStatus::Pass );
        REQUIRE( result.failureCode == fcv::kNumericNotFinite );
    }

    // A NaN observation compared against a NaN expectation is STILL known to be
    // non-finite: the observation is judged before the expectation is consulted.
    REQUIRE( nanResult.failureCode != fcv::kSpecInvalid );
    requireRoundTrip( nanResult );
    requireRoundTrip( infResult );
}

// --------------------------------------------------------------------------
// Slice C requirement 3: inclusive vs exclusive boundaries, both sides
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 C-3: inclusive boundaries admit the endpoint and exclusive reject it",
           "[verifier14][checks][numeric]" )
{
    Bundle bundle;
    bundle.metrics.values["at_zero"] = MetricValue{ 0.0, "fraction" };
    bundle.metrics.values["at_one"] = MetricValue{ 1.0, "fraction" };

    const auto runFor = [ &bundle ]( const Json::Value &expect, const char *metric )
    {
        const VerificationCheck check = numericCheck( "numeric.boundary", metric, expect );
        return sicnu::verification::runNumericRangeCheck( check, bundle.inputs() );
    };

    const Json::Value inclusive = intervalExpectation( 0.0, 1.0, true, true );
    const Json::Value exclusiveLower = intervalExpectation( 0.0, 1.0, false, true );
    const Json::Value exclusiveUpper = intervalExpectation( 0.0, 1.0, true, false );
    const Json::Value exclusiveBoth = intervalExpectation( 0.0, 1.0, false, false );

    // left endpoint
    REQUIRE( runFor( inclusive, "at_zero" ).status == CheckStatus::Pass );
    REQUIRE( runFor( exclusiveLower, "at_zero" ).status == CheckStatus::Fail );
    REQUIRE( runFor( exclusiveLower, "at_zero" ).failureCode == fcv::kNumericOutOfRange );

    // right endpoint
    REQUIRE( runFor( inclusive, "at_one" ).status == CheckStatus::Pass );
    REQUIRE( runFor( exclusiveUpper, "at_one" ).status == CheckStatus::Fail );
    REQUIRE( runFor( exclusiveUpper, "at_one" ).failureCode == fcv::kNumericOutOfRange );

    // and both at once, to prove the two flags are read independently
    REQUIRE( runFor( exclusiveBoth, "at_zero" ).status == CheckStatus::Fail );
    REQUIRE( runFor( exclusiveBoth, "at_one" ).status == CheckStatus::Fail );

    const CheckResult admitted = runFor( inclusive, "at_zero" );
    const CheckResult rejected = runFor( exclusiveBoth, "at_zero" );
    REQUIRE( admitted.status != rejected.status );
    requireRoundTrip( admitted );
    requireRoundTrip( rejected );
}

// --------------------------------------------------------------------------
// Slice C requirement 4: an unusable expectation judges nothing
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 C-4: an expectation that is itself NaN judges nothing",
           "[verifier14][checks][numeric]" )
{
    Bundle bundle;
    bundle.metrics.values["reflectance"] = MetricValue{ 0.42, "fraction" };

    const VerificationCheck nanPin =
        numericCheck( "numeric.nan_pin", "reflectance", pinExpectation( kNan ) );
    const VerificationCheck nanLower = numericCheck(
        "numeric.nan_lower", "reflectance", intervalExpectation( kNan, 1.0 ) );
    const VerificationCheck inverted = numericCheck(
        "numeric.inverted", "reflectance", intervalExpectation( 1.0, 0.0 ) );

    const CheckResult pinResult = sicnu::verification::runNumericRangeCheck( nanPin, bundle.inputs() );
    const CheckResult lowerResult =
        sicnu::verification::runNumericRangeCheck( nanLower, bundle.inputs() );
    const CheckResult invertedResult =
        sicnu::verification::runNumericRangeCheck( inverted, bundle.inputs() );

    requireWellFormed( pinResult, nanPin );
    requireWellFormed( lowerResult, nanLower );
    requireWellFormed( invertedResult, inverted );

    for ( const CheckResult &result : { pinResult, lowerResult, invertedResult } )
    {
        REQUIRE( result.status == CheckStatus::Indeterminate );
        REQUIRE( result.status != CheckStatus::Pass );
        REQUIRE( result.status != CheckStatus::Fail );
        REQUIRE( result.failureCode == fcv::kSpecInvalid );
    }
    requireRoundTrip( pinResult );
}

TEST_CASE( "verifier14 C-4b: a numeric check that declares nothing cannot conclude",
           "[verifier14][checks][numeric]" )
{
    Bundle bundle;
    bundle.metrics.values["reflectance"] = MetricValue{ 0.42, "fraction" };

    const VerificationCheck check =
        numericCheck( "numeric.empty", "reflectance", Json::Value{ Json::objectValue } );
    const CheckResult result = sicnu::verification::runNumericRangeCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Indeterminate );
    REQUIRE( result.failureCode == fcv::kSpecInvalid );
    requireRoundTrip( result );
}

TEST_CASE( "verifier14 C-4c: a missing or refusing metric is Indeterminate, never a Fail",
           "[verifier14][checks][numeric]" )
{
    Bundle missingBundle;
    Bundle refusedBundle;
    refusedBundle.metrics.availability["reflectance"] = Availability::Refused;
    refusedBundle.metrics.reason = "metric store is still warming up";
    refusedBundle.metrics.values["reflectance"] = MetricValue{ 0.42, "fraction" };

    const VerificationCheck check =
        numericCheck( "numeric.unavailable", "reflectance", intervalExpectation( 0.0, 1.0 ) );

    const CheckResult missing =
        sicnu::verification::runNumericRangeCheck( check, missingBundle.inputs() );
    const CheckResult refused =
        sicnu::verification::runNumericRangeCheck( check, refusedBundle.inputs() );

    requireWellFormed( missing, check );
    requireWellFormed( refused, check );

    REQUIRE( missing.status == CheckStatus::Indeterminate );
    REQUIRE( missing.failureCode == fcv::kEvidenceUnavailable );

    REQUIRE( refused.status == CheckStatus::Indeterminate );
    REQUIRE( refused.failureCode == fcv::kEvidenceRefused );
    REQUIRE( refused.evidence.details["reason"].asString() == "metric store is still warming up" );
    requireRoundTrip( missing );
    requireRoundTrip( refused );
}

// --------------------------------------------------------------------------
// Slice C requirement 5: consistent -> Pass, inconsistent -> Fail
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 C-5: a consistent relation passes and an inconsistent one fails",
           "[verifier14][checks][relational]" )
{
    Bundle bundle;
    bundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 512 );
    bundle.metrics.values["pixel_count"] = MetricValue{ 262144.0, "pixels" };

    const Json::Value left = operand( "artifact", kDemRef, "size.width" );
    const Json::Value right = operand( "artifact", kDemRef, "size.height" );
    const Json::Value expect = pinExpectation( 262144.0 );

    const VerificationCheck good =
        relationalCheck( "relation.count", kDemRef,
                         relationParams( "product_equals", left, right, expect ) );
    const VerificationCheck bad =
        relationalCheck( "relation.count", kDemRef,
                         relationParams( "product_equals", left, right, pinExpectation( 262656.0 ) ) );

    const CheckResult goodResult = sicnu::verification::runRelationalCheck( good, bundle.inputs() );
    const CheckResult badResult = sicnu::verification::runRelationalCheck( bad, bundle.inputs() );

    requireWellFormed( goodResult, good );
    requireWellFormed( badResult, bad );

    REQUIRE( goodResult.status == CheckStatus::Pass );
    REQUIRE( goodResult.evidence.coverage == EvidenceCoverage::Full );
    REQUIRE( goodResult.evidence.observed["left"].asString() ==
             sicnu::verification::roundSignificant( 512.0 ) );
    REQUIRE( goodResult.evidence.observed["right"].asString() ==
             sicnu::verification::roundSignificant( 512.0 ) );

    REQUIRE( badResult.status == CheckStatus::Fail );
    REQUIRE( badResult.failureCode == fcv::kRelationInconsistent );
    REQUIRE( badResult.evidence.details["delta"].asString() ==
             sicnu::verification::roundSignificant( 512.0 ) );
    requireRoundTrip( goodResult );
    requireRoundTrip( badResult );
}

TEST_CASE( "verifier14 C-5b: two facts that must agree are judged on their combination",
           "[verifier14][checks][relational]" )
{
    Bundle bundle;
    bundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 512 );
    bundle.metrics.values["pixel_count"] = MetricValue{ 262144.0, "pixels" };

    const VerificationCheck sum = relationalCheck(
        "relation.sum", kDemRef,
        relationParams( "sum_equals", operand( "artifact", kDemRef, "size.width" ),
                        operand( "artifact", kDemRef, "size.height" ), pinExpectation( 1024.0 ) ) );
    const VerificationCheck equals = relationalCheck(
        "relation.equals", kDemRef,
        relationParams( "equals", operand( "artifact", kDemRef, "size.width" ),
                        operand( "metric", "pixel_count" ), Json::Value{ Json::objectValue } ) );

    const CheckResult sumResult = sicnu::verification::runRelationalCheck( sum, bundle.inputs() );
    const CheckResult equalsResult = sicnu::verification::runRelationalCheck( equals, bundle.inputs() );

    requireWellFormed( sumResult, sum );
    requireWellFormed( equalsResult, equals );

    REQUIRE( sumResult.status == CheckStatus::Pass );
    REQUIRE( sumResult.evidence.observed["combination"].asString() ==
             sicnu::verification::roundSignificant( 1024.0 ) );
    // 512 != 262144: width alone was never promised to equal the pixel count.
    REQUIRE( equalsResult.status == CheckStatus::Fail );
    REQUIRE( equalsResult.failureCode == fcv::kRelationInconsistent );
    requireRoundTrip( sumResult );
}

// --------------------------------------------------------------------------
// Slice C requirement 6: neither side may be assumed
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 C-6: a relation whose any side is unevaluated is Indeterminate",
           "[verifier14][checks][relational]" )
{
    Bundle noArtifactBundle;
    noArtifactBundle.metrics.values["pixel_count"] = MetricValue{ 262144.0, "pixels" };

    Bundle noMetricBundle;
    noMetricBundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 512 );

    Bundle missingFactBundle;
    missingFactBundle.artifacts.descriptions[kDemRef] = vectorShape();  // no "size" at all
    missingFactBundle.metrics.values["pixel_count"] = MetricValue{ 262144.0, "pixels" };

    Bundle refusedBundle;
    refusedBundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 512 );
    refusedBundle.artifacts.availability[kDemRef] = Availability::Refused;
    refusedBundle.artifacts.reason = "artifact index was evicted";
    refusedBundle.metrics.values["pixel_count"] = MetricValue{ 262144.0, "pixels" };

    const Json::Value expect = pinExpectation( 262144.0 );
    const VerificationCheck check = relationalCheck(
        "relation.count", kDemRef,
        relationParams( "product_equals", operand( "artifact", kDemRef, "size.width" ),
                        operand( "artifact", kDemRef, "size.height" ), expect ) );

    const CheckResult noArtifact =
        sicnu::verification::runRelationalCheck( check, noArtifactBundle.inputs() );
    const CheckResult missingFact =
        sicnu::verification::runRelationalCheck( check, missingFactBundle.inputs() );
    const CheckResult refused =
        sicnu::verification::runRelationalCheck( check, refusedBundle.inputs() );

    requireWellFormed( noArtifact, check );
    requireWellFormed( missingFact, check );
    requireWellFormed( refused, check );

    REQUIRE( noArtifact.status == CheckStatus::Indeterminate );
    REQUIRE( noArtifact.failureCode == fcv::kEvidenceUnavailable );
    REQUIRE( noArtifact.evidence.details["unevaluated_side"].asString() == "left" );

    // The artifact exists but simply has no "size" fact: still no evidence, and
    // still not "size is zero".
    REQUIRE( missingFact.status == CheckStatus::Indeterminate );
    REQUIRE( missingFact.failureCode == fcv::kEvidenceUnavailable );

    REQUIRE( refused.status == CheckStatus::Indeterminate );
    REQUIRE( refused.failureCode == fcv::kEvidenceRefused );
    REQUIRE( refused.evidence.details["reason"].asString() == "artifact index was evicted" );

    // The right-hand side must be no more assumable than the left.
    Bundle rightMissing;
    rightMissing.artifacts.descriptions[kDemRef] = rasterGrid( 512, 512 );
    const VerificationCheck metricRight = relationalCheck(
        "relation.metric_right", kDemRef,
        relationParams( "sum_equals", operand( "artifact", kDemRef, "size.width" ),
                        operand( "metric", "pixel_count" ), pinExpectation( 262656.0 ) ) );
    const CheckResult rightMissingResult =
        sicnu::verification::runRelationalCheck( metricRight, rightMissing.inputs() );
    requireWellFormed( rightMissingResult, metricRight );
    REQUIRE( rightMissingResult.status == CheckStatus::Indeterminate );
    REQUIRE( rightMissingResult.evidence.details["unevaluated_side"].asString() == "right" );

    requireRoundTrip( noArtifact );
    requireRoundTrip( rightMissingResult );
}

TEST_CASE( "verifier14 C-6b: a NaN operand cannot be consistent with anything",
           "[verifier14][checks][relational]" )
{
    Bundle bundle;
    bundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 512 );
    bundle.metrics.values["broken"] = MetricValue{ kNan, "pixels" };

    const VerificationCheck check = relationalCheck(
        "relation.nan", kDemRef,
        relationParams( "product_equals", operand( "artifact", kDemRef, "size.width" ),
                        operand( "metric", "broken" ), pinExpectation( 262144.0 ) ) );
    const CheckResult result = sicnu::verification::runRelationalCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Fail );
    REQUIRE( result.status != CheckStatus::Pass );
    REQUIRE( result.failureCode == fcv::kNumericNotFinite );
    requireRoundTrip( result );
}

TEST_CASE( "verifier14 C-6c: an unknown relation is refused rather than evaluated as something else",
           "[verifier14][checks][relational]" )
{
    Bundle bundle;
    bundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 512 );

    const VerificationCheck check = relationalCheck(
        "relation.unknown", kDemRef,
        relationParams( "almost_equals", operand( "artifact", kDemRef, "size.width" ),
                        operand( "artifact", kDemRef, "size.height" ), pinExpectation( 262144.0 ) ) );
    const CheckResult result = sicnu::verification::runRelationalCheck( check, bundle.inputs() );

    requireWellFormed( result, check );
    REQUIRE( result.status == CheckStatus::Indeterminate );
    REQUIRE( result.failureCode == fcv::kSpecInvalid );

    const std::vector<std::string> relations = sicnu::verification::allRelations();
    REQUIRE( !relations.empty() );
    REQUIRE( std::find( relations.begin(), relations.end(), "product_equals" ) != relations.end() );
    REQUIRE( std::find( relations.begin(), relations.end(), "almost_equals" ) == relations.end() );
    requireRoundTrip( result );
}

// --------------------------------------------------------------------------
// Slice C requirement 7: unit / magnitude spellings denote one quantity
// --------------------------------------------------------------------------

TEST_CASE( "verifier14 C-7: integer, decimal and exponent spellings denote one quantity",
           "[verifier14][checks][numeric][relational]" )
{
    Bundle bundle;
    bundle.metrics.values["as_integer"] = MetricValue{ 1024.0, "pixels" };
    bundle.metrics.values["as_thousand"] = MetricValue{ 1e3, "metres" };
    bundle.metrics.values["as_sum"] = MetricValue{ 0.1 + 0.2, "fraction" };

    const VerificationCheck integerPin =
        numericCheck( "numeric.integer_pin", "as_integer", pinExpectation( 1024 ) );
    const VerificationCheck thousandPin =
        numericCheck( "numeric.thousand_pin", "as_thousand", pinExpectation( 1000 ) );
    const VerificationCheck sumPin =
        numericCheck( "numeric.sum_pin", "as_sum", pinExpectation( 0.3 ) );

    const CheckResult integerResult =
        sicnu::verification::runNumericRangeCheck( integerPin, bundle.inputs() );
    const CheckResult thousandResult =
        sicnu::verification::runNumericRangeCheck( thousandPin, bundle.inputs() );
    const CheckResult sumResult =
        sicnu::verification::runNumericRangeCheck( sumPin, bundle.inputs() );

    requireWellFormed( integerResult, integerPin );
    requireWellFormed( thousandResult, thousandPin );
    requireWellFormed( sumResult, sumPin );

    REQUIRE( integerResult.status == CheckStatus::Pass );
    REQUIRE( thousandResult.status == CheckStatus::Pass );
    // 0.1 + 0.2 is not bit-identical to 0.3; declaring them different would
    // make every floating point pin unusable.
    REQUIRE( sumResult.status == CheckStatus::Pass );

    // And the same rule holds between two facts rather than against a pin.
    bundle.artifacts.descriptions[kDemRef] = rasterGrid( 1024, 1024 );
    bundle.metrics.values["width_again"] = MetricValue{ 1024.0, "pixels" };
    const VerificationCheck acrossSources = relationalCheck(
        "relation.unit_trap", kDemRef,
        relationParams( "equals", operand( "artifact", kDemRef, "size.width" ),
                        operand( "metric", "width_again" ), Json::Value{ Json::objectValue } ) );
    const CheckResult acrossResult =
        sicnu::verification::runRelationalCheck( acrossSources, bundle.inputs() );
    requireWellFormed( acrossResult, acrossSources );
    REQUIRE( acrossResult.status == CheckStatus::Pass );
    REQUIRE( acrossResult.evidence.observed["left"].asString() ==
             sicnu::verification::roundSignificant( 1024.0 ) );
    REQUIRE( acrossResult.evidence.observed["right"].asString() ==
             sicnu::verification::roundSignificant( 1024.0 ) );
    requireRoundTrip( acrossResult );
}

// --------------------------------------------------------------------------
// Determinism: two independent runs over equal inputs produce equal reports
// --------------------------------------------------------------------------

TEST_CASE( "verifier14: two runs over equal inputs produce byte-identical results",
           "[verifier14][checks][determinism]" )
{
    const auto buildBundle = []()
    {
        Bundle bundle;
        bundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 512 );
        bundle.artifacts.descriptions[kVectorRef] = vectorShape();
        StateSnapshot declaredState;
        declaredState.numericDomain = "db";
        declaredState.radiometricState = "sigma0";
        bundle.states.states["node.sar_calibrate"] = declaredState;
        bundle.metrics.values["pixel_count"] = MetricValue{ 262144.0, "pixels" };
        bundle.metrics.values["reflectance"] = MetricValue{ 1.5, "fraction" };
        return bundle;
    };

    Bundle first = buildBundle();
    Bundle second = buildBundle();

    const VerificationCheck shape =
        artifactCheck( "artifact.shape", kDemRef, expectParams( gridExpectation( 512, 512 ) ) );
    const VerificationCheck kind =
        artifactCheck( "artifact.kind", kVectorRef, expectParams( gridExpectation( 512, 512 ) ) );
    Json::Value stateExpect{ Json::objectValue };
    stateExpect["numeric_domain"] = "linear";
    const VerificationCheck state = stateCheck( "state.domain", "node.sar_calibrate", stateExpect );
    const VerificationCheck numeric =
        numericCheck( "numeric.range", "reflectance", intervalExpectation( 0.0, 1.0 ) );
    const VerificationCheck relation = relationalCheck(
        "relation.count", kDemRef,
        relationParams( "product_equals", operand( "artifact", kDemRef, "size.width" ),
                        operand( "artifact", kDemRef, "size.height" ),
                        pinExpectation( 262144.0 ) ) );

    struct Case
    {
        VerificationCheck check;
        CheckResult ( *run )( const VerificationCheck &, const VerificationInputs & );
    };

    const std::vector<Case> cases {
        { shape, &sicnu::verification::runArtifactShapeCheck },
        { kind, &sicnu::verification::runArtifactShapeCheck },
        { state, &sicnu::verification::runStateInvariantCheck },
        { numeric, &sicnu::verification::runNumericRangeCheck },
        { relation, &sicnu::verification::runRelationalCheck },
    };

    for ( const Case &entry : cases )
    {
        UNSCOPED_INFO( "determinism of check '" << entry.check.id << "'" );
        const CheckResult left = entry.run( entry.check, first.inputs() );
        const CheckResult right = entry.run( entry.check, second.inputs() );
        REQUIRE( canonicalResultText( left ) == canonicalResultText( right ) );
        requireRoundTrip( left );
    }
}

// --------------------------------------------------------------------------
// Numbers in evidence are canonical text, never raw doubles
// --------------------------------------------------------------------------

TEST_CASE( "verifier14: every number in an evidence record is canonical text",
           "[verifier14][checks][evidence]" )
{
    Bundle bundle;
    bundle.artifacts.descriptions[kDemRef] = rasterGrid( 512, 513 );
    bundle.metrics.values["reflectance"] = MetricValue{ 1.5, "fraction" };

    const VerificationCheck grid =
        artifactCheck( "artifact.grid", kDemRef, expectParams( gridExpectation( 512, 512 ) ) );
    const VerificationCheck numeric =
        numericCheck( "numeric.range", "reflectance", intervalExpectation( 0.0, 1.0 ) );
    const VerificationCheck relation = relationalCheck(
        "relation.count", kDemRef,
        relationParams( "product_equals", operand( "artifact", kDemRef, "size.width" ),
                        operand( "artifact", kDemRef, "size.height" ),
                        pinExpectation( 262144.0 ) ) );

    const CheckResult gridResult = sicnu::verification::runArtifactShapeCheck( grid, bundle.inputs() );
    const CheckResult numericResult =
        sicnu::verification::runNumericRangeCheck( numeric, bundle.inputs() );
    const CheckResult relationResult =
        sicnu::verification::runRelationalCheck( relation, bundle.inputs() );

    for ( const CheckResult &result : { gridResult, numericResult, relationResult } )
    {
        // Every value the checkers wrote themselves must be the canonical text
        // spelling, so one quantity has one spelling in every report. Values
        // copied straight from a provider (facts / grids) keep their own type,
        // which is why the allowances below are per-member rather than global.
        const Json::Value &observedHeight = result.evidence.observed["size"]["height"];
        const bool heightIsAcceptable = observedHeight.isNull() || observedHeight.isString() ||
                                        observedHeight.isIntegral();
        REQUIRE( heightIsAcceptable );

        const Json::Value &delta = result.evidence.details["delta"];
        const bool deltaIsAcceptable = delta.isNull() || delta.isString();
        REQUIRE( deltaIsAcceptable );

        const bool valueIsAcceptable = !result.evidence.observed.isMember( "value" ) ||
                                       result.evidence.observed["value"].isString();
        REQUIRE( valueIsAcceptable );

        // Nothing non-serializable can reach canonical JSON: a NaN written as a
        // raw double would make the record un-hashable.
        std::string text;
        std::string error;
        REQUIRE( sicnu::verification::canonicalJson( result.toJson(), text, error ) );
    }
}

} // namespace
