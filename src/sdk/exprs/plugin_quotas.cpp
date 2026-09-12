/***************************************************************************
 * exprs/plugin_quotas.cpp
 ***************************************************************************/
#include "exprs/plugin_quotas.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace exprs {

namespace {

/// Parses an env override LOUDLY: an unset variable keeps the fallback
/// silently, but a set-but-non-numeric or out-of-range value keeps the
/// fallback AND reports on stderr (there is no diagnostics channel before a
/// runtime exists). A negative quota would silently DISABLE enforcement
/// (workerMemoryBytes <= 0 turns off the job-object/RLIMIT bound), so these
/// ceilings must fail safe, never fail open. Ranges mirror parseManifest.
long long envLongLongInRange( const char *name, long long fallback, long long low,
                              long long high )
{
    const char *raw = std::getenv( name );
    if ( !raw || !*raw )
        return fallback;
    char *end = nullptr;
    const long long parsed = std::strtoll( raw, &end, 10 );
    if ( !end || *end != '\0' )
    {
        std::fprintf( stderr, "plugin-quotas: %s='%s' is not a number; default %lld kept\n",
                      name, raw, fallback );
        return fallback;
    }
    if ( parsed < low || parsed > high )
    {
        std::fprintf( stderr,
                      "plugin-quotas: %s=%lld outside [%lld, %lld]; default %lld kept\n", name,
                      parsed, low, high, fallback );
        return fallback;
    }
    return parsed;
}

/// Clamps an env-provided byte bound into [1024, 1 GiB]: beyond the clamp a
/// truncated 32-bit long (MSVC) could become 0 = "no bound" (fail-open).
/// Non-positive or non-numeric values are refused loudly (default kept);
/// positive values keep the clamp semantics.
long long clampedBytes( const char *name, long long fallback )
{
    const long long value = envLongLongInRange( name, fallback, 1,
                                                1024LL * 1024LL * 1024LL * 64LL );
    return std::min( std::max( value, 1024LL ), 1024LL * 1024LL * 1024LL );
}

} // namespace

PluginQuota PluginQuota::fromEnvironment()
{
    PluginQuota quota;
    quota.maxRequestConcurrency = static_cast<int>( envLongLongInRange(
        "SICNU_PLUGIN_QUOTA_CONCURRENCY", quota.maxRequestConcurrency, 1, 64 ) );
    quota.requestDeadlineMs = static_cast<int>( envLongLongInRange(
        "SICNU_PLUGIN_QUOTA_DEADLINE_MS", quota.requestDeadlineMs, 1000,
        24LL * 60 * 60 * 1000 ) );
    quota.maxResponseBytes =
        static_cast<long>( clampedBytes( "SICNU_PLUGIN_QUOTA_RESPONSE_BYTES",
                                         quota.maxResponseBytes ) );
    quota.maxRequestBytes = static_cast<long>(
        clampedBytes( "SICNU_PLUGIN_QUOTA_REQUEST_BYTES", quota.maxRequestBytes ) );
    quota.workerMemoryBytes =
        envLongLongInRange( "SICNU_PLUGIN_QUOTA_MEMORY_BYTES", quota.workerMemoryBytes, 0,
                            1LL << 62 );
    quota.workerCpuRatePercent = static_cast<int>( envLongLongInRange(
        "SICNU_PLUGIN_QUOTA_CPU_PERCENT", quota.workerCpuRatePercent, 0, 100 ) );
    quota.maxChildProcesses = static_cast<int>( envLongLongInRange(
        "SICNU_PLUGIN_QUOTA_CHILD_PROCESSES", quota.maxChildProcesses, 0, 256 ) );
    if ( const char *gpu = std::getenv( "SICNU_PLUGIN_QUOTA_GPU" ) )
        quota.gpuHint = gpu;
    return quota;
}

void PluginQuota::parseManifest( const Json::Value &quotas, std::vector<std::string> &warnings )
{
    if ( !quotas.isObject() )
    {
        if ( !quotas.isNull() )
            warnings.push_back( "manifest quotas is not an object; ignored" );
        return;
    }
    auto readInt = [&]( const char *key, int &target, int low, int high ) {
        const Json::Value &value = quotas[ key ];
        if ( value.isNull() )
            return;
        if ( value.isInt() && value.asInt() >= low && value.asInt() <= high )
            target = value.asInt();
        else
            warnings.push_back( std::string( "quotas." ) + key + " out of range; host ceiling kept" );
    };
    auto readLong = [&]( const char *key, long long &target, long long low, long long high ) {
        const Json::Value &value = quotas[ key ];
        if ( value.isNull() )
            return;
        if ( ( value.isInt64() || value.isInt() ) && value.asInt64() >= low
             && value.asInt64() <= high )
            target = value.asInt64();
        else
            warnings.push_back( std::string( "quotas." ) + key + " out of range; host ceiling kept" );
    };

    readInt( "maxRequestConcurrency", maxRequestConcurrency, 1, 64 );
    readInt( "requestDeadlineMs", requestDeadlineMs, 1000, 24 * 60 * 60 * 1000 );
    {
        long long responseBytes = static_cast<long long>( maxResponseBytes );
        readLong( "maxResponseBytes", responseBytes, 1024, 1024LL * 1024LL * 1024LL );
        maxResponseBytes = static_cast<long>( responseBytes );
    }
    {
        long long requestBytes = static_cast<long long>( maxRequestBytes );
        readLong( "maxRequestBytes", requestBytes, 1024, 1024LL * 1024LL * 1024LL );
        maxRequestBytes = static_cast<long>( requestBytes );
    }
    readLong( "workerMemoryBytes", workerMemoryBytes, 0, 1LL << 62 );
    readInt( "workerCpuRatePercent", workerCpuRatePercent, 0, 100 );
    readInt( "maxChildProcesses", maxChildProcesses, 0, 256 );

    const Json::Value &gpu = quotas[ "gpuHint" ];
    if ( gpu.isString() )
        gpuHint = gpu.asString();
    else if ( !gpu.isNull() )
        warnings.push_back( "quotas.gpuHint must be a string; ignored" );
}

void PluginQuota::clampTo( const PluginQuota &ceilings )
{
    maxRequestConcurrency = std::min( maxRequestConcurrency, ceilings.maxRequestConcurrency );
    requestDeadlineMs = std::min( requestDeadlineMs, ceilings.requestDeadlineMs );
    maxResponseBytes = std::min( maxResponseBytes, ceilings.maxResponseBytes );
    maxRequestBytes = std::min( maxRequestBytes, ceilings.maxRequestBytes );
    if ( workerMemoryBytes > 0 && ceilings.workerMemoryBytes > 0 )
        workerMemoryBytes = std::min( workerMemoryBytes, ceilings.workerMemoryBytes );
    else if ( ceilings.workerMemoryBytes > 0 )
        workerMemoryBytes = ceilings.workerMemoryBytes;
    if ( workerCpuRatePercent > 0 )
        workerCpuRatePercent = ceilings.workerCpuRatePercent > 0
                                   ? std::min( workerCpuRatePercent, ceilings.workerCpuRatePercent )
                                   : workerCpuRatePercent;
    maxChildProcesses = std::min( maxChildProcesses, ceilings.maxChildProcesses );
    if ( gpuHint.empty() )
        gpuHint = ceilings.gpuHint;
}

Json::Value PluginQuota::toJson() const
{
    Json::Value json( Json::objectValue );
    json["maxRequestConcurrency"] = maxRequestConcurrency;
    json["requestDeadlineMs"] = requestDeadlineMs;
    json["maxResponseBytes"] = static_cast<Json::Int64>( maxResponseBytes );
    json["maxRequestBytes"] = static_cast<Json::Int64>( maxRequestBytes );
    json["workerMemoryBytes"] = static_cast<Json::Int64>( workerMemoryBytes );
    json["workerCpuRatePercent"] = workerCpuRatePercent;
    json["maxChildProcesses"] = maxChildProcesses;
    if ( !gpuHint.empty() )
        json["gpuHint"] = gpuHint;
    return json;
}

} // namespace exprs
