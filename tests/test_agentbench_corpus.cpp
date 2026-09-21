// tests/test_agentbench_corpus.cpp
//
// Slice H RED part 2: corpus self-consistency + version pinning over the
// checked-in pack (data/agent/bench). The pack must stay schema-valid, cover
// all six task families with ≥ 20 cases, grade end-to-end through the suite
// runner, and pin its digests — any content drift is a conscious suite
// version bump, never a silent one.

#include <catch2/catch_test_macros.hpp>

#include "agentbench/case_schema.h"
#include "agentbench/json_writer.h"
#include "agentbench/suite.h"

#include <filesystem>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace sicnu::agentbench;

namespace
{

/// FNV-1a 64 — local drift fingerprint for digest pinning (the digest
/// equality across runs is asserted separately; this pin only detects
/// accidental content change between review and merge).
std::uint64_t fnv1a64( const std::string &text )
{
	std::uint64_t hash = 1469598103934665603ull;
	for ( const char c : text )
	{
		hash ^= static_cast<std::uint8_t>( c );
		hash *= 1099511628211ull;
	}
	return hash;
}

const std::filesystem::path kCorpusRoot = std::filesystem::path( CMAKE_SOURCE_DIR ) / "data" / "agent" / "bench";

std::string readFile( const std::filesystem::path &path )
{
	std::ifstream in( path, std::ios::binary );
	std::ostringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

Loader corpusLoader()
{
	return []( const std::string &relative ) -> std::optional<std::string> {
		const std::filesystem::path path = kCorpusRoot / relative;
		if ( !std::filesystem::exists( path ) )
			return std::nullopt;
		return readFile( path );
	};
}

const SuiteDoc &suite()
{
	static const SuiteDoc parsed = parseSuite( readFile( kCorpusRoot / "suite.json" ) ).parsed.value();
	return parsed;
}

std::vector<std::filesystem::path> caseFiles()
{
	std::vector<std::filesystem::path> files;
	for ( const auto &entry : std::filesystem::directory_iterator( kCorpusRoot / "cases" ) )
		if ( entry.path().extension() == ".json" )
			files.push_back( entry.path() );
	return files;
}

} // namespace

TEST_CASE( "suite doc loads and every corpus document parses", "[agentbench][corpus]" )
{
	const auto suiteText = readFile( kCorpusRoot / "suite.json" );
	const SuiteParse suiteParsed = parseSuite( suiteText );
	REQUIRE( suiteParsed.error.code.empty() );
	REQUIRE( suiteParsed.parsed.has_value() );
	CHECK( suiteParsed.parsed->suiteId == "rs14-agent-bench-core" );

	for ( const auto &file : caseFiles() )
	{
		CAPTURE( file.filename().string() );
		const CaseParse parsed = parseCase( readFile( file ) );
		INFO( parsed.error.code << " " << deterministicSerialize( parsed.error.details ) );
		REQUIRE( parsed.parsed.has_value() );
	}
}

TEST_CASE( "pack size and family coverage meet the track floor", "[agentbench][corpus]" )
{
	REQUIRE( suite().entries.size() >= 20 );

	std::map<std::string, int> families;
	for ( const auto &file : caseFiles() )
	{
		const AgentCase parsed = parseCase( readFile( file ) ).parsed.value();
		families[taskFamilyToString( parsed.family )]++;
	}
	for ( const char *family : { "optical", "classification", "change", "temporal", "model", "map_delivery" } )
	{
		CAPTURE( family );
		CHECK( families[family] >= 3 );
	}
}

TEST_CASE( "every suite entry pairs a valid case with a valid script or trace", "[agentbench][corpus]" )
{
	std::vector<std::string> pairs;
	for ( const SuiteEntry &entry : suite().entries )
	{
		CAPTURE( entry.casePath );
		CHECK( std::filesystem::exists( kCorpusRoot / entry.casePath ) );
		const bool oneSource = entry.scriptPath.empty() != entry.tracePath.empty();
		CHECK( oneSource );
		const std::string sourcePath = entry.scriptPath.empty() ? entry.tracePath : entry.scriptPath;
		CHECK( std::filesystem::exists( kCorpusRoot / sourcePath ) );
		pairs.push_back( entry.casePath + ">" + sourcePath );
	}
	// Entry uniqueness is on (case, source) pairs: the same case may appear
	// twice only with different sources (script vs recorded replay).
	std::sort( pairs.begin(), pairs.end() );
	CHECK( std::adjacent_find( pairs.begin(), pairs.end() ) == pairs.end() );
}

TEST_CASE( "the whole pack grades end-to-end with the pinned summary", "[agentbench][corpus]" )
{
	const SuiteRun run = runSuite( suite(), corpusLoader() );
	REQUIRE( run.error.code.empty() );
	REQUIRE( run.report.has_value() );
	INFO( run.error.code << " " << run.error.summary );

	// Pinned summary: the pack authors fix these counts when cases change.
	CHECK( run.report->cases.size() == suite().entries.size() );
	CHECK( run.report->summary.passCount + run.report->summary.warningsCount + run.report->summary.failCount ==
	       static_cast<int>( suite().entries.size() ) );
	CHECK( run.report->summary.passCount == 20 );
	CHECK( run.report->summary.warningsCount == 0 );
	CHECK( run.report->summary.failCount == 4 );

	// The known-failure entries and their closed classes (regression pins).
	std::map<std::string, std::string> failureClass;
	for ( const SuiteCaseResult &entry : run.report->cases )
		if ( entry.verdict == "FAIL" )
			failureClass[entry.caseId + "|" + entry.sourceKind] = entry.failureClass;
	REQUIRE( failureClass.size() == 4 );
	CHECK( failureClass["classification/supervised-basic|trace"] == "scope_violation" );
	CHECK( failureClass["model/inference-basic|trace"] == "silent_failure" );
	CHECK( failureClass["change/service-unavailable|script"] == "verification_failed" );
	CHECK( failureClass["temporal/budget-tight|script"] == "budget_exhausted" );

	// Recovery and honesty probes on scripted fault cases.
	std::map<std::string, double> recovery;
	for ( const SuiteCaseResult &entry : run.report->cases )
		for ( const Json::Value &metric : entry.metrics )
			if ( metric["name"].asString() == "recovery_quality" && metric["value"].isNumeric() )
				recovery[entry.caseId] = metric["value"].asDouble();
	CHECK( recovery["optical/ndvi-transient"] == 1.0 );
	CHECK( recovery["temporal/gap-fill-transient"] == 1.0 );
	CHECK( recovery["map/publisher-transient"] == 1.0 );
	CHECK( recovery["classification/ensemble-transient"] == 1.0 );
}

TEST_CASE( "pack digests are stable across double runs and pinned", "[agentbench][corpus]" )
{
	const SuiteRun first = runSuite( suite(), corpusLoader() );
	REQUIRE( first.report.has_value() );
	const SuiteRun second = runSuite( suite(), corpusLoader() );
	REQUIRE( second.report.has_value() );
	CHECK( first.report->packDigest == second.report->packDigest );
	CHECK( first.report->digest == second.report->digest );

	// Version pin: bump suite.json's version consciously when these change.
	CHECK( suite().version == "1.0.0" );
	CHECK( fnv1a64( first.report->packDigest ) == 0xa553029dbbcc6d83ull ); // rs14 starter pack v1.0.0 (to_chars serializer)
	for ( const SuiteCaseResult &entry : first.report->cases )
	{
		if ( entry.caseId == "optical/ndvi-basic" && entry.sourceKind == "script" )
			CHECK( fnv1a64( entry.evaluationDigest ) == 0xb7d5b3a1b0b86c1eull ); // optical/ndvi-basic reference evaluation v1.0.0
	}
}

TEST_CASE( "mutating any pinned document moves the pack digest", "[agentbench][corpus]" )
{
	const SuiteRun baseline = runSuite( suite(), corpusLoader() );
	REQUIRE( baseline.report.has_value() );

	// In-memory mutation of one case goal — no file writes.
	Loader mutatedLoader = corpusLoader();
	Loader mutated = [mutatedLoader]( const std::string &path ) -> std::optional<std::string> {
		if ( path == "cases/optical-ndvi-basic.json" )
		{
			const CaseParse parsed = parseCase( *mutatedLoader( path ) );
			Json::Value doc = caseToJson( *parsed.parsed );
			doc["goal"] = "Mutated goal for pin testing.";
			return deterministicSerialize( doc );
		}
		return mutatedLoader( path );
	};
	const SuiteRun changed = runSuite( suite(), mutated );
	REQUIRE( changed.report.has_value() );
	CHECK( baseline.report->packDigest != changed.report->packDigest );
}
