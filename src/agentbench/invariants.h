// src/agentbench/invariants.h
#pragma once

//
// Hidden-invariant oracle: evaluates a case's invariants against a trace.
//
// Invariants are the grading truth — the agent never sees them. Every kind
// is closed (see InvariantKind in case_schema.h) and produces a typed
// InvariantResult with the observed evidence; there is no partial credit and
// no silent fallback: missing observations mean "not passed", with the
// reason recorded. Evaluation is a pure function of (case, trace, replay).
//

#include "case_schema.h"
#include "trace.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::agentbench
{

struct InvariantResult
{
	std::string invariantId;
	bool passed = false;
	Severity severity = Severity::Error;
	InvariantDimension dimension = InvariantDimension::Process;
	Json::Value evidence{Json::objectValue}; ///< observed values / failure reason
};

struct InvariantContext
{
	const AgentCase &caseValue;
	const AgentTrace &trace;
	const ReplayValidation &replay;
};

/// Evaluates every invariant in the case, in case order.
std::vector<InvariantResult> evaluateInvariants( const InvariantContext &context );

} // namespace sicnu::agentbench
