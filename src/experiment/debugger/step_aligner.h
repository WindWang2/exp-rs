// step_aligner.h — deterministic step matching between two run snapshots
// (RS14-06, ADR 0174). Slice B.
//
// Alignment answers "which reference step corresponds to which student
// step" — it is PROCESS matching, not verdict matching: a matched pair may
// still be the first divergence. Matching is deliberately conservative and
// honest:
//   - same plan signature → steps match by step id (one exact pass);
//   - different plans     → structural matching in topological order:
//       candidates share the operator id; a pair is accepted when their
//       already-matched parents correspond one-to-one (extra unmatched
//       parents are allowed on either side and reported via
//       parentCoverageComplete=false so upstream analysis can see a
//       missing/extra producer);
//   - step ids are labels: id equality never overrides operator mismatch,
//     and id inequality never blocks a structural match;
//   - content identity (equal output digest + mode) wins candidate choice —
//     two steps that produced byte-identical output are almost surely the
//     same scientific step;
//   - everything unmatched is listed verbatim; nothing is dropped or
//     invented, and the pass is deterministic (same evidence ⇒ same matches).
#pragma once

#include "../../data/data_result.h"
#include "run_snapshot.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::experiment::debugger
{

inline constexpr char kCodeAlignmentBudgetExceeded[] = "experiment.debugger.alignment_budget_exceeded";
inline constexpr int kDefaultMaxAlignmentComparisons = 4'000'000;

struct StepMatch
{
    enum class Kind
    {
        ExactId,        ///< same step id (same plan signature pass)
        Structural,     ///< operator + matched-parent correspondence
        EquivalentRule, ///< matched through a declared operator-group rule
    };
    Kind kind = Kind::Structural;
    QString referenceStepId;
    QString studentStepId;
    /// False when either side of the pair carries parents that have no
    /// counterpart on the other side (a producer appeared/disappeared) —
    /// visible input for missing-preprocessing analysis.
    bool parentCoverageComplete = true;
    /// Rule id of the operator-group equivalence that accepted this match
    /// (kind EquivalentRule); empty otherwise.
    QString equivalenceRuleId;

    QJsonObject toJson() const;
    bool operator==( const StepMatch & ) const = default;
};

struct AlignmentResult
{
    enum class PlanRelation
    {
        SamePlanSignature,
        DifferentPlan,
        InsufficientEvidence,   ///< plan signature missing on either side
    };

    PlanRelation planRelation = PlanRelation::InsufficientEvidence;
    QVector<StepMatch> matches;
    QStringList unmatchedReference;   ///< reference steps with no counterpart
    QStringList unmatchedStudent;     ///< student steps with no counterpart

    QJsonObject toJson() const;
    bool operator==( const AlignmentResult & ) const = default;
};

struct AlignmentBudget
{
    int maxComparisons = kDefaultMaxAlignmentComparisons;
};

class EquivalenceProfile;

class StepAligner
{
  public:
    /// Deterministic alignment of two snapshots' step graphs. Fails typed
    /// (kCodeAlignmentBudgetExceeded) when the candidate-comparison budget
    /// is exhausted — never degrades to a partial silent answer.
    static sicnu::data::Result<AlignmentResult> align( const RunSnapshot &reference,
                                                       const RunSnapshot &student,
                                                       const AlignmentBudget &budget = {} );

    /// Profile-aware overload: candidates whose operator differs may still
    /// match through a declared operator-group rule; such matches carry
    /// kind EquivalentRule and the accepting rule id.
    static sicnu::data::Result<AlignmentResult> align( const RunSnapshot &reference,
                                                       const RunSnapshot &student,
                                                       const EquivalenceProfile &profile,
                                                       const AlignmentBudget &budget = {} );
};

} // namespace sicnu::experiment::debugger
