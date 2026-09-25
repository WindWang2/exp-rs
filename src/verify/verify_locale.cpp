// src/verify/verify_locale.cpp — per-thread numeric-locale pin (see the
// header for the digest-determinism rationale).
#include "verify_locale.h"

#if defined( _WIN32 )
#include <locale.h>
#else
#include <clocale>
#if defined( __APPLE__ )
#include <xlocale.h>
#endif
#endif

namespace sicnu::verify
{

#if defined( _WIN32 )

ClassicNumericLocale::ClassicNumericLocale()
{
    // Per-thread locale discipline: the previous _configthreadlocale state
    // and the previous LC_NUMERIC setting are both restored, so a thread
    // that already opted into per-thread locales keeps its regime.
    mPreviousThreadSetting = _configthreadlocale( _ENABLE_PER_THREAD_LOCALE );
    if ( const char *previous = setlocale( LC_NUMERIC, nullptr ) )
        mPreviousSetting = previous;
    setlocale( LC_NUMERIC, "C" );
}

ClassicNumericLocale::~ClassicNumericLocale()
{
    if ( !mPreviousSetting.empty() )
        setlocale( LC_NUMERIC, mPreviousSetting.c_str() );
    if ( mPreviousThreadSetting != -1 )
        _configthreadlocale( mPreviousThreadSetting );
}

#elif defined( LC_NUMERIC_MASK )

ClassicNumericLocale::ClassicNumericLocale()
{
    // uselocale() is per-thread POSIX locale switching: the process-global
    // locale and other threads are untouched. The classic locale is created
    // once per process (thread-safe static init) and never freed — it lives
    // as long as the canonical-text contract does.
    static locale_t sClassic = newlocale( LC_NUMERIC_MASK, "C", static_cast<locale_t>( nullptr ) );
    if ( sClassic )
        mPreviousLocale = uselocale( sClassic );
}

ClassicNumericLocale::~ClassicNumericLocale()
{
    if ( mPreviousLocale )
        uselocale( static_cast<locale_t>( mPreviousLocale ) );
}

#else

// Platform without a per-thread locale API in reach of this build: the
// guard degrades to a no-op, which preserves today's behavior (and today's
// digests) exactly — canonical text on such a platform remains correct as
// long as the process stays in the C locale, which is its default.
ClassicNumericLocale::ClassicNumericLocale() = default;
ClassicNumericLocale::~ClassicNumericLocale() = default;

#endif

} // namespace sicnu::verify
