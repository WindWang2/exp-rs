// tests/test_r21_e_faultlab_verifier.cpp — R21-E W2 (#1613): the
// fault-scenario verifier declaration (rules_ref / must_fail_assertions)
// documented by data/faultlab/faults.schema.json:160-171 becomes a real,
// enforced contract instead of a refusal (or worse, inert acceptance).
//
// RED (against master 911e0b17): the loader REJECTS any non-empty
// expected.verifier ("schema v1 has no verifier consumer"), so a scenario
// shape the published schema documents cannot even load — the first test
// below fails. The runner-side fail-closed and hook folding assertions
// arrive with the W2 implementation (they need the new options member).

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <json/json.h>

#include "faultlab/fault_report.h"
#include "faultlab/fault_runner.h"
#include "faultlab/fault_types.h"

#include <string>

using namespace sicnu::faultlab;

namespace
{
/// Minimal valid scenario (mirrors tests/test_faultlab.cpp::validScenarioDoc)
/// with the schema-documented expected.verifier attached.
Json::Value verifierScenarioDoc()
{
    Json::Value doc( Json::objectValue );
    doc["schema_version"] = kFaultScenarioSchemaId;
    doc["scenario_id"] = "fs_band_role_swap_ndvi";
    doc["title"] = "Band role swap on an index pair";

    Json::Value fault( Json::objectValue );
    fault["family"] = "band_role_swap";
    Json::Value params( Json::objectValue );
    params["role_a"] = "red";
    params["role_b"] = "nir";
    fault["params"] = params;
    fault["seed"] = 42u;
    doc["fault"] = fault;

    Json::Value fixture( Json::objectValue );
    fixture["fixture_id"] = "two_band_index_pair";
    fixture["params"] = Json::objectValue;
    fixture["seed"] = 42u;
    doc["base_fixture"] = fixture;

    Json::Value sandbox( Json::objectValue );
    sandbox["class"] = "temp_copy";
    sandbox["max_bytes"] = Json::Value::UInt64( 1048576 );
    sandbox["require_source_unchanged"] = true;
    doc["sandbox"] = sandbox;

    Json::Value expected( Json::objectValue );
    Json::Value obs( Json::arrayValue );
    Json::Value o1( Json::objectValue );
    o1["id"] = "band_roles";
    o1["relation"] = "changed";
    obs.append( o1 );
    Json::Value o2( Json::objectValue );
    // Direction matters: band_role_swap drives the index DOWN (red/nir
    // exchange), so the canonical relation is delta_le with a negative
    // bound — the same expectation tests/test_faultlab.cpp pins for the
    // all-green base (a wrong direction silently fails expectations).
    o2["id"] = "index_mean";
    o2["relation"] = "delta_le";
    o2["value"] = -0.5;
    obs.append( o2 );
    expected["observables"] = obs;
    Json::Value diagnosis( Json::objectValue );
    diagnosis["signature"] = "all_negative_index";
    expected["diagnosis"] = diagnosis;

    // The shape data/faultlab/faults.schema.json:160-171 documents.
    Json::Value verifier( Json::objectValue );
    verifier["rules_ref"] = "ndvi_basics";
    Json::Value mustFail( Json::arrayValue );
    mustFail.append( "stats.ndvi_mean_sigma" );
    verifier["must_fail_assertions"] = mustFail;
    expected["verifier"] = verifier;
    doc["expected"] = expected;

    Json::Value lo( Json::objectValue );
    lo["id"] = "LO-03";
    lo["statement"] = "Recognize that band metadata, not band position, defines an index.";
    doc["learning_objective"] = lo;

    Json::Value cleanup( Json::objectValue );
    cleanup["required"] = true;
    cleanup["verify_no_residue"] = true;
    doc["cleanup"] = cleanup;
    return doc;
}
} // namespace

TEST_CASE( "faultlab verifier: loader accepts the schema-documented verifier",
           "[r21e][faultlab_verifier]" )
{
    const auto result = loadFaultScenario( verifierScenarioDoc() );
    if ( !result.ok )
    {
        const std::string first = result.diagnostics.empty()
                                      ? std::string( "(no diagnostics)" )
                                      : result.diagnostics.front().code + ": " +
                                            result.diagnostics.front().message;
        INFO( "loader refused: " << first );
    }
    REQUIRE( result.ok );
    CHECK( result.value.verifier["rules_ref"].asString() == "ndvi_basics" );
    CHECK( result.value.verifier["must_fail_assertions"].size() == 1 );
}

TEST_CASE( "faultlab verifier: empty verifier stays accepted (back-compat)",
           "[r21e][faultlab_verifier]" )
{
    Json::Value doc = verifierScenarioDoc();
    doc["expected"].removeMember( "verifier" );
    const auto result = loadFaultScenario( doc );
    REQUIRE( result.ok );
    CHECK( result.value.verifier.empty() );
}

TEST_CASE( "faultlab verifier: malformed verifier is a typed refusal",
           "[r21e][faultlab_verifier]" )
{
    // rules_ref must be a non-empty string when the verifier object carries
    // must_fail_assertions; unknown members are refused (closed shape).
    {
        Json::Value doc = verifierScenarioDoc();
        doc["expected"]["verifier"]["rules_ref"] = "";
        const auto result = loadFaultScenario( doc );
        CHECK( !result.ok );
    }
    {
        Json::Value doc = verifierScenarioDoc();
        doc["expected"]["verifier"]["surprise"] = true;
        const auto result = loadFaultScenario( doc );
        CHECK( !result.ok );
    }
    {
        Json::Value doc = verifierScenarioDoc();
        doc["expected"]["verifier"]["must_fail_assertions"].append( "" );
        const auto result = loadFaultScenario( doc );
        CHECK( !result.ok );
    }
}

namespace
{
/// The all-green base (mirrors tests/test_faultlab.cpp::bandSwapScenario):
/// faultApplied/expectations/diagnosis/sandbox/source/replay all hold, so
/// report.passed is exactly the verifier fold's verdict.
FaultScenario loadedVerifierScenario()
{
    Json::Value doc( Json::objectValue );
    doc["schema_version"] = kFaultScenarioSchemaId;
    doc["scenario_id"] = "fs_band_role_swap_ndvi";
    doc["title"] = "Band role swap on an NDVI pair";
    Json::Value fault( Json::objectValue );
    fault["family"] = "band_role_swap";
    fault["params"] = Json::Value( Json::objectValue );
    fault["params"]["role_a"] = "red";
    fault["params"]["role_b"] = "nir";
    fault["seed"] = 42u;
    doc["fault"] = fault;
    Json::Value fixture( Json::objectValue );
    fixture["fixture_id"] = "two_band_index_pair";
    fixture["params"] = Json::objectValue;
    fixture["seed"] = 42u;
    doc["base_fixture"] = fixture;
    Json::Value sandbox( Json::objectValue );
    sandbox["max_bytes"] = Json::Value::UInt64( 1048576 );
    doc["sandbox"] = sandbox;
    Json::Value expected( Json::objectValue );
    Json::Value obs( Json::arrayValue );
    Json::Value changed( Json::objectValue );
    changed["id"] = "band_roles";
    changed["relation"] = "changed";
    obs.append( changed );
    Json::Value delta( Json::objectValue );
    delta["id"] = "index_mean";
    delta["relation"] = "delta_le";
    delta["value"] = -0.5;
    obs.append( delta );
    expected["observables"] = obs;
    Json::Value diagnosis( Json::objectValue );
    diagnosis["signature"] = "all_negative_index";
    expected["diagnosis"] = diagnosis;
    Json::Value verifier( Json::objectValue );
    verifier["rules_ref"] = "ndvi_basics";
    Json::Value mustFail( Json::arrayValue );
    mustFail.append( "stats.ndvi_mean_sigma" );
    verifier["must_fail_assertions"] = mustFail;
    expected["verifier"] = verifier;
    doc["expected"] = expected;
    Json::Value lo( Json::objectValue );
    lo["id"] = "LO-03";
    lo["statement"] = "Band metadata, not band position, defines an index.";
    doc["learning_objective"] = lo;
    Json::Value cleanup( Json::objectValue );
    cleanup["required"] = true;
    doc["cleanup"] = cleanup;

    const auto loaded = loadFaultScenario( doc );
    REQUIRE( loaded.ok );
    return loaded.value;
}

bool hasDiagnostic( const FaultRunReport &report, const std::string &code )
{
    for ( const FaultDiagnostic &diagnostic : report.diagnostics )
        if ( diagnostic.code == code )
            return true;
    return false;
}
} // namespace

TEST_CASE( "faultlab verifier: declared verifier without a hook fails closed",
           "[r21e][faultlab_verifier]" )
{
    const FaultScenario scenario = loadedVerifierScenario();
    const auto run = runFaultScenario( scenario ); // default options: no hook
    REQUIRE( run.ok );
    CHECK_FALSE( run.value.passed );
    CHECK( run.value.verifierDeclared );
    CHECK_FALSE( run.value.verifierChecked );
    CHECK( hasDiagnostic( run.value, "faultlab.verifier_missing" ) );
    // The report surfaces the unchecked fault honestly.
    CHECK( run.value.toJson().isMember( "verifier" ) );
    CHECK( run.value.toJson()["verifier"]["checked"].asBool() == false );
}

TEST_CASE( "faultlab verifier: hook outcome folds into passed",
           "[r21e][faultlab_verifier]" )
{
    const FaultScenario scenario = loadedVerifierScenario();

    SECTION( "hook confirms the must-fail assertions fired" )
    {
        FaultRunOptions options;
        options.verifierHook = []( const FaultGrid &, const FaultVerifierSpec &spec ) {
            FaultVerifierOutcome outcome;
            outcome.ran = true;
            outcome.mustFailSatisfied = true;
            outcome.observedFailedAssertions = spec.mustFailAssertions;
            return outcome;
        };
        const auto run = runFaultScenario( scenario, options );
        REQUIRE( run.ok );
        CHECK( run.value.passed );
        CHECK( run.value.verifierChecked );
        CHECK( run.value.verifierMustFailObserved );
        const Json::Value json = run.value.toJson();
        CHECK( json["verifier"]["checked"].asBool() );
        CHECK( json["verifier"]["must_fail_observed"].asBool() );
        CHECK( json["verifier"]["observed_failed"].size() == 1 );
        CHECK( json["verifier"]["observed_failed"][0].asString() == "stats.ndvi_mean_sigma" );
    }

    SECTION( "hook reports the grader did not catch the fault" )
    {
        FaultRunOptions options;
        options.verifierHook = []( const FaultGrid &, const FaultVerifierSpec & ) {
            FaultVerifierOutcome outcome;
            outcome.ran = true;
            outcome.mustFailSatisfied = false; // grader passed the faulted artifact
            return outcome;
        };
        const auto run = runFaultScenario( scenario, options );
        REQUIRE( run.ok );
        CHECK_FALSE( run.value.passed );
        CHECK( run.value.verifierChecked );
        CHECK_FALSE( run.value.verifierMustFailObserved );
        CHECK( hasDiagnostic( run.value, "faultlab.verifier_must_fail_not_observed" ) );
    }

    SECTION( "hook itself could not run (fail closed, not guessed)" )
    {
        FaultRunOptions options;
        options.verifierHook = []( const FaultGrid &, const FaultVerifierSpec & ) {
            FaultVerifierOutcome outcome; // ran = false
            outcome.note = "grading unavailable";
            return outcome;
        };
        const auto run = runFaultScenario( scenario, options );
        REQUIRE( run.ok );
        CHECK_FALSE( run.value.passed );
        CHECK_FALSE( run.value.verifierChecked );
        CHECK( hasDiagnostic( run.value, "faultlab.verifier_did_not_run" ) );
    }
}

TEST_CASE( "faultlab verifier: undeclared scenario keeps the legacy report shape",
           "[r21e][faultlab_verifier]" )
{
    Json::Value doc = verifierScenarioDoc();
    doc["expected"].removeMember( "verifier" );
    const auto loaded = loadFaultScenario( doc );
    REQUIRE( loaded.ok );
    const auto run = runFaultScenario( loaded.value );
    REQUIRE( run.ok );
    CHECK_FALSE( run.value.verifierDeclared );
    CHECK( run.value.passed ); // the six-way conjunction alone holds
    CHECK_FALSE( run.value.toJson().isMember( "verifier" ) );
}

TEST_CASE( "faultlab verifier: vacuous and rule-less declarations are refused",
           "[r21e][faultlab_verifier]" )
{
    // rules_ref without any must_fail assertion observes nothing — the
    // #1464 accepted-but-inert shape one level down (R1b P2).
    {
        Json::Value doc = verifierScenarioDoc();
        doc["expected"]["verifier"].removeMember( "must_fail_assertions" );
        const auto result = loadFaultScenario( doc );
        CHECK( !result.ok );
    }
    // must_fail without rules_ref has nothing to grade against.
    {
        Json::Value doc = verifierScenarioDoc();
        doc["expected"]["verifier"].removeMember( "rules_ref" );
        const auto result = loadFaultScenario( doc );
        CHECK( !result.ok );
    }
    // An empty must_fail ARRAY is the same vacuous shape.
    {
        Json::Value doc = verifierScenarioDoc();
        doc["expected"]["verifier"]["must_fail_assertions"] = Json::arrayValue;
        const auto result = loadFaultScenario( doc );
        CHECK( !result.ok );
    }
}

TEST_CASE( "faultlab verifier: non-object declaration fails closed in the runner",
           "[r21e][faultlab_verifier]" )
{
    // The loader refuses non-object expected.verifier; a programmatically
    // built scenario that bypasses it must still fail closed (R1b P1).
    const FaultScenario scenario = loadedVerifierScenario();
    Json::Value doc = verifierScenarioDoc();
    doc["expected"]["verifier"] = Json::Value( "not-an-object" );
    const auto loaded = loadFaultScenario( doc );
    REQUIRE( !loaded.ok ); // the door holds
    // Reach the runner with the malformed declaration by hand:
    FaultScenario hand = scenario;
    hand.verifier = Json::Value( "not-an-object" );
    const auto run = runFaultScenario( hand );
    REQUIRE( run.ok );
    CHECK_FALSE( run.value.passed );
    CHECK( run.value.verifierDeclared );
    CHECK( hasDiagnostic( run.value, "faultlab.verifier_spec_invalid" ) );
}

TEST_CASE( "faultlab verifier: an aborted run still shows the declaration",
           "[r21e][faultlab_verifier]" )
{
    // Sandbox creation fails (sandbox root is a FILE, not a directory): the
    // run aborts before the fold, but the report must still carry the
    // verifier block with checked=false — never masquerade as undeclared.
    QTemporaryDir temp;
    REQUIRE( temp.isValid() );
    const QString blocker = temp.filePath( QStringLiteral( "not_a_dir" ) );
    {
        QFile file( blocker );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( "x" );
        file.close();
    }
    FaultRunOptions options;
    options.sandboxRoot = blocker.toStdString();
    const auto run = runFaultScenario( loadedVerifierScenario(), options );
    REQUIRE( run.ok );
    CHECK_FALSE( run.value.passed );
    CHECK( run.value.verifierDeclared );
    CHECK_FALSE( run.value.verifierChecked );
    CHECK( run.value.toJson().isMember( "verifier" ) );
}

TEST_CASE( "faultlab verifier: fault that cannot apply skips the verifier (R2 P3-4)",
           "[r21e][faultlab_verifier]" )
{
    // band_role_swap with roles the fixture does not carry: the fault never
    // applies, so there is no faulted artifact to grade. The fold must skip
    // with a warning (not a silent pass), and the scenario still fails
    // through faultApplied.
    Json::Value probe = verifierScenarioDoc();
    probe["fault"]["params"]["role_a"] = "swir1"; // absent from two_band_index_pair
    probe["fault"]["params"]["role_b"] = "swir2";
    const auto loaded = loadFaultScenario( probe );
    REQUIRE( loaded.ok );

    bool hookCalled = false;
    FaultRunOptions options;
    options.verifierHook = [ &hookCalled ]( const FaultGrid &, const FaultVerifierSpec & ) {
        hookCalled = true;
        FaultVerifierOutcome outcome;
        outcome.ran = true;
        outcome.mustFailSatisfied = true;
        return outcome;
    };
    const auto run = runFaultScenario( loaded.value, options );
    REQUIRE( run.ok );
    CHECK_FALSE( run.value.passed );            // faultApplied=false alone fails it
    CHECK_FALSE( run.value.faultApplied );
    CHECK( run.value.verifierDeclared );        // declaration still visible
    CHECK_FALSE( run.value.verifierChecked );   // nothing was graded
    CHECK_FALSE( hookCalled );                  // the hook was never invoked
    CHECK( hasDiagnostic( run.value, "faultlab.verifier_skipped_no_fault" ) );
}
