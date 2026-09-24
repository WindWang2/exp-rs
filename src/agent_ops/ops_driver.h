// src/agent_ops/ops_driver.h
#pragma once

//
// OpsDriver — the thin production driver over OperationsCoordinator +
// sessionSurfaceApply (the de-facto session wire contract).
//
// What it adds over the bare surface:
//
//   * per-session bookkeeping: status/timeline/export may name a session
//     (default: the most recent one) instead of only seeing whatever the
//     coordinator ran last — one driver serving several sessions can no
//     longer return another session's data;
//   * `reconcile`: inspect a journal WITHOUT resuming (typed verdict incl.
//     duplicate-submit hazard and submitted run ids), so a restart is a
//     decision, not a gamble;
//   * a seam-completeness gate: run/resume refuse with a typed code BEFORE
//     the loop starts when the injected seams cannot serve the requested
//     mode. A host without production seams fails closed instead of
//     running fake science through the loop;
//   * the cancel/pause disarm actions (clear_pause / clear_cancel) on the
//     wire, so a driver can arm the coordinator for new work after an
//     observed cancel.
//
// The driver invents no state machine and no second scheduler: every
// control flows through the surface onto OperationsCoordinator, and the
// loop stays the only state machine.
//

#include "agent_ops/live_session_recorder.h"
#include "agent_ops/operations_coordinator.h"
#include "agent_ops/resume_reconciler.h"

#include <json/json.h>

#include <deque>
#include <map>
#include <string>

namespace sicnu::agent_ops {

inline constexpr const char *kOpsDriverSchema = "sicnu.agent_ops.ops_driver/v1";

class OpsDriver {
  public:
    struct Options {
        OperationsCoordinator::Dependencies deps;
        LiveSessionRecorder::Options recorder;
    };

    explicit OpsDriver(Options options);

    /// Bounded session bookkeeping: when more sessions are tracked, the
    /// oldest is evicted (the journal directory remains the durable record).
    static constexpr std::size_t kMaxTrackedSessions = 128;

    /// Apply a named action. Typed failure documents (ok=false + machine
    /// reason) on missing args / unknown sessions / unavailable seams —
    /// never silent success. Wrong-typed arguments come back as INVALID_ARGS
    /// (never a Json::LogicError escaping the wire contract). Run/resume
    /// results are remembered per session.
    Json::Value apply(const std::string &action,
                      const Json::Value &args = Json::Value(Json::objectValue));

    /// Discovery document (the surface's advertised actions + reconcile).
    Json::Value actions() const;

    /// Direct coordinator access for hosts that compose further projections.
    OperationsCoordinator &coordinator() { return mCoordinator; }

  private:
    Json::Value applyChecked(const std::string &action, const Json::Value &args);

    const OpsRunResult *findResult(const std::string &sessionId) const;
    void remember(const OpsRunResult &result);
    /// Typed SEAMS_UNAVAILABLE doc when the injected seams cannot serve the
    /// requested run mode (missing names listed; nothing is launched).
    std::string missingSeamsFor(sicnu::agent_loop::RunMode mode) const;
    Json::Value statusFor(const OpsRunResult &result, const std::string &action) const;

    OperationsCoordinator mCoordinator;
    ResumeReconciler mReconciler;
    std::map<std::string, OpsRunResult> mSessions;
    std::deque<std::string> mOrder; ///< insertion order for bounded eviction
    std::string mLastSessionId;
};

} // namespace sicnu::agent_ops
