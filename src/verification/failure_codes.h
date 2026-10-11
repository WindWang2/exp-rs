/***************************************************************************
  failure_codes.h — closed, machine-readable failure vocabulary (Slice A)

  Why a SEPARATE table instead of adding to `harness::error_codes`
  (src/agent/harness/harness_error.h):

    1. Different semantics. Harness codes describe *what went wrong while
       running the plan* (dataset not found, CRS mismatch, tool not found).
       These describe *what the verification concluded about a result*
       (expected vs observed mismatch, evidence unavailable, budget exceeded).
       Merging them would make every agent consumer guess which half applies.
    2. Adding a harness code is a nine-place registration (its table, its
       census/drift scanners, mirrors, docs...). This track must not touch
       those files — see the no-scope-expansion rule in BASELINE.md.
    3. A result-verification code needs a REPLAN CLASS, not just a retry
       class: "evidence unavailable" wants more evidence (Retry), while
       "state invariant violated" requires a different plan (Replan), and
       "spec invalid" is a caller bug that must stop the loop (Abort).

  The table is closed and introspectable (`allFailureCodes()`), mirroring the
  harness discipline that any surface listing the vocabulary must derive from
  the table rather than keep its own copy. A single `toHarnessCode()` mapping
  is provided so existing harness surfaces can later display these codes
  without guessing.
 ***************************************************************************/
#pragma once

#include <string>
#include <vector>

namespace sicnu::verification
{

/// What a caller should DO about a failure. Distinct from RetryClass: it also
/// covers the cases where retrying is actively wrong.
enum class ReplanClass
{
    None,   ///< Nothing to do (informational only).
    Retry,  ///< Re-run the same plan after supplying the missing input/evidence.
    Replan, ///< The plan itself is wrong; produce a different one.
    Abort,  ///< Caller-side defect (bad spec); stop rather than loop.
};

namespace failure_codes
{
// Wire strings are frozen once published: consumers compare them literally.
inline constexpr const char *kSpecInvalid = "VERIFY.SPEC_INVALID";
inline constexpr const char *kUnsupportedCheckKind = "VERIFY.UNSUPPORTED_CHECK_KIND";
inline constexpr const char *kNoChecks = "VERIFY.NO_CHECKS";
inline constexpr const char *kEvidenceUnavailable = "VERIFY.EVIDENCE_UNAVAILABLE";
inline constexpr const char *kEvidenceRefused = "VERIFY.EVIDENCE_REFUSED";
inline constexpr const char *kStateTokenMissing = "VERIFY.STATE_TOKEN_MISSING";
inline constexpr const char *kStateInvariantViolation = "VERIFY.STATE_INVARIANT_VIOLATION";
inline constexpr const char *kArtifactMissing = "VERIFY.ARTIFACT_MISSING";
inline constexpr const char *kArtifactKindMismatch = "VERIFY.ARTIFACT_KIND_MISMATCH";
inline constexpr const char *kArtifactGridMismatch = "VERIFY.ARTIFACT_GRID_MISMATCH";
inline constexpr const char *kArtifactSchemaMismatch = "VERIFY.ARTIFACT_SCHEMA_MISMATCH";
inline constexpr const char *kNumericOutOfRange = "VERIFY.NUMERIC_OUT_OF_RANGE";
inline constexpr const char *kNumericNotFinite = "VERIFY.NUMERIC_NOT_FINITE";
inline constexpr const char *kRelationInconsistent = "VERIFY.RELATION_INCONSISTENT";
inline constexpr const char *kProvenanceIncomplete = "VERIFY.PROVENANCE_INCOMPLETE";
inline constexpr const char *kReproducibilityDigestMismatch = "VERIFY.REPRODUCIBILITY_DIGEST_MISMATCH";
inline constexpr const char *kCrossOutputInconsistent = "VERIFY.CROSS_OUTPUT_INCONSISTENT";
inline constexpr const char *kBudgetExceeded = "VERIFY.BUDGET_EXCEEDED";
} // namespace failure_codes

/// Every code in the closed table, sorted and de-duplicated. Let no surface
/// keep its own copy of this list.
std::vector<std::string> allFailureCodes();

/// True when @p code is part of the table. Unknown codes must never be treated
/// as a passing result; callers are expected to fail visibly.
bool isKnownFailureCode( const std::string &code );

/// Category: "validation" | "io" | "resource" | "runtime" (last one is the
/// conservative fallback for unknown codes — never empty, never "none").
std::string failureCategoryForCode( const std::string &code );

/// What the caller should do. Unknown codes fall back to Replan: the safest
/// non-pass answer, and visibly wrong rather than silently ignored.
ReplanClass replanClassForCode( const std::string &code );

const char *replanClassToWire( ReplanClass replanClass );

/// Human-readable explanation template for teaching surfaces. Never empty.
std::string failureHintForCode( const std::string &code );

/// Best-effort projection onto harness's existing code vocabulary so existing
/// harness surfaces can display a verification failure without inventing a
/// mapping of their own. Returns the closest harness code, or empty when there
/// is no honest counterpart.
std::string toHarnessCode( const std::string &code );

} // namespace sicnu::verification
