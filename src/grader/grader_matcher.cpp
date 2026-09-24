// grader_matcher.cpp — evidence indexing and per-criterion judgment.
// See grader_matcher.h for the normative judgment semantics and the
// append-only reason vocabulary.
#include "grader/grader_matcher.h"

#include "grader/grader_json.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace sicnu::grader {

namespace {

/// Band-edge epsilon: pure function of doubles, keeps `0.83 >= 0.85 - 0.02`
/// deterministic across compilers without loosening the teacher's band
/// beyond noise.
constexpr double kBandEps = 1e-12;

/// Declared expectation for a stage key: the stage criterion owning the key
/// wins; a requiredStages entry is the fallback; `nullopt` = any state.
std::optional<std::string> declaredStageExpectation( const std::string &stageKey,
                                                     const GradingRubric &rubric )
{
    for ( const auto &dimension : rubric.dimensions )
        for ( const auto &criterion : dimension.criteria )
            if ( criterion.kind == CriterionKind::Stage && criterion.evidenceKey == stageKey &&
                 !criterion.stage.expectedState.empty() )
                return criterion.stage.expectedState;
    for ( const auto &requirement : rubric.requiredStages )
        if ( requirement.stageKey == stageKey && !requirement.expectedState.empty() )
            return requirement.expectedState;
    return std::nullopt;
}

/// Stage-kind evidence satisfying a stage key's declared expectation.
std::vector<const GradeEvidenceItem *> satisfyingStageEvidence( const std::string &stageKey,
                                                               const GradingRubric &rubric,
                                                               const EvidenceIndex &index )
{
    std::vector<const GradeEvidenceItem *> satisfying;
    const auto expectation = declaredStageExpectation( stageKey, rubric );
    for ( const GradeEvidenceItem *item : index.stageItems( stageKey ) ) {
        if ( !expectation || item->state == *expectation )
            satisfying.push_back( item );
    }
    return satisfying;
}

/// Sequencing check shared by criteria and requiredStages: does some
/// candidate evidence for `key` record at/after every orderedAfter target?
/// Returns earned/order-violated/order-unprovable.
enum class SequenceVerdict
{
    Satisfied,
    Violated,
    Unprovable,
};

SequenceVerdict checkSequencing( const std::string &key,
                                 const std::vector<const GradeEvidenceItem *> &candidates,
                                 const std::vector<std::string> &orderedAfter,
                                 const GradingRubric &rubric, const EvidenceIndex &index,
                                 std::vector<const GradeEvidenceItem *> &cited )
{
    if ( orderedAfter.empty() )
        return SequenceVerdict::Satisfied;

    // Prerequisite satisfaction first: a stage that never happened cannot
    // have been "after" anything.
    std::vector<std::vector<const GradeEvidenceItem *>> prerequisites;
    prerequisites.reserve( orderedAfter.size() );
    for ( const std::string &after : orderedAfter ) {
        auto prereq = satisfyingStageEvidence( after, rubric, index );
        if ( prereq.empty() )
            return SequenceVerdict::Violated; // reason decided by the caller
        prerequisites.push_back( std::move( prereq ) );
    }

    // ∃ candidate evidence for `key` recorded at/after EVERY prerequisite.
    // Every needed stamp must be present; a missing stamp cannot prove or
    // disprove the declared order.
    bool sawMissingStamp = false;
    for ( const GradeEvidenceItem *candidate : candidates ) {
        if ( candidate->recordedAtUtc.empty() ) {
            sawMissingStamp = true;
            continue;
        }
        bool afterAll = true;
        for ( const auto &prereq : prerequisites ) {
            bool afterThis = false;
            for ( const GradeEvidenceItem *before : prereq ) {
                if ( before->recordedAtUtc.empty() ) {
                    sawMissingStamp = true;
                    continue;
                }
                if ( before->recordedAtUtc <= candidate->recordedAtUtc ) {
                    afterThis = true;
                    break;
                }
            }
            if ( !afterThis ) {
                afterAll = false;
                break;
            }
        }
        if ( afterAll ) {
            cited.push_back( candidate );
            for ( const auto &prereq : prerequisites )
                cited.insert( cited.end(), prereq.begin(), prereq.end() );
            return SequenceVerdict::Satisfied;
        }
    }
    return sawMissingStamp ? SequenceVerdict::Unprovable : SequenceVerdict::Violated;
}

} // namespace

// ---------------------------------------------------------------------------
// EvidenceIndex
// ---------------------------------------------------------------------------

EvidenceIndex::EvidenceIndex( const std::vector<GradeEvidenceItem> &items )
{
    for ( const GradeEvidenceItem &item : items ) {
        std::vector<const GradeEvidenceItem *> &bucket = m_byKey[item.key];
        const auto position =
            std::lower_bound( bucket.begin(), bucket.end(), &item,
                              []( const GradeEvidenceItem *a, const GradeEvidenceItem *b ) {
                                  return a->evidenceId < b->evidenceId;
                              } );
        bucket.insert( position, &item );
    }
}

const std::vector<const GradeEvidenceItem *> &EvidenceIndex::byKey( const std::string &key ) const
{
    static const std::vector<const GradeEvidenceItem *> kEmpty;
    const auto it = m_byKey.find( key );
    return it == m_byKey.end() ? kEmpty : it->second;
}

std::vector<const GradeEvidenceItem *> EvidenceIndex::stageItems( const std::string &key ) const
{
    std::vector<const GradeEvidenceItem *> stage;
    for ( const GradeEvidenceItem *item : byKey( key ) )
        if ( item->kind == EvidenceKind::Stage )
            stage.push_back( item );
    return stage;
}

// ---------------------------------------------------------------------------
// Stage
// ---------------------------------------------------------------------------

CriterionJudgment match::judgeStage( const Criterion &criterion, const EvidenceIndex &index,
                                     const GradingRubric &rubric )
{
    CriterionJudgment judgment;

    // Candidates: this key plus accepted alternatives (any-of), stage kind,
    // evidenceId-sorted via the index.
    std::vector<const GradeEvidenceItem *> candidates = index.stageItems( criterion.evidenceKey );
    for ( const std::string &alternative : criterion.stage.acceptedAlternatives ) {
        std::vector<const GradeEvidenceItem *> alt = index.stageItems( alternative );
        candidates.insert( candidates.end(), alt.begin(), alt.end() );
    }
    std::sort( candidates.begin(), candidates.end(),
               []( const GradeEvidenceItem *a, const GradeEvidenceItem *b ) {
                   return a->evidenceId < b->evidenceId;
               } );
    candidates.erase( std::unique( candidates.begin(), candidates.end(),
                                   []( const GradeEvidenceItem *a, const GradeEvidenceItem *b ) {
                                       return a->evidenceId == b->evidenceId;
                                   } ),
                      candidates.end() );

    if ( candidates.empty() ) {
        judgment.status = OutcomeStatus::Indeterminate;
        judgment.reasonCodes.push_back( "grader:stage-missing" );
        judgment.explanation = "no stage evidence was recorded for key '" + criterion.evidenceKey + "'";
        return judgment;
    }

    std::vector<const GradeEvidenceItem *> matching;
    for ( const GradeEvidenceItem *candidate : candidates )
        if ( candidate->state == criterion.stage.expectedState )
            matching.push_back( candidate );

    if ( matching.empty() ) {
        judgment.status = OutcomeStatus::NotEarned;
        judgment.reasonCodes.push_back( "grader:stage-state-mismatch" );
        for ( const GradeEvidenceItem *candidate : candidates )
            judgment.evidenceIds.push_back( candidate->evidenceId );
        judgment.explanation = "stage '" + criterion.evidenceKey + "' was recorded, but never in state '" +
                               criterion.stage.expectedState + "'";
        return judgment;
    }

    if ( static_cast<int>( matching.size() ) < criterion.stage.minDistinct ) {
        judgment.status = OutcomeStatus::NotEarned;
        judgment.reasonCodes.push_back( "grader:stage-insufficient-distinct" );
        for ( const GradeEvidenceItem *item : matching )
            judgment.evidenceIds.push_back( item->evidenceId );
        judgment.explanation = "stage '" + criterion.evidenceKey + "' needs " +
                               std::to_string( criterion.stage.minDistinct ) +
                               " distinct matching record(s), found " + std::to_string( matching.size() );
        return judgment;
    }

    if ( !criterion.stage.orderedAfter.empty() ) {
        std::vector<const GradeEvidenceItem *> cited;
        const SequenceVerdict verdict =
            checkSequencing( criterion.evidenceKey, matching, criterion.stage.orderedAfter, rubric, index, cited );
        for ( const GradeEvidenceItem *item : matching )
            judgment.evidenceIds.push_back( item->evidenceId );
        if ( verdict != SequenceVerdict::Satisfied ) {
            // Cite the deciding prerequisite evidence too: the violation is
            // only meaningful against the records it contradicts.
            for ( const std::string &after : criterion.stage.orderedAfter )
                for ( const GradeEvidenceItem *item : satisfyingStageEvidence( after, rubric, index ) )
                    judgment.evidenceIds.push_back( item->evidenceId );
            std::sort( judgment.evidenceIds.begin(), judgment.evidenceIds.end() );
            judgment.evidenceIds.erase( std::unique( judgment.evidenceIds.begin(), judgment.evidenceIds.end() ),
                                        judgment.evidenceIds.end() );
        }
        if ( verdict == SequenceVerdict::Unprovable ) {
            judgment.status = OutcomeStatus::Indeterminate;
            judgment.reasonCodes.push_back( "grader:stage-order-unprovable" );
            judgment.explanation = "the declared stage order cannot be proven from the recorded timestamps";
            return judgment;
        }
        if ( verdict == SequenceVerdict::Violated ) {
            judgment.status = OutcomeStatus::NotEarned;
            // Distinguish a never-satisfied prerequisite from a real inversion.
            bool prereqMissing = false;
            for ( const std::string &after : criterion.stage.orderedAfter )
                if ( satisfyingStageEvidence( after, rubric, index ).empty() )
                    prereqMissing = true;
            judgment.reasonCodes.push_back( prereqMissing ? "grader:prerequisite-stage-missing"
                                                          : "grader:stage-order-violated" );
            judgment.explanation = prereqMissing
                                       ? "a stage declared as a prerequisite was never satisfied"
                                       : "the recorded evidence contradicts the declared stage order";
            return judgment;
        }
        for ( const GradeEvidenceItem *item : cited )
            judgment.evidenceIds.push_back( item->evidenceId );
        std::sort( judgment.evidenceIds.begin(), judgment.evidenceIds.end() );
        judgment.evidenceIds.erase( std::unique( judgment.evidenceIds.begin(), judgment.evidenceIds.end() ),
                                    judgment.evidenceIds.end() );
    }
    else
    {
        for ( const GradeEvidenceItem *item : matching )
            judgment.evidenceIds.push_back( item->evidenceId );
    }

    judgment.rawEarned = criterion.maxPoints;
    judgment.status = OutcomeStatus::Earned;
    judgment.explanation = "stage '" + criterion.evidenceKey + "' recorded in state '" +
                           criterion.stage.expectedState + "'";
    return judgment;
}

// ---------------------------------------------------------------------------
// Metric
// ---------------------------------------------------------------------------

namespace {

/// Scores one observation against the metric expectation. Returns the earned
/// fraction in [0,1] plus the not-full reason slug (empty when full).
std::pair<double, std::string> scoreMetricValue( const MetricExpectation &metric, double value )
{
    switch ( metric.mode ) {
    case MetricExpectation::Mode::AtLeast: {
        const double full = metric.value - metric.tolerance;
        if ( value >= full - kBandEps )
            return { 1.0, {} };
        if ( metric.hasLinearWindow && value > metric.windowFrom ) {
            const double span = metric.windowTo - metric.windowFrom;
            if ( span > 0.0 ) {
                const double fraction = ( value - metric.windowFrom ) / span;
                return { std::clamp( fraction, 0.0, 1.0 ), "grader:metric-partial-window" };
            }
        }
        return { 0.0, "grader:metric-below-band" };
    }
    case MetricExpectation::Mode::AtMost: {
        const double full = metric.value + metric.tolerance;
        if ( value <= full + kBandEps )
            return { 1.0, {} };
        if ( metric.hasLinearWindow && value < metric.windowFrom ) {
            const double span = metric.windowFrom - metric.windowTo;
            if ( span > 0.0 ) {
                const double fraction = ( metric.windowFrom - value ) / span;
                return { std::clamp( fraction, 0.0, 1.0 ), "grader:metric-partial-window" };
            }
        }
        return { 0.0, "grader:metric-above-band" };
    }
    case MetricExpectation::Mode::Equals:
        if ( std::fabs( value - metric.value ) <= metric.tolerance + kBandEps )
            return { 1.0, {} };
        return { 0.0, "grader:metric-value-mismatch" };
    case MetricExpectation::Mode::Range:
        // Tolerance and linear windows are Equals/AtLeast/AtMost surfaces;
        // a range is the teacher's full band, verbatim.
        if ( value >= metric.value - kBandEps && value <= metric.valueMax + kBandEps )
            return { 1.0, {} };
        return { 0.0, "grader:metric-out-of-range" };
    }
    return { 0.0, "grader:metric-value-mismatch" };
}

} // namespace

CriterionJudgment match::judgeMetric( const Criterion &criterion, const EvidenceIndex &index )
{
    CriterionJudgment judgment;
    const std::vector<const GradeEvidenceItem *> &candidates = index.byKey( criterion.evidenceKey );

    std::vector<const GradeEvidenceItem *> observations;
    for ( const GradeEvidenceItem *item : candidates )
        if ( item->kind == EvidenceKind::Metric && item->hasValue )
            observations.push_back( item );

    if ( observations.empty() ) {
        judgment.status = OutcomeStatus::Indeterminate;
        judgment.reasonCodes.push_back( "grader:metric-missing" );
        judgment.explanation = "no metric observation was recorded for key '" + criterion.evidenceKey + "'";
        return judgment;
    }

    // Ambiguity: conflicting observations on one key cannot be silently
    // resolved — the bundle carries contradictory recorded truth.
    bool conflicting = false;
    const double reference = observations.front()->value;
    for ( const GradeEvidenceItem *item : observations )
        if ( item->value != reference )
            conflicting = true;
    if ( conflicting ) {
        judgment.status = OutcomeStatus::Indeterminate;
        judgment.reasonCodes.push_back( "grader:metric-ambiguous" );
        for ( const GradeEvidenceItem *item : observations )
            judgment.evidenceIds.push_back( item->evidenceId );
        judgment.explanation =
            "metric '" + criterion.evidenceKey + "' carries conflicting recorded values";
        return judgment;
    }

    for ( const GradeEvidenceItem *item : observations )
        judgment.evidenceIds.push_back( item->evidenceId );

    const auto [fraction, reason] = scoreMetricValue( criterion.metric, reference );
    judgment.rawEarned = criterion.maxPoints * fraction;
    if ( reason.empty() ) {
        judgment.status = OutcomeStatus::Earned;
        judgment.explanation = "metric '" + criterion.evidenceKey + "' satisfies the declared band";
    }
    else if ( fraction > 0.0 ) {
        judgment.status = OutcomeStatus::Partial;
        judgment.reasonCodes.push_back( reason );
        judgment.explanation = "metric '" + criterion.evidenceKey +
                               "' earns partial credit inside the teacher-declared window";
    }
    else
    {
        judgment.status = OutcomeStatus::NotEarned;
        judgment.reasonCodes.push_back( reason );
        judgment.explanation = "metric '" + criterion.evidenceKey + "' is outside the declared band";
    }
    return judgment;
}

// ---------------------------------------------------------------------------
// Fact
// ---------------------------------------------------------------------------

bool match::factsSatisfy( const GradeEvidenceItem &item,
                          const std::map<std::string, std::string> &required )
{
    for ( const auto &[key, expected] : required ) {
        if ( !item.facts.isMember( key ) )
            return false;
        GraderError error;
        const auto canonical = canonicalizeJson( item.facts[key], error );
        if ( !canonical || *canonical != expected )
            return false;
    }
    return true;
}

/// Fact-capable taxonomy check shared by judgeFact and the engine's
/// required-evidence constraints.
bool isFactCapableKind( EvidenceKind kind )
{
    switch ( kind ) {
    case EvidenceKind::ArtifactState:
    case EvidenceKind::Provenance:
    case EvidenceKind::Checkpoint:
    case EvidenceKind::ArtifactGrade:
    case EvidenceKind::VerifierVerdict:
    case EvidenceKind::ReplayReadiness:
    case EvidenceKind::Custom:
        return true;
    case EvidenceKind::Stage:
    case EvidenceKind::Metric:
    case EvidenceKind::Answer:
        return false;
    }
    return false;
}

bool match::factGate( const GradeEvidenceItem &item, const FactExpectation &fact )
{
    if ( !fact.expectedState.empty() && item.state != fact.expectedState )
        return false;
    return factsSatisfy( item, fact.requiredFacts );
}

bool match::isFactCapable( EvidenceKind kind )
{
    return isFactCapableKind( kind );
}

CriterionJudgment match::judgeFact( const Criterion &criterion, const EvidenceIndex &index )
{
    CriterionJudgment judgment;
    std::vector<const GradeEvidenceItem *> candidates;
    for ( const GradeEvidenceItem *item : index.byKey( criterion.evidenceKey ) ) {
        if ( !isFactCapableKind( item->kind ) )
            continue;
        if ( criterion.requireEvidenceKind && item->kind != *criterion.requireEvidenceKind )
            continue;
        candidates.push_back( item );
    }

    if ( candidates.empty() ) {
        judgment.status = OutcomeStatus::Indeterminate;
        judgment.reasonCodes.push_back( "grader:fact-missing" );
        judgment.explanation = criterion.requireEvidenceKind
                                   ? "no evidence of the required kind was recorded for key '" +
                                         criterion.evidenceKey + "'"
                                   : "no evidence was recorded for key '" + criterion.evidenceKey + "'";
        return judgment;
    }

    std::vector<const GradeEvidenceItem *> matched;
    bool sawStateMismatch = false;
    for ( const GradeEvidenceItem *item : candidates ) {
        if ( !criterion.fact.expectedState.empty() && item->state != criterion.fact.expectedState ) {
            sawStateMismatch = true;
            continue;
        }
        if ( !factsSatisfy( *item, criterion.fact.requiredFacts ) )
            continue;
        matched.push_back( item );
    }

    for ( const GradeEvidenceItem *item : matched )
        judgment.evidenceIds.push_back( item->evidenceId );

    if ( matched.empty() ) {
        judgment.status = OutcomeStatus::NotEarned;
        // Cite the contradicted candidates: the reason names the first gate
        // that failed, the evidence list shows everything considered.
        for ( const GradeEvidenceItem *item : candidates )
            judgment.evidenceIds.push_back( item->evidenceId );
        std::sort( judgment.evidenceIds.begin(), judgment.evidenceIds.end() );
        judgment.evidenceIds.erase( std::unique( judgment.evidenceIds.begin(), judgment.evidenceIds.end() ),
                                    judgment.evidenceIds.end() );
        if ( sawStateMismatch ) {
            judgment.reasonCodes.push_back( "grader:fact-state-mismatch" );
            judgment.explanation = "evidence for key '" + criterion.evidenceKey +
                                   "' was recorded, but never in the expected state";
        }
        else
        {
            judgment.reasonCodes.push_back( "grader:fact-missing-required-fact" );
            judgment.explanation =
                "evidence for key '" + criterion.evidenceKey + "' lacks one or more required facts";
        }
        return judgment;
    }

    if ( static_cast<int>( matched.size() ) < criterion.fact.minCount ) {
        judgment.status = OutcomeStatus::NotEarned;
        judgment.reasonCodes.push_back( "grader:fact-insufficient-count" );
        judgment.explanation = "key '" + criterion.evidenceKey + "' needs " +
                               std::to_string( criterion.fact.minCount ) +
                               " matching record(s), found " + std::to_string( matched.size() );
        return judgment;
    }

    judgment.rawEarned = criterion.maxPoints;
    judgment.status = OutcomeStatus::Earned;
    judgment.explanation = "evidence for key '" + criterion.evidenceKey + "' satisfies the fact gate";
    return judgment;
}

// ---------------------------------------------------------------------------
// Answer
// ---------------------------------------------------------------------------

namespace {

/// Locale-free ASCII classification: std::isspace/std::tolower follow
/// LC_CTYPE, and a locale where tolower('I') != 'i' would silently change
/// judgments (and therefore digests) — the same locale determinism rule the
/// number canonicalizer already enforces.
bool isAsciiSpace( char ch )
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

char asciiLower( char ch )
{
    return ( ch >= 'A' && ch <= 'Z' ) ? static_cast<char>( ch - 'A' + 'a' ) : ch;
}

bool isAsciiWordChar( char ch )
{
    const char lowered = asciiLower( ch );
    return ( lowered >= 'a' && lowered <= 'z' ) || ( lowered >= '0' && lowered <= '9' ) || lowered == '_';
}

} // namespace

std::string match::normalizeAnswerText( const std::string &text )
{
    std::string out;
    out.reserve( text.size() );
    bool pendingSpace = false;
    for ( const char raw : text ) {
        if ( isAsciiSpace( raw ) ) {
            pendingSpace = !out.empty();
            continue;
        }
        if ( pendingSpace ) {
            out.push_back( ' ' );
            pendingSpace = false;
        }
        out.push_back( asciiLower( raw ) );
    }
    return out;
}

bool match::containsKeyword( const std::string &haystack, const std::string &needle )
{
    if ( needle.empty() || haystack.empty() )
        return false;
    const auto isWordChar = []( char ch ) { return isAsciiWordChar( ch ); };
    std::size_t position = 0;
    while ( ( position = haystack.find( needle, position ) ) != std::string::npos ) {
        const bool leftOk = position == 0 || !isWordChar( haystack[position - 1] );
        const std::size_t end = position + needle.size();
        const bool rightOk = end == haystack.size() || !isWordChar( haystack[end] );
        if ( leftOk && rightOk )
            return true;
        ++position;
    }
    return false;
}

namespace {

bool conceptHit( const AnswerConcept &answerConcept, const std::string &normalized )
{
    for ( const auto &group : answerConcept.keywordGroups ) {
        bool all = true;
        for ( const std::string &keyword : group ) {
            if ( !match::containsKeyword( normalized, match::normalizeAnswerText( keyword ) ) ) {
                all = false;
                break;
            }
        }
        if ( all )
            return true;
    }
    return false;
}

bool misconceptionHit( const AnswerMisconception &answerMisconception, const std::string &normalized )
{
    for ( const std::string &pattern : answerMisconception.patterns )
        if ( match::containsKeyword( normalized, match::normalizeAnswerText( pattern ) ) )
            return true;
    return false;
}

} // namespace

CriterionJudgment match::judgeAnswer( const Criterion &criterion, const EvidenceIndex &index )
{
    CriterionJudgment judgment;
    std::vector<const GradeEvidenceItem *> answers;
    for ( const GradeEvidenceItem *item : index.byKey( criterion.answer.questionId ) )
        if ( item->kind == EvidenceKind::Answer )
            answers.push_back( item );

    if ( answers.empty() ) {
        judgment.status = OutcomeStatus::Indeterminate;
        judgment.reasonCodes.push_back( "grader:answer-missing" );
        judgment.explanation = "no answer was recorded for question '" + criterion.answer.questionId + "'";
        return judgment;
    }

    // Conflicting submissions on one question are ambiguous recorded truth.
    bool conflicting = false;
    const Json::Value &reference = answers.front()->facts["answerText"];
    for ( const GradeEvidenceItem *item : answers )
        if ( item->facts["answerText"].asString() != reference.asString() )
            conflicting = true;
    if ( conflicting ) {
        judgment.status = OutcomeStatus::Indeterminate;
        judgment.reasonCodes.push_back( "grader:answer-ambiguous" );
        for ( const GradeEvidenceItem *item : answers )
            judgment.evidenceIds.push_back( item->evidenceId );
        judgment.explanation =
            "question '" + criterion.answer.questionId + "' carries conflicting recorded answers";
        return judgment;
    }

    for ( const GradeEvidenceItem *item : answers )
        judgment.evidenceIds.push_back( item->evidenceId );

    const std::string normalized = normalizeAnswerText( reference.asString() );

    double earned = 0.0;
    bool missingConcept = false;
    std::string missingConceptIds;
    for ( const AnswerConcept &answerConcept : criterion.answer.concepts ) {
        if ( conceptHit( answerConcept, normalized ) )
            earned += answerConcept.points;
        else
        {
            missingConcept = true;
            if ( !missingConceptIds.empty() )
                missingConceptIds += ", ";
            missingConceptIds += answerConcept.conceptId;
        }
    }

    double deduction = 0.0;
    std::string hitMisconceptionIds;
    for ( const AnswerMisconception &answerMisconception : criterion.answer.misconceptions ) {
        if ( misconceptionHit( answerMisconception, normalized ) ) {
            deduction += answerMisconception.deductPoints;
            if ( !hitMisconceptionIds.empty() )
                hitMisconceptionIds += ", ";
            hitMisconceptionIds += answerMisconception.misconceptionId;
        }
    }

    const bool clampedHigh = earned > criterion.maxPoints + 1e-12;
    judgment.rawEarned = std::clamp( earned - deduction, 0.0, criterion.maxPoints );

    if ( deduction > 0.0 ) {
        judgment.reasonCodes.push_back( "grader:misconception-deduction" );
        judgment.explanation += "deduction applied for: " + hitMisconceptionIds + ".";
    }
    if ( clampedHigh ) {
        judgment.reasonCodes.push_back( "grader:answer-points-clamped" );
        judgment.explanation += " concept points exceed the criterion maximum; payout is clamped.";
    }
    if ( missingConcept && judgment.rawEarned < criterion.maxPoints ) {
        judgment.reasonCodes.push_back( "grader:concept-missing" );
        judgment.explanation += " concepts not addressed: " + missingConceptIds + ".";
    }

    if ( judgment.rawEarned <= 0.0 )
        judgment.status = OutcomeStatus::NotEarned;
    else if ( judgment.rawEarned >= criterion.maxPoints - 1e-12 )
        judgment.status = OutcomeStatus::Earned;
    else
        judgment.status = OutcomeStatus::Partial;

    // Reason-chain invariant (ADR 0174 §4): a non-full outcome always
    // carries at least one slug. Two teacher-legal rubric shapes would
    // otherwise fall through silently: declared concept points that do not
    // sum to the criterion maximum (all concepts hit, points still short),
    // and misconception-only criteria whose patterns never matched.
    if ( judgment.status != OutcomeStatus::Earned && judgment.reasonCodes.empty() ) {
        judgment.reasonCodes.push_back( "grader:answer-points-short" );
        judgment.explanation +=
            ( judgment.explanation.empty() ? std::string {} : std::string { " " } ) +
            "the criterion's declared concept points do not reach its maximum; the shortfall is not earned";
    }
    if ( judgment.explanation.empty() )
        judgment.explanation = "answer for question '" + criterion.answer.questionId +
                               "' addressed all declared concepts";
    return judgment;
}

// ---------------------------------------------------------------------------
// Required stages
// ---------------------------------------------------------------------------

StageOutcome match::judgeStageRequirement( const StageRequirement &requirement,
                                           const EvidenceIndex &index, const GradingRubric &rubric )
{
    StageOutcome outcome;
    outcome.stageKey = requirement.stageKey;

    auto candidates = satisfyingStageEvidence( requirement.stageKey, rubric, index );
    if ( candidates.empty() ) {
        outcome.explanation = "no stage evidence for key '" + requirement.stageKey + "'" +
                              ( requirement.expectedState.empty()
                                    ? std::string {}
                                    : " in state '" + requirement.expectedState + "'" );
        return outcome;
    }

    if ( requirement.orderedAfter.empty() ) {
        outcome.satisfied = true;
        for ( const GradeEvidenceItem *item : candidates )
            outcome.evidenceIds.push_back( item->evidenceId );
        outcome.explanation = "stage '" + requirement.stageKey + "' satisfied by recorded evidence";
        return outcome;
    }

    std::vector<const GradeEvidenceItem *> cited;
    const SequenceVerdict verdict =
        checkSequencing( requirement.stageKey, candidates, requirement.orderedAfter, rubric, index, cited );
    if ( verdict == SequenceVerdict::Satisfied ) {
        outcome.satisfied = true;
        for ( const GradeEvidenceItem *item : cited )
            outcome.evidenceIds.push_back( item->evidenceId );
        std::sort( outcome.evidenceIds.begin(), outcome.evidenceIds.end() );
        outcome.evidenceIds.erase( std::unique( outcome.evidenceIds.begin(), outcome.evidenceIds.end() ),
                                   outcome.evidenceIds.end() );
        outcome.explanation = "stage '" + requirement.stageKey + "' satisfied in the declared order";
    }
    else if ( verdict == SequenceVerdict::Unprovable )
    {
        outcome.explanation = "the declared order for stage '" + requirement.stageKey +
                              "' cannot be proven from the recorded timestamps";
    }
    else
    {
        bool prereqMissing = false;
        for ( const std::string &after : requirement.orderedAfter )
            if ( satisfyingStageEvidence( after, rubric, index ).empty() )
                prereqMissing = true;
        outcome.explanation = prereqMissing
                                  ? "a prerequisite stage of '" + requirement.stageKey + "' was never satisfied"
                                  : "recorded evidence contradicts the declared order for stage '" +
                                        requirement.stageKey + "'";
    }
    return outcome;
}

} // namespace sicnu::grader
