// sar_unwrap_provider.h — external unwrap provider adapter (Advanced InSAR
// 11.0, package D; DECISIONS D-004).
//
// The BUILT-IN reference unwrapper (sar_insar.h qualityGuidedUnwrap) stays
// the default and is untouched. This module is the seam behind the
// rs:sar_unwrap `provider` parameter: a GENERIC external-executable
// contract with NO third-party code or dependency — SNAPHU or any other
// tool slots in as a process whose command line follows the template
// below. No provider binary ships with the platform; an absent binary is
// a typed refusal (UNWRAP_PROVIDER_UNAVAILABLE), never a silent fallback
// to the built-in.
//
// PROCESS CONTRACT (the whole integration surface):
//   1. Wrapped phase is written as a raw Float32 plane (row-major, w·h
//      samples, metres of nothing — radians in (−π, π]); input NaN pixels
//      are written as 0.0 and the input validity mask is re-applied to the
//      output (masked pixels never adopt the tool's guess).
//   2. The command line is [bin] + argsTemplate with the placeholders
//      {input} {output} {width} {height} substituted (token-level; a
//      template without any placeholder is a refusal — it cannot address
//      the data).
//   3. The tool must write the unwrapped phase as a raw Float32 plane of
//      EXACTLY w·h samples, aligned with the input (the platform converts
//      to/from its own rasters around the call).
//   4. Exit code 0 and a well-formed output are required. Anything else is
//      a typed failure; stderr is captured into the error message tail.
//   5. All scratch lives in a QTemporaryDir created under the caller's
//      work directory and is removed on EVERY exit path (success, failure,
//      timeout, cancellation) — no half-products outlive the call.
//   6. Binary discovery order: explicit binPath → environment
//      SICNU_SAR_UNWRAP_<PROVIDER>_BIN (upper-cased name) → PATH lookup
//      of the bare name. The provider NAME itself must match
//      [A-Za-z0-9_-]+ (it is never interpreted as a path).
//
// Status vocabulary (domain codes in the operator seam): Ok,
// Unavailable → UNWRAP_PROVIDER_UNAVAILABLE, Failed →
// UNWRAP_PROVIDER_FAILED, Timeout → UNWRAP_PROVIDER_TIMEOUT,
// InvalidOutput → UNWRAP_PROVIDER_INVALID_OUTPUT, Cancelled → the
// operator context's Cancelled error.
#pragma once

#include <complex>
#include <functional>
#include <QString>

#include <vector>

namespace sicnu::sar
{

enum class UnwrapProviderStatus
{
    Ok,
    Unavailable,
    Failed,
    Timeout,
    InvalidOutput,
    Cancelled,
};

struct UnwrapProviderRequest
{
    QString providerName;                 ///< e.g. "snaphu" (not "builtin")
    QString binPath;                      ///< explicit binary path (optional)
    std::vector<QString> argsTemplate;    ///< with {input}/{output}/{width}/{height}
    const double *wrapped = nullptr;      ///< w·h wrapped phase (radians, NaN invalid)
    int w = 0;
    int h = 0;
    int timeoutMs = 600000;               ///< hard process timeout
    QString workDir;                      ///< scratch parent (caller-owned)
    /// Returns true when the caller wants the call aborted (polled).
    std::function<bool()> cancelQuery;
};

struct UnwrapProviderResult
{
    std::vector<double> unwrapped; ///< w·h, NaN where the input was NaN
    long validCount = 0;           ///< finite output samples
    QString commandLine;           ///< provenance (redacted of scratch paths? no —
                                   ///  scratch paths are per-call temp names)
};

/// Runs the external provider under the process contract. Never throws;
/// every failure is a status + domain-coded human-readable @a error.
UnwrapProviderStatus runExternalUnwrapProvider( const UnwrapProviderRequest &request,
                                                UnwrapProviderResult *result,
                                                QString *error = nullptr );

/// The provider name grammar ([A-Za-z0-9_-]+, "builtin" reserved).
bool isValidProviderName( const QString &name );

} // namespace sicnu::sar
