// tests/test_agentbench_case.cpp
//
// Slice A RED: AgentCase schema — parse, validate, version, digest.
// Wire contract: sicnu.agentbench.case/v1; typed agentbench.* error codes,
// never prose.

#include <catch2/catch_test_macros.hpp>

#include "agentbench/case_schema.h"

#include <json/json.h>

#include <string>

using namespace sicnu::agentbench;

namespace
{

Json::Value minimalValidCaseDoc()
{
	Json::Value doc{Json::objectValue};
	doc["schema"] = "sicnu.agentbench.case/v1";
	doc["case_id"] = "optical/ndvi-basic";
	doc["title"] = "Basic NDVI delivery";
	doc["description"] = "Compute NDVI over a small optical scene and deliver evidence.";
	doc["task_family"] = "optical";
	doc["goal"] = "Produce an NDVI raster for the pinned scene and report the mean value.";
	Json::Value initial{Json::objectValue};
	initial["scene"] = "fixture://optical/small-scene";
	doc["initial_state"] = initial;

	Json::Value tools{Json::arrayValue};
	tools.append( "rs:ndvi" );
	tools.append( "harness:verify" );
	doc["allowed_tools"] = tools;

	Json::Value invariants{Json::arrayValue};
	{
		Json::Value inv{Json::objectValue};
		inv["id"] = "sci-ndvi-verdict";
		inv["kind"] = "verdict_is";
		inv["dimension"] = "scientific";
		inv["severity"] = "error";
		Json::Value params{Json::objectValue};
		params["evidence_id"] = "ndvi-raster";
		params["verdict"] = "PASS";
		inv["params"] = params;
		invariants.append( inv );
	}
	{
		Json::Value inv{Json::objectValue};
		inv["id"] = "proc-no-leak";
		inv["kind"] = "tool_not_used";
		inv["dimension"] = "process";
		inv["severity"] = "warning";
		Json::Value params{Json::objectValue};
		params["tool"] = "rs:dangerous_export";
		inv["params"] = params;
		invariants.append( inv );
	}
	doc["invariants"] = invariants;

	Json::Value evidence{Json::arrayValue};
	{
		Json::Value ev{Json::objectValue};
		ev["id"] = "ndvi-raster";
		ev["kind"] = "raster";
		Json::Value fields{Json::arrayValue};
		fields.append( "mean" );
		fields.append( "crs" );
		ev["required_fields"] = fields;
		evidence.append( ev );
	}
	doc["expected_evidence"] = evidence;

	Json::Value budget{Json::objectValue};
	budget["max_tool_calls"] = 8;
	budget["max_tokens"] = 4000;
	budget["max_retries"] = 1;
	doc["resource_budget"] = budget;

	doc["minimal_steps"] = 2;
	return doc;
}

CaseParse parseDoc( const Json::Value &doc )
{
	return parseCase( deterministicSerialize( doc ) );
}

} // namespace

TEST_CASE( "minimal valid case parses with typed enums and round-trips", "[agentbench][case]" )
{
	const CaseParse result = parseDoc( minimalValidCaseDoc() );
	REQUIRE( result.error.code.empty() );
	REQUIRE( result.parsed.has_value() );

	const AgentCase &parsed = *result.parsed;
	CHECK( parsed.caseId == "optical/ndvi-basic" );
	CHECK( parsed.title == "Basic NDVI delivery" );
	CHECK( parsed.family == TaskFamily::Optical );
	CHECK( parsed.allowedTools.size() == 2 );
	CHECK( parsed.allowedTools[0] == "rs:ndvi" );
	REQUIRE( parsed.invariants.size() == 2 );
	CHECK( parsed.invariants[0].kind == InvariantKind::VerdictIs );
	CHECK( parsed.invariants[0].dimension == InvariantDimension::Scientific );
	CHECK( parsed.invariants[0].severity == Severity::Error );
	CHECK( parsed.invariants[1].kind == InvariantKind::ToolNotUsed );
	CHECK( parsed.invariants[1].dimension == InvariantDimension::Process );
	CHECK( parsed.invariants[1].severity == Severity::Warning );
	REQUIRE( parsed.evidence.size() == 1 );
	CHECK( parsed.evidence[0].kind == "raster" );
	CHECK( parsed.evidence[0].requiredFields.size() == 2 );
	CHECK( parsed.budget.maxToolCalls == 8 );
	CHECK( parsed.budget.maxTokens == 4000 );
	CHECK( parsed.budget.maxRetries == 1 );
	CHECK( parsed.minimalSteps == 2 );
	CHECK( parsed.faults.empty() );
	CHECK( parsed.redundantTools.empty() );

	// toJson → parse → equal digest (serialization round-trip stability).
	const std::string firstDigest = parsed.digest();
	Json::Value roundTrip = caseToJson( parsed );
	const CaseParse again = parseDoc( roundTrip );
	REQUIRE( again.parsed.has_value() );
	CHECK( again.parsed->digest() == firstDigest );
}

TEST_CASE( "case digest is stable under key reordering but sensitive to content", "[agentbench][case]" )
{
	const Json::Value doc = minimalValidCaseDoc();
	const std::string digestA = parseDoc( doc ).parsed->digest();

	// Same content, different insertion order.
	Json::Value reordered{Json::objectValue};
	reordered["minimal_steps"] = doc["minimal_steps"];
	reordered["resource_budget"] = doc["resource_budget"];
	reordered["expected_evidence"] = doc["expected_evidence"];
	reordered["invariants"] = doc["invariants"];
	reordered["allowed_tools"] = doc["allowed_tools"];
	reordered["initial_state"] = doc["initial_state"];
	reordered["goal"] = doc["goal"];
	reordered["task_family"] = doc["task_family"];
	reordered["description"] = doc["description"];
	reordered["title"] = doc["title"];
	reordered["case_id"] = doc["case_id"];
	reordered["schema"] = doc["schema"];
	const std::string digestB = parseDoc( reordered ).parsed->digest();
	CHECK( digestA == digestB );

	// Any content change must change the digest (suite pinning relies on it).
	Json::Value changed = doc;
	changed["title"] = "Different title";
	const std::string digestC = parseDoc( changed ).parsed->digest();
	CHECK( digestA != digestC );
}

TEST_CASE( "unknown or missing schema tag is a typed version error", "[agentbench][case]" )
{
	Json::Value doc = minimalValidCaseDoc();
	doc["schema"] = "sicnu.agentbench.case/v999";
	CaseParse result = parseDoc( doc );
	CHECK( result.parsed == std::nullopt );
	CHECK( result.error.code == "agentbench.schema_version_unknown" );
	CHECK( result.error.details["found"].asString() == "sicnu.agentbench.case/v999" );

	doc["schema"] = "some.other.schema/v1";
	result = parseDoc( doc );
	CHECK( result.error.code == "agentbench.schema_version_unknown" );

	Json::Value noTag = minimalValidCaseDoc();
	noTag.removeMember( "schema" );
	result = parseDoc( noTag );
	CHECK( result.error.code == "agentbench.schema_version_unknown" );
}

TEST_CASE( "structurally broken documents are case_malformed", "[agentbench][case]" )
{
	CaseParse result = parseCase( "{not json" );
	CHECK( result.parsed == std::nullopt );
	CHECK( result.error.code == "agentbench.case_malformed" );

	result = parseCase( "[1,2,3]" );
	CHECK( result.parsed == std::nullopt );
	CHECK( result.error.code == "agentbench.case_malformed" );
}

TEST_CASE( "required-field violations are case_invalid with field details", "[agentbench][case]" )
{
	struct Violation
	{
		std::string label;
		std::string field;
		void ( *mutate )( Json::Value & );
	};
	const std::vector<Violation> violations{
		{ "empty case_id", "case_id", []( Json::Value &d ) { d["case_id"] = ""; } },
		{ "missing goal", "goal", []( Json::Value &d ) { d.removeMember( "goal" ); } },
		{ "unknown task family", "task_family", []( Json::Value &d ) { d["task_family"] = "astro"; } },
		{ "empty allowed tools", "allowed_tools", []( Json::Value &d ) { d["allowed_tools"] = Json::Value( Json::arrayValue ); } },
		{ "duplicate allowed tool", "allowed_tools", []( Json::Value &d ) { d["allowed_tools"][1] = "rs:ndvi"; } },
		{ "zero minimal steps", "minimal_steps", []( Json::Value &d ) { d["minimal_steps"] = 0; } },
		{ "missing budget", "resource_budget", []( Json::Value &d ) { d.removeMember( "resource_budget" ); } },
		{ "non-positive max tokens", "resource_budget.max_tokens", []( Json::Value &d ) { d["resource_budget"]["max_tokens"] = 0; } },
		{ "initial state not an object", "initial_state", []( Json::Value &d ) { d["initial_state"] = "scene"; } },
	};

	for ( const auto &violation : violations )
	{
		Json::Value doc = minimalValidCaseDoc();
		violation.mutate( doc );
		CAPTURE( violation.label );
		const CaseParse result = parseDoc( doc );
		CHECK( result.parsed == std::nullopt );
		REQUIRE( result.error.code == "agentbench.case_invalid" );
		CHECK( result.error.details["field"].asString() == violation.field );
	}
}

TEST_CASE( "invariant entries are validated: closed kind, dimension, severity, params, cross-refs", "[agentbench][case]" )
{
	auto withInvariant = []( Json::Value inv ) {
		Json::Value doc = minimalValidCaseDoc();
		doc["invariants"] = Json::Value( Json::arrayValue );
		doc["invariants"].append( inv );
		return doc;
	};

	// Unknown kind.
	Json::Value inv{Json::objectValue};
	inv["id"] = "x";
	inv["kind"] = "mind_read";
	inv["dimension"] = "process";
	inv["severity"] = "error";
	inv["params"] = Json::Value( Json::objectValue );
	CaseParse result = parseDoc( withInvariant( inv ) );
	REQUIRE( result.error.code == "agentbench.case_invalid" );
	CHECK( result.error.details["field"].asString() == "invariants[0].kind" );

	// Unknown dimension.
	inv["kind"] = "tool_used";
	inv["dimension"] = "cosmic";
	result = parseDoc( withInvariant( inv ) );
	CHECK( result.error.details["field"].asString() == "invariants[0].dimension" );

	// Unknown severity.
	inv["dimension"] = "scientific";
	inv["severity"] = "catastrophic";
	result = parseDoc( withInvariant( inv ) );
	CHECK( result.error.details["field"].asString() == "invariants[0].severity" );

	// Missing kind params (tool_used requires params.tool).
	inv["severity"] = "error";
	result = parseDoc( withInvariant( inv ) );
	CHECK( result.error.details["field"].asString() == "invariants[0].params.tool" );

	// Duplicate invariant ids.
	Json::Value doc = minimalValidCaseDoc();
	doc["invariants"][1]["id"] = doc["invariants"][0]["id"];
	result = parseDoc( doc );
	CHECK( result.error.details["field"].asString() == "invariants[1].id" );

	// evidence_exists references unknown evidence id.
	Json::Value dangling{Json::objectValue};
	dangling["id"] = "dangling";
	dangling["kind"] = "evidence_exists";
	dangling["dimension"] = "process";
	dangling["severity"] = "error";
	Json::Value params{Json::objectValue};
	params["evidence_id"] = "no-such-evidence";
	dangling["params"] = params;
	result = parseDoc( withInvariant( dangling ) );
	CHECK( result.error.details["field"].asString() == "invariants[0].params.evidence_id" );
}

TEST_CASE( "expected evidence entries are validated", "[agentbench][case]" )
{
	Json::Value doc = minimalValidCaseDoc();

	// Unknown evidence kind.
	doc["expected_evidence"][0]["kind"] = "hologram";
	CaseParse result = parseDoc( doc );
	CHECK( result.error.code == "agentbench.case_invalid" );
	CHECK( result.error.details["field"].asString() == "expected_evidence[0].kind" );

	// Duplicate evidence ids.
	doc = minimalValidCaseDoc();
	Json::Value clone = doc["expected_evidence"][0];
	doc["expected_evidence"].append( clone );
	result = parseDoc( doc );
	CHECK( result.error.details["field"].asString() == "expected_evidence[1].id" );
}

TEST_CASE( "fault entries are validated against the closed kind vocabulary", "[agentbench][case]" )
{
	Json::Value doc = minimalValidCaseDoc();
	Json::Value fault{Json::objectValue};
	fault["kind"] = "solar_flare";
	fault["at_step"] = 1;
	doc["faults"] = Json::Value( Json::arrayValue );
	doc["faults"].append( fault );
	CaseParse result = parseDoc( doc );
	REQUIRE( result.error.code == "agentbench.case_invalid" );
	CHECK( result.error.details["field"].asString() == "faults[0].kind" );

	doc = minimalValidCaseDoc();
	fault["kind"] = "transient_failure";
	fault["at_step"] = -1;
	doc["faults"][0] = fault;
	result = parseDoc( doc );
	CHECK( result.error.details["field"].asString() == "faults[0].at_step" );

	// Valid fault parses into the typed model.
	doc = minimalValidCaseDoc();
	fault["kind"] = "transient_failure";
	fault["at_step"] = 1;
	doc["faults"] = Json::Value( Json::arrayValue );
	doc["faults"].append( fault );
	result = parseDoc( doc );
	REQUIRE( result.parsed.has_value() );
	REQUIRE( result.parsed->faults.size() == 1 );
	CHECK( result.parsed->faults[0].kind == FaultKind::TransientFailure );
	CHECK( result.parsed->faults[0].atStep == 1 );
}

TEST_CASE( "failure expectation must name a class from the closed taxonomy", "[agentbench][case]" )
{
	Json::Value doc = minimalValidCaseDoc();
	doc["failure_expectation"]["failure_class"] = "exploded";
	CaseParse result = parseDoc( doc );
	REQUIRE( result.error.code == "agentbench.case_invalid" );
	CHECK( result.error.details["field"].asString() == "failure_expectation.failure_class" );

	doc["failure_expectation"]["failure_class"] = "impossible_task";
	result = parseDoc( doc );
	REQUIRE( result.parsed.has_value() );
	CHECK( result.parsed->failureExpectation["failure_class"].asString() == "impossible_task" );
}

TEST_CASE( "integer range violations are typed rejections (review pass 1)", "[agentbench][case]" )
{
	Json::Value doc = minimalValidCaseDoc();
	doc["resource_budget"]["max_tokens"] = Json::Value( Json::Int64( 9999999999ll ) );
	CaseParse result = parseDoc( doc );
	CHECK( result.parsed == std::nullopt );
	REQUIRE( result.error.code == "agentbench.case_invalid" );
	CHECK( result.error.details["field"].asString() == "resource_budget.max_tokens" );

	doc = minimalValidCaseDoc();
	Json::Value params = doc["invariants"][0]["params"];
	doc = minimalValidCaseDoc();
	doc["minimal_steps"] = Json::Value( Json::Int64( 4294967296ll ) );
	result = parseDoc( doc );
	REQUIRE( result.error.code == "agentbench.case_invalid" );
	CHECK( result.error.details["field"].asString() == "minimal_steps" );
}
