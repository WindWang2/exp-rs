// src/agentbench/case_schema.cpp
#include "case_schema.h"

#include "failure_taxonomy.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::agentbench
{
namespace
{

constexpr const char *kEvidenceKinds[] = { "raster", "vector", "map", "table", "report" };
constexpr const char *kVerdicts[] = { "PASS", "PASS_WITH_WARNINGS", "FAIL" };

BenchError invalidField( std::string field, const std::string &why )
{
	Json::Value details{Json::objectValue};
	details["field"] = std::move( field );
	details["reason"] = why;
	return makeError( error_codes::kCaseInvalid, "case document violates the v1 schema", std::move( details ) );
}

bool isNonEmptyString( const Json::Value &value )
{
	return value.isString() && !value.asString().empty();
}

bool isIntegral( const Json::Value &value )
{
	return value.isIntegral();
}

struct EnumTable
{
	const char *wire;
	int value;
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

const EnumTable kTaskFamilies[] = {
	{ "optical", int( TaskFamily::Optical ) },
	{ "classification", int( TaskFamily::Classification ) },
	{ "change", int( TaskFamily::Change ) },
	{ "temporal", int( TaskFamily::Temporal ) },
	{ "model", int( TaskFamily::Model ) },
	{ "map_delivery", int( TaskFamily::MapDelivery ) },
};

const EnumTable kInvariantDimensions[] = {
	{ "scientific", int( InvariantDimension::Scientific ) },
	{ "process", int( InvariantDimension::Process ) },
};

const EnumTable kSeverities[] = {
	{ "error", int( Severity::Error ) },
	{ "warning", int( Severity::Warning ) },
};

const EnumTable kInvariantKinds[] = {
	{ "tool_used", int( InvariantKind::ToolUsed ) },
	{ "tool_not_used", int( InvariantKind::ToolNotUsed ) },
	{ "step_order", int( InvariantKind::StepOrder ) },
	{ "result_success", int( InvariantKind::ResultSuccess ) },
	{ "error_code_present", int( InvariantKind::ErrorCodePresent ) },
	{ "evidence_exists", int( InvariantKind::EvidenceExists ) },
	{ "field_equals", int( InvariantKind::FieldEquals ) },
	{ "field_contains", int( InvariantKind::FieldContains ) },
	{ "numeric_le", int( InvariantKind::NumericLe ) },
	{ "numeric_ge", int( InvariantKind::NumericGe ) },
	{ "verdict_is", int( InvariantKind::VerdictIs ) },
	{ "explanation_mentions", int( InvariantKind::ExplanationMentions ) },
	{ "claim_consistent", int( InvariantKind::ClaimConsistent ) },
	{ "budget_within", int( InvariantKind::BudgetWithin ) },
};

const EnumTable kFaultKinds[] = {
	{ "transient_failure", int( FaultKind::TransientFailure ) },
	{ "corrupted_result", int( FaultKind::CorruptedResult ) },
	{ "tool_unavailable", int( FaultKind::ToolUnavailable ) },
};

bool isKnownEvidenceKind( const std::string &kind )
{
	for ( const char *candidate : kEvidenceKinds )
	{
		if ( kind == candidate )
			return true;
	}
	return false;
}

bool isKnownVerdict( const std::string &verdict )
{
	for ( const char *candidate : kVerdicts )
	{
		if ( verdict == candidate )
			return true;
	}
	return false;
}

bool hasUniqueNonEmptyStrings( const Json::Value &array, std::string &duplicate )
{
	std::vector<std::string> seen;
	for ( const Json::Value &entry : array )
	{
		if ( !isNonEmptyString( entry ) )
			return false;
		for ( const std::string &prior : seen )
		{
			if ( prior == entry.asString() )
			{
				duplicate = entry.asString();
				return false;
			}
		}
		seen.push_back( entry.asString() );
	}
	return true;
}

bool isDeclaredEvidenceId( const Json::Value &params, const Json::Value &declaredEvidence )
{
	const Json::Value &evidenceId = params["evidence_id"];
	if ( !isNonEmptyString( evidenceId ) )
		return false;
	for ( const Json::Value &entry : declaredEvidence )
	{
		if ( entry.isObject() && entry["id"].asString() == evidenceId.asString() )
			return true;
	}
	return false;
}

BenchError validateInvariant( const Json::Value &entry, size_t index, const Json::Value &declaredEvidence )
{
	const std::string at = "invariants[" + std::to_string( index ) + "]";
	if ( !entry.isObject() )
		return invalidField( at, "invariant must be an object" );
	if ( !isNonEmptyString( entry["id"] ) )
		return invalidField( at + ".id", "invariant id must be a non-empty string" );

	int kindValue = 0;
	if ( !isNonEmptyString( entry["kind"] ) ||
	     !lookupEnum( kInvariantKinds, sizeof( kInvariantKinds ) / sizeof( kInvariantKinds[0] ), entry["kind"].asString(), kindValue ) )
		return invalidField( at + ".kind", "unknown invariant kind" );

	int dimensionValue = 0;
	if ( !isNonEmptyString( entry["dimension"] ) ||
	     !lookupEnum( kInvariantDimensions, sizeof( kInvariantDimensions ) / sizeof( kInvariantDimensions[0] ), entry["dimension"].asString(), dimensionValue ) )
		return invalidField( at + ".dimension", "unknown invariant dimension" );

	int severityValue = 0;
	if ( !isNonEmptyString( entry["severity"] ) ||
	     !lookupEnum( kSeverities, sizeof( kSeverities ) / sizeof( kSeverities[0] ), entry["severity"].asString(), severityValue ) )
		return invalidField( at + ".severity", "unknown invariant severity" );

	const Json::Value &params = entry["params"];
	if ( !params.isObject() )
		return invalidField( at + ".params", "params must be an object" );

	const auto kind = static_cast<InvariantKind>( kindValue );
	const auto requireString = [&]( const char *key ) -> BenchError {
		if ( !isNonEmptyString( params[key] ) )
			return invalidField( at + ".params." + key, std::string( key ) + " must be a non-empty string" );
		return BenchError{};
	};
	const auto requireStepIndex = [&]( const char *key ) -> BenchError {
		if ( !isIntegral( params[key] ) || params[key].asInt() < 0 )
			return invalidField( at + ".params." + key, std::string( key ) + " must be a non-negative integer" );
		return BenchError{};
	};
	const auto requireEvidenceRef = [&]() -> BenchError {
		if ( !isDeclaredEvidenceId( params, declaredEvidence ) )
		{
			if ( !isNonEmptyString( params["evidence_id"] ) )
				return invalidField( at + ".params.evidence_id", "evidence_id must be a non-empty string" );
			return invalidField( at + ".params.evidence_id", "evidence_id references undeclared expected_evidence" );
		}
		return BenchError{};
	};

	switch ( kind )
	{
		case InvariantKind::ToolUsed:
		case InvariantKind::ToolNotUsed:
			if ( BenchError error = requireString( "tool" ); error )
				return error;
			break;
		case InvariantKind::StepOrder:
		{
			const Json::Value &steps = params["steps"];
			std::string duplicate;
			if ( !steps.isArray() || steps.size() < 2 || !hasUniqueNonEmptyStrings( steps, duplicate ) )
				return invalidField( at + ".params.steps", "steps must be an array of at least 2 unique non-empty tool names" );
			break;
		}
		case InvariantKind::ResultSuccess:
			if ( BenchError error = requireStepIndex( "step_index" ); error )
				return error;
			break;
		case InvariantKind::ErrorCodePresent:
			if ( BenchError error = requireStepIndex( "step_index" ); error )
				return error;
			if ( BenchError error = requireString( "error_code" ); error )
				return error;
			break;
		case InvariantKind::EvidenceExists:
			if ( BenchError error = requireEvidenceRef(); error )
				return error;
			break;
		case InvariantKind::FieldEquals:
		case InvariantKind::FieldContains:
			if ( BenchError error = requireEvidenceRef(); error )
				return error;
			if ( BenchError error = requireString( "field" ); error )
				return error;
			if ( !params.isMember( "value" ) )
				return invalidField( at + ".params.value", "value is required" );
			break;
		case InvariantKind::NumericLe:
		case InvariantKind::NumericGe:
			if ( BenchError error = requireEvidenceRef(); error )
				return error;
			if ( BenchError error = requireString( "field" ); error )
				return error;
			if ( kind == InvariantKind::NumericLe && !params["max"].isNumeric() )
				return invalidField( at + ".params.max", "max must be a number" );
			if ( kind == InvariantKind::NumericGe && !params["min"].isNumeric() )
				return invalidField( at + ".params.min", "min must be a number" );
			break;
		case InvariantKind::VerdictIs:
			if ( BenchError error = requireEvidenceRef(); error )
				return error;
			if ( !isNonEmptyString( params["verdict"] ) || !isKnownVerdict( params["verdict"].asString() ) )
				return invalidField( at + ".params.verdict", "verdict must be one of PASS|PASS_WITH_WARNINGS|FAIL" );
			break;
		case InvariantKind::ExplanationMentions:
			if ( BenchError error = requireString( "phrase" ); error )
				return error;
			break;
		case InvariantKind::ClaimConsistent:
		case InvariantKind::BudgetWithin:
			break;
	}
	return BenchError{};
}

} // namespace

std::string taskFamilyToString( TaskFamily family )
{
	return enumToWire( kTaskFamilies, sizeof( kTaskFamilies ) / sizeof( kTaskFamilies[0] ), int( family ) );
}

bool parseTaskFamily( const std::string &wire, TaskFamily &out )
{
	int value = 0;
	if ( !lookupEnum( kTaskFamilies, sizeof( kTaskFamilies ) / sizeof( kTaskFamilies[0] ), wire, value ) )
		return false;
	out = static_cast<TaskFamily>( value );
	return true;
}

std::string invariantDimensionToString( InvariantDimension dimension )
{
	return enumToWire( kInvariantDimensions, sizeof( kInvariantDimensions ) / sizeof( kInvariantDimensions[0] ), int( dimension ) );
}

bool parseInvariantDimension( const std::string &wire, InvariantDimension &out )
{
	int value = 0;
	if ( !lookupEnum( kInvariantDimensions, sizeof( kInvariantDimensions ) / sizeof( kInvariantDimensions[0] ), wire, value ) )
		return false;
	out = static_cast<InvariantDimension>( value );
	return true;
}

std::string severityToString( Severity severity )
{
	return enumToWire( kSeverities, sizeof( kSeverities ) / sizeof( kSeverities[0] ), int( severity ) );
}

bool parseSeverity( const std::string &wire, Severity &out )
{
	int value = 0;
	if ( !lookupEnum( kSeverities, sizeof( kSeverities ) / sizeof( kSeverities[0] ), wire, value ) )
		return false;
	out = static_cast<Severity>( value );
	return true;
}

std::string invariantKindToString( InvariantKind kind )
{
	return enumToWire( kInvariantKinds, sizeof( kInvariantKinds ) / sizeof( kInvariantKinds[0] ), int( kind ) );
}

bool parseInvariantKind( const std::string &wire, InvariantKind &out )
{
	int value = 0;
	if ( !lookupEnum( kInvariantKinds, sizeof( kInvariantKinds ) / sizeof( kInvariantKinds[0] ), wire, value ) )
		return false;
	out = static_cast<InvariantKind>( value );
	return true;
}

std::string faultKindToString( FaultKind kind )
{
	return enumToWire( kFaultKinds, sizeof( kFaultKinds ) / sizeof( kFaultKinds[0] ), int( kind ) );
}

bool parseFaultKind( const std::string &wire, FaultKind &out )
{
	int value = 0;
	if ( !lookupEnum( kFaultKinds, sizeof( kFaultKinds ) / sizeof( kFaultKinds[0] ), wire, value ) )
		return false;
	out = static_cast<FaultKind>( value );
	return true;
}

CaseParse parseCase( const std::string &jsonText )
{
	CaseParse result;

	Json::CharReaderBuilder builder;
	builder["collectComments"] = false;
	// Structural defense in depth: the corpus is trusted repo data, but the
	// reader still gets an explicit depth bound (the repo has a history of
	// jsoncpp depth bombs — see issues #1154/#1155; we simply do not rely on
	// defaults anywhere new).
	builder["stackLimit"] = 128;
	std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
	Json::Value root;
	std::string parseErrors;
	if ( !reader->parse( jsonText.data(), jsonText.data() + jsonText.size(), &root, &parseErrors ) )
	{
		Json::Value details{Json::objectValue};
		details["reason"] = parseErrors;
		result.error = makeError( error_codes::kCaseMalformed, "document is not valid JSON", std::move( details ) );
		return result;
	}
	if ( !root.isObject() )
	{
		result.error = makeError( error_codes::kCaseMalformed, "case document root must be an object" );
		return result;
	}

	// Version gate first: never reinterpret a foreign schema.
	if ( !isNonEmptyString( root["schema"] ) || root["schema"].asString() != kAgentCaseSchemaTag )
	{
		Json::Value details{Json::objectValue};
		details["found"] = root["schema"].isString() ? root["schema"].asString() : "";
		details["expected"] = kAgentCaseSchemaTag;
		result.error = makeError( error_codes::kSchemaVersionUnknown, "unsupported case schema version", std::move( details ) );
		return result;
	}

	AgentCase parsed;
	parsed.raw = root;

	if ( !isNonEmptyString( root["case_id"] ) )
	{
		result.error = invalidField( "case_id", "case_id must be a non-empty string" );
		return result;
	}
	parsed.caseId = root["case_id"].asString();

	if ( !isNonEmptyString( root["title"] ) )
	{
		result.error = invalidField( "title", "title must be a non-empty string" );
		return result;
	}
	parsed.title = root["title"].asString();

	if ( root.isMember( "description" ) && !root["description"].isNull() )
	{
		if ( !root["description"].isString() )
		{
			result.error = invalidField( "description", "description must be a string" );
			return result;
		}
		parsed.description = root["description"].asString();
	}

	if ( !isNonEmptyString( root["task_family"] ) ||
	     !parseTaskFamily( root["task_family"].asString(), parsed.family ) )
	{
		result.error = invalidField( "task_family", "task_family must be one of optical|classification|change|temporal|model|map_delivery" );
		return result;
	}

	if ( !isNonEmptyString( root["goal"] ) )
	{
		result.error = invalidField( "goal", "goal must be a non-empty string" );
		return result;
	}
	parsed.goal = root["goal"].asString();

	if ( !root["initial_state"].isObject() )
	{
		result.error = invalidField( "initial_state", "initial_state must be an object" );
		return result;
	}
	if ( root["initial_state"].isMember( "workspace_roots" ) )
	{
		std::string duplicate;
		if ( !root["initial_state"]["workspace_roots"].isArray() ||
		     !hasUniqueNonEmptyStrings( root["initial_state"]["workspace_roots"], duplicate ) )
		{
			result.error = invalidField( "initial_state.workspace_roots", "workspace_roots must be an array of unique non-empty strings" );
			return result;
		}
	}
	parsed.initialState = root["initial_state"];

	const Json::Value &tools = root["allowed_tools"];
	std::string duplicateTool;
	if ( !tools.isArray() || tools.empty() || !hasUniqueNonEmptyStrings( tools, duplicateTool ) )
	{
		result.error = invalidField( "allowed_tools", "allowed_tools must be a non-empty array of unique non-empty strings" );
		return result;
	}
	for ( const Json::Value &tool : tools )
		parsed.allowedTools.push_back( tool.asString() );

	// Evidence is validated before invariants: invariant params carry
	// cross-references to declared expected_evidence ids.
	const Json::Value &evidence = root["expected_evidence"];
	// Expected evidence may be empty: refusal/impossible tasks legitimately
	// demand no artifact (the correct run delivers nothing on purpose).
	if ( !evidence.isArray() )
	{
		result.error = invalidField( "expected_evidence", "expected_evidence must be an array (possibly empty)" );
		return result;
	}
	for ( const Json::Value &entry : evidence )
	{
		const std::string at = "expected_evidence[" + std::to_string( parsed.evidence.size() ) + "]";
		if ( !entry.isObject() )
		{
			result.error = invalidField( at, "expected evidence entry must be an object" );
			return result;
		}
		if ( !isNonEmptyString( entry["id"] ) )
		{
			result.error = invalidField( at + ".id", "evidence id must be a non-empty string" );
			return result;
		}
		for ( const EvidenceExpectation &prior : parsed.evidence )
		{
			if ( prior.id == entry["id"].asString() )
			{
				result.error = invalidField( at + ".id", "duplicate evidence id" );
				return result;
			}
		}
		if ( !isNonEmptyString( entry["kind"] ) || !isKnownEvidenceKind( entry["kind"].asString() ) )
		{
			result.error = invalidField( at + ".kind", "evidence kind must be one of raster|vector|map|table|report" );
			return result;
		}
		EvidenceExpectation expectation;
		expectation.id = entry["id"].asString();
		expectation.kind = entry["kind"].asString();
		if ( entry.isMember( "path" ) )
		{
			if ( !isNonEmptyString( entry["path"] ) )
			{
				result.error = invalidField( at + ".path", "path must be a non-empty string" );
				return result;
			}
			expectation.path = entry["path"].asString();
		}
		if ( entry.isMember( "require_in_explanation" ) )
		{
			if ( !entry["require_in_explanation"].isBool() )
			{
				result.error = invalidField( at + ".require_in_explanation", "require_in_explanation must be a boolean" );
				return result;
			}
			expectation.requireInExplanation = entry["require_in_explanation"].asBool();
		}
		if ( entry.isMember( "required_fields" ) )
		{
			std::string duplicate;
			if ( !entry["required_fields"].isArray() || !hasUniqueNonEmptyStrings( entry["required_fields"], duplicate ) )
			{
				result.error = invalidField( at + ".required_fields", "required_fields must be an array of unique non-empty strings" );
				return result;
			}
			for ( const Json::Value &field : entry["required_fields"] )
				expectation.requiredFields.push_back( field.asString() );
		}
		parsed.evidence.push_back( std::move( expectation ) );
	}

	const Json::Value &invariants = root["invariants"];
	if ( !invariants.isArray() || invariants.empty() )
	{
		result.error = invalidField( "invariants", "invariants must be a non-empty array" );
		return result;
	}
	for ( const Json::Value &entry : invariants )
	{
		BenchError invariantError = validateInvariant( entry, parsed.invariants.size(), evidence );
		if ( invariantError )
		{
			result.error = std::move( invariantError );
			return result;
		}
		Invariant invariant;
		invariant.id = entry["id"].asString();
		parseInvariantKind( entry["kind"].asString(), invariant.kind );
		parseInvariantDimension( entry["dimension"].asString(), invariant.dimension );
		parseSeverity( entry["severity"].asString(), invariant.severity );
		invariant.params = entry["params"];
		parsed.invariants.push_back( std::move( invariant ) );
	}
	for ( size_t i = 0; i < parsed.invariants.size(); ++i )
	{
		for ( size_t j = 0; j < i; ++j )
		{
			if ( parsed.invariants[i].id == parsed.invariants[j].id )
			{
				result.error = invalidField( "invariants[" + std::to_string( i ) + "].id", "duplicate invariant id" );
				return result;
			}
		}
	}

	const Json::Value &budget = root["resource_budget"];
	if ( !budget.isObject() )
	{
		result.error = invalidField( "resource_budget", "resource_budget must be an object" );
		return result;
	}
	if ( !isIntegral( budget["max_tool_calls"] ) || budget["max_tool_calls"].asInt() < 1 )
	{
		result.error = invalidField( "resource_budget.max_tool_calls", "max_tool_calls must be a positive integer" );
		return result;
	}
	parsed.budget.maxToolCalls = budget["max_tool_calls"].asInt();
	if ( !isIntegral( budget["max_tokens"] ) || budget["max_tokens"].asInt() < 1 )
	{
		result.error = invalidField( "resource_budget.max_tokens", "max_tokens must be a positive integer" );
		return result;
	}
	parsed.budget.maxTokens = budget["max_tokens"].asInt();
	if ( budget.isMember( "max_retries" ) )
	{
		if ( !isIntegral( budget["max_retries"] ) || budget["max_retries"].asInt() < 0 )
		{
			result.error = invalidField( "resource_budget.max_retries", "max_retries must be a non-negative integer" );
			return result;
		}
		parsed.budget.maxRetries = budget["max_retries"].asInt();
	}

	if ( !isIntegral( root["minimal_steps"] ) || root["minimal_steps"].asInt() < 1 )
	{
		result.error = invalidField( "minimal_steps", "minimal_steps must be a positive integer" );
		return result;
	}
	parsed.minimalSteps = root["minimal_steps"].asInt();

	if ( root.isMember( "redundant_tools" ) )
	{
		std::string duplicate;
		if ( !root["redundant_tools"].isArray() || !hasUniqueNonEmptyStrings( root["redundant_tools"], duplicate ) )
		{
			result.error = invalidField( "redundant_tools", "redundant_tools must be an array of unique non-empty strings" );
			return result;
		}
		for ( const Json::Value &tool : root["redundant_tools"] )
			parsed.redundantTools.push_back( tool.asString() );
	}

	if ( root.isMember( "faults" ) )
	{
		const Json::Value &faults = root["faults"];
		if ( !faults.isArray() )
		{
			result.error = invalidField( "faults", "faults must be an array" );
			return result;
		}
		for ( const Json::Value &entry : faults )
		{
			const std::string at = "faults[" + std::to_string( parsed.faults.size() ) + "]";
			if ( !entry.isObject() )
			{
				result.error = invalidField( at, "fault entry must be an object" );
				return result;
			}
			FaultSpec fault;
			if ( !isNonEmptyString( entry["kind"] ) || !parseFaultKind( entry["kind"].asString(), fault.kind ) )
			{
				result.error = invalidField( at + ".kind", "fault kind must be one of transient_failure|corrupted_result|tool_unavailable" );
				return result;
			}
			if ( !isIntegral( entry["at_step"] ) || entry["at_step"].asInt() < 0 )
			{
				result.error = invalidField( at + ".at_step", "at_step must be a non-negative integer" );
				return result;
			}
			fault.atStep = entry["at_step"].asInt();
			if ( entry.isMember( "tool" ) )
			{
				if ( !isNonEmptyString( entry["tool"] ) )
				{
					result.error = invalidField( at + ".tool", "tool must be a non-empty string" );
					return result;
				}
				fault.tool = entry["tool"].asString();
			}
			parsed.faults.push_back( std::move( fault ) );
		}
	}

	if ( root.isMember( "failure_expectation" ) )
	{
		const Json::Value &expectation = root["failure_expectation"];
		if ( !expectation.isObject() || !isNonEmptyString( expectation["failure_class"] ) ||
		     !isValidFailureClass( expectation["failure_class"].asString() ) )
		{
			result.error = invalidField( "failure_expectation.failure_class", "failure_class must name a class from the closed failure taxonomy" );
			return result;
		}
		parsed.failureExpectation = expectation;
	}

	result.parsed = std::move( parsed );
	return result;
}

Json::Value caseToJson( const AgentCase &caseValue )
{
	Json::Value doc{Json::objectValue};
	doc["schema"] = kAgentCaseSchemaTag;
	doc["case_id"] = caseValue.caseId;
	doc["title"] = caseValue.title;
	if ( !caseValue.description.empty() )
		doc["description"] = caseValue.description;
	doc["task_family"] = taskFamilyToString( caseValue.family );
	doc["goal"] = caseValue.goal;
	doc["initial_state"] = caseValue.initialState;

	Json::Value tools{Json::arrayValue};
	for ( const std::string &tool : caseValue.allowedTools )
		tools.append( tool );
	doc["allowed_tools"] = tools;

	Json::Value invariants{Json::arrayValue};
	for ( const Invariant &invariant : caseValue.invariants )
	{
		Json::Value entry{Json::objectValue};
		entry["id"] = invariant.id;
		entry["kind"] = invariantKindToString( invariant.kind );
		entry["dimension"] = invariantDimensionToString( invariant.dimension );
		entry["severity"] = severityToString( invariant.severity );
		entry["params"] = invariant.params;
		invariants.append( entry );
	}
	doc["invariants"] = invariants;

	Json::Value evidence{Json::arrayValue};
	for ( const EvidenceExpectation &expectation : caseValue.evidence )
	{
		Json::Value entry{Json::objectValue};
		entry["id"] = expectation.id;
		entry["kind"] = expectation.kind;
		if ( !expectation.path.empty() )
			entry["path"] = expectation.path;
		if ( expectation.requireInExplanation )
			entry["require_in_explanation"] = true;
		Json::Value fields{Json::arrayValue};
		for ( const std::string &field : expectation.requiredFields )
			fields.append( field );
		entry["required_fields"] = fields;
		evidence.append( entry );
	}
	doc["expected_evidence"] = evidence;

	Json::Value budget{Json::objectValue};
	budget["max_tool_calls"] = caseValue.budget.maxToolCalls;
	budget["max_tokens"] = caseValue.budget.maxTokens;
	budget["max_retries"] = caseValue.budget.maxRetries;
	doc["resource_budget"] = budget;

	doc["minimal_steps"] = caseValue.minimalSteps;

	if ( !caseValue.redundantTools.empty() )
	{
		Json::Value redundant{Json::arrayValue};
		for ( const std::string &tool : caseValue.redundantTools )
			redundant.append( tool );
		doc["redundant_tools"] = redundant;
	}

	if ( !caseValue.faults.empty() )
	{
		Json::Value faults{Json::arrayValue};
		for ( const FaultSpec &fault : caseValue.faults )
		{
			Json::Value entry{Json::objectValue};
			entry["kind"] = faultKindToString( fault.kind );
			entry["at_step"] = fault.atStep;
			if ( !fault.tool.empty() )
				entry["tool"] = fault.tool;
			faults.append( entry );
		}
		doc["faults"] = faults;
	}

	if ( !caseValue.failureExpectation.isNull() )
		doc["failure_expectation"] = caseValue.failureExpectation;

	return doc;
}

std::vector<std::string> caseScopeRoots( const AgentCase &caseValue )
{
	std::vector<std::string> roots;
	const Json::Value &declared = caseValue.initialState["workspace_roots"];
	if ( declared.isArray() )
	{
		for ( const Json::Value &root : declared )
			if ( root.isString() )
				roots.push_back( root.asString() );
	}
	return roots;
}

} // namespace sicnu::agentbench
