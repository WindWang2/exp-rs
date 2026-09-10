// test_diagnostic_report.cpp — machine-readable diagnostic envelope contract
// (task G, Verification 7.0). Qt-free; links sicnu_runtime.
#include <catch2/catch_test_macros.hpp>

#include "runtime/observability/diagnostic_report.h"

using sicnu::runtime::observability::diagnostics::DiagnosticReport;
using sicnu::runtime::observability::diagnostics::Recoverability;

TEST_CASE( "diagnostic report: full envelope encodes every field", "[diag]" )
{
    DiagnosticReport report;
    report.code = "output.publish_failed";
    report.component = "processing.output_committer";
    report.run = "run-01";
    report.task = "42";
    report.job = "job-9";
    report.recoverability = Recoverability::Manual;
    report.suggestedAction = "check stable path permissions and free disk space";
    report.causeChain = { "workflow:ndvi-run", "task:42", "operator:rs:ndvi",
                          "rename failed: target locked" };
    report.artifacts = { "checkpoint_run-01.json", "scratch.tif.new" };

    const std::string json = report.toJson();
    REQUIRE( json.find( "\"schema\":\"exp.diag.v1\"" ) != std::string::npos );
    REQUIRE( json.find( "\"code\":\"output.publish_failed\"" ) != std::string::npos );
    REQUIRE( json.find( "\"component\":\"processing.output_committer\"" ) != std::string::npos );
    REQUIRE( json.find( "\"run\":\"run-01\"" ) != std::string::npos );
    REQUIRE( json.find( "\"task\":\"42\"" ) != std::string::npos );
    REQUIRE( json.find( "\"job\":\"job-9\"" ) != std::string::npos );
    REQUIRE( json.find( "\"recoverability\":\"manual\"" ) != std::string::npos );
    REQUIRE( json.find( "\"suggested_action\":\"check stable path" ) != std::string::npos );
    REQUIRE( json.find( "\"cause_chain\":[\"workflow:ndvi-run\"" ) != std::string::npos );
    REQUIRE( json.find( "\"artifacts\":" ) != std::string::npos );
    // One line, no raw newlines even with multi-entry chains.
    REQUIRE( json.find( '\n' ) == std::string::npos );
}

TEST_CASE( "diagnostic report: unknown codes survive verbatim and honestly",
           "[diag]" )
{
    // Catalog rule: never rename, never swallow. An unknown code carries
    // recoverability unknown — never a fabricated retry promise.
    DiagnosticReport report;
    report.code = "TOTALLY_UNKNOWN_CODE";
    report.component = "agent.harness";
    report.recoverability = Recoverability::Unknown;
    const std::string json = report.toJson();
    REQUIRE( json.find( "\"code\":\"TOTALLY_UNKNOWN_CODE\"" ) != std::string::npos );
    REQUIRE( json.find( "\"recoverability\":\"unknown\"" ) != std::string::npos );
    REQUIRE( json.find( "suggested_action" ) == std::string::npos ); // honest: no advice invented
    REQUIRE( json.find( "cause_chain" ) == std::string::npos );
}

TEST_CASE( "diagnostic report: escaping and one-line guarantee", "[diag]" )
{
    DiagnosticReport report;
    report.code = "weird\"code\\with\nnewline";
    report.suggestedAction = "tab\there";
    const std::string json = report.toJson();
    REQUIRE( json.find( '\n' ) == std::string::npos );
    REQUIRE( json.find( "\\n" ) != std::string::npos );
    REQUIRE( json.find( "\\\"" ) != std::string::npos );
    REQUIRE( json.find( "\\\\" ) != std::string::npos );
    REQUIRE( json.find( "\\t" ) != std::string::npos );
}
