// tests/test_agentbench_report.cpp
//
// Slice G RED: machine JSON + human Markdown reports. Rendering is a pure
// function of the evaluation (byte-deterministic double render); writing is
// confined to a caller-supplied directory and fails with a typed error, not
// a partial silent file.

#include <catch2/catch_test_macros.hpp>

#include "agentbench/case_schema.h"
#include "agentbench/evaluator.h"
#include "agentbench/fake_agent.h"
#include "agentbench/json_writer.h"
#include "agentbench/report.h"

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace sicnu::agentbench;

namespace
{

struct World
{
	AgentCase agentCase;
	AgentScript script;
};

World happyWorld( const Json::Value &caseExtra = Json::Value(), const Json::Value &scriptExtra = Json::Value() )
{
	World world;
	Json::Value doc{Json::objectValue};
	doc["schema"] = "sicnu.agentbench.case/v1";
	doc["case_id"] = "optical/report";
	doc["title"] = "Report case";
	doc["task_family"] = "optical";
	doc["goal"] = "Deliver NDVI.";
	doc["initial_state"] = Json::Value( Json::objectValue );
	Json::Value tools{Json::arrayValue};
	tools.append( "rs:ndvi" );
	tools.append( "harness:verify" );
	doc["allowed_tools"] = tools;
	Json::Value invariants{Json::arrayValue};
	Json::Value inv{Json::objectValue};
	inv["id"] = "sci";
	inv["kind"] = "claim_consistent";
	inv["dimension"] = "scientific";
	inv["severity"] = "error";
	inv["params"] = Json::Value( Json::objectValue );
	invariants.append( inv );
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
	doc["resource_budget"] = budget;
	doc["minimal_steps"] = 2;
	if ( !caseExtra.isNull() && caseExtra.isObject() )
		for ( const auto &name : caseExtra.getMemberNames() )
			doc[name] = caseExtra[name];
	world.agentCase = parseCase( deterministicSerialize( doc ) ).parsed.value();

	Json::Value script{Json::objectValue};
	script["schema"] = "sicnu.agentbench.script/v1";
	script["script_id"] = "ref/report";
	script["case_id"] = "optical/report";
	Json::Value agent{Json::objectValue};
	agent["name"] = "scripted-reference";
	agent["version"] = "1.0.0";
	script["agent"] = agent;
	script["explanation"] = "Done.";
	Json::Value steps{Json::arrayValue};
	Json::Value step0{Json::objectValue};
	step0["tool"] = "rs:ndvi";
	step0["input"] = Json::Value( Json::objectValue );
	step0["payload"] = Json::Value( Json::objectValue );
	steps.append( step0 );
	if ( !scriptExtra.isNull() && scriptExtra.isObject() )
	{
		for ( const auto &name : scriptExtra.getMemberNames() )
			script[name] = scriptExtra[name];
	}
	else
		script["steps"] = steps;
	world.script = parseScript( deterministicSerialize( script ) ).parsed.value();
	return world;
}

CaseEvaluation evaluateHappy()
{
	const World world = happyWorld();
	const ScriptRun scriptRun = runScript( world.agentCase, world.script );
	REQUIRE( scriptRun.trace.has_value() );
	return evaluateCase( world.agentCase, *scriptRun.trace );
}

} // namespace

TEST_CASE( "markdown report renders verdict, metrics and failure class deterministically", "[agentbench][report]" )
{
	const CaseEvaluation evaluation = evaluateHappy();
	const std::string first = renderReportMarkdown( evaluation );
	const std::string second = renderReportMarkdown( evaluation );
	REQUIRE( first == second );

	CHECK( first.find( "# Agent Benchmark Report" ) == 0 );
	CHECK( first.find( "optical/report" ) != std::string::npos );
	CHECK( first.find( "PASS" ) != std::string::npos );
	CHECK( first.find( "task_completion" ) != std::string::npos );
	CHECK( first.find( "failure class" ) != std::string::npos );

	// Unavailable metrics render as n/a with the reason — never as 0.
	const World world = happyWorld();
	AgentTrace emptyTrace;
	emptyTrace.traceId = "t-empty";
	emptyTrace.caseId = world.agentCase.caseId;
	emptyTrace.agentName = "a";
	emptyTrace.agentKind = AgentKind::Recorded;
	emptyTrace.agentVersion = "1";
	emptyTrace.stopReason = StopReason::GaveUp;
	const CaseEvaluation emptyEval = evaluateCase( world.agentCase, emptyTrace );
	const std::string emptyMarkdown = renderReportMarkdown( emptyEval );
	CHECK( emptyEval.metric( "plan_efficiency" )->value.isNull() );
	CHECK( emptyMarkdown.find( "n/a" ) != std::string::npos );
	CHECK( emptyMarkdown.find( "not_started" ) != std::string::npos );
}

TEST_CASE( "json report is the versioned evaluation document, byte-stable on rewrite", "[agentbench][report]" )
{
	const CaseEvaluation evaluation = evaluateHappy();
	const std::string json = renderReportJson( evaluation );
	CHECK( json.find( "sicnu.agentbench.evaluation/v1" ) != std::string::npos );
	CHECK( json == renderReportJson( evaluateHappy() ) );

	// Writes go to a caller-supplied directory only.
	const std::filesystem::path dir = std::filesystem::temp_directory_path() /
	                                  ( "agentbench-report-test-" + std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() ) );
	std::filesystem::create_directories( dir );
	const ReportResult result = writeReport( evaluation, dir.string(), "case-report" );
	REQUIRE( result.error.code.empty() );
	CHECK( std::filesystem::exists( dir / "case-report.json" ) );
	CHECK( std::filesystem::exists( dir / "case-report.md" ) );

	std::ifstream jsonFile( dir / "case-report.json" );
	std::stringstream jsonBuffer;
	jsonBuffer << jsonFile.rdbuf();
	CHECK( jsonBuffer.str() == json );

	std::ifstream mdFile( dir / "case-report.md" );
	std::stringstream mdBuffer;
	mdBuffer << mdFile.rdbuf();
	CHECK( mdBuffer.str() == renderReportMarkdown( evaluation ) );

	std::filesystem::remove_all( dir );
}

TEST_CASE( "write failures are typed and leave no partial file", "[agentbench][report]" )
{
	const CaseEvaluation evaluation = evaluateHappy();
	// A file used as a directory → open fails.
	const std::filesystem::path blocker = std::filesystem::temp_directory_path() / "agentbench-report-blocker.txt";
	{
		std::ofstream out( blocker );
		out << "not a directory";
	}
	const ReportResult result = writeReport( evaluation, blocker.string(), "case-report" );
	CHECK( result.error.code == "agentbench.report_write_failed" );
	std::filesystem::remove( blocker );
}
