// tests/test_agentbench_trace.cpp
//
// Slice B RED: AgentTrace schema (sicnu.agentbench.trace/v1), replay
// validation against a case (allowed tools, scope, budget) and resource
// accounting.

#include <catch2/catch_test_macros.hpp>

#include "agentbench/case_schema.h"
#include "agentbench/json_writer.h"
#include "agentbench/trace.h"

#include <json/json.h>

#include <string>

using namespace sicnu::agentbench;

namespace
{

Json::Value traceDoc()
{
	Json::Value doc{Json::objectValue};
	doc["schema"] = "sicnu.agentbench.trace/v1";
	doc["trace_id"] = "tr-001";
	doc["case_id"] = "optical/ndvi-basic";
	Json::Value agent{Json::objectValue};
	agent["name"] = "scripted-reference";
	agent["kind"] = "fake";
	agent["version"] = "1.0.0";
	doc["agent"] = agent;
	doc["seed"] = 7;

	Json::Value steps{Json::arrayValue};
	{
		Json::Value step{Json::objectValue};
		step["index"] = 0;
		step["tool"] = "rs:ndvi";
		Json::Value input{Json::objectValue};
		input["scene"] = "work://case-42/scene.tif";
		step["input"] = input;
		step["success"] = true;
		Json::Value payload{Json::objectValue};
		payload["output_path"] = "work://case-42/ndvi.tif";
		step["payload"] = payload;
		step["tokens"] = 120;
		steps.append( step );
	}
	{
		Json::Value step{Json::objectValue};
		step["index"] = 1;
		step["tool"] = "harness:verify";
		step["input"] = Json::Value( Json::objectValue );
		step["success"] = false;
		step["error_code"] = "OUTPUT_INVALID";
		step["payload"] = Json::Value( Json::objectValue );
		step["tokens"] = 40;
		steps.append( step );
	}
	{
		Json::Value step{Json::objectValue};
		step["index"] = 2;
		step["tool"] = "harness:verify";
		step["input"] = Json::Value( Json::objectValue );
		step["success"] = true;
		step["payload"] = Json::Value( Json::objectValue );
		step["tokens"] = 40;
		steps.append( step );
	}
	doc["steps"] = steps;

	Json::Value evidence{Json::arrayValue};
	{
		Json::Value entry{Json::objectValue};
		entry["id"] = "ndvi-raster";
		entry["kind"] = "raster";
		entry["path"] = "work://case-42/ndvi.tif";
		Json::Value fields{Json::objectValue};
		fields["mean"] = 0.42;
		fields["crs"] = "EPSG:32633";
		entry["fields"] = fields;
		entry["verdict"] = "PASS";
		evidence.append( entry );
	}
	doc["final_evidence"] = evidence;

	doc["explanation"] = "Computed NDVI with rs:ndvi, verified the output, and report mean 0.42.";
	Json::Value claim{Json::objectValue};
	claim["success"] = true;
	claim["note"] = "Goal achieved within budget.";
	doc["outcome_claim"] = claim;
	doc["stop_reason"] = "completed";
	return doc;
}

Json::Value replayCaseDoc( const Json::Value &extraInitial = Json::Value() )
{
	Json::Value doc{Json::objectValue};
	doc["schema"] = "sicnu.agentbench.case/v1";
	doc["case_id"] = "optical/ndvi-basic";
	doc["title"] = "Basic NDVI delivery";
	doc["task_family"] = "optical";
	doc["goal"] = "Produce an NDVI raster.";
	Json::Value initial{Json::objectValue};
	initial["scene"] = "work://case-42/scene.tif";
	if ( !extraInitial.isNull() )
	{
		for ( const auto &name : extraInitial.getMemberNames() )
			initial[name] = extraInitial[name];
	}
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
	return doc;
}

const AgentCase &replayCase()
{
	static const AgentCase parsed = parseCase( deterministicSerialize( replayCaseDoc() ) ).parsed.value();
	return parsed;
}

TraceParse parseDoc( const Json::Value &doc )
{
	return parseTrace( deterministicSerialize( doc ) );
}

} // namespace

TEST_CASE( "minimal valid trace parses with typed fields and defaults", "[agentbench][trace]" )
{
	const TraceParse result = parseDoc( traceDoc() );
	REQUIRE( result.error.code.empty() );
	REQUIRE( result.parsed.has_value() );

	const AgentTrace &trace = *result.parsed;
	CHECK( trace.traceId == "tr-001" );
	CHECK( trace.caseId == "optical/ndvi-basic" );
	CHECK( trace.agentKind == AgentKind::Fake );
	CHECK( trace.agentName == "scripted-reference" );
	CHECK( trace.agentVersion == "1.0.0" );
	REQUIRE( trace.steps.size() == 3 );
	CHECK( trace.steps[0].tool == "rs:ndvi" );
	CHECK( trace.steps[0].success );
	CHECK( trace.steps[0].payload["output_path"].asString() == "work://case-42/ndvi.tif" );
	CHECK( trace.steps[0].tokens == 120 );
	CHECK( trace.steps[1].success == false );
	CHECK( trace.steps[1].errorCode == "OUTPUT_INVALID" );
	CHECK( trace.evidence.size() == 1 );
	CHECK( trace.evidence[0].verdict == "PASS" );
	CHECK( trace.evidence[0].fields["mean"].asDouble() == 0.42 );
	CHECK( trace.outcomeClaimSuccess );
	CHECK( trace.stopReason == StopReason::Completed );

	// Round-trip digest stability.
	const std::string digest = trace.digest();
	Json::Value roundTrip = traceToJson( trace );
	const TraceParse again = parseDoc( roundTrip );
	REQUIRE( again.parsed.has_value() );
	CHECK( again.parsed->digest() == digest );
}

TEST_CASE( "trace schema tag is version gated", "[agentbench][trace]" )
{
	Json::Value doc = traceDoc();
	doc["schema"] = "sicnu.agentbench.trace/v2";
	TraceParse result = parseDoc( doc );
	CHECK( result.parsed == std::nullopt );
	CHECK( result.error.code == "agentbench.schema_version_unknown" );
	CHECK( result.error.details["found"].asString() == "sicnu.agentbench.trace/v2" );
}

TEST_CASE( "structurally broken traces are trace_malformed", "[agentbench][trace]" )
{
	TraceParse result = parseTrace( "nope" );
	CHECK( result.error.code == "agentbench.trace_malformed" );
	result = parseTrace( "\"a string\"" );
	CHECK( result.error.code == "agentbench.trace_malformed" );
}

TEST_CASE( "trace field violations are trace_invalid with field details", "[agentbench][trace]" )
{
	struct Violation
	{
		std::string label;
		std::string field;
		void ( *mutate )( Json::Value & );
	};
	const std::vector<Violation> violations{
		{ "missing trace_id", "trace_id", []( Json::Value &d ) { d.removeMember( "trace_id" ); } },
		{ "unknown agent kind", "agent.kind", []( Json::Value &d ) { d["agent"]["kind"] = "psychic"; } },
		{ "unknown stop reason", "stop_reason", []( Json::Value &d ) { d["stop_reason"] = "ascended"; } },
		{ "step index mismatch", "steps[2].index", []( Json::Value &d ) { d["steps"][2]["index"] = 5; } },
		{ "failed step without error code", "steps[1].error_code", []( Json::Value &d ) { d["steps"][1].removeMember( "error_code" ); } },
		{ "successful step with error code", "steps[0].error_code", []( Json::Value &d ) { d["steps"][0]["error_code"] = "MYSTERY"; } },
		{ "negative tokens", "steps[0].tokens", []( Json::Value &d ) { d["steps"][0]["tokens"] = -3; } },
		{ "duplicate evidence id", "final_evidence[1].id", []( Json::Value &d ) {
			 Json::Value clone = d["final_evidence"][0];
			 d["final_evidence"].append( clone );
		 } },
		{ "unknown evidence kind", "final_evidence[0].kind", []( Json::Value &d ) { d["final_evidence"][0]["kind"] = "hologram"; } },
		{ "bad evidence verdict", "final_evidence[0].verdict", []( Json::Value &d ) { d["final_evidence"][0]["verdict"] = "PROBABLY"; } },
		{ "missing outcome claim", "outcome_claim", []( Json::Value &d ) { d.removeMember( "outcome_claim" ); } },
	};

	for ( const auto &violation : violations )
	{
		Json::Value doc = traceDoc();
		violation.mutate( doc );
		CAPTURE( violation.label );
		const TraceParse result = parseDoc( doc );
		CHECK( result.parsed == std::nullopt );
		REQUIRE( result.error.code == "agentbench.trace_invalid" );
		CHECK( result.error.details["field"].asString() == violation.field );
	}
}

TEST_CASE( "empty step list is a valid not-started trace", "[agentbench][trace]" )
{
	Json::Value doc = traceDoc();
	doc["steps"] = Json::Value( Json::arrayValue );
	doc["final_evidence"] = Json::Value( Json::arrayValue );
	doc["stop_reason"] = "gave_up";
	doc["outcome_claim"]["success"] = false;
	const TraceParse result = parseDoc( doc );
	REQUIRE( result.parsed.has_value() );
	CHECK( result.parsed->steps.empty() );
}

TEST_CASE( "replay accounting counts calls, tokens and post-failure retries", "[agentbench][trace]" )
{
	const ReplayValidation validation = validateReplay( replayCase(), parseDoc( traceDoc() ).parsed.value() );
	REQUIRE( validation.violations.empty() );
	CHECK( validation.usage.toolCalls == 3 );
	CHECK( validation.usage.tokens == 200 );
	// Step 2 repeats (tool,input) of failed step 1 → exactly one retry.
	CHECK( validation.usage.retries == 1 );
}

TEST_CASE( "replay flags disallowed tools, id mismatches and over-budget runs", "[agentbench][trace]" )
{
	auto violationsOf = []( Json::Value traceOverride, Json::Value caseExtraInitial = Json::Value() ) {
		const AgentCase agentCase = parseCase( deterministicSerialize( replayCaseDoc( caseExtraInitial ) ) ).parsed.value();
		Json::Value doc = traceDoc();
		for ( const auto &name : traceOverride.getMemberNames() )
			doc[name] = traceOverride[name];
		return validateReplay( agentCase, parseDoc( doc ).parsed.value() );
	};

	// Disallowed tool.
	Json::Value rogue{Json::objectValue};
	rogue["steps"] = Json::Value( Json::arrayValue );
	Json::Value step{Json::objectValue};
	step["index"] = 0;
	step["tool"] = "rs:forbidden";
	step["input"] = Json::Value( Json::objectValue );
	step["success"] = true;
	step["payload"] = Json::Value( Json::objectValue );
	step["tokens"] = 1;
	rogue["steps"].append( step );
	ReplayValidation validation = violationsOf( rogue );
	REQUIRE( validation.violations.size() == 1 );
	CHECK( validation.violations[0].code == "agentbench.tool_not_allowed" );
	CHECK( validation.violations[0].details["tool"].asString() == "rs:forbidden" );

	// Case/trace id mismatch.
	Json::Value mismatch{Json::objectValue};
	mismatch["case_id"] = "other/case";
	validation = violationsOf( mismatch );
	REQUIRE( validation.violations.size() == 1 );
	CHECK( validation.violations[0].code == "agentbench.trace_invalid" );

	// Tool-call budget exceeded (budget is 8; run 9 calls).
	Json::Value overCalls{Json::objectValue};
	overCalls["steps"] = Json::Value( Json::arrayValue );
	for ( int i = 0; i < 9; ++i )
	{
		Json::Value budgetStep{Json::objectValue};
		budgetStep["index"] = i;
		budgetStep["tool"] = "rs:ndvi";
		budgetStep["input"] = Json::Value( Json::objectValue );
		budgetStep["success"] = true;
		budgetStep["payload"] = Json::Value( Json::objectValue );
		budgetStep["tokens"] = 1;
		overCalls["steps"].append( budgetStep );
	}
	validation = violationsOf( overCalls );
	bool sawBudget = false;
	for ( const auto &violation : validation.violations )
		if ( violation.code == "agentbench.budget_exceeded" && violation.details["resource"].asString() == "tool_calls" )
			sawBudget = true;
	CHECK( sawBudget );
	CHECK( validation.usage.toolCalls == 9 );

	// Token budget exceeded (budget is 4000).
	Json::Value overTokens{Json::objectValue};
	overTokens["steps"] = Json::Value( Json::arrayValue );
	Json::Value fatStep{Json::objectValue};
	fatStep["index"] = 0;
	fatStep["tool"] = "rs:ndvi";
	fatStep["input"] = Json::Value( Json::objectValue );
	fatStep["success"] = true;
	fatStep["payload"] = Json::Value( Json::objectValue );
	fatStep["tokens"] = 5000;
	overTokens["steps"].append( fatStep );
	validation = violationsOf( overTokens );
	REQUIRE( validation.violations.size() == 1 );
	CHECK( validation.violations[0].code == "agentbench.budget_exceeded" );
	CHECK( validation.violations[0].details["resource"].asString() == "tokens" );

	// Retry budget: 3 consecutive identical retries after failure, budget allows 1.
	Json::Value overRetries{Json::objectValue};
	overRetries["steps"] = Json::Value( Json::arrayValue );
	for ( int i = 0; i < 4; ++i )
	{
		Json::Value retryStep{Json::objectValue};
		retryStep["index"] = i;
		retryStep["tool"] = "rs:ndvi";
		Json::Value input{Json::objectValue};
		input["same"] = true;
		retryStep["input"] = input;
		retryStep["success"] = ( i == 3 );
		if ( !retryStep["success"].asBool() )
			retryStep["error_code"] = "TRANSIENT_FAILURE";
		retryStep["payload"] = Json::Value( Json::objectValue );
		retryStep["tokens"] = 1;
		overRetries["steps"].append( retryStep );
	}
	validation = violationsOf( overRetries );
	REQUIRE( validation.usage.retries == 3 );
	bool sawRetryBudget = false;
	for ( const auto &violation : validation.violations )
		if ( violation.code == "agentbench.budget_exceeded" && violation.details["resource"].asString() == "retries" )
			sawRetryBudget = true;
	CHECK( sawRetryBudget );
}

TEST_CASE( "replay enforces declared workspace scope on paths", "[agentbench][trace]" )
{
	Json::Value scopeRoots{Json::arrayValue};
	scopeRoots.append( "work://case-42" );
	Json::Value extraInitial{Json::objectValue};
	extraInitial["workspace_roots"] = scopeRoots;

	auto run = [&]( Json::Value traceOverride ) {
		const AgentCase agentCase = parseCase( deterministicSerialize( replayCaseDoc( extraInitial ) ) ).parsed.value();
		Json::Value doc = traceDoc();
		for ( const auto &name : traceOverride.getMemberNames() )
			doc[name] = traceOverride[name];
		return validateReplay( agentCase, parseDoc( doc ).parsed.value() );
	};

	// The baseline trace stays inside work://case-42 → clean.
	ReplayValidation validation = run( Json::Value() );
	REQUIRE( validation.violations.empty() );

	// Evidence path escaping the scope.
	Json::Value escape{Json::objectValue};
	escape["final_evidence"] = Json::Value( Json::arrayValue );
	Json::Value entry{Json::objectValue};
	entry["id"] = "ndvi-raster";
	entry["kind"] = "raster";
	entry["path"] = "work://other-case/ndvi.tif";
	Json::Value fields{Json::objectValue};
	entry["fields"] = fields;
	escape["final_evidence"].append( entry );
	validation = run( escape );
	REQUIRE( validation.violations.size() == 1 );
	CHECK( validation.violations[0].code == "agentbench.path_outside_scope" );
	CHECK( validation.violations[0].details["path"].asString() == "work://other-case/ndvi.tif" );

	// Step payload path escaping the scope (any *_path string value).
	Json::Value payloadEscape{Json::objectValue};
	payloadEscape["steps"] = Json::Value( Json::arrayValue );
	Json::Value badStep{Json::objectValue};
	badStep["index"] = 0;
	badStep["tool"] = "rs:ndvi";
	badStep["input"] = Json::Value( Json::objectValue );
	badStep["success"] = true;
	Json::Value payload{Json::objectValue};
	payload["output_path"] = "file:///etc/passwd";
	badStep["payload"] = payload;
	badStep["tokens"] = 1;
	payloadEscape["steps"].append( badStep );
	payloadEscape["final_evidence"] = Json::Value( Json::arrayValue );
	payloadEscape["stop_reason"] = "blocked";
	payloadEscape["outcome_claim"]["success"] = false;
	validation = run( payloadEscape );
	REQUIRE( validation.violations.size() == 1 );
	CHECK( validation.violations[0].code == "agentbench.path_outside_scope" );
	CHECK( validation.violations[0].details["path"].asString() == "file:///etc/passwd" );
}

TEST_CASE( "case documents validate the workspace_roots declaration", "[agentbench][case]" )
{
	Json::Value caseDoc = replayCaseDoc();
	Json::Value roots{Json::arrayValue};
	roots.append( "work://case-42" );
	caseDoc["initial_state"]["workspace_roots"] = roots;
	CaseParse result = parseCase( deterministicSerialize( caseDoc ) );
	REQUIRE( result.parsed.has_value() );
	CHECK( caseScopeRoots( *result.parsed ).size() == 1 );

	caseDoc["initial_state"]["workspace_roots"] = "work://case-42";
	result = parseCase( deterministicSerialize( caseDoc ) );
	CHECK( result.error.code == "agentbench.case_invalid" );
	CHECK( result.error.details["field"].asString() == "initial_state.workspace_roots" );
}

TEST_CASE( "double replay validation is deterministic", "[agentbench][trace]" )
{
	const AgentTrace trace = parseDoc( traceDoc() ).parsed.value();
	const ReplayValidation first = validateReplay( replayCase(), trace );
	const ReplayValidation second = validateReplay( replayCase(), trace );
	REQUIRE( first.violations.size() == second.violations.size() );
	CHECK( first.usage.toolCalls == second.usage.toolCalls );
	CHECK( first.usage.tokens == second.usage.tokens );
	CHECK( first.usage.retries == second.usage.retries );
}
