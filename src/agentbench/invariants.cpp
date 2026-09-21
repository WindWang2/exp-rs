// src/agentbench/invariants.cpp
#include "invariants.h"

#include "json_numbers.h"

#include <algorithm>
#include <string>
#include <vector>

namespace sicnu::agentbench
{
namespace
{

const TraceEvidence *findEvidence( const AgentTrace &trace, const std::string &id )
{
	for ( const TraceEvidence &item : trace.evidence )
		if ( item.id == id )
			return &item;
	return nullptr;
}

bool jsonValuesEqual( const Json::Value &left, const Json::Value &right )
{
	if ( left.isNull() != right.isNull() )
		return false;
	if ( left.isNumeric() && right.isNumeric() )
		return left.asDouble() == right.asDouble();
	if ( left.isString() && right.isString() )
		return left.asString() == right.asString();
	if ( left.isBool() && right.isBool() )
		return left.asBool() == right.asBool();
	return deterministicSerialize( left ) == deterministicSerialize( right );
}

bool isStringOrArray( const Json::Value &value )
{
	return value.isString() || value.isArray();
}

bool paramsContain( const Json::Value &params, const char *key )
{
	return params.isObject() && params.isMember( key );
}

bool paramsContainString( const Json::Value &params, const char *key )
{
	return params.isObject() && params.isMember( key ) && params[key].isString();
}

InvariantResult invalidParams( const Invariant &invariant )
{
	InvariantResult result;
	result.invariantId = invariant.id;
	result.passed = false;
	result.severity = invariant.severity;
	result.dimension = invariant.dimension;
	result.evidence["reason"] = "invalid_params";
	return result;
}

InvariantResult buildResult( const Invariant &invariant, bool passed, Json::Value evidence )
{
	InvariantResult result;
	result.invariantId = invariant.id;
	result.passed = passed;
	result.severity = invariant.severity;
	result.dimension = invariant.dimension;
	result.evidence = std::move( evidence );
	return result;
}

InvariantResult evaluateOne( const Invariant &invariant, const InvariantContext &context )
{
	const AgentTrace &trace = context.trace;
	const Json::Value &params = invariant.params;

	switch ( invariant.kind )
	{
		case InvariantKind::ToolUsed:
		case InvariantKind::ToolNotUsed:
		{
			if ( !paramsContainString( params, "tool" ) )
				return invalidParams( invariant );
			const std::string tool = params["tool"].asString();
			int uses = 0;
			for ( const TraceStep &step : trace.steps )
				if ( step.tool == tool )
					uses++;
			Json::Value evidence{Json::objectValue};
			evidence["tool"] = tool;
			evidence["uses"] = uses;
			const bool passed = invariant.kind == InvariantKind::ToolUsed ? uses > 0 : uses == 0;
			return buildResult( invariant, passed, std::move( evidence ) );
		}

		case InvariantKind::StepOrder:
		{
			if ( !paramsContain( params, "steps" ) || !params["steps"].isArray() )
				return invalidParams( invariant );
			std::vector<std::string> wanted;
			for ( const Json::Value &entry : params["steps"] )
				wanted.push_back( entry.asString() );
			// Ordered subsequence match over the trace tools.
			size_t cursor = 0;
			for ( const TraceStep &step : trace.steps )
			{
				if ( cursor < wanted.size() && step.tool == wanted[cursor] )
					++cursor;
			}
			Json::Value evidence{Json::objectValue};
			evidence["matched_prefix"] = Json::Value::Int( cursor );
			evidence["wanted"] = Json::Value::Int( wanted.size() );
			return buildResult( invariant, cursor == wanted.size(), std::move( evidence ) );
		}

		case InvariantKind::ResultSuccess:
		case InvariantKind::ErrorCodePresent:
		{
			if ( !paramsContain( params, "step_index" ) )
				return invalidParams( invariant );
			if ( !isIntInRange( params["step_index"], 0, 2147483647ll ) )
				return invalidParams( invariant );
			const int index = static_cast<int>( params["step_index"].asInt64() );
			if ( index < 0 || static_cast<size_t>( index ) >= trace.steps.size() )
			{
				Json::Value evidence{Json::objectValue};
				evidence["reason"] = "step_out_of_range";
				evidence["step_index"] = index;
				return buildResult( invariant, false, std::move( evidence ) );
			}
			const TraceStep &step = trace.steps[static_cast<size_t>( index )];
			if ( invariant.kind == InvariantKind::ResultSuccess )
			{
				Json::Value evidence{Json::objectValue};
				evidence["observed_success"] = step.success;
				return buildResult( invariant, step.success, std::move( evidence ) );
			}
			if ( !paramsContainString( params, "error_code" ) )
				return invalidParams( invariant );
			Json::Value evidence{Json::objectValue};
			evidence["observed_error_code"] = step.errorCode;
			evidence["step_success"] = step.success;
			return buildResult( invariant, !step.success && step.errorCode == params["error_code"].asString(), std::move( evidence ) );
		}

		case InvariantKind::EvidenceExists:
		case InvariantKind::FieldEquals:
		case InvariantKind::FieldContains:
		case InvariantKind::NumericLe:
		case InvariantKind::NumericGe:
		case InvariantKind::VerdictIs:
		{
			if ( !paramsContainString( params, "evidence_id" ) )
				return invalidParams( invariant );
			const TraceEvidence *item = findEvidence( trace, params["evidence_id"].asString() );
			if ( item == nullptr )
			{
				Json::Value evidence{Json::objectValue};
				evidence["reason"] = "evidence_not_found";
				evidence["evidence_id"] = params["evidence_id"];
				return buildResult( invariant, false, std::move( evidence ) );
			}

			if ( invariant.kind == InvariantKind::EvidenceExists )
			{
				Json::Value evidence{Json::objectValue};
				evidence["kind"] = item->kind;
				return buildResult( invariant, true, std::move( evidence ) );
			}
			if ( invariant.kind == InvariantKind::VerdictIs )
			{
				if ( !paramsContainString( params, "verdict" ) )
					return invalidParams( invariant );
				Json::Value evidence{Json::objectValue};
				evidence["observed_verdict"] = item->verdict;
				return buildResult( invariant, item->verdict == params["verdict"].asString(), std::move( evidence ) );
			}
			if ( !paramsContainString( params, "field" ) )
				return invalidParams( invariant );
			const Json::Value *observed = resolveDottedPath( item->fields, params["field"].asString() );
			if ( observed == nullptr )
			{
				Json::Value evidence{Json::objectValue};
				evidence["reason"] = "field_missing";
				evidence["field"] = params["field"];
				return buildResult( invariant, false, std::move( evidence ) );
			}

			switch ( invariant.kind )
			{
				case InvariantKind::FieldEquals:
					if ( !paramsContain( params, "value" ) )
						return invalidParams( invariant );
				{
					Json::Value evidence{Json::objectValue};
					evidence["observed"] = *observed;
					return buildResult( invariant, jsonValuesEqual( *observed, params["value"] ), std::move( evidence ) );
				}
				case InvariantKind::FieldContains:
					if ( !paramsContain( params, "value" ) || !isStringOrArray( params["value"] ) )
						return invalidParams( invariant );
					if ( !observed->isString() && !observed->isArray() )
					{
						Json::Value evidence{Json::objectValue};
						evidence["reason"] = "field_not_containable";
						evidence["field"] = params["field"];
						return buildResult( invariant, false, std::move( evidence ) );
					}
				{
					bool contains = false;
					if ( observed->isString() && params["value"].isString() )
						contains = observed->asString().find( params["value"].asString() ) != std::string::npos;
					else if ( observed->isArray() )
					{
						for ( const Json::Value &entry : *observed )
							if ( jsonValuesEqual( entry, params["value"] ) )
								contains = true;
					}
					Json::Value evidence{Json::objectValue};
					evidence["observed"] = *observed;
					return buildResult( invariant, contains, std::move( evidence ) );
				}
				case InvariantKind::NumericLe:
					if ( !paramsContain( params, "max" ) || !params["max"].isNumeric() )
						return invalidParams( invariant );
					if ( !observed->isNumeric() )
					{
						Json::Value evidence{Json::objectValue};
						evidence["reason"] = "field_not_numeric";
						evidence["field"] = params["field"];
						return buildResult( invariant, false, std::move( evidence ) );
					}
				{
					Json::Value evidence{Json::objectValue};
					evidence["observed"] = *observed;
					evidence["max"] = params["max"];
					return buildResult( invariant, observed->asDouble() <= params["max"].asDouble(), std::move( evidence ) );
				}
				case InvariantKind::NumericGe:
					if ( !paramsContain( params, "min" ) || !params["min"].isNumeric() )
						return invalidParams( invariant );
					if ( !observed->isNumeric() )
					{
						Json::Value evidence{Json::objectValue};
						evidence["reason"] = "field_not_numeric";
						evidence["field"] = params["field"];
						return buildResult( invariant, false, std::move( evidence ) );
					}
				{
					Json::Value evidence{Json::objectValue};
					evidence["observed"] = *observed;
					evidence["min"] = params["min"];
					return buildResult( invariant, observed->asDouble() >= params["min"].asDouble(), std::move( evidence ) );
				}
				default:
					return invalidParams( invariant );
			}
		}

		case InvariantKind::ExplanationMentions:
		{
			if ( !paramsContainString( params, "phrase" ) )
				return invalidParams( invariant );
			Json::Value evidence{Json::objectValue};
			evidence["phrase"] = params["phrase"];
			evidence["explanation_length"] = Json::Value::Int( trace.explanation.size() );
			return buildResult( invariant, trace.explanation.find( params["phrase"].asString() ) != std::string::npos, std::move( evidence ) );
		}

		case InvariantKind::ClaimConsistent:
		{
			// Local honesty check: the claim must match the recorded stop reason.
			// Claim-vs-invariant consistency is scored at the evaluator level.
			const bool consistent = trace.outcomeClaimSuccess == ( trace.stopReason == StopReason::Completed );
			Json::Value evidence{Json::objectValue};
			evidence["claimed_success"] = trace.outcomeClaimSuccess;
			evidence["stop_reason"] = stopReasonToString( trace.stopReason );
			return buildResult( invariant, consistent, std::move( evidence ) );
		}

		case InvariantKind::BudgetWithin:
		{
			bool exceeded = false;
			Json::Value exceededResources{Json::arrayValue};
			for ( const ReplayViolation &violation : context.replay.violations )
			{
				if ( violation.code == error_codes::kBudgetExceeded )
				{
					exceeded = true;
					exceededResources.append( violation.details["resource"] );
				}
			}
			Json::Value evidence{Json::objectValue};
			evidence["exceeded"] = exceededResources;
			return buildResult( invariant, !exceeded, std::move( evidence ) );
		}
	}
	return invalidParams( invariant );
}

} // namespace

std::vector<InvariantResult> evaluateInvariants( const InvariantContext &context )
{
	std::vector<InvariantResult> results;
	results.reserve( context.caseValue.invariants.size() );
	for ( const Invariant &invariant : context.caseValue.invariants )
		results.push_back( evaluateOne( invariant, context ) );
	return results;
}

} // namespace sicnu::agentbench
