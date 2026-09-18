// src/agent/harness/context_checkpoint.h
#pragma once

//
// Scientific Workflow Compiler 10.0 (ADR 0149): harness-side long-task
// context — checkpoint / resume / compaction / stale invalidation.
//
// The engine checkpoints RUNS (ADR 0123). This module checkpoints the
// compiler CONTEXT around runs: the normalized WorkflowIR, its analysis,
// repair records and refusals, the plan binding, typed decisions, failed
// repair attempts, and the stage cursor of the planner pipeline. Everything
// here is authoritative state the caller passes in — never chat memory.
//
// Persistence follows the engine's own convention (WorkflowCheckpointManager):
// one JSON document per session under ~/.rs_studio/harness_sessions/
// (override: SICNU_HARNESS_SESSION_DIR, or setDirectory for tests), atomic
// writes, bounded store (oldest sessions evict first).
//
// Stale invalidation: at save time every input slot's fact identity
// (path, size, mtime, revision) is recorded. resume re-stats the files;
// a slot whose identity changed is stale — the facts predate the current
// bytes — and the stage cursor rewinds to grounding for a re-ground, while
// decisions and attempt history survive (they are not facts).
//

#include <json/json.h>
#include <optional>
#include <QString>
#include <string>

#include "harness_error.h"

namespace sicnu::agent::harness {

inline constexpr const char *kHarnessSessionSchemaVersion = "1.0";

/// Closed stage-cursor vocabulary (planner + loop stages).
namespace session_stages {
inline constexpr const char *kIntent = "intent";
inline constexpr const char *kGrounding = "grounding";
inline constexpr const char *kCandidates = "candidates";
inline constexpr const char *kIr = "ir";
inline constexpr const char *kAnalysis = "analysis";
inline constexpr const char *kRepair = "repair";
inline constexpr const char *kLower = "lower";
inline constexpr const char *kExecuting = "executing";
inline constexpr const char *kVerifying = "verifying";
inline constexpr const char *kDone = "done";
} // namespace session_stages

bool isKnownSessionStage( const std::string &stage );

/// True when @p sessionId is filename-safe: 1..128 characters drawn from
/// [A-Za-z0-9_.-]. Enforced uniformly on EVERY operation that derives a
/// filesystem path from the id (#1056) — save validated it, load/delete/
/// resume/staleness did not, so a crafted id could read or unlink outside
/// the store directory.
bool isFilenameSafeSessionId( const std::string &sessionId );

/// One harness session: the resumable compiler context.
struct HarnessSessionState
{
    std::string sessionId;
    std::string schemaVersion = kHarnessSessionSchemaVersion;
    std::string savedAt;
    std::string stageCursor = session_stages::kIntent;
    std::string goal;
    std::string intent;
    Json::Value ir{Json::Value()};            ///< normalized WorkflowIR document (or null)
    Json::Value analysis{Json::Value()};      ///< last IrAnalysis document (or null)
    Json::Value repairs{Json::arrayValue};    ///< IrRepairRecord documents
    Json::Value refusals{Json::arrayValue};   ///< IrRefusal documents
    Json::Value planBinding{Json::Value()};   ///< {run_id, plan_id, plan_fingerprint, ...}
    Json::Value decisions{Json::arrayValue};  ///< typed decision rows (ContextLedger shape)
    Json::Value failedAttempts{Json::arrayValue}; ///< typed attempt rows
    Json::Value factIdentities{Json::objectValue}; ///< slot -> {path, size, mtime, revision}

    Json::Value toJson() const;
    static std::optional<HarnessSessionState> fromJson( const Json::Value &doc,
                                                        std::string *error = nullptr );
    /// Approximate token cost of the bounded document (~bytes/4, the run
    /// summary convention).
    int approxTokens() const;
};

/// File-backed store. All methods are thread-safe and deterministic.
class HarnessSessionStore
{
  public:
    static HarnessSessionStore &instance();

    /// Directory override (tests); empty restores the default
    /// ($SICNU_HARNESS_SESSION_DIR, ~/.rs_studio/harness_sessions).
    void setDirectory( const QString &directory );
    QString directory() const;

    /// Saves atomically. Evicts the oldest session beyond the store bound.
    /// Oversized documents are compacted first (documented projection).
    /// Returns the written path; typed error on failure.
    QString saveSession( const HarnessSessionState &state, HarnessError &error );

    /// Loads a session (fail-closed on corrupt or foreign-version documents).
    std::optional<HarnessSessionState> loadSession( const std::string &sessionId,
                                                    HarnessError &error ) const;

    /// Bounded listing: [{session_id, saved_at, stage_cursor, goal, intent,
    /// approx_tokens, bytes}] oldest first.
    Json::Value listSessions() const;

    /// Deletes the session checkpoint. False (with a typed @p error when
    /// provided) for an invalid session id or when no such session exists.
    bool deleteSession( const std::string &sessionId, HarnessError *error = nullptr );

    /// Per-slot staleness of the SAVED fact identities against the current
    /// files: {slot: {stale, reason, saved {..}, current {..}}}. Slots with
    /// no identity are reported stale with reason "no_identity".
    Json::Value stalenessReport( const HarnessSessionState &state ) const;

    /// Resume outcome: the loaded state plus which slots went stale and the
    /// stage the pipeline must rewind to ("grounding" when anything is stale,
    /// else the saved cursor).
    struct ResumeResult
    {
        HarnessSessionState state;
        Json::Value staleSlots{Json::objectValue};
        std::string rewindToStage;
    };

    std::optional<ResumeResult> resumeSession( const std::string &sessionId,
                                               HarnessError &error ) const;

    /// The bounded compaction projection: drops analysis.checks details and
    /// per-slot fact bodies, keeps IR, verdicts, repair/refusal records,
    /// decisions, attempts, binding. Same input -> same output.
    static Json::Value compactState( const HarnessSessionState &state );

    /// Store bounds (drift anchor for tests/docs).
    static constexpr int kMaxSessions = 8;
    static constexpr long kMaxDocumentBytes = 64 * 1024;

  private:
    HarnessSessionStore() = default;
    QString defaultDirectory() const;
    /// Locked variants — callers hold gStoreMutex; these never lock
    /// (the public accessors do, and the mutex is non-recursive).
    QString directoryLocked() const;
    QString sessionPathLocked( const std::string &sessionId ) const;

    QString mDirectory;
};

/// Registers `harness:workflow_session` (save/resume/list/delete/staleness)
/// on the SpatialToolRegistry. Idempotent.
void registerContextSessionTools();

} // namespace sicnu::agent::harness
