// src/verify_adapters/checkpoint_state_view.h — real IStateView over a
// PipelineRunCoordinator checkpoint document (ADR 0172 provider seam,
// verify_context.h).
//
// The authority is the on-disk record the coordinator writes
// ("d17_pipeline_checkpoint", versions 1.0/1.1 — pipeline_run_coordinator.cpp):
// the view projects that record's own vocabulary and invents nothing.
//
//   key grammar                          projected value
//   ---------------------------------    -------------------------------
//   "run/id"                             string runId
//   "run/finished"                       bool
//   "run/success"                        bool
//   "run/attempt"                        integer
//   "node/<nodeId>/state"                checkpoint state, lower-cased:
//                                        "pending" "ready" "running"
//                                        "succeeded" "failed" "cancelled"
//                                        "skipped" — any other spelling
//                                        (a future vocabulary, a corrupt
//                                        document) projects as "unknown",
//                                        so no expectation of a known
//                                        state can read it as success.
//   "node/<nodeId>/artifact"             string artifact path (may be empty)
//   "node/<nodeId>/fingerprint"          string artifact fingerprint
//
// There is deliberately NO "refused" projection: refusal is a resume-time
// coordinator DECISION, not a state the checkpoint records, and a view that
// synthesizes it would fabricate authority. Callers needing the distinction
// read the typed status below and name it themselves.
//
// Fail-closed reading: a checkpoint that is missing, oversized, unparseable
// or of a foreign envelope loads as a typed status, and every state(key)
// then answers nullopt — the engine renders that as a loud non-pass, never
// a skip.
#pragma once

#include "verify/verify_context.h"

#include <json/json.h>

#include <optional>
#include <string>

namespace sicnu::verify_adapters
{

inline constexpr const char *kCheckpointKind = "d17_pipeline_checkpoint";
/// Closed version set the coordinator itself accepts (1.1 adds per-node
/// artifact identity; 1.0 is the legacy reader compatibility set).
inline constexpr const char *kCheckpointVersionCurrent = "1.1";
inline constexpr const char *kCheckpointVersionLegacy = "1.0";

enum class CheckpointReadStatus
{
    Ok,
    Missing,
    Unreadable,
    Oversized,
    ForeignEnvelope ///< wrong kind, or a version outside the closed set
};

struct CheckpointReadResult
{
    CheckpointReadStatus status = CheckpointReadStatus::Missing;
    Json::Value document; ///< the whole checkpoint document when status == Ok
    std::string detail;
};

/// Typed read + envelope gate of checkpoint_<runId>.json. Mirrors the
/// coordinator's own loader discipline: bounded read, kind gate, closed
/// version set. Never throws.
CheckpointReadResult readCheckpoint( const std::string &checkpointPath );

/// Lower-cased state vocabulary the view projects (see header table).
/// Exposed for spec authors and tests; "unknown" is the fail-closed bucket.
extern const char *const kCheckpointStateUnknown;

class CheckpointStateView final : public sicnu::verify::IStateView
{
  public:
    /// @p document must be a checkpoint document (status == Ok from
    /// readCheckpoint). A non-object document loads as foreign-envelope:
    /// every lookup answers nullopt.
    explicit CheckpointStateView( Json::Value document );

    std::optional<Json::Value> state( const std::string &key ) override;

  private:
    Json::Value mNodes;   ///< "nodes" array when well-formed, else null
    Json::Value mRun;     ///< run-level members (id/finished/success/attempt)
    bool mUsable = false;
};

} // namespace sicnu::verify_adapters
