// tests/test_catalog_size.cpp
//
// Tool catalog token-efficiency regression (goal: LLM context must not grow
// without bound as atomic operators are added). Two layers:
//
//   1. Per-operator schema ceiling (#1361): a single tool definition must not
//      exceed its own budget, so growth is attributed to NEW OPERATORS, not
//      to silent per-entry bloat. Failures name the offending operators.
//   2. Whole-catalog envelope (secondary guard): bounds the full OpenAI tool
//      definition export and demonstrates that a compact discovery layer is
//      substantially smaller than the full schema injection.
//
// Budget revision process (TEST_INFRA.md "Budget governance"): a raise of
// either budget must attach the top-offenders report printed by this test
// (SICNU_CATALOG_SIZE_REPORT=1 or the Catch2 failure output) as attribution
// evidence — proving the growth came from added operators, not bloat.
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/atomic_algorithm_registry.h"
#include "operators/framework/rs_operator_registry.h"

#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

using namespace sicnu::processing;

namespace
{

// #1361: the envelope was ratcheted 100 -> 160 -> 176 -> 288 KiB because
// nothing bounded a single operator's schema. This ceiling is the actual
// guard: one operator definition (function name/description/parameters
// JSON-Schema, compact-serialized) must stay under its own budget, so a
// catalog that adds operators can still grow the envelope, but an operator
// whose metadata inflates is named here instead of re-raising the
// whole-catalog budget.
//
// Derivation (r5 measured, 189 operators): largest single definition is
// rs_temporal_phenology at 4026 bytes; the next tier sits at 2.7-4.0 KiB.
// 6 KiB ≈ +53% over today's worst operator — loose enough that schema
// evolution noise passes, tight enough that a doubled schema is named and
// must be attributed. Raising it requires the same evidence as the envelope
// (a named operator genuinely needing a larger contract).
constexpr size_t kPerOperatorBudgetBytes = 6 * 1024;

struct ToolSize
{
    std::string id;
    size_t bytes = 0;
};

std::string writeCompact( const Json::Value &value )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString( builder, value );
}

std::vector<ToolSize> rankedToolSizes( const Json::Value &tools )
{
    std::vector<ToolSize> sizes;
    sizes.reserve( tools.size() );
    for ( const auto &tool : tools )
    {
        ToolSize entry;
        if ( tool.isMember( "function" ) && tool["function"].isMember( "name" ) )
            entry.id = tool["function"]["name"].asString();
        else
            entry.id = "<unnamed>";
        entry.bytes = writeCompact( tool ).size();
        sizes.push_back( entry );
    }
    std::sort( sizes.begin(), sizes.end(),
               []( const ToolSize &a, const ToolSize &b ) { return a.bytes > b.bytes; } );
    return sizes;
}

/// One-line-per-operator attribution report (largest first), the evidence
/// a budget-revision PR must attach (#1361 acceptance). totalBudget == 0
/// omits the envelope line (per-operator test).
std::string topOffendersReport( const std::vector<ToolSize> &sizes, size_t topN,
                                size_t perOperatorBudget, size_t totalBudget )
{
    std::string report = "catalog size attribution (bytes, largest first):\n";
    size_t sumBytes = 0;
    for ( const auto &entry : sizes )
        sumBytes += entry.bytes;
    if ( totalBudget )
        report += "  total_budget=" + std::to_string( totalBudget );
    else
        report += "  total_budget=(per-operator test)";
    report += " per_operator_budget=" + std::to_string( perOperatorBudget );
    report += " operators=" + std::to_string( sizes.size() );
    report += " per_tool_sum_bytes=" + std::to_string( sumBytes ) + "\n";
    const size_t count = std::min( topN, sizes.size() );
    for ( size_t i = 0; i < count; ++i )
    {
        report += "  " + std::to_string( i + 1 ) + ". " + sizes[i].id + " = "
                  + std::to_string( sizes[i].bytes ) + "\n";
    }
    return report;
}

} // namespace

TEST_CASE( "Per-operator tool schema stays within its own budget", "[agent][catalog][size]" )
{
    (void)sicnu::operators::RSOperatorRegistry::instance();
    auto &registry = AtomicAlgorithmRegistry::instance();
    registry.initialize();

    const Json::Value tools = registry.exportOpenAiToolDefinitions();
    REQUIRE( tools.isArray() );
    REQUIRE( tools.size() >= 40 );

    const std::vector<ToolSize> sizes = rankedToolSizes( tools );

    // #1361: per-operator guard — see kPerOperatorBudgetBytes above.
    std::vector<ToolSize> offenders;
    for ( const auto &entry : sizes )
    {
        if ( entry.bytes > kPerOperatorBudgetBytes )
            offenders.push_back( entry );
    }

    // Always print the top offenders into the Catch2 log (visible with
    // --success false on failure; the report env var dumps the full ranking
    // for budget reviews).
    const std::string report = topOffendersReport( sizes, 15, kPerOperatorBudgetBytes, 0 );
    INFO( report );
    if ( qEnvironmentVariableIsEmpty( "SICNU_CATALOG_SIZE_REPORT" ) )
    {
        // default: quiet on success
    }
    else
    {
        std::fputs( report.c_str(), stdout );
        std::fflush( stdout );
    }

    if ( !offenders.empty() )
    {
        std::string msg = "per-operator schema budget exceeded by "
                          + std::to_string( offenders.size() ) + " operator(s); "
                          + "shrink the metadata or raise the ceiling WITH attribution:\n";
        for ( const auto &entry : offenders )
        {
            msg += "  " + entry.id + " = " + std::to_string( entry.bytes ) + " bytes"
                   + " (budget " + std::to_string( kPerOperatorBudgetBytes ) + ")\n";
        }
        FAIL( msg );
    }
}

TEST_CASE( "Tool catalog export stays within the context budget", "[agent][catalog][size]" )
{
    (void)sicnu::operators::RSOperatorRegistry::instance();
    auto &registry = AtomicAlgorithmRegistry::instance();
    registry.initialize();

    const Json::Value tools = registry.exportOpenAiToolDefinitions();
    REQUIRE( tools.isArray() );
    REQUIRE( tools.size() >= 40 );

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    const std::string full = Json::writeString( builder, tools );

    // ~4 chars per token; budget ≈ tokens — a hard regression ceiling so
    // adding atomic operators cannot silently blow the LLM context. This is
    // the SECONDARY guard (#1361): growth of the envelope must be attributable
    // to newly added operators (bounded per-operator by the sibling case),
    // not to per-entry bloat. The failure INFO below prints the top offenders
    // so any raise/decrease of this budget carries attribution data.
    // Platform 3.0 (2026-09): the catalog grew from ~65 to ~82 algorithms with
    // the SAR / temporal-2.0 / feature families (17 new rs: operators, each
    // with full JSON-Schema contracts). The ceiling was raised 100 → 160 KiB
    // to fit the platform scope; discovery stays progressive — agents search
    // and shortlist (compact layer below) before injecting any full schema,
    // so the whole-catalog export is a listing envelope, not the common path.
    // Scientific Processing 8.0: four more operators (rs:sar_geocode,
    // rs:sar_temporal_stats, rs:rasterize, rs:zonal_stats) pushed the full
    // envelope past 160 KiB (measured 167 KiB) — raised to 176 KiB.
    // ci-redzone-r4 re-measure: the operator waves since the 8.0 raise
    // (review PR #1200 fix batch and the agent-ops/io/processing hardening
    // waves) grew the full envelope to a measured 268,388 bytes (~262 KiB)
    // on this exact assertion — raised to 288 KiB (~+10% headroom), same
    // convention as the 160→176 raise. The lower bound keeps the budget
    // honest in both directions: a silent catalog shrink must be noticed
    // as loudly as a growth.
    const size_t budgetBytes = 288 * 1024;
    const std::vector<ToolSize> sizes = rankedToolSizes( tools );
    INFO( topOffendersReport( sizes, 15, kPerOperatorBudgetBytes, budgetBytes ) );
    REQUIRE( full.size() > 250 * 1024 );
    REQUIRE( full.size() < budgetBytes );

    // Compact discovery layer (id/name/group/purpose only) must be meaningfully
    // smaller than the full schema injection — the progressive-disclosure
    // premise (compact < 50% of full).
    Json::Value compact( Json::arrayValue );
    for ( const auto &desc : registry.listDescriptors() )
    {
        Json::Value entry( Json::objectValue );
        entry["id"] = desc.id;
        entry["name"] = desc.displayName;
        entry["group"] = desc.group;
        entry["purpose"] = desc.agentMetadata.purpose;
        entry["largeRasterSafe"] = desc.agentMetadata.largeRasterSafe;
        compact.append( entry );
    }
    const std::string compactStr = Json::writeString( builder, compact );
    REQUIRE( compactStr.size() * 2 < full.size() );
}
