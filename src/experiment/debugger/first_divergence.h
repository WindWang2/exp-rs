// first_divergence.h — first-divergence localization over two run snapshots
// (RS14-06, ADR 0174). Slice C.
//
// The analyzer answers ONE question honestly: given a reference run and a
// student/agent run, WHERE did their recorded scientific processes first
// diverge, WHAT KIND of difference is it, and HOW CONFIDENT can we be that
// this difference caused the outcome divergence?
//
// Honesty contract:
//   - Verdicts are typed and closed: identical | equivalent | divergent |
//     incomplete | non_comparable. There is no "probably fine".
//   - Causal confidence is derived ONLY from evidence completeness (upstream
//     verification, digest presence and mode agreement, parameter
//     availability). It never claims certainty the evidence cannot back.
//   - Incomparable evidence (digest modes that cannot be mixed, missing
//     digests, absent step evidence) produces UnknownNonComparable findings
//     and named evidence gaps — never a guessed classification.
//   - The analyzer is a pure read-side projection: it executes nothing,
//     writes nothing, and never repairs or reinterprets recorded state.
#pragma once

#include "../../data/data_result.h"
#include "../experiment_types.h"
#include "run_snapshot.h"
#include "step_aligner.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::experiment::debugger
{

inline constexpr char kDivergenceSchemaKind[] = "exp.debugger.divergence.v1";
inline constexpr int kDefaultMaxDivergenceFindings = 64;

/// Closed divergence taxonomy (track contract; ADR 0174).
enum class DivergenceKind
{
    None,
    EquivalentAlternativePath,
    DifferentInputState,
    MissingPreprocessing,
    ParameterDivergence,
    DataSubsetDivergence,
    GeometryAlignmentDivergence,
    ResultDivergenceWithoutProcessDivergence,
    UnknownNonComparable,
};
QString divergenceKindName( DivergenceKind kind );

/// How strongly the evidence supports THIS difference as the cause of the
/// outcome divergence. Derived from evidence completeness only.
enum class CausalConfidence
{
    None,    ///< no evidence either way — the finding is a gap, not a guess
    Low,     ///< indirect evidence (e.g. lineage signature differs, parameters unavailable)
    Medium,  ///< direct evidence with partial upstream verification
    High,    ///< direct evidence with fully verified upstream identity
};
QString causalConfidenceName( CausalConfidence confidence );

struct DivergenceFinding
{
    DivergenceKind kind = DivergenceKind::None;
    /// Where: step ids on each side (run-level findings leave both empty).
    QString referenceStepId;
    QString studentStepId;
    CausalConfidence confidence = CausalConfidence::None;
    /// Typed evidence entries (dimension: value pairs, human-readable but
    /// structured; never free-form prose only).
    QStringList evidence;
    /// What evidence, if present, would raise the confidence.
    QStringList missingEvidence;
    /// Equivalence rule that accepted this difference (Slice D); empty here.
    QString equivalenceRuleId;

    QJsonObject toJson() const;
    bool operator==( const DivergenceFinding & ) const = default;
};

struct FirstDivergenceOptions
{
    int maxFindings = kDefaultMaxDivergenceFindings;
};

struct FirstDivergenceReport
{
    QString referenceRunId;
    QString studentRunId;
    /// identical | equivalent | divergent | incomplete | non_comparable
    QString verdict;
    bool hasFirstDivergence = false;
    DivergenceFinding firstDivergence;
    QVector<DivergenceFinding> additionalFindings;   ///< capped by options
    QJsonObject alignment;                            ///< AlignmentResult::toJson
    QJsonObject runLevelComparison;                   ///< RunComparison::toJson passthrough
    QStringList evidenceGaps;

    QJsonObject toJson() const;
    bool operator==( const FirstDivergenceReport & ) const = default;
};

class FirstDivergenceAnalyzer
{
  public:
    /// Pure analysis over one reference run and one student run. The
    /// ExperimentRun records power the run-level comparability gate
    /// (RunComparison — the existing single truth for pin comparability);
    /// the snapshots power the step-level walk. Callers obtain both from
    /// the same evidence source, so the two views cannot disagree.
    static sicnu::data::Result<FirstDivergenceReport> analyze(
        const ExperimentRun &referenceRun,
        const ExperimentRun &studentRun,
        const RunSnapshot &reference,
        const RunSnapshot &student,
        const FirstDivergenceOptions &options = {} );

};

} // namespace sicnu::experiment::debugger
