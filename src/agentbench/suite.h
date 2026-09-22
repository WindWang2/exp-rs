// src/agentbench/suite.h
#pragma once

//
// Suite runner (sicnu.agentbench.suite/v1 → sicnu.agentbench.suite_report/v1).
//
// A suite pins a set of benchmark cases and, per case, either a reference
// script (executed by the deterministic fake agent) or a recorded trace
// (replayed as-is). The runner is pure: documents come from an injected
// Loader, so the same code runs in-memory and over the checked-in pack.
// The pack digest is computed over the SORTED set of (case digest, source
// digest) pairs plus the suite document — any content change moves it, which
// is what makes historical scores explainable (version pinning).
//

#include "case_schema.h"
#include "errors.h"
#include "evaluator.h"
#include "json_writer.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::agentbench
{

inline constexpr const char *kAgentSuiteSchemaTag = "sicnu.agentbench.suite/v1";
inline constexpr const char *kAgentSuiteReportSchemaTag = "sicnu.agentbench.suite_report/v1";

struct SuiteEntry
{
	std::string casePath;
	std::string scriptPath; ///< exactly one of script/trace
	std::string tracePath;
};

struct SuiteDoc
{
	std::string suiteId;
	std::string version;
	std::string description;
	std::vector<SuiteEntry> entries;
	Json::Value raw;

	std::string digest() const { return deterministicSerialize( raw ); }
};

struct SuiteParse
{
	std::optional<SuiteDoc> parsed;
	BenchError error;
};

SuiteParse parseSuite( const std::string &jsonText );

/// Returns the document text for a corpus-relative path, or nullopt when
/// the document is missing.
using Loader = std::function<std::optional<std::string>( const std::string &path )>;

struct SuiteCaseResult
{
	std::string caseId;
	std::string taskFamily;
	std::string caseDigest;
	std::string sourcePath; ///< script or trace path as declared by the suite
	std::string sourceKind; ///< "script" | "trace"
	std::string verdict;    ///< PASS | PASS_WITH_WARNINGS | FAIL
	std::string failureClass;
	std::string evaluationDigest;
	Json::Value metrics{Json::arrayValue}; ///< the 8 metric results
};

struct SuiteSummary
{
	int passCount = 0;
	int warningsCount = 0;
	int failCount = 0;
};

struct SuiteReport
{
	std::string suiteId;
	std::string version;
	std::string packDigest;
	std::vector<SuiteCaseResult> cases;
	SuiteSummary summary;
	std::string digest;
};

struct SuiteRun
{
	std::optional<SuiteReport> report;
	BenchError error; ///< agentbench.suite_invalid with the offending path
};

SuiteRun runSuite( const SuiteDoc &suite, const Loader &loader );
Json::Value suiteReportToJson( const SuiteReport &report );

} // namespace sicnu::agentbench
