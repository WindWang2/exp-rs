// src/workflow/pipeline_timeout.h — ONE pipeline step-await timeout policy.
#pragma once

#include <chrono>
#include <cstdlib>

namespace sicnu::workflow {

/// swarm-review WFC-2: the pipeline timeout was hardcoded to 30 minutes in
/// two independent places (the CLI runner's deadline loops and the runtime's
/// ExecutionPlane await) with no override, so a long workload (full-scene
/// inference, SAR terrain correction) could never finish — and --resume hit
/// the same wall from scratch. Both sites now resolve the ONE policy here:
///
///   SICNU_PIPELINE_TIMEOUT_SEC unset/empty/invalid -> 1800 s (unchanged);
///   "0"                                            -> unlimited;
///   N > 0                                          -> N seconds.
///
/// Read per call (not cached) so a host that sets the variable before a run
/// is honored; getenv is negligible against operator execution time.
inline std::chrono::milliseconds pipelineTimeoutMs()
{
  constexpr auto kDefault = std::chrono::minutes( 30 );
  // "Unlimited" still needs a finite bound for the deadline arithmetic
  // (steady_clock + duration) and await()'s wait_for loop; one year is past
  // any real workload and far inside steady_clock's representable range.
  constexpr auto kUnlimited = std::chrono::hours( 24 * 365 );
  // Anything at or beyond one year IS "unlimited"; the clamp also keeps the
  // seconds→milliseconds widening inside the duration's representable range
  // (atoll saturates at LLONG_MAX on a digit bomb, which would otherwise
  // overflow the cast).
  constexpr long long kUnlimitedSeconds =
      std::chrono::duration_cast<std::chrono::seconds>( kUnlimited ).count();
  const char *raw = std::getenv( "SICNU_PIPELINE_TIMEOUT_SEC" );
  if ( raw == nullptr || *raw == '\0' )
    return kDefault;
  const long long seconds = std::atoll( raw );
  if ( seconds < 0 )
    return kDefault;
  if ( seconds == 0 || seconds >= kUnlimitedSeconds )
    return kUnlimited;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
           std::chrono::seconds( seconds ) );
}

} // namespace sicnu::workflow
