/***************************************************************************
  lab/session_store.h
  Durable storage for LabSessions (ADR 0174).

  Layout: <root>/<labId>/<studentId>-<seq>.session.json, canonical bytes
  (see session_state). Persistence mirrors the WorkflowCheckpointManager
  discipline: unique per-save tmp name, fsync, std::filesystem::rename
  (rename(2) / MoveFileEx-REPLACE_EXISTING — atomic old-or-new, never a
  half-written session). Loads are fail-closed: corrupt payloads, foreign
  schema generations and spec-fingerprint drift are typed refusals, never
  silently adopted.
***************************************************************************/

#ifndef SICNU_LAB_SESSION_STORE_H
#define SICNU_LAB_SESSION_STORE_H

#include "session_state.h"

#include <string>
#include <vector>

namespace sicnu::lab
{

class LabSessionStore
{
  public:
    /// Store rooted at an explicit directory (tests inject a temp root).
    explicit LabSessionStore( std::string rootDir );

    /// SICNU_LAB_SESSION_DIR → <cwd>/.sicnu/lab/sessions.
    static std::string defaultRoot();

    /// Starts a new session for (labId, studentId) numbered after whatever is
    /// already in the store — deterministic restart numbering without a
    /// central counter.
    LabResult<LabSession> create( const LabRuntimePlan &plan, const SessionMeta &meta,
                                  const std::string &specFingerprint );

    /// Atomic canonical-bytes save. Same session saved twice yields
    /// byte-identical files.
    LabResult<> save( const LabSession &session );

    /// Loads a session and enforces the spec fingerprint: a lab spec that
    /// changed underneath a session is `lab.session.spec_drift`, never a
    /// silent mismatch. Empty `currentSpecFingerprint` skips the check.
    LabResult<LabSession> load( const std::string &sessionId,
                                const std::string &currentSpecFingerprint ) const;

    /// Loads without the fingerprint check (read-side projections).
    LabResult<LabSession> loadAny( const std::string &sessionId ) const;

    /// All session ids, lexicographically sorted.
    std::vector<std::string> listSessionIds() const;

    struct Summary
    {
      std::string sessionId;
      std::string labId;
      std::string studentId;
      std::string state;
      std::string planSource;
      long long lastSeq = 0;
    };

    struct Listing
    {
      std::vector<Summary> summaries;        // sorted by sessionId
      std::vector<LabDiag> warnings;         // unreadable files are skipped, not fatal
    };

    Listing listSessions() const;

  private:
    std::string sessionPath( const std::string &sessionId, std::vector<LabDiag> &diags ) const;

    std::string m_rootDir;
};

} // namespace sicnu::lab

#endif // SICNU_LAB_SESSION_STORE_H
