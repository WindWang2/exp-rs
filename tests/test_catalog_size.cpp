// tests/test_catalog_size.cpp
//
// Tool catalog token-efficiency regression (goal: LLM context must not grow
// without bound as atomic operators are added). Bounds the full OpenAI tool
// definition export and demonstrates that a compact discovery layer is
// substantially smaller than the full schema injection.
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/atomic_algorithm_registry.h"

#include <QString>

using namespace sicnu::processing;

TEST_CASE( "Tool catalog export stays within the context budget", "[agent][catalog][size]" )
{
    auto &registry = AtomicAlgorithmRegistry::instance();
    registry.initialize();

    const Json::Value tools = registry.exportOpenAiToolDefinitions();
    REQUIRE( tools.isArray() );
    REQUIRE( tools.size() >= 40 );

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    const std::string full = Json::writeString( builder, tools );

    // ~4 chars per token; budget 100 KiB ≈ 25k tokens — a hard regression
    // ceiling so adding atomic operators cannot silently blow the LLM context.
    const size_t budgetBytes = 100 * 1024;
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
