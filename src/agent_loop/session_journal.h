// src/agent_loop/session_journal.h
#pragma once

//
// RS14-11 Evidence-first Agent Loop: the replayable session journal.
//
// The journal is the session's audit trail: an append-only, bounded log of
// stage transitions, decisions (typed DecisionRecords) and payloads. It
// follows the persistence conventions of the harness session store
// (src/agent/harness/context_checkpoint.h): one versioned JSON document
// per session, atomic write (temp + rename), bounded size, fail-closed
// reads — but implemented in plain C++ so the core stays Qt-free.
//
// Replay reconstructs the session history from the journal ALONE: no seam
// is re-invoked, no work is re-executed. A replayed session therefore
// cannot lie about what happened and cannot re-run science.
//

#include "decision_record.h"
#include "session_state.h"

#include <json/json.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::agent_loop {

inline constexpr const char *kSessionJournalSchemaVersion = "1.0";

struct JournalEntry {
    long long seq = 0;
    long long at = 0;
    std::string stage;   ///< a known stage, or the terminal state for "terminal" entries
    std::string event;   ///< "stage_enter" | "decision" | "note" | "terminal" | ...
    Json::Value payload{ Json::objectValue };
    std::optional< DecisionRecord > decision;
};

class SessionJournal {
  public:
    static constexpr std::size_t kMaxEntries = 4096;
    static constexpr long kMaxDocumentBytes = 1024 * 1024;

    SessionJournal() = default;
    explicit SessionJournal( std::string sessionId,
                             std::size_t maxEntries = kMaxEntries );

    const std::string &sessionId() const { return mSessionId; }
    const std::vector< JournalEntry > &entries() const { return mEntries; }
    std::size_t size() const { return mEntries.size(); }
    long long nextSeq() const { return mNextSeq; }

    /// Appends one entry. Returns false (and appends nothing) when the
    /// event is empty or the stage is outside the known vocabulary.
    bool append( const std::string &event, const std::string &stage, Json::Value payload,
                 long long at, std::optional< DecisionRecord > decision = std::nullopt );

    Json::Value toJson() const;

    /// Fail-closed reader (unknown schema_version, foreign stages,
    /// non-monotonic seq, malformed decision records are all errors).
    static std::optional< SessionJournal > fromJson( const Json::Value &doc,
                                                     std::string *error = nullptr );

    /// Session ids become file names: only a conservative charset is safe.
    static bool isSafeSessionId( const std::string &sessionId );

    /// Atomic persist under `directory` as `<session_id>.json` (temp +
    /// rename). Documents larger than kMaxDocumentBytes are saved through
    /// the deterministic compaction projection instead. Returns false with
    /// a message in `error` on any failure.
    bool save( const std::string &directory, std::string *error = nullptr ) const;

    static std::optional< SessionJournal > load( const std::string &directory,
                                                 const std::string &sessionId,
                                                 std::string *error = nullptr );

    /// The bounded compaction projection: payload bodies are replaced by
    /// {compacted, original_bytes}; the envelope and every decision record
    /// (the evidence) are preserved. Same input -> same output.
    Json::Value compactProjection() const;

    struct ReplayResult {
        std::vector< JournalEntry > entries;
        std::string finalStage;
        std::string terminalState; ///< empty while the session is running
        std::string stopReason;    ///< typed stop reason of the terminal entry
        int replanCount = 0;
    };

    ReplayResult replay() const;

  private:
    std::string mSessionId;
    std::vector< JournalEntry > mEntries;
    long long mNextSeq = 1;
    std::size_t mMaxEntries;
};

} // namespace sicnu::agent_loop
