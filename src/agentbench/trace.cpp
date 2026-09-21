// src/agentbench/trace.cpp
#include "trace.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::agentbench
{
namespace
{

constexpr const char *kVerdicts[] = { "PASS", "PASS_WITH_WARNINGS", "FAIL" };
constexpr const char *kEvidenceKinds[] = { "raster", "vector", "map", "table", "report" };

struct EnumTable
{
	const char *wire;
	int value;
};

const EnumTable kAgentKinds[] = {
	{ "fake", int( AgentKind::Fake ) },
	{ "recorded", int( AgentKind::Recorded ) },
	{ "live", int( AgentKind::Live ) },
};

const EnumTable kStopReasons[] = {
	{ "completed", int( StopReason::Completed ) },
	{ "budget_exhausted", int( StopReason::BudgetExhausted ) },
	{ "gave_up", int( StopReason::GaveUp ) },
	{ "blocked", int( StopReason::Blocked ) },
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

const char *enumToWire( const EnumTable *table, size_t size, int value )
{
	for ( size_t i = 0; i < size; ++i )
	{
		if ( table[i].value == value )
			return table[i].wire;
	}
	return "";
}

BenchError traceInvalid( std::string field, const std::string &why )
{
	Json::Value details{Json::objectValue};
	details["field"] = std::move( field );
	details["reason"] = why;
	return makeError( error_codes::kTraceInvalid, "trace document violates the v1 schema", std::move( details ) );
}

bool isNonEmptyString( const Json::Value &value )
{
	return value.isString() && !value.asString().empty();
}

bool isKnownEvidenceKind( const std::string &kind )
{
	for ( const char *candidate : kEvidenceKinds )
		if ( kind == candidate )
			return true;
	return false;
}

bool isKnownVerdict( const std::string &verdict )
{
	for ( const char *candidate : kVerdicts )
		if ( verdict == candidate )
			return true;
	return false;
}

bool pathInsideRoot( const std::string &path, const std::string &root )
{
	if ( path == root )
		return true;
	if ( path.size() <= root.size() || path.compare( 0, root.size(), root ) != 0 )
		return false;
	return path[root.size()] == '/';
}

/// Top-level path-typed members of an object: key == "path" or key ends with
/// "_path". Values must stay inside one of the declared scope roots.
void collectPathViolations( const Json::Value &object, const std::vector<std::string> &roots,
                            const std::string &where, std::vector<ReplayViolation> &out )
{
	if ( !object.isObject() )
		return;
	for ( const auto &name : object.getMemberNames() )
	{
		const bool isPathKey = name == "path" || ( name.size() > 5 && name.compare( name.size() - 5, 5, "_path" ) == 0 );
		if ( !isPathKey || !object[name].isString() || object[name].asString().empty() )
			continue;
		const std::string &path = object[name].asString();
		bool inside = roots.empty(); // no declared roots → no constraint
		for ( const std::string &root : roots )
		{
			if ( pathInsideRoot( path, root ) )
			{
				inside = true;
				break;
			}
		}
		if ( !inside )
		{
			ReplayViolation violation;
			violation.code = error_codes::kPathOutsideScope;
			violation.summary = "trace path escapes the case-declared workspace scope";
			violation.details["path"] = path;
			violation.details["where"] = where;
			out.push_back( std::move( violation ) );
		}
	}
}

} // namespace

std::string agentKindToString( AgentKind kind )
{
	return enumToWire( kAgentKinds, sizeof( kAgentKinds ) / sizeof( kAgentKinds[0] ), int( kind ) );
}

bool parseAgentKind( const std::string &wire, AgentKind &out )
{
	int value = 0;
	if ( !lookupEnum( kAgentKinds, sizeof( kAgentKinds ) / sizeof( kAgentKinds[0] ), wire, value ) )
		return false;
	out = static_cast<AgentKind>( value );
	return true;
}

std::string stopReasonToString( StopReason reason )
{
	return enumToWire( kStopReasons, sizeof( kStopReasons ) / sizeof( kStopReasons[0] ), int( reason ) );
}

bool parseStopReason( const std::string &wire, StopReason &out )
{
	int value = 0;
	if ( !lookupEnum( kStopReasons, sizeof( kStopReasons ) / sizeof( kStopReasons[0] ), wire, value ) )
		return false;
	out = static_cast<StopReason>( value );
	return true;
}

TraceParse parseTrace( const std::string &jsonText )
{
	TraceParse result;

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
		result.error = makeError( error_codes::kTraceMalformed, "document is not valid JSON", std::move( details ) );
		return result;
	}
	if ( !root.isObject() )
	{
		result.error = makeError( error_codes::kTraceMalformed, "trace document root must be an object" );
		return result;
	}

	if ( !isNonEmptyString( root["schema"] ) || root["schema"].asString() != kAgentTraceSchemaTag )
	{
		Json::Value details{Json::objectValue};
		details["found"] = root["schema"].isString() ? root["schema"].asString() : "";
		details["expected"] = kAgentTraceSchemaTag;
		result.error = makeError( error_codes::kSchemaVersionUnknown, "unsupported trace schema version", std::move( details ) );
		return result;
	}

	AgentTrace trace;
	trace.raw = root;

	if ( !isNonEmptyString( root["trace_id"] ) )
	{
		result.error = traceInvalid( "trace_id", "trace_id must be a non-empty string" );
		return result;
	}
	trace.traceId = root["trace_id"].asString();

	if ( !isNonEmptyString( root["case_id"] ) )
	{
		result.error = traceInvalid( "case_id", "case_id must be a non-empty string" );
		return result;
	}
	trace.caseId = root["case_id"].asString();

	const Json::Value &agent = root["agent"];
	if ( !agent.isObject() || !isNonEmptyString( agent["name"] ) )
	{
		result.error = traceInvalid( "agent.name", "agent.name must be a non-empty string" );
		return result;
	}
	trace.agentName = agent["name"].asString();
	if ( !isNonEmptyString( agent["kind"] ) || !parseAgentKind( agent["kind"].asString(), trace.agentKind ) )
	{
		result.error = traceInvalid( "agent.kind", "agent.kind must be one of fake|recorded|live" );
		return result;
	}
	if ( !isNonEmptyString( agent["version"] ) )
	{
		result.error = traceInvalid( "agent.version", "agent.version must be a non-empty string" );
		return result;
	}
	trace.agentVersion = agent["version"].asString();

	if ( root.isMember( "seed" ) )
	{
		if ( !root["seed"].isIntegral() || root["seed"].asInt64() < 0 )
		{
			result.error = traceInvalid( "seed", "seed must be a non-negative integer" );
			return result;
		}
		trace.seed = root["seed"].asInt64();
	}

	const Json::Value &steps = root["steps"];
	if ( !steps.isArray() )
	{
		result.error = traceInvalid( "steps", "steps must be an array (possibly empty)" );
		return result;
	}
	for ( const Json::Value &entry : steps )
	{
		const std::string at = "steps[" + std::to_string( trace.steps.size() ) + "]";
		if ( !entry.isObject() )
		{
			result.error = traceInvalid( at, "step must be an object" );
			return result;
		}
		TraceStep step;
		step.index = static_cast<int>( trace.steps.size() );
		if ( !entry["index"].isIntegral() || entry["index"].asInt() != step.index )
		{
			result.error = traceInvalid( at + ".index", "step index must equal the array position (canonical form)" );
			return result;
		}
		if ( !isNonEmptyString( entry["tool"] ) )
		{
			result.error = traceInvalid( at + ".tool", "tool must be a non-empty string" );
			return result;
		}
		step.tool = entry["tool"].asString();
		if ( entry.isMember( "input" ) )
		{
			if ( !entry["input"].isObject() )
			{
				result.error = traceInvalid( at + ".input", "input must be an object" );
				return result;
			}
			step.input = entry["input"];
		}
		if ( !entry["success"].isBool() )
		{
			result.error = traceInvalid( at + ".success", "success must be a boolean" );
			return result;
		}
		step.success = entry["success"].asBool();
		if ( !step.success )
		{
			if ( !isNonEmptyString( entry["error_code"] ) )
			{
				result.error = traceInvalid( at + ".error_code", "a failed step must carry a non-empty error_code" );
				return result;
			}
			step.errorCode = entry["error_code"].asString();
		}
		else if ( entry.isMember( "error_code" ) && isNonEmptyString( entry["error_code"] ) )
		{
			result.error = traceInvalid( at + ".error_code", "a successful step must not carry an error_code" );
			return result;
		}
		if ( entry.isMember( "payload" ) )
		{
			if ( !entry["payload"].isObject() )
			{
				result.error = traceInvalid( at + ".payload", "payload must be an object" );
				return result;
			}
			step.payload = entry["payload"];
		}
		if ( entry.isMember( "tokens" ) )
		{
			if ( !entry["tokens"].isIntegral() || entry["tokens"].asInt() < 0 )
			{
				result.error = traceInvalid( at + ".tokens", "tokens must be a non-negative integer" );
				return result;
			}
			step.tokens = entry["tokens"].asInt();
		}
		trace.steps.push_back( std::move( step ) );
	}

	const Json::Value &evidence = root["final_evidence"];
	if ( !evidence.isNull() )
	{
		if ( !evidence.isArray() )
		{
			result.error = traceInvalid( "final_evidence", "final_evidence must be an array" );
			return result;
		}
		for ( const Json::Value &entry : evidence )
		{
			const std::string at = "final_evidence[" + std::to_string( trace.evidence.size() ) + "]";
			if ( !entry.isObject() )
			{
				result.error = traceInvalid( at, "evidence entry must be an object" );
				return result;
			}
			TraceEvidence item;
			if ( !isNonEmptyString( entry["id"] ) )
			{
				result.error = traceInvalid( at + ".id", "evidence id must be a non-empty string" );
				return result;
			}
			item.id = entry["id"].asString();
			for ( const TraceEvidence &prior : trace.evidence )
			{
				if ( prior.id == item.id )
				{
					result.error = traceInvalid( at + ".id", "duplicate evidence id" );
					return result;
				}
			}
			if ( !isNonEmptyString( entry["kind"] ) || !isKnownEvidenceKind( entry["kind"].asString() ) )
			{
				result.error = traceInvalid( at + ".kind", "evidence kind must be one of raster|vector|map|table|report" );
				return result;
			}
			item.kind = entry["kind"].asString();
			if ( entry.isMember( "path" ) )
			{
				if ( !isNonEmptyString( entry["path"] ) )
				{
					result.error = traceInvalid( at + ".path", "path must be a non-empty string" );
					return result;
				}
				item.path = entry["path"].asString();
			}
			if ( entry.isMember( "fields" ) )
			{
				if ( !entry["fields"].isObject() )
				{
					result.error = traceInvalid( at + ".fields", "fields must be an object" );
					return result;
				}
				item.fields = entry["fields"];
			}
			if ( entry.isMember( "verdict" ) )
			{
				if ( !isNonEmptyString( entry["verdict"] ) || !isKnownVerdict( entry["verdict"].asString() ) )
				{
					result.error = traceInvalid( at + ".verdict", "verdict must be one of PASS|PASS_WITH_WARNINGS|FAIL" );
					return result;
				}
				item.verdict = entry["verdict"].asString();
			}
			trace.evidence.push_back( std::move( item ) );
		}
	}

	if ( root.isMember( "explanation" ) )
	{
		if ( !root["explanation"].isString() )
		{
			result.error = traceInvalid( "explanation", "explanation must be a string" );
			return result;
		}
		trace.explanation = root["explanation"].asString();
	}

	const Json::Value &claim = root["outcome_claim"];
	if ( !claim.isObject() || !claim["success"].isBool() )
	{
		result.error = traceInvalid( "outcome_claim", "outcome_claim must be an object with a boolean success" );
		return result;
	}
	trace.outcomeClaimSuccess = claim["success"].asBool();
	if ( claim.isMember( "note" ) )
	{
		if ( !claim["note"].isString() )
		{
			result.error = traceInvalid( "outcome_claim.note", "outcome_claim.note must be a string" );
			return result;
		}
		trace.outcomeClaimNote = claim["note"].asString();
	}

	if ( !isNonEmptyString( root["stop_reason"] ) || !parseStopReason( root["stop_reason"].asString(), trace.stopReason ) )
	{
		result.error = traceInvalid( "stop_reason", "stop_reason must be one of completed|budget_exhausted|gave_up|blocked" );
		return result;
	}

	result.parsed = std::move( trace );
	return result;
}

Json::Value traceToJson( const AgentTrace &trace )
{
	Json::Value doc{Json::objectValue};
	doc["schema"] = kAgentTraceSchemaTag;
	doc["trace_id"] = trace.traceId;
	doc["case_id"] = trace.caseId;
	Json::Value agent{Json::objectValue};
	agent["name"] = trace.agentName;
	agent["kind"] = agentKindToString( trace.agentKind );
	agent["version"] = trace.agentVersion;
	doc["agent"] = agent;
	doc["seed"] = Json::Value( Json::Int64( trace.seed ) );

	Json::Value steps{Json::arrayValue};
	for ( const TraceStep &step : trace.steps )
	{
		Json::Value entry{Json::objectValue};
		entry["index"] = step.index;
		entry["tool"] = step.tool;
		entry["input"] = step.input;
		entry["success"] = step.success;
		if ( !step.success )
			entry["error_code"] = step.errorCode;
		entry["payload"] = step.payload;
		entry["tokens"] = step.tokens;
		steps.append( entry );
	}
	doc["steps"] = steps;

	Json::Value evidence{Json::arrayValue};
	for ( const TraceEvidence &item : trace.evidence )
	{
		Json::Value entry{Json::objectValue};
		entry["id"] = item.id;
		entry["kind"] = item.kind;
		if ( !item.path.empty() )
			entry["path"] = item.path;
		entry["fields"] = item.fields;
		if ( !item.verdict.empty() )
			entry["verdict"] = item.verdict;
		evidence.append( entry );
	}
	doc["final_evidence"] = evidence;

	doc["explanation"] = trace.explanation;
	Json::Value claim{Json::objectValue};
	claim["success"] = trace.outcomeClaimSuccess;
	if ( !trace.outcomeClaimNote.empty() )
		claim["note"] = trace.outcomeClaimNote;
	doc["outcome_claim"] = claim;
	doc["stop_reason"] = stopReasonToString( trace.stopReason );
	return doc;
}

ReplayValidation validateReplay( const AgentCase &caseValue, const AgentTrace &trace )
{
	ReplayValidation validation;

	// Pairing gate: a trace graded against the wrong case is not graded at all.
	if ( trace.caseId != caseValue.caseId )
	{
		ReplayViolation violation;
		violation.code = error_codes::kTraceInvalid;
		violation.summary = "trace was not produced for this case";
		violation.details["trace_case_id"] = trace.caseId;
		violation.details["case_id"] = caseValue.caseId;
		validation.violations.push_back( std::move( violation ) );
		return validation;
	}

	// Declared workspace scope (case-side, optional).
	std::vector<std::string> roots;
	const Json::Value &declaredRoots = caseValue.initialState["workspace_roots"];
	if ( declaredRoots.isArray() )
	{
		for ( const Json::Value &root : declaredRoots )
			if ( root.isString() )
				roots.push_back( root.asString() );
	}

	for ( size_t i = 0; i < trace.steps.size(); ++i )
	{
		const TraceStep &step = trace.steps[i];
		const std::string where = "steps[" + std::to_string( i ) + "]";
		if ( std::find( caseValue.allowedTools.begin(), caseValue.allowedTools.end(), step.tool ) == caseValue.allowedTools.end() )
		{
			ReplayViolation violation;
			violation.code = error_codes::kToolNotAllowed;
			violation.summary = "trace step uses a tool outside the case allow-list";
			violation.details["tool"] = step.tool;
			violation.details["step"] = static_cast<Json::Int>( i );
			validation.violations.push_back( std::move( violation ) );
			continue;
		}
		collectPathViolations( step.input, roots, where + ".input", validation.violations );
		collectPathViolations( step.payload, roots, where + ".payload", validation.violations );

		validation.usage.toolCalls++;
		validation.usage.tokens += step.tokens;

		// A retry is a post-failure repeat of an identical (tool, input).
		if ( i > 0 )
		{
			const TraceStep &previous = trace.steps[i - 1];
			if ( !previous.success && step.tool == previous.tool && deterministicSerialize( step.input ) == deterministicSerialize( previous.input ) )
				validation.usage.retries++;
		}
	}

	for ( const TraceEvidence &item : trace.evidence )
	{
		if ( item.path.empty() )
			continue;
		Json::Value holder{Json::objectValue};
		holder["path"] = item.path;
		collectPathViolations( holder, roots, "final_evidence[" + item.id + "]", validation.violations );
	}

	// Resource budget: every exceeded dimension is reported individually.
	const auto checkBudget = [ &validation, &caseValue ]( const char *resource, long long used, long long max ) {
		if ( used > max )
		{
			ReplayViolation violation;
			violation.code = error_codes::kBudgetExceeded;
			violation.summary = "trace exceeds the case resource budget";
			violation.details["resource"] = resource;
			violation.details["used"] = Json::Value( Json::Int64( used ) );
			violation.details["max"] = Json::Value( Json::Int64( max ) );
			validation.violations.push_back( std::move( violation ) );
		}
	};
	checkBudget( "tool_calls", validation.usage.toolCalls, caseValue.budget.maxToolCalls );
	checkBudget( "tokens", validation.usage.tokens, caseValue.budget.maxTokens );
	checkBudget( "retries", validation.usage.retries, caseValue.budget.maxRetries );

	return validation;
}

} // namespace sicnu::agentbench
