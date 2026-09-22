// src/agent_ops/live_session_recorder.h
#pragma once

//
// Feature A: Live Session Recorder.
// Append-only crash-safe observation over a SessionJournal, projecting to
// sicnu.agentbench.trace/v1. Bounded; no secrets; truncated payloads flagged;
// injected-fault markers preserved.
//

#include "agent_ops/ops_types.h"
#include "agent_loop/session_journal.h"
#include "agentbench/trace.h"

#include <cstddef>
#include <optional>
#include <string>

namespace sicnu::agent_ops {

class LiveSessionRecorder {
  public:
    struct Options {
        std::size_t maxPayloadChars = 4096;
        std::size_t maxTraceBytes = 512 * 1024;
        std::string agentName = "agent_ops";
        std::string agentVersion = "1.0";
    };

    LiveSessionRecorder() = default;
    explicit LiveSessionRecorder(Options options) : mOptions(std::move(options)) {}

    /// Project a journal into an AgentTrace. Fail-closed on empty session id.
    /// Does not mutate the journal. Returns nullopt when the projection would
    /// exceed maxTraceBytes after compaction (caller may still use compactProjection).
    std::optional<sicnu::agentbench::AgentTrace> projectTrace(
        const sicnu::agent_loop::SessionJournal &journal,
        const std::string &caseId = "live",
        std::string *error = nullptr) const;

    /// Persist journal via SessionJournal::save (atomic temp+rename).
    bool persistJournal(const sicnu::agent_loop::SessionJournal &journal,
                        const std::string &directory, std::string *error = nullptr) const;

    /// Load + validate; unknown/corrupt ≠ success.
    std::optional<sicnu::agent_loop::SessionJournal> loadJournal(
        const std::string &directory, const std::string &sessionId,
        std::string *error = nullptr) const;

    const Options &options() const { return mOptions; }

  private:
    Options mOptions;
};

} // namespace sicnu::agent_ops
