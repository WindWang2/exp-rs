#pragma once

// suitability_time.h — the one ISO-8601 UTC reader of the suitability module.
//
// Every timestamp key in this module is suffixed "_utc": the wire contract
// says the stamp IS UTC even when it carries no zone designator. Qt would
// otherwise parse a zoneless stamp as Qt::LocalTime and interpret it through
// the SYSTEM zone, so two hosts (or one host across DST changes) disagree on
// the absolute moment while serializing byte-identical digests. Both prior
// inline copies lived in scene_candidate.cpp; goal windows now share this
// helper with them — keep it the single authority.

#include <QDateTime>
#include <QTimeZone>
#include <QString>

namespace sicnu::suitability
{

/// Parses an ISO-8601 stamp. A zoneless stamp is read as UTC (the keys
/// carrying it are suffixed "_utc"); a stamp with an explicit offset keeps
/// that offset, which is already an absolute moment. Invalid text returns an
/// invalid QDateTime — callers decide between typed failure and unknown.
///
/// Qt parses a zoneless stamp into a system-zone-backed spec (Qt::LocalTime
/// or Qt::TimeZone depending on the Qt minor version) — comparing those
/// QDateTime objects across hosts silently flips the absolute moment. That
/// is why the zone check must cover BOTH specs, not just Qt::LocalTime.
inline QDateTime parseIsoUtc( const QString &text )
{
    QDateTime parsed = QDateTime::fromString( text, Qt::ISODate );
    const Qt::TimeSpec spec = parsed.timeSpec();
    if ( parsed.isValid() && ( spec == Qt::LocalTime || spec == Qt::TimeZone ) )
        parsed.setTimeZone( QTimeZone::utc() );
    return parsed;
}

} // namespace sicnu::suitability
