// src/agentbench/fake_agent.cpp
#include "fake_agent.h"

#include "json_numbers.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::agentbench
{
namespace
{

struct EnumTable
{
	const char *wire;
	int value;
};

const EnumTable kOnFailure[] = {
	{ "abort", int( OnFailure::Abort ) },
	{ "retry_once", int( OnFailure::RetryOnce ) },
	{ "skip", int( OnFailure::Skip ) },
};

bool lookupEnum( const EnumTable *table, size_t size, const std::string &wire, int &out )
{
	for ( size_t i = 0; i < size; ++i )
	{
		if ( wire == table[i].wire )
		{
			out = table[i].value;
			return true;
		}
	}
	return false;
}

bool isNonEmptyString( const Json::Value &value )
{
	return value.isString() && !value.asString().empty();
}

BenchError scriptInvalid( std::string field, const std::string &why )
{
	Json::Value details{Json::objectValue};
	details["field"] = std::move( field );
	details["reason"] = why;
	return makeError( error_codes::kScriptInvalid, "script document violates the v1 schema", std::move( details ) );
}

bool isKnownVerdict( const std::string &verdict )
{
	return verdict == "PASS" || verdict == "PASS_WITH_WARNINGS" || verdict == "FAIL";
}

Json::Value mergedPayload( const Json::Value &declared, const char *faultKind )
{
	Json::Value payload = declared;
	if ( payload.isObject() )
	{
		payload["fault"] = faultKind;
		payload["verdict"] = "FAIL";
	}
	return payload;
}

} // namespace

std::string onFailureToString( OnFailure policy )
{
	for ( const EnumTable &entry : kOnFailure )
		if ( entry.value == int( policy ) )
			return entry.wire;
	return "";
}

bool parseOnFailure( const std::string &wire, OnFailure &out )
{
	int value = 0;
	if ( !lookupEnum( kOnFailure, sizeof( kOnFailure ) / sizeof( kOnFailure[0] ), wire, value ) )
		return false;
	out = static_cast<OnFailure>( value );
	return true;
}

ScriptParse parseScript( const std::string &jsonText )
{
	ScriptParse result;

	Json::CharReaderBuilder builder;
	builder["collectComments"] = false;
	builder["stackLimit"] = 128;
	std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
	Json::Value root;
	std::string parseErrors;
	if ( !reader->parse( jsonText.data(), jsonText.data() + jsonText.size(), &root, &parseErrors ) )
	{
		Json::Value details{Json::objectValue};
		details["reason"] = parseErrors;
		result.error = makeError( error_codes::kScriptInvalid, "document is not valid JSON", std::move( details ) );
		return result;
	}
	if ( !root.isObject() )
	{
		result.error = makeError( error_codes::kScriptInvalid, "script document root must be an object" );
		return result;
	}
	if ( !isNonEmptyString( root["schema"] ) || root["schema"].asString() != kAgentScriptSchemaTag )
	{
		Json::Value details{Json::objectValue};
		details["found"] = root["schema"].isString() ? root["schema"].asString() : "";
		details["expected"] = kAgentScriptSchemaTag;
		result.error = makeError( error_codes::kSchemaVersionUnknown, "unsupported script schema version", std::move( details ) );
		return result;
	}

	AgentScript script;
	script.raw = root;

	if ( !isNonEmptyString( root["script_id"] ) )
	{
		result.error = scriptInvalid( "script_id", "script_id must be a non-empty string" );
		return result;
	}
	script.scriptId = root["script_id"].asString();
	if ( !isNonEmptyString( root["case_id"] ) )
	{
		result.error = scriptInvalid( "case_id", "case_id must be a non-empty string" );
		return result;
	}
	script.caseId = root["case_id"].asString();

	const Json::Value &agent = root["agent"];
	if ( !agent.isObject() || !isNonEmptyString( agent["name"] ) )
	{
		result.error = scriptInvalid( "agent.name", "agent.name must be a non-empty string" );
		return result;
	}
	script.agentName = agent["name"].asString();
	if ( !isNonEmptyString( agent["version"] ) )
	{
		result.error = scriptInvalid( "agent.version", "agent.version must be a non-empty string" );
		return result;
	}
	script.agentVersion = agent["version"].asString();

	if ( root.isMember( "seed" ) )
	{
		if ( !isIntInRange( root["seed"], 0, 9223372036854775807ll ) )
		{
			result.error = scriptInvalid( "seed", "seed must be a non-negative integer" );
			return result;
		}
		script.seed = root["seed"].asInt64();
	}

	const Json::Value &steps = root["steps"];
	if ( !steps.isArray() || steps.empty() )
	{
		result.error = scriptInvalid( "steps", "steps must be a non-empty array" );
		return result;
	}
	for ( const Json::Value &entry : steps )
	{
		const std::string at = "steps[" + std::to_string( script.steps.size() ) + "]";
		if ( !entry.isObject() )
		{
			result.error = scriptInvalid( at, "script step must be an object" );
			return result;
		}
		ScriptStep step;
		if ( !isNonEmptyString( entry["tool"] ) )
		{
			result.error = scriptInvalid( at + ".tool", "tool must be a non-empty string" );
			return result;
		}
		step.tool = entry["tool"].asString();
		if ( entry.isMember( "input" ) )
		{
			if ( !entry["input"].isObject() )
			{
				result.error = scriptInvalid( at + ".input", "input must be an object" );
				return result;
			}
			step.input = entry["input"];
		}
		if ( entry.isMember( "payload" ) )
		{
			if ( !entry["payload"].isObject() )
			{
				result.error = scriptInvalid( at + ".payload", "payload must be an object" );
				return result;
			}
			step.payload = entry["payload"];
		}
		if ( entry.isMember( "on_failure" ) )
		{
			if ( !isNonEmptyString( entry["on_failure"] ) || !parseOnFailure( entry["on_failure"].asString(), step.onFailure ) )
			{
				result.error = scriptInvalid( at + ".on_failure", "on_failure must be one of abort|retry_once|skip" );
				return result;
			}
		}
		if ( entry.isMember( "evidence" ) )
		{
			if ( !isNonEmptyString( entry["evidence"] ) )
			{
				result.error = scriptInvalid( at + ".evidence", "evidence must be a non-empty string" );
				return result;
			}
			step.evidenceId = entry["evidence"].asString();
		}
		if ( entry.isMember( "tokens" ) )
		{
			if ( !isIntInRange( entry["tokens"], 0, 2147483647ll ) )
			{
				result.error = scriptInvalid( at + ".tokens", "tokens must be a non-negative integer" );
				return result;
			}
			step.tokens = static_cast<int>( entry["tokens"].asInt64() );
		}
		script.steps.push_back( std::move( step ) );
	}

	if ( root.isMember( "explanation" ) )
	{
		if ( !root["explanation"].isString() )
		{
			result.error = scriptInvalid( "explanation", "explanation must be a string" );
			return result;
		}
		script.explanation = root["explanation"].asString();
	}
	if ( root.isMember( "outcome_claim" ) )
	{
		const Json::Value &claim = root["outcome_claim"];
		if ( !claim.isObject() || !claim["success"].isBool() )
		{
			result.error = scriptInvalid( "outcome_claim", "outcome_claim must be an object with a boolean success" );
			return result;
		}
		script.hasOutcomeClaim = true;
		script.outcomeClaimSuccess = claim["success"].asBool();
		if ( claim.isMember( "note" ) )
		{
			if ( !claim["note"].isString() )
			{
				result.error = scriptInvalid( "outcome_claim.note", "outcome_claim.note must be a string" );
				return result;
			}
			script.outcomeClaimNote = claim["note"].asString();
		}
	}
	if ( root.isMember( "stop_reason_override" ) )
	{
		StopReason parsed = StopReason::GaveUp;
		if ( !isNonEmptyString( root["stop_reason_override"] ) ||
		     !parseStopReason( root["stop_reason_override"].asString(), parsed ) )
		{
			result.error = scriptInvalid( "stop_reason_override", "stop_reason_override must be one of completed|budget_exhausted|gave_up|blocked" );
			return result;
		}
		script.stopReasonOverride = root["stop_reason_override"].asString();
	}

	result.parsed = std::move( script );
	return result;
}

Json::Value scriptToJson( const AgentScript &script )
{
	Json::Value doc{Json::objectValue};
	doc["schema"] = kAgentScriptSchemaTag;
	doc["script_id"] = script.scriptId;
	doc["case_id"] = script.caseId;
	Json::Value agent{Json::objectValue};
	agent["name"] = script.agentName;
	agent["version"] = script.agentVersion;
	doc["agent"] = agent;
	doc["seed"] = Json::Value( Json::Int64( script.seed ) );

	Json::Value steps{Json::arrayValue};
	for ( const ScriptStep &step : script.steps )
	{
		Json::Value entry{Json::objectValue};
		entry["tool"] = step.tool;
		entry["input"] = step.input;
		entry["payload"] = step.payload;
		entry["on_failure"] = onFailureToString( step.onFailure );
		if ( !step.evidenceId.empty() )
			entry["evidence"] = step.evidenceId;
		entry["tokens"] = step.tokens;
		steps.append( entry );
	}
	doc["steps"] = steps;

	doc["explanation"] = script.explanation;
	if ( script.hasOutcomeClaim )
	{
		Json::Value claim{Json::objectValue};
		claim["success"] = script.outcomeClaimSuccess;
		if ( !script.outcomeClaimNote.empty() )
			claim["note"] = script.outcomeClaimNote;
		doc["outcome_claim"] = claim;
	}
	if ( !script.stopReasonOverride.empty() )
		doc["stop_reason_override"] = script.stopReasonOverride;
	return doc;
}

ScriptRun runScript( const AgentCase &caseValue, const AgentScript &script )
{
	ScriptRun result;

	// Cross-validation BEFORE running: a script that misuses the case never
	// produces a trace.
	if ( script.caseId != caseValue.caseId )
	{
		Json::Value details{Json::objectValue};
		details["field"] = "case_id";
		details["reason"] = "script targets a different case";
		details["script_case_id"] = script.caseId;
		details["case_id"] = caseValue.caseId;
		result.error = makeError( error_codes::kScriptInvalid, "script targets a different case", std::move( details ) );
		return result;
	}
	for ( size_t i = 0; i < script.steps.size(); ++i )
	{
		const ScriptStep &step = script.steps[i];
		if ( std::find( caseValue.allowedTools.begin(), caseValue.allowedTools.end(), step.tool ) == caseValue.allowedTools.end() )
		{
			Json::Value details{Json::objectValue};
			details["field"] = "steps[" + std::to_string( i ) + "].tool";
			details["reason"] = "script step uses a tool outside the case allow-list";
			details["tool"] = step.tool;
			result.error = makeError( error_codes::kToolNotAllowed, "script step uses a disallowed tool", std::move( details ) );
			return result;
		}
		if ( !step.evidenceId.empty() )
		{
			bool declared = false;
			for ( const EvidenceExpectation &expectation : caseValue.evidence )
				if ( expectation.id == step.evidenceId )
					declared = true;
			if ( !declared )
			{
				Json::Value details{Json::objectValue};
				details["field"] = "steps[" + std::to_string( i ) + "].evidence";
				details["reason"] = "script step declares evidence the case does not expect";
				details["evidence_id"] = step.evidenceId;
				result.error = makeError( error_codes::kScriptInvalid, "script declares undeclared evidence", std::move( details ) );
				return result;
			}
		}
	}

	// Fault schedule from the case (first fault per script-step index wins).
	std::vector<const FaultSpec *> faults( script.steps.size(), nullptr );
	for ( const FaultSpec &fault : caseValue.faults )
	{
		if ( fault.atStep < 0 || static_cast<size_t>( fault.atStep ) >= script.steps.size() )
			continue;
		if ( !fault.tool.empty() && fault.tool != script.steps[fault.atStep].tool )
			continue;
		if ( faults[fault.atStep] == nullptr )
			faults[fault.atStep] = &fault;
	}
	// A script may declare each evidence id at most once — the parser rejects
	// duplicate ids in recorded traces, and scripted runs never round-trip
	// through it.
	std::vector<std::string> declaredEvidenceIds;
	for ( size_t i = 0; i < script.steps.size(); ++i )
	{
		if ( script.steps[i].evidenceId.empty() )
			continue;
		if ( std::find( declaredEvidenceIds.begin(), declaredEvidenceIds.end(), script.steps[i].evidenceId ) != declaredEvidenceIds.end() )
		{
			Json::Value details{Json::objectValue};
			details["field"] = "steps[" + std::to_string( i ) + "].evidence";
			details["reason"] = "script declares the same evidence id more than once";
			details["evidence_id"] = script.steps[i].evidenceId;
			result.error = makeError( error_codes::kScriptInvalid, "script declares duplicate evidence", std::move( details ) );
			return result;
		}
		declaredEvidenceIds.push_back( script.steps[i].evidenceId );
	}
	std::vector<bool> faultApplied( script.steps.size(), false );

	AgentTrace trace;
	trace.traceId = script.scriptId + "/" + caseValue.digest().substr( 0, 12 );
	trace.caseId = caseValue.caseId;
	trace.agentName = script.agentName;
	trace.agentKind = AgentKind::Fake;
	trace.agentVersion = script.agentVersion;
	trace.seed = script.seed;
	trace.explanation = script.explanation;

	bool aborted = false;
	for ( size_t i = 0; i < script.steps.size() && !aborted; ++i )
	{
		const ScriptStep &step = script.steps[i];
		bool firstAttempt = true;
		while ( true )
		{
			TraceStep recorded;
			recorded.index = static_cast<int>( trace.steps.size() );
			recorded.tool = step.tool;
			recorded.input = step.input;
			recorded.tokens = step.tokens;

			const FaultSpec *fault = faults[i];
			const bool applyFault = fault != nullptr && ( !faultApplied[i] || fault->kind == FaultKind::ToolUnavailable );
			if ( fault != nullptr && applyFault )
				faultApplied[i] = true;

			if ( applyFault && fault->kind == FaultKind::TransientFailure )
			{
				recorded.success = false;
				recorded.errorCode = "TRANSIENT_FAILURE";
				recorded.payload = mergedPayload( step.payload, "transient_failure" );
			}
			else if ( applyFault && fault->kind == FaultKind::ToolUnavailable )
			{
				recorded.success = false;
				recorded.errorCode = "TOOL_NOT_FOUND";
				recorded.payload = mergedPayload( step.payload, "tool_unavailable" );
			}
			else if ( applyFault && fault->kind == FaultKind::CorruptedResult )
			{
				recorded.success = true;
				recorded.payload = mergedPayload( step.payload, "corrupted_result" );
			}
			else
			{
				recorded.success = true;
				recorded.payload = step.payload;
			}

			trace.steps.push_back( std::move( recorded ) );

			if ( trace.steps.back().success )
			{
				// Evidence declaration: only from a successful execution.
				if ( !step.evidenceId.empty() )
				{
					TraceEvidence evidence;
					evidence.id = step.evidenceId;
					for ( const EvidenceExpectation &expectation : caseValue.evidence )
					{
						if ( expectation.id == step.evidenceId )
							evidence.kind = expectation.kind;
					}
					const Json::Value &payload = trace.steps.back().payload;
					if ( payload.isMember( "output_path" ) && payload["output_path"].isString() )
						evidence.path = payload["output_path"].asString();
					if ( payload.isMember( "fields" ) && payload["fields"].isObject() )
						evidence.fields = payload["fields"];
					if ( payload.isMember( "verdict" ) && payload["verdict"].isString() &&
					     isKnownVerdict( payload["verdict"].asString() ) )
						evidence.verdict = payload["verdict"].asString();
					trace.evidence.push_back( std::move( evidence ) );
				}
				break;
			}

			// Failure policy.
			if ( step.onFailure == OnFailure::Skip && firstAttempt )
			{
				// Record the failure, route around, keep going.
				break;
			}
			if ( step.onFailure == OnFailure::RetryOnce && firstAttempt )
			{
				firstAttempt = false;
				continue;
			}
			// Abort (or retry budget exhausted): honest stop.
			aborted = true;
			break;
		}
	}

	if ( !script.stopReasonOverride.empty() )
		parseStopReason( script.stopReasonOverride, trace.stopReason );
	else
		trace.stopReason = aborted ? StopReason::GaveUp : StopReason::Completed;

	if ( script.hasOutcomeClaim )
	{
		trace.outcomeClaimSuccess = script.outcomeClaimSuccess;
		trace.outcomeClaimNote = script.outcomeClaimNote;
	}
	else
	{
		// Default claim is honest: success only when the flow completed.
		trace.outcomeClaimSuccess = trace.stopReason == StopReason::Completed;
	}

	result.trace = std::move( trace );
	return result;
}

} // namespace sicnu::agentbench
