// src/agent_loop/session_state.h
#pragma once

//
// RS14-11 Evidence-first Agent Loop: the session state machine.
//
// A closed stage vocabulary (goal normalization → … → delivery), three
// terminal states (delivered / refused / aborted) and a typed transition
// table. The machine mirrors the discipline of the mission timeline
// (src/app/workbench/mission_stage.h): an illegal transition NEVER mutates
// state and NEVER bumps counters — it returns a typed error, so a driver
// bug cannot silently skip a stage or re-open a terminal session.
//
// This module is pure values: no I/O, no clocks, no threads.
//

#include <string>

namespace sicnu::agent_loop {

/// Closed stage vocabulary (wire strings; never rename).
namespace stages {
inline constexpr const char *kGoalNormalization = "goal_normalization";
inline constexpr const char *kDataStateSnapshot = "data_state_snapshot";
inline constexpr const char *kPlanRequest = "plan_request";
inline constexpr const char *kPreflight = "preflight";
inline constexpr const char *kRepairApproval = "repair_approval";
inline constexpr const char *kExecute = "execute";
inline constexpr const char *kVerify = "verify";
inline constexpr const char *kDiagnose = "diagnose";
inline constexpr const char *kReplan = "replan";
inline constexpr const char *kDelivery = "delivery";
} // namespace stages

/// Terminal states (wire strings; never rename).
namespace terminal_states {
inline constexpr const char *kDelivered = "delivered";
inline constexpr const char *kRefused = "refused";
inline constexpr const char *kAborted = "aborted";
} // namespace terminal_states

/// Typed stop reasons. The harness taxonomy codes (harness_error.h) are
/// reused verbatim where they exist (PREFLIGHT_BLOCKED, EXECUTION_FAILED,
/// OUTPUT_INVALID, RESOURCE_OVER_BUDGET, CANCELLED, TEACHING_REFUSAL);
/// the SESSION_* codes are session-level policy stops owned here.
namespace stop_reasons {
inline constexpr const char *kInvalidGoal = "SESSION_INVALID_GOAL";
inline constexpr const char *kInvalidPolicy = "SESSION_INVALID_POLICY";
inline constexpr const char *kInternalError = "SESSION_INTERNAL_ERROR";
inline constexpr const char *kNoProgress = "SESSION_NO_PROGRESS";
inline constexpr const char *kReplanLimit = "SESSION_REPLAN_LIMIT";
inline constexpr const char *kStepLimit = "SESSION_STEP_LIMIT";
inline constexpr const char *kExecutorTimeout = "SESSION_EXECUTOR_TIMEOUT";
inline constexpr const char *kPreflightBlocked = "PREFLIGHT_BLOCKED";
inline constexpr const char *kExecutionFailed = "EXECUTION_FAILED";
inline constexpr const char *kOutputInvalid = "OUTPUT_INVALID";
inline constexpr const char *kResourceOverBudget = "RESOURCE_OVER_BUDGET";
inline constexpr const char *kCancelled = "CANCELLED";
inline constexpr const char *kTeachingRefusal = "TEACHING_REFUSAL";
inline constexpr const char *kInvalidPlan = "INVALID_PLAN";
/// A resumed session was run with a goal other than the journalled one:
/// one session narrates one mission; a swapped goal must start a new one.
inline constexpr const char *kGoalMismatch = "SESSION_GOAL_MISMATCH";
} // namespace stop_reasons

/// Machine-readable error codes for state-machine misuse.
namespace error_codes {
inline constexpr const char *kIllegalTransition = "SESSION_ILLEGAL_TRANSITION";
inline constexpr const char *kAlreadyTerminal = "SESSION_ALREADY_TERMINAL";
inline constexpr const char *kUnknownStage = "SESSION_UNKNOWN_STAGE";
inline constexpr const char *kUnknownTerminalState = "SESSION_UNKNOWN_TERMINAL_STATE";
} // namespace error_codes

bool isKnownStage( const std::string &stage );
bool isKnownTerminalState( const std::string &state );
/// A journal entry stage may carry a stage or a terminal state (terminal
/// entries are stamped with the terminal state they reached).
bool isKnownStageOrTerminal( const std::string &stage );

/// The typed transition table: true when `from -> to` is a legal edge.
/// Terminal states are absorbing (nothing leaves them).
bool isLegalTransition( const std::string &from, const std::string &to );

struct TransitionError {
    std::string code;
    std::string message;
};

struct TransitionResult {
    bool ok = false;
    std::string from;
    std::string to;
    std::string stopReason; ///< only set by terminate()
    TransitionError error;
};

/// The session stage machine. Starts at goal_normalization; terminal is
/// absorbing; replan entries are counted (the budget owner reads this).
class SessionStageMachine {
  public:
    SessionStageMachine();

    std::string stage() const { return mStage; }
    bool terminal() const { return !mTerminal.empty(); }
    std::string terminalState() const { return mTerminal; }
    int replanCount() const { return mReplans; }

    /// Advances to another running stage. Typed error, no mutation, when
    /// the edge is illegal or the machine is already terminal.
    TransitionResult advance( const std::string &to );

    /// Terminates with a typed stop reason. Legal from any running stage.
    TransitionResult terminate( const std::string &terminalState,
                                const std::string &stopReason );

    /// Restarts the machine at `stage` (and the replay count) — used ONLY
    /// by session resume, which reconstructs the cursor from the persisted
    /// journal. Typed error when the stage is unknown or a terminal state.
    TransitionResult rewindTo( const std::string &stage, int replanCount = 0 );

  private:
    std::string mStage;
    std::string mTerminal;
    int mReplans = 0;
};

} // namespace sicnu::agent_loop
