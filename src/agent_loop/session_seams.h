// src/agent_loop/session_seams.h
#pragma once

//
// RS14-11 Evidence-first Agent Loop: the seam interfaces.
//
// Every capability the session needs is reached through one of these
// interfaces; the session itself owns no science and no scheduler:
//
//   IDataStateProvider — understand the world (facts, never prose)
//   IPlanner           — draft a plan (intent, steps, outputs, estimates)
//   IPreflight         — ok / fixable / blocked over resolved facts
//   IExecutor          — run a plan through the AUTHORITATIVE engine
//   IVerifier          — tri-state verification of the produced artifacts
//   IDiagnoser         — typed root cause + bounded repair proposals
//
// SEAM STATUS (re-verified 2026-09): the only implementations in the tree
// are the deterministic offline doubles in fake_seams.h and the harness
// verifier adapter (src/agent/tools/agent_session_adapter.h). The remaining
// production seams (planner over compileWorkflow, preflight over
// preflightIntent, executor over WorkflowRunCoordinator/ExecutionPlane,
// diagnoser over harness:diagnose_run) are NOT wired yet — a host injects
// them through OperationsCoordinator::Dependencies; missing seams fail
// closed (the driver refuses SEAMS_UNAVAILABLE before the loop starts).
// Deterministic offline doubles for tests and the offline exemplar live in
// fake_seams.h.
//
// All value types are plain data with JSON projections; no Qt.
//

#include "decision_record.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::agent_loop {

// ---------------------------------------------------------------------------
// Data state
// ---------------------------------------------------------------------------

struct AssetFact {
    std::string slot;             ///< plan slot name, e.g. "primary"
    std::string ref;              ///< what the caller asked to resolve
    std::string kind;             ///< "raster" | "vector" | "temporal" | "unknown"
    bool resolved = false;
    Json::Value facts{ Json::objectValue }; ///< bounded understanding document
    std::string resolutionError;             ///< typed code when unresolved

    Json::Value toJson() const;
};

struct DataStateSnapshot {
    std::string schemaVersion = "1.0";
    std::vector< AssetFact > assets;
    Json::Value summary{ Json::objectValue }; ///< bounded aggregate (counts, modalities)

    Json::Value toJson() const;
    static std::optional< DataStateSnapshot > fromJson( const Json::Value &doc,
                                                        std::string *error = nullptr );
};

class IDataStateProvider {
  public:
    virtual ~IDataStateProvider() = default;
    virtual DataStateSnapshot snapshot( const std::string &goal, const Json::Value &refs ) = 0;
};

// ---------------------------------------------------------------------------
// Planner
// ---------------------------------------------------------------------------

struct PlanRequest {
    std::string goal;
    std::string intent;                ///< closed intent vocabulary; "" = custom
    DataStateSnapshot snapshot;
    std::vector< std::string > approvedRepairs; ///< repair rule ids approved for this attempt
    Json::Value diagnosis{ Json::Value() };     ///< previous failure diagnosis (replan), null on first
    int attempt = 1;
};

struct PlanEstimate {
    std::string stepId;
    long ramMb = 0; ///< 0 = the operator declared nothing
};

struct PlanDraft {
    std::string planId;
    std::string intent;
    int attempt = 1;
    Json::Value steps{ Json::arrayValue };
    Json::Value outputs{ Json::arrayValue };
    std::vector< PlanEstimate > estimates;
    std::string fingerprint; ///< identity of this draft (attempt-sensitive)
    /// Attempt-INDEPENDENT identity of the science the plan performs
    /// (intent + operators + approved repairs). Two drafts with the same
    /// identity do the same work — the no-progress detector keys on it:
    /// repeating the same science and failing the same way is no progress.
    std::string identity;
    Json::Value missingFacts{ Json::arrayValue };
    std::vector< DecisionAlternative > droppedAlternatives;
    bool valid = false;
    std::string error; ///< typed error code when invalid

    /// Sum of the declared per-step estimates (0 when nothing is declared).
    long totalRamMb() const;
    Json::Value toJson() const;
};

class IPlanner {
  public:
    virtual ~IPlanner() = default;
    virtual PlanDraft plan( const PlanRequest &request ) = 0;
};

// ---------------------------------------------------------------------------
// Preflight
// ---------------------------------------------------------------------------

struct PreflightIssue {
    std::string code;
    std::string severity; ///< "info" | "warning" | "error"
    std::string message;
};

struct RepairProposal {
    std::string ruleId;
    std::string riskClass; ///< "shape_preserving" | "radiometric" | "science_changing"
    std::string operatorId;
    Json::Value arguments{ Json::objectValue };
    std::string rationale;
};

struct PreflightReport {
    std::string verdict; ///< "ok" | "fixable" | "blocked"
    std::vector< PreflightIssue > issues;
    std::vector< RepairProposal > proposals;
    Json::Value checks{ Json::arrayValue };

    Json::Value toJson() const;
};

class IPreflight {
  public:
    virtual ~IPreflight() = default;
    virtual PreflightReport check( const PlanDraft &plan, const DataStateSnapshot &snapshot ) = 0;
};

// ---------------------------------------------------------------------------
// Executor (the single path to the authoritative engine)
// ---------------------------------------------------------------------------

struct ExecutionStart {
    std::string runId;
    bool started = false;
    std::string error; ///< typed error code when the engine refused to start
};

struct ExecutionOutcome {
    bool finished = false;
    bool succeeded = false;
    std::string runId;
    std::string state; ///< "running" | "succeeded" | "failed" | "cancelled"
    std::vector< std::string > artifacts;
    std::string errorCode;
    std::string errorMessage;
    Json::Value resultPayload{ Json::objectValue };
};

class IExecutor {
  public:
    virtual ~IExecutor() = default;
    virtual ExecutionStart begin( const PlanDraft &plan ) = 0;
    /// Bounded wait; returns a non-finished outcome only on timeout.
    virtual ExecutionOutcome poll( const ExecutionStart &start, long long timeoutMs ) = 0;
    virtual void cancel( const ExecutionStart &start ) = 0;
};

// ---------------------------------------------------------------------------
// Verifier
// ---------------------------------------------------------------------------

struct ArtifactVerificationReport {
    std::string path;
    std::string verdict; ///< "PASS" | "PASS_WITH_WARNINGS" | "FAIL"
    std::vector< std::string > warnings;

    Json::Value toJson() const;
};

struct VerificationReport {
    std::vector< ArtifactVerificationReport > artifacts;
    std::string verdictValue = "PASS"; ///< aggregate; set by the verifier implementation

    const std::string &verdict() const { return verdictValue; }

    /// Any FAIL -> FAIL; any warning -> PASS_WITH_WARNINGS; else PASS.
    static std::string aggregate( const std::vector< ArtifactVerificationReport > &artifacts );

    Json::Value toJson() const;
};

class IVerifier {
  public:
    virtual ~IVerifier() = default;
    virtual VerificationReport verify( const PlanDraft &plan,
                                       const ExecutionOutcome &outcome ) = 0;
};

// ---------------------------------------------------------------------------
// Diagnoser
// ---------------------------------------------------------------------------

struct Diagnosis {
    std::string rootCauseCode;
    std::string summary;
    std::vector< RepairProposal > proposals;
    Json::Value evidence{ Json::objectValue };

    Json::Value toJson() const;
};

class IDiagnoser {
  public:
    virtual ~IDiagnoser() = default;
    virtual Diagnosis diagnose( const PlanDraft &plan, const ExecutionOutcome &outcome,
                                const VerificationReport &verification ) = 0;
};

} // namespace sicnu::agent_loop
