// src/agentbench/trace.h
#pragma once

//
// AgentTrace — versioned trajectory schema (sicnu.agentbench.trace/v1) and
// replay validation against a case.
//
// A trace is the unit of benchmark input: an ordered list of tool calls with
// recorded results (produced by the deterministic fake agent, replayed from a
// recording, or projected from a live session), the final evidence the agent
// claims, its explanation, and its outcome claim. Parsing is total and typed;
// replay validation is a pure function of (case, trace) that never executes
// anything — it reports typed violations and resource usage for the evaluator
// to score. Double validation of the same pair is deterministic.
//

#include "case_schema.h"
#include "errors.h"
#include "json_writer.h"

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace sicnu::agentbench
{

inline constexpr const char *kAgentTraceSchemaTag = "sicnu.agentbench.trace/v1";
inline constexpr int kAgentTraceSchemaVersion = 1;

/// Where a trace came from (wire strings).
enum class AgentKind
{
	Fake,
	Recorded,
	Live,
};
std::string agentKindToString( AgentKind kind );
bool parseAgentKind( const std::string &wire, AgentKind &out );

/// Why the agent stopped (wire strings).
enum class StopReason
{
	Completed,
	BudgetExhausted,
	GaveUp,
	Blocked,
};
std::string stopReasonToString( StopReason reason );
bool parseStopReason( const std::string &wire, StopReason &out );

struct TraceStep
{
	int index = 0;
	std::string tool;
	Json::Value input{Json::objectValue};
	bool success = false;
	std::string errorCode; ///< non-empty exactly when success == false
	Json::Value payload{Json::objectValue};
	int tokens = 0;
};

struct TraceEvidence
{
	std::string id;
	std::string kind; ///< closed: raster|vector|map|table|report
	std::string path; ///< optional, must lie inside the case scope when declared
	Json::Value fields{Json::objectValue};
	std::string verdict; ///< optional closed: PASS|PASS_WITH_WARNINGS|FAIL
};

struct AgentTrace
{
	std::string traceId;
	std::string caseId;
	std::string agentName;
	AgentKind agentKind = AgentKind::Fake;
	std::string agentVersion;
	long long seed = 0;
	std::vector<TraceStep> steps;
	std::vector<TraceEvidence> evidence;
	std::string explanation;
	bool outcomeClaimSuccess = false;
	std::string outcomeClaimNote;
	StopReason stopReason = StopReason::GaveUp;
	Json::Value raw; ///< document as parsed (digest input)

	/// Deterministic content digest of the trace document as parsed.
	std::string digest() const { return deterministicSerialize( raw ); }
};

struct TraceParse
{
	std::optional<AgentTrace> parsed; ///< has_value on success
	BenchError error;                 ///< meaningful when !parsed
};

TraceParse parseTrace( const std::string &jsonText );
Json::Value traceToJson( const AgentTrace &trace );

/// One typed replay violation against the case contract.
struct ReplayViolation
{
	std::string code; ///< agentbench.* error code
	std::string summary;
	Json::Value details{Json::objectValue};
};

/// Resource usage computed from the trace (already retries-aware).
struct ResourceUsage
{
	int toolCalls = 0;
	long long tokens = 0;
	int retries = 0; ///< post-failure repeats of an identical (tool, input)
};

struct ReplayValidation
{
	std::vector<ReplayViolation> violations; ///< empty when the trace is replayable as-is
	ResourceUsage usage;
};

/// Validates a trace against a case: case/trace pairing, allowed-tool scope,
/// declared workspace-scope path containment, and resource budget. Pure —
/// never executes anything; deterministic for identical inputs.
ReplayValidation validateReplay( const AgentCase &caseValue, const AgentTrace &trace );

} // namespace sicnu::agentbench
