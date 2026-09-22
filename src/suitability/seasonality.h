#pragma once

// seasonality.h — month-to-season mapping for the temporal seasonality
// criterion.

#include <QString>

namespace sicnu::suitability
{

/// Maps a 1-12 month number to its meteorological season:
/// winter (12, 1, 2), spring (3-5), summer (6-8), autumn (9-11).
///
/// Declared assumption: NORTHERN-hemisphere meteorological seasons — the
/// goal schema carries no target hemisphere, so a southern-hemisphere goal
/// would need an explicit offset parameter added before this mapping is
/// reused there. Any month outside 1-12 returns an empty string.
QString seasonForMonth( int month1to12 );

} // namespace sicnu::suitability
