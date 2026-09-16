// src/agent/harness/workflow_explain.h
#pragma once

//
// Compiler & Grounding 11.0: decision traceback (compile -> run -> failure).
//
// Answers "WHICH fact / contract / decision produced this outcome" from the
// compiler's own records — never from chat memory: the analysis (which check
// failed and why), the repair evidence (which rule inserted what), the
// refusals (which decisions are waiting on the caller), and the run's typed
// error code (which contract the run violated). Every cause cites the fact
// provenance that grounds it, so an explanation is auditable back to
// observed/declared/derived/unknown.
//
// Readable and bounded: the summary and causes carry zh-CN one-liners
// (closed templates keyed by error code), each clamped; the whole output is
// capped (kMaxCauses causes, kMaxTextBytes serialized budget) and reports
// truncation honestly. Structured fields stay machine-readable for the UI.
//

#include <json/json.h>
#include <string>
#include <vector>

#include "workflow_analysis.h"
#include "workflow_ir.h"

namespace sicnu::agent::harness::explain {

struct ExplainLimits
{
    static constexpr int kMaxCauses = 8;
    static constexpr int kMaxTextChars = 256;   ///< per zh string (clamped, counted)
    static constexpr int kMaxTextBytes = 8192;  ///< serialized output budget
};

/// Machine-readable mirror of ExplainLimits.
Json::Value explainLimits();

struct ExplainRequest
{
    const WorkflowIr *ir = nullptr;                 ///< optional context
    const IrAnalysis *analysis = nullptr;           ///< required: the check ledger + issues
    const std::vector<IrRepairRecord> *repairs = nullptr;   ///< optional
    const std::vector<IrRefusal> *refusals = nullptr;       ///< optional
    std::string runErrorCode;                       ///< optional typed run failure
    std::string failedNode;                         ///< optional failing node id
};

/// Builds the bounded decision-traceback document:
/// { schema_version, summary_zh, causes[], causes_total, truncated,
///   serialized_bytes, fact_basis{...} }.
Json::Value explainDecisionChain( const ExplainRequest &request );

/// The closed zh-CN template table for error codes (drift anchor for tests).
/// Returns "" for codes with no template — callers fall back to the code.
std::string zhTemplateForCode( const std::string &code );

} // namespace sicnu::agent::harness::explain
