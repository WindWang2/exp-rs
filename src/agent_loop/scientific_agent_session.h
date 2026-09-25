// src/agent_loop/scientific_agent_session.h
#pragma once

//
// RS14-11 Evidence-first Agent Loop: the session driver.
//
// ScientificAgentSession runs one bounded, evidence-first session:
//
//   goal normalization → data-state snapshot → plan request → preflight →
//   (repair approval) → execute → verify → (diagnose → replan)* → delivery
//
// It ORCHESTRATES: every capability is reached through the seam interfaces
// (session_seams.h), the authoritative engine is only ever touched through
// the executor seam, and every key decision is appended to the journal as
// a typed DecisionRecord. The driver never plans science itself, never
// inserts repairs, never verifies by itself, and never reports success it
// did not verify.
//
// Bounds (session_policy.h): run mode, replan budget, no-progress
// detection, resource budget against the plan's declared estimates, a hard
// step limit, an executor poll timeout, and cooperative cancellation.
//

#include "decision_record.h"
#include "session_journal.h"
#include "session_policy.h"
#include "session_seams.h"
#include "session_state.h"

#include <atomic>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace sicnu::agent_loop {

struct SessionRunRequest {
    std::string goal;
    std::string intent; ///< optional intent hint; empty = the planner decides
    Json::Value refs{ Json::objectValue }; ///< slot -> reference (paths / asset ids)
};

/// The machine-readable deliverable: one document a future agent (or a
/// student) can read to understand exactly what the session did and why.
struct EvidenceSummary {
    std::string schemaVersion = "1.0";
    std::string sessionId;
    std::string goal;
    std::string mode;
    std::string outcome;    ///< "delivered" | "refused" | "aborted"
    std::string stopReason; ///< typed code; empty on delivered
    Json::Value policy{ Json::objectValue };
    std::vector< std::string > stages; ///< visited stages, in order
    std::vector< DecisionRecord > decisions;
    std::string verificationVerdict; ///< "" when nothing was verified
    std::vector< std::string > artifacts;
    Json::Value budgets{ Json::objectValue };
    std::size_t journalEntries = 0;
    Json::Value replay{ Json::objectValue };
    bool wouldExecute = false; ///< dry_run: the plan that WOULD have run

    Json::Value toJson() const;
};

struct SessionResult {
    std::string sessionId;
    bool ok = false; ///< true only for a delivered session
    std::string terminalState;
    std::string stopReason;
    EvidenceSummary summary;
    SessionJournal journal;
};

class ScientificAgentSession {
  public:
    struct Dependencies {
        IDataStateProvider *data = nullptr;
        IPlanner *planner = nullptr;
        IPreflight *preflight = nullptr;
        IExecutor *executor = nullptr;
        IVerifier *verifier = nullptr;
        IDiagnoser *diagnoser = nullptr;
    };

    /// `clock` injects logical time (journal `at`); the default is a
    /// deterministic monotonic counter so tests never depend on wall time.
    /// `run` may be called once per instance.
    ScientificAgentSession( SessionPolicy policy, Dependencies deps,
                            std::function< long long() > clock = {},
                            std::string sessionId = {} );

    /// Resume a session from its persisted journal: the stage machine
    /// restarts at the journal's final stage, the journal (with its
    /// sequence and decision numbering) is adopted, and the attempt counter
    /// is derived from the recorded replans. Seams are re-injected — no
    /// work is re-executed; the resumed session appends to the same
    /// journal.
    /// Only the PRE-PLAN stages (goal_normalization, data_state_snapshot)
    /// are resumable: everything from plan_request onward dispatches on
    /// in-memory state the journal does not carry as values, so a resume
    /// there would run real seams over default-constructed state and
    /// fabricate a delivery. A session parked past the plan seam (or a
    /// terminal journal) returns nullopt and must restart as a new session.
    /// run() on a resumed session must restate the journalled goal; a
    /// different goal is refused (SESSION_GOAL_MISMATCH).
    static std::optional< ScientificAgentSession > resume(
        const SessionJournal &journal, SessionPolicy policy, Dependencies deps,
        std::function< long long() > clock = {} );

    /// Cooperative cancellation: the session stops at the next stage
    /// boundary and cancels any in-flight execution.
    void requestCancel() { mCancelRequested->store( true ); }

    /// Opt-in crash-evidence sink: invoked after EVERY journal append with
    /// the journal snapshot, so a host can flush the evidence trail to disk
    /// at each boundary. The loop stays the only state machine — the sink
    /// observes the journal, it never mutates machine state, and when unset
    /// the loop behaves exactly as before. The sink must be cheap (it runs
    /// mid-step) and must not re-enter the session.
    void setCheckpointSink( std::function< void( const SessionJournal & ) > sink )
    {
        mCheckpointSink = std::move( sink );
    }

    SessionResult run( const SessionRunRequest &request );

  private:
    /// Adopting constructor for resume(); see resume().
    ScientificAgentSession( SessionPolicy policy, Dependencies deps, SessionJournal adopted,
                            int attempt, int decisionSeq,
                            std::function< long long() > clock );

    // --- journal helpers -------------------------------------------------
    long long now();
    bool enterStage( const std::string &stage );
    void note( const std::string &event, const std::string &stage, Json::Value payload );
    void recordDecision( const std::string &stage, DecisionRecord decision );

    // --- terminal helpers ------------------------------------------------
    bool refuse( const std::string &stopReason );
    bool abort( const std::string &stopReason );

    /// Builds the machine-readable delivery document from the journal, the
    /// decisions and the budget consumption.
    EvidenceSummary buildSummary() const;

    // --- stage handlers (return true to continue the loop) ----------------
    bool stageGoalNormalization( const SessionRunRequest &request );
    bool stageDataStateSnapshot( const SessionRunRequest &request );
    bool stagePlanRequest( const SessionRunRequest &request );
    bool stagePreflight();
    bool stageRepairApproval();
    bool stageExecute();
    bool stageVerify();
    bool stageDiagnose();
    bool stageReplan();
    bool stageDelivery();

    // --- shared state ------------------------------------------------------
    SessionPolicy mPolicy;
    Dependencies mDeps;
    SessionStageMachine mMachine;
    SessionJournal mJournal;
    std::function< long long() > mClock;
    std::function< void( const SessionJournal & ) > mCheckpointSink;
    long long mClockValue = 0;
    std::shared_ptr< std::atomic< bool > > mCancelRequested{ std::make_shared< std::atomic< bool > >( false ) };

    std::string mGoal;
    DataStateSnapshot mSnapshot;
    PlanDraft mPlan;
    PreflightReport mPreflightReport;
    ExecutionOutcome mOutcome;
    VerificationReport mVerification;
    Diagnosis mDiagnosis;
    int mAttempt = 1;
    int mStepCount = 0;
    int mDecisionSeq = 0;
    bool mVerified = false;
    std::string mStopReason;
    std::string mLastFailureCode;
    std::map< std::string, int > mFailureKeys; ///< no-progress detector
    std::vector< std::string > mApprovedRepairs;
    std::vector< std::string > mVisitedStages;
    std::vector< DecisionRecord > mDecisions;
};

/// Teaching gate predicate on ALREADY-NORMALIZED policy inputs: the lab
/// (teaching) domain with the student role withholds artifact-producing
/// execution. Role normalization itself belongs to the adapter
/// (harness_actions::normalizeLabRole is the single source); this core
/// only applies the decision to normalized values.
bool teachingGateBlocksExecution( const TeachingPolicy &policy );

} // namespace sicnu::agent_loop
