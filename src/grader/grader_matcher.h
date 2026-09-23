// grader_matcher.h — evidence indexing and per-criterion judgment for the
// process-aware experiment grader (ADR 0174, slices B/C).
//
// The matcher consumes ONLY recorded evidence. Its judgments are pure
// functions of (rubric expectation, evidence bundle): no clock, no store, no
// invented curve. Every non-full outcome carries at least one machine
// reason slug (`grader:<slug>`, append-only vocabulary below) and cites the
// evidenceIds it was judged against — a lost point without a cited reason is
// a P0 defect.
//
// Reason vocabulary (append-only; consumers match on these spellings):
//   stage      grader:stage-missing | grader:stage-state-mismatch |
//              grader:stage-insufficient-distinct |
//              grader:prerequisite-stage-missing |
//              grader:stage-order-violated | grader:stage-order-unprovable
//   metric     grader:metric-missing | grader:metric-ambiguous |
//              grader:metric-below-band | grader:metric-above-band |
//              grader:metric-value-mismatch | grader:metric-out-of-range |
//              grader:metric-partial-window
//   fact       grader:fact-missing | grader:fact-state-mismatch |
//              grader:fact-missing-required-fact |
//              grader:fact-insufficient-count
//   answer     grader:answer-missing | grader:answer-ambiguous |
//              grader:concept-missing | grader:misconception-deduction |
//              grader:answer-points-clamped
//   constraints grader:constraint-cap | grader:constraint-zero
//
// Judgment semantics pinned here (and by tests/test_grader_engine.cpp):
//   * MISSING vs CONTRADICTED. No evidence for the matcher key at all → the
//     judgment cannot be made → `indeterminate`. Evidence exists but fails
//     the expectation → `not_earned`. Neither is ever a silent zero.
//   * AMBIGUITY. Multiple metric (or answer) items on one key with
//     conflicting observations → `indeterminate` citing every conflicting
//     item. Agreeing duplicates are one observation; citations list all
//     contributing items in evidenceId-sorted order (stable tie-break).
//   * SEQUENCING. `orderedAfter` is the only order the grader can see; it is
//     carried by evidence `recordedAtUtc` (ISO-8601 UTC, lexicographic =
//     chronological). A needed pair with an empty stamp cannot prove order →
//     `indeterminate` (never a silent violation); stamps present but
//     inverted → `not_earned`. Equal stamps satisfy the constraint (same-
//     time records cannot be distinguished, and the grader does not guess).
#pragma once

#include "grader_types.h"

#include <map>
#include <string>
#include <vector>

namespace sicnu::grader {

/// Indexed view of an evidence bundle. Items are bucketed by key and kept in
/// evidenceId-sorted order so every downstream judgment and citation order is
/// deterministic under arbitrary input permutations.
class EvidenceIndex
{
  public:
    explicit EvidenceIndex( const std::vector<GradeEvidenceItem> &items );

    /// All items (any kind) with this key, evidenceId-sorted.
    const std::vector<const GradeEvidenceItem *> &byKey( const std::string &key ) const;

    /// Stage-kind items with this key, evidenceId-sorted.
    std::vector<const GradeEvidenceItem *> stageItems( const std::string &key ) const;

  private:
    std::map<std::string, std::vector<const GradeEvidenceItem *>> m_byKey;
};

/// The outcome of judging one criterion (before constraint capping).
struct CriterionJudgment
{
    double rawEarned = 0.0;
    OutcomeStatus status = OutcomeStatus::NotEarned;
    std::vector<std::string> reasonCodes;
    std::vector<std::string> evidenceIds;
    std::string explanation;
};

namespace match {

/// Stage-kind items matching the stage expectation (key or accepted
/// alternatives, expected state). Distinctness is by evidenceId.
CriterionJudgment judgeStage( const Criterion &criterion, const EvidenceIndex &index,
                              const GradingRubric &rubric );

/// Metric-kind observation vs the tolerance band / linear window.
CriterionJudgment judgeMetric( const Criterion &criterion, const EvidenceIndex &index );

/// Fact-capable items subset-matching expectedState + requiredFacts.
CriterionJudgment judgeFact( const Criterion &criterion, const EvidenceIndex &index );

/// Deterministic keyword-boundary concept scoring + misconception deductions.
CriterionJudgment judgeAnswer( const Criterion &criterion, const EvidenceIndex &index );

/// Shared stage-satisfaction probe for requiredStages (state match +
/// declared ordering). Also used by judgeStage for orderedAfter targets.
StageOutcome judgeStageRequirement( const StageRequirement &requirement, const EvidenceIndex &index,
                                    const GradingRubric &rubric );

/// True when the item's facts contain every member of `required`
/// (canonical-JSON string comparison per member).
bool factsSatisfy( const GradeEvidenceItem &item,
                   const std::map<std::string, std::string> &required );

/// Shared gate for fact criteria and required-evidence hard constraints:
/// expectedState match (when declared) + requiredFacts subset.
bool factGate( const GradeEvidenceItem &item, const FactExpectation &fact );

/// True when the taxonomy kind can satisfy a fact criterion.
bool isFactCapable( EvidenceKind kind );

/// Word-boundary containment of @p needle in lowercased @p haystack.
bool containsKeyword( const std::string &haystack, const std::string &needle );

/// The grader-side text normalization: ASCII-lowercase, whitespace runs
/// collapsed to single spaces, trimmed. Deliberately locale-free.
std::string normalizeAnswerText( const std::string &text );

} // namespace match

} // namespace sicnu::grader
