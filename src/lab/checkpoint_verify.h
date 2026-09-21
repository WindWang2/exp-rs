/***************************************************************************
  lab/checkpoint_verify.h
  Machine-verifiable checkpoint contracts (ADR 0174).

  Verification is deliberately lightweight and honest: it checks the
  session's own recorded history (tool choices, answers) and the presence /
  pinned content of observable artifacts through an injected FileProbe. It is
  NOT the grading engine (grading stays with sicnu.lab.rules + OutputVerifier)
  and never inspects raster semantics.

  Every check produces evidence {observed, expected} in the grader tradition
  — failures carry both sides, unverifiable conditions (over-budget reads,
  exhausted attempt budgets) are typed, never silently skipped.
***************************************************************************/

#ifndef SICNU_LAB_CHECKPOINT_VERIFY_H
#define SICNU_LAB_CHECKPOINT_VERIFY_H

#include "session_state.h"

namespace sicnu::lab
{

/// Read-only view of the project filesystem, injected so the runtime stays
/// testable and sandboxed (checkpoint paths were validated as safe-relative
/// at spec parse time).
class FileProbe
{
  public:
    virtual ~FileProbe() = default;
    virtual bool exists( const std::string &relPath ) = 0;
    virtual long long fileSize( const std::string &relPath ) = 0;
    /// Reads the file into `out`; false when absent/unreadable/over budget.
    virtual bool readFile( const std::string &relPath, std::string &out,
                           long long budgetBytes ) = 0;
};

/// Upper bound for one artifact digest read: checkpoint verification is a
/// metadata-layer operation and must never turn into an unbounded IO job.
inline constexpr long long kDefaultArtifactReadBudget = 64LL * 1024 * 1024;

/// Verifies one checkpoint against the session's recorded history, appends
/// the CheckpointResult (monotonic seq, attempt counter) and recomputes the
/// owning stage's advisory status: a gate checkpoint whose latest verdict is
/// not Pass puts the stage into blocked_advance; a stage whose checkpoints
/// all pass becomes advanced. Observe checkpoints never constrain anything.
///
/// Refusals: unknown checkpoint (`lab.session.unknown_checkpoint`),
/// non-active session (`lab.session.bad_transition`), invalid level
/// (`lab.session.field` — not produced here, reserved).
LabResult<CheckpointResult> verifyCheckpoint( LabSession &session, const LabRuntimePlan &plan,
                                              const std::string &checkpointId, FileProbe &probe,
                                              long long readBudget = kDefaultArtifactReadBudget );

} // namespace sicnu::lab

#endif // SICNU_LAB_CHECKPOINT_VERIFY_H
