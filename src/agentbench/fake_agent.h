// src/agentbench/fake_agent.h
#pragma once

//
// Deterministic fake agent — scripted policy → trace (sicnu.agentbench.script/v1).
//
// A script is the benchmark's controlled "agent behavior": an ordered list of
// tool calls with declared result payloads and an explicit failure policy
// (abort | retry_once | skip). Running a script against a case applies the
// case's fault schedule deterministically and produces an AgentTrace. There
// is no randomness and no environment dependence: identical (case, script)
// inputs yield byte-identical traces. The fake agent never fabricates
// success: an unresolved failure ends gave_up/blocked with an honest outcome
// claim (overridable in the script for refusal scenarios).
//

#include "case_schema.h"
#include "errors.h"
#include "trace.h"

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace sicnu::agentbench
{

inline constexpr const char *kAgentScriptSchemaTag = "sicnu.agentbench.script/v1";

/// Per-step failure policy (wire strings).
enum class OnFailure
{
	Abort,
	RetryOnce,
	Skip,
};
std::string onFailureToString( OnFailure policy );
bool parseOnFailure( const std::string &wire, OnFailure &out );

struct ScriptStep
{
	std::string tool;
	Json::Value input{Json::objectValue};
	Json::Value payload{Json::objectValue}; ///< declared tool-result payload (fake surface)
	OnFailure onFailure = OnFailure::Abort;
	std::string evidenceId; ///< "" = this step declares no evidence
	int tokens = 100;       ///< declared token cost of the call
};

struct AgentScript
{
	std::string scriptId;
	std::string caseId;
	std::string agentName;
	std::string agentVersion;
	long long seed = 0;
	std::vector<ScriptStep> steps;
	std::string explanation;
	bool hasOutcomeClaim = false;
	bool outcomeClaimSuccess = false;
	std::string outcomeClaimNote;
	std::string stopReasonOverride; ///< "" = derive from execution flow
	Json::Value raw;

	std::string digest() const { return deterministicSerialize( raw ); }
};

struct ScriptParse
{
	std::optional<AgentScript> parsed;
	BenchError error; ///< scripts share a single invalid code; field paths in details
};

ScriptParse parseScript( const std::string &jsonText );
Json::Value scriptToJson( const AgentScript &script );

struct ScriptRun
{
	std::optional<AgentTrace> trace; ///< absent when the script misuses the case
	BenchError error;
};

/// Runs a script against a case (applying the case's fault schedule) and
/// produces a deterministic trace. Cross-validates the script against the
/// case (tool allow-list, evidence references) BEFORE running; a misusing
/// script yields a typed error and no trace.
ScriptRun runScript( const AgentCase &caseValue, const AgentScript &script );

} // namespace sicnu::agentbench
