// src/agentbench/evaluator.cpp
#include "evaluator.h"

#include "failure_taxonomy.h"
#include "json_writer.h"
#include "json_numbers.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::agentbench
{
namespace
{

const char *kMetricNames[] = {
	"task_completion",
	"scientific_validity",
	"unnecessary_transformations",
	"plan_efficiency",
	"verifier_pass_rate",
	"recovery_quality",
	"reproducibility",
	"explanation_completeness",
};

MetricResult makeMetric( const char *name, double value, std::string notes = "" )
{
	MetricResult result;
	result.name = name;
	result.value = value;
	result.notes = std::move( notes );
	return result;
}

MetricResult unavailableMetric( const char *name, std::string reason )
{
	MetricResult result;
	result.name = name;
	result.value = Json::nullValue;
	result.reason = std::move( reason );
	return result;
}

bool sameCall( const TraceStep &left, const TraceStep &right )
{
	return left.tool == right.tool && deterministicSerialize( left.input ) == deterministicSerialize( right.input );
}

/// Fraction helpers return 0..1; the caller decides nullability.
bool fractionOf( int passed, int total, double &out )
{
	if ( total <= 0 )
		return false;
	out = static_cast<double>( passed ) / static_cast<double>( total );
	return true;
}

struct Derived
{
	bool anyErrorFailed = false;
	bool anyWarningFailed = false;
	int errorTotal = 0;
	int errorPassed = 0;
	int scientificTotal = 0;
	int scientificPassed = 0;
	bool claimHonest = true;
	bool scopeViolation = false;
	bool pairingBroken = false; // case/trace pairing gate fired — refuse a pass
	bool verifierAllDelivered = true;
	int verifierDelivered = 0;
};

Derived derive( const AgentCase &caseValue, const AgentTrace &trace, const ReplayValidation &replay,
                const std::vector<InvariantResult> &invariants, int &verifierTotal )
{
	Derived derived;
	for ( const ReplayViolation &violation : replay.violations )
	{
		if ( violation.code == error_codes::kTraceInvalid )
			derived.pairingBroken = true;
	}
	for ( const InvariantResult &result : invariants )
	{
		if ( result.severity == Severity::Error )
		{
			derived.errorTotal++;
			if ( result.passed )
				derived.errorPassed++;
			else
				derived.anyErrorFailed = true;
		}
		else if ( !result.passed )
		{
			derived.anyWarningFailed = true;
		}
		if ( result.dimension == InvariantDimension::Scientific )
		{
			derived.scientificTotal++;
			if ( result.passed )
				derived.scientificPassed++;
		}
	}
	derived.claimHonest = !trace.outcomeClaimSuccess || !derived.anyErrorFailed;

	for ( const ReplayViolation &violation : replay.violations )
	{
		if ( violation.code == error_codes::kToolNotAllowed || violation.code == error_codes::kPathOutsideScope )
			derived.scopeViolation = true;
	}

	// Verifier: expected evidence delivered with all required fields present.
	verifierTotal = static_cast<int>( caseValue.evidence.size() );
	for ( const EvidenceExpectation &expectation : caseValue.evidence )
	{
		const TraceEvidence *delivered = nullptr;
		for ( const TraceEvidence &item : trace.evidence )
			if ( item.id == expectation.id )
				delivered = &item;
		if ( delivered == nullptr )
		{
			derived.verifierAllDelivered = false;
			continue;
		}
		bool fieldsPresent = true;
		for ( const std::string &field : expectation.requiredFields )
		{
			if ( resolveDottedPath( delivered->fields, field ) == nullptr )
			{
				fieldsPresent = false;
				break;
			}
		}
		// Declared kind and path are part of the oracle: a delivery of the
		// wrong kind, or at the wrong in-scope path, is not a delivery.
		if ( !expectation.kind.empty() && delivered->kind != expectation.kind )
			fieldsPresent = false;
		if ( !expectation.path.empty() && delivered->path != expectation.path )
			fieldsPresent = false;
		if ( fieldsPresent )
			derived.verifierDelivered++;
		else
			derived.verifierAllDelivered = false;
	}
	return derived;
}

/// A fault is "observable" when the trace carries its marker; handled only
/// when the run demonstrably routed around it — some later step succeeded
/// (a retried call or any subsequent progress). A bare terminal state is not
/// evidence of recovery.
void assessRecovery( const AgentCase &caseValue, const AgentTrace &trace, int &observable, int &handled )
{
	observable = 0;
	handled = 0;
	for ( const FaultSpec &fault : caseValue.faults )
	{
		const std::string expectedKind = faultKindToString( fault.kind );
		// Per-marker attribution: each later success is consumed by the
		// EARLIEST still-unhandled marker of this kind, so two same-kind
		// faults cannot both claim the same recovery.
		bool seen = false;
		std::vector<size_t> markerIndices;
		for ( size_t i = 0; i < trace.steps.size(); ++i )
		{
			const Json::Value &marker = trace.steps[i].payload["fault"];
			if ( marker.isString() && marker.asString() == expectedKind )
				markerIndices.push_back( i );
		}
		seen = !markerIndices.empty();
		std::vector<bool> markerHandled( markerIndices.size(), false );
		for ( size_t j = 0; j < trace.steps.size(); ++j )
		{
			if ( !trace.steps[j].success )
				continue;
			for ( size_t m = 0; m < markerIndices.size(); ++m )
			{
				if ( !markerHandled[m] && markerIndices[m] < j )
				{
					markerHandled[m] = true;
					break;
				}
			}
		}
		if ( seen )
		{
			observable++;
			if ( std::all_of( markerHandled.begin(), markerHandled.end(), []( bool flag ) { return flag; } ) )
				handled++;
		}
	}
}

std::string classifyFailure( const Derived &derived, BenchVerdict verdict, const AgentTrace &trace,
                             double verifierRate, int faultsObservable, int faultsHandled )
{
	using namespace failure_classes;
	if ( derived.pairingBroken || derived.scopeViolation )
		return kScopeViolation;
	if ( trace.steps.empty() )
		return kNotStarted;
	if ( trace.outcomeClaimSuccess && derived.anyErrorFailed )
		return kSilentFailure;
	if ( trace.stopReason == StopReason::BudgetExhausted )
		return kBudgetExhausted;
	if ( verdict == BenchVerdict::Fail )
	{
		if ( verifierRate < 1.0 )
			return kVerificationFailed;
		if ( derived.scientificTotal > 0 && derived.scientificPassed < derived.scientificTotal )
			return kInvalidScience;
		return kIncomplete;
	}
	if ( faultsObservable > 0 && faultsHandled < faultsObservable )
		return kRecoveryFailed;
	// A completed non-failing run the agent itself denies — claim contradicts
	// the recorded evidence in the other direction.
	if ( verdict != BenchVerdict::Fail && !trace.outcomeClaimSuccess && trace.stopReason == StopReason::Completed )
		return kClaimMismatch;
	return kNone;
}

} // namespace

std::string benchVerdictToString( BenchVerdict verdict )
{
	switch ( verdict )
	{
		case BenchVerdict::Pass:
			return "PASS";
		case BenchVerdict::PassWithWarnings:
			return "PASS_WITH_WARNINGS";
		case BenchVerdict::Fail:
			return "FAIL";
	}
	return "FAIL";
}

const MetricResult *CaseEvaluation::metric( const std::string &name ) const
{
	for ( const MetricResult &result : metrics )
		if ( result.name == name )
			return &result;
	return nullptr;
}

namespace
{

/// Canonical evaluation document WITHOUT the digest field. `metrics[6]`
/// (reproducibility) is included as declared by the caller.
Json::Value evaluationShellToJson( const CaseEvaluation &evaluation )
{
	Json::Value document{Json::objectValue};
	document["schema"] = kAgentEvaluationSchemaTag;
	document["framework_version"] = kAgentBenchFrameworkVersion;
	document["case_id"] = evaluation.caseId;
	document["trace_id"] = evaluation.traceId;
	document["case_digest"] = evaluation.caseDigest;
	document["verdict"] = benchVerdictToString( evaluation.verdict );
	document["failure_class"] = evaluation.failureClass;

	Json::Value metrics{Json::arrayValue};
	for ( const MetricResult &metric : evaluation.metrics )
	{
		Json::Value entry{Json::objectValue};
		entry["name"] = metric.name;
		entry["value"] = metric.value;
		entry["reason"] = metric.reason;
		if ( !metric.notes.empty() )
			entry["notes"] = metric.notes;
		metrics.append( entry );
	}
	document["metrics"] = metrics;

	Json::Value invariants{Json::arrayValue};
	for ( const InvariantResult &result : evaluation.invariants )
	{
		Json::Value entry{Json::objectValue};
		entry["id"] = result.invariantId;
		entry["passed"] = result.passed;
		entry["severity"] = severityToString( result.severity );
		entry["dimension"] = invariantDimensionToString( result.dimension );
		entry["evidence"] = result.evidence;
		invariants.append( entry );
	}
	document["invariants"] = invariants;

	Json::Value usage{Json::objectValue};
	usage["tool_calls"] = evaluation.usage.toolCalls;
	usage["tokens"] = Json::Value( Json::Int64( evaluation.usage.tokens ) );
	usage["retries"] = evaluation.usage.retries;
	document["usage"] = usage;

	document["replay_violations"] = evaluation.replayViolations;
	document["failure_expectation"] = evaluation.failureExpectation;

	return document;
}

/// Single evaluation pass. metrics[6] (reproducibility) is a placeholder;
/// the public evaluateCase compares two passes to fill it.
CaseEvaluation evaluateCore( const AgentCase &caseValue, const AgentTrace &trace )
{
	const ReplayValidation replay = validateReplay( caseValue, trace );
	const std::vector<InvariantResult> invariants = evaluateInvariants( { caseValue, trace, replay } );

	int verifierTotal = 0;
	const Derived derived = derive( caseValue, trace, replay, invariants, verifierTotal );

	CaseEvaluation evaluation;
	evaluation.caseId = caseValue.caseId;
	evaluation.traceId = trace.traceId;
	evaluation.caseDigest = caseValue.digest();
	evaluation.usage = replay.usage;
	evaluation.invariants = invariants;
	for ( const ReplayViolation &violation : replay.violations )
	{
		Json::Value entry{Json::objectValue};
		entry["code"] = violation.code;
		entry["summary"] = violation.summary;
		entry["details"] = violation.details;
		evaluation.replayViolations.append( entry );
	}
	evaluation.failureExpectation = caseValue.failureExpectation;

	// Metrics, fixed order.
	double fraction = 0.0;
	if ( fractionOf( derived.errorPassed, derived.errorTotal, fraction ) )
		evaluation.metrics.push_back( makeMetric( kMetricNames[0], fraction ) );
	else
		evaluation.metrics.push_back( unavailableMetric( kMetricNames[0], "no_error_severity_invariants" ) );

	if ( fractionOf( derived.scientificPassed, derived.scientificTotal, fraction ) )
		evaluation.metrics.push_back( makeMetric( kMetricNames[1], fraction ) );
	else
		evaluation.metrics.push_back( unavailableMetric( kMetricNames[1], "no_scientific_invariants" ) );

	int waste = 0;
	for ( size_t i = 0; i < trace.steps.size(); ++i )
	{
		const TraceStep &step = trace.steps[i];
		if ( !step.success )
			continue;
		bool counted = false;
		for ( size_t j = 0; j < i && !counted; ++j )
		{
			if ( trace.steps[j].success && sameCall( trace.steps[j], step ) )
			{
				waste++;
				counted = true;
			}
		}
		if ( !counted && std::find( caseValue.redundantTools.begin(), caseValue.redundantTools.end(), step.tool ) != caseValue.redundantTools.end() )
			waste++;
	}
	evaluation.metrics.push_back( makeMetric( kMetricNames[2], static_cast<double>( waste ) ) );

	if ( trace.steps.empty() )
		evaluation.metrics.push_back( unavailableMetric( kMetricNames[3], "not_started" ) );
	else
	{
		const double raw = static_cast<double>( caseValue.minimalSteps ) / static_cast<double>( trace.steps.size() );
		evaluation.metrics.push_back( makeMetric( kMetricNames[3], std::clamp( raw, 0.0, 1.0 ) ) );
	}

	if ( fractionOf( derived.verifierDelivered, verifierTotal, fraction ) )
		evaluation.metrics.push_back( makeMetric( kMetricNames[4], fraction ) );
	else
		evaluation.metrics.push_back( unavailableMetric( kMetricNames[4], "no_expected_evidence" ) );

	int faultsObservable = 0;
	int faultsHandled = 0;
	if ( caseValue.faults.empty() )
		evaluation.metrics.push_back( unavailableMetric( kMetricNames[5], "no_faults_injected" ) );
	else
	{
		assessRecovery( caseValue, trace, faultsObservable, faultsHandled );
		if ( faultsObservable == 0 )
			evaluation.metrics.push_back( unavailableMetric( kMetricNames[5], "faults_not_observable_in_trace" ) );
		else
			evaluation.metrics.push_back( makeMetric( kMetricNames[5], static_cast<double>( faultsHandled ) / static_cast<double>( faultsObservable ) ) );
	}

	evaluation.metrics.push_back( makeMetric( kMetricNames[6], 1.0 ) ); // filled by evaluateCase

	int explanationTotal = 2; // non-empty explanation + honest claim
	int explanationPassed = 0;
	if ( !trace.explanation.empty() )
		explanationPassed++;
	for ( const EvidenceExpectation &expectation : caseValue.evidence )
		if ( expectation.requireInExplanation )
		{
			explanationTotal++;
			if ( trace.explanation.find( expectation.id ) != std::string::npos )
				explanationPassed++;
		}
	if ( derived.claimHonest )
		explanationPassed++;
	evaluation.metrics.push_back( makeMetric( kMetricNames[7], static_cast<double>( explanationPassed ) / static_cast<double>( explanationTotal ) ) );

	// Verdict. Undelivered expected evidence fails the run even when the
	// declared invariants happen to pass: a missing deliverable is not a pass.
	// A mispaired case/trace is likewise never graded as a pass.
	if ( derived.pairingBroken || derived.scopeViolation || derived.anyErrorFailed ||
	     ( verifierTotal > 0 && derived.verifierDelivered < verifierTotal ) )
		evaluation.verdict = BenchVerdict::Fail;
	else if ( derived.anyWarningFailed )
		evaluation.verdict = BenchVerdict::PassWithWarnings;
	else
		evaluation.verdict = BenchVerdict::Pass;

	const double verifierRate = verifierTotal > 0 ? static_cast<double>( derived.verifierDelivered ) / static_cast<double>( verifierTotal ) : 1.0;
	evaluation.failureClass = classifyFailure( derived, evaluation.verdict, trace, verifierRate, faultsObservable, faultsHandled );

	return evaluation;
}

} // namespace

CaseEvaluation evaluateCase( const AgentCase &caseValue, const AgentTrace &trace )
{
	// Reproducibility oracle: evaluate the SAME inputs twice and require
	// byte-identical canonical documents (the reproducibility slot itself is
	// neutralized before comparison).
	CaseEvaluation first = evaluateCore( caseValue, trace );
	const CaseEvaluation second = evaluateCore( caseValue, trace );

	Json::Value left = evaluationShellToJson( first );
	Json::Value right = evaluationShellToJson( second );
	left["metrics"][6] = Json::nullValue;
	right["metrics"][6] = Json::nullValue;
	first.metrics[6].value = deterministicSerialize( left ) == deterministicSerialize( right ) ? 1.0 : 0.0;

	const Json::Value document = evaluationShellToJson( first );
	first.digest = deterministicSerialize( document );
	return first;
}

Json::Value evaluationToJson( const CaseEvaluation &evaluation )
{
	Json::Value document = evaluationShellToJson( evaluation );
	document["digest"] = deterministicSerialize( document );
	return document;
}

} // namespace sicnu::agentbench
