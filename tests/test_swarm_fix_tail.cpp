// tests/test_swarm_fix_tail.cpp
//
// swarm-review 2026-11-04 — the LT1/LT2 long-tail findings (12), one
// behavioural anchor each (LT1-5 is a comment-contract fix and carries no
// runtime behaviour to pin). Every case here is RED on the pre-fix tree:
//
//   LT1-1 planner: after calibration the analysis step converges EVERY
//         bridge output plus a direct reference to each asset that already
//         sat in the required domain (master hung it off the LAST bridge
//         only — earlier bridge outputs orphaned, in-domain assets gone).
//   LT1-2 planner: a step input referencing its OWN step id is a 1-step
//         cycle; the fail-closed reader and the value-object validator
//         both refused it on master only by accident of ordering.
//   LT1-3 scientific_state: the noData census paired the catalog mirror by
//         position even when the band layer had refused to merge it —
//         attesting a "declared" policy (Known) over undeclared bands.
//   LT1-4 teaching_admin: an oversize operator sidecar is a typed issue,
//         never an unbounded readAll() slurp.
//   LT1-6 faultlab: kObservableTolerance is RELATIVE — the band scales with
//         the compared magnitude (master consumed it as an absolute band).
//   LT2-1 agent_loop: preflight "fixable" with ZERO proposals no longer
//         bypasses allowUnapprovedFixable into execution.
//   LT2-2 agent_loop: SessionJournal::load stats before it reads.
//   LT2-3 agent_loop: a verification aggregate over ZERO artifacts is
//         INDETERMINATE, never a fail-open "PASS".
//   LT2-4 teaching: the verdict-stamp content digest covers EVERY byte.
//   LT2-5 agent_loop: empty fake script vectors fail closed instead of
//         indexing size()-1 (SIZE_MAX) out of bounds.
//   LT2-6 explain: the guidance directory scan stats before it reads.
//
// Lane note: agent_loop/fake_seams.h comes FIRST — Qt's `slots` macro
// collides with FakeScenario::slots (same convention as
// tests/test_agent_session_adapter.cpp).
#include <catch2/catch_test_macros.hpp>

#include "agent_loop/fake_seams.h"
#include "agent_loop/scientific_agent_session.h"
#include "agent_loop/session_journal.h"
#include "agent_loop/session_seams.h"
#include "agent_loop/session_state.h"

#include "explain/guidance_store.h"
#include "faultlab/fault_expectations.h"
#include "faultlab/fault_observables.h"
#include "lab/sha256.h"
#include "planner/planner_core.h"
#include "planner/scientific_plan.h"
#include "scientific_state/asset_state_resolver.h"
#include "teaching/verdict_stamp.h"
#include "teaching_admin/operator_catalog.h"

#include <QTemporaryDir>

#include <json/json.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define SICNU_TAIL_GETPID ::_getpid
#else
#include <unistd.h>
#define SICNU_TAIL_GETPID ::getpid
#endif

namespace {

// ---------------------------------------------------------------------------
// shared helpers
// ---------------------------------------------------------------------------

std::string jsonToString( const Json::Value &doc )
{
    Json::StreamWriterBuilder builder;
    builder[ "commentStyle" ] = "None";
    builder[ "indentation" ] = "";
    return Json::writeString( builder, doc );
}

void writeFile( const std::filesystem::path &path, const std::string &bytes )
{
    std::ofstream out( path, std::ios::binary );
    out << bytes;
}

std::string readFile( const std::filesystem::path &path )
{
    std::ifstream in( path, std::ios::binary );
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// A process-unique scratch directory (repo convention: pid suffix, wiped).
std::filesystem::path tempDir( const char *tag )
{
    const auto dir = std::filesystem::temp_directory_path()
                     / ( std::string( "swarm_fix_tail_" ) + tag + "_"
                         + std::to_string( SICNU_TAIL_GETPID() ) );
    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
    std::filesystem::create_directories( dir, ec );
    return dir;
}

// ---------------------------------------------------------------------------
// LT1-1 / LT1-2 — planner
// ---------------------------------------------------------------------------

class TailProvider final : public sicnu::planner::CapabilityProvider
{
  public:
    void add( const std::string &family, sicnu::planner::PlannerCapability capability )
    {
        capability.family = family;
        byFamily_[ family ].push_back( std::move( capability ) );
    }

    std::vector< sicnu::planner::PlannerCapability > capabilitiesForFamily(
        const std::string &family ) const override
    {
        const auto it = byFamily_.find( family );
        return it == byFamily_.end() ? std::vector< sicnu::planner::PlannerCapability >{}
                                     : it->second;
    }

  private:
    std::map< std::string, std::vector< sicnu::planner::PlannerCapability > > byFamily_;
};

sicnu::planner::PlannerCapability tailCapability( const std::string &operatorId,
                                                  const std::string &costClass,
                                                  const std::string &inputDomain,
                                                  const std::string &outputDomain )
{
    sicnu::planner::PlannerCapability capability;
    capability.operatorId = operatorId;
    capability.costClass = costClass;
    capability.inputDomain = inputDomain;
    capability.outputDomain = outputDomain;
    return capability;
}

sicnu::planner::PlannerAssetFacts tailAsset( const std::string &ref, const std::string &domain )
{
    sicnu::planner::PlannerAssetFacts asset;
    asset.ref = ref;
    asset.kind = "raster";
    asset.modality = "optical";
    asset.numericDomain = domain;
    asset.crs = "EPSG:32648";
    asset.resolutionM = 10.0;
    asset.dates = { "2024-01-10" };
    asset.state = "ready";
    return asset;
}

const sicnu::planner::PlannerStep *stepByOperator( const sicnu::planner::ScientificPlan &plan,
                                                   const std::string &operatorId )
{
    for ( const auto &step : plan.steps )
        if ( step.operatorId == operatorId )
            return &step;
    return nullptr;
}

bool hasInputFromStep( const sicnu::planner::PlannerStep &step, const std::string &stepId )
{
    return std::any_of( step.inputs.begin(), step.inputs.end(),
                        [ &stepId ]( const sicnu::planner::StepInput &input ) {
                            return input.fromStepId == stepId;
                        } );
}

bool hasInputAsset( const sicnu::planner::PlannerStep &step, const std::string &assetRef )
{
    return std::any_of( step.inputs.begin(), step.inputs.end(),
                        [ &assetRef ]( const sicnu::planner::StepInput &input ) {
                            return input.assetRef == assetRef;
                        } );
}

} // namespace

TEST_CASE( "LT1-1: the analysis step converges every calibration bridge plus in-domain assets",
           "[swarm_review][lt1][planner]" )
{
    using namespace sicnu::planner;

    // Two domain gaps (dn and radiance both need a bridge to features) and
    // one asset already sitting in the required domain.
    PlanningContext context;
    context.assets = { tailAsset( "asset-dn-1", "dn" ), tailAsset( "asset-dn-2", "dn" ),
                       tailAsset( "asset-radiance", "radiance" ),
                       tailAsset( "asset-in-domain", "features" ) };
    context.mode = ModePolicy{ "agent", "full", false };

    TailProvider provider;
    provider.add( "data_import", tailCapability( "rs:mosaic", "low", "", "" ) );
    provider.add( "calibration", tailCapability( "rs:band_math", "low", "dn", "features" ) );
    provider.add( "calibration", tailCapability( "rs:solar_geometry", "low", "radiance", "features" ) );
    provider.add( "analysis", tailCapability( "rs:change", "medium", "features", "none" ) );
    provider.add( "publication", tailCapability( "io:translate", "low", "", "" ) );

    ScientificGoal goal;
    goal.goalId = "goal-tail-mixed-domains";
    goal.kind = "change";
    goal.subject = "mixed-domain change detection";

    const PlanningResult result =
        planScientificWork( goal, context, PlannerProviders{ &provider } );
    REQUIRE( result.candidates.size() == 1 );
    const ScientificPlan &plan = *result.primary();
    CHECK( plan.verdict == "feasible" );

    const PlannerStep *dnBridge = stepByOperator( plan, "rs:band_math" );
    const PlannerStep *radianceBridge = stepByOperator( plan, "rs:solar_geometry" );
    const PlannerStep *analyze = stepByOperator( plan, "rs:change" );
    REQUIRE( dnBridge != nullptr );
    REQUIRE( radianceBridge != nullptr );
    REQUIRE( analyze != nullptr );

    // EVERY bridge output converges on the analysis step — master wired
    // only the last one, orphaning the earlier bridge.
    CHECK( hasInputFromStep( *analyze, dnBridge->stepId ) );
    CHECK( hasInputFromStep( *analyze, radianceBridge->stepId ) );

    // The asset that needed no bridge keeps its direct reference — master
    // dropped it from the data flow while its precondition stayed.
    CHECK( hasInputAsset( *analyze, "asset-in-domain" ) );

    // Bridged assets travel through their bridge, not as direct refs.
    CHECK_FALSE( hasInputAsset( *analyze, "asset-dn-1" ) );
    CHECK_FALSE( hasInputAsset( *analyze, "asset-dn-2" ) );
    CHECK_FALSE( hasInputAsset( *analyze, "asset-radiance" ) );

    // The document still validates and survives the fail-closed reader.
    CHECK( validateScientificPlan( plan ).empty() );
    ScientificPlan reparsed;
    std::string error;
    INFO( "plan " << plan.planId );
    REQUIRE( scientificPlanFromJson( scientificPlanToJson( plan ), reparsed, error ) );
    CHECK( validateScientificPlan( reparsed ).empty() );
    CHECK( scientificPlanFingerprint( reparsed ) == scientificPlanFingerprint( plan ) );
}

TEST_CASE( "LT1-2: a self-referencing step input is a refused 1-step cycle",
           "[swarm_review][lt1][planner]" )
{
    using namespace sicnu::planner;

    ScientificPlan plan;
    plan.planId = "plan-tail-selfref";
    plan.goalId = "goal-tail";
    plan.goalKind = "change";
    plan.rulesRevision = "planner-rules/1";
    plan.modeKind = "agent";
    plan.autonomy = "full";
    plan.verdict = "feasible";

    PlannerStep upstream;
    upstream.stepId = "step-upstream";
    upstream.role = "preprocess";
    upstream.operatorId = "rs:brdf_normalization";
    upstream.family = "calibration";
    upstream.inputs = { StepInput{ "", "asset-a", "input" } };
    upstream.costClass = "medium";
    PlannerStep analyze;
    analyze.stepId = "step-analyze";
    analyze.role = "analyze";
    analyze.operatorId = "rs:change";
    analyze.family = "analysis";
    analyze.costClass = "low";
    plan.steps = { upstream, analyze };

    // Control: the lawful backward reference parses and validates.
    analyze.inputs = { StepInput{ "step-upstream", "", "input" } };
    plan.steps = { upstream, analyze };
    {
        ScientificPlan parsed;
        std::string error;
        REQUIRE( scientificPlanFromJson( scientificPlanToJson( plan ), parsed, error ) );
        CHECK( validateScientificPlan( parsed ).empty() );
    }

    // The self-reference: master's own-id insert laundered it past the
    // earlier-step lookup, so the reader accepted a 1-step cycle.
    analyze.inputs = { StepInput{ "step-analyze", "", "input" } };
    plan.steps = { upstream, analyze };
    {
        ScientificPlan parsed;
        std::string error;
        CHECK_FALSE( scientificPlanFromJson( scientificPlanToJson( plan ), parsed, error ) );
        CHECK( error.find( "later step" ) != std::string::npos );
    }
    const std::vector< std::string > problems = validateScientificPlan( plan );
    bool dangling = false;
    for ( const std::string &problem : problems )
        if ( problem.find( "dangling step input" ) != std::string::npos )
            dangling = true;
    CHECK( dangling );
}

// ---------------------------------------------------------------------------
// LT1-3 — scientific_state
// ---------------------------------------------------------------------------

TEST_CASE( "LT1-3: a misaligned catalog mirror feeds neither bands nor the noData census",
           "[swarm_review][lt1][scientific_state]" )
{
    using namespace sicnu::state;

    // File axis {2,3} against a mirror {1,2}: the band layer refuses to
    // merge the mirror (structure mismatch), so its NoData declarations
    // must not count toward the policy census either.
    auto misaligned = [] {
        DatasetFacts dataset;
        dataset.sourcePath = "/data/misaligned.tif";
        dataset.driverName = "GTiff";
        dataset.bandCount = 2;
        for ( int index : { 2, 3 } )
        {
            BandFacts band;
            band.index = index;
            band.dataType = "Byte";
            dataset.bands.push_back( band );
        }
        CatalogFacts catalog;
        catalog.assetId = "misaligned";
        catalog.structureBandCount = 2;
        for ( int index : { 1, 2 } )
        {
            CatalogBandFacts band;
            band.index = index;
            band.hasNoData = true;
            band.noDataValue = -9999.0;
            catalog.bands.push_back( band );
        }
        StateResolutionInput input;
        input.dataset = dataset;
        input.catalog = catalog;
        return resolveAssetState( input ).state;
    }();

    bool mismatchNoted = false;
    for ( const ResolutionNote &note : misaligned.notes )
        if ( note.code == "bands.catalog_structure_mismatch" )
            mismatchNoted = true;
    CHECK( mismatchNoted );
    REQUIRE( misaligned.bands.size() == 2 );
    CHECK_FALSE( misaligned.bands[ 0 ].hasNoData );
    CHECK_FALSE( misaligned.bands[ 1 ].hasNoData );
    // Master counted the mirror positionally: "declared" + Known confidence
    // over bands that never declared anything.
    CHECK( misaligned.validity.noDataPolicy == "undeclared" );
    CHECK( claimFor( misaligned, "validity.no_data_policy" ).kind == ClaimKind::Inferred );

    // Control: an aligned mirror still pairs and still counts.
    DatasetFacts dataset;
    dataset.sourcePath = "/data/aligned.tif";
    dataset.driverName = "GTiff";
    dataset.bandCount = 2;
    for ( int index : { 1, 2 } )
    {
        BandFacts band;
        band.index = index;
        band.dataType = "Byte";
        dataset.bands.push_back( band );
    }
    CatalogFacts catalog;
    catalog.assetId = "aligned";
    catalog.structureBandCount = 2;
    for ( int index : { 1, 2 } )
    {
        CatalogBandFacts band;
        band.index = index;
        band.hasNoData = true;
        band.noDataValue = -9999.0;
        catalog.bands.push_back( band );
    }
    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;
    const RemoteSensingAssetState aligned = resolveAssetState( input ).state;
    CHECK( aligned.validity.noDataPolicy == "declared" );
    CHECK( claimFor( aligned, "validity.no_data_policy" ).kind == ClaimKind::Known );
}

// ---------------------------------------------------------------------------
// LT1-6 — faultlab relative tolerance
// ---------------------------------------------------------------------------

TEST_CASE( "LT1-6: the observable tolerance band scales with the compared magnitude",
           "[swarm_review][lt1][faultlab]" )
{
    using namespace sicnu::faultlab;

    auto makeNumber = []( const std::string &id, double value ) {
        Observable observable;
        observable.id = id;
        observable.kind = ObservableKind::Number;
        observable.number = value;
        return observable;
    };

    // Same absolute wobble (1e-4) on two very different magnitudes.
    ObservableSet clean;
    clean.emplace( "big", makeNumber( "big", 1000000.0 ) );
    clean.emplace( "small", makeNumber( "small", 0.001 ) );
    ObservableSet faulted;
    faulted.emplace( "big", makeNumber( "big", 1000000.0 + 1e-4 ) );
    faulted.emplace( "small", makeNumber( "small", 0.001 + 1e-4 ) );

    ObservableExpectation bigChanged;
    bigChanged.id = "big";
    bigChanged.relation = ExpectationRelation::Changed;
    ObservableExpectation smallChanged;
    smallChanged.id = "small";
    smallChanged.relation = ExpectationRelation::Changed;

    const std::vector< ExpectationResult > results =
        checkExpectations( clean, faulted, { bigChanged, smallChanged } );
    REQUIRE( results.size() == 2 );

    // 1e-4 on a 1e6 scale is noise: the relative band (1e-9 * 1e6) swallows
    // it. Master's absolute band called it a change.
    CHECK_FALSE( results[ 0 ].passed );
    // The same 1e-4 on a 1e-3 scale is a real difference.
    CHECK( results[ 1 ].passed );

    // The helper's contract: the constant is the floor for near-zero
    // operands, never the whole band for large ones.
    CHECK( observableToleranceBand( 0.0 ) == kObservableTolerance );
    CHECK( observableToleranceBand( 1000000.0 ) > kObservableTolerance );
}

// ---------------------------------------------------------------------------
// LT2-1 / LT2-3 / LT2-5 — agent_loop session
// ---------------------------------------------------------------------------

namespace {

sicnu::agent_loop::ScientificAgentSession::Dependencies tailDependencies(
    sicnu::agent_loop::FakeSeams &seams )
{
    return { &seams.dataProvider(), &seams.planner(), &seams.preflight(), &seams.executor(),
             &seams.verifier(), &seams.diagnoser() };
}

sicnu::agent_loop::SessionRunRequest tailRequest()
{
    sicnu::agent_loop::SessionRunRequest request;
    request.goal = "compute NDVI for the scene";
    request.intent = "ndvi";
    return request;
}

bool hasDecision( const sicnu::agent_loop::EvidenceSummary &summary, const std::string &stage,
                  const std::string &action )
{
    for ( const auto &decision : summary.decisions )
        if ( decision.stage == stage && decision.selected[ "action" ] == action )
            return true;
    return false;
}

/// A verifier that reports NOTHING verified (empty artifact list) — the
/// production adapter's shape when execution produced no artifact.
class EmptyVerifier final : public sicnu::agent_loop::IVerifier
{
  public:
    sicnu::agent_loop::VerificationReport verify( const sicnu::agent_loop::PlanDraft &,
                                                  const sicnu::agent_loop::ExecutionOutcome & ) override
    {
        return sicnu::agent_loop::VerificationReport{};
    }
};

} // namespace

TEST_CASE( "LT2-1: a fixable preflight with zero proposals never bypasses the repair policy",
           "[swarm_review][lt2][agent_loop]" )
{
    using namespace sicnu::agent_loop;

    auto scenarioFor = [] {
        FakeScenario scenario;
        scenario.preflight = { PreflightScript{ "fixable", {} } }; // fixable, ZERO proposals
        scenario.execution = { { true, "", { "ndvi.tif" } } };
        scenario.verification = { { "PASS", "" } };
        return scenario;
    };

    // Default policy (allowUnapprovedFixable == false): master fell through
    // all three repair-approval branches and executed anyway.
    {
        const FakeScenario scenario = scenarioFor();
        FakeSeams seams( scenario );
        SessionPolicy policy = SessionPolicy::defaults();
        policy.mode = RunMode::ExecuteWithVerify;
        auto deps = tailDependencies( seams );
        ScientificAgentSession session( policy, deps, {}, "sess-tail-fixable-empty" );
        const SessionResult result = session.run( tailRequest() );

        CHECK_FALSE( result.ok );
        CHECK( result.terminalState == terminal_states::kRefused );
        CHECK( result.stopReason == stop_reasons::kPreflightBlocked );
        CHECK( seams.executorFake().beginCount() == 0 );
    }

    // Opt-in policy: proceeding unfixed is allowed, but the decision is
    // recorded — never silent.
    {
        const FakeScenario scenario = scenarioFor();
        FakeSeams seams( scenario );
        SessionPolicy policy = SessionPolicy::defaults();
        policy.mode = RunMode::ExecuteWithVerify;
        policy.repairApproval.allowUnapprovedFixable = true;
        ScientificAgentSession session( policy, tailDependencies( seams ), {}, "sess-tail-fixable-allowed" );
        const SessionResult result = session.run( tailRequest() );

        CHECK( result.ok );
        CHECK( result.terminalState == terminal_states::kDelivered );
        CHECK( hasDecision( result.summary, stages::kRepairApproval, "proceed_unfixed" ) );
        CHECK( seams.executorFake().beginCount() == 1 );
    }
}

TEST_CASE( "LT2-3: a verification aggregate over zero artifacts is INDETERMINATE, not PASS",
           "[swarm_review][lt2][agent_loop]" )
{
    using namespace sicnu::agent_loop;

    CHECK( VerificationReport::aggregate( {} ) == kVerificationIndeterminate );
    CHECK( VerificationReport{}.verdict() == kVerificationIndeterminate );

    ArtifactVerificationReport pass;
    pass.path = "ndvi.tif";
    pass.verdict = "PASS";
    CHECK( VerificationReport::aggregate( { pass } ) == "PASS" );
    ArtifactVerificationReport warned = pass;
    warned.verdict = "PASS_WITH_WARNINGS";
    CHECK( VerificationReport::aggregate( { pass, warned } ) == "PASS_WITH_WARNINGS" );
    ArtifactVerificationReport failed = pass;
    failed.verdict = "FAIL";
    CHECK( VerificationReport::aggregate( { pass, failed } ) == "FAIL" );

    // End to end: a verifier that verified nothing must not deliver success.
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } } };
    FakeSeams seams( scenario );
    auto deps = tailDependencies( seams );
    EmptyVerifier verifier;
    deps.verifier = &verifier;
    SessionPolicy policy = SessionPolicy::defaults();
    policy.mode = RunMode::ExecuteWithVerify;
    ScientificAgentSession session( policy, deps, {}, "sess-tail-nothing-verified" );
    const SessionResult result = session.run( tailRequest() );

    CHECK_FALSE( result.ok );
    CHECK( result.terminalState == terminal_states::kRefused );
    CHECK( result.stopReason == stop_reasons::kOutputInvalid );
    CHECK( result.summary.verificationVerdict == kVerificationIndeterminate );
}

TEST_CASE( "LT2-5: empty fake script vectors fail closed instead of underflowing",
           "[swarm_review][lt2][agent_loop]" )
{
    using namespace sicnu::agent_loop;

    // Every seam with an empty script vector must answer a typed failure;
    // master indexed size()-1 (SIZE_MAX) — out-of-bounds UB.
    {
        FakeScenario scenario;
        scenario.preflight = {}; // no preflight script at all
        FakeSeams seams( scenario );
        SessionPolicy policy = SessionPolicy::defaults();
        policy.mode = RunMode::ExecuteWithVerify;
        ScientificAgentSession session( policy, tailDependencies( seams ), {}, "sess-tail-no-preflight" );
        const SessionResult result = session.run( tailRequest() );

        CHECK_FALSE( result.ok );
        CHECK( result.terminalState == terminal_states::kRefused );
        CHECK( result.stopReason == stop_reasons::kPreflightBlocked );
    }
    {
        FakeScenario scenario;
        scenario.preflight = { PreflightScript{ "ok", {} } };
        scenario.execution = {}; // no execution script
        FakeSeams seams( scenario );
        SessionPolicy policy = SessionPolicy::defaults();
        policy.mode = RunMode::ExecuteWithVerify;
        ScientificAgentSession session( policy, tailDependencies( seams ), {}, "sess-tail-no-execution" );
        const SessionResult result = session.run( tailRequest() );

        CHECK_FALSE( result.ok );
        CHECK( seams.executorFake().beginCount() == 1 ); // it ran, and failed typed
        CHECK( result.stopReason == std::string( "EXECUTION_FAILED" ) );
    }
    {
        FakeScenario scenario;
        scenario.preflight = { PreflightScript{ "ok", {} } };
        scenario.execution = { { true, "", { "ndvi.tif" } } };
        scenario.verification = {}; // no verification script
        FakeSeams seams( scenario );
        SessionPolicy policy = SessionPolicy::defaults();
        policy.mode = RunMode::ExecuteWithVerify;
        ScientificAgentSession session( policy, tailDependencies( seams ), {}, "sess-tail-no-verification" );
        const SessionResult result = session.run( tailRequest() );

        CHECK_FALSE( result.ok );
        CHECK( result.terminalState == terminal_states::kRefused );
        CHECK( result.stopReason == stop_reasons::kOutputInvalid );
    }
    {
        FakeScenario scenario;
        scenario.preflight = { PreflightScript{ "ok", {} } };
        scenario.execution = { { false, "EXEC_FAILED", {} } };
        scenario.diagnosis = {}; // no diagnosis script
        FakeSeams seams( scenario );
        SessionPolicy policy = SessionPolicy::defaults();
        policy.mode = RunMode::ExecuteWithVerify;
        ScientificAgentSession session( policy, tailDependencies( seams ), {}, "sess-tail-no-diagnosis" );
        const SessionResult result = session.run( tailRequest() );

        CHECK_FALSE( result.ok );
        CHECK( result.terminalState == terminal_states::kRefused );
    }
}

// ---------------------------------------------------------------------------
// LT2-2 — agent_loop journal bound
// ---------------------------------------------------------------------------

TEST_CASE( "LT2-2: the journal loader stats before it reads", "[swarm_review][lt2][agent_loop]" )
{
    using namespace sicnu::agent_loop;

    const std::filesystem::path dir = tempDir( "journal" );

    // Happy path unchanged: a saved journal still loads.
    {
        SessionJournal journal( "sess-tail-ok" );
        journal.append( "stage_enter", stages::kGoalNormalization, Json::objectValue, 1 );
        std::string error;
        REQUIRE( journal.save( dir.string(), &error ) );
        const auto loaded = SessionJournal::load( dir.string(), "sess-tail-ok", &error );
        REQUIRE( loaded.has_value() );
        CHECK( loaded->size() == 1 );
    }

    // A document larger than the write cap is hostile input: typed
    // fail-closed nullopt, never an unbounded rdbuf slurp. (Sparse file —
    // the logical size is what the loader must bound.)
    {
        const auto oversize =
            dir / ( "sess-tail-huge" + std::string( ".json" ) );
        // resize_file() truncates — it does not create: touch the file first
        // so the sparse extension below actually lands.
        { std::ofstream create( oversize, std::ios::binary ); }
        std::error_code ec;
        std::filesystem::resize_file(
            oversize, static_cast< std::uintmax_t >( SessionJournal::kJournalWriteCapBytes ) + 1, ec );
        REQUIRE_FALSE( ec );

        std::string error;
        const auto loaded = SessionJournal::load( dir.string(), "sess-tail-huge", &error );
        CHECK_FALSE( loaded.has_value() );
        CHECK( error.find( "byte bound" ) != std::string::npos );
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

// ---------------------------------------------------------------------------
// LT2-4 — teaching verdict stamp
// ---------------------------------------------------------------------------

TEST_CASE( "LT2-4: the verdict-stamp content digest covers every byte",
           "[swarm_review][lt2][teaching]" )
{
    using namespace sicnu::teaching;

    const std::filesystem::path dir = tempDir( "verdict_stamp" );
    const auto artifact = dir / "graded.tif";

    // A real-shaped artifact (>512 KiB) with a marker in a region the old
    // window sampling never read.
    std::string bytes( 3 * 512 * 1024, 'a' );
    const std::size_t marker = 768 * 1024;
    bytes[ marker ] = 'Z';
    writeFile( artifact, bytes );

    const std::string digest = verdictArtifactContentDigest( artifact.string() );
    CHECK( digest.size() == 64 );
    // The digest IS the full-file sha256 — master hashed sampled windows.
    CHECK( digest == sicnu::lab::sha256Hex( bytes ) );

    // Tampering between the old sample windows must trip drift.
    std::string tampered = bytes;
    tampered[ marker ] = 'Y';
    writeFile( artifact, tampered );
    CHECK( verdictArtifactContentDigest( artifact.string() ) != digest );

    // The stamp binds the summary digest, the size and the full digest.
    const std::string stamp = makeVerdictStamp( std::string( 64, '0' ), artifact.string() );
    CHECK( stamp.rfind( "v1:", 0 ) == 0 );
    const VerdictStamp parsed = parseVerdictStamp( stamp );
    REQUIRE( parsed.parsed );
    CHECK( parsed.size == static_cast< unsigned long long >( tampered.size() ) );
    CHECK( parsed.contentDigest == sicnu::lab::sha256Hex( tampered ) );

    // An empty artifact has no bytes to bind: fail closed, never a match.
    const auto empty = dir / "empty.tif";
    writeFile( empty, std::string() );
    CHECK( verdictArtifactContentDigest( empty.string() ).empty() );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

// ---------------------------------------------------------------------------
// LT2-6 — explain guidance store
// ---------------------------------------------------------------------------

TEST_CASE( "LT2-6: the guidance directory scan stats before it reads",
           "[swarm_review][lt2][explain]" )
{
    using namespace sicnu::explain;

    const std::filesystem::path dir = tempDir( "guidance" );

    const std::string entry = R"({
  "schema": "exp.step_guidance.v1",
  "operatorId": "rs:ndvi",
  "purpose": "teaching purpose"
})";
    writeFile( dir / "ndvi.json", entry );

    // Oversize document (sparse: the logical size is the scan surface).
    // resize_file() truncates — it does not create: touch the file first.
    { std::ofstream create( dir / "huge.json", std::ios::binary ); }
    std::error_code ec;
    std::filesystem::resize_file( dir / "huge.json", 16ull * 1024ull * 1024ull + 1, ec );
    REQUIRE_FALSE( ec );

    std::vector< GuidanceLoadProblem > problems;
    const std::unique_ptr< GuidanceStore > store =
        GuidanceStore::loadFromDirectory( dir.string(), problems );

    REQUIRE( store != nullptr );
    CHECK( store->entryCount() == 1 );
    CHECK( store->guidanceFor( "rs:ndvi", "" ).has_value() );
    bool oversize = false;
    for ( const GuidanceLoadProblem &problem : problems )
        if ( problem.code == "file_oversize" )
            oversize = true;
    // Master slurped the whole file and reported a parse failure instead.
    CHECK( oversize );

    std::filesystem::remove_all( dir, ec );
}

// ---------------------------------------------------------------------------
// LT1-4 — teaching_admin operator catalog
// ---------------------------------------------------------------------------

TEST_CASE( "LT1-4: an oversize operator sidecar is a typed issue, never a slurp",
           "[swarm_review][lt1][teaching_admin]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    {
        QFile sidecar( dir.filePath( QStringLiteral( "rs-ndvi.json" ) ) );
        REQUIRE( sidecar.open( QIODevice::WriteOnly ) );
        sidecar.write( R"({ "id": "rs:ndvi" })" );
        sidecar.close();
    }
    // Oversize sidecar (sparse: the logical size is what must be bounded).
    {
        QFile sidecar( dir.filePath( QStringLiteral( "rs-huge.json" ) ) );
        REQUIRE( sidecar.open( QIODevice::WriteOnly ) );
        sidecar.close();
        std::error_code ec;
        std::filesystem::resize_file(
            sidecar.fileName().toStdString(), 16ull * 1024ull * 1024ull + 1, ec );
        REQUIRE_FALSE( ec );
    }

    const sicnu::teaching_admin::OperatorCatalog catalog =
        sicnu::teaching_admin::loadOperatorCatalog( dir.path() );

    CHECK( catalog.operatorIds.contains( QStringLiteral( "rs:ndvi" ) ) );
    bool oversize = false;
    for ( const sicnu::teaching_admin::AdminIssue &issue : catalog.issues )
        if ( issue.code == QStringLiteral( "operator_sidecar_oversize" ) )
            oversize = true;
    // Master readAll()'d the whole file into memory before parsing.
    CHECK( oversize );
}
