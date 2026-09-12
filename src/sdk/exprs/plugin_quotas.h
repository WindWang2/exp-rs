/***************************************************************************
 * exprs/plugin_quotas.h — per-plugin resource quotas
 *
 * Quotas bound what one plugin (and its host-process worker) may consume;
 * exceeding one is a typed refusal (E6007), never host degradation. A
 * manifest may declare a "quotas" object; the host CLAMPS every declared
 * value to its own ceilings (environment overrides) — a manifest can lower
 * its own limits but never raise them beyond what policy allows.
 *
 * Enforcement matrix (documented honestly, docs/plugins/capabilities.md):
 *   maxRequestConcurrency  host-side session gate (exact, all platforms)
 *   requestDeadlineMs      host-side per-request ceiling (exact)
 *   maxResponseBytes       frame cap on every worker->host frame (exact,
 *                          protocol 1.2 per-direction: no longer side-caps
 *                          host->worker request frames)
 *   maxRequestBytes        frame cap on every host->worker request frame
 *                          (exact; protocol 1.2; shared fallback for 1.1
 *                          peers is min(maxRequestBytes, maxResponseBytes))
 *   workerMemoryBytes      Windows: job object limit (exact); POSIX:
 *                          RLIMIT_AS best effort (coarse, documented)
 *   workerCpuRatePercent   Windows: job object CPU rate control; POSIX: not
 *                          enforced (advisory only)
 *   maxChildProcesses      Windows: job object active-process limit (exact);
 *                          POSIX: RLIMIT_NPROC is per-user, so advisory
 *                          unless the platform provides job control
 *   gpuHint                advisory always (documented)
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <string>

namespace exprs {

struct PluginQuota
{
    int maxRequestConcurrency = 4;
    int requestDeadlineMs = 120000;         ///< ceiling for per-request deadlines
    long maxResponseBytes = 32L * 1024L * 1024L;
    long maxRequestBytes = 32L * 1024L * 1024L;  ///< host->worker request frames (1.2)
    long long workerMemoryBytes = 0;        ///< 0 = platform default
    int workerCpuRatePercent = 0;           ///< 1..100, 0 = uncapped
    int maxChildProcesses = 8;              ///< worker + children cap (Job)
    std::string gpuHint;                    ///< advisory passthrough

    /// Environment defaults (SICNU_PLUGIN_QUOTA_*), applied on top of the
    /// struct defaults before manifest clamping.
    static PluginQuota fromEnvironment();

    /// Parses a manifest "quotas" object. Invalid values produce a warning
    /// string and keep the current setting; clamping to @p ceilings happens
    /// in clampTo.
    void parseManifest( const Json::Value &quotas, std::vector<std::string> &warnings );

    /// Clamps every field to @p ceilings (host authority).
    void clampTo( const PluginQuota &ceilings );

    Json::Value toJson() const;
};

} // namespace exprs
