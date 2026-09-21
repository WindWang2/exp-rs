// src/agentbench/report.cpp
#include "report.h"

#include "json_writer.h"

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace sicnu::agentbench
{
namespace
{

std::string metricValueText( const MetricResult &metric )
{
	if ( metric.value.isNull() )
		return "n/a (" + metric.reason + ")";
	if ( metric.value.isIntegral() && !metric.value.isDouble() )
	{
		std::ostringstream out;
		out << metric.value.asInt64();
		return out.str();
	}
	char buffer[40];
	std::snprintf( buffer, sizeof( buffer ), "%.3f", metric.value.asDouble() );
	return buffer;
}

} // namespace

std::string renderReportMarkdown( const CaseEvaluation &evaluation )
{
	std::ostringstream out;
	out << "# Agent Benchmark Report\n\n";
	out << "- case: `" << evaluation.caseId << "`\n";
	out << "- trace: `" << evaluation.traceId << "`\n";
	out << "- case digest: `" << evaluation.caseDigest.substr( 0, 16 ) << "…`\n";
	out << "- verdict: **" << benchVerdictToString( evaluation.verdict ) << "**\n";
	out << "- failure class: `" << evaluation.failureClass << "`\n";
	out << "- evaluation digest: `" << evaluation.digest.substr( 0, 16 ) << "…`\n\n";

	out << "## Metrics\n\n";
	out << "| metric | value | notes |\n";
	out << "|---|---|---|\n";
	for ( const MetricResult &metric : evaluation.metrics )
	{
		out << "| " << metric.name << " | " << metricValueText( metric );
		if ( !metric.notes.empty() )
			out << " | " << metric.notes << " |\n";
		else
			out << " | |\n";
	}
	out << "\n";

	out << "## Hidden invariants\n\n";
	out << "| invariant | severity | dimension | passed |\n";
	out << "|---|---|---|---|\n";
	for ( const InvariantResult &result : evaluation.invariants )
	{
		out << "| " << result.invariantId << " | " << severityToString( result.severity ) << " | "
		    << invariantDimensionToString( result.dimension ) << " | " << ( result.passed ? "yes" : "NO" ) << " |\n";
	}
	out << "\n";

	out << "## Resource usage\n\n";
	out << "- tool calls: " << evaluation.usage.toolCalls << "\n";
	out << "- tokens: " << evaluation.usage.tokens << "\n";
	out << "- retries: " << evaluation.usage.retries << "\n";
	return out.str();
}

std::string renderReportJson( const CaseEvaluation &evaluation )
{
	const Json::Value document = evaluationToJson( evaluation );
	Json::StreamWriterBuilder builder;
	builder["indentation"] = "  ";
	builder["commentStyle"] = "None";
	builder["enableYAMLCompatibility"] = false;
	builder["precision"] = 17;
	builder["precisionType"] = "significant";
	return Json::writeString( builder, document );
}

ReportResult writeReport( const CaseEvaluation &evaluation, const std::string &outputDir, const std::string &baseName )
{
	ReportResult result;
	const std::filesystem::path directory( outputDir );
	if ( !std::filesystem::is_directory( directory ) )
	{
		Json::Value details{Json::objectValue};
		details["output_dir"] = outputDir;
		details["reason"] = "output path is not a directory";
		result.error = makeError( error_codes::kReportWriteFailed, "report could not be written", std::move( details ) );
		return result;
	}

	const std::filesystem::path jsonPath = directory / ( baseName + ".json" );
	const std::filesystem::path markdownPath = directory / ( baseName + ".md" );

	{
		std::ofstream jsonOut( jsonPath, std::ios::binary | std::ios::trunc );
		if ( !jsonOut )
		{
			Json::Value details{Json::objectValue};
			details["path"] = jsonPath.string();
			details["reason"] = "cannot open json report for writing";
			result.error = makeError( error_codes::kReportWriteFailed, "report could not be written", std::move( details ) );
			return result;
		}
		jsonOut << renderReportJson( evaluation );
		if ( !jsonOut )
		{
			Json::Value details{Json::objectValue};
			details["path"] = jsonPath.string();
			details["reason"] = "json report write failed";
			result.error = makeError( error_codes::kReportWriteFailed, "report could not be written", std::move( details ) );
			return result;
		}
	}
	{
		std::ofstream markdownOut( markdownPath, std::ios::binary | std::ios::trunc );
		if ( !markdownOut )
		{
			Json::Value details{Json::objectValue};
			details["path"] = markdownPath.string();
			details["reason"] = "cannot open markdown report for writing";
			result.error = makeError( error_codes::kReportWriteFailed, "report could not be written", std::move( details ) );
			return result;
		}
		markdownOut << renderReportMarkdown( evaluation );
		if ( !markdownOut )
		{
			Json::Value details{Json::objectValue};
			details["path"] = markdownPath.string();
			details["reason"] = "markdown report write failed";
			result.error = makeError( error_codes::kReportWriteFailed, "report could not be written", std::move( details ) );
			return result;
		}
	}

	result.jsonPath = jsonPath.string();
	result.markdownPath = markdownPath.string();
	return result;
}

} // namespace sicnu::agentbench
