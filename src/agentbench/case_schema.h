// src/agentbench/case_schema.h
#pragma once

//
// AgentCase — versioned benchmark case schema (sicnu.agentbench.case/v1).
//
// A case is a goal-level benchmark document: goal, declared initial state,
// allowed tools, HIDDEN invariants (the agent never sees them — they are the
// grading oracle), a resource budget, expected evidence, optional fault
// injections and an optional closed-taxonomy failure expectation.
//
// Parsing is total and typed: every rejection carries a machine-readable
// `agentbench.*` code plus the offending field path in `details.field`.
// Unknown extra fields are tolerated (additive forward compatibility) and
// pinned by the digest: `digest()` hashes the document bytes as parsed, so
// any content change — including unknown fields — changes the digest.
// `digest()` is a local deterministic serialization (see json_writer.h); it
// pins and replays benchmark content and is never a run identity.
//

#include "errors.h"
#include "json_writer.h"

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace sicnu::agentbench
{

inline constexpr const char *kAgentCaseSchemaTag = "sicnu.agentbench.case/v1";
inline constexpr int kAgentCaseSchemaVersion = 1;

/// Closed task families (wire strings).
enum class TaskFamily
{
	Optical,
	Classification,
	Change,
	Temporal,
	Model,
	MapDelivery,
};
std::string taskFamilyToString( TaskFamily family );
bool parseTaskFamily( const std::string &wire, TaskFamily &out );

enum class InvariantDimension
{
	Scientific,
	Process,
};
std::string invariantDimensionToString( InvariantDimension dimension );
bool parseInvariantDimension( const std::string &wire, InvariantDimension &out );

enum class Severity
{
	Error,
	Warning,
};
std::string severityToString( Severity severity );
bool parseSeverity( const std::string &wire, Severity &out );

/// Closed invariant kinds (the grading oracle vocabulary, Slice D).
enum class InvariantKind
{
	ToolUsed,
	ToolNotUsed,
	StepOrder,
	ResultSuccess,
	ErrorCodePresent,
	EvidenceExists,
	FieldEquals,
	FieldContains,
	NumericLe,
	NumericGe,
	VerdictIs,
	ExplanationMentions,
	ClaimConsistent,
	BudgetWithin,
};
std::string invariantKindToString( InvariantKind kind );
bool parseInvariantKind( const std::string &wire, InvariantKind &out );

/// Closed fault kinds (Slice F).
enum class FaultKind
{
	TransientFailure,
	CorruptedResult,
	ToolUnavailable,
};
std::string faultKindToString( FaultKind kind );
bool parseFaultKind( const std::string &wire, FaultKind &out );

struct Invariant
{
	std::string id;
	InvariantKind kind = InvariantKind::ToolUsed;
	InvariantDimension dimension = InvariantDimension::Process;
	Severity severity = Severity::Error;
	Json::Value params{Json::objectValue};
};

struct EvidenceExpectation
{
	std::string id;
	std::string kind; ///< closed: raster|vector|map|table|report
	std::vector<std::string> requiredFields;
	std::string path;                 ///< optional expected path inside case scope
	bool requireInExplanation = false; ///< explanation-completeness requirement (metric 8)
};

struct FaultSpec
{
	FaultKind kind = FaultKind::TransientFailure;
	int atStep = 0;      ///< 0-based scripted-step anchor
	std::string tool;    ///< optional tool-name anchor ("" = step anchor only)
};

struct ResourceBudget
{
	int maxToolCalls = 0;
	int maxTokens = 0;
	int maxRetries = 0;
};

struct AgentCase
{
	std::string caseId;
	std::string title;
	std::string description;
	TaskFamily family = TaskFamily::Optical;
	std::string goal;
	Json::Value initialState{Json::objectValue};
	std::vector<std::string> allowedTools;
	std::vector<Invariant> invariants;
	std::vector<EvidenceExpectation> evidence;
	ResourceBudget budget;
	int minimalSteps = 0;
	std::vector<std::string> redundantTools;
	Json::Value failureExpectation; ///< null when absent
	std::vector<FaultSpec> faults;
	Json::Value raw; ///< the document as parsed (digest input)

	/// Deterministic content digest of the document as parsed.
	std::string digest() const { return deterministicSerialize( raw ); }
};

struct CaseParse
{
	std::optional<AgentCase> parsed; ///< has_value on success
	BenchError error;                ///< meaningful when !parsed
};

/// Parses a case document from JSON text.
CaseParse parseCase( const std::string &jsonText );

/// Reconstructs the canonical v1 document from the typed model (round-trip
/// fidelity for documents carrying only v1-known fields).
Json::Value caseToJson( const AgentCase &caseValue );

/// Declared workspace scope roots (`initial_state.workspace_roots`); empty
/// when the case declares no path constraint.
std::vector<std::string> caseScopeRoots( const AgentCase &caseValue );

} // namespace sicnu::agentbench
