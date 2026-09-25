#include "preflight/engine.h"

#include "preflight/sha256.h"

#include <algorithm>
#include <map>

namespace sicnu::preflight {
namespace {

bool isValidRunMode( const std::string &mode )
{
    return mode == "teaching" || mode == "agent";
}

/// The typed truncation marker. Budgets are configuration, not named risk:
/// an over-budget report cannot be accepted into validity, so the marker is
/// a block — silent partial coverage is exactly the failure mode this engine
/// exists to prevent.
PreflightFinding budgetMarker( const std::string &subject,
                               const Json::Value &evidence )
{
    PreflightFinding f;
    f.code = "SPF_BUDGET_EXCEEDED";
    f.severity = PreflightSeverity::Block;
    f.ruleId = "preflight.engine";
    f.ruleRevision = 1;
    f.domain = "budget";
    f.subject = subject;
    f.evidence = evidence;
    f.basis = "observed";
    f.humanExplanation =
        "The report hit a deterministic budget cap; unlisted results were dropped and are "
        "counted here instead of being hidden.";
    f.machineExplanation["expectation"] = "results within budget";
    f.machineExplanation["actual"] = "budget exceeded";
    return f;
}

/// Canonical (sorted-key, compact) JSON of one finding — the total-order
/// tie-break so no two distinct findings can sort equal.
std::string canonicalFindingText( const PreflightFinding &f )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["commentStyle"] = "None";
    builder["precision"] = 12;
    builder["precisionType"] = "significant";
    builder["emitUTF8"] = true;
    return Json::writeString( builder, f.toJson() );
}

} // namespace

// ---- PreflightRequest -------------------------------------------------------

Json::Value PreflightRequest::toRequestJson() const
{
    Json::Value j( Json::objectValue );
    j["operator"] = operatorId;
    j["operator_params"] = operatorParams.isObject() ? operatorParams
                                                     : Json::Value( Json::objectValue );
    j["intent"] = intent;
    j["mode"] = mode;

    Json::Value in( Json::arrayValue );
    for ( const auto &slot : inputs )
    {
        Json::Value entry( Json::objectValue );
        entry["slot"] = slot.first;
        entry["ref"] = slot.second;
        in.append( entry );
    }
    j["inputs"] = in;

    Json::Value acks( Json::arrayValue );
    for ( const auto &code : acknowledgements )
        acks.append( code );
    j["acknowledgements"] = acks;
    Json::Value subjects( Json::arrayValue );
    for ( const auto &ack : acknowledgedSubjects )
    {
        Json::Value entry( Json::objectValue );
        entry["code"] = ack.code;
        entry["subject"] = ack.subject;
        subjects.append( entry );
    }
    j["acknowledged_subjects"] = subjects;

    // Budgets belong in the digest: they change what gets truncated, so two
    // runs that differ only here must not share a provenance handle.
    j["budgets"] = budgets.toJson();
    return j;
}

std::string computeRequestDigest( const PreflightRequest &request )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["commentStyle"] = "None";
    builder["precision"] = 12;
    builder["precisionType"] = "significant";
    builder["emitUTF8"] = true;
    return shortDigest( Json::writeString( builder, request.toRequestJson() ) );
}

std::string computeRulesRevision(
    const std::vector<std::pair<std::string, int>> &idRevisions )
{
    std::vector<std::string> lines;
    lines.reserve( idRevisions.size() );
    for ( const auto &idRev : idRevisions )
        lines.push_back( idRev.first + "@" + std::to_string( idRev.second ) );
    std::sort( lines.begin(), lines.end() );
    std::string joined;
    for ( std::size_t i = 0; i < lines.size(); ++i )
    {
        if ( i > 0 )
            joined += '\n';
        joined += lines[i];
    }
    return shortDigest( joined );
}

bool totalFindingOrder( const PreflightFinding &a, const PreflightFinding &b )
{
    if ( findingLess( a, b ) )
        return true;
    if ( findingLess( b, a ) )
        return false;
    return canonicalFindingText( a ) < canonicalFindingText( b );
}

// ---- PreflightEngine --------------------------------------------------------

void PreflightEngine::setBudgets( const PreflightBudgets &budgets )
{
    registryBudgets_ = budgets;
}

PreflightBudgets PreflightEngine::budgets() const
{
    return registryBudgets_;
}

RegistrationResult PreflightEngine::registerRule( PreflightRulePtr rule )
{
    if ( !rule )
    {
        lastError_ = "null rule";
        return RegistrationResult::DuplicateId;
    }
    const std::string id = rule->id();
    if ( id.empty() )
    {
        lastError_ = "rule with empty id";
        return RegistrationResult::DuplicateId;
    }
    const auto pos =
        std::lower_bound( rules_.begin(), rules_.end(), id,
                          []( const std::unique_ptr<IPreflightRule> &r, const std::string &key ) {
                              return r->id() < key;
                          } );
    if ( pos != rules_.end() && ( *pos )->id() == id )
    {
        lastError_ = "duplicate rule id: " + id;
        return RegistrationResult::DuplicateId;
    }
    if ( registryBudgets_.maxRules > 0 &&
         static_cast<int>( rules_.size() ) >= registryBudgets_.maxRules )
    {
        lastError_ = "registry full (max_rules=" + std::to_string( registryBudgets_.maxRules ) +
                     "): " + id;
        return RegistrationResult::RegistryFull;
    }
    rules_.insert( pos, std::move( rule ) );
    lastError_.clear();
    return RegistrationResult::Ok;
}

std::string PreflightEngine::registrationError() const
{
    return lastError_;
}

std::vector<std::string> PreflightEngine::ruleIds() const
{
    std::vector<std::string> ids;
    ids.reserve( rules_.size() );
    for ( const auto &rule : rules_ )
        ids.push_back( rule->id() );
    return ids;
}

std::size_t PreflightEngine::ruleCount() const
{
    return rules_.size();
}

PreflightReport PreflightEngine::evaluate( const PreflightRequest &request,
                                           const IAssetFactsProvider &assetFacts,
                                           const ICapabilityProvider &capability ) const
{
    // The report envelope must always satisfy the fail-closed report reader:
    // an invalid mode is coerced to the teaching default and the raw value
    // travels inside the request finding's evidence instead.
    const bool modeValid = isValidRunMode( request.mode );
    PreflightReport report = PreflightReport::makeEmpty(
        request.humanOperatorId, modeValid ? request.mode : std::string( "teaching" ) );
    report.requestDigest = computeRequestDigest( request );

    // Fail-closed request validation: an unjudgeable request is blocked with
    // a typed finding, never an empty "ok".
    std::vector<PreflightFinding> findings;
    const bool requestValid = modeValid && !request.inputs.empty();
    if ( !requestValid )
    {
        PreflightFinding f;
        f.code = "SPF_REQUEST_INVALID";
        f.severity = PreflightSeverity::Block;
        f.ruleId = "preflight.request";
        f.ruleRevision = 1;
        f.domain = "request";
        f.subject = "request";
        f.basis = "observed";
        f.humanExplanation =
            "The request itself is malformed (mode or inputs) and cannot be judged.";
        f.machineExplanation["expectation"] = "mode in [teaching, agent] and at least one input";
        f.machineExplanation["remediation"] = Json::Value( Json::arrayValue );
        f.evidence["mode"] = request.mode;
        f.evidence["inputs"] = static_cast<int>( request.inputs.size() );
        findings.push_back( std::move( f ) );
    }

    // Budgets: non-positive caps fall back to the schema defaults; the report
    // records the APPLIED budgets so the artifact always round-trips.
    PreflightBudgets applied = request.budgets;
    if ( applied.maxInputs <= 0 )
        applied.maxInputs = 64;
    if ( applied.maxFindings <= 0 )
        applied.maxFindings = 256;
    if ( applied.maxRules <= 0 )
        applied.maxRules = 512;
    report.budgets = applied;

    const int maxInputs = applied.maxInputs;
    const int maxFindings = applied.maxFindings;
    int droppedInputs = 0;

    // Resolve authority answers once per request, read-only.
    RuleFacts ruleFacts;
    ruleFacts.capability = capability.entryForOperator( request.operatorId, request.operatorParams );
    if ( requestValid )
    {
        for ( const auto &slot : request.inputs )
        {
            if ( static_cast<int>( ruleFacts.slots.size() ) >= maxInputs )
            {
                droppedInputs = static_cast<int>( request.inputs.size() ) - maxInputs;
                break;
            }
            RuleFacts::Slot entry;
            entry.slot = slot.first;
            entry.assetRef = slot.second;
            entry.facts = assetFacts.slotFacts( slot.second );
            entry.facts.facts.slot = slot.first;  // engine stamps the slot name
            entry.facts.facts.assetRef = slot.second;
            ruleFacts.slots.push_back( std::move( entry ) );
        }
    }

    // Evaluate in sorted-id order (rules_ is kept sorted).
    std::vector<std::pair<std::string, int>> idRevisions;
    idRevisions.reserve( rules_.size() );
    for ( const auto &rule : rules_ )
    {
        idRevisions.emplace_back( rule->id(), rule->revision() );
        if ( !requestValid )
            continue;

        PreflightEvaluatedRule trace;
        trace.ruleId = rule->id();
        trace.revision = rule->revision();
        trace.outcome = "pass";

        RuleResult result = rule->evaluate( request, ruleFacts );
        for ( auto &f : result.findings )
        {
            // The engine owns the trace fields that identify the rule.
            if ( f.ruleId.empty() )
                f.ruleId = rule->id();
            if ( f.ruleRevision <= 0 )
                f.ruleRevision = rule->revision();
            findings.push_back( std::move( f ) );
        }
        if ( !result.outcome.empty() && isValidEvaluatedOutcome( result.outcome ) )
            trace.outcome = result.outcome;
        else
            trace.outcome = result.findings.empty() ? "pass" : "finding";
        trace.detail = result.detail;
        report.evaluated.push_back( std::move( trace ) );
    }
    report.rulesRevision = computeRulesRevision( idRevisions );

    if ( !requestValid )
    {
        // Judge nothing else; keep the blocked finding, empty evaluated trace.
        report.findings = std::move( findings );
        report.verdict = "blocked";
        return report;
    }

    // Budget: dropped inputs are loud before evaluation.
    if ( droppedInputs > 0 )
    {
        Json::Value evidence( Json::objectValue );
        evidence["dropped_inputs"] = droppedInputs;
        evidence["cap"] = maxInputs;
        findings.push_back( budgetMarker( "inputs", std::move( evidence ) ) );
    }

    // Acknowledgement — the single decision point. Acknowledged is recomputed
    // from the request for every finding: require_ack with a matching code
    // clears, blocks never clear, and rule-claimed acknowledgements carry no
    // weight.
    for ( auto &f : findings )
    {
        f.acknowledged = false;
        if ( f.severity != PreflightSeverity::RequireAck )
            continue;
        for ( const auto &code : request.acknowledgements )
        {
            if ( code == f.code )
            {
                f.acknowledged = true;
                break;
            }
        }
        if ( !f.acknowledged )
        {
            for ( const auto &ack : request.acknowledgedSubjects )
            {
                if ( ack.code == f.code && ack.subject == f.subject )
                {
                    f.acknowledged = true;
                    break;
                }
            }
        }
    }

    // Deterministic presentation order, then loud findings truncation.
    std::stable_sort( findings.begin(), findings.end(), totalFindingOrder );
    if ( static_cast<int>( findings.size() ) > maxFindings )
    {
        const int dropped = static_cast<int>( findings.size() ) - ( maxFindings - 1 );
        findings.resize( std::max( 0, maxFindings - 1 ) );
        Json::Value evidence( Json::objectValue );
        evidence["dropped_findings"] = dropped;
        evidence["cap"] = maxFindings;
        findings.push_back( budgetMarker( "findings", std::move( evidence ) ) );
    }

    // Verdict: block dominates; unacknowledged require_ack gates; else ok.
    std::string verdict = "ok";
    for ( const auto &f : findings )
    {
        if ( f.severity == PreflightSeverity::Block )
        {
            verdict = "blocked";
            break;
        }
        if ( f.severity == PreflightSeverity::RequireAck && !f.acknowledged )
            verdict = "requires_ack";
    }

    report.findings = std::move( findings );
    report.verdict = std::move( verdict );
    return report;
}

} // namespace sicnu::preflight
