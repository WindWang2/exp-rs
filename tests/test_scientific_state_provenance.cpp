/***************************************************************************
  tests/test_scientific_state_provenance.cpp
  RS14-01 Scientific Data Passport — Slice D: provenance + model-derived
  projection + classifier sidecar parsing.

  Lane: sicnu_add_sdk_test (Catch2 + jsoncpp only). Two seams are pinned:

  1. DerivationFacts → ProvenanceSection projection with known claims
     (source "catalog:DerivationRecord"); a derivation record's absence
     means provenance.isDerived stays false and no claim is emitted.
  2. parseClassifierSidecarJson parses the repo's real classifier sidecar
     ("<model>.meta.json", versions 1 and 2, see
     src/analysis/classification/rs_classification_pipeline.h) with the
     repo's untrusted-input discipline (CharReaderBuilder + stackLimit 128
     + try/catch). The sidecar's method value is projected verbatim — an
     unknown method is a fact about the model, not an error.

  Sidecar JSON shape (from saveModelSidecarV2):
    { "version": 1|2, "method": "<backend>",
      "classes": [ { "id": <int>, "color": "#rrggbb" }, ... ],
      "features": [ <1-based band>, ... ],
      "validation": { "overallAccuracy": <num>, "kappa": <num>, ... },
      "featureSchema": { "version": 1, "features": [ { "name": ... } ] } }
 ***************************************************************************/

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "scientific_state/asset_state_json.h"
#include "scientific_state/asset_state_resolver.h"
#include "scientific_state/model_sidecar.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace sicnu::state;

namespace
{

/// No explicit claim whose path lives under @p prefix. Query-time synthesized
/// unknowns don't count — they never enter the serialized document — and a
/// minimal resolution always carries sensor/acquisition unknown claims.
bool hasClaimUnder( const RemoteSensingAssetState &state, const std::string &prefix )
{
    for ( const ClaimRecord &claim : state.claims )
    {
        if ( claim.path.rfind( prefix, 0 ) == 0 )
            return true;
    }
    return false;
}

/// v2 sidecar with classes (out of order, one duplicate), accuracy and a
/// typed feature schema — mirrors saveModelSidecarV2 output.
const char *kSidecarV2 = R"JSON({
  "version": 2,
  "method": "random_forest",
  "classes": [
    { "id": 3, "color": "#0000cc" },
    { "id": 1, "color": "#cc0000" },
    { "id": 2, "color": "#00cc00" },
    { "id": 1, "color": "#cc0000" }
  ],
  "features": [ 2, 3, 1 ],
  "validation": { "overallAccuracy": 0.93, "kappa": 0.9, "perClass": {} },
  "featureSchema": { "version": 1,
                     "features": [ { "name": "ndvi", "kind": "index", "source": "(b2-b3)" },
                                   { "name": "b1", "kind": "band", "source": "1" } ] }
})JSON";

/// v1 sidecar: no validation, no feature schema, no v2 sections.
const char *kSidecarV1 = R"JSON({
  "version": 1,
  "method": "bayes",
  "classes": [ { "id": 0, "color": "#ff0000" }, { "id": 1, "color": "#00ff00" } ],
  "features": [ 1, 2 ]
})JSON";

DerivationFacts makeDerivationFacts()
{
    DerivationFacts derivation;
    derivation.algorithmId = "sicnu.ndvi.change";
    derivation.algorithmVersion = "2.1.0";
    derivation.completedAtUtc = "2026-09-22T10:00:00Z";
    derivation.executionFingerprint = "fp-8f2c41";
    derivation.softwareVersion = "sicnu 14.0";
    derivation.workflowRef = "wf-1234";
    derivation.cacheHit = true;

    DerivationFacts::Input late;
    late.assetId = "asset-b";
    late.revision = "2";
    late.bandReferences = { "nir", "red" };
    late.valueDomain = "surface_reflectance";
    DerivationFacts::Input early;
    early.assetId = "asset-a";
    early.revision = "1";
    early.bandReferences = { "red", "green" };
    derivation.inputs = { late, early };
    return derivation;
}

} // namespace

// ---------------------------------------------------------------------------
// Sidecar parsing
// ---------------------------------------------------------------------------

TEST_CASE( "v2 classifier sidecar parses into model facts with sorted labels",
           "[scientific_state][slice_d]" )
{
    ModelSidecarFacts facts;
    AssetStateError error;
    REQUIRE( parseClassifierSidecarJson( kSidecarV2, "/models/rf.meta.json", facts, error ) );
    REQUIRE( error.ok() );

    REQUIRE( facts.modelKind == "random_forest" );
    REQUIRE( facts.sidecarPath == "/models/rf.meta.json" );
    REQUIRE( facts.labels == std::vector<std::string>{ "1", "2", "3" } );
    REQUIRE( facts.hasAccuracy );
    REQUIRE( facts.accuracy == 0.93 );
    REQUIRE( facts.featureSchema == "ndvi,b1" );
}

TEST_CASE( "v1 classifier sidecar parses without v2 sections", "[scientific_state][slice_d]" )
{
    ModelSidecarFacts facts;
    AssetStateError error;
    REQUIRE( parseClassifierSidecarJson( kSidecarV1, "/models/bayes.meta.json", facts, error ) );

    REQUIRE( facts.modelKind == "bayes" );
    REQUIRE( facts.labels == std::vector<std::string>{ "0", "1" } );
    // v1 carries no validation section: accuracy absence is explicit.
    REQUIRE( !facts.hasAccuracy );
    REQUIRE( facts.featureSchema.empty() );
}

TEST_CASE( "sidecar without a classes section has empty labels", "[scientific_state][slice_d]" )
{
    const std::string text = R"JSON({ "version": 2, "method": "kmeans" })JSON";
    ModelSidecarFacts facts;
    AssetStateError error;
    REQUIRE( parseClassifierSidecarJson( text, "/m.meta.json", facts, error ) );
    REQUIRE( facts.labels.empty() );
    REQUIRE( facts.modelKind == "kmeans" );
}

TEST_CASE( "unknown method values are projected verbatim, never refused",
           "[scientific_state][slice_d]" )
{
    const std::string text = R"JSON({ "version": 2, "method": "quantum_classifier_v9" })JSON";
    ModelSidecarFacts facts;
    AssetStateError error;
    REQUIRE( parseClassifierSidecarJson( text, "/m.meta.json", facts, error ) );
    REQUIRE( facts.modelKind == "quantum_classifier_v9" );
}

TEST_CASE( "non-JSON sidecar text is a typed MalformedJson", "[scientific_state][slice_d]" )
{
    ModelSidecarFacts facts;
    AssetStateError error;
    REQUIRE( !parseClassifierSidecarJson( "not json at all", "/m.meta.json", facts, error ) );
    REQUIRE( error.code == StateErrorCode::MalformedJson );
}

TEST_CASE( "JSON sidecar missing key fields is a typed InvalidField",
           "[scientific_state][slice_d]" )
{
    ModelSidecarFacts facts;
    AssetStateError error;

    REQUIRE( !parseClassifierSidecarJson( R"JSON({ "method": "rf" })JSON", "/m", facts,
                                          error ) );
    REQUIRE( error.code == StateErrorCode::InvalidField );

    REQUIRE( !parseClassifierSidecarJson( R"JSON({ "version": 2 })JSON", "/m", facts, error ) );
    REQUIRE( error.code == StateErrorCode::InvalidField );

    REQUIRE( !parseClassifierSidecarJson( R"JSON({ "version": 7, "method": "rf" })JSON", "/m",
                                          facts, error ) );
    REQUIRE( error.code == StateErrorCode::InvalidField );
}

TEST_CASE( "malformed classes entries are InvalidField", "[scientific_state][slice_d]" )
{
    const std::string text =
        R"JSON({ "version": 1, "method": "rf", "classes": [ { "color": "#ffffff" } ] })JSON";
    ModelSidecarFacts facts;
    AssetStateError error;
    REQUIRE( !parseClassifierSidecarJson( text, "/m", facts, error ) );
    REQUIRE( error.code == StateErrorCode::InvalidField );
}

TEST_CASE( "deeply nested sidecar JSON is refused as MalformedJson without crashing",
           "[scientific_state][slice_d]" )
{
    std::string bomb( 300, '[' );
    bomb.append( 300, ']' );

    ModelSidecarFacts facts;
    AssetStateError error;
    REQUIRE( !parseClassifierSidecarJson( bomb, "/bomb.meta.json", facts, error ) );
    REQUIRE( error.code == StateErrorCode::MalformedJson );
}

// ---------------------------------------------------------------------------
// Provenance projection
// ---------------------------------------------------------------------------

TEST_CASE( "derivation facts project the provenance section with known claims",
           "[scientific_state][slice_d]" )
{
    StateResolutionInput input;
    input.derivation = makeDerivationFacts();

    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.provenance.isDerived );
    REQUIRE( state.provenance.algorithmId == "sicnu.ndvi.change" );
    REQUIRE( state.provenance.algorithmVersion == "2.1.0" );
    REQUIRE( state.provenance.completedAtUtc == "2026-09-22T10:00:00Z" );
    REQUIRE( state.provenance.executionFingerprint == "fp-8f2c41" );
    REQUIRE( state.provenance.softwareVersion == "sicnu 14.0" );
    REQUIRE( state.provenance.workflowRef == "wf-1234" );
    REQUIRE( state.provenance.cacheHit );

    // Inputs sorted by assetId, bandReferences sorted and unique.
    REQUIRE( state.provenance.inputs.size() == 2 );
    REQUIRE( state.provenance.inputs[0].assetId == "asset-a" );
    REQUIRE( state.provenance.inputs[0].bandReferences ==
             std::vector<std::string>{ "green", "red" } );
    REQUIRE( state.provenance.inputs[1].assetId == "asset-b" );

    const ClaimRecord algorithmClaim = claimFor( state, "provenance.algorithm" );
    REQUIRE( algorithmClaim.kind == ClaimKind::Known );
    REQUIRE( algorithmClaim.sources ==
             std::vector<std::string>{ "catalog:DerivationRecord" } );
    const ClaimRecord inputsClaim = claimFor( state, "provenance.inputs" );
    REQUIRE( inputsClaim.kind == ClaimKind::Known );
    REQUIRE( inputsClaim.sources == std::vector<std::string>{ "catalog:DerivationRecord" } );
}

TEST_CASE( "no derivation record means isDerived=false and no provenance claims",
           "[scientific_state][slice_d]" )
{
    StateResolutionInput input;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !state.provenance.isDerived );
    REQUIRE( claimFor( state, "provenance.algorithm" ).kind == ClaimKind::Unknown );
    REQUIRE( claimFor( state, "provenance.inputs" ).kind == ClaimKind::Unknown );
    REQUIRE( !hasClaimUnder( state, "provenance." ) );
}

TEST_CASE( "a derivation record without algorithm or inputs claims nothing beyond presence",
           "[scientific_state][slice_d]" )
{
    StateResolutionInput input;
    input.derivation = DerivationFacts{};

    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.provenance.isDerived );
    REQUIRE( !hasClaimUnder( state, "provenance." ) );
}

// ---------------------------------------------------------------------------
// Model-derived projection
// ---------------------------------------------------------------------------

TEST_CASE( "model sidecar facts project the model-derived section with a known labels claim",
           "[scientific_state][slice_d]" )
{
    ModelSidecarFacts facts;
    facts.modelKind = "random_forest";
    facts.labels = { "water", "forest", "urban" };
    facts.hasAccuracy = true;
    facts.accuracy = 0.91;
    facts.sidecarPath = "/models/rf.meta.json";
    facts.featureSchema = "ndvi,b1";

    StateResolutionInput input;
    input.modelSidecar = facts;

    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.modelDerived.present );
    REQUIRE( state.modelDerived.modelKind == "random_forest" );
    REQUIRE( state.modelDerived.labels ==
             std::vector<std::string>{ "forest", "urban", "water" } );
    REQUIRE( state.modelDerived.hasAccuracy );
    REQUIRE( state.modelDerived.accuracy == 0.91 );
    REQUIRE( state.modelDerived.sidecarPath == "/models/rf.meta.json" );
    REQUIRE( state.modelDerived.featureSchema == "ndvi,b1" );

    const ClaimRecord claim = claimFor( state, "model_derived.labels" );
    REQUIRE( claim.kind == ClaimKind::Known );
    REQUIRE( claim.sources == std::vector<std::string>{ "sidecar:classifier-meta" } );
}

TEST_CASE( "sidecar without labels records a typed unknown for the labels claim",
           "[scientific_state][slice_d]" )
{
    ModelSidecarFacts facts;
    facts.modelKind = "kmeans";
    facts.sidecarPath = "/models/km.meta.json";

    StateResolutionInput input;
    input.modelSidecar = facts;

    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.modelDerived.present );
    REQUIRE( state.modelDerived.labels.empty() );
    REQUIRE( claimFor( state, "model_derived.labels" ).kind == ClaimKind::Unknown );
    REQUIRE( std::find( state.unknowns.begin(), state.unknowns.end(),
                        "model_derived.labels" ) != state.unknowns.end() );
}

TEST_CASE( "no sidecar leaves the model-derived section absent", "[scientific_state][slice_d]" )
{
    StateResolutionInput input;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !state.modelDerived.present );
    REQUIRE( !hasClaimUnder( state, "model_derived." ) );
}

TEST_CASE( "full sidecar text flows through parse and resolve deterministically",
           "[scientific_state][slice_d]" )
{
    ModelSidecarFacts facts;
    AssetStateError error;
    REQUIRE( parseClassifierSidecarJson( kSidecarV2, "/models/rf.meta.json", facts, error ) );

    StateResolutionInput input;
    input.modelSidecar = facts;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.modelDerived.modelKind == "random_forest" );
    REQUIRE( state.modelDerived.labels == std::vector<std::string>{ "1", "2", "3" } );
    const std::string first = serializeState( state );
    const std::string second = serializeState( resolveAssetState( input ).state );
    REQUIRE( first == second );
}
