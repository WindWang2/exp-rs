// tests/test_agentbench_suite.cpp
//
// Slice H RED part 1: suite doc (sicnu.agentbench.suite/v1) + suite runner.
// The runner is pure: documents come from an injected loader, so the same
// code runs in-memory (tests) and over the checked-in pack (corpus test).
// Pack digest pins content: any case/trace/script change moves it.

#include <catch2/catch_test_macros.hpp>

#include "agentbench/json_writer.h"
#include "agentbench/suite.h"

#include <json/json.h>

#include <map>
#include <string>

using namespace sicnu::agentbench;

namespace
{

Json::Value caseDoc( const std::string &caseId, const std::string &family )
{
	Json::Value doc{Json::objectValue};
	doc["schema"] = "sicnu.agentbench.case/v1";
	doc["case_id"] = caseId;
	doc["title"] = "Suite case " + caseId;
	doc["task_family"] = family;
	doc["goal"] = "Deliver the product.";
	doc["initial_state"] = Json::Value( Json::objectValue );
	Json::Value tools{Json::arrayValue};
	tools.append( "rs:core" );
	doc["allowed_tools"] = tools;
	Json::Value invariants{Json::arrayValue};
	Json::Value inv{Json::objectValue};
	inv["id"] = "i";
	inv["kind"] = "claim_consistent";
	inv["dimension"] = "scientific";
	inv["severity"] = "error";
	inv["params"] = Json::Value( Json::objectValue );
	invariants.append( inv );
	doc["invariants"] = invariants;
	Json::Value ev{Json::objectValue};
	ev["id"] = "product";
	ev["kind"] = "raster";
	ev["required_fields"] = Json::Value( Json::arrayValue );
	Json::Value evs{Json::arrayValue};
	evs.append( ev );
	doc["expected_evidence"] = evs;
	Json::Value budget{Json::objectValue};
	budget["max_tool_calls"] = 8;
	budget["max_tokens"] = 4000;
	doc["resource_budget"] = budget;
	doc["minimal_steps"] = 1;
	return doc;
}

Json::Value scriptDoc( const std::string &caseId, bool succeed )
{
	Json::Value doc{Json::objectValue};
	doc["schema"] = "sicnu.agentbench.script/v1";
	doc["script_id"] = "ref/" + caseId;
	doc["case_id"] = caseId;
	Json::Value agent{Json::objectValue};
	agent["name"] = "scripted-reference";
	agent["version"] = "1.0.0";
	doc["agent"] = agent;
	Json::Value steps{Json::arrayValue};
	Json::Value step{Json::objectValue};
	step["tool"] = "rs:core";
	step["input"] = Json::Value( Json::objectValue );
	Json::Value payload{Json::objectValue};
	if ( succeed )
	{
		payload["verdict"] = "PASS";
		step["evidence"] = "product";
	}
	else
	{
		payload["verdict"] = "FAIL";
	}
	step["payload"] = payload;
	steps.append( step );
	doc["steps"] = steps;
	return doc;
}

Json::Value suiteDoc()
{
	Json::Value doc{Json::objectValue};
	doc["schema"] = "sicnu.agentbench.suite/v1";
	doc["suite_id"] = "rs14-agent-bench-core";
	doc["version"] = "1.0.0";
	doc["description"] = "In-memory two-case suite.";
	Json::Value cases{Json::arrayValue};
	Json::Value entry0{Json::objectValue};
	entry0["case"] = "cases/a.json";
	entry0["script"] = "scripts/a.json";
	cases.append( entry0 );
	Json::Value entry1{Json::objectValue};
	entry1["case"] = "cases/b.json";
	entry1["script"] = "scripts/b.json";
	cases.append( entry1 );
	doc["cases"] = cases;
	return doc;
}

Loader makeLoader( const Json::Value &caseA, const Json::Value &caseB )
{
	Loader loader = [caseA, caseB]( const std::string &path ) -> std::optional<std::string> {
		if ( path == "suite.json" )
			return deterministicSerialize( suiteDoc() );
		if ( path == "cases/a.json" )
			return deterministicSerialize( caseA );
		if ( path == "cases/b.json" )
			return deterministicSerialize( caseB );
		if ( path == "scripts/a.json" )
			return deterministicSerialize( scriptDoc( "suite/a", true ) );
		if ( path == "scripts/b.json" )
			return deterministicSerialize( scriptDoc( "suite/b", false ) );
		return std::nullopt;
	};
	return loader;
}

} // namespace

TEST_CASE( "suite doc parsing is versioned and entry-validated", "[agentbench][suite]" )
{
	const SuiteParse ok = parseSuite( deterministicSerialize( suiteDoc() ) );
	REQUIRE( ok.error.code.empty() );
	REQUIRE( ok.parsed.has_value() );
	CHECK( ok.parsed->suiteId == "rs14-agent-bench-core" );
	CHECK( ok.parsed->version == "1.0.0" );
	REQUIRE( ok.parsed->entries.size() == 2 );
	CHECK( ok.parsed->entries[0].casePath == "cases/a.json" );
	CHECK( ok.parsed->entries[0].scriptPath == "scripts/a.json" );
	CHECK( ok.parsed->entries[0].tracePath.empty() );

	// Entry must name exactly one of script/trace.
	Json::Value bad = suiteDoc();
	bad["cases"][0]["trace"] = "traces/a.json";
	const SuiteParse both = parseSuite( deterministicSerialize( bad ) );
	CHECK( both.parsed == std::nullopt );
	CHECK( both.error.code == "agentbench.suite_invalid" );
	CHECK( both.error.details["field"].asString() == "cases[0]" );

	// Version gate.
	bad = suiteDoc();
	bad["schema"] = "sicnu.agentbench.suite/v2";
	const SuiteParse versioned = parseSuite( deterministicSerialize( bad ) );
	CHECK( versioned.parsed == std::nullopt );
	CHECK( versioned.error.code == "agentbench.schema_version_unknown" );
}

TEST_CASE( "suite run grades every entry and summarizes verdicts", "[agentbench][suite]" )
{
	const SuiteDoc suite = parseSuite( deterministicSerialize( suiteDoc() ) ).parsed.value();
	const SuiteRun run = runSuite( suite, makeLoader( caseDoc( "suite/a", "optical" ), caseDoc( "suite/b", "change" ) ) );
	REQUIRE( run.error.code.empty() );
	REQUIRE( run.report.has_value() );
	const SuiteReport &report = *run.report;
	REQUIRE( report.cases.size() == 2 );
	CHECK( report.cases[0].caseId == "suite/a" );
	CHECK( report.cases[0].verdict == "PASS" );
	CHECK( report.cases[1].verdict == "FAIL" );
	CHECK( report.summary.passCount == 1 );
	CHECK( report.summary.failCount == 1 );
	CHECK( report.summary.warningsCount == 0 );
	CHECK( !report.packDigest.empty() );
	CHECK( !report.digest.empty() );

	// Deterministic double run.
	const SuiteRun again = runSuite( suite, makeLoader( caseDoc( "suite/a", "optical" ), caseDoc( "suite/b", "change" ) ) );
	REQUIRE( again.report.has_value() );
	CHECK( report.digest == again.report->digest );
	CHECK( report.packDigest == again.report->packDigest );
}

TEST_CASE( "pack digest pins content: any document change moves it", "[agentbench][suite]" )
{
	const SuiteDoc suite = parseSuite( deterministicSerialize( suiteDoc() ) ).parsed.value();
	const SuiteRun baseline = runSuite( suite, makeLoader( caseDoc( "suite/a", "optical" ), caseDoc( "suite/b", "change" ) ) );
	REQUIRE( baseline.report.has_value() );

	// Mutate one case document.
	Json::Value mutated = caseDoc( "suite/a", "optical" );
	mutated["goal"] = "Deliver the product, better.";
	const SuiteRun changed = runSuite( suite, makeLoader( mutated, caseDoc( "suite/b", "change" ) ) );
	REQUIRE( changed.report.has_value() );
	CHECK( baseline.report->packDigest != changed.report->packDigest );

	// Missing document → typed error.
	SuiteRun missing = runSuite( suite, []( const std::string &path ) -> std::optional<std::string> {
		if ( path == "scripts/b.json" )
			return std::nullopt;
		if ( path == "suite.json" )
			return deterministicSerialize( suiteDoc() );
		if ( path == "scripts/a.json" )
			return deterministicSerialize( scriptDoc( "suite/a", true ) );
		if ( path == "cases/a.json" )
			return deterministicSerialize( caseDoc( "suite/a", "optical" ) );
		if ( path == "cases/b.json" )
			return deterministicSerialize( caseDoc( "suite/b", "change" ) );
		return std::nullopt;
	} );
	CHECK( missing.report == std::nullopt );
	REQUIRE( missing.error.code == "agentbench.suite_invalid" );
	CHECK( missing.error.details["path"].asString() == "scripts/b.json" );
}

TEST_CASE( "suite report renders to the versioned json document", "[agentbench][suite]" )
{
	const SuiteDoc suite = parseSuite( deterministicSerialize( suiteDoc() ) ).parsed.value();
	const SuiteRun run = runSuite( suite, makeLoader( caseDoc( "suite/a", "optical" ), caseDoc( "suite/b", "change" ) ) );
	REQUIRE( run.report.has_value() );
	const Json::Value json = suiteReportToJson( *run.report );
	CHECK( json["schema"].asString() == "sicnu.agentbench.suite_report/v1" );
	CHECK( json["suite_id"].asString() == "rs14-agent-bench-core" );
	CHECK( json["summary"]["pass"].asInt() == 1 );
	CHECK( json["summary"]["fail"].asInt() == 1 );
	CHECK( json["cases"].size() == 2 );
	CHECK( json["digest"].asString() == run.report->digest );
}
