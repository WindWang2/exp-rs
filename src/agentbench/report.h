// src/agentbench/report.h
#pragma once

//
// Benchmark report: machine-readable JSON + human-readable Markdown.
//
// Rendering is a pure function of the evaluation — identical evaluations
// render to identical bytes (reports never embed timestamps, host names, or
// paths). Writing is the ONLY file-touching step in agentbench: it accepts a
// caller-supplied output directory, writes <base>.json and <base>.md, and
// fails with a typed error instead of leaving a partial silent file.
//

#include "evaluator.h"

#include <string>

namespace sicnu::agentbench
{

/// Human-readable Markdown report (deterministic bytes).
std::string renderReportMarkdown( const CaseEvaluation &evaluation );

/// Pretty-printed JSON report: the versioned evaluation document
/// (sicnu.agentbench.evaluation/v1) including its digest.
std::string renderReportJson( const CaseEvaluation &evaluation );

struct ReportResult
{
	BenchError error;      ///< agentbench.report_write_failed on failure
	std::string jsonPath;  ///< written when error is empty
	std::string markdownPath;
};

/// Writes <outputDir>/<baseName>.json and .md. Creates nothing outside
/// `outputDir`; refuses to write when the directory is not a directory.
ReportResult writeReport( const CaseEvaluation &evaluation, const std::string &outputDir, const std::string &baseName );

} // namespace sicnu::agentbench
