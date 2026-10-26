/***************************************************************************
  sicnu_test_time_budget.h — scaled wall-clock ceilings for test assertions

  #1699: absolute wall-clock thresholds (40ms/50ms/2s…) measured on a quiet
  Release build flake when the same case runs under ASan/UBSan (typically
  2-5x slower) or inside a parallel ctest lane on a loaded shared machine.
  Tests that assert "operation X stays inside budget B" keep B as their
  Release-meaningful base and instead assert
  `elapsed < wallClockBudgetMs(B)`: unchanged by default, widened 4x under
  sanitizers, and multipliable ad hoc via SICNU_TEST_TIME_SCALE (e.g. a
  reproduce run on a thrashing host: SICNU_TEST_TIME_SCALE=10 ctest -R ...).

  Deliberately NOT a license to relax budgets: a base that only passes
  because of the multiplier is a perf regression the Release lane still
  catches, since Release runs with scale 1.0.

  Qt-free on purpose so any test (Catch2, QtCore-only, ...) can include it.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstdlib>

namespace sicnu::testsupport
{

// The multiplier applied to a base ceiling. Computed once per process.
// Detection covers GCC's __SANITIZE_*__ and clang's __has_feature ASan/TSan
// (UBSan-only clang and MSVC /fsanitize are NOT detected — no portable
// macro; such lanes set SICNU_TEST_TIME_SCALE explicitly).
inline double wallClockScale()
{
  static const double scale = [] {
    double s = 1.0;
#if defined( __SANITIZE_ADDRESS__ ) || defined( __SANITIZE_THREAD__ ) || defined( __SANITIZE_UNDEFINED__ )
    s *= 4.0;
#elif defined( __has_feature )
#if __has_feature( address_sanitizer ) || __has_feature( thread_sanitizer )
    s *= 4.0;
#endif
#endif
    if ( const char *env = std::getenv( "SICNU_TEST_TIME_SCALE" ) )
    {
      char *end = nullptr;
      const double v = std::strtod( env, &end );
      // Bounded parse (R1-9): reject garbage, NaN, inf and absurd factors —
      // a hostile/garbage value must not turn budgets into UB or no-ops.
      if ( end != env && v >= 0.001 && v <= 1000000.0 )
        s *= v;
    }
    return s;
  }();
  return scale;
}

// baseMs: the ceiling that must hold on a quiet, unsanitized build.
inline std::int64_t wallClockBudgetMs( std::int64_t baseMs )
{
  return static_cast<std::int64_t>( static_cast<double>( baseMs ) * wallClockScale() ) + 1;
}

} // namespace sicnu::testsupport
