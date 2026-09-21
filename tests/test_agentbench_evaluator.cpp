// tests/test_agentbench_evaluator.cpp
//
// Slice E RED: the 8-metric evaluator + closed failure taxonomy.
// Every metric has a passing AND a demonstrably failing probe (anti-vacuous);
// recovery quality is null-with-reason on fault-free cases; classification
// priority is deterministic.

#include <catch2/catch_test_macros.hpp>

#include "agentbench/case_schema.h"
#include "agentbench/evaluator.h"
#include "agentbench/fake_agent.h"
#include "agentbench/json_writer.h"
#include "agentbench/trace.h"

#include <json/json.h>

#include <string>

using namespace sicnu::agentbench;

namespace
{

struct World
{
	AgentCase agentCase;
	AgentScript script;

	CaseEvaluation run() const
	{
		const ScriptRun scriptRun = runScript( agentCase, script );
		REQUIRE( scriptRun.error.code.empty() );
		REQUIRE( scriptRun.trace.has_value() );
		return evaluateCase( agentCase, *scriptRun.trace );
	}
};

Json::Value baseCaseDoc( const Json::Value &extra = Json::Value() )
{
	Json::Value doc{Json::objectValue};
	doc["schema"] = "sicnu.agentbench.case/v1";
	doc["case_id"] = "optical/eval";
	doc["title"] = "Eval case";
	doc["task_family"] = "optical";
	doc["goal"] = "Deliver NDVI.";
	Json::Value initial{Json::objectValue};
	initial["scene"] = "work://c/scene.tif";
	Json::Value roots{Json::arrayValue};
	roots.append( "work://c" );
	initial["workspace_roots"] = roots;
	doc["initial_state"] = initial;
	Json::Value tools{Json::arrayValue};
	tools.append( "rs:ndvi" );
	tools.append( "harness:verify" );
	doc["allowed_tools"] = tools;

	Json::Value invariants{Json::arrayValue};
	Json::Value sci{Json::objectValue};
	sci["id"] = "sci-verdict";
	sci["kind"] = "verdict_is";
	sci["dimension"] = "scientific";
	sci["severity"] = "error";
	Json::Value sciParams{Json::objectValue};
	sciParams["evidence_id"] = "ndvi";
	sciParams["verdict"] = "PASS";
	sci["params"] = sciParams;
	invariants.append( sci );

	Json::Value warn{Json::objectValue};
	warn["id"] = "warn-explain";
	warn["kind"] = "explanation_mentions";
	warn["dimension"] = "process";
	warn["severity"] = "warning";
	Json::Value warnParams{Json::objectValue};
	warnParams["phrase"] = "cloud";
	warn["params"] = warnParams;
	invariants.append( warn );
	doc["invariants"] = invariants;

	Json::Value ev{Json::objectValue};
	ev["id"] = "ndvi";
	ev["kind"] = "raster";
	ev["required_fields"] = Json::Value( Json::arrayValue );
	Json::Value evs{Json::arrayValue};
	evs.append( ev );
	doc["expected_evidence"] = evs;

	Json::Value budget{Json::objectValue};
	budget["max_tool_calls"] = 8;
	budget["max_tokens"] = 4000;
	budget["max_retries"] = 2;
	doc["resource_budget"] = budget;
	doc["minimal_steps"] = 2;
	if ( !extra.isNull() && extra.isObject() )
	{
		for ( const auto &name : extra.getMemberNames() )
			doc[name] = extra[name];
	}
	return doc;
}

World happyWorld( const Json::Value &caseExtra = Json::Value(), const Json::Value &scriptExtra = Json::Value() )
{
	World world;
	world.agentCase = parseCase( deterministicSerialize( baseCaseDoc( caseExtra ) ) ).parsed.value();

	Json::Value script{Json::objectValue};
	script["schema"] = "sicnu.agentbench.script/v1";
	script["script_id"] = "ref/eval";
	script["case_id"] = "optical/eval";
	Json::Value agent{Json::objectValue};
	agent["name"] = "scripted-reference";
	agent["version"] = "1.0.0";
	script["agent"] = agent;
	script["explanation"] = "Computed NDVI with a cloud mask and verified the delivery.";

	Json::Value steps{Json::arrayValue};
	Json::Value step0{Json::objectValue};
	step0["tool"] = "rs:ndvi";
	Json::Value input0{Json::objectValue};
	input0["scene"] = "work://c/scene.tif";
	step0["input"] = input0;
	Json::Value payload0{Json::objectValue};
	payload0["output_path"] = "work://c/ndvi.tif";
	step0["payload"] = payload0;
	steps.append( step0 );

	Json::Value step1{Json::objectValue};
	step1["tool"] = "harness:verify";
	step1["input"] = Json::Value( Json::objectValue );
	Json::Value payload1{Json::objectValue};
	payload1["verdict"] = "PASS";
	payload1["output_path"] = "work://c/ndvi.tif";
	Json::Value fields{Json::objectValue};
	fields["mean"] = 0.42;
	payload1["fields"] = fields;
	step1["payload"] = payload1;
	step1["evidence"] = "ndvi";
	steps.append( step1 );

	if ( scriptExtra.isObject() && scriptExtra.isMember( "steps" ) )
		script["steps"] = scriptExtra["steps"];
	else
		script["steps"] = steps;
	if ( scriptExtra.isObject() )
	{
		for ( const auto &name : scriptExtra.getMemberNames() )
			if ( name != "steps" )
				script[name] = scriptExtra[name];
	}
	world.script = parseScript( deterministicSerialize( script ) ).parsed.value();
	return world;
}

Json::Value faultList( const char *kind, int atStep )
{
	Json::Value faults{Json::arrayValue};
	Json::Value fault{Json::objectValue};
	fault["kind"] = kind;
	fault["at_step"] = atStep;
	faults.append( fault );
	return faults;
}

Json::Value retryOverrides( const char *policy )
{
	// Rebuild the two happy-path steps with an explicit failure policy.
	Json::Value steps{Json::arrayValue};
	Json::Value step0{Json::objectValue};
	step0["tool"] = "rs:ndvi";
	Json::Value input0{Json::objectValue};
	input0["scene"] = "work://c/scene.tif";
	step0["input"] = input0;
	Json::Value payload0{Json::objectValue};
	payload0["output_path"] = "work://c/ndvi.tif";
	step0["payload"] = payload0;
	step0["on_failure"] = policy;
	steps.append( step0 );
	Json::Value step1{Json::objectValue};
	step1["tool"] = "harness:verify";
	step1["input"] = Json::Value( Json::objectValue );
	Json::Value payload1{Json::objectValue};
	payload1["verdict"] = "PASS";
	payload1["output_path"] = "work://c/ndvi.tif";
	Json::Value fields{Json::objectValue};
	fields["mean"] = 0.42;
	payload1["fields"] = fields;
	step1["payload"] = payload1;
	step1["evidence"] = "ndvi";
	steps.append( step1 );
	return steps;
}

const MetricResult &metric( const CaseEvaluation &evaluation, const std::string &name )
{
	const MetricResult *found = evaluation.metric( name );
	REQUIRE( found != nullptr );
	return *found;
}

} // namespace

TEST_CASE( "happy path scores full marks with recovery unavailable-by-design", "[agentbench][evaluator]" )
{
	const CaseEvaluation evaluation = happyWorld().run();
	CHECK( evaluation.verdict == BenchVerdict::Pass );
	CHECK( evaluation.failureClass == "none" );
	CHECK( metric( evaluation, "task_completion" ).value.asDouble() == 1.0 );
	CHECK( metric( evaluation, "scientific_validity" ).value.asDouble() == 1.0 );
	CHECK( metric( evaluation, "unnecessary_transformations" ).value.asInt() == 0 );
	CHECK( metric( evaluation, "plan_efficiency" ).value.asDouble() == 1.0 );
	CHECK( metric( evaluation, "verifier_pass_rate" ).value.asDouble() == 1.0 );
	CHECK( metric( evaluation, "recovery_quality" ).value.isNull() );
	CHECK( metric( evaluation, "recovery_quality" ).reason == "no_faults_injected" );
	CHECK( metric( evaluation, "reproducibility" ).value.asDouble() == 1.0 );
	CHECK( metric( evaluation, "explanation_completeness" ).value.asDouble() == 1.0 );
	CHECK( evaluation.metrics.size() == 8 );
	CHECK( evaluation.usage.toolCalls == 2 );
}

TEST_CASE( "corrupted science fails the run; dishonest claim is a silent_failure", "[agentbench][evaluator]" )
{
	Json::Value caseExtra{Json::objectValue};
	caseExtra["faults"] = faultList( "corrupted_result", 1 );
	World world = happyWorld( caseExtra );
	const CaseEvaluation evaluation = world.run();
	CHECK( evaluation.verdict == BenchVerdict::Fail );
	CHECK( metric( evaluation, "scientific_validity" ).value.asDouble() == 0.0 );
	// The agent completed the flow, so the default claim claims success — over
	// failed science. That is exactly the silent-failure signature, which
	// outranks invalid_science in the closed classification.
	CHECK( metric( evaluation, "explanation_completeness" ).value.asDouble() < 1.0 ); // dishonest claim
	CHECK( evaluation.failureClass == "silent_failure" );
}

TEST_CASE( "honest refusal of corrupted science classifies as invalid_science", "[agentbench][evaluator]" )
{
	Json::Value caseExtra{Json::objectValue};
	caseExtra["faults"] = faultList( "corrupted_result", 1 );
	Json::Value scriptExtra{Json::objectValue};
	Json::Value claim{Json::objectValue};
	claim["success"] = false;
	claim["note"] = "refusing";
	scriptExtra["outcome_claim"] = claim;
	World world = happyWorld( caseExtra, scriptExtra );
	const CaseEvaluation evaluation = world.run();
	CHECK( evaluation.verdict == BenchVerdict::Fail );
	CHECK( metric( evaluation, "explanation_completeness" ).value.asDouble() == 1.0 ); // honest claim, explained
	CHECK( evaluation.failureClass == "invalid_science" );
}

TEST_CASE( "duplicate successful computation is counted as unnecessary", "[agentbench][evaluator]" )
{
	Json::Value scriptExtra{Json::objectValue};
	Json::Value steps = retryOverrides( "abort" );
	// Step 0 twice with identical (tool,input): the second successful run is waste.
	Json::Value ndviCopy = steps[0];
	Json::Value stepsWithCopy{Json::arrayValue};
	stepsWithCopy.append( steps[0] );
	stepsWithCopy.append( ndviCopy );
	stepsWithCopy.append( steps[1] );
	scriptExtra["steps"] = stepsWithCopy;
	World world = happyWorld( Json::Value(), scriptExtra );
	const CaseEvaluation evaluation = world.run();
	CHECK( metric( evaluation, "unnecessary_transformations" ).value.asInt() >= 1 );
	CHECK( metric( evaluation, "plan_efficiency" ).value.asDouble() < 1.0 );
}

TEST_CASE( "declared redundant tools are counted when used", "[agentbench][evaluator]" )
{
	Json::Value caseExtra{Json::objectValue};
	Json::Value redundant{Json::arrayValue};
	redundant.append( "harness:verify" );
	caseExtra["redundant_tools"] = redundant;
	World world = happyWorld( caseExtra );
	const CaseEvaluation evaluation = world.run();
	CHECK( metric( evaluation, "unnecessary_transformations" ).value.asInt() == 1 );
}

TEST_CASE( "transient fault recovered by retry scores full recovery", "[agentbench][evaluator]" )
{
	Json::Value caseExtra{Json::objectValue};
	caseExtra["faults"] = faultList( "transient_failure", 0 );
	Json::Value scriptExtra{Json::objectValue};
	scriptExtra["steps"] = retryOverrides( "retry_once" );
	World world = happyWorld( caseExtra, scriptExtra );
	const CaseEvaluation evaluation = world.run();
	CHECK( evaluation.verdict == BenchVerdict::Pass );
	CHECK( metric( evaluation, "recovery_quality" ).value.asDouble() == 1.0 );
	CHECK( evaluation.failureClass == "none" );
}

TEST_CASE( "unrecovered persistent fault fails recovery and the run", "[agentbench][evaluator]" )
{
	Json::Value caseExtra{Json::objectValue};
	caseExtra["faults"] = faultList( "tool_unavailable", 0 );
	// retry_once against a persistent fault: exhausts → gave_up, evidence missing.
	Json::Value scriptExtra{Json::objectValue};
	scriptExtra["steps"] = retryOverrides( "retry_once" );
	World world = happyWorld( caseExtra, scriptExtra );
	const CaseEvaluation evaluation = world.run();
	CHECK( evaluation.verdict == BenchVerdict::Fail );
	CHECK( metric( evaluation, "recovery_quality" ).value.asDouble() == 0.0 );
	CHECK( evaluation.failureClass == "verification_failed" ); // verdict FAIL via missing evidence
}

TEST_CASE( "routing around a fault (skip) still counts as handled recovery", "[agentbench][evaluator]" )
{
	// The NDVI step fails transiently and the script skips it; the verify step
	// still declares evidence, so the goal survives. Handled = reached a
	// non-gave_up terminal state after the fault.
	Json::Value caseExtra{Json::objectValue};
	caseExtra["faults"] = faultList( "transient_failure", 0 );
	Json::Value scriptExtra{Json::objectValue};
	scriptExtra["steps"] = retryOverrides( "skip" );
	World world = happyWorld( caseExtra, scriptExtra );
	const CaseEvaluation evaluation = world.run();
	CHECK( metric( evaluation, "recovery_quality" ).value.asDouble() == 1.0 );
}

TEST_CASE( "warning-only failures downgrade to PASS_WITH_WARNINGS", "[agentbench][evaluator]" )
{
	World world = happyWorld();
	world.script.explanation = ""; // warning invariant asks for "cloud" mention
	const ScriptRun scriptRun = runScript( world.agentCase, world.script );
	REQUIRE( scriptRun.trace.has_value() );
	const CaseEvaluation evaluation = evaluateCase( world.agentCase, *scriptRun.trace );
	CHECK( evaluation.verdict == BenchVerdict::PassWithWarnings );
	CHECK( evaluation.failureClass == "none" );
}

TEST_CASE( "scope violations and empty traces classify before everything", "[agentbench][evaluator]" )
{
	// Rogue trace: tool outside allow-list.
	AgentTrace rogue;
	rogue.traceId = "rogue";
	rogue.caseId = "optical/eval";
	rogue.agentName = "x";
	rogue.agentKind = AgentKind::Recorded;
	rogue.agentVersion = "1";
	TraceStep step;
	step.index = 0;
	step.tool = "rs:forbidden";
	step.success = true;
	step.tokens = 1;
	rogue.steps.push_back( step );
	rogue.stopReason = StopReason::Completed;

	const AgentCase agentCase = parseCase( deterministicSerialize( baseCaseDoc() ) ).parsed.value();
	const CaseEvaluation evaluation = evaluateCase( agentCase, rogue );
	CHECK( evaluation.verdict == BenchVerdict::Fail );
	CHECK( evaluation.failureClass == "scope_violation" );

	// Empty trace → not_started.
	AgentTrace empty = rogue;
	empty.steps.clear();
	empty.caseId = "optical/eval";
	empty.stopReason = StopReason::GaveUp;
	const CaseEvaluation emptyEvaluation = evaluateCase( agentCase, empty );
	CHECK( emptyEvaluation.failureClass == "not_started" );
	CHECK( metric( emptyEvaluation, "plan_efficiency" ).value.isNull() );
}

TEST_CASE( "evaluation json is versioned and digest-stable", "[agentbench][evaluator]" )
{
	const CaseEvaluation evaluation = happyWorld().run();
	const Json::Value json = evaluationToJson( evaluation );
	CHECK( json["schema"].asString() == "sicnu.agentbench.evaluation/v1" );
	CHECK( json["framework_version"].asString() == std::string( kAgentBenchFrameworkVersion ) );
	CHECK( json["verdict"].asString() == "PASS" );
	CHECK( json["metrics"].size() == 8 );

	const CaseEvaluation again = happyWorld().run();
	CHECK( evaluation.digest == again.digest );
	CHECK( !evaluation.digest.empty() );
}

// ------------------------------------------------------------------
// Review-pass-1 probes: pairing gate, claim_mismatch reachability,
// verifier kind/path oracle, recovery_failed / incomplete fixtures.
// ------------------------------------------------------------------

TEST_CASE( "a mispaired case/trace is never graded as a pass", "[agentbench][evaluator]" )
{
	World world = happyWorld();
	const ScriptRun scriptRun = runScript( world.agentCase, world.script );
	REQUIRE( scriptRun.trace.has_value() );
	AgentTrace trace = *scriptRun.trace; // fully passing trajectory…
	trace.caseId = "other/case";         // …bound to the wrong case

	const CaseEvaluation evaluation = evaluateCase( world.agentCase, trace );
	CHECK( evaluation.verdict == BenchVerdict::Fail );
	CHECK( evaluation.failureClass == "scope_violation" );
	REQUIRE( evaluation.replayViolations.size() == 1 );
	CHECK( evaluation.replayViolations[0]["code"].asString() == "agentbench.trace_invalid" );
}

TEST_CASE( "denying a completed passing run is a claim_mismatch", "[agentbench][evaluator]" )
{
	World world = happyWorld();
	const ScriptRun scriptRun = runScript( world.agentCase, world.script );
	REQUIRE( scriptRun.trace.has_value() );
	AgentTrace trace = *scriptRun.trace; // fully passing…
	trace.outcomeClaimSuccess = false;   // …but the agent denies it
	const CaseEvaluation evaluation = evaluateCase( world.agentCase, trace );
	CHECK( evaluation.verdict == BenchVerdict::Pass );
	CHECK( evaluation.failureClass == "claim_mismatch" );
}

TEST_CASE( "verifier rejects deliveries of the wrong kind or path", "[agentbench][evaluator]" )
{
	// Case whose evidence declares kind and path; script delivers both.
	Json::Value caseExtra{Json::objectValue};
	// baseCaseDoc cannot reach into expected_evidence via the merge, so swap
	// the whole expected_evidence through a targeted doc edit.
	Json::Value doc = baseCaseDoc();
	doc["expected_evidence"] = Json::Value( Json::arrayValue );
	Json::Value entry{Json::objectValue};
	entry["id"] = "ndvi";
	entry["kind"] = "raster";
	entry["path"] = "work://c/ndvi.tif";
	entry["required_fields"] = Json::Value( Json::arrayValue );
	doc["expected_evidence"].append( entry );

	World world;
	world.agentCase = parseCase( deterministicSerialize( doc ) ).parsed.value();
	world.script = happyWorld().script; // delivers id ndvi, kind raster, path work://c/ndvi.tif
	const ScriptRun scriptRun = runScript( world.agentCase, world.script );
	REQUIRE( scriptRun.trace.has_value() );

	CHECK( evaluateCase( world.agentCase, *scriptRun.trace ).verdict == BenchVerdict::Pass );

	AgentTrace wrongKind = *scriptRun.trace;
	wrongKind.evidence[0].kind = "table";
	CHECK( evaluateCase( world.agentCase, wrongKind ).verdict == BenchVerdict::Fail );

	AgentTrace wrongPath = *scriptRun.trace;
	wrongPath.evidence[0].path = "work://c/other.tif";
	const CaseEvaluation evaluation = evaluateCase( world.agentCase, wrongPath );
	CHECK( evaluation.verdict == BenchVerdict::Fail );
	CHECK( metric( evaluation, "verifier_pass_rate" ).value.asDouble() == 0.0 );
}

TEST_CASE( "an unhandled fault on the final step fails recovery while passing", "[agentbench][evaluator]" )
{
	// Evidence comes from step 0; the fault hits the final step and the
	// script routes around it (skip) — the run passes, but the fault was
	// never recovered: recovery_failed.
	Json::Value caseExtra{Json::objectValue};
	caseExtra["faults"] = faultList( "transient_failure", 1 );
	Json::Value steps{Json::arrayValue};
	Json::Value step0{Json::objectValue};
	step0["tool"] = "rs:ndvi";
	step0["input"] = Json::Value( Json::objectValue );
	Json::Value payload0{Json::objectValue};
	payload0["verdict"] = "PASS";
	payload0["output_path"] = "work://c/ndvi.tif";
	step0["payload"] = payload0;
	step0["evidence"] = "ndvi";
	steps.append( step0 );
	Json::Value step1{Json::objectValue};
	step1["tool"] = "harness:verify";
	step1["input"] = Json::Value( Json::objectValue );
	step1["payload"] = Json::Value( Json::objectValue );
	step1["on_failure"] = "skip";
	steps.append( step1 );
	Json::Value scriptExtra{Json::objectValue};
	scriptExtra["steps"] = steps;

	World world = happyWorld( caseExtra, scriptExtra );
	const CaseEvaluation evaluation = world.run();
	CHECK( evaluation.verdict == BenchVerdict::Pass );
	CHECK( metric( evaluation, "recovery_quality" ).value.asDouble() == 0.0 );
	CHECK( evaluation.failureClass == "recovery_failed" );
}

TEST_CASE( "an honest failed run with delivered evidence classifies incomplete", "[agentbench][evaluator]" )
{
	// Hand-built: evidence delivered (verifier + science fine), a process
	// error invariant fails, claim honest, stopped early.
	AgentCase agentCase = parseCase( deterministicSerialize( baseCaseDoc() ) ).parsed.value();
	Json::Value inv{Json::objectValue};
	Json::Value params{Json::objectValue};
	params["step_index"] = 0;
	inv["id"] = "proc-step0";
	inv["kind"] = "result_success";
	inv["dimension"] = "process";
	inv["severity"] = "error";
	inv["params"] = params;
	// Inject via case doc re-parse to keep parse-time validation in the loop.
	Json::Value doc = baseCaseDoc();
	doc["invariants"].append( inv );
	agentCase = parseCase( deterministicSerialize( doc ) ).parsed.value();

	World world = happyWorld();
	const ScriptRun scriptRun = runScript( world.agentCase, world.script );
	REQUIRE( scriptRun.trace.has_value() );
	AgentTrace trace = *scriptRun.trace;
	trace.caseId = agentCase.caseId;
	trace.steps[0].success = false; // proc-step0 fails
	trace.steps[0].errorCode = "EXECUTION_FAILED";
	trace.stopReason = StopReason::GaveUp;
	trace.outcomeClaimSuccess = false;

	const CaseEvaluation evaluation = evaluateCase( agentCase, trace );
	CHECK( evaluation.verdict == BenchVerdict::Fail );
	CHECK( metric( evaluation, "verifier_pass_rate" ).value.asDouble() == 1.0 );
	CHECK( metric( evaluation, "scientific_validity" ).value.asDouble() == 1.0 );
	CHECK( evaluation.failureClass == "incomplete" );
}
