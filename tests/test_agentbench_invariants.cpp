// tests/test_agentbench_invariants.cpp
//
// Slice D RED: the hidden-invariant oracle. Every closed invariant kind gets
// a passing AND a failing probe — no vacuous oracles. Program-built cases
// with invalid params must degrade to typed "not passed" results, never
// crash (defensive: parseCase already rejects bad params).

#include <catch2/catch_test_macros.hpp>

#include "agentbench/case_schema.h"
#include "agentbench/invariants.h"
#include "agentbench/json_writer.h"
#include "agentbench/trace.h"

#include <json/json.h>

#include <string>

using namespace sicnu::agentbench;

namespace
{

Invariant makeInvariant( const std::string &id, InvariantKind kind, Json::Value params,
                         Severity severity = Severity::Error,
                         InvariantDimension dimension = InvariantDimension::Scientific )
{
	Invariant invariant;
	invariant.id = id;
	invariant.kind = kind;
	invariant.dimension = dimension;
	invariant.severity = severity;
	invariant.params = std::move( params );
	return invariant;
}

TraceStep makeStep( int index, const std::string &tool, bool success, const std::string &errorCode = "" )
{
	TraceStep step;
	step.index = index;
	step.tool = tool;
	step.success = success;
	step.errorCode = errorCode;
	step.tokens = 10;
	return step;
}

TraceEvidence makeEvidence( const std::string &id, const std::string &verdict = "" )
{
	TraceEvidence evidence;
	evidence.id = id;
	evidence.kind = "raster";
	if ( !verdict.empty() )
		evidence.verdict = verdict;
	evidence.fields["stats"]["mean"] = 0.42;
	evidence.fields["crs"] = "EPSG:32633";
	evidence.fields["notes"] = "delivered with cloud mask";
	return evidence;
}

AgentTrace baseTrace()
{
	AgentTrace trace;
	trace.traceId = "t";
	trace.caseId = "c";
	trace.agentName = "a";
	trace.agentVersion = "1";
	trace.steps.push_back( makeStep( 0, "rs:ndvi", true ) );
	trace.steps.push_back( makeStep( 1, "harness:verify", false, "OUTPUT_INVALID" ) );
	trace.steps.push_back( makeStep( 2, "harness:verify", true ) );
	trace.evidence.push_back( makeEvidence( "ndvi-raster", "PASS" ) );
	trace.stopReason = StopReason::Completed;
	trace.outcomeClaimSuccess = true;
	return trace;
}

const AgentCase &emptyCase()
{
	static const AgentCase parsed = [] {
		Json::Value doc{Json::objectValue};
		doc["schema"] = "sicnu.agentbench.case/v1";
		doc["case_id"] = "c";
		doc["title"] = "t";
		doc["task_family"] = "optical";
		doc["goal"] = "g";
		doc["initial_state"] = Json::Value( Json::objectValue );
		Json::Value tools{Json::arrayValue};
		tools.append( "rs:ndvi" );
		tools.append( "harness:verify" );
		doc["allowed_tools"] = tools;
		Json::Value invariants{Json::arrayValue};
		Json::Value inv{Json::objectValue};
		inv["id"] = "placeholder";
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
		budget["max_tokens"] = 400;
		budget["max_retries"] = 2;
		doc["resource_budget"] = budget;
		doc["minimal_steps"] = 2;
		return parseCase( deterministicSerialize( doc ) ).parsed.value();
	}();
	return parsed;
}

ReplayValidation cleanReplay()
{
	return validateReplay( emptyCase(), baseTrace() );
}

std::vector<InvariantResult> evalOne( const Invariant &invariant, const AgentTrace &trace = baseTrace(),
                                      const ReplayValidation &replay = ReplayValidation{} )
{
	AgentCase agentCase = emptyCase();
	agentCase.invariants = { invariant };
	return evaluateInvariants( { agentCase, trace, replay } );
}

const InvariantResult &soleResult( const std::vector<InvariantResult> &results )
{
	REQUIRE( results.size() == 1 );
	return results[0];
}

} // namespace

TEST_CASE( "tool_used passes on any use and fails when absent", "[agentbench][invariants]" )
{
	Json::Value params{Json::objectValue};
	params["tool"] = "harness:verify";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ToolUsed, params ) ) ).passed );
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ToolUsed, params ) ) ).evidence["uses"].asInt() == 2 );

	params["tool"] = "rs:unmix";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ToolUsed, params ) ) ).passed == false );
}

TEST_CASE( "tool_not_used fails when the forbidden tool appears", "[agentbench][invariants]" )
{
	Json::Value params{Json::objectValue};
	params["tool"] = "rs:dangerous_export";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ToolNotUsed, params ) ) ).passed );

	params["tool"] = "rs:ndvi";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ToolNotUsed, params ) ) ).passed == false );
}

TEST_CASE( "step_order requires the ordered tool subsequence", "[agentbench][invariants]" )
{
	Json::Value params{Json::objectValue};
	Json::Value steps{Json::arrayValue};
	steps.append( "rs:ndvi" );
	steps.append( "harness:verify" );
	params["steps"] = steps;
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::StepOrder, params ) ) ).passed );

	Json::Value reversed{Json::arrayValue};
	reversed.append( "harness:verify" );
	reversed.append( "rs:ndvi" );
	params["steps"] = reversed;
	// harness:verify appears first at step 1, but rs:ndvi never appears after it.
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::StepOrder, params ) ) ).passed == false );
}

TEST_CASE( "result_success and error_code_present inspect recorded steps", "[agentbench][invariants]" )
{
	Json::Value params{Json::objectValue};
	params["step_index"] = 2;
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ResultSuccess, params ) ) ).passed );

	params["step_index"] = 1;
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ResultSuccess, params ) ) ).passed == false );

	Json::Value errorParams{Json::objectValue};
	errorParams["step_index"] = 1;
	errorParams["error_code"] = "OUTPUT_INVALID";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ErrorCodePresent, errorParams ) ) ).passed );

	errorParams["error_code"] = "CANCELLED";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ErrorCodePresent, errorParams ) ) ).passed == false );
}

TEST_CASE( "evidence_exists and verdict_is gate on declared evidence", "[agentbench][invariants]" )
{
	Json::Value params{Json::objectValue};
	params["evidence_id"] = "ndvi-raster";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::EvidenceExists, params ) ) ).passed );

	params["evidence_id"] = "ghost";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::EvidenceExists, params ) ) ).passed == false );

	Json::Value verdictParams{Json::objectValue};
	verdictParams["evidence_id"] = "ndvi-raster";
	verdictParams["verdict"] = "PASS";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::VerdictIs, verdictParams ) ) ).passed );

	verdictParams["verdict"] = "FAIL";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::VerdictIs, verdictParams ) ) ).passed == false );
}

TEST_CASE( "field_equals resolves dotted paths and compares typed values", "[agentbench][invariants]" )
{
	Json::Value params{Json::objectValue};
	params["evidence_id"] = "ndvi-raster";
	params["field"] = "stats.mean";
	params["value"] = 0.42;
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::FieldEquals, params ) ) ).passed );

	params["value"] = 0.5;
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::FieldEquals, params ) ) ).passed == false );

	params["field"] = "crs";
	params["value"] = "EPSG:32633";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::FieldEquals, params ) ) ).passed );

	params["field"] = "missing.path";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::FieldEquals, params ) ) ).passed == false );
}

TEST_CASE( "field_contains works on substrings and array membership", "[agentbench][invariants]" )
{
	Json::Value params{Json::objectValue};
	params["evidence_id"] = "ndvi-raster";
	params["field"] = "notes";
	params["value"] = "cloud mask";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::FieldContains, params ) ) ).passed );

	params["value"] = "sar heroes";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::FieldContains, params ) ) ).passed == false );
}

TEST_CASE( "numeric bounds are inclusive at the boundary", "[agentbench][invariants]" )
{
	Json::Value le{Json::objectValue};
	le["evidence_id"] = "ndvi-raster";
	le["field"] = "stats.mean";
	le["max"] = 0.42;
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::NumericLe, le ) ) ).passed );
	le["max"] = 0.41;
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::NumericLe, le ) ) ).passed == false );

	Json::Value ge{Json::objectValue};
	ge["evidence_id"] = "ndvi-raster";
	ge["field"] = "stats.mean";
	ge["min"] = 0.42;
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::NumericGe, ge ) ) ).passed );
	ge["min"] = 0.43;
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::NumericGe, ge ) ) ).passed == false );
}

TEST_CASE( "explanation_mentions checks the agent's final explanation", "[agentbench][invariants]" )
{
	AgentTrace trace = baseTrace();
	trace.explanation = "Used NDVI with a cloud mask; delivered mean 0.42.";

	Json::Value params{Json::objectValue};
	params["phrase"] = "cloud mask";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ExplanationMentions, params ), trace ) ).passed );

	params["phrase"] = "sar heroes";
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ExplanationMentions, params ), trace ) ).passed == false );
}

TEST_CASE( "claim_consistent ties the claim to the stop reason", "[agentbench][invariants]" )
{
	Json::Value params{Json::objectValue};
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ClaimConsistent, params ) ) ).passed );

	AgentTrace lying = baseTrace();
	lying.stopReason = StopReason::GaveUp;
	lying.outcomeClaimSuccess = true;
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ClaimConsistent, params ), lying ) ).passed == false );

	AgentTrace honestRefusal = baseTrace();
	honestRefusal.stopReason = StopReason::Blocked;
	honestRefusal.outcomeClaimSuccess = false;
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ClaimConsistent, params ), honestRefusal ) ).passed );
}

TEST_CASE( "budget_within consumes replay violations", "[agentbench][invariants]" )
{
	Json::Value params{Json::objectValue};
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::BudgetWithin, params ), baseTrace(), cleanReplay() ) ).passed );

	ReplayValidation overBudget;
	ReplayViolation violation;
	violation.code = "agentbench.budget_exceeded";
	overBudget.violations.push_back( violation );
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::BudgetWithin, params ), baseTrace(), overBudget ) ).passed == false );
}

TEST_CASE( "every invariant result echoes id, severity and dimension", "[agentbench][invariants]" )
{
	Json::Value params{Json::objectValue};
	params["tool"] = "rs:ndvi";
	const InvariantResult result = soleResult(
		evalOne( makeInvariant( "proc", InvariantKind::ToolUsed, params, Severity::Warning, InvariantDimension::Process ) ) );
	CHECK( result.invariantId == "proc" );
	CHECK( result.severity == Severity::Warning );
	CHECK( result.dimension == InvariantDimension::Process );
}

TEST_CASE( "invalid runtime params degrade to typed not-passed results", "[agentbench][invariants]" )
{
	// Programmatically-built invariant with missing params (parseCase would
	// reject this; the evaluator must still not crash).
	const InvariantResult invalidParams = soleResult(
		evalOne( makeInvariant( "i", InvariantKind::ToolUsed, Json::Value( Json::objectValue ) ) ) );
	CHECK( invalidParams.passed == false );
	CHECK( invalidParams.evidence["reason"].asString() == "invalid_params" );

	// Out-of-range step index.
	Json::Value params{Json::objectValue};
	params["step_index"] = 99;
	CHECK( soleResult( evalOne( makeInvariant( "i", InvariantKind::ResultSuccess, params ) ) ).passed == false );
}

TEST_CASE( "all invariants are evaluated in case order", "[agentbench][invariants]" )
{
	AgentCase agentCase = emptyCase();
	agentCase.invariants.clear();
	Json::Value toolParams{Json::objectValue};
	toolParams["tool"] = "rs:ndvi";
	agentCase.invariants.push_back( makeInvariant( "b-second", InvariantKind::ToolUsed, toolParams ) );
	Json::Value verdictParams{Json::objectValue};
	verdictParams["evidence_id"] = "ndvi-raster";
	verdictParams["verdict"] = "PASS";
	agentCase.invariants.push_back( makeInvariant( "a-first", InvariantKind::VerdictIs, verdictParams, Severity::Warning ) );

	const std::vector<InvariantResult> results = evaluateInvariants( { agentCase, baseTrace(), ReplayValidation{} } );
	REQUIRE( results.size() == 2 );
	CHECK( results[0].invariantId == "b-second" );
	CHECK( results[1].invariantId == "a-first" );
}
