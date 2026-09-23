// src/planner/planner_core.cpp — deterministic goal→plan baseline.
#include "planner/planner_core.h"

#include "contracts/scientific_contract.h"
#include "planner/json_util.h"
#include "planner/planner_constraints.h"
#include "planner/planner_rules.h"
#include "planner/sha256_util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <optional>
#include <set>

namespace sicnu::planner {

namespace {

using ContractsContract = sicnu::contracts::ScientificContract;

std::string lowerCopy( std::string text )
{
    std::transform( text.begin(), text.end(), text.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return text;
}

bool vocabularyAllows( const std::vector<std::string> &allowlist, const std::string &value )
{
    return allowlist.empty()
           || std::find( allowlist.begin(), allowlist.end(), value ) != allowlist.end();
}

/// Deterministic candidate ranking: cost class asc, then operator id.
void rankCandidates( std::vector<PlannerCapability> &candidates )
{
    std::stable_sort( candidates.begin(), candidates.end(),
                      []( const PlannerCapability &a, const PlannerCapability &b ) {
                          const int ra = costClassRank( a.costClass );
                          const int rb = costClassRank( b.costClass );
                          if ( ra != rb )
                              return ra < rb;
                          return a.operatorId < b.operatorId;
                      } );
}

/// First contracts-verified capability of @p family, cheapest-operator order.
std::optional<PlannerCapability> firstVerifiableCapability(
    const CapabilityProvider &provider, const PlanningContext &context,
    const std::string &family )
{
    auto entries = provider.capabilitiesForFamily( family );
    const PlannerCapability *best = nullptr;
    for ( const auto &entry : entries )
    {
        if ( entry.family != family || entry.operatorId.empty() )
            continue;
        if ( !satisfiesFamilyAllowlist( context, family ) )
            continue;
        if ( !isKnownCostClass( entry.costClass ) )
            continue;
        if ( !capabilityAgreesWithContracts( entry ) )
            continue;
        if ( !best
             || std::make_pair( costClassRank( entry.costClass ), entry.operatorId )
                    < std::make_pair( costClassRank( best->costClass ), best->operatorId ) )
            best = &entry;
    }
    if ( !best )
        return std::nullopt;
    return *best;
}

/// Deterministic question list: content-deduped, stable-sorted, typed ids.
void finalizeQuestions( std::vector<PlanOpenQuestion> &questions )
{
    std::vector<PlanOpenQuestion> deduped;
    for ( auto &question : questions )
    {
        bool seen = false;
        for ( const auto &existing : deduped )
        {
            if ( existing.kind == question.kind && existing.detail == question.detail )
            {
                seen = true;
                break;
            }
        }
        if ( !seen )
            deduped.push_back( question );
    }
    std::stable_sort( deduped.begin(), deduped.end(),
                      []( const PlanOpenQuestion &a, const PlanOpenQuestion &b ) {
                          if ( a.kind != b.kind )
                              return a.kind < b.kind;
                          if ( a.blocking != b.blocking )
                              return a.blocking > b.blocking;
                          return a.detail < b.detail;
                      } );
    int n = 1;
    for ( auto &question : deduped )
    {
        char buffer[24];
        std::snprintf( buffer, sizeof( buffer ), "q-%02d", n++ );
        question.questionId = std::string( buffer ) + "-" + question.kind;
    }
    questions = std::move( deduped );
}

bool hasBlockingQuestion( const ScientificPlan &plan )
{
    return std::any_of( plan.openQuestions.begin(), plan.openQuestions.end(),
                        []( const PlanOpenQuestion &q ) { return q.blocking; } );
}

struct AssetView
{
    const PlannerAssetFacts *facts = nullptr;
    bool consumable = false; // ready hard fact
};

/// Asset classification + typed asset-gap questions (integration.md §3:
/// only ready assets back hard preconditions; everything else is a question).
std::vector<AssetView> classifyAssets( const PlanningContext &context,
                                       std::vector<PlanOpenQuestion> &questions )
{
    std::vector<AssetView> views;
    int readyRasterCount = 0;
    for ( const auto &asset : context.assets )
    {
        AssetView view;
        view.facts = &asset;
        view.consumable = asset.kind == "raster" && isHardFactAssetState( asset.state );
        if ( view.consumable )
        {
            ++readyRasterCount;
        }
        else if ( asset.kind == "raster" )
        {
            const std::string detail =
                "asset \"" + asset.ref + "\" is declared " + asset.state
                + " — it cannot back the plan as a hard fact";
            questions.push_back( PlanOpenQuestion{ "", "insufficient_data", true, detail,
                                                   "resolve the asset state and re-plan" } );
        }
        views.push_back( view );
    }
    return views;
}

/// Bridges one numeric-domain gap via contracts-verified calibration facts.
std::optional<PlannerCapability> findBridge( const CapabilityProvider &provider,
                                             const PlanningContext &context,
                                             const std::string &fromDomain,
                                             const std::string &toDomain,
                                             std::vector<PlanOpenQuestion> &questions )
{
    auto entries = provider.capabilitiesForFamily( "calibration" );
    const PlannerCapability *best = nullptr;
    bool sawAuthorityConflict = false;
    for ( const auto &entry : entries )
    {
        if ( entry.family != "calibration" || entry.inputDomain != fromDomain
             || entry.outputDomain != toDomain )
            continue;
        if ( !satisfiesFamilyAllowlist( context, "calibration" ) )
            continue;
        if ( !capabilityAgreesWithContracts( entry ) )
        {
            sawAuthorityConflict = true;
            continue;
        }
        if ( !best
             || std::make_pair( costClassRank( entry.costClass ), entry.operatorId )
                    < std::make_pair( costClassRank( best->costClass ), best->operatorId ) )
            best = &entry;
    }
    if ( sawAuthorityConflict && !best )
    {
        questions.push_back( PlanOpenQuestion{
            "", "ambiguity", false,
            "calibration candidates for " + fromDomain + "→" + toDomain
                + " contradict the contracts registry and were excluded",
            "fix the provider facts; contracts is the authority" } );
    }
    if ( !best )
        return std::nullopt;
    return *best;
}

std::string stepIdFor( const std::string &role, int occurrence )
{
    char buffer[16];
    std::snprintf( buffer, sizeof( buffer ), "-%02d", occurrence );
    return "step-" + role + buffer;
}

/// One candidate plan around one analysis capability.
ScientificPlan buildCandidatePlan( const ScientificGoal &goal, const PlanningContext &context,
                                   const CapabilityProvider &provider,
                                   const PlannerCapability &analysis,
                                   const std::vector<AssetView> &assets,
                                   const std::vector<PlanOpenQuestion> &globalQuestions )
{
    ScientificPlan plan;
    plan.goalId = goal.goalId;
    plan.goalKind = goal.kind;
    plan.rulesRevision = kPlannerRulesRevision;
    plan.modeKind = context.mode.kind;
    plan.autonomy = context.mode.autonomy;
    plan.verdict = "feasible";
    std::vector<PlanOpenQuestion> questions = globalQuestions;

    const bool hasConsumable =
        std::any_of( assets.begin(), assets.end(),
                     []( const AssetView &view ) { return view.consumable; } );

    int occurrence = 0;
    std::string lastStepId;

    if ( !hasConsumable )
    {
        questions.push_back( PlanOpenQuestion{
            "", "insufficient_data", true,
            "no ready raster asset backs this plan"
                + std::string( context.assets.empty()
                                   ? " (the planning context declares no assets)"
                                   : "" ),
            "add a ready asset to the context or resolve the declared ones" } );
        plan.verdict = "feasible_with_gaps";
    }

    // ---- import stage (required when anything is consumable) ------------
    if ( hasConsumable )
    {
        auto importCapability = firstVerifiableCapability( provider, context, "data_import" );
        if ( importCapability )
        {
            PlannerStep importStep;
            importStep.role = "import";
            importStep.family = "data_import";
            importStep.operatorId = importCapability->operatorId;
            importStep.costClass = importCapability->costClass;
            importStep.estimatedRamMb = importCapability->estimatedRamMb;
            importStep.deterministic = importCapability->deterministic;
            importStep.stepId = stepIdFor( "import", ++occurrence );
            for ( const auto &view : assets )
            {
                if ( !view.consumable )
                    continue;
                importStep.inputs.push_back( StepInput{ "", view.facts->ref, "input" } );
                importStep.preconditions.push_back(
                    StepPrecondition{ "asset_state", view.facts->ref, "", 0,
                                      "asset ready at plan time" } );
            }
            plan.steps.push_back( importStep );
            lastStepId = importStep.stepId;
        }
        else
        {
            questions.push_back( PlanOpenQuestion{
                "", "decision_required", true,
                "no data_import capability is available; assets cannot be staged lawfully",
                "provide an import capability or pre-imported ready assets" } );
            plan.verdict = "feasible_with_gaps";
        }
    }

    // Per-asset current numeric domain of the consumable assets.
    std::map<std::string, std::string> currentDomain;
    for ( const auto &view : assets )
    {
        if ( view.consumable )
            currentDomain[view.facts->ref] = view.facts->numericDomain;
    }

    // ---- grid alignment gate ---------------------------------------------
    std::set<std::string> crsSet;
    std::set<double> resolutionSet;
    for ( const auto &view : assets )
    {
        if ( !view.consumable )
            continue;
        if ( !view.facts->crs.empty() )
            crsSet.insert( lowerCopy( view.facts->crs ) );
        if ( view.facts->resolutionM >= 0 )
            resolutionSet.insert( view.facts->resolutionM );
    }
    if ( crsSet.size() > 1 || resolutionSet.size() > 1 )
    {
        auto alignment = firstVerifiableCapability( provider, context, "alignment" );
        if ( alignment )
        {
            PlannerStep alignStep;
            alignStep.role = "preprocess";
            alignStep.family = "alignment";
            alignStep.operatorId = alignment->operatorId;
            alignStep.costClass = alignment->costClass;
            alignStep.estimatedRamMb = alignment->estimatedRamMb;
            alignStep.deterministic = alignment->deterministic;
            alignStep.stepId = stepIdFor( "preprocess", ++occurrence );
            alignStep.preconditions.push_back( StepPrecondition{
                "grid", "", "", 0,
                "cross-scene grid mismatch (crs/resolution) must be resolved first" } );
            for ( const auto &view : assets )
            {
                if ( view.consumable )
                    alignStep.inputs.push_back( StepInput{ "", view.facts->ref, "input" } );
            }
            plan.steps.push_back( alignStep );
            lastStepId = alignStep.stepId;
        }
        else
        {
            questions.push_back( PlanOpenQuestion{
                "", "decision_required", true,
                "scenes sit on different grids (crs/resolution) and no alignment capability is "
                "available to the planner",
                "provide an alignment capability or pre-aligned scenes" } );
            plan.verdict = "feasible_with_gaps";
        }
    }

    // ---- calibration gate -------------------------------------------------
    const std::string requiredInput =
        analysis.inputDomain.empty() ? std::string( "any" ) : analysis.inputDomain;
    if ( requiredInput != "any" )
    {
        std::map<std::string, int> gaps; // fromDomain → affected asset count
        for ( const auto &[ref, domain] : currentDomain )
        {
            if ( domain != requiredInput )
                ++gaps[domain];
        }
        for ( const auto &[fromDomain, count] : gaps )
        {
            auto bridge = findBridge( provider, context, fromDomain, requiredInput, questions );
            if ( bridge )
            {
                PlannerStep calibStep;
                calibStep.role = "preprocess";
                calibStep.family = "calibration";
                calibStep.operatorId = bridge->operatorId;
                calibStep.costClass = bridge->costClass;
                calibStep.estimatedRamMb = bridge->estimatedRamMb;
                calibStep.deterministic = bridge->deterministic;
                calibStep.stepId = stepIdFor( "preprocess", ++occurrence );
                for ( auto &[ref, domain] : currentDomain )
                {
                    if ( domain == fromDomain )
                    {
                        calibStep.inputs.push_back( StepInput{ "", ref, "input" } );
                        calibStep.expectedTransitions.push_back(
                            ExpectedTransition{ ref, fromDomain, requiredInput } );
                        domain = requiredInput;
                    }
                }
                plan.steps.push_back( calibStep );
                lastStepId = calibStep.stepId;
            }
            else
            {
                questions.push_back( PlanOpenQuestion{
                    "", "insufficient_data", true,
                    "analysis requires " + requiredInput + " but " + std::to_string( count )
                        + " asset(s) are " + fromDomain
                        + " and no contracts-verified calibration maps " + fromDomain + "→"
                        + requiredInput,
                    "provide a calibration capability for this domain pair" } );
                plan.verdict = "feasible_with_gaps";
            }
        }
    }

    // ---- analysis stage -----------------------------------------------------
    std::string analysisStepId;
    {
        PlannerStep analyzeStep;
        analyzeStep.role = "analyze";
        analyzeStep.family = "analysis";
        analyzeStep.operatorId = analysis.operatorId;
        analyzeStep.costClass = analysis.costClass;
        analyzeStep.estimatedRamMb = analysis.estimatedRamMb;
        analyzeStep.deterministic = analysis.deterministic;
        analyzeStep.stepId = stepIdFor( "analyze", ++occurrence );
        analyzeStep.studentDecision = context.mode.kind == "teaching"
                                      && ( context.mode.autonomy != "full"
                                           || context.mode.studentDecisionDefault );
        if ( !lastStepId.empty() )
            analyzeStep.inputs.push_back( StepInput{ lastStepId, "", "input" } );
        else
        {
            for ( const auto &view : assets )
            {
                if ( view.consumable )
                    analyzeStep.inputs.push_back( StepInput{ "", view.facts->ref, "input" } );
            }
        }
        for ( const auto &[ref, domain] : currentDomain )
        {
            analyzeStep.preconditions.push_back(
                StepPrecondition{ "numeric_domain", ref,
                                  requiredInput == "any" ? domain : requiredInput, 0,
                                  "analysis consumes the bridged data scale" } );
            if ( !analysis.outputDomain.empty() && analysis.outputDomain != domain )
                analyzeStep.expectedTransitions.push_back(
                    ExpectedTransition{ ref, domain, analysis.outputDomain } );
        }
        if ( !analysis.deterministic )
        {
            const ContractsContract *contract = contractForOperator( analysis.operatorId );
            analyzeStep.riskNotes.push_back(
                std::string( "stochastic operator; seed policy: " )
                + ( contract && !contract->seedPolicy.empty() ? contract->seedPolicy : "unknown" ) );
        }
        plan.steps.push_back( analyzeStep );
        analysisStepId = analyzeStep.stepId;
        lastStepId = analysisStepId;
    }

    // ---- verify stage ---------------------------------------------------------
    const bool needsVerification = !goal.acceptanceCriteria.empty()
                                   || context.quality.requireUncertainty
                                   || context.quality.requireValidationSplit
                                   || context.quality.minAccuracy >= 0;
    if ( needsVerification )
    {
        auto verification = firstVerifiableCapability( provider, context, "verification" );
        if ( verification )
        {
            PlannerStep verifyStep;
            verifyStep.role = "verify";
            verifyStep.family = "verification";
            verifyStep.operatorId = verification->operatorId;
            verifyStep.costClass = verification->costClass;
            verifyStep.estimatedRamMb = verification->estimatedRamMb;
            verifyStep.deterministic = verification->deterministic;
            verifyStep.stepId = stepIdFor( "verify", ++occurrence );
            verifyStep.inputs.push_back( StepInput{ analysisStepId, "", "input" } );
            for ( const auto &criterion : goal.acceptanceCriteria )
                verifyStep.verifierTargets.push_back(
                    VerifierTarget{ criterion.check, criterion.target } );
            if ( context.quality.minAccuracy >= 0 )
                verifyStep.verifierTargets.push_back( VerifierTarget{
                    "product accuracy",
                    "accuracy >= " + std::to_string( context.quality.minAccuracy ) } );
            if ( context.quality.requireValidationSplit )
                verifyStep.verifierTargets.push_back(
                    VerifierTarget{ "validation split held out from fitting", "" } );
            if ( context.quality.requireUncertainty )
                verifyStep.verifierTargets.push_back(
                    VerifierTarget{ "uncertainty estimate accompanies the product", "" } );
            plan.steps.push_back( verifyStep );
            lastStepId = verifyStep.stepId;
        }
        else
        {
            questions.push_back( PlanOpenQuestion{
                "", "decision_required", true,
                "goal declares acceptance criteria / quality requirements but no verification "
                "capability is available to the planner",
                "provide a verification capability or relax the quality block" } );
            plan.verdict = "feasible_with_gaps";
        }
    }

    // ---- publish stage ----------------------------------------------------------
    auto publication = firstVerifiableCapability( provider, context, "publication" );
    if ( publication )
    {
        PlannerStep publishStep;
        publishStep.role = "publish";
        publishStep.family = "publication";
        publishStep.operatorId = publication->operatorId;
        publishStep.costClass = publication->costClass;
        publishStep.estimatedRamMb = publication->estimatedRamMb;
        publishStep.deterministic = publication->deterministic;
        publishStep.stepId = stepIdFor( "publish", ++occurrence );
        publishStep.inputs.push_back(
            StepInput{ lastStepId.empty() ? analysisStepId : lastStepId, "", "input" } );
        plan.steps.push_back( publishStep );
    }
    else
    {
        questions.push_back( PlanOpenQuestion{
            "", "decision_required", true,
            "no publication capability is available; the plan cannot name how the product is "
            "published",
            "provide a publication capability" } );
        plan.verdict = "feasible_with_gaps";
    }

    // ---- costs + typed stochastic risks ------------------------------------------
    std::vector<std::string> costClasses;
    long long totalRam = 0;
    for ( const auto &step : plan.steps )
    {
        costClasses.push_back( step.costClass );
        totalRam += step.estimatedRamMb;
        if ( !step.deterministic )
        {
            plan.risks.push_back( PlanRisk{ "stochastic_operator",
                                            "step " + step.stepId
                                                + " uses stochastic operator " + step.operatorId,
                                            step.stepId } );
        }
    }
    plan.cost.aggregateCostClass = aggregateCostClass( costClasses );
    plan.cost.totalEstimatedRamMb = totalRam;

    finalizeQuestions( questions );
    plan.openQuestions = std::move( questions );
    if ( !plan.openQuestions.empty() && plan.verdict == "feasible" )
        plan.verdict = "feasible_with_gaps";
    return plan;
}

} // namespace

const ContractsContract *contractForOperator( const std::string &operatorId )
{
    return sicnu::contracts::findScientificContract( operatorId );
}

bool capabilityAgreesWithContracts( const PlannerCapability &capability )
{
    const ContractsContract *contract = contractForOperator( capability.operatorId );
    if ( !contract )
        return false;
    const std::string declaredInput =
        capability.inputDomain.empty() ? std::string( "any" ) : capability.inputDomain;
    const std::string contractInput =
        contract->inputDomain.empty() ? std::string( "any" ) : contract->inputDomain;
    if ( declaredInput != "any" && contractInput != "any" && declaredInput != contractInput )
        return false;
    if ( !capability.outputDomain.empty() && !contract->outputDomain.empty()
         && capability.outputDomain != contract->outputDomain )
        return false;
    return true;
}

PlanningResult planScientificWork( const ScientificGoal &goal, const PlanningContext &context,
                                   const PlannerProviders &providers )
{
    PlanningResult result;

    const auto goalProblems = validateScientificGoal( goal );

    ScientificPlan fallback;
    fallback.goalId = goal.goalId;
    fallback.goalKind = goal.kind;
    fallback.rulesRevision = kPlannerRulesRevision;
    fallback.modeKind = context.mode.kind;
    fallback.autonomy = context.mode.autonomy;
    fallback.cost = PlanCost{ "low", 0 };

    if ( !goalProblems.empty() || providers.capability == nullptr )
    {
        fallback.verdict = "infeasible";
        fallback.verdictReasons = goalProblems;
        if ( providers.capability == nullptr )
            fallback.verdictReasons.push_back(
                "fail-closed: capability provider seam is not wired" );
        fallback.planId = "plan-" + scientificPlanFingerprint( fallback );
        result.candidates.push_back( std::move( fallback ) );
        return result;
    }

    std::vector<PlanOpenQuestion> globalQuestions;
    const auto assetViews = classifyAssets( context, globalQuestions );
    if ( goal.temporalScope && goal.temporalScope->minScenes > 0 )
    {
        const int readyCount = static_cast<int>( std::count_if(
            assetViews.begin(), assetViews.end(),
            []( const AssetView &view ) { return view.consumable; } ) );
        if ( readyCount < goal.temporalScope->minScenes )
        {
            globalQuestions.push_back( PlanOpenQuestion{
                "", "insufficient_data", true,
                "goal declares min " + std::to_string( goal.temporalScope->minScenes )
                    + " scenes but only " + std::to_string( readyCount )
                    + " ready raster asset(s) are available",
                "add scenes or lower the declared minimum" } );
        }
    }

    // Analysis candidates: provider facts narrowed by the rule table and the
    // linked contracts authority.
    std::vector<PlannerCapability> analysisCandidates;
    std::vector<PlanOpenQuestion> constraintExclusions;
    const auto &allowedOutputs = allowedAnalysisOutputsForGoalKind( goal.kind );
    for ( const auto &entry : providers.capability->capabilitiesForFamily( "analysis" ) )
    {
        if ( entry.family != "analysis" || entry.operatorId.empty() )
            continue;
        if ( !isLawfulCandidate( context, entry ) )
        {
            constraintExclusions.push_back( PlanOpenQuestion{
                "", "decision_required", false,
                "analysis capability \"" + entry.operatorId
                    + "\" was excluded by constraints (forbidden operator / determinism "
                      "requirement / family allowlist)",
                "relax the constraint if this capability should be planable" } );
            continue;
        }
        if ( !isKnownCostClass( entry.costClass ) )
        {
            globalQuestions.push_back( PlanOpenQuestion{
                "", "ambiguity", false,
                "analysis capability \"" + entry.operatorId
                    + "\" carries an unknown cost class and was excluded",
                "fix the provider facts" } );
            continue;
        }
        if ( !capabilityAgreesWithContracts( entry ) )
        {
            globalQuestions.push_back( PlanOpenQuestion{
                "", "ambiguity", false,
                "analysis capability \"" + entry.operatorId
                    + "\" contradicts the contracts registry and was excluded",
                "fix the provider facts; contracts is the authority" } );
            continue;
        }
        if ( !vocabularyAllows( allowedOutputs, entry.outputDomain ) )
            continue;
        analysisCandidates.push_back( entry );
    }
    rankCandidates( analysisCandidates );

    if ( analysisCandidates.empty() )
    {
        globalQuestions.push_back( PlanOpenQuestion{
            "", "insufficient_data", true,
            "no lawful analysis capability for goal kind \"" + goal.kind + "\"",
            "wire a capability provider that knows a suitable analysis operator" } );
        for ( auto &question : constraintExclusions )
            question.blocking = true; // no lawful sibling: someone must relax a constraint
        for ( const auto &question : constraintExclusions )
            globalQuestions.push_back( question );
        fallback.verdict = "infeasible";
        fallback.verdictReasons.push_back(
            "no analysis capability matches the goal kind's output contract" );
        fallback.openQuestions = std::move( globalQuestions );
        finalizeQuestions( fallback.openQuestions );
        fallback.planId = "plan-" + scientificPlanFingerprint( fallback );
        result.candidates.push_back( std::move( fallback ) );
        return result;
    }

    // Narrowing while a lawful sibling exists is silent by design (the
    // sibling IS the answer); exclusions surface as blocking decisions only
    // in the no-lawful-candidate path below.
    for ( const auto &candidate : analysisCandidates )
    {
        result.candidates.push_back( buildCandidatePlan( goal, context, *providers.capability,
                                                         candidate, assetViews,
                                                         globalQuestions ) );
    }

    // The primary carries the merged DATA-question view so a consumer reading
    // one document still sees every typed gap (deduped, deterministic ids).
    // Budget questions are candidate-specific and are applied AFTER the merge
    // so a sibling's overrun never stains the primary.
    {
        auto &primary = result.candidates.front();
        std::vector<PlanOpenQuestion> merged = primary.openQuestions;
        for ( size_t i = 1; i < result.candidates.size(); ++i )
        {
            for ( const auto &question : result.candidates[i].openQuestions )
                merged.push_back( question );
        }
        finalizeQuestions( merged );
        primary.openQuestions = std::move( merged );
        if ( !primary.openQuestions.empty() && primary.verdict == "feasible" )
            primary.verdict = "feasible_with_gaps";
    }
    for ( auto &plan : result.candidates )
        applyResourceBudget( context, plan );

    // Alternatives on the primary (slice D): why the primary won, why each
    // sibling lost, candidateIndex into PlanningResult::candidates.
    if ( result.candidates.size() > 1 )
    {
        auto &primary = result.candidates.front();
        const int primaryRank = costClassRank( primary.cost.aggregateCostClass );
        for ( size_t i = 1; i < result.candidates.size(); ++i )
        {
            const auto &sibling = result.candidates[i];
            std::string siblingOperator;
            for ( const auto &step : sibling.steps )
            {
                if ( step.role == "analyze" )
                    siblingOperator = step.operatorId;
            }
            PlanAlternative alternative;
            alternative.alternativeId = "alt-" + std::to_string( i );
            alternative.summary = "analysis via " + siblingOperator;
            const int siblingRank = costClassRank( sibling.cost.aggregateCostClass );
            alternative.whyNot = siblingRank > primaryRank
                                     ? "higher aggregate cost class"
                                     : "ranked after the primary by deterministic tiebreak";
            alternative.candidateIndex = static_cast<int>( i );
            primary.alternatives.push_back( alternative );
        }
    }

    for ( auto &plan : result.candidates )
        plan.planId = "plan-" + scientificPlanFingerprint( plan );

    return result;
}

} // namespace sicnu::planner
