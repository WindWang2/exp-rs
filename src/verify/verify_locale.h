// src/verify/verify_locale.h
#pragma once

#include <string>
//
// Unified Scientific Verifier (ADR 0172) — numeric-locale pin.
//
// The canonical digest contract (canonicalJsonText) is only deterministic if
// doubles serialize with the classic '.' decimal point on every host. The
// JSON machinery below us formats through snprintf("%.*g") and reads back
// through strtod, and BOTH follow the calling thread's LC_NUMERIC: on a
// de_DE host a canonical text would carry "0,5" — digests diverge across
// machines and stop round-tripping as JSON. Pinning the thread's numeric
// locale to "C" for the duration of a canonical write or bounded parse
// removes that divergence without touching any byte under the C locale.
//
// Scoped, per-thread, no process-global mutation: threads that never enter
// this guard keep their locale untouched, and the platform's ONE canonical
// text stays byte-identical everywhere.
//

namespace sicnu::verify
{

class ClassicNumericLocale
{
  public:
    ClassicNumericLocale();
    ~ClassicNumericLocale();

    ClassicNumericLocale( const ClassicNumericLocale & ) = delete;
    ClassicNumericLocale &operator=( const ClassicNumericLocale & ) = delete;

  private:
#if defined( _WIN32 )
    int mPreviousThreadSetting = -1;
    std::string mPreviousSetting;
#else
    void *mPreviousLocale = nullptr; ///< opaque locale_t (LC_GLOBAL_LOCALE allowed)
#endif
};

} // namespace sicnu::verify
