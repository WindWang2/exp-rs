/***************************************************************************
  lab/spec_runtime.h
  LabSpec v3 (`spec_version: 3`) — the runtime-capable LabSpec generation.

  ADR 0174. LabSpec 2 (ADR 0146) owns static teaching content; spec_version 3
  adds ONE optional top-level `runtime` block that makes a lab
  machine-executable as a teaching session: stages (grouped step ranges with
  per-stage objectives and an optional allowed-tools whitelist), machine-
  verifiable checkpoints (artifact_present / operator_invoked /
  question_answered), structured reflection questions, a hint policy
  (budgeted, escalating) and reproducibility requirements (seed,
  deterministic-only declaration).

  Versioning: spec_version is append-only; master consumed "2" for its
  static-content superset, so the runtime generation is 3. v3 is a strict
  superset of v2 — the block is optional, every diagnostic is typed
  (`lab.runtime.*`), and parsing never throws.

  This module is Qt-free (jsoncpp + std only). The layer guard is the test
  link graph: tests/test_lab_runtime links no Qt.
***************************************************************************/

#ifndef SICNU_LAB_SPEC_RUNTIME_H
#define SICNU_LAB_SPEC_RUNTIME_H

#include <json/json.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sicnu::lab
{

/// Typed diagnostic; `code` is a stable, dotted lowercase vocabulary entry
/// (append-only; mirrors the sicnu::data::Diagnostic discipline without its
/// Qt dependency). `message` carries a locator such as
/// `runtime.stages[0].step_indices[2]`.
struct LabDiag
{
  std::string code;
  std::string message;
};

/// Value-semantic result: `ok` mirrors sicnu::data::Result's operator bool;
/// failures carry one or more diagnostics, never a fallback value.
template <typename T>
struct LabResult
{
  bool ok = false;
  T value{};
  std::vector<LabDiag> diagnostics;

  static LabResult success( T v ) { return LabResult{ true, std::move( v ), {} }; }
  static LabResult failure( std::vector<LabDiag> diags )
  {
    return LabResult{ false, {}, std::move( diags ) };
  }
};

enum class CheckKind
{
  ArtifactPresent,
  OperatorInvoked,
  QuestionAnswered,
};

enum class AdvancePolicy
{
  Observe, // record-only; never constrains the student (default)
  Gate,    // stage reports blocked_advance until the checkpoint passes
};

struct CheckpointCheck
{
  CheckKind kind = CheckKind::ArtifactPresent;
  // artifact_present
  std::string path;
  long long minBytes = 0;
  std::string sha256; // 64 lowercase hex; empty = not pinned
  // operator_invoked (exactly one of operatorId / operatorIdAny)
  std::string operatorId;
  std::vector<std::string> operatorIdAny;
  std::map<std::string, std::string> paramsSubset; // scalar values, stringified
  // question_answered
  std::string questionId;
};

struct Checkpoint
{
  std::string id;
  std::string title;
  std::string titleZh;
  std::vector<CheckpointCheck> checks;
  int attemptsAllowed = 0; // 0 = unlimited
  AdvancePolicy advance = AdvancePolicy::Observe;
};

struct Stage
{
  std::string id;
  std::string title;
  std::string titleZh;
  std::string objective;
  std::string objectiveZh;
  std::vector<int> stepIndices; // strictly ascending, in-range, disjoint across stages
  std::vector<std::string> allowedTools; // exact id or trailing-'*' prefix; empty = unrestricted
  std::vector<Checkpoint> checkpoints;
};

enum class QuestionKind
{
  FreeText,
  Numeric,
  Choice,
};

struct Question
{
  std::string id;
  std::string prompt;
  std::string promptZh;
  QuestionKind kind = QuestionKind::FreeText;
  std::vector<std::string> choices; // Choice only
  bool hasExpectedNumeric = false;  // Numeric only
  double expectedMin = 0.0;
  double expectedMax = 0.0;
};

struct HintEntry
{
  int level = 1; // 1-based index into Hints::Policy::escalation
  std::string text;
  std::string textZh;
  bool hasStep = false;
  int targetStep = 0;
  bool hasCheckpoint = false;
  std::string targetCheckpoint;
};

struct Hints
{
  struct Policy
  {
    int maxRevealsPerTarget = 0; // 0 = unlimited
    std::vector<std::string> escalation{ "hint" };
  };
  Policy policy;
  std::vector<HintEntry> entries;
};

struct Reproducibility
{
  bool requireSeed = false;
  bool deterministicOperatorsOnly = false;
  std::string notes;
};

struct LabRuntimePlan
{
  std::vector<std::string> dataPacks;
  std::vector<Stage> stages;
  std::vector<Question> questions;
  Hints hints;
  Reproducibility reproducibility;
};

/// True when the lab document carries a `runtime` block (v3 capability).
bool hasRuntimeBlock( const Json::Value &labDoc );

/// Parses and validates the `runtime` block of a lab document against the
/// enclosing document (steps array bounds). Collects every violation; an
/// empty diagnostic list means the returned plan is complete and consistent.
LabResult<LabRuntimePlan> parseRuntimeBlock( const Json::Value &labDoc );

/// SHA-256 of the raw spec file bytes, 64 lowercase hex. The lab-session
/// layer pins this fingerprint to detect spec drift on resume.
std::string specFingerprint( std::string_view specBytes );

} // namespace sicnu::lab

#endif // SICNU_LAB_SPEC_RUNTIME_H
