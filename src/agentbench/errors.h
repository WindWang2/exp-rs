// src/agentbench/errors.h
#pragma once

//
// Typed machine-readable error vocabulary for the agentbench surface.
//
// Codes are wire strings; consumers match on code, never on prose. The
// "agentbench.*" prefix keeps them disjoint from the experiment layer's
// "experiment.*" diagnostics and the harness's SCREAMING_SNAKE tool-error
// taxonomy (tool-level error codes inside traces are opaque data here —
// agentbench never re-interprets them).
//

#include <json/json.h>
#include <string>

namespace sicnu::agentbench
{

/// A typed error: empty `code` means "no error".
struct BenchError
{
	std::string code;
	std::string summary;
	Json::Value details{Json::objectValue};

	explicit operator bool() const { return !code.empty(); }
};

namespace error_codes
{
inline constexpr const char *kSchemaVersionUnknown = "agentbench.schema_version_unknown";
inline constexpr const char *kCaseMalformed = "agentbench.case_malformed";
inline constexpr const char *kCaseInvalid = "agentbench.case_invalid";
inline constexpr const char *kTraceMalformed = "agentbench.trace_malformed";
inline constexpr const char *kTraceInvalid = "agentbench.trace_invalid";
inline constexpr const char *kScriptInvalid = "agentbench.script_invalid";
inline constexpr const char *kToolNotAllowed = "agentbench.tool_not_allowed";
inline constexpr const char *kPathOutsideScope = "agentbench.path_outside_scope";
inline constexpr const char *kBudgetExceeded = "agentbench.budget_exceeded";
inline constexpr const char *kSuiteInvalid = "agentbench.suite_invalid";
inline constexpr const char *kReportWriteFailed = "agentbench.report_write_failed";
} // namespace error_codes

inline BenchError makeError( const std::string &code, const std::string &summary )
{
	BenchError error;
	error.code = code;
	error.summary = summary;
	return error;
}

inline BenchError makeError( const std::string &code, const std::string &summary, Json::Value details )
{
	BenchError error;
	error.code = code;
	error.summary = summary;
	error.details = std::move( details );
	return error;
}

} // namespace sicnu::agentbench
