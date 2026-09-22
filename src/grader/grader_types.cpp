// grader_types.cpp — serde + fail-closed validation for the three versioned
// grader documents. See grader_types.h for the normative contracts.
#include "grader/grader_types.h"

#include "grader/grader_json.h"
#include "grader/grader_sha256.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace sicnu::grader {

namespace {

constexpr double kEps = 1e-9;

bool stringMember( const Json::Value &doc, const char *key, std::string &out )
{
    if ( !doc.isMember( key ) || !doc[key].isString() )
        return false;
    out = doc[key].asString();
    return true;
}

bool finiteNumberMember( const Json::Value &doc, const char *key, double &out )
{
    if ( !doc.isMember( key ) || ( !doc[key].isDouble() && !doc[key].isIntegral() ) )
        return false;
    const double value = doc[key].asDouble();
    if ( !std::isfinite( value ) )
        return false;
    out = value;
    return true;
}

/// Reads a JSON number that must land in `int`: both type- and RANGE-checked.
/// jsoncpp's `asInt()` THROWS for |v| beyond 2^31-1 even when `isIntegral()`
/// is true, and widening to Int64 then narrowing would silently wrap
/// (4294967297 -> 1, past every `>= 1` validation) — so the fit is tested
/// explicitly and a hostile magnitude is a typed refusal, never an exception
/// or a wrapped value.
bool boundedIntMember( const Json::Value &object, const char *key, int &out )
{
    if ( !object.isMember( key ) || object[key].isBool() )
        return false;
    const Json::Value &value = object[key];
    if ( value.isUInt64() )
    {
        const Json::UInt64 magnitude = value.asUInt64();
        if ( magnitude > static_cast<Json::UInt64>( std::numeric_limits<int>::max() ) )
            return false;
        out = static_cast<int>( magnitude );
        return true;
    }
    if ( !value.isIntegral() || !value.isInt64() )
        return false;
    const Json::Int64 signedValue = value.asInt64();
    if ( signedValue < std::numeric_limits<int>::min() ||
         signedValue > std::numeric_limits<int>::max() )
        return false;
    out = static_cast<int>( signedValue );
    return true;
}

bool parseStageExpectation( const Json::Value &doc, const std::string &path, StageExpectation &out, GraderError &error )
{
    if ( doc.isMember( "stage" ) ) {
        const Json::Value &stage = doc["stage"];
        if ( !stage.isObject() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "stage must be an object", path + ".stage" );
            return false;
        }
        if ( !stringMember( stage, "expectedState", out.expectedState ) || out.expectedState.empty() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "stage.expectedState must be a non-empty string",
                               path + ".stage.expectedState" );
            return false;
        }
        if ( stage.isMember( "acceptedAlternatives" ) ) {
            if ( !stage["acceptedAlternatives"].isArray() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "acceptedAlternatives must be an array",
                                   path + ".stage.acceptedAlternatives" );
                return false;
            }
            for ( const auto &entry : stage["acceptedAlternatives"] ) {
                if ( !entry.isString() || entry.asString().empty() ) {
                    error = makeError( GraderErrorCode::SchemaShapeInvalid, "acceptedAlternatives entries must be non-empty strings",
                                       path + ".stage.acceptedAlternatives" );
                    return false;
                }
                out.acceptedAlternatives.push_back( entry.asString() );
            }
        }
        if ( stage.isMember( "orderedAfter" ) ) {
            if ( !stage["orderedAfter"].isArray() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "orderedAfter must be an array",
                                   path + ".stage.orderedAfter" );
                return false;
            }
            for ( const auto &entry : stage["orderedAfter"] ) {
                if ( !entry.isString() || entry.asString().empty() ) {
                    error = makeError( GraderErrorCode::SchemaShapeInvalid, "orderedAfter entries must be non-empty strings",
                                       path + ".stage.orderedAfter" );
                    return false;
                }
                out.orderedAfter.push_back( entry.asString() );
            }
        }
        if ( stage.isMember( "minDistinct" ) ) {
            int minDistinct = 0;
            if ( !boundedIntMember( stage, "minDistinct", minDistinct ) || minDistinct < 1 ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "minDistinct must be an integer >= 1",
                                   path + ".stage.minDistinct" );
                return false;
            }
            out.minDistinct = minDistinct;
        }
    } else {
        out.expectedState = "Completed";
    }
    return true;
}

bool parseMetricExpectation( const Json::Value &doc, const std::string &path, MetricExpectation &out, GraderError &error )
{
    if ( !doc.isMember( "metric" ) || !doc["metric"].isObject() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "metric criterion requires a metric object", path + ".metric" );
        return false;
    }
    const Json::Value &metric = doc["metric"];
    const std::string modePath = path + ".metric.mode";
    std::string modeSpelling;
    if ( !stringMember( metric, "mode", modeSpelling ) ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "metric.mode must be a string", modePath );
        return false;
    }
    if ( modeSpelling == "at_least" )
        out.mode = MetricExpectation::Mode::AtLeast;
    else if ( modeSpelling == "at_most" )
        out.mode = MetricExpectation::Mode::AtMost;
    else if ( modeSpelling == "equals" )
        out.mode = MetricExpectation::Mode::Equals;
    else if ( modeSpelling == "range" )
        out.mode = MetricExpectation::Mode::Range;
    else {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "unknown metric.mode spelling: " + modeSpelling, modePath );
        return false;
    }
    if ( !finiteNumberMember( metric, "value", out.value ) ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "metric.value must be a finite number", path + ".metric.value" );
        return false;
    }
    if ( out.mode == MetricExpectation::Mode::Range ) {
        if ( !finiteNumberMember( metric, "valueMax", out.valueMax ) ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "metric range requires finite valueMax",
                               path + ".metric.valueMax" );
            return false;
        }
        if ( !( out.valueMax > out.value ) ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "metric range requires valueMax > value",
                               path + ".metric.valueMax" );
            return false;
        }
    }
    if ( metric.isMember( "tolerance" ) ) {
        if ( !finiteNumberMember( metric, "tolerance", out.tolerance ) || out.tolerance < 0.0 ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "metric.tolerance must be a finite number >= 0",
                               path + ".metric.tolerance" );
            return false;
        }
    }
    if ( metric.isMember( "linearWindow" ) ) {
        const Json::Value &window = metric["linearWindow"];
        if ( !window.isObject() || !finiteNumberMember( window, "from", out.windowFrom ) ||
             !finiteNumberMember( window, "to", out.windowTo ) || !( out.windowTo > out.windowFrom ) ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "linearWindow requires finite from < to",
                               path + ".metric.linearWindow" );
            return false;
        }
        out.hasLinearWindow = true;
    }
    return true;
}

bool parseFactExpectation( const Json::Value &doc, const std::string &path, const char *member, FactExpectation &out, GraderError &error )
{
    if ( !doc.isMember( member ) )
        return true;
    const Json::Value &facts = doc[member];
    if ( !facts.isObject() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, std::string( member ) + " must be an object",
                           path + "." + member );
        return false;
    }
    if ( facts.isMember( "expectedState" ) ) {
        if ( !stringMember( facts, "expectedState", out.expectedState ) ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, std::string( member ) + ".expectedState must be a string",
                               path + "." + member + ".expectedState" );
            return false;
        }
    }
    if ( facts.isMember( "requiredFacts" ) ) {
        const Json::Value &required = facts["requiredFacts"];
        if ( !required.isObject() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, std::string( member ) + ".requiredFacts must be an object",
                               path + "." + member + ".requiredFacts" );
            return false;
        }
        for ( const auto &key : required.getMemberNames() ) {
            // Fail visibly: an uncanonicalizable fact value (e.g. 1e999,
            // which jsoncpp parses to a non-finite double with no parse
            // error) must not silently become the unmatchable empty string.
            GraderError canonicalError;
            auto canonical = canonicalizeJson( required[key], canonicalError );
            if ( !canonical.has_value() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid,
                                   "requiredFacts['" + key + "'] cannot be canonicalized: " +
                                       canonicalError.message,
                                   path + "." + member + ".requiredFacts." + key );
                return false;
            }
            out.requiredFacts[key] = *canonical;
        }
    }
    if ( facts.isMember( "minCount" ) ) {
        int minCount = 0;
        if ( !boundedIntMember( facts, "minCount", minCount ) || minCount < 1 ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, std::string( member ) + ".minCount must be an integer >= 1",
                               path + "." + member + ".minCount" );
            return false;
        }
        out.minCount = minCount;
    }
    return true;
}

bool parseAnswerExpectation( const Json::Value &doc, const std::string &path, AnswerExpectation &out, GraderError &error )
{
    if ( !doc.isMember( "answer" ) || !doc["answer"].isObject() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "answer criterion requires an answer object", path + ".answer" );
        return false;
    }
    const Json::Value &answer = doc["answer"];
    if ( !stringMember( answer, "questionId", out.questionId ) || out.questionId.empty() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "answer.questionId must be a non-empty string",
                           path + ".answer.questionId" );
        return false;
    }
    if ( answer.isMember( "concepts" ) ) {
        if ( !answer["concepts"].isArray() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "answer.concepts must be an array",
                               path + ".answer.concepts" );
            return false;
        }
        std::set<std::string> conceptIds;
        for ( const auto &entry : answer["concepts"] ) {
            AnswerConcept answerConcept;
            const std::string conceptPath = path + ".answer.concepts";
            if ( !entry.isObject() || !stringMember( entry, "conceptId", answerConcept.conceptId ) || answerConcept.conceptId.empty() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "conceptId must be a non-empty string",
                                   conceptPath + ".conceptId" );
                return false;
            }
            if ( !conceptIds.insert( answerConcept.conceptId ).second ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "duplicate conceptId: " + answerConcept.conceptId,
                                   conceptPath + ".conceptId" );
                return false;
            }
            if ( entry.isMember( "description" ) )
                stringMember( entry, "description", answerConcept.description );
            if ( !entry.isMember( "keywordGroups" ) || !entry["keywordGroups"].isArray() || entry["keywordGroups"].empty() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "concept requires non-empty keywordGroups",
                                   conceptPath + ".keywordGroups" );
                return false;
            }
            for ( const auto &group : entry["keywordGroups"] ) {
                if ( !group.isArray() || group.empty() ) {
                    error = makeError( GraderErrorCode::SchemaShapeInvalid, "keywordGroups entries must be non-empty arrays",
                                       conceptPath + ".keywordGroups" );
                    return false;
                }
                std::vector<std::string> keywords;
                for ( const auto &keyword : group ) {
                    if ( !keyword.isString() || keyword.asString().empty() ) {
                        error = makeError( GraderErrorCode::SchemaShapeInvalid, "keywords must be non-empty strings",
                                           conceptPath + ".keywordGroups" );
                        return false;
                    }
                    keywords.push_back( keyword.asString() );
                }
                answerConcept.keywordGroups.push_back( std::move( keywords ) );
            }
            if ( !finiteNumberMember( entry, "points", answerConcept.points ) || answerConcept.points <= 0.0 ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "concept.points must be a finite number > 0",
                                   conceptPath + ".points" );
                return false;
            }
            out.concepts.push_back( std::move( answerConcept ) );
        }
    }
    if ( answer.isMember( "misconceptions" ) ) {
        if ( !answer["misconceptions"].isArray() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "answer.misconceptions must be an array",
                               path + ".answer.misconceptions" );
            return false;
        }
        std::set<std::string> ids;
        for ( const auto &entry : answer["misconceptions"] ) {
            AnswerMisconception misconception;
            const std::string misconPath = path + ".answer.misconceptions";
            if ( !entry.isObject() || !stringMember( entry, "misconceptionId", misconception.misconceptionId ) ||
                 misconception.misconceptionId.empty() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "misconceptionId must be a non-empty string",
                                   misconPath + ".misconceptionId" );
                return false;
            }
            if ( !ids.insert( misconception.misconceptionId ).second ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid,
                                   "duplicate misconceptionId: " + misconception.misconceptionId,
                                   misconPath + ".misconceptionId" );
                return false;
            }
            if ( !entry.isMember( "patterns" ) || !entry["patterns"].isArray() || entry["patterns"].empty() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "misconception requires non-empty patterns",
                                   misconPath + ".patterns" );
                return false;
            }
            for ( const auto &pattern : entry["patterns"] ) {
                if ( !pattern.isString() || pattern.asString().empty() ) {
                    error = makeError( GraderErrorCode::SchemaShapeInvalid, "misconception patterns must be non-empty strings",
                                       misconPath + ".patterns" );
                    return false;
                }
                misconception.patterns.push_back( pattern.asString() );
            }
            if ( !finiteNumberMember( entry, "deductPoints", misconception.deductPoints ) || misconception.deductPoints < 0.0 ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "misconception.deductPoints must be a finite number >= 0",
                                   misconPath + ".deductPoints" );
                return false;
            }
            if ( entry.isMember( "explanation" ) )
                stringMember( entry, "explanation", misconception.explanation );
            out.misconceptions.push_back( std::move( misconception ) );
        }
    }
    if ( out.concepts.empty() && out.misconceptions.empty() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid,
                           "answer criterion requires at least one concept or one misconception", path + ".answer" );
        return false;
    }
    return true;
}

} // namespace

std::string criterionKindSpelling( CriterionKind kind )
{
    switch ( kind ) {
    case CriterionKind::Stage: return "stage";
    case CriterionKind::Metric: return "metric";
    case CriterionKind::Fact: return "fact";
    case CriterionKind::Answer: return "answer";
    }
    return {};
}

std::optional<CriterionKind> parseCriterionKind( const std::string &spelling )
{
    if ( spelling == "stage" )
        return CriterionKind::Stage;
    if ( spelling == "metric" )
        return CriterionKind::Metric;
    if ( spelling == "fact" )
        return CriterionKind::Fact;
    if ( spelling == "answer" )
        return CriterionKind::Answer;
    return std::nullopt;
}

std::string evidenceKindSpelling( EvidenceKind kind )
{
    switch ( kind ) {
    case EvidenceKind::Stage: return "stage";
    case EvidenceKind::Metric: return "metric";
    case EvidenceKind::ArtifactState: return "artifact_state";
    case EvidenceKind::Provenance: return "provenance";
    case EvidenceKind::Checkpoint: return "checkpoint";
    case EvidenceKind::Answer: return "answer";
    case EvidenceKind::ArtifactGrade: return "artifact_grade";
    case EvidenceKind::VerifierVerdict: return "verifier_verdict";
    case EvidenceKind::ReplayReadiness: return "replay_readiness";
    case EvidenceKind::Custom: return "custom";
    }
    return {};
}

std::optional<EvidenceKind> parseEvidenceKind( const std::string &spelling )
{
    if ( spelling == "stage" )
        return EvidenceKind::Stage;
    if ( spelling == "metric" )
        return EvidenceKind::Metric;
    if ( spelling == "artifact_state" )
        return EvidenceKind::ArtifactState;
    if ( spelling == "provenance" )
        return EvidenceKind::Provenance;
    if ( spelling == "checkpoint" )
        return EvidenceKind::Checkpoint;
    if ( spelling == "answer" )
        return EvidenceKind::Answer;
    if ( spelling == "artifact_grade" )
        return EvidenceKind::ArtifactGrade;
    if ( spelling == "verifier_verdict" )
        return EvidenceKind::VerifierVerdict;
    if ( spelling == "replay_readiness" )
        return EvidenceKind::ReplayReadiness;
    if ( spelling == "custom" )
        return EvidenceKind::Custom;
    return std::nullopt;
}

std::string outcomeStatusSpelling( OutcomeStatus status )
{
    switch ( status ) {
    case OutcomeStatus::Earned: return "earned";
    case OutcomeStatus::Partial: return "partial";
    case OutcomeStatus::NotEarned: return "not_earned";
    case OutcomeStatus::Indeterminate: return "indeterminate";
    case OutcomeStatus::Capped: return "capped";
    }
    return {};
}

std::string reportVerdictSpelling( ReportVerdict verdict )
{
    switch ( verdict ) {
    case ReportVerdict::Pass: return "pass";
    case ReportVerdict::Partial: return "partial";
    case ReportVerdict::Fail: return "fail";
    case ReportVerdict::Blocked: return "blocked";
    }
    return {};
}

// ---------------------------------------------------------------------------
// GradingRubric
// ---------------------------------------------------------------------------

bool GradingRubric::validate( GraderError &error ) const
{
    // Budgets first: refuse oversized rubrics before walking them.
    if ( budgets.maxCriteria < 1 || budgets.maxCriteria > kHardMaxCriteria ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid,
                           "budgets.maxCriteria must be between 1 and " + std::to_string( kHardMaxCriteria ),
                           "budgets.maxCriteria" );
        return false;
    }
    if ( budgets.maxEvidenceItems < 1 || budgets.maxEvidenceItems > kHardMaxEvidenceItems ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid,
                           "budgets.maxEvidenceItems must be between 1 and " + std::to_string( kHardMaxEvidenceItems ),
                           "budgets.maxEvidenceItems" );
        return false;
    }

    std::size_t criterionCount = 0;
    for ( const auto &dimension : dimensions )
        criterionCount += dimension.criteria.size();
    if ( criterionCount > static_cast<std::size_t>( budgets.maxCriteria ) ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid,
                           "rubric declares " + std::to_string( criterionCount ) + " criteria, over budget " +
                               std::to_string( budgets.maxCriteria ),
                           "budgets" );
        return false;
    }

    if ( rubricId.empty() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "rubricId must be a non-empty string", "rubricId" );
        return false;
    }
    if ( revision < 1 ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "revision must be >= 1", "revision" );
        return false;
    }
    if ( !( totalPoints > 0.0 ) || !std::isfinite( totalPoints ) ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "totalPoints must be a finite number > 0", "totalPoints" );
        return false;
    }
    if ( !std::isfinite( passingScore ) || passingScore < 0.0 || passingScore > totalPoints ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "passingScore must be within [0, totalPoints]",
                           "passingScore" );
        return false;
    }
    if ( dimensions.empty() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "rubric requires at least one dimension", "dimensions" );
        return false;
    }

    std::set<std::string> dimensionIds;
    std::set<std::string> criterionIds;
    std::set<std::string> stageKeys; // evidenceKeys of stage criteria (for orderedAfter refs)
    double weightSum = 0.0;
    for ( std::size_t d = 0; d < dimensions.size(); ++d ) {
        const Dimension &dimension = dimensions[d];
        const std::string dimPath = "dimensions[" + std::to_string( d ) + "]";
        if ( dimension.dimensionId.empty() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "dimensionId must be a non-empty string",
                               dimPath + ".dimensionId" );
            return false;
        }
        if ( !dimensionIds.insert( dimension.dimensionId ).second ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "duplicate dimensionId: " + dimension.dimensionId,
                               dimPath + ".dimensionId" );
            return false;
        }
        if ( dimension.criteria.empty() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "dimension requires at least one criterion",
                               dimPath + ".criteria" );
            return false;
        }
        double criteriaSum = 0.0;
        for ( std::size_t c = 0; c < dimension.criteria.size(); ++c ) {
            const Criterion &criterion = dimension.criteria[c];
            const std::string critPath = dimPath + ".criteria[" + std::to_string( c ) + "]";
            if ( criterion.criterionId.empty() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "criterionId must be a non-empty string",
                                   critPath + ".criterionId" );
                return false;
            }
            if ( !criterionIds.insert( criterion.criterionId ).second ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "duplicate criterionId: " + criterion.criterionId,
                                   critPath + ".criterionId" );
                return false;
            }
            if ( !std::isfinite( criterion.maxPoints ) || criterion.maxPoints <= 0.0 ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "maxPoints must be a finite number > 0",
                                   critPath + ".maxPoints" );
                return false;
            }
            criteriaSum += criterion.maxPoints;
            if ( ( criterion.kind == CriterionKind::Stage || criterion.kind == CriterionKind::Metric ||
                   criterion.kind == CriterionKind::Fact ) &&
                 criterion.evidenceKey.empty() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid,
                                   "criterion of kind " + criterionKindSpelling( criterion.kind ) +
                                       " requires an evidenceKey",
                                   critPath + ".evidenceKey" );
                return false;
            }
            if ( criterion.kind == CriterionKind::Stage ) {
                if ( criterion.stage.expectedState.empty() ) {
                    error = makeError( GraderErrorCode::SchemaShapeInvalid, "stage criterion requires expectedState",
                                       critPath + ".stage.expectedState" );
                    return false;
                }
                if ( criterion.stage.minDistinct < 1 ) {
                    error = makeError( GraderErrorCode::SchemaShapeInvalid, "minDistinct must be >= 1",
                                       critPath + ".stage.minDistinct" );
                    return false;
                }
                stageKeys.insert( criterion.evidenceKey );
            }
            if ( criterion.kind == CriterionKind::Metric && criterion.metric.mode == MetricExpectation::Mode::Equals &&
                 criterion.metric.tolerance < 0.0 ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "tolerance must be >= 0", critPath + ".metric.tolerance" );
                return false;
            }
            if ( criterion.kind == CriterionKind::Fact && criterion.fact.minCount < 1 ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "fact.minCount must be >= 1",
                                   critPath + ".fact.minCount" );
                return false;
            }
        }
        // Duplicate-id detection has already run; compare sums with an epsilon.
        if ( std::fabs( criteriaSum - dimension.weight ) > kEps ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid,
                               "dimension weight (" + std::to_string( dimension.weight ) +
                                   ") must equal the sum of its criteria maxPoints (" + std::to_string( criteriaSum ) + ")",
                               dimPath + ".weight" );
            return false;
        }
        weightSum += dimension.weight;
    }

    if ( std::fabs( weightSum - totalPoints ) > kEps ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid,
                           "dimension weights sum (" + std::to_string( weightSum ) + ") must equal totalPoints (" +
                               std::to_string( totalPoints ) + ")" );
        return false;
    }

    // Stage ordering references must point at declared stage criteria.
    for ( std::size_t d = 0; d < dimensions.size(); ++d ) {
        const Dimension &dimension = dimensions[d];
        for ( std::size_t c = 0; c < dimension.criteria.size(); ++c ) {
            const Criterion &criterion = dimension.criteria[c];
            const std::string critPath = "dimensions[" + std::to_string( d ) + "].criteria[" + std::to_string( c ) + "]";
            if ( criterion.kind != CriterionKind::Stage )
                continue;
            for ( const std::string &after : criterion.stage.orderedAfter ) {
                if ( stageKeys.count( after ) == 0 ) {
                    error = makeError( GraderErrorCode::SchemaShapeInvalid,
                                       "orderedAfter references unknown stage key: " + after, critPath + ".stage.orderedAfter" );
                    return false;
                }
            }
        }
    }

    // Hard constraints.
    std::set<std::string> constraintIds;
    for ( std::size_t i = 0; i < hardConstraints.size(); ++i ) {
        const HardConstraint &constraint = hardConstraints[i];
        const std::string path = "hardConstraints[" + std::to_string( i ) + "]";
        if ( constraint.constraintId.empty() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "constraintId must be a non-empty string",
                               path + ".constraintId" );
            return false;
        }
        if ( !constraintIds.insert( constraint.constraintId ).second ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "duplicate constraintId: " + constraint.constraintId,
                               path + ".constraintId" );
            return false;
        }
        if ( constraint.evidenceKey.empty() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "hard constraint requires an evidenceKey",
                               path + ".evidenceKey" );
            return false;
        }
        if ( constraint.effect == HardConstraint::Effect::Cap ) {
            if ( !std::isfinite( constraint.capPoints ) || constraint.capPoints < 0.0 ||
                 !( constraint.capPoints < totalPoints ) ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid,
                                   "cap constraint requires 0 <= capPoints < totalPoints (a cap that cannot bind is a rubric bug)",
                                   path + ".capPoints" );
                return false;
            }
        }
        if ( constraint.mode == HardConstraint::Mode::RequiredEvidence && constraint.fact.minCount < 1 ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "required constraint fact.minCount must be >= 1",
                               path + ".fact.minCount" );
            return false;
        }
    }

    // Alternate pathways.
    std::set<std::string> pathwayIds;
    for ( std::size_t i = 0; i < alternatePathways.size(); ++i ) {
        const AlternatePathway &pathway = alternatePathways[i];
        const std::string path = "alternatePathways[" + std::to_string( i ) + "]";
        if ( pathway.pathwayId.empty() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "pathwayId must be a non-empty string",
                               path + ".pathwayId" );
            return false;
        }
        if ( !pathwayIds.insert( pathway.pathwayId ).second ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "duplicate pathwayId: " + pathway.pathwayId,
                               path + ".pathwayId" );
            return false;
        }
        if ( pathway.criterionIds.empty() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "pathway requires at least one criterionId",
                               path + ".criterionIds" );
            return false;
        }
        for ( const std::string &criterionId : pathway.criterionIds ) {
            if ( criterionIds.count( criterionId ) == 0 ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid,
                                   "pathway references unknown criterionId: " + criterionId, path + ".criterionIds" );
                return false;
            }
        }
    }

    // requiredStages keys must reference declared stage criteria.
    for ( std::size_t i = 0; i < requiredStages.size(); ++i ) {
        const StageRequirement &requirement = requiredStages[i];
        const std::string path = "requiredStages[" + std::to_string( i ) + "]";
        if ( requirement.stageKey.empty() || stageKeys.count( requirement.stageKey ) == 0 ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid,
                               "requiredStages must reference a declared stage criterion evidenceKey: " + requirement.stageKey,
                               path + ".stageKey" );
            return false;
        }
        for ( const std::string &after : requirement.orderedAfter ) {
            if ( stageKeys.count( after ) == 0 ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid,
                                   "requiredStages orderedAfter references unknown stage key: " + after,
                                   path + ".orderedAfter" );
                return false;
            }
        }
    }

    return true;
}

Json::Value GradingRubric::toJson() const
{
    Json::Value doc{ Json::objectValue };
    doc["schema"] = kRubricSchemaId;
    doc["rubricId"] = rubricId;
    doc["revision"] = revision;
    doc["title"] = title;
    doc["totalPoints"] = totalPoints;
    doc["passingScore"] = passingScore;

    Json::Value dims{ Json::arrayValue };
    for ( const auto &dimension : dimensions ) {
        Json::Value dim{ Json::objectValue };
        dim["dimensionId"] = dimension.dimensionId;
        dim["title"] = dimension.title;
        dim["weight"] = dimension.weight;
        Json::Value criteria{ Json::arrayValue };
        for ( const auto &criterion : dimension.criteria ) {
            Json::Value c{ Json::objectValue };
            c["criterionId"] = criterion.criterionId;
            c["title"] = criterion.title;
            c["maxPoints"] = criterion.maxPoints;
            c["kind"] = criterionKindSpelling( criterion.kind );
            if ( !criterion.evidenceKey.empty() )
                c["evidenceKey"] = criterion.evidenceKey;
            if ( criterion.requireEvidenceKind )
                c["requireEvidenceKind"] = evidenceKindSpelling( *criterion.requireEvidenceKind );
            switch ( criterion.kind ) {
            case CriterionKind::Stage: {
                Json::Value stage{ Json::objectValue };
                stage["expectedState"] = criterion.stage.expectedState;
                if ( !criterion.stage.acceptedAlternatives.empty() ) {
                    Json::Value alternatives{ Json::arrayValue };
                    for ( const auto &alt : criterion.stage.acceptedAlternatives )
                        alternatives.append( alt );
                    stage["acceptedAlternatives"] = alternatives;
                }
                if ( !criterion.stage.orderedAfter.empty() ) {
                    Json::Value ordered{ Json::arrayValue };
                    for ( const auto &after : criterion.stage.orderedAfter )
                        ordered.append( after );
                    stage["orderedAfter"] = ordered;
                }
                stage["minDistinct"] = criterion.stage.minDistinct;
                c["stage"] = stage;
                break;
            }
            case CriterionKind::Metric: {
                Json::Value metric{ Json::objectValue };
                switch ( criterion.metric.mode ) {
                case MetricExpectation::Mode::AtLeast: metric["mode"] = "at_least"; break;
                case MetricExpectation::Mode::AtMost: metric["mode"] = "at_most"; break;
                case MetricExpectation::Mode::Equals: metric["mode"] = "equals"; break;
                case MetricExpectation::Mode::Range: metric["mode"] = "range"; break;
                }
                metric["value"] = criterion.metric.value;
                if ( criterion.metric.mode == MetricExpectation::Mode::Range )
                    metric["valueMax"] = criterion.metric.valueMax;
                if ( criterion.metric.tolerance != 0.0 )
                    metric["tolerance"] = criterion.metric.tolerance;
                if ( criterion.metric.hasLinearWindow ) {
                    Json::Value window{ Json::objectValue };
                    window["from"] = criterion.metric.windowFrom;
                    window["to"] = criterion.metric.windowTo;
                    metric["linearWindow"] = window;
                }
                c["metric"] = metric;
                break;
            }
            case CriterionKind::Fact: {
                Json::Value fact{ Json::objectValue };
                if ( !criterion.fact.expectedState.empty() )
                    fact["expectedState"] = criterion.fact.expectedState;
                if ( !criterion.fact.requiredFacts.empty() ) {
                    Json::Value required{ Json::objectValue };
                    for ( const auto &entry : criterion.fact.requiredFacts )
                        required[entry.first] = entry.second;
                    fact["requiredFacts"] = required;
                }
                fact["minCount"] = criterion.fact.minCount;
                c["fact"] = fact;
                break;
            }
            case CriterionKind::Answer: {
                Json::Value answer{ Json::objectValue };
                answer["questionId"] = criterion.answer.questionId;
                Json::Value concepts{ Json::arrayValue };
                for ( const auto &answerConcept : criterion.answer.concepts ) {
                    Json::Value conceptJson{ Json::objectValue };
                    conceptJson["conceptId"] = answerConcept.conceptId;
                    conceptJson["description"] = answerConcept.description;
                    Json::Value groups{ Json::arrayValue };
                    for ( const auto &group : answerConcept.keywordGroups ) {
                        Json::Value groupJson{ Json::arrayValue };
                        for ( const auto &keyword : group )
                            groupJson.append( keyword );
                        groups.append( groupJson );
                    }
                    conceptJson["keywordGroups"] = groups;
                    conceptJson["points"] = answerConcept.points;
                    concepts.append( conceptJson );
                }
                answer["concepts"] = concepts;
                Json::Value misconceptions{ Json::arrayValue };
                for ( const auto &misconception : criterion.answer.misconceptions ) {
                    Json::Value mis{ Json::objectValue };
                    mis["misconceptionId"] = misconception.misconceptionId;
                    Json::Value patterns{ Json::arrayValue };
                    for ( const auto &pattern : misconception.patterns )
                        patterns.append( pattern );
                    mis["patterns"] = patterns;
                    mis["deductPoints"] = misconception.deductPoints;
                    mis["explanation"] = misconception.explanation;
                    misconceptions.append( mis );
                }
                answer["misconceptions"] = misconceptions;
                c["answer"] = answer;
                break;
            }
            }
            if ( !criterion.studentHint.empty() || !criterion.teacherHint.empty() ) {
                Json::Value hints{ Json::objectValue };
                if ( !criterion.studentHint.empty() )
                    hints["student"] = criterion.studentHint;
                if ( !criterion.teacherHint.empty() )
                    hints["teacher"] = criterion.teacherHint;
                c["hints"] = hints;
            }
            criteria.append( c );
        }
        dim["criteria"] = criteria;
        dims.append( dim );
    }
    doc["dimensions"] = dims;

    if ( !hardConstraints.empty() ) {
        Json::Value constraints{ Json::arrayValue };
        for ( const auto &constraint : hardConstraints ) {
            Json::Value hc{ Json::objectValue };
            hc["constraintId"] = constraint.constraintId;
            hc["title"] = constraint.title;
            hc["mode"] = constraint.mode == HardConstraint::Mode::ForbiddenEvidence ? "forbidden" : "required";
            hc["evidenceKey"] = constraint.evidenceKey;
            hc["effect"] = constraint.effect == HardConstraint::Effect::Cap ? "cap" : "zero";
            if ( constraint.effect == HardConstraint::Effect::Cap )
                hc["capPoints"] = constraint.capPoints;
            if ( !constraint.explanation.empty() )
                hc["explanation"] = constraint.explanation;
            if ( constraint.mode == HardConstraint::Mode::RequiredEvidence &&
                 ( !constraint.fact.expectedState.empty() || !constraint.fact.requiredFacts.empty() ) ) {
                Json::Value fact{ Json::objectValue };
                if ( !constraint.fact.expectedState.empty() )
                    fact["expectedState"] = constraint.fact.expectedState;
                if ( !constraint.fact.requiredFacts.empty() ) {
                    Json::Value required{ Json::objectValue };
                    for ( const auto &entry : constraint.fact.requiredFacts )
                        required[entry.first] = entry.second;
                    fact["requiredFacts"] = required;
                }
                hc["fact"] = fact;
            }
            constraints.append( hc );
        }
        doc["hardConstraints"] = constraints;
    }

    if ( !requiredStages.empty() ) {
        Json::Value stages{ Json::arrayValue };
        for ( const auto &requirement : requiredStages ) {
            Json::Value stage{ Json::objectValue };
            stage["stageKey"] = requirement.stageKey;
            if ( !requirement.expectedState.empty() )
                stage["expectedState"] = requirement.expectedState;
            if ( !requirement.orderedAfter.empty() ) {
                Json::Value ordered{ Json::arrayValue };
                for ( const auto &after : requirement.orderedAfter )
                    ordered.append( after );
                stage["orderedAfter"] = ordered;
            }
            stages.append( stage );
        }
        doc["requiredStages"] = stages;
    }

    if ( !alternatePathways.empty() ) {
        Json::Value pathways{ Json::arrayValue };
        for ( const auto &pathway : alternatePathways ) {
            Json::Value p{ Json::objectValue };
            p["pathwayId"] = pathway.pathwayId;
            p["title"] = pathway.title;
            Json::Value ids{ Json::arrayValue };
            for ( const auto &id : pathway.criterionIds )
                ids.append( id );
            p["criterionIds"] = ids;
            pathways.append( p );
        }
        doc["alternatePathways"] = pathways;
    }

    Json::Value budgetsJson{ Json::objectValue };
    budgetsJson["maxCriteria"] = budgets.maxCriteria;
    budgetsJson["maxEvidenceItems"] = budgets.maxEvidenceItems;
    doc["budgets"] = budgetsJson;

    return doc;
}

std::optional<GradingRubric> GradingRubric::fromJson( const Json::Value &doc, GraderError &error )
{
    if ( !doc.isObject() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "rubric document must be a JSON object" );
        return std::nullopt;
    }
    if ( !doc.isMember( "schema" ) || !doc["schema"].isString() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "rubric schema must be a string member", "schema" );
        return std::nullopt;
    }
    if ( doc["schema"].asString() != kRubricSchemaId ) {
        error = makeError( GraderErrorCode::SchemaVersionUnsupported,
                           "unsupported rubric schema: " + doc["schema"].asString() + " (expected " + kRubricSchemaId + ")",
                           "schema" );
        return std::nullopt;
    }

    GradingRubric rubric;
    if ( !stringMember( doc, "rubricId", rubric.rubricId ) ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "rubricId must be a non-empty string", "rubricId" );
        return std::nullopt;
    }
    if ( !doc.isMember( "revision" ) ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "revision must be an integer", "revision" );
        return std::nullopt;
    }
    int revision = 0;
    if ( !boundedIntMember( doc, "revision", revision ) ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid,
                           "revision must be an integer within 32-bit range", "revision" );
        return std::nullopt;
    }
    rubric.revision = revision;
    if ( doc.isMember( "title" ) )
        stringMember( doc, "title", rubric.title );
    if ( !finiteNumberMember( doc, "totalPoints", rubric.totalPoints ) ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "totalPoints must be a finite number", "totalPoints" );
        return std::nullopt;
    }
    if ( doc.isMember( "passingScore" ) ) {
        if ( !finiteNumberMember( doc, "passingScore", rubric.passingScore ) ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "passingScore must be a finite number", "passingScore" );
            return std::nullopt;
        }
    }

    if ( !doc.isMember( "dimensions" ) || !doc["dimensions"].isArray() || doc["dimensions"].empty() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "rubric requires a non-empty dimensions array", "dimensions" );
        return std::nullopt;
    }

    for ( const auto &dimJson : doc["dimensions"] ) {
        Dimension dimension;
        const std::string dimPath = "dimensions[" + std::to_string( rubric.dimensions.size() ) + "]";
        if ( !dimJson.isObject() || !stringMember( dimJson, "dimensionId", dimension.dimensionId ) ||
             dimension.dimensionId.empty() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "dimensionId must be a non-empty string",
                               dimPath + ".dimensionId" );
            return std::nullopt;
        }
        if ( dimJson.isMember( "title" ) )
            stringMember( dimJson, "title", dimension.title );
        if ( !finiteNumberMember( dimJson, "weight", dimension.weight ) ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "dimension weight must be a finite number",
                               dimPath + ".weight" );
            return std::nullopt;
        }
        if ( !dimJson.isMember( "criteria" ) || !dimJson["criteria"].isArray() || dimJson["criteria"].empty() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "dimension requires a non-empty criteria array",
                               dimPath + ".criteria" );
            return std::nullopt;
        }
        for ( const auto &critJson : dimJson["criteria"] ) {
            Criterion criterion;
            const std::string critPath = dimPath + ".criteria[" + std::to_string( dimension.criteria.size() ) + "]";
            if ( !critJson.isObject() || !stringMember( critJson, "criterionId", criterion.criterionId ) ||
                 criterion.criterionId.empty() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "criterionId must be a non-empty string",
                                   critPath + ".criterionId" );
                return std::nullopt;
            }
            if ( critJson.isMember( "title" ) )
                stringMember( critJson, "title", criterion.title );
            if ( !finiteNumberMember( critJson, "maxPoints", criterion.maxPoints ) || criterion.maxPoints <= 0.0 ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "maxPoints must be a finite number > 0",
                                   critPath + ".maxPoints" );
                return std::nullopt;
            }
            std::string kindSpelling;
            if ( !stringMember( critJson, "kind", kindSpelling ) ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "criterion kind must be a string", critPath + ".kind" );
                return std::nullopt;
            }
            auto kind = parseCriterionKind( kindSpelling );
            if ( !kind ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "unknown criterion kind: " + kindSpelling,
                                   critPath + ".kind" );
                return std::nullopt;
            }
            criterion.kind = *kind;
            if ( critJson.isMember( "evidenceKey" ) )
                stringMember( critJson, "evidenceKey", criterion.evidenceKey );
            if ( critJson.isMember( "requireEvidenceKind" ) ) {
                std::string evidenceSpelling;
                stringMember( critJson, "requireEvidenceKind", evidenceSpelling );
                auto evidenceKind = parseEvidenceKind( evidenceSpelling );
                if ( !evidenceKind ) {
                    error = makeError( GraderErrorCode::SchemaShapeInvalid, "unknown requireEvidenceKind: " + evidenceSpelling,
                                       critPath + ".requireEvidenceKind" );
                    return std::nullopt;
                }
                criterion.requireEvidenceKind = *evidenceKind;
            }
            switch ( criterion.kind ) {
            case CriterionKind::Stage:
                if ( !parseStageExpectation( critJson, critPath, criterion.stage, error ) )
                    return std::nullopt;
                break;
            case CriterionKind::Metric:
                if ( !parseMetricExpectation( critJson, critPath, criterion.metric, error ) )
                    return std::nullopt;
                break;
            case CriterionKind::Fact:
                if ( !parseFactExpectation( critJson, critPath, "fact", criterion.fact, error ) )
                    return std::nullopt;
                break;
            case CriterionKind::Answer:
                if ( !parseAnswerExpectation( critJson, critPath, criterion.answer, error ) )
                    return std::nullopt;
                break;
            }
            if ( critJson.isMember( "hints" ) && critJson["hints"].isObject() ) {
                stringMember( critJson["hints"], "student", criterion.studentHint );
                stringMember( critJson["hints"], "teacher", criterion.teacherHint );
            }
            dimension.criteria.push_back( std::move( criterion ) );
        }
        rubric.dimensions.push_back( std::move( dimension ) );
    }

    if ( doc.isMember( "hardConstraints" ) ) {
        if ( !doc["hardConstraints"].isArray() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "hardConstraints must be an array", "hardConstraints" );
            return std::nullopt;
        }
        for ( const auto &hcJson : doc["hardConstraints"] ) {
            HardConstraint constraint;
            const std::string path = "hardConstraints[" + std::to_string( rubric.hardConstraints.size() ) + "]";
            if ( !hcJson.isObject() || !stringMember( hcJson, "constraintId", constraint.constraintId ) ||
                 constraint.constraintId.empty() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "constraintId must be a non-empty string",
                                   path + ".constraintId" );
                return std::nullopt;
            }
            if ( hcJson.isMember( "title" ) )
                stringMember( hcJson, "title", constraint.title );
            std::string modeSpelling;
            if ( !stringMember( hcJson, "mode", modeSpelling ) ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "hard constraint mode must be a string", path + ".mode" );
                return std::nullopt;
            }
            if ( modeSpelling == "forbidden" )
                constraint.mode = HardConstraint::Mode::ForbiddenEvidence;
            else if ( modeSpelling == "required" )
                constraint.mode = HardConstraint::Mode::RequiredEvidence;
            else {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "unknown hard constraint mode: " + modeSpelling,
                                   path + ".mode" );
                return std::nullopt;
            }
            if ( !stringMember( hcJson, "evidenceKey", constraint.evidenceKey ) || constraint.evidenceKey.empty() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "hard constraint requires an evidenceKey",
                                   path + ".evidenceKey" );
                return std::nullopt;
            }
            std::string effectSpelling;
            if ( !stringMember( hcJson, "effect", effectSpelling ) ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "hard constraint effect must be a string",
                                   path + ".effect" );
                return std::nullopt;
            }
            if ( effectSpelling == "cap" )
                constraint.effect = HardConstraint::Effect::Cap;
            else if ( effectSpelling == "zero" )
                constraint.effect = HardConstraint::Effect::Zero;
            else {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "unknown hard constraint effect: " + effectSpelling,
                                   path + ".effect" );
                return std::nullopt;
            }
            if ( hcJson.isMember( "capPoints" ) ) {
                if ( !finiteNumberMember( hcJson, "capPoints", constraint.capPoints ) ) {
                    error = makeError( GraderErrorCode::SchemaShapeInvalid, "capPoints must be a finite number",
                                       path + ".capPoints" );
                    return std::nullopt;
                }
            }
            if ( hcJson.isMember( "explanation" ) )
                stringMember( hcJson, "explanation", constraint.explanation );
            if ( !parseFactExpectation( hcJson, path, "fact", constraint.fact, error ) )
                return std::nullopt;
            rubric.hardConstraints.push_back( std::move( constraint ) );
        }
    }

    if ( doc.isMember( "requiredStages" ) ) {
        if ( !doc["requiredStages"].isArray() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "requiredStages must be an array", "requiredStages" );
            return std::nullopt;
        }
        for ( const auto &stageJson : doc["requiredStages"] ) {
            StageRequirement requirement;
            const std::string path = "requiredStages[" + std::to_string( rubric.requiredStages.size() ) + "]";
            if ( !stageJson.isObject() || !stringMember( stageJson, "stageKey", requirement.stageKey ) ||
                 requirement.stageKey.empty() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "stageKey must be a non-empty string",
                                   path + ".stageKey" );
                return std::nullopt;
            }
            if ( stageJson.isMember( "expectedState" ) )
                stringMember( stageJson, "expectedState", requirement.expectedState );
            if ( stageJson.isMember( "orderedAfter" ) && stageJson["orderedAfter"].isArray() ) {
                for ( const auto &entry : stageJson["orderedAfter"] ) {
                    if ( entry.isString() )
                        requirement.orderedAfter.push_back( entry.asString() );
                }
            }
            rubric.requiredStages.push_back( std::move( requirement ) );
        }
    }

    if ( doc.isMember( "alternatePathways" ) ) {
        if ( !doc["alternatePathways"].isArray() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "alternatePathways must be an array",
                               "alternatePathways" );
            return std::nullopt;
        }
        for ( const auto &pathwayJson : doc["alternatePathways"] ) {
            AlternatePathway pathway;
            const std::string path = "alternatePathways[" + std::to_string( rubric.alternatePathways.size() ) + "]";
            if ( !pathwayJson.isObject() || !stringMember( pathwayJson, "pathwayId", pathway.pathwayId ) ||
                 pathway.pathwayId.empty() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "pathwayId must be a non-empty string",
                                   path + ".pathwayId" );
                return std::nullopt;
            }
            if ( pathwayJson.isMember( "title" ) )
                stringMember( pathwayJson, "title", pathway.title );
            if ( !pathwayJson.isMember( "criterionIds" ) || !pathwayJson["criterionIds"].isArray() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "pathway requires a criterionIds array",
                                   path + ".criterionIds" );
                return std::nullopt;
            }
            for ( const auto &entry : pathwayJson["criterionIds"] ) {
                if ( !entry.isString() || entry.asString().empty() ) {
                    error = makeError( GraderErrorCode::SchemaShapeInvalid, "criterionIds entries must be non-empty strings",
                                       path + ".criterionIds" );
                    return std::nullopt;
                }
                pathway.criterionIds.push_back( entry.asString() );
            }
            rubric.alternatePathways.push_back( std::move( pathway ) );
        }
    }

    if ( doc.isMember( "budgets" ) ) {
        if ( !doc["budgets"].isObject() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "budgets must be an object", "budgets" );
            return std::nullopt;
        }
        // jsoncpp's asInt() throws for |v| beyond 2^31-1 even when
        // isIntegral() is true — budgets are read through a bounded reader so
        // a hostile magnitude is a typed refusal, never an exception.
        int maxCriteria = 0;
        if ( doc["budgets"].isMember( "maxCriteria" ) ) {
            if ( !boundedIntMember( doc["budgets"], "maxCriteria", maxCriteria ) ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid,
                                   "budgets.maxCriteria must be an integer within 32-bit range",
                                   "budgets.maxCriteria" );
                return std::nullopt;
            }
            rubric.budgets.maxCriteria = maxCriteria;
        }
        if ( doc["budgets"].isMember( "maxEvidenceItems" ) ) {
            int maxEvidenceItems = 0;
            if ( !boundedIntMember( doc["budgets"], "maxEvidenceItems", maxEvidenceItems ) ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid,
                                   "budgets.maxEvidenceItems must be an integer within 32-bit range",
                                   "budgets.maxEvidenceItems" );
                return std::nullopt;
            }
            rubric.budgets.maxEvidenceItems = maxEvidenceItems;
        }
    }

    if ( !rubric.validate( error ) )
        return std::nullopt;
    return rubric;
}

// ---------------------------------------------------------------------------
// GradeEvidence
// ---------------------------------------------------------------------------

bool GradeEvidence::validate( GraderError &error ) const
{
    if ( items.size() > static_cast<std::size_t>( kHardMaxEvidenceItems ) ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid,
                           "evidence bundle holds " + std::to_string( items.size() ) +
                               " items, over the hard cap " + std::to_string( kHardMaxEvidenceItems ),
                           "items" );
        return false;
    }
    std::set<std::string> ids;
    for ( std::size_t i = 0; i < items.size(); ++i ) {
        const GradeEvidenceItem &item = items[i];
        const std::string path = "items[" + std::to_string( i ) + "]";
        if ( item.evidenceId.empty() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "evidenceId must be a non-empty string",
                               path + ".evidenceId" );
            return false;
        }
        if ( !ids.insert( item.evidenceId ).second ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "duplicate evidenceId: " + item.evidenceId,
                               path + ".evidenceId" );
            return false;
        }
        if ( item.key.empty() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "evidence key must be a non-empty string", path + ".key" );
            return false;
        }
        if ( item.kind == EvidenceKind::Metric && ( !item.hasValue || !std::isfinite( item.value ) ) ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid,
                               "metric evidence " + item.evidenceId + " requires a finite value", path + ".value" );
            return false;
        }
        if ( item.kind == EvidenceKind::Answer ) {
            const Json::Value &text = item.facts["answerText"];
            if ( !text.isString() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid,
                                   "answer evidence " + item.evidenceId +
                                       " requires a string facts.answerText member",
                                   path + ".facts.answerText" );
                return false;
            }
            if ( text.asString().size() > static_cast<std::size_t>( kMaxAnswerBytes ) ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid,
                                   "answer evidence " + item.evidenceId + " exceeds the " +
                                       std::to_string( kMaxAnswerBytes ) + "-byte cap",
                                   path + ".facts.answerText" );
                return false;
            }
        }
    }
    return true;
}

Json::Value GradeEvidence::toJson() const
{
    Json::Value doc{ Json::objectValue };
    doc["schema"] = kEvidenceSchemaId;
    Json::Value subjectJson{ Json::objectValue };
    if ( !subject.experimentId.empty() )
        subjectJson["experimentId"] = subject.experimentId;
    if ( !subject.runId.empty() )
        subjectJson["runId"] = subject.runId;
    doc["subject"] = subjectJson;
    Json::Value itemsJson{ Json::arrayValue };
    for ( const auto &item : items ) {
        Json::Value itemJson{ Json::objectValue };
        itemJson["evidenceId"] = item.evidenceId;
        itemJson["kind"] = evidenceKindSpelling( item.kind );
        itemJson["key"] = item.key;
        if ( !item.state.empty() )
            itemJson["state"] = item.state;
        if ( item.hasValue )
            itemJson["value"] = item.value;
        itemJson["facts"] = item.facts;
        if ( !item.source.empty() )
            itemJson["source"] = item.source;
        if ( !item.recordedAtUtc.empty() )
            itemJson["recordedAtUtc"] = item.recordedAtUtc;
        itemsJson.append( itemJson );
    }
    doc["items"] = itemsJson;
    return doc;
}

std::optional<GradeEvidence> GradeEvidence::fromJson( const Json::Value &doc, GraderError &error )
{
    if ( !doc.isObject() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "evidence document must be a JSON object" );
        return std::nullopt;
    }
    if ( !doc.isMember( "schema" ) || !doc["schema"].isString() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "evidence schema must be a string member", "schema" );
        return std::nullopt;
    }
    if ( doc["schema"].asString() != kEvidenceSchemaId ) {
        error = makeError( GraderErrorCode::SchemaVersionUnsupported,
                           "unsupported evidence schema: " + doc["schema"].asString() + " (expected " + kEvidenceSchemaId + ")",
                           "schema" );
        return std::nullopt;
    }

    GradeEvidence evidence;
    if ( doc.isMember( "subject" ) && doc["subject"].isObject() ) {
        stringMember( doc["subject"], "experimentId", evidence.subject.experimentId );
        stringMember( doc["subject"], "runId", evidence.subject.runId );
    }

    if ( !doc.isMember( "items" ) || !doc["items"].isArray() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "evidence document requires an items array", "items" );
        return std::nullopt;
    }
    if ( doc["items"].size() > static_cast<Json::ArrayIndex>( kHardMaxEvidenceItems ) ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid,
                           "evidence bundle over the hard cap of " + std::to_string( kHardMaxEvidenceItems ) + " items",
                           "items" );
        return std::nullopt;
    }

    for ( const auto &itemJson : doc["items"] ) {
        GradeEvidenceItem item;
        const std::string path = "items[" + std::to_string( evidence.items.size() ) + "]";
        if ( !itemJson.isObject() || !stringMember( itemJson, "evidenceId", item.evidenceId ) || item.evidenceId.empty() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "evidenceId must be a non-empty string",
                               path + ".evidenceId" );
            return std::nullopt;
        }
        std::string kindSpelling;
        if ( !stringMember( itemJson, "kind", kindSpelling ) ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "evidence kind must be a string", path + ".kind" );
            return std::nullopt;
        }
        auto kind = parseEvidenceKind( kindSpelling );
        if ( !kind ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "unknown evidence kind: " + kindSpelling,
                               path + ".kind" );
            return std::nullopt;
        }
        item.kind = *kind;
        if ( !stringMember( itemJson, "key", item.key ) || item.key.empty() ) {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "evidence key must be a non-empty string", path + ".key" );
            return std::nullopt;
        }
        stringMember( itemJson, "state", item.state );
        if ( itemJson.isMember( "value" ) ) {
            if ( !finiteNumberMember( itemJson, "value", item.value ) ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid,
                                   "evidence item " + item.evidenceId + " has a non-finite value", path + ".value" );
                return std::nullopt;
            }
            item.hasValue = true;
        }
        if ( itemJson.isMember( "facts" ) ) {
            if ( !itemJson["facts"].isObject() ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid, "evidence facts must be an object", path + ".facts" );
                return std::nullopt;
            }
            item.facts = itemJson["facts"];
        } else {
            item.facts = Json::Value{ Json::objectValue };
        }
        stringMember( itemJson, "source", item.source );
        stringMember( itemJson, "recordedAtUtc", item.recordedAtUtc );
        evidence.items.push_back( std::move( item ) );
    }

    if ( !evidence.validate( error ) )
        return std::nullopt;
    return evidence;
}

// ---------------------------------------------------------------------------
// GradeReport
// ---------------------------------------------------------------------------

Json::Value GradeReport::toBodyJson() const
{
    Json::Value doc{ Json::objectValue };
    doc["schema"] = kReportSchemaId;
    Json::Value subjectJson{ Json::objectValue };
    if ( !subject.experimentId.empty() )
        subjectJson["experimentId"] = subject.experimentId;
    if ( !subject.runId.empty() )
        subjectJson["runId"] = subject.runId;
    doc["subject"] = subjectJson;
    Json::Value rubricRef{ Json::objectValue };
    rubricRef["rubricId"] = rubricId;
    rubricRef["revision"] = rubricRevision;
    rubricRef["digest"] = rubricDigest;
    doc["rubricRef"] = rubricRef;
    doc["evidenceDigest"] = evidenceDigest;
    doc["score"] = score;
    doc["totalPoints"] = totalPoints;
    doc["passingScore"] = passingScore;
    doc["verdict"] = reportVerdictSpelling( verdict );

    Json::Value constraints{ Json::arrayValue };
    for ( const auto &constraint : hardConstraintOutcomes ) {
        Json::Value hc{ Json::objectValue };
        hc["constraintId"] = constraint.constraintId;
        hc["violated"] = constraint.violated;
        Json::Value evidenceIds{ Json::arrayValue };
        for ( const auto &id : constraint.evidenceIds )
            evidenceIds.append( id );
        hc["evidenceIds"] = evidenceIds;
        hc["effect"] = constraint.effect;
        hc["explanation"] = constraint.explanation;
        constraints.append( hc );
    }
    doc["hardConstraintOutcomes"] = constraints;

    Json::Value dims{ Json::arrayValue };
    for ( const auto &dimension : dimensions ) {
        Json::Value dim{ Json::objectValue };
        dim["dimensionId"] = dimension.dimensionId;
        dim["weight"] = dimension.weight;
        dim["earned"] = dimension.earned;
        Json::Value criteria{ Json::arrayValue };
        for ( const auto &criterion : dimension.criteria ) {
            Json::Value c{ Json::objectValue };
            c["criterionId"] = criterion.criterionId;
            c["maxPoints"] = criterion.maxPoints;
            c["rawEarned"] = criterion.rawEarned;
            c["earned"] = criterion.earned;
            c["status"] = outcomeStatusSpelling( criterion.status );
            Json::Value reasonCodes{ Json::arrayValue };
            for ( const auto &reason : criterion.reasonCodes )
                reasonCodes.append( reason );
            c["reasonCodes"] = reasonCodes;
            Json::Value evidenceIds{ Json::arrayValue };
            for ( const auto &id : criterion.evidenceIds )
                evidenceIds.append( id );
            c["evidenceIds"] = evidenceIds;
            c["explanation"] = criterion.explanation;
            criteria.append( c );
        }
        dim["criteria"] = criteria;
        dims.append( dim );
    }
    doc["dimensions"] = dims;

    Json::Value stages{ Json::arrayValue };
    for ( const auto &stage : requiredStageOutcomes ) {
        Json::Value stageJson{ Json::objectValue };
        stageJson["stageKey"] = stage.stageKey;
        stageJson["satisfied"] = stage.satisfied;
        Json::Value evidenceIds{ Json::arrayValue };
        for ( const auto &id : stage.evidenceIds )
            evidenceIds.append( id );
        stageJson["evidenceIds"] = evidenceIds;
        stageJson["explanation"] = stage.explanation;
        stages.append( stageJson );
    }
    doc["requiredStageOutcomes"] = stages;

    Json::Value pathways{ Json::arrayValue };
    for ( const auto &pathway : matchedPathways )
        pathways.append( pathway );
    doc["matchedPathways"] = pathways;

    return doc;
}

Json::Value GradeReport::toJson() const
{
    Json::Value doc = toBodyJson();
    if ( !digest.empty() ) {
        // Engine-built reports carry their digest; honor it so the document
        // round-trips byte-identically.
        doc["digest"] = digest;
    } else {
        GraderError ignored;
        const auto body = canonicalizeJson( doc, ignored );
        // Fail visible: a body that cannot be canonicalized (non-finite
        // numbers) gets NO digest — an unverifiable report must not
        // masquerade as digest-sealed with a sha256 of the empty string.
        // verifyDigest() and fromJson() consumers refuse empty digests.
        doc["digest"] = body.has_value() ? sha256Hex( *body ) : std::string{};
    }
    return doc;
}

std::optional<GradeReport> GradeReport::fromJson( const Json::Value &doc, GraderError &error )
{
    if ( !doc.isObject() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "report document must be a JSON object" );
        return std::nullopt;
    }
    if ( !doc.isMember( "schema" ) || !doc["schema"].isString() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "report schema must be a string member", "schema" );
        return std::nullopt;
    }
    if ( doc["schema"].asString() != kReportSchemaId ) {
        error = makeError( GraderErrorCode::SchemaVersionUnsupported,
                           "unsupported report schema: " + doc["schema"].asString() + " (expected " + kReportSchemaId + ")",
                           "schema" );
        return std::nullopt;
    }

    GradeReport report;
    if ( doc.isMember( "subject" ) && doc["subject"].isObject() ) {
        stringMember( doc["subject"], "experimentId", report.subject.experimentId );
        stringMember( doc["subject"], "runId", report.subject.runId );
    }
    if ( doc.isMember( "rubricRef" ) && doc["rubricRef"].isObject() ) {
        const Json::Value &ref = doc["rubricRef"];
        stringMember( ref, "rubricId", report.rubricId );
        if ( ref.isMember( "revision" ) ) {
            int revision = 0;
            if ( !boundedIntMember( ref, "revision", revision ) ) {
                error = makeError( GraderErrorCode::SchemaShapeInvalid,
                                   "rubricRef.revision must be an integer within 32-bit range",
                                   "rubricRef.revision" );
                return std::nullopt;
            }
            report.rubricRevision = revision;
        }
        stringMember( ref, "digest", report.rubricDigest );
    }
    stringMember( doc, "evidenceDigest", report.evidenceDigest );
    // A present-but-non-numeric score/totalPoints/passingScore is a typed
    // refusal — silently defaulting it to 0.0 would put a fabricated number
    // into the value object (digest binding stays the tamper gate either
    // way; absent members keep their defaults for schema tolerance).
    if ( doc.isMember( "score" ) && !finiteNumberMember( doc, "score", report.score ) ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "score must be a finite number", "score" );
        return std::nullopt;
    }
    if ( doc.isMember( "totalPoints" ) && !finiteNumberMember( doc, "totalPoints", report.totalPoints ) ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "totalPoints must be a finite number", "totalPoints" );
        return std::nullopt;
    }
    if ( doc.isMember( "passingScore" ) && !finiteNumberMember( doc, "passingScore", report.passingScore ) ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "passingScore must be a finite number", "passingScore" );
        return std::nullopt;
    }
    std::string verdictSpelling;
    if ( stringMember( doc, "verdict", verdictSpelling ) ) {
        if ( verdictSpelling == "pass" )
            report.verdict = ReportVerdict::Pass;
        else if ( verdictSpelling == "partial" )
            report.verdict = ReportVerdict::Partial;
        else if ( verdictSpelling == "fail" )
            report.verdict = ReportVerdict::Fail;
        else if ( verdictSpelling == "blocked" )
            report.verdict = ReportVerdict::Blocked;
        else {
            error = makeError( GraderErrorCode::SchemaShapeInvalid, "unknown report verdict: " + verdictSpelling, "verdict" );
            return std::nullopt;
        }
    }
    stringMember( doc, "digest", report.digest );
    return report;
}

bool GradeReport::verifyDigest( GraderError &error ) const
{
    if ( digest.empty() ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "report carries no digest", "digest" );
        return false;
    }
    GraderError ignored;
    const std::optional<std::string> body = canonicalizeJson( toBodyJson(), ignored );
    if ( !body ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid, "report body cannot be canonicalized", "digest" );
        return false;
    }
    const std::string computed = sha256Hex( *body );
    if ( computed != digest ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid,
                           "report digest mismatch: body was modified after grading (expected " + digest + ", computed " +
                               computed + ")",
                           "digest" );
        return false;
    }
    return true;
}

} // namespace sicnu::grader
