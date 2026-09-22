/***************************************************************************
  agent/spatial_tools/temporal_spatial_tools.cpp
  Temporal Phenology Timeline Studio (D16) — agent temporal tool kernels.
  ---------------------------
  See temporal_spatial_tools.h for the seam contract (ADR 0161).
 ***************************************************************************/

#include "agent/spatial_tools/temporal_spatial_tools.h"

#include "processing/algorithms/phenology_metrics.h"
#include "processing/algorithms/trend_analysis.h"

#include <cmath>
#include <map>

namespace sicnu::agent
{

namespace
{

constexpr double kQuantum = 0.25;

int quantize( double coordinate )
{
    return static_cast<int>( std::floor( coordinate / kQuantum ) );
}

Json::Value rejected( const std::string &reason )
{
    Json::Value out;
    out["status"] = "rejected";
    out["reason"] = reason;
    return out;
}

bool inRange( const Json::Value &arguments, double lonMin, double lonMax, double latMin,
              double latMax )
{
    return arguments["lon"].isNumeric() && arguments["lat"].isNumeric() &&
           arguments["lon"].asDouble() >= lonMin && arguments["lon"].asDouble() <= lonMax &&
           arguments["lat"].asDouble() >= latMin && arguments["lat"].asDouble() <= latMax;
}

double monthMidDoy( int month0Based )
{
    // Mid-month day-of-year on a 30.4-day month grid (month 0 -> 15.5).
    return 15.5 + 30.4 * month0Based;
}

} // namespace

void TemporalSpatialTool::ingestSeries( const std::string &metric, double lon, double lat,
                                        int startYear, const std::vector<float> &monthlyValues )
{
    SeriesKey key{ metric, quantize( lon ), quantize( lat ) };
    SeriesRecord record;
    record.startYear = startYear;
    record.monthly = monthlyValues;
    catalog()[key] = std::move( record );
}

void TemporalSpatialTool::clearSeries()
{
    catalog().clear();
}

TemporalSpatialTool::SeriesRecord *TemporalSpatialTool::findSeries( const std::string &metric,
                                                                    double lon, double lat )
{
    auto found = catalog().find( SeriesKey{ metric, quantize( lon ), quantize( lat ) } );
    return found == catalog().end() ? nullptr : &found->second;
}

std::map<TemporalSpatialTool::SeriesKey, TemporalSpatialTool::SeriesRecord>
    &TemporalSpatialTool::catalog()
{
    static std::map<SeriesKey, SeriesRecord> store;
    return store;
}

Json::Value TemporalSpatialTool::toolSchema()
{
    Json::Value schema;
    schema["type"] = "function_toolset";
    Json::Value tools( Json::arrayValue );

    {
        Json::Value fn;
        fn["name"] = "temporal:phenology_query";
        fn["description"] =
            "Start/peak/end of season for one pixel and year from the ingested monthly series.";
        Json::Value params;
        params["type"] = "object";
        Json::Value props;
        props["lon"]["type"] = "number";
        props["lon"]["description"] = "longitude in [-180, 180]";
        props["lat"]["type"] = "number";
        props["lat"]["description"] = "latitude in [-90, 90]";
        props["metric"]["type"] = "string";
        props["metric"]["description"] = "index name, e.g. NDVI";
        props["year"]["type"] = "integer";
        params["properties"] = props;
        params["required"] = Json::Value( Json::arrayValue );
        params["required"].append( "lon" );
        params["required"].append( "lat" );
        params["required"].append( "metric" );
        params["required"].append( "year" );
        fn["parameters"] = params;
        tools.append( fn );
    }
    {
        Json::Value fn;
        fn["name"] = "temporal:trend_inspect";
        fn["description"] = "Theil-Sen slope and Mann-Kendall significance over the full series.";
        Json::Value params;
        params["type"] = "object";
        Json::Value props;
        props["lon"]["type"] = "number";
        props["lat"]["type"] = "number";
        props["metric"]["type"] = "string";
        params["properties"] = props;
        params["required"] = Json::Value( Json::arrayValue );
        params["required"].append( "lon" );
        params["required"].append( "lat" );
        params["required"].append( "metric" );
        fn["parameters"] = params;
        tools.append( fn );
    }
    {
        Json::Value fn;
        fn["name"] = "temporal:anomaly_alert";
        fn["description"] =
            "Standardized monthly anomaly screen; raises a drought alert at >= 3 consecutive "
            "months with z <= -1.5 in the target year.";
        Json::Value params;
        params["type"] = "object";
        Json::Value props;
        props["lon"]["type"] = "number";
        props["lat"]["type"] = "number";
        props["metric"]["type"] = "string";
        props["year"]["type"] = "integer";
        params["properties"] = props;
        params["required"] = Json::Value( Json::arrayValue );
        params["required"].append( "lon" );
        params["required"].append( "lat" );
        params["required"].append( "metric" );
        params["required"].append( "year" );
        fn["parameters"] = params;
        tools.append( fn );
    }
    schema["tools"] = tools;
    return schema;
}

Json::Value TemporalSpatialTool::executeTool( const std::string &toolName,
                                              const Json::Value &arguments )
{
    if ( !arguments.isObject() )
        return rejected( "arguments must be a JSON object" );

    const bool coordinatesValid = inRange( arguments, -180.0, 180.0, -90.0, 90.0 );
    if ( !coordinatesValid )
        return rejected( "coordinates out of range" );
    if ( !arguments["metric"].isString() || arguments["metric"].asString().empty() )
        return rejected( "metric is required" );
    const std::string metric = arguments["metric"].asString();

    SeriesRecord *record = findSeries( metric, arguments["lon"].asDouble(),
                                       arguments["lat"].asDouble() );
    if ( !record || record->monthly.empty() )
        return rejected( "no series ingested for this pixel and metric" );

    const int years = static_cast<int>( record->monthly.size() / 12 );
    if ( years < 1 )
        return rejected( "series shorter than one year" );

    if ( toolName == "temporal:phenology_query" || toolName == "temporal:anomaly_alert" )
    {
        if ( !arguments["year"].isIntegral() )
            return rejected( "year is required as an integer" );
    }

    if ( toolName == "temporal:phenology_query" )
    {
        const int year = arguments["year"].asInt();
        const int offset = year - record->startYear;
        if ( offset < 0 || offset >= years )
            return rejected( "year outside the ingested range" );

        std::vector<float> values( 12 );
        std::vector<double> t( 12 );
        for ( int m = 0; m < 12; ++m )
        {
            values[static_cast<std::size_t>( m )] =
                record->monthly[static_cast<std::size_t>( offset * 12 + m )];
            t[static_cast<std::size_t>( m )] = monthMidDoy( m );
        }
        const sicnu::temporal::PhenologyMetrics metrics =
            // Widest doy window (1..366): doyOf can emit the day-366 leap bucket.
            sicnu::temporal::PhenologyExtractor::extractDynamicThreshold( values, t, 0.2, 1, 366 );
        if ( !metrics.valid )
            return rejected( "phenology order violated or season undefined" );

        Json::Value out;
        out["status"] = "success";
        out["sos"] = metrics.sos;
        out["pos"] = metrics.pos;
        out["eos"] = metrics.eos;
        out["los"] = metrics.los;
        out["base"] = metrics.baseVal;
        out["peak"] = metrics.peakVal;
        return out;
    }

    if ( toolName == "temporal:trend_inspect" )
    {
        // Caveat (reported in the response): the plain Mann-Kendall test on
        // a strongly seasonal series reads seasonality as significance —
        // callers should deseasonalize (or request seasonal MK) for trend
        // claims on such series.
        const std::size_t count = record->monthly.size();
        std::vector<float> values( count );
        std::vector<double> t( count );
        for ( int y = 0; y < years; ++y )
            for ( int m = 0; m < 12; ++m )
            {
                const std::size_t i = static_cast<std::size_t>( y * 12 + m );
                values[i] = record->monthly[i];
                t[i] = 365.25 * y + monthMidDoy( m );
            }
        const sicnu::temporal::MannKendallResult trend =
            sicnu::temporal::TrendAnalyzer::computeMannKendall( values, t );
        if ( !trend.valid )
            return rejected( "series too short for a trend test" );

        Json::Value out;
        out["status"] = "success";
        out["caveat"] = "plain Mann-Kendall on a seasonal series overstates significance";
        out["sen_slope_per_year"] = trend.senSlope * 365.25;
        out["z"] = trend.zScore;
        out["p_value"] = trend.pValue;
        out["significant_005"] = trend.isSignificant( 0.05 );
        out["samples"] = trend.sampleCount;
        return out;
    }

    if ( toolName == "temporal:anomaly_alert" )
    {
        const int year = arguments["year"].asInt();
        const int offset = year - record->startYear;
        if ( offset < 0 || offset >= years )
            return rejected( "year outside the ingested range" );
        if ( years < 2 )
            return rejected( "climatology needs at least two years" );

        // Per-calendar-month climatology across the ingested years. NaN
        // months are skipped (never averaged in); a non-finite target month
        // is a structured refusal, not a fabricated z.
        double mean[12] = { 0 };
        double m2[12] = { 0 };
        int counted[12] = { 0 };
        for ( int y = 0; y < years; ++y )
            for ( int m = 0; m < 12; ++m )
            {
                const double v = record->monthly[static_cast<std::size_t>( y * 12 + m )];
                if ( !std::isfinite( v ) )
                    continue;
                mean[m] += v;
                m2[m] += v * v;
                ++counted[m];
            }
        double sigma[12] = { 0 };
        for ( int m = 0; m < 12; ++m )
        {
            if ( counted[m] < 2 )
                return rejected( "climatology needs at least two finite years for every month" );
            mean[m] /= counted[m];
            sigma[m] = std::sqrt( std::max( m2[m] / counted[m] - mean[m] * mean[m], 0.0 ) );
        }

        Json::Value zMonths( Json::arrayValue );
        int longestRun = 0;
        int run = 0;
        for ( int m = 0; m < 12; ++m )
        {
            const double v = record->monthly[static_cast<std::size_t>( offset * 12 + m )];
            if ( !std::isfinite( v ) )
                return rejected( "target year contains non-finite months" );
            const double z = sigma[m] > 1e-9 ? ( v - mean[m] ) / sigma[m] : 0.0;
            zMonths.append( z );
            if ( z <= -1.5 )
            {
                ++run;
                longestRun = std::max( longestRun, run );
            }
            else
            {
                run = 0;
            }
        }

        Json::Value out;
        out["status"] = "success";
        out["is_drought_alert"] = longestRun >= 3;
        out["negative_months"] = longestRun;
        out["year"] = year;
        out["z_by_month"] = zMonths;
        return out;
    }

    return rejected( "unknown tool: " + toolName );
}

} // namespace sicnu::agent
