// grader_engine.cpp — Slices B/C: the matching and scoring pipeline
// (ADR 0174). Validation stays fail-closed (slice A): an invalid document is
// a typed refusal with NO report. A valid pair is graded deterministically:
//
//   1. index the evidence bundle (evidenceId-sorted buckets, stable under
//      input permutation);
//   2. judge every criterion (stage/metric/fact/answer) into rawEarned;
//   3. evaluate hard constraints (forbidden/required evidence) and apply
//      Zero (dominant) or Cap (min capPoints, proportional scaling);
//   4. evaluate requiredStages, resolve alternatePathways deterministically
//      (highest summed rawEarned, tie → pathwayId lexicographic);
//   5. aggregate dimension outcomes, derive the verdict, seal the digests.
//
// The engine is pure: no I/O, no clock, no store access. Digests are sha256
// over the canonical JSON of the graded documents; double grading is
// byte-identical.
#include "grader/grader_engine.h"

#include "grader/grader_json.h"
#include "grader/grader_matcher.h"
#include "grader/grader_sha256.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>

namespace sicnu::grader {

namespace {

/// FP guard for score comparisons (verdict thresholds, cap clamping).
constexpr double kScoreEps = 1e-9;

struct HardConstraintJudgment
{
    bool violated = false;
    std::vector<std::string> evidenceIds;
    std::string explanation;
};

std::string digestOf( const Json::Value &document, GraderError &error )
{
    const auto canonical = canonicalizeJson( document, error );
    if ( !canonical )
        return {};
    return sha256Hex( *canonical );
}

/// Forbidden evidence: any recorded item on the key violates.
/// Required evidence: the key must carry at least fact.minCount items
/// passing the shared fact gate.
HardConstraintJudgment judgeConstraint( const HardConstraint &constraint, const EvidenceIndex &index )
{
    HardConstraintJudgment judgment;
    if ( constraint.mode == HardConstraint::Mode::ForbiddenEvidence ) {
        for ( const GradeEvidenceItem *item : index.byKey( constraint.evidenceKey ) ) {
            judgment.violated = true;
            judgment.evidenceIds.push_back( item->evidenceId );
        }
        judgment.explanation =
            judgment.violated
                ? ( constraint.explanation.empty()
                        ? "forbidden evidence key '" + constraint.evidenceKey + "' was recorded"
                        : constraint.explanation )
                : ( constraint.explanation.empty()
                        ? "forbidden evidence key '" + constraint.evidenceKey + "' is absent"
                        : constraint.explanation );
        return judgment;
    }

    int matched = 0;
    for ( const GradeEvidenceItem *item : index.byKey( constraint.evidenceKey ) ) {
        if ( match::factGate( *item, constraint.fact ) ) {
            ++matched;
            judgment.evidenceIds.push_back( item->evidenceId );
        }
    }
    judgment.violated = matched < constraint.fact.minCount;
    judgment.explanation =
        judgment.violated
            ? ( constraint.explanation.empty()
                    ? "required evidence key '" + constraint.evidenceKey +
                          "' is missing or does not satisfy the gate"
                    : constraint.explanation )
            : ( constraint.explanation.empty()
                    ? "required evidence key '" + constraint.evidenceKey +
                          "' is present and satisfies the gate"
                    : constraint.explanation );
    return judgment;
}

/// Pathway score for the deterministic choice: summed rawEarned of members.
double pathwayEarned( const AlternatePathway &pathway,
                      const std::map<std::string, const CriterionOutcome *> &judgments )
{
    double total = 0.0;
    for ( const std::string &criterionId : pathway.criterionIds ) {
        const auto it = judgments.find( criterionId );
        if ( it != judgments.end() && it->second )
            total += it->second->rawEarned;
    }
    return total;
}

/// Pathway holds when ALL member criteria earned raw points.
bool pathwayHolds( const AlternatePathway &pathway,
                   const std::map<std::string, const CriterionOutcome *> &judgments )
{
    for ( const std::string &criterionId : pathway.criterionIds ) {
        const auto it = judgments.find( criterionId );
        if ( it == judgments.end() || !it->second || !( it->second->rawEarned > 0.0 ) )
            return false;
    }
    return true;
}

} // namespace

GradeOutcome grade( const GradingRubric &rubric, const GradeEvidence &evidence )
{
    GradeOutcome out;
    if ( GraderError error; !rubric.validate( error ) ) {
        out.error = error;
        return out;
    }
    if ( GraderError error; !evidence.validate( error ) ) {
        out.error = error;
        return out;
    }
    const std::size_t evidenceCount = evidence.items.size();
    if ( evidenceCount > static_cast<std::size_t>( rubric.budgets.maxEvidenceItems ) ) {
        out.error = makeError( GraderErrorCode::SchemaShapeInvalid,
                               "evidence bundle holds " + std::to_string( evidenceCount ) +
                                   " items, over the rubric budget " +
                                   std::to_string( rubric.budgets.maxEvidenceItems ),
                               "items" );
        return out;
    }

    // Digest inputs FIRST: a bundle that cannot be canonicalized (non-finite
    // numbers planted in facts) is an invalid document — typed refusal, no
    // report (fail closed beats a report with a fabricated digest).
    GraderError digestError;
    GradeReport report;
    report.rubricDigest = digestOf( rubric.toJson(), digestError );
    if ( digestError.ok() )
        report.evidenceDigest = digestOf( evidence.toJson(), digestError );
    if ( !digestError.ok() ) {
        out.error = makeError( GraderErrorCode::SchemaShapeInvalid,
                               "graded document cannot be canonicalized: " + digestError.message,
                               digestError.path );
        return out;
    }

    const EvidenceIndex index( evidence.items );

    // 1. Judge every criterion (raw, before constraint capping).
    std::map<std::string, const CriterionOutcome *> judgmentsById;
    report.dimensions.reserve( rubric.dimensions.size() );
    for ( const Dimension &dimension : rubric.dimensions ) {
        DimensionOutcome dimensionOutcome;
        dimensionOutcome.dimensionId = dimension.dimensionId;
        dimensionOutcome.weight = dimension.weight;
        dimensionOutcome.criteria.reserve( dimension.criteria.size() );
        for ( const Criterion &criterion : dimension.criteria ) {
            CriterionJudgment judgment;
            switch ( criterion.kind ) {
            case CriterionKind::Stage: judgment = match::judgeStage( criterion, index, rubric ); break;
            case CriterionKind::Metric: judgment = match::judgeMetric( criterion, index ); break;
            case CriterionKind::Fact: judgment = match::judgeFact( criterion, index ); break;
            case CriterionKind::Answer: judgment = match::judgeAnswer( criterion, index ); break;
            }
            CriterionOutcome outcome;
            outcome.criterionId = criterion.criterionId;
            outcome.maxPoints = criterion.maxPoints;
            outcome.rawEarned = judgment.rawEarned;
            outcome.earned = judgment.rawEarned;
            outcome.status = judgment.status;
            outcome.reasonCodes = judgment.reasonCodes;
            outcome.evidenceIds = judgment.evidenceIds;
            outcome.explanation = judgment.explanation;
            dimensionOutcome.criteria.push_back( std::move( outcome ) );
            judgmentsById[criterion.criterionId] = &dimensionOutcome.criteria.back();
        }
        report.dimensions.push_back( std::move( dimensionOutcome ) );
    }

    // 2. Hard constraints (all of them evaluated, none short-circuited: the
    //    report must show every violated constraint, evidence-cited).
    bool zeroViolated = false;
    double capPoints = std::numeric_limits<double>::infinity();
    for ( const HardConstraint &constraint : rubric.hardConstraints ) {
        const HardConstraintJudgment judgment = judgeConstraint( constraint, index );
        HardConstraintOutcome outcome;
        outcome.constraintId = constraint.constraintId;
        outcome.violated = judgment.violated;
        outcome.evidenceIds = judgment.evidenceIds;
        outcome.effect = constraint.effect == HardConstraint::Effect::Cap ? "cap" : "zero";
        outcome.explanation = judgment.explanation;
        report.hardConstraintOutcomes.push_back( std::move( outcome ) );
        if ( judgment.violated ) {
            if ( constraint.effect == HardConstraint::Effect::Zero )
                zeroViolated = true;
            else
                capPoints = std::min( capPoints, constraint.capPoints );
        }
    }

    // 3. Apply constraints: Zero dominates; Cap scales earned proportionally
    //    so the payout respects the teacher's bound without re-judging.
    double rawTotal = 0.0;
    for ( const DimensionOutcome &dimension : report.dimensions )
        for ( const CriterionOutcome &criterion : dimension.criteria )
            rawTotal += criterion.rawEarned;

    const bool capBinds = !zeroViolated && std::isfinite( capPoints ) && rawTotal > capPoints + kScoreEps;
    const double factor = zeroViolated ? 0.0 : ( capBinds ? capPoints / rawTotal : 1.0 );

    double score = 0.0;
    for ( DimensionOutcome &dimension : report.dimensions ) {
        double dimensionEarned = 0.0;
        for ( CriterionOutcome &criterion : dimension.criteria ) {
            criterion.earned = criterion.rawEarned * factor;
            if ( factor < 1.0 && criterion.rawEarned > 0.0 ) {
                criterion.status = OutcomeStatus::Capped;
                criterion.reasonCodes.push_back( zeroViolated ? "grader:constraint-zero"
                                                              : "grader:constraint-cap" );
                std::string violatedIds;
                for ( const HardConstraintOutcome &constraint : report.hardConstraintOutcomes )
                    if ( constraint.violated )
                        violatedIds += violatedIds.empty() ? constraint.constraintId : ", " + constraint.constraintId;
                criterion.explanation +=
                    ( criterion.explanation.empty() ? std::string {} : std::string { " " } ) +
                    "Payout constrained by violated hard constraint(s): " + violatedIds + ".";
            }
            dimensionEarned += criterion.earned;
        }
        dimension.earned = dimensionEarned;
        score += dimensionEarned;
    }
    if ( capBinds )
        score = std::min( score, capPoints );

    // 4. Required stages + alternate pathways.
    for ( const StageRequirement &requirement : rubric.requiredStages )
        report.requiredStageOutcomes.push_back(
            match::judgeStageRequirement( requirement, index, rubric ) );

    const AlternatePathway *chosen = nullptr;
    double chosenEarned = 0.0;
    for ( const AlternatePathway &pathway : rubric.alternatePathways ) {
        if ( !pathwayHolds( pathway, judgmentsById ) )
            continue;
        const double earned = pathwayEarned( pathway, judgmentsById );
        const bool better = !chosen || earned > chosenEarned + kScoreEps ||
                            ( earned >= chosenEarned - kScoreEps && pathway.pathwayId < chosen->pathwayId );
        if ( better ) {
            chosen = &pathway;
            chosenEarned = earned;
        }
    }
    if ( chosen )
        report.matchedPathways.push_back( chosen->pathwayId );

    // 5. Verdict + identity.
    report.subject = evidence.subject;
    report.rubricId = rubric.rubricId;
    report.rubricRevision = rubric.revision;
    report.totalPoints = rubric.totalPoints;
    report.passingScore = rubric.passingScore;
    report.score = score;
    if ( zeroViolated )
        report.verdict = ReportVerdict::Blocked;
    else if ( score >= rubric.passingScore - kScoreEps )
        report.verdict = ReportVerdict::Pass;
    else if ( score > kScoreEps )
        report.verdict = ReportVerdict::Partial;
    else
        report.verdict = ReportVerdict::Fail;

    // Seal the report digest over the canonical body so the returned value
    // object and its JSON form carry the same tamper gate.
    GraderError sealError;
    report.digest = digestOf( report.toBodyJson(), sealError );
    if ( !sealError.ok() ) {
        // Unreachable for validated inputs (all numeric members are
        // finite-checked); refuse rather than emit an unsealed report.
        out.error = makeError( GraderErrorCode::Internal, "report body cannot be canonicalized" );
        return out;
    }

    out.report = std::move( report );
    out.ok = true;
    return out;
}

} // namespace sicnu::grader
