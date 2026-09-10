// diagnostic_report.h — machine-readable diagnostic envelope (task G,
// Verification 7.0).
//
// One shape for every module's failures, built ON TOP of the existing stable
// vocabularies (harness_error codes, RSOperatorError, GeoError, output.*
// diagnostics) — never a second taxonomy. Rules inherited from
// DiagnosticCatalog: the origin code is preserved verbatim; unknown codes are
// carried honestly (recoverability Unknown), never renamed, never swallowed.
//
//   { "schema": "exp.diag.v1", "code", "component", "run", "task", "job",
//     "recoverability": "none|manual|transient|unknown",
//     "suggested_action", "cause_chain": [...], "artifacts": [...] }
//
// The report correlates with the unified trace through the same
// run/task/job ids (see trace_id.h).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::runtime::observability::diagnostics
{

enum class Recoverability : uint8_t
{
    None,      ///< do not retry; operator input or code defect
    Manual,    ///< retry after human action (fix inputs, free disk, …)
    Transient, ///< bounded automatic retry is meaningful
    Unknown    ///< origin did not declare; honest default
};

inline const char *recoverabilityName( Recoverability r )
{
    switch ( r )
    {
    case Recoverability::None: return "none";
    case Recoverability::Manual: return "manual";
    case Recoverability::Transient: return "transient";
    case Recoverability::Unknown: return "unknown";
    }
    return "unknown";
}

struct DiagnosticReport
{
    std::string code;     ///< stable origin code, verbatim
    std::string component;///< emitting module ("processing.output_committer", …)
    std::string run;      ///< workflow / harness run id
    std::string task;     ///< TaskCenter task id
    std::string job;      ///< JobEngine job id
    Recoverability recoverability = Recoverability::Unknown;
    std::string suggestedAction; ///< one-line, honest guidance ("" = none known)
    /// Causal chain, outermost first (e.g. workflow → task → operator error).
    std::vector<std::string> causeChain;
    /// Artifact references for post-mortem (checkpoint path, temp file, …).
    std::vector<std::string> artifacts;

    /// Single-line JSON (schema exp.diag.v1). All strings escaped; empty
    /// fields omitted. Never throws.
    std::string toJson() const;
};

} // namespace sicnu::runtime::observability::diagnostics
