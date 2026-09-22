// grader_types.h — the three versioned value documents of the process-aware
// experiment grader (ADR 0174):
//
//   * GradingRubric  — `sicnu.grader.rubric/1`   (teacher authority)
//   * GradeEvidence  — `sicnu.grader.evidence/1` (projected facts)
//   * GradeReport    — `sicnu.grader.report/1`   (deterministic verdict)
//
// Design contracts (ADR 0174, normative):
//
//   EVIDENCE, NOT BUTTONS. The grader consumes recorded facts (workflow
//   states, metric values, artifact digests, student answers). There is no
//   representation of UI action order anywhere in the schema; a rubric may
//   declare `orderedAfter` between STAGES, and that is the only sequencing
//   the grader can ever see.
//
//   REASON CHAIN. Every criterion that did not earn full points carries at
//   least one machine-readable reason slug (`grader:<slug>`, append-only)
//   plus the evidenceIds it was judged against. A lost point without a
//   cited reason is a P0 defect (inherited from ADR 0150's evidence-less
//   deduction rule).
//
//   PARTIAL CREDIT, TEACHER-BOUNDED. Tolerance bands and linear windows are
//   declared in the rubric; the grader never invents a curve.
//
//   FAIL CLOSED. Foreign schema versions, duplicate ids, weight-sum
//   mismatches, dangling references and budget overruns are typed hard
//   errors (grader:e-*) and produce NO report. Missing or ambiguous
//   evidence is NOT an error: it becomes `indeterminate`/`not_earned`
//   outcomes with reasons in the report.
//
//   DETERMINISM. Reports carry no wall clock; digest = sha256 over the
//   canonical JSON body (digest member removed). Double-grading is
//   byte-identical; tests pin this.
//
// Serde discipline: fromJson() refuses foreign/missing schema versions and
// unknown enum spellings with typed errors naming the offending path.
#pragma once

#include "grader_error.h"

#include <json/json.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::grader {

inline constexpr const char *kRubricSchemaId = "sicnu.grader.rubric/1";
inline constexpr const char *kEvidenceSchemaId = "sicnu.grader.evidence/1";
inline constexpr const char *kReportSchemaId = "sicnu.grader.report/1";

/// Budget defaults (rubric may override downward or upward; hard caps exist
/// so a hostile rubric cannot make grading unbounded).
inline constexpr int kDefaultMaxCriteria = 512;
inline constexpr int kDefaultMaxEvidenceItems = 10000;
inline constexpr int kHardMaxCriteria = 4096;
inline constexpr int kHardMaxEvidenceItems = 100000;
/// A single answer text is capped (untrusted student input).
inline constexpr int kMaxAnswerBytes = 64 * 1024;

// ---------------------------------------------------------------------------
// Rubric
// ---------------------------------------------------------------------------

/// Matcher class of a criterion. Joins onto the evidence taxonomy:
///   Stage  — evidence kind "stage"
///   Metric — evidence kind "metric"
///   Fact   — any of artifact_state|provenance|checkpoint|artifact_grade|
///            verifier_verdict|replay_readiness|custom (optionally narrowed
///            with requireEvidenceKind)
///   Answer — evidence kind "answer"
enum class CriterionKind
{
    Stage,
    Metric,
    Fact,
    Answer,
};

std::string criterionKindSpelling( CriterionKind kind );
std::optional<CriterionKind> parseCriterionKind( const std::string &spelling );

/// Evidence taxonomy kinds (closed set; an unknown spelling is a typed
/// refusal, never a best-effort guess).
enum class EvidenceKind
{
    Stage,
    Metric,
    ArtifactState,
    Provenance,
    Checkpoint,
    Answer,
    ArtifactGrade,
    VerifierVerdict,
    ReplayReadiness,
    Custom,
};

std::string evidenceKindSpelling( EvidenceKind kind );
std::optional<EvidenceKind> parseEvidenceKind( const std::string &spelling );

struct MetricExpectation
{
    enum class Mode
    {
        AtLeast,
        AtMost,
        Equals,
        Range,
    };
    Mode mode = Mode::AtLeast;
    double value = 0.0;
    /// Range upper bound (Mode::Range only).
    double valueMax = 0.0;
    /// Pass-band half width (Mode::Equals) or band width below/above the
    /// threshold (AtLeast/AtMost). 0 = exact.
    double tolerance = 0.0;
    /// Optional teacher-declared linear partial-credit window. For AtLeast:
    /// earned fraction rises linearly from windowFrom (0) to the threshold
    /// (full). AtMost mirrors it. Ignored for Equals/Range.
    bool hasLinearWindow = false;
    double windowFrom = 0.0;
    double windowTo = 0.0;
};

struct StageExpectation
{
    /// Resulting state the evidence must carry (e.g. "Completed").
    std::string expectedState;
    /// Other stage keys whose evidence satisfies this criterion (any-of).
    std::vector<std::string> acceptedAlternatives;
    /// Stage keys that must ALSO be satisfied (by any evidence in the
    /// bundle) for this criterion to earn points. Declared sequencing —
    /// the only order the grader can see.
    std::vector<std::string> orderedAfter;
    /// Require this many distinct evidence items (≥ 1).
    int minDistinct = 1;
};

struct FactExpectation
{
    /// Required evidence state spelling (empty = any).
    std::string expectedState;
    /// Subset match against the evidence item's facts (string comparison on
    /// canonical JSON of each member).
    std::map<std::string, std::string> requiredFacts;
    /// Require this many distinct evidence items (≥ 1).
    int minCount = 1;
};

struct AnswerConcept
{
    std::string conceptId;
    std::string description;
    /// Groups of keywords; a concept hits when ANY group has ALL of its
    /// keywords present in the normalized answer text.
    std::vector<std::vector<std::string>> keywordGroups;
    /// Points contributed by this concept (> 0).
    double points = 0.0;
};

struct AnswerMisconception
{
    std::string misconceptionId;
    /// ANY of these patterns found in the normalized answer triggers the
    /// deduction.
    std::vector<std::string> patterns;
    /// Points deducted (≥ 0); total deduction clamps the criterion at 0.
    double deductPoints = 0.0;
    std::string explanation;
};

struct AnswerExpectation
{
    /// Matches evidence kind "answer" items by key == questionId.
    std::string questionId;
    std::vector<AnswerConcept> concepts;
    std::vector<AnswerMisconception> misconceptions;
};

struct Criterion
{
    std::string criterionId;
    std::string title;
    double maxPoints = 0.0;
    CriterionKind kind = CriterionKind::Fact;

    /// Evidence key this criterion matches (stage/metric/fact kinds).
    std::string evidenceKey;
    /// Fact criteria only: narrow the accepted evidence taxonomy kinds.
    std::optional<EvidenceKind> requireEvidenceKind;

    StageExpectation stage;
    MetricExpectation metric;
    FactExpectation fact;
    AnswerExpectation answer;

    /// Feedback text surfaced to the student / teacher respectively.
    std::string studentHint;
    std::string teacherHint;
};

struct Dimension
{
    std::string dimensionId;
    std::string title;
    /// Equal to the sum of its criteria's maxPoints (validated).
    double weight = 0.0;
    std::vector<Criterion> criteria;
};

struct HardConstraint
{
    enum class Mode
    {
        /// The evidence key must NOT be present (e.g. a forbidden
        /// preprocessing step recorded in provenance).
        ForbiddenEvidence,
        /// The evidence key MUST be present and satisfy `fact`.
        RequiredEvidence,
    };
    enum class Effect
    {
        /// Cap the whole report at capPoints.
        Cap,
        /// Zero the whole report.
        Zero,
    };

    std::string constraintId;
    std::string title;
    Mode mode = Mode::ForbiddenEvidence;
    std::string evidenceKey;
    Effect effect = Effect::Zero;
    /// Effect::Cap only; must be < totalPoints (a cap that cannot bind is a
    /// rubric bug).
    double capPoints = 0.0;
    std::string explanation;
    /// For RequiredEvidence: optional fact gate.
    FactExpectation fact;
};

struct StageRequirement
{
    std::string stageKey;
    std::string expectedState;
    std::vector<std::string> orderedAfter;
};

struct AlternatePathway
{
    std::string pathwayId;
    std::string title;
    /// The pathway holds when ALL of these criteria earned > 0.
    std::vector<std::string> criterionIds;
};

struct GraderBudgets
{
    int maxCriteria = kDefaultMaxCriteria;
    int maxEvidenceItems = kDefaultMaxEvidenceItems;
};

struct GradingRubric
{
    std::string rubricId;
    int revision = 0;
    std::string title;
    double totalPoints = 0.0;
    double passingScore = 60.0;
    std::vector<Dimension> dimensions;
    std::vector<HardConstraint> hardConstraints;
    std::vector<StageRequirement> requiredStages;
    std::vector<AlternatePathway> alternatePathways;
    GraderBudgets budgets;

    Json::Value toJson() const;
    static std::optional<GradingRubric> fromJson( const Json::Value &doc, GraderError &error );
    /// Structural validation (weight sums, unique ids, dangling refs,
    /// budgets, tolerance sanity). fromJson already applies it; exposed for
    /// value-object authors.
    bool validate( GraderError &error ) const;
};

// ---------------------------------------------------------------------------
// Evidence
// ---------------------------------------------------------------------------

struct GradeEvidenceItem
{
    std::string evidenceId;
    EvidenceKind kind = EvidenceKind::Custom;
    /// Matcher key: stage key, metric name, question id, artifact name…
    std::string key;
    /// Observed state spelling (workflow state, presence, verdict…).
    std::string state;
    /// Observed numeric value (metric items only; must be finite).
    bool hasValue = false;
    double value = 0.0;
    /// Free-form typed payload (digests, edge kinds, answer text under
    /// facts.answerText, verifier codes…).
    Json::Value facts{ Json::objectValue };
    /// Where the fact came from (provenance file, store projection…).
    std::string source;
    std::string recordedAtUtc;
};

struct EvidenceSubject
{
    std::string experimentId;
    std::string runId;
    bool empty() const { return experimentId.empty() && runId.empty(); }
};

struct GradeEvidence
{
    EvidenceSubject subject;
    std::vector<GradeEvidenceItem> items;

    Json::Value toJson() const;
    static std::optional<GradeEvidence> fromJson( const Json::Value &doc, GraderError &error );
    bool validate( GraderError &error ) const;
};

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------

enum class OutcomeStatus
{
    Earned,
    Partial,
    NotEarned,
    /// Cannot be judged from the supplied evidence (missing metric key,
    /// non-finite observation). Never silently folded into NotEarned.
    Indeterminate,
    /// Earned points exist but a hard constraint caps/zeroes the report.
    Capped,
};

std::string outcomeStatusSpelling( OutcomeStatus status );

struct CriterionOutcome
{
    std::string criterionId;
    double maxPoints = 0.0;
    /// Points before constraint capping (the raw judgment).
    double rawEarned = 0.0;
    /// Points after constraint capping (what the report pays out).
    double earned = 0.0;
    OutcomeStatus status = OutcomeStatus::NotEarned;
    std::vector<std::string> reasonCodes;
    std::vector<std::string> evidenceIds;
    std::string explanation;
};

struct DimensionOutcome
{
    std::string dimensionId;
    double weight = 0.0;
    double earned = 0.0;
    std::vector<CriterionOutcome> criteria;
};

struct HardConstraintOutcome
{
    std::string constraintId;
    bool violated = false;
    std::vector<std::string> evidenceIds;
    std::string effect;
    std::string explanation;
};

struct StageOutcome
{
    std::string stageKey;
    bool satisfied = false;
    std::vector<std::string> evidenceIds;
    std::string explanation;
};

enum class ReportVerdict
{
    Pass,
    Partial,
    Fail,
    Blocked,
};

std::string reportVerdictSpelling( ReportVerdict verdict );

struct GradeReport
{
    EvidenceSubject subject;
    std::string rubricId;
    int rubricRevision = 0;
    std::string rubricDigest;
    std::string evidenceDigest;
    double score = 0.0;
    double totalPoints = 0.0;
    double passingScore = 0.0;
    ReportVerdict verdict = ReportVerdict::Fail;
    std::vector<HardConstraintOutcome> hardConstraintOutcomes;
    std::vector<DimensionOutcome> dimensions;
    std::vector<StageOutcome> requiredStageOutcomes;
    std::vector<std::string> matchedPathways;
    /// sha256 over the canonical body (digest member removed).
    std::string digest;

    /// Canonical body (digest member removed, members sorted).
    Json::Value toBodyJson() const;
    /// Full document including digest — computed on demand over the body.
    Json::Value toJson() const;
    static std::optional<GradeReport> fromJson( const Json::Value &doc, GraderError &error );
    /// Recomputes and verifies the digest; refuses tampered documents.
    bool verifyDigest( GraderError &error ) const;
};

} // namespace sicnu::grader
