/***************************************************************************
  lab/session_state.h
  LabRuntimeSession — the student lab-session value model (ADR 0174).

  One LabSession records WHO runs WHICH lab HOW FAR: stage statuses, per-
  checkpoint attempts and verdicts with evidence, question answers, hint
  reveals, tool choices (free exploration is recorded, never blocked) and
  optional execution references (experiment run ids — provenance stays with
  the provenance systems; the session only points at it).

  Determinism contract: the canonical JSON form is byte-stable (jsoncpp's
  key-sorted objects + no timestamps; ordering comes from the monotonic
  `seq` counter). All mutations are validated transitions with typed
  `lab.session.*` refusals — no silent fallback, no zombie edits on a
  terminal session.
***************************************************************************/

#ifndef SICNU_LAB_SESSION_STATE_H
#define SICNU_LAB_SESSION_STATE_H

#include "spec_runtime.h"

#include <optional>

namespace sicnu::lab
{

inline constexpr const char *kSessionSchemaId = "sicnu.lab-session/1";

enum class SessionState
{
  Active,
  Completed,
  Abandoned,
};

enum class StageStatus
{
  Active,
  Advanced,        // every checkpoint of the stage passed
  BlockedAdvance,  // a gate checkpoint of the stage has not passed (advisory only)
};

enum class Verdict
{
  Pass,
  Fail,
  Unverifiable,
};

struct CheckEvidence
{
  int checkIndex = 0;
  bool ok = false;
  std::string observed;
  std::string expected;
};

struct CheckpointResult
{
  std::string checkpointId;
  Verdict verdict = Verdict::Fail;
  int attempt = 0;
  std::vector<CheckEvidence> evidence;
  long long seq = 0;
};

struct QuestionAnswer
{
  std::string questionId;
  std::string answer;
  bool hasNumeric = false;
  double numeric = 0.0;
  long long seq = 0;
};

struct HintEvent
{
  std::string target; // "step:<n>" or "checkpoint:<id>"
  int level = 1;
  long long seq = 0;
};

struct ToolChoice
{
  std::string stageId;
  std::string operatorId;
  std::map<std::string, std::string> paramsSubset;
  bool allowed = true; // recorded honestly; an out-of-whitelist use is data, not an error
  long long seq = 0;   // assigned by recordToolUse from the ledger; caller values are ignored
};

struct ExecutionRef
{
  std::string kind; // closed vocabulary, e.g. "experiment_run"
  std::string id;
  long long seq = 0;
};

struct SessionStage
{
  std::string stageId;
  StageStatus status = StageStatus::Active;
};

struct SessionMeta
{
  std::string labId;
  std::string studentId;
  int labSpecVersion = 3;
  std::string planSource; // authored_v3 | derived_from_v2 | derived_from_v1
  bool hasSeed = false;
  long long seed = 0;
};

struct LabSession
{
  std::string schemaId = kSessionSchemaId;
  std::string sessionId; // "<labId>/<studentId>/<seq>"
  std::string labId;
  std::string labSpecFingerprint;
  int labSpecVersion = 3;
  std::string planSource;
  std::string studentId;
  bool hasSeed = false;
  long long seed = 0;
  SessionState state = SessionState::Active;
  std::vector<SessionStage> stages;
  std::vector<CheckpointResult> checkpointResults;
  std::vector<QuestionAnswer> questionAnswers;
  std::vector<HintEvent> hintEvents;
  std::vector<ToolChoice> toolChoices;
  std::vector<ExecutionRef> executionRefs;
  long long lastSeq = 0;
};

/// Monotonic sequence number for the next recorded event (1-based; 0 means
/// "no events yet").
long long nextSeq( const LabSession &session );

/// Starts a session for (labId, studentId) at the given per-student sequence
/// number (1-based; the store derives it deterministically from the store
/// contents). Refusals: malformed identity (`lab.session.field`), unknown
/// plan source (`lab.session.field`), missing required seed
/// (`lab.session.seed_required`).
LabResult<LabSession> startSession( const LabRuntimePlan &plan, const SessionMeta &meta,
                                    long long seq );

/// Active → Completed. Refused (`lab.session.incomplete`, one diagnostic per
/// unmet requirement) unless every gate checkpoint's latest result passes and
/// every plan question is answered; refused with `lab.session.bad_transition`
/// when the session is not Active.
LabResult<> completeSession( LabSession &session, const LabRuntimePlan &plan );

/// Active → Abandoned; anything else is `lab.session.bad_transition`.
LabResult<> abandonSession( LabSession &session );

/// Abandoned → Active (a student may pick a lab back up); anything else is
/// `lab.session.bad_transition`.
LabResult<> reopenSession( LabSession &session );

/// Records a tool use in a stage. The stage must exist
/// (`lab.session.unknown_stage`). Whitespace-level params are matched as
/// strings; `allowed` is computed from the stage whitelist and recorded
/// honestly — out-of-whitelist exploration is data, never a refusal.
LabResult<> recordToolUse( LabSession &session, const LabRuntimePlan &plan, ToolChoice choice );

/// Records an answer to a plan question. Unknown question →
/// `lab.session.answer_unknown_question`; choice answers must be one of the
/// declared choices and numeric questions need a numeric payload
/// (`lab.session.field`). Re-answering appends (latest wins).
LabResult<> recordAnswer( LabSession &session, const LabRuntimePlan &plan,
                          const std::string &questionId, const std::string &answerText,
                          std::optional<double> numeric );

/// Records a pointer at an external execution record (e.g. an experiment run
/// id recorded by the existing bridges). No provenance is copied here.
LabResult<> recordExecutionRef( LabSession &session, const std::string &kind,
                                const std::string &id );

/// Canonical JSON: key-sorted compact object, no timestamps — byte-stable
/// across processes and platforms. Field vocabulary is the schema of record
/// for `sicnu.lab-session/1`.
Json::Value sessionToJson( const LabSession &session );

std::string sessionToCanonicalBytes( const LabSession &session );

/// Strict envelope parser: refuses unknown keys, wrong types, unknown
/// vocabulary values (`lab.session.schema`) and future/foreign schema
/// generations (`lab.session.version`). Never adopts partially.
LabResult<LabSession> sessionFromJson( const Json::Value &doc );

} // namespace sicnu::lab

#endif // SICNU_LAB_SESSION_STATE_H
