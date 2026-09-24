// grader_render.cpp — deterministic report views. See grader_render.h for
// the audience contracts (student views leak no golden rubric internals).
#include "grader/grader_render.h"

namespace sicnu::grader {

namespace {

const Criterion *findCriterion( const GradingRubric &rubric, const std::string &criterionId )
{
    for ( const auto &dimension : rubric.dimensions )
        for ( const auto &criterion : dimension.criteria )
            if ( criterion.criterionId == criterionId )
                return &criterion;
    return nullptr;
}

const Dimension *findDimension( const GradingRubric &rubric, const std::string &dimensionId )
{
    for ( const auto &dimension : rubric.dimensions )
        if ( dimension.dimensionId == dimensionId )
            return &dimension;
    return nullptr;
}

Json::Value subjectJson( const EvidenceSubject &subject )
{
    Json::Value json{ Json::objectValue };
    if ( !subject.experimentId.empty() )
        json["experimentId"] = subject.experimentId;
    if ( !subject.runId.empty() )
        json["runId"] = subject.runId;
    return json;
}

} // namespace

Json::Value renderStudentFeedback( const GradeReport &report, const GradingRubric *rubric )
{
    Json::Value view{ Json::objectValue };
    view["schema"] = "sicnu.grader.student-feedback/1";
    view["rubricId"] = report.rubricId;
    view["subject"] = subjectJson( report.subject );
    view["score"] = report.score;
    view["totalPoints"] = report.totalPoints;
    view["passingScore"] = report.passingScore;
    view["verdict"] = reportVerdictSpelling( report.verdict );

    // Blocking constraints are student-visible facts (their PAYOUT changed);
    // ids only — the teacher's explanation text is not the student's.
    if ( report.verdict == ReportVerdict::Blocked ) {
        Json::Value blocked{ Json::arrayValue };
        for ( const auto &constraint : report.hardConstraintOutcomes )
            if ( constraint.violated )
                blocked.append( constraint.constraintId );
        view["blockedBy"] = blocked;
    }

    Json::Value dimensions{ Json::arrayValue };
    for ( const auto &dimension : report.dimensions ) {
        Json::Value dimJson{ Json::objectValue };
        dimJson["dimensionId"] = dimension.dimensionId;
        if ( rubric ) {
            if ( const Dimension *declared = findDimension( *rubric, dimension.dimensionId ) )
                dimJson["title"] = declared->title;
        }
        dimJson["earned"] = dimension.earned;
        Json::Value criteria{ Json::arrayValue };
        for ( const auto &criterion : dimension.criteria ) {
            Json::Value c{ Json::objectValue };
            c["criterionId"] = criterion.criterionId;
            if ( rubric ) {
                if ( const Criterion *declared = findCriterion( *rubric, criterion.criterionId ) )
                    c["title"] = declared->title;
            }
            c["maxPoints"] = criterion.maxPoints;
            c["earned"] = criterion.earned;
            c["status"] = outcomeStatusSpelling( criterion.status );
            c["explanation"] = criterion.explanation;
            // Student hints join ONLY for criteria that did not earn full
            // points — a hint on an earned criterion is noise.
            if ( rubric && criterion.status != OutcomeStatus::Earned ) {
                if ( const Criterion *declared = findCriterion( *rubric, criterion.criterionId ) )
                    if ( !declared->studentHint.empty() )
                        c["studentHint"] = declared->studentHint;
            }
            criteria.append( c );
        }
        dimJson["criteria"] = criteria;
        dimensions.append( dimJson );
    }
    view["dimensions"] = dimensions;
    return view;
}

Json::Value renderTeacherDiagnostics( const GradeReport &report, const GradingRubric &rubric )
{
    Json::Value view{ Json::objectValue };
    view["schema"] = "sicnu.grader.teacher-diagnostics/1";
    view["rubricId"] = report.rubricId;
    view["rubricRevision"] = report.rubricRevision;
    view["subject"] = subjectJson( report.subject );
    view["score"] = report.score;
    view["totalPoints"] = report.totalPoints;
    view["passingScore"] = report.passingScore;
    view["verdict"] = reportVerdictSpelling( report.verdict );
    view["rubricDigest"] = report.rubricDigest;
    view["evidenceDigest"] = report.evidenceDigest;

    Json::Value indeterminate{ Json::arrayValue };

    Json::Value dimensions{ Json::arrayValue };
    for ( const auto &dimension : report.dimensions ) {
        Json::Value dimJson{ Json::objectValue };
        dimJson["dimensionId"] = dimension.dimensionId;
        dimJson["weight"] = dimension.weight;
        dimJson["earned"] = dimension.earned;
        if ( const Dimension *declared = findDimension( rubric, dimension.dimensionId ) )
            dimJson["title"] = declared->title;
        Json::Value criteria{ Json::arrayValue };
        for ( const auto &criterion : dimension.criteria ) {
            Json::Value c{ Json::objectValue };
            c["criterionId"] = criterion.criterionId;
            c["maxPoints"] = criterion.maxPoints;
            c["rawEarned"] = criterion.rawEarned;
            c["earned"] = criterion.earned;
            c["status"] = outcomeStatusSpelling( criterion.status );
            Json::Value reasons{ Json::arrayValue };
            for ( const auto &reason : criterion.reasonCodes )
                reasons.append( reason );
            c["reasonCodes"] = reasons;
            Json::Value evidenceIds{ Json::arrayValue };
            for ( const auto &id : criterion.evidenceIds )
                evidenceIds.append( id );
            c["evidenceIds"] = evidenceIds;
            c["explanation"] = criterion.explanation;
            if ( const Criterion *declared = findCriterion( rubric, criterion.criterionId ) ) {
                c["title"] = declared->title;
                if ( !declared->teacherHint.empty() )
                    c["teacherHint"] = declared->teacherHint;
                if ( !declared->studentHint.empty() )
                    c["studentHint"] = declared->studentHint;
            }
            criteria.append( c );

            if ( criterion.status == OutcomeStatus::Indeterminate ) {
                Json::Value triage{ Json::objectValue };
                triage["criterionId"] = criterion.criterionId;
                triage["explanation"] = criterion.explanation;
                if ( !criterion.reasonCodes.empty() )
                    triage["reasonCode"] = criterion.reasonCodes.front();
                indeterminate.append( triage );
            }
        }
        dimJson["criteria"] = criteria;
        dimensions.append( dimJson );
    }
    view["dimensions"] = dimensions;

    Json::Value constraints{ Json::arrayValue };
    for ( const auto &constraint : report.hardConstraintOutcomes ) {
        Json::Value hc{ Json::objectValue };
        hc["constraintId"] = constraint.constraintId;
        hc["violated"] = constraint.violated;
        hc["effect"] = constraint.effect;
        Json::Value evidenceIds{ Json::arrayValue };
        for ( const auto &id : constraint.evidenceIds )
            evidenceIds.append( id );
        hc["evidenceIds"] = evidenceIds;
        hc["explanation"] = constraint.explanation;
        constraints.append( hc );
    }
    view["hardConstraints"] = constraints;

    Json::Value stages{ Json::arrayValue };
    for ( const auto &stage : report.requiredStageOutcomes ) {
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
    view["requiredStages"] = stages;

    Json::Value pathways{ Json::arrayValue };
    for ( const auto &pathway : report.matchedPathways )
        pathways.append( pathway );
    view["matchedPathways"] = pathways;

    view["indeterminate"] = indeterminate;
    return view;
}

Json::Value renderMachineSummary( const GradeReport &report )
{
    Json::Value view{ Json::objectValue };
    view["schema"] = "sicnu.grader.machine-summary/1";
    view["rubricId"] = report.rubricId;
    view["rubricRevision"] = report.rubricRevision;
    view["subject"] = subjectJson( report.subject );
    view["score"] = report.score;
    view["totalPoints"] = report.totalPoints;
    view["passingScore"] = report.passingScore;
    view["verdict"] = reportVerdictSpelling( report.verdict );
    view["digest"] = report.digest;
    view["evidenceDigest"] = report.evidenceDigest;
    view["rubricDigest"] = report.rubricDigest;

    Json::Value pathways{ Json::arrayValue };
    for ( const auto &pathway : report.matchedPathways )
        pathways.append( pathway );
    view["matchedPathways"] = pathways;

    Json::Value violated{ Json::arrayValue };
    for ( const auto &constraint : report.hardConstraintOutcomes ) {
        if ( !constraint.violated )
            continue;
        Json::Value hc{ Json::objectValue };
        hc["constraintId"] = constraint.constraintId;
        hc["effect"] = constraint.effect;
        Json::Value evidenceIds{ Json::arrayValue };
        for ( const auto &id : constraint.evidenceIds )
            evidenceIds.append( id );
        hc["evidenceIds"] = evidenceIds;
        hc["explanation"] = constraint.explanation;
        violated.append( hc );
    }
    view["violatedConstraints"] = violated;
    return view;
}

} // namespace sicnu::grader
