/***************************************************************************
  test_verifier_packs_14.cpp — Unified Scientific Verifier 14 (packs slice)

  The recurring theme of this lane is: SILENT MERGING IS FAILURE.

  Every case below targets a place where a careless implementation returns
  "nothing to complain about", which a downstream roll-up then reads as Pass:

    - overlapping check ids merged last-write-wins, with no record;
    - an unknown operator answered with an empty, ok pack, which rolls up to
      VERIFY.NO_CHECKS and must never be Pass;
    - an empty composition handed back as an empty vector that looks like
      "all zero expectations held".

  Qt-free, QGIS-free, filesystem-free: every fixture is built in memory, per
  the repo convention that lanes synthesize their own fixtures. The scientific
  contract registry is READ here and never mutated: it is a shared source.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "contracts/scientific_contract.h"
#include "verification/canonical_json.h"
#include "verification/failure_codes.h"
#include "verification/pack.h"
#include "verification/packs_builtin.h"
#include "verification/spec.h"
#include "verification/verification_types.h"

#include <json/json.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{

using sicnu::verification::CheckKind;
using sicnu::verification::ComposeOutcome;
using sicnu::verification::PackConflictPolicy;
using sicnu::verification::PackOverride;
using sicnu::verification::VerifierPack;
using sicnu::verification::VerificationCheck;
using sicnu::verification::VerificationSpec;

namespace failure_codes = sicnu::verification::failure_codes;

/// A check that records which pack declared it, so a merged result can be
/// traced back to a source even when two packs use the same id.
VerificationCheck tracingCheck( const std::string &id, const std::string &declaredBy )
{
    VerificationCheck check;
    check.id = id;
    check.kind = sicnu::verification::checkKindToWire( CheckKind::StateInvariant );
    check.title = id + " (declared by " + declaredBy + ")";
    check.subject = sicnu::verification::SubjectRef{ "task", declaredBy };
    check.params["declared_by"] = declaredBy;
    check.failureCode = failure_codes::kStateInvariantViolation;
    return check;
}

VerifierPack packOf( const std::string &packId, const std::vector<std::string> &checkIds )
{
    VerifierPack pack;
    pack.id = packId;
    pack.version = "1";
    for ( const std::string &checkId : checkIds )
    {
        pack.checks.push_back( tracingCheck( checkId, packId ) );
    }
    return pack;
}

std::vector<std::string> idsOf( const std::vector<VerificationCheck> &checks )
{
    std::vector<std::string> ids;
    ids.reserve( checks.size() );
    for ( const VerificationCheck &check : checks )
    {
        ids.push_back( check.id );
    }
    return ids;
}

const VerificationCheck *findCheck( const VerifierPack &pack, const std::string &id )
{
    for ( const VerificationCheck &check : pack.checks )
    {
        if ( check.id == id )
        {
            return &check;
        }
    }
    return nullptr;
}

/// The spec a composed check list is usually fed into. Identical for both
/// composition orders, so any digest difference can only come from the checks.
VerificationSpec specWithChecks( const std::vector<VerificationCheck> &checks )
{
    VerificationSpec spec;
    spec.specId = "rs14.composed";
    spec.specVersion = "1";
    spec.checks = checks;
    spec.requiredEvidence = { "artifact" };
    return spec;
}

bool inVocabulary( const std::vector<std::string> &vocabulary, const std::string &value )
{
    return std::find( vocabulary.begin(), vocabulary.end(), value ) != vocabulary.end();
}

} // namespace

TEST_CASE( "verifier14: disjoint packs add up and compose in a deterministic order", "[verifier14][packs]" )
{
    const VerifierPack alpha = packOf( "alpha", { "alpha.one", "alpha.two" } );
    const VerifierPack beta = packOf( "beta", { "beta.one", "beta.two", "beta.three" } );

    const ComposeOutcome forward = sicnu::verification::composePacks( { alpha, beta }, PackConflictPolicy::Reject );
    const ComposeOutcome backward = sicnu::verification::composePacks( { beta, alpha }, PackConflictPolicy::Reject );

    REQUIRE( forward.ok );
    REQUIRE( backward.ok );
    REQUIRE( forward.failureCode.empty() );
    REQUIRE( forward.duplicateIds.empty() );
    REQUIRE( forward.overrides.empty() );

    // Counts add: nothing was dropped on the way in.
    CHECK( forward.checks.size() == 5 );
    CHECK( backward.checks.size() == 5 );

    // Order is a property of PACK IDENTITY, not of the caller's listing: packs
    // sorted by id, declaration order preserved inside a pack.
    const std::vector<std::string> expected { "alpha.one", "alpha.two", "beta.one", "beta.two", "beta.three" };
    CHECK( idsOf( forward.checks ) == expected );
    CHECK( idsOf( backward.checks ) == expected );

    // The built-in packs are disjoint by construction, so they must compose
    // without a single conflict.
    const ComposeOutcome builtins = sicnu::verification::composePacks(
        { sicnu::verification::reproducibilityPack(), sicnu::verification::structuralPack(),
          sicnu::verification::provenancePack() },
        PackConflictPolicy::Reject );
    REQUIRE( builtins.ok );
    CHECK( builtins.checks.size() == sicnu::verification::structuralPack().checks.size()
           + sicnu::verification::provenancePack().checks.size()
           + sicnu::verification::reproducibilityPack().checks.size() );
    const std::vector<std::string> builtinOrder = idsOf( builtins.checks );
    REQUIRE_FALSE( builtinOrder.empty() );
    CHECK( builtinOrder.front() == sicnu::verification::provenancePack().checks.front().id );
    CHECK( builtinOrder.back() == sicnu::verification::structuralPack().checks.back().id );
}

TEST_CASE( "verifier14: overlapping check ids are refused instead of merged last-write-wins", "[verifier14][packs]" )
{
    const VerifierPack alpha = packOf( "alpha", { "shared", "alpha.only" } );
    const VerifierPack beta = packOf( "beta", { "shared", "beta.only" } );
    const VerifierPack zeta = packOf( "zeta", { "shared", "zeta.only" } );

    const ComposeOutcome outcome =
        sicnu::verification::composePacks( { zeta, beta, alpha }, PackConflictPolicy::Reject );

    // Fail closed: a conflict is a result, not a warning.
    REQUIRE_FALSE( outcome.ok );
    REQUIRE( outcome.failureCode == failure_codes::kSpecInvalid );
    REQUIRE_FALSE( outcome.reason.empty() );
    UNSCOPED_INFO( "reason: " << outcome.reason );

    CHECK( outcome.duplicateIds == std::vector<std::string>{ "shared" } );
    CHECK( outcome.reason.find( "shared" ) != std::string::npos );

    // The decisive assertion: a refused composition carries NO checks. A
    // half-merged set is exactly what a downstream roll-up would read as
    // "everything that survived passed".
    CHECK( outcome.checks.empty() );

    // And nothing was silently kept from a later pack either.
    CHECK( outcome.overrides.empty() );
}

TEST_CASE( "verifier14: Override keeps the first declaration and records every drop", "[verifier14][packs]" )
{
    const VerifierPack alpha = packOf( "alpha", { "shared", "alpha.only" } );
    const VerifierPack beta = packOf( "beta", { "shared", "beta.only" } );
    const VerifierPack zeta = packOf( "zeta", { "shared", "zeta.only" } );

    const ComposeOutcome outcome =
        sicnu::verification::composePacks( { zeta, beta, alpha }, PackConflictPolicy::Override );

    REQUIRE( outcome.ok );
    CHECK( outcome.failureCode.empty() );

    // One check per id, four distinct ids: two were dropped, not merged.
    CHECK( outcome.checks.size() == 4 );
    CHECK( idsOf( outcome.checks )
           == std::vector<std::string>{ "shared", "alpha.only", "beta.only", "zeta.only" } );

    // The survivor is the EARLIEST declaration in pack-id order, never the
    // last writer: params still name "alpha".
    const VerificationCheck *survivor = nullptr;
    for ( const VerificationCheck &check : outcome.checks )
    {
        if ( check.id == "shared" )
        {
            survivor = &check;
        }
    }
    REQUIRE( survivor != nullptr );
    CHECK( survivor->params["declared_by"].asString() == "alpha" );

    // Traceability: every drop names the id, what was kept and what went.
    CHECK( outcome.duplicateIds == std::vector<std::string>{ "shared" } );
    REQUIRE( outcome.overrides.size() == 2 );
    for ( const PackOverride &record : outcome.overrides )
    {
        CHECK( record.checkId == "shared" );
        CHECK( record.keptFrom == "alpha" );
        CHECK_FALSE( record.droppedFrom.empty() );
    }
    CHECK( outcome.overrides[0].droppedFrom == "beta" );
    CHECK( outcome.overrides[1].droppedFrom == "zeta" );
}

TEST_CASE( "verifier14: an empty composition is refused as no-checks, never a pass", "[verifier14][packs]" )
{
    const ComposeOutcome nothing = sicnu::verification::composePacks( {}, PackConflictPolicy::Reject );
    REQUIRE_FALSE( nothing.ok );
    CHECK( nothing.failureCode == failure_codes::kNoChecks );
    CHECK( nothing.checks.empty() );
    CHECK_FALSE( nothing.reason.empty() );

    VerifierPack empty;
    empty.id = "empty";
    const VerifierPack emptyPack = empty;
    const ComposeOutcome onlyEmpty = sicnu::verification::composePacks( { emptyPack }, PackConflictPolicy::Override );
    REQUIRE_FALSE( onlyEmpty.ok );
    CHECK( onlyEmpty.failureCode == failure_codes::kNoChecks );

    // An empty pack alongside a real one contributes nothing and is fine.
    const ComposeOutcome mixed =
        sicnu::verification::composePacks( { emptyPack, sicnu::verification::provenancePack() },
                                           PackConflictPolicy::Reject );
    REQUIRE( mixed.ok );
    CHECK( mixed.checks.size() == sicnu::verification::provenancePack().checks.size() );
}

TEST_CASE( "verifier14: a known operator derives at least three checks from its declared domains", "[verifier14][packs]" )
{
    const std::string operatorId = "rs:spectral_index";
    const sicnu::contracts::ScientificContract *contract =
        sicnu::contracts::findScientificContract( operatorId );
    REQUIRE( contract != nullptr );

    const sicnu::verification::DerivedPack derived = sicnu::verification::scientificContractFor( operatorId );

    REQUIRE_FALSE( derived.indeterminate );
    REQUIRE( derived.failureCode.empty() );
    REQUIRE( derived.reason.empty() );
    REQUIRE_FALSE( derived.pack.id.empty() );
    CHECK( derived.pack.derivedFrom == operatorId );

    // At least three: a state invariant over the declared domains is the
    // minimum, and scale/offset plus NoData are separate expectations.
    REQUIRE( derived.pack.checks.size() >= 3 );

    // Every derived kind must be one this build can actually evaluate: an
    // unknown kind here would be a silent Indeterminate masquerading as a
    // derivation.
    for ( const VerificationCheck &check : derived.pack.checks )
    {
        CheckKind kind{};
        CHECK( sicnu::verification::checkKindFromWire( check.kind, kind ) );
    }
}

TEST_CASE( "verifier14: an unknown operator is Indeterminate with a typed code, never an empty ok pack", "[verifier14][packs]" )
{
    const std::string operatorId = "rs:definitely_not_registered";

    // First establish the premise, so this case fails loudly if the id ever
    // becomes registered instead of quietly changing meaning.
    REQUIRE( sicnu::contracts::findScientificContract( operatorId ) == nullptr );

    const sicnu::verification::DerivedPack derived = sicnu::verification::scientificContractFor( operatorId );

    REQUIRE( derived.indeterminate );
    REQUIRE( derived.failureCode == failure_codes::kUnsupportedCheckKind );
    REQUIRE_FALSE( derived.reason.empty() );
    REQUIRE( derived.pack.derivedFrom == operatorId );

    // The decisive assertions. An empty pack would roll up to
    // VERIFY.NO_CHECKS, and NO_CHECKS must never be read as Pass — so the
    // absence has to be visible in the pack itself.
    REQUIRE_FALSE( derived.pack.checks.empty() );
    REQUIRE( derived.pack.checks.size() == 1 );

    const VerificationCheck &placeholder = derived.pack.checks.front();
    CHECK_FALSE( placeholder.id.empty() );
    CHECK( placeholder.failureCode == failure_codes::kUnsupportedCheckKind );

    // The placeholder is deliberately UNEVALUABLE: its kind is outside the
    // vocabulary, so no checker can turn it into Pass.
    CheckKind kind{};
    CHECK_FALSE( sicnu::verification::checkKindFromWire( placeholder.kind, kind ) );
    CHECK( placeholder.params["operator_id"].asString() == operatorId );

    // And it must not compose into an empty spec either.
    const ComposeOutcome composed = sicnu::verification::composePacks( { derived.pack }, PackConflictPolicy::Reject );
    REQUIRE( composed.ok );
    CHECK( composed.checks.size() == 1 );
}

TEST_CASE( "verifier14: derived checks carry the contract's own literals, not a second copy", "[verifier14][packs]" )
{
    // Four operators from four different families. If the derivation kept its
    // own table, these would not track the registry.
    const std::vector<std::string> operators { "rs:spectral_index", "rs:qa_mask",
                                               "rs:sar_calibrate", "rs:kmeans_classification" };

    std::set<std::string> outputDomainsSeen;

    for ( const std::string &operatorId : operators )
    {
        UNSCOPED_INFO( "operator: " << operatorId );
        const sicnu::contracts::ScientificContract *contract =
            sicnu::contracts::findScientificContract( operatorId );
        REQUIRE( contract != nullptr );

        const sicnu::verification::DerivedPack derived = sicnu::verification::scientificContractFor( operatorId );
        REQUIRE_FALSE( derived.indeterminate );

        const VerificationCheck *domains = findCheck( derived.pack, "contract.domain_transition" );
        const VerificationCheck *scale = findCheck( derived.pack, "contract.scale_offset" );
        const VerificationCheck *noData = findCheck( derived.pack, "contract.no_data" );
        const VerificationCheck *provenance = findCheck( derived.pack, "contract.provenance" );
        REQUIRE( domains != nullptr );
        REQUIRE( scale != nullptr );
        REQUIRE( noData != nullptr );
        REQUIRE( provenance != nullptr );

        // Same literals, same source — character for character.
        CHECK( domains->params["input_domain"].asString() == contract->inputDomain );
        CHECK( domains->params["output_domain"].asString() == contract->outputDomain );
        CHECK( domains->params["operator_id"].asString() == contract->operatorId );
        CHECK( scale->params["scale_offset"].asString() == contract->scaleOffset );
        CHECK( noData->params["no_data_policy"].asString() == contract->noDataPolicy );
        CHECK( provenance->params["provenance"].asString() == contract->provenance );

        // ...and they are the CONTRACT vocabulary, never the artifact-fact
        // vocabulary ("surface_reflectance"/"toa"/... belong to another layer
        // and must not be translated into here).
        CHECK_FALSE( contract->inputDomain.empty() );
        CHECK( inVocabulary( sicnu::contracts::kNumericDomains, contract->inputDomain ) );
        CHECK( inVocabulary( sicnu::contracts::kNumericDomains, contract->outputDomain ) );
        CHECK( inVocabulary( sicnu::contracts::kScaleOffsetPolicies, contract->scaleOffset ) );
        CHECK( inVocabulary( sicnu::contracts::kNoDataPolicies, contract->noDataPolicy ) );
        CHECK( inVocabulary( sicnu::contracts::kProvenanceExpectations, contract->provenance ) );

        outputDomainsSeen.insert( domains->params["output_domain"].asString() );
    }

    // A hardcoded copy would give every operator the same value. These four
    // are chosen because their declared output domains genuinely differ.
    CHECK( outputDomainsSeen.size() >= 3 );
}

TEST_CASE( "verifier14: a pack round-trips through JSON carrying its schema identity", "[verifier14][packs]" )
{
    const VerifierPack original = sicnu::verification::structuralPack();
    REQUIRE( original.schema == sicnu::verification::kVerifierPackSchema );

    const Json::Value json = original.toJson();
    REQUIRE( json["schema"].asString() == std::string( sicnu::verification::kVerifierPackSchema ) );
    REQUIRE( json["id"].asString() == original.id );
    REQUIRE( json["version"].asString() == original.version );
    REQUIRE( json["checks"].size() == static_cast<Json::ArrayIndex>( original.checks.size() ) );

    VerifierPack loaded;
    std::string error;
    REQUIRE( VerifierPack::fromJson( json, loaded, error ) );

    CHECK( loaded.schema == original.schema );
    CHECK( loaded.id == original.id );
    CHECK( loaded.version == original.version );
    CHECK( loaded.derivedFrom == original.derivedFrom );
    REQUIRE( loaded.checks.size() == original.checks.size() );
    for ( std::size_t index = 0; index < original.checks.size(); ++index )
    {
        CHECK( loaded.checks[index].id == original.checks[index].id );
        CHECK( loaded.checks[index].kind == original.checks[index].kind );
        CHECK( loaded.checks[index].failureCode == original.checks[index].failureCode );
    }

    // Same content => byte-identical canonical text, twice over.
    std::string firstRun;
    std::string secondRun;
    std::string canonicalError;
    REQUIRE( sicnu::verification::canonicalJson( loaded.toJson(), firstRun, canonicalError ) );
    REQUIRE( sicnu::verification::canonicalJson( original.toJson(), secondRun, canonicalError ) );
    CHECK( firstRun == secondRun );

    // A derived pack must survive the same trip.
    const sicnu::verification::DerivedPack derived = sicnu::verification::scientificContractFor( "rs:qa_mask" );
    VerifierPack reloadedDerived;
    REQUIRE( VerifierPack::fromJson( derived.pack.toJson(), reloadedDerived, error ) );
    CHECK( reloadedDerived.derivedFrom == "rs:qa_mask" );
    CHECK( reloadedDerived.checks.size() == derived.pack.checks.size() );

    // Foreign schemas are refused rather than reinterpreted.
    Json::Value foreign = json;
    foreign["schema"] = "exp.verification.pack.v2";
    VerifierPack refused;
    CHECK_FALSE( VerifierPack::fromJson( foreign, refused, error ) );
    CHECK_FALSE( error.empty() );

    // A pack whose checks collide is refused too: duplicate ids are a
    // composition decision, not something to launder through serialization.
    Json::Value colliding = json;
    REQUIRE( colliding["checks"].size() >= 2 );
    colliding["checks"][1]["id"] = colliding["checks"][0]["id"].asString();
    CHECK_FALSE( VerifierPack::fromJson( colliding, refused, error ) );
}

TEST_CASE( "verifier14: composition order does not change spec identity", "[verifier14][packs]" )
{
    const VerifierPack alpha = packOf( "alpha", { "alpha.one", "alpha.two" } );
    const VerifierPack beta = packOf( "beta", { "beta.one", "beta.two", "beta.three" } );

    const ComposeOutcome forward = sicnu::verification::composePacks( { alpha, beta }, PackConflictPolicy::Reject );
    const ComposeOutcome backward = sicnu::verification::composePacks( { beta, alpha }, PackConflictPolicy::Reject );
    REQUIRE( forward.ok );
    REQUIRE( backward.ok );

    // The composed vectors themselves are byte-for-byte the same, in the same
    // order — not merely equal as sets.
    CHECK( idsOf( forward.checks ) == idsOf( backward.checks ) );

    const VerificationSpec specForward = specWithChecks( forward.checks );
    const VerificationSpec specBackward = specWithChecks( backward.checks );
    REQUIRE( sicnu::verification::validateSpec( specForward ).empty() );
    REQUIRE( sicnu::verification::validateSpec( specBackward ).empty() );

    const std::string digestForward = sicnu::verification::specDigest( specForward );
    const std::string digestBackward = sicnu::verification::specDigest( specBackward );
    REQUIRE_FALSE( digestForward.empty() );
    CHECK( digestForward == digestBackward );

    // Two runs produce byte-identical canonical output.
    std::string textForward;
    std::string textBackward;
    std::string error;
    REQUIRE( sicnu::verification::canonicalJson( specForward.toJson(), textForward, error ) );
    REQUIRE( sicnu::verification::canonicalJson( specBackward.toJson(), textBackward, error ) );
    CHECK( textForward == textBackward );

    const std::string textForwardAgain = [ &specForward ]
    {
        std::string text;
        std::string canonicalError;
        sicnu::verification::canonicalJson( specForward.toJson(), text, canonicalError );
        return text;
    }();
    CHECK( textForwardAgain == textForward );
    CHECK( sicnu::verification::specDigest( specForward ) == digestForward );

    // The digest must still be sensitive to a real change, or equality above
    // would prove nothing.
    REQUIRE_FALSE( specForward.checks.empty() );
    VerificationSpec changed = specForward;
    changed.checks.back().title += " (changed)";
    CHECK( sicnu::verification::specDigest( changed ) != digestForward );
}

TEST_CASE( "verifier14: every built-in and derived check id is non-empty and unique within its pack", "[verifier14][packs]" )
{
    std::vector<VerifierPack> packs;
    packs.push_back( sicnu::verification::structuralPack() );
    packs.push_back( sicnu::verification::provenancePack() );
    packs.push_back( sicnu::verification::reproducibilityPack() );
    for ( const std::string &operatorId : { "rs:spectral_index", "rs:qa_mask", "rs:sar_calibrate" } )
    {
        packs.push_back( sicnu::verification::scientificContractFor( operatorId ).pack );
    }

    for ( const VerifierPack &pack : packs )
    {
        UNSCOPED_INFO( "pack: " << pack.id );
        CHECK_FALSE( pack.id.empty() );
        CHECK_FALSE( pack.version.empty() );
        CHECK_FALSE( pack.checks.empty() );

        std::set<std::string> seen;
        for ( const VerificationCheck &check : pack.checks )
        {
            CHECK_FALSE( check.id.empty() );
            CHECK_FALSE( check.kind.empty() );
            CHECK_FALSE( check.title.empty() );
            CHECK( sicnu::verification::isKnownFailureCode( check.failureCode ) );
            CHECK( seen.insert( check.id ).second );
        }
    }
}
