// src/agentbench/suite.cpp
#include "suite.h"

#include "fake_agent.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::agentbench
{
namespace
{

bool isNonEmptyString( const Json::Value &value )
{
	return value.isString() && !value.asString().empty();
}

BenchError suiteInvalid( Json::Value details, const std::string &summary )
{
	return makeError( error_codes::kSuiteInvalid, summary, std::move( details ) );
}

BenchError suiteInvalidAt( const std::string &field, const std::string &reason )
{
	Json::Value details{Json::objectValue};
	details["field"] = field;
	details["reason"] = reason;
	return suiteInvalid( std::move( details ), "suite document violates the v1 schema" );
}

/// Report document WITHOUT the digest field.
Json::Value suiteReportShellToJson( const SuiteReport &report )
{
	Json::Value document{Json::objectValue};
	document["schema"] = kAgentSuiteReportSchemaTag;
	document["framework_version"] = kAgentBenchFrameworkVersion;
	document["suite_id"] = report.suiteId;
	document["version"] = report.version;
	document["pack_digest"] = report.packDigest;

	Json::Value cases{Json::arrayValue};
	for ( const SuiteCaseResult &entry : report.cases )
	{
		Json::Value entryJson{Json::objectValue};
		entryJson["case_id"] = entry.caseId;
		entryJson["task_family"] = entry.taskFamily;
		entryJson["case_digest"] = entry.caseDigest;
		entryJson["source_path"] = entry.sourcePath;
		entryJson["source_kind"] = entry.sourceKind;
		entryJson["verdict"] = entry.verdict;
		entryJson["failure_class"] = entry.failureClass;
		entryJson["evaluation_digest"] = entry.evaluationDigest;
		entryJson["metrics"] = entry.metrics;
		cases.append( entryJson );
	}
	document["cases"] = cases;

	Json::Value summary{Json::objectValue};
	summary["pass"] = report.summary.passCount;
	summary["pass_with_warnings"] = report.summary.warningsCount;
	summary["fail"] = report.summary.failCount;
	document["summary"] = summary;

	return document;
}

} // namespace

SuiteParse parseSuite( const std::string &jsonText )
{
	SuiteParse result;

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
		result.error = makeError( error_codes::kSuiteInvalid, "document is not valid JSON", std::move( details ) );
		return result;
	}
	if ( !root.isObject() )
	{
		result.error = makeError( error_codes::kSuiteInvalid, "suite document root must be an object" );
		return result;
	}
	if ( !isNonEmptyString( root["schema"] ) || root["schema"].asString() != kAgentSuiteSchemaTag )
	{
		Json::Value details{Json::objectValue};
		details["found"] = root["schema"].isString() ? root["schema"].asString() : "";
		details["expected"] = kAgentSuiteSchemaTag;
		result.error = makeError( error_codes::kSchemaVersionUnknown, "unsupported suite schema version", std::move( details ) );
		return result;
	}

	SuiteDoc suite;
	suite.raw = root;
	if ( !isNonEmptyString( root["suite_id"] ) )
	{
		result.error = suiteInvalidAt( "suite_id", "suite_id must be a non-empty string" );
		return result;
	}
	suite.suiteId = root["suite_id"].asString();
	if ( !isNonEmptyString( root["version"] ) )
	{
		result.error = suiteInvalidAt( "version", "version must be a non-empty string" );
		return result;
	}
	suite.version = root["version"].asString();
	if ( root.isMember( "description" ) && !root["description"].isString() )
	{
		result.error = suiteInvalidAt( "description", "description must be a string" );
		return result;
	}
	suite.description = root["description"].isNull() ? "" : root["description"].asString();

	const Json::Value &cases = root["cases"];
	if ( !cases.isArray() || cases.empty() )
	{
		result.error = suiteInvalidAt( "cases", "cases must be a non-empty array" );
		return result;
	}
	for ( const Json::Value &entry : cases )
	{
		const std::string at = "cases[" + std::to_string( suite.entries.size() ) + "]";
		if ( !entry.isObject() || !isNonEmptyString( entry["case"] ) )
		{
			result.error = suiteInvalidAt( at + ".case", "entry must name a case path" );
			return result;
		}
		SuiteEntry parsed;
		parsed.casePath = entry["case"].asString();
		const bool hasScript = entry.isMember( "script" ) && isNonEmptyString( entry["script"] );
		const bool hasTrace = entry.isMember( "trace" ) && isNonEmptyString( entry["trace"] );
		if ( hasScript == hasTrace )
		{
			result.error = suiteInvalidAt( at, "entry must name exactly one of script|trace" );
			return result;
		}
		parsed.scriptPath = hasScript ? entry["script"].asString() : "";
		parsed.tracePath = hasTrace ? entry["trace"].asString() : "";
		suite.entries.push_back( std::move( parsed ) );
	}

	result.parsed = std::move( suite );
	return result;
}

SuiteRun runSuite( const SuiteDoc &suite, const Loader &loader )
{
	SuiteRun result;

	// Sorted (casePath → digests) map so the pack digest is order-insensitive
	// over entries while remaining sensitive to every byte of content.
	std::map<std::string, Json::Value> packPins;
	SuiteReport report;
	report.suiteId = suite.suiteId;
	report.version = suite.version;

	for ( const SuiteEntry &entry : suite.entries )
	{
		const std::optional<std::string> caseText = loader( entry.casePath );
		if ( !caseText )
		{
			Json::Value details{Json::objectValue};
			details["path"] = entry.casePath;
			details["reason"] = "case document missing";
			result.error = suiteInvalid( std::move( details ), "suite document missing" );
			return result;
		}
		const CaseParse caseParsed = parseCase( *caseText );
		if ( !caseParsed.parsed )
		{
			Json::Value details{Json::objectValue};
			details["path"] = entry.casePath;
			details["reason"] = "case document failed to parse: " + caseParsed.error.code;
			result.error = suiteInvalid( std::move( details ), "case document failed to parse" );
			return result;
		}
		const AgentCase &agentCase = *caseParsed.parsed;

		const bool fromScript = !entry.scriptPath.empty();
		const std::string sourcePath = fromScript ? entry.scriptPath : entry.tracePath;
		const std::optional<std::string> sourceText = loader( sourcePath );
		if ( !sourceText )
		{
			Json::Value details{Json::objectValue};
			details["path"] = sourcePath;
			details["reason"] = "trace/script document missing";
			result.error = suiteInvalid( std::move( details ), "trace/script document missing" );
			return result;
		}

		AgentTrace trace;
		std::string sourceDigest;
		if ( fromScript )
		{
			const ScriptParse scriptParsed = parseScript( *sourceText );
			if ( !scriptParsed.parsed )
			{
				Json::Value details{Json::objectValue};
				details["path"] = sourcePath;
				details["reason"] = "script failed to parse: " + scriptParsed.error.code;
				result.error = suiteInvalid( std::move( details ), "script failed to parse" );
				return result;
			}
			const ScriptRun scriptRun = runScript( agentCase, *scriptParsed.parsed );
			if ( !scriptRun.trace )
			{
				Json::Value details{Json::objectValue};
				details["path"] = sourcePath;
				details["reason"] = "script misuses the case: " + scriptRun.error.code;
				result.error = suiteInvalid( std::move( details ), "script misuses the case" );
				return result;
			}
			trace = std::move( *scriptRun.trace );
			sourceDigest = scriptParsed.parsed->digest();
		}
		else
		{
			const TraceParse traceParsed = parseTrace( *sourceText );
			if ( !traceParsed.parsed )
			{
				Json::Value details{Json::objectValue};
				details["path"] = sourcePath;
				details["reason"] = "trace failed to parse: " + traceParsed.error.code;
				result.error = suiteInvalid( std::move( details ), "trace failed to parse" );
				return result;
			}
			trace = std::move( *traceParsed.parsed );
			sourceDigest = traceParsed.parsed->digest();
		}

		const CaseEvaluation evaluation = evaluateCase( agentCase, trace );

		SuiteCaseResult entry_result;
		entry_result.caseId = agentCase.caseId;
		entry_result.taskFamily = taskFamilyToString( agentCase.family );
		entry_result.caseDigest = agentCase.digest();
		entry_result.sourcePath = sourcePath;
		entry_result.sourceKind = fromScript ? "script" : "trace";
		entry_result.verdict = benchVerdictToString( evaluation.verdict );
		entry_result.failureClass = evaluation.failureClass;
		entry_result.evaluationDigest = evaluation.digest;
		for ( const MetricResult &metric : evaluation.metrics )
		{
			Json::Value metricJson{Json::objectValue};
			metricJson["name"] = metric.name;
			metricJson["value"] = metric.value;
			metricJson["reason"] = metric.reason;
			entry_result.metrics.append( metricJson );
		}

		Json::Value pin{Json::objectValue};
		pin["case"] = entry_result.caseDigest;
		pin["source"] = sourceDigest;
		pin["source_kind"] = entry_result.sourceKind;
		packPins[entry.casePath] = pin;

		if ( entry_result.verdict == "PASS" )
			report.summary.passCount++;
		else if ( entry_result.verdict == "PASS_WITH_WARNINGS" )
			report.summary.warningsCount++;
		else
			report.summary.failCount++;

		report.cases.push_back( std::move( entry_result ) );
	}

	Json::Value pins{Json::objectValue};
	for ( const auto &pair : packPins )
		pins[pair.first] = pair.second;
	Json::Value pack{Json::objectValue};
	pack["suite"] = suite.digest();
	pack["pins"] = pins;
	report.packDigest = deterministicSerialize( pack );

	report.digest = deterministicSerialize( suiteReportShellToJson( report ) );

	result.report = std::move( report );
	return result;
}

Json::Value suiteReportToJson( const SuiteReport &report )
{
	Json::Value document = suiteReportShellToJson( report );
	document["digest"] = deterministicSerialize( document );
	return document;
}

} // namespace sicnu::agentbench
