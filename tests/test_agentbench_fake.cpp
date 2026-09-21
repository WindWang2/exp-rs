// tests/test_agentbench_fake.cpp
//
// Slice C RED: deterministic fake agent — scripted policy → trace.
// Same (case, script) ⇒ byte-identical trace; faults applied per the case
// schedule; on_failure discipline (abort | retry_once | skip) with no fake
// success: a failed script ends blocked/gave_up with an honest claim.

#include <catch2/catch_test_macros.hpp>

#include "agentbench/case_schema.h"
#include "agentbench/fake_agent.h"
#include "agentbench/json_writer.h"
#include "agentbench/trace.h"

#include <json/json.h>

#include <string>

using namespace sicnu::agentbench;

namespace
{

Json::Value caseDoc( const Json::Value &faults = Json::Value() )
{
	Json::Value doc{Json::objectValue};
	doc["schema"] = "sicnu.agentbench.case/v1";
	doc["case_id"] = "optical/ndvi-basic";
	doc["title"] = "Basic NDVI delivery";
	doc["task_family"] = "optical";
	doc["goal"] = "Produce an NDVI raster.";
	Json::Value initial{Json::objectValue};
	initial["scene"] = "work://case-42/scene.tif";
	Json::Value roots{Json::arrayValue};
	roots.append( "work://case-42" );
	initial["workspace_roots"] = roots;
	doc["initial_state"] = initial;
	Json::Value tools{Json::arrayValue};
	tools.append( "rs:ndvi" );
	tools.append( "harness:verify" );
	doc["allowed_tools"] = tools;

	Json::Value invariants{Json::arrayValue};
	Json::Value inv{Json::objectValue};
	inv["id"] = "i1";
	inv["kind"] = "claim_consistent";
	inv["dimension"] = "process";
	inv["severity"] = "error";
	inv["params"] = Json::Value( Json::objectValue );
	invariants.append( inv );
	doc["invariants"] = invariants;

	Json::Value ev{Json::objectValue};
	ev["id"] = "ndvi-raster";
	ev["kind"] = "raster";
	ev["required_fields"] = Json::Value( Json::arrayValue );
	Json::Value evs{Json::arrayValue};
	evs.append( ev );
	doc["expected_evidence"] = evs;

	Json::Value budget{Json::objectValue};
	budget["max_tool_calls"] = 8;
	budget["max_tokens"] = 4000;
	budget["max_retries"] = 1;
	doc["resource_budget"] = budget;
	doc["minimal_steps"] = 2;
	if ( !faults.isNull() )
		doc["faults"] = faults;
	return doc;
}

Json::Value scriptDoc( const Json::Value &stepOverrides = Json::Value( Json::arrayValue ),
                       const Json::Value &extra = Json::Value() )
{
	Json::Value doc{Json::objectValue};
	doc["schema"] = "sicnu.agentbench.script/v1";
	doc["script_id"] = "ref/ndvi-basic";
	doc["case_id"] = "optical/ndvi-basic";
	Json::Value agent{Json::objectValue};
	agent["name"] = "scripted-reference";
	agent["version"] = "1.0.0";
	doc["agent"] = agent;
	doc["seed"] = 7;

	Json::Value steps{Json::arrayValue};
	Json::Value step0{Json::objectValue};
	step0["tool"] = "rs:ndvi";
	Json::Value input0{Json::objectValue};
	input0["scene"] = "work://case-42/scene.tif";
	step0["input"] = input0;
	Json::Value payload0{Json::objectValue};
	payload0["output_path"] = "work://case-42/ndvi.tif";
	step0["payload"] = payload0;
	steps.append( step0 );

	Json::Value step1{Json::objectValue};
	step1["tool"] = "harness:verify";
	step1["input"] = Json::Value( Json::objectValue );
	Json::Value payload1{Json::objectValue};
	payload1["verdict"] = "PASS";
	payload1["output_path"] = "work://case-42/ndvi.tif";
	Json::Value fields{Json::objectValue};
	fields["mean"] = 0.42;
	payload1["fields"] = fields;
	step1["payload"] = payload1;
	step1["evidence"] = "ndvi-raster";
	steps.append( step1 );

	if ( stepOverrides.size() > 0 )
	{
		for ( const auto &name : stepOverrides.getMemberNames() )
		{
			const int index = std::stoi( name );
			steps[index] = stepOverrides[name];
		}
	}
	doc["steps"] = steps;

	doc["explanation"] = "Computed NDVI and verified the delivery.";
	if ( !extra.isNull() )
	{
		for ( const auto &name : extra.getMemberNames() )
			doc[name] = extra[name];
	}
	return doc;
}

ScriptRun run( const Json::Value &caseOverride = Json::Value(), const Json::Value &scriptOverride = Json::Value(),
               const Json::Value &scriptSteps = Json::Value( Json::arrayValue ) )
{
	Json::Value caseJson = caseDoc();
	if ( !caseOverride.isNull() && caseOverride.isObject() )
	{
		for ( const auto &name : caseOverride.getMemberNames() )
			caseJson[name] = caseOverride[name];
	}
	const AgentCase agentCase = parseCase( deterministicSerialize( caseJson ) ).parsed.value();
	Json::Value scriptJson = scriptDoc( scriptSteps, scriptOverride );
	const AgentScript script = parseScript( deterministicSerialize( scriptJson ) ).parsed.value();
	return runScript( agentCase, script );
}

Json::Value caseOverrideWithFaults( const Json::Value &faults )
{
	Json::Value override{Json::objectValue};
	override["faults"] = faults;
	return override;
}

} // namespace

TEST_CASE( "script parses and references its case", "[agentbench][fake]" )
{
	const ScriptParse result = parseScript( deterministicSerialize( scriptDoc() ) );
	REQUIRE( result.error.code.empty() );
	REQUIRE( result.parsed.has_value() );
	CHECK( result.parsed->scriptId == "ref/ndvi-basic" );
	CHECK( result.parsed->caseId == "optical/ndvi-basic" );
	REQUIRE( result.parsed->steps.size() == 2 );
	CHECK( result.parsed->steps[0].onFailure == OnFailure::Abort );
	CHECK( result.parsed->steps[1].evidenceId == "ndvi-raster" );
}

TEST_CASE( "happy-path script produces a clean deterministic trace", "[agentbench][fake]" )
{
	const ScriptRun first = run();
	REQUIRE( first.error.code.empty() );
	REQUIRE( first.trace.has_value() );

	const ReplayValidation validation = validateReplay(
		parseCase( deterministicSerialize( caseDoc() ) ).parsed.value(), *first.trace );
	CHECK( validation.violations.empty() );

	const AgentTrace &trace = *first.trace;
	REQUIRE( trace.steps.size() == 2 );
	CHECK( trace.steps[0].success );
	CHECK( trace.steps[1].success );
	REQUIRE( trace.evidence.size() == 1 );
	CHECK( trace.evidence[0].id == "ndvi-raster" );
	CHECK( trace.evidence[0].verdict == "PASS" );
	CHECK( trace.evidence[0].path == "work://case-42/ndvi.tif" );
	CHECK( trace.stopReason == StopReason::Completed );
	CHECK( trace.outcomeClaimSuccess );
	CHECK( trace.explanation == "Computed NDVI and verified the delivery." );

	const ScriptRun second = run();
	REQUIRE( second.trace.has_value() );
	CHECK( deterministicSerialize( traceToJson( *first.trace ) ) ==
	       deterministicSerialize( traceToJson( *second.trace ) ) );
}

TEST_CASE( "transient fault retried once recovers and is accounted as a retry", "[agentbench][fake]" )
{
	Json::Value faults{Json::arrayValue};
	Json::Value fault{Json::objectValue};
	fault["kind"] = "transient_failure";
	fault["at_step"] = 0;
	faults.append( fault );

	Json::Value stepOverrides{Json::objectValue};
	Json::Value step0 = scriptDoc()["steps"][0];
	step0["on_failure"] = "retry_once";
	stepOverrides["0"] = step0;

	const ScriptRun result = run( caseOverrideWithFaults( faults ), Json::Value(), stepOverrides );
	REQUIRE( result.error.code.empty() );
	REQUIRE( result.trace.has_value() );
	REQUIRE( result.trace->steps.size() == 3 ); // fail, retry success, verify
	CHECK( result.trace->steps[0].success == false );
	CHECK( result.trace->steps[0].errorCode == "TRANSIENT_FAILURE" );
	CHECK( result.trace->steps[1].success );
	CHECK( result.trace->steps[1].tool == "rs:ndvi" );
	CHECK( result.trace->steps[2].success );
	CHECK( result.trace->stopReason == StopReason::Completed );
	// Retried pair must be recognizable as a retry by replay accounting.
	const AgentTrace &trace = *result.trace;
	CHECK( trace.steps[1].tool == trace.steps[0].tool );
	CHECK( deterministicSerialize( trace.steps[1].input ) == deterministicSerialize( trace.steps[0].input ) );
}

TEST_CASE( "unhandled failure aborts with an honest claim and gave_up", "[agentbench][fake]" )
{
	Json::Value faults{Json::arrayValue};
	Json::Value fault{Json::objectValue};
	fault["kind"] = "tool_unavailable";
	fault["at_step"] = 0;
	faults.append( fault );

	const ScriptRun result = run( caseOverrideWithFaults( faults ) );
	REQUIRE( result.error.code.empty() );
	REQUIRE( result.trace.has_value() );
	REQUIRE( result.trace->steps.size() == 1 ); // abort: later steps never run
	CHECK( result.trace->steps[0].success == false );
	CHECK( result.trace->steps[0].errorCode == "TOOL_NOT_FOUND" );
	CHECK( result.trace->evidence.empty() );
	CHECK( result.trace->stopReason == StopReason::GaveUp );
	CHECK( result.trace->outcomeClaimSuccess == false );
}

TEST_CASE( "retry_once against a persistent fault exhausts and aborts", "[agentbench][fake]" )
{
	Json::Value faults{Json::arrayValue};
	Json::Value fault{Json::objectValue};
	fault["kind"] = "tool_unavailable";
	fault["at_step"] = 0;
	faults.append( fault );

	Json::Value stepOverrides{Json::objectValue};
	Json::Value step0 = scriptDoc()["steps"][0];
	step0["on_failure"] = "retry_once";
	stepOverrides["0"] = step0;

	const ScriptRun result = run( caseOverrideWithFaults( faults ), Json::Value(), stepOverrides );
	REQUIRE( result.trace.has_value() );
	REQUIRE( result.trace->steps.size() == 2 ); // fail + one retry, both failed
	CHECK( result.trace->steps[0].success == false );
	CHECK( result.trace->steps[1].success == false );
	CHECK( result.trace->steps[1].errorCode == "TOOL_NOT_FOUND" );
	CHECK( result.trace->stopReason == StopReason::GaveUp );
	CHECK( result.trace->outcomeClaimSuccess == false );
}

TEST_CASE( "skip routes around a failed step and continues", "[agentbench][fake]" )
{
	Json::Value faults{Json::arrayValue};
	Json::Value fault{Json::objectValue};
	fault["kind"] = "transient_failure";
	fault["at_step"] = 0;
	faults.append( fault );

	Json::Value stepOverrides{Json::objectValue};
	Json::Value step0 = scriptDoc()["steps"][0];
	step0["on_failure"] = "skip";
	stepOverrides["0"] = step0;

	const ScriptRun result = run( caseOverrideWithFaults( faults ), Json::Value(), stepOverrides );
	REQUIRE( result.trace.has_value() );
	REQUIRE( result.trace->steps.size() == 2 ); // failed step recorded, then verify
	CHECK( result.trace->steps[0].success == false );
	CHECK( result.trace->steps[1].success );
	CHECK( result.trace->stopReason == StopReason::Completed );
}

TEST_CASE( "corrupted result is recorded as success with FAIL payload", "[agentbench][fake]" )
{
	Json::Value faults{Json::arrayValue};
	Json::Value fault{Json::objectValue};
	fault["kind"] = "corrupted_result";
	fault["at_step"] = 1;
	faults.append( fault );

	const ScriptRun result = run( caseOverrideWithFaults( faults ) );
	REQUIRE( result.trace.has_value() );
	REQUIRE( result.trace->steps.size() == 2 );
	CHECK( result.trace->steps[1].success ); // the tool "succeeded"…
	REQUIRE( result.trace->evidence.size() == 1 );
	CHECK( result.trace->evidence[0].verdict == "FAIL" ); // …but the verdict is FAIL
}

TEST_CASE( "script validation is typed: tools, evidence refs, vocabulary", "[agentbench][fake]" )
{
	// Disallowed tool → tool_not_allowed (cross-validated against the case).
	{
		const AgentCase agentCase = parseCase( deterministicSerialize( caseDoc() ) ).parsed.value();
		Json::Value scriptJson = scriptDoc();
		scriptJson["steps"][0]["tool"] = "rs:forbidden";
		const ScriptParse parsed = parseScript( deterministicSerialize( scriptJson ) );
		REQUIRE( parsed.parsed.has_value() );
		const ScriptRun result = runScript( agentCase, *parsed.parsed );
		CHECK( result.trace == std::nullopt );
		REQUIRE( result.error.code == "agentbench.tool_not_allowed" );
	}
	// Evidence id not declared by the case.
	{
		const AgentCase agentCase = parseCase( deterministicSerialize( caseDoc() ) ).parsed.value();
		Json::Value scriptJson = scriptDoc();
		scriptJson["steps"][1]["evidence"] = "ghost-product";
		const ScriptRun result = runScript( agentCase, *parseScript( deterministicSerialize( scriptJson ) ).parsed );
		CHECK( result.trace == std::nullopt );
		REQUIRE( result.error.code == "agentbench.script_invalid" );
	}
	// Unknown on_failure vocabulary (parse-time).
	{
		Json::Value scriptJson = scriptDoc();
		scriptJson["steps"][0]["on_failure"] = "pray";
		const ScriptParse parsed = parseScript( deterministicSerialize( scriptJson ) );
		CHECK( parsed.parsed == std::nullopt );
		REQUIRE( parsed.error.code == "agentbench.script_invalid" );
		CHECK( parsed.error.details["field"].asString() == "steps[0].on_failure" );
	}
	// Unknown schema tag.
	{
		Json::Value scriptJson = scriptDoc();
		scriptJson["schema"] = "sicnu.agentbench.script/v9";
		const ScriptParse parsed = parseScript( deterministicSerialize( scriptJson ) );
		CHECK( parsed.parsed == std::nullopt );
		REQUIRE( parsed.error.code == "agentbench.schema_version_unknown" );
	}
	// Empty steps refused.
	{
		Json::Value scriptJson = scriptDoc();
		scriptJson["steps"] = Json::Value( Json::arrayValue );
		const ScriptParse parsed = parseScript( deterministicSerialize( scriptJson ) );
		CHECK( parsed.parsed == std::nullopt );
		REQUIRE( parsed.error.code == "agentbench.script_invalid" );
	}
}

TEST_CASE( "impossible-task scripts may stop blocked with an honest refusal", "[agentbench][fake]" )
{
	Json::Value extra{Json::objectValue};
	extra["stop_reason_override"] = "blocked";
	Json::Value claim{Json::objectValue};
	claim["success"] = false;
	claim["note"] = "Scene lacks the NIR band required for NDVI; refusing.";
	extra["outcome_claim"] = claim;
	Json::Value stepOverrides{Json::objectValue};
	Json::Value step0{Json::objectValue};
	step0["tool"] = "harness:verify";
	step0["input"] = Json::Value( Json::objectValue );
	step0["payload"] = Json::Value( Json::objectValue );
	stepOverrides["0"] = step0;
	Json::Value steps1{Json::arrayValue};
	steps1.append( step0 );

	Json::Value scriptJson = scriptDoc( stepOverrides, extra );
	scriptJson["steps"] = steps1;
	const AgentCase agentCase = parseCase( deterministicSerialize( caseDoc() ) ).parsed.value();
	const AgentScript script = parseScript( deterministicSerialize( scriptJson ) ).parsed.value();
	const ScriptRun result = runScript( agentCase, script );
	REQUIRE( result.trace.has_value() );
	CHECK( result.trace->stopReason == StopReason::Blocked );
	CHECK( result.trace->outcomeClaimSuccess == false );
	CHECK( result.trace->outcomeClaimNote.find( "refusing" ) != std::string::npos );
}
