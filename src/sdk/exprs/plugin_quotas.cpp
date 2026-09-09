/***************************************************************************
 * exprs/plugin_quotas.cpp
 ***************************************************************************/
#include "exprs/plugin_quotas.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace exprs {

namespace {

long long envLongLong( const char *name, long long fallback )
{
    const char *value = std::getenv( name );
    if ( !value || !*value )
        return fallback;
    char *end = nullptr;
    const long long parsed = std::strtoll( value, &end, 10 );
    return ( end && *end == '\0' ) ? parsed : fallback;
}

} // namespace

PluginQuota PluginQuota::fromEnvironment()
{
    PluginQuota quota;
    quota.maxRequestConcurrency = static_cast<int>(
        envLongLong( "SICNU_PLUGIN_QUOTA_CONCURRENCY", quota.maxRequestConcurrency ) );
    quota.requestDeadlineMs = static_cast<int>(
        envLongLong( "SICNU_PLUGIN_QUOTA_DEADLINE_MS", quota.requestDeadlineMs ) );
    quota.maxResponseBytes = static_cast<long>(
        envLongLong( "SICNU_PLUGIN_QUOTA_RESPONSE_BYTES", quota.maxResponseBytes ) );
    quota.workerMemoryBytes =
        envLongLong( "SICNU_PLUGIN_QUOTA_MEMORY_BYTES", quota.workerMemoryBytes );
    quota.workerCpuRatePercent = static_cast<int>(
        envLongLong( "SICNU_PLUGIN_QUOTA_CPU_PERCENT", quota.workerCpuRatePercent ) );
    quota.maxChildProcesses = static_cast<int>(
        envLongLong( "SICNU_PLUGIN_QUOTA_CHILD_PROCESSES", quota.maxChildProcesses ) );
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
    json["workerMemoryBytes"] = static_cast<Json::Int64>( workerMemoryBytes );
    json["workerCpuRatePercent"] = workerCpuRatePercent;
    json["maxChildProcesses"] = maxChildProcesses;
    if ( !gpuHint.empty() )
        json["gpuHint"] = gpuHint;
    return json;
}

} // namespace exprs
