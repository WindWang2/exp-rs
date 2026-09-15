// tests/test_temporal_agent_tools.cpp — D16 Package H: agent temporal
// tools — schema contract, drought z-score screen, anti-hallucination
// guards. Truth: deterministic synthetic series with hand-placed anomalies.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "agent/spatial_tools/temporal_spatial_tools.h"
#include "agent/tools/temporal_tool.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using sicnu::agent::TemporalSpatialTool;
using sicnu::agent::TemporalTool;

namespace
{

/// Deterministic 5-year monthly NDVI: seasonal cosine around 0.5 with a
/// small deterministic year wiggle; yearOffset adds a flat anomaly.
std::vector<float> fiveYearSeries( int droughtYear = -1, double droughtOffset = 0.0 )
{
    std::vector<float> monthly;
    for ( int y = 2021; y <= 2025; ++y )
        for ( int m = 0; m < 12; ++m )
        {
            double v = 0.5 + 0.1 * std::cos( 2.0 * M_PI * ( m - 6 ) / 12.0 );
            v += 0.02 * ( ( ( y + m ) % 3 ) - 1 ); // -0.02 / 0 / +0.02 wiggle
            if ( y == droughtYear && m >= 3 && m <= 5 )
                v += droughtOffset;
            monthly.push_back( static_cast<float>( v ) );
        }
    return monthly;
}

Json::Value pixelArgs( const char *metric, int year )
{
    Json::Value args;
    args["lon"] = 104.06;
    args["lat"] = 30.67;
    args["metric"] = metric;
    if ( year > 0 )
        args["year"] = year;
    return args;
}

} // namespace

TEST_CASE( "Tool schema registers the three temporal functions", "[d16][agent]" )
{
    const Json::Value schema = TemporalSpatialTool::toolSchema();
    REQUIRE( schema.isObject() );
    REQUIRE( schema["tools"].isArray() );
    REQUIRE( schema["tools"].size() == 3 );

    bool hasPhenology = false;
    bool hasTrend = false;
    bool hasAlert = false;
    for ( const Json::Value &tool : schema["tools"] )
    {
        const std::string name = tool["function"].isNull() ? tool["name"].asString()
                                                           : tool["function"]["name"].asString();
        REQUIRE( tool["parameters"]["required"].isArray() );
        REQUIRE( tool["parameters"]["properties"]["lon"].isObject() );
        REQUIRE( tool["parameters"]["properties"]["lat"].isObject() );
        REQUIRE( tool["parameters"]["properties"]["metric"].isObject() );
        if ( name == "temporal:phenology_query" )
        {
            hasPhenology = true;
            REQUIRE( tool["parameters"]["properties"]["year"].isObject() );
        }
        if ( name == "temporal:trend_inspect" )
            hasTrend = true;
        if ( name == "temporal:anomaly_alert" )
            hasAlert = true;
    }
    REQUIRE( hasPhenology );
    REQUIRE( hasTrend );
    REQUIRE( hasAlert );

    // Catalog wrapper mirrors the same surface.
    REQUIRE( std::string( TemporalTool::name() ) == "temporal" );
    REQUIRE( TemporalTool::schema() == schema );
}

TEST_CASE( "Drought screen fires on three consecutive negative z months",
           "[d16][agent]" )
{
    TemporalSpatialTool::clearSeries();
    TemporalSpatialTool::ingestSeries( "NDVI", 104.06, 30.67, 2021,
                                       fiveYearSeries( 2023, -0.30 ) );

    Json::Value args = pixelArgs( "NDVI", 2023 );
    args["metric"] = "NDVI";
    const Json::Value response = TemporalSpatialTool::executeTool( "temporal:anomaly_alert", args );
    INFO( "response: " << response.toStyledString() );
    REQUIRE( response["status"].asString() == "success" );
    REQUIRE( response["is_drought_alert"].asBool() == true );
    REQUIRE( response["negative_months"].asInt() >= 3 );
    REQUIRE( response["z_by_month"].size() == 12 );
    // Months 3..5 (Mar..May) carry the drought anomaly.
    for ( int m = 3; m <= 5; ++m )
        REQUIRE( response["z_by_month"][m].asDouble() <= -1.5 );

    // A normal year must not raise the alert.
    const Json::Value calm = TemporalSpatialTool::executeTool( "temporal:anomaly_alert",
                                                               pixelArgs( "NDVI", 2024 ) );
    REQUIRE( calm["status"].asString() == "success" );
    REQUIRE( calm["is_drought_alert"].asBool() == false );
}

TEST_CASE( "Phenology query and trend inspection return honest numbers",
           "[d16][agent]" )
{
    TemporalSpatialTool::clearSeries();
    TemporalSpatialTool::ingestSeries( "NDVI", 104.06, 30.67, 2021, fiveYearSeries() );

    // Phenology: the synthetic cosine peaks at month 6 -> mid-July doy ~198.
    const Json::Value phen = TemporalSpatialTool::executeTool( "temporal:phenology_query",
                                                               pixelArgs( "NDVI", 2023 ) );
    INFO( "phen: " << phen.toStyledString() );
    REQUIRE( phen["status"].asString() == "success" );
    REQUIRE( phen["sos"].asDouble() < phen["pos"].asDouble() );
    REQUIRE( phen["pos"].asDouble() < phen["eos"].asDouble() );
    REQUIRE( phen["pos"].asDouble() > 167.0 ); // June..August window
    REQUIRE( phen["pos"].asDouble() < 229.0 );

    // Trend: flat series -> insignificant slope; the tool reports p honestly.
    const Json::Value trend = TemporalSpatialTool::executeTool( "temporal:trend_inspect",
                                                                pixelArgs( "NDVI", 0 ) );
    INFO( "trend: " << trend.toStyledString() );
    REQUIRE( trend["status"].asString() == "success" );
    REQUIRE( trend["significant_005"].asBool() == false );
    REQUIRE( trend["p_value"].asDouble() > 0.05 );

    // Catalog wrapper executes identically.
    const Json::Value viaWrapper = TemporalTool::execute( "temporal:trend_inspect",
                                                          pixelArgs( "NDVI", 0 ) );
    REQUIRE( viaWrapper == trend );
}

TEST_CASE( "Guards reject invalid inputs with structured reasons",
           "[d16][agent]" )
{
    TemporalSpatialTool::clearSeries();
    TemporalSpatialTool::ingestSeries( "NDVI", 104.06, 30.67, 2021, fiveYearSeries() );

    SECTION( "coordinates out of range" )
    {
        Json::Value args = pixelArgs( "NDVI", 2023 );
        args["lon"] = 200.0;
        const Json::Value r = TemporalSpatialTool::executeTool( "temporal:anomaly_alert", args );
        REQUIRE( r["status"].asString() == "rejected" );
        REQUIRE( !r["reason"].asString().empty() );

        Json::Value argsLat = pixelArgs( "NDVI", 2023 );
        argsLat["lat"] = -95.0;
        REQUIRE( TemporalSpatialTool::executeTool( "temporal:anomaly_alert", argsLat )["status"].asString() ==
                 "rejected" );
    }

    SECTION( "unknown tool is rejected, never guessed" )
    {
        const Json::Value r = TemporalSpatialTool::executeTool( "temporal:hallucinate",
                                                                pixelArgs( "NDVI", 2023 ) );
        REQUIRE( r["status"].asString() == "rejected" );
    }

    SECTION( "missing series is rejected" )
    {
        const Json::Value r = TemporalSpatialTool::executeTool( "temporal:phenology_query",
                                                                pixelArgs( "EVI", 2023 ) );
        REQUIRE( r["status"].asString() == "rejected" );
        REQUIRE( r["reason"].asString().find( "no series" ) != std::string::npos );
    }

    SECTION( "year outside the ingested range is rejected" )
    {
        const Json::Value r = TemporalSpatialTool::executeTool( "temporal:anomaly_alert",
                                                                pixelArgs( "NDVI", 2099 ) );
        REQUIRE( r["status"].asString() == "rejected" );
    }

    SECTION( "phenology order violation yields a structured refusal" )
    {
        // Monotonically rising monthly values: no season, extraction invalid,
        // and the tool must refuse rather than fabricate dates.
        std::vector<float> rising;
        for ( int i = 0; i < 24; ++i )
            rising.push_back( 0.1f + 0.01f * i );
        TemporalSpatialTool::ingestSeries( "NDVI", 104.06, 30.67, 2021, rising );
        const Json::Value r = TemporalSpatialTool::executeTool( "temporal:phenology_query",
                                                                pixelArgs( "NDVI", 2021 ) );
        REQUIRE( r["status"].asString() == "rejected" );
        REQUIRE( r["reason"].asString().find( "phenology" ) != std::string::npos );
    }
}

TEST_CASE( "Non-finite months are refused, never averaged into climatology",
           "[d16][agent]" )
{
    TemporalSpatialTool::clearSeries();
    auto series = fiveYearSeries();
    series[3 * 12 + 6] = std::numeric_limits<float>::quiet_NaN(); // NaN in target year
    TemporalSpatialTool::ingestSeries( "NDVI", 104.06, 30.67, 2021, series );

    const Json::Value r = TemporalSpatialTool::executeTool( "temporal:anomaly_alert",
                                                            pixelArgs( "NDVI", 2024 ) );
    REQUIRE( r["status"].asString() == "rejected" );
    REQUIRE( !r["reason"].asString().empty() );

    // A NaN in a CLIMATOLOGY year (not the target) is skipped, not fatal.
    auto series2 = fiveYearSeries();
    series2[0 * 12 + 6] = std::numeric_limits<float>::quiet_NaN(); // 2021 July
    TemporalSpatialTool::clearSeries();
    TemporalSpatialTool::ingestSeries( "NDVI", 104.06, 30.67, 2021, series2 );
    const Json::Value ok = TemporalSpatialTool::executeTool( "temporal:anomaly_alert",
                                                             pixelArgs( "NDVI", 2024 ) );
    REQUIRE( ok["status"].asString() == "success" );
}
