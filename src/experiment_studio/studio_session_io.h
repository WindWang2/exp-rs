// studio_session_io.h — bounded, atomic Studio session persistence (R30-T07).
//
// The dock used to write its single AppData file with QFile(WriteOnly|Truncate)
// and ignore write()'s return, then restore it with an unbounded readAll().
// Both are replaced here by the project's durable-sidecar authority:
//   * write: staged temp -> fsync -> atomic rename -> read-back verify ->
//     last-good rotation. A failed save leaves the previous document intact.
//   * read: main -> last-good, size-bounded (no unbounded allocation).
// Schema/version and numeric validation stay with the CALLER-side decoder
// (StudioSessionState::fromJson), matching the sidecar contract.
#pragma once

#include "experiment_studio/studio_session.h"

#include <QString>
#include <QStringList>

namespace sicnu::experiment_studio
{

/// A session is UI navigation state, not a raster: 4 MiB is generous for a
/// full 1000-row report echo and still bounds a hostile/corrupt document.
inline constexpr qint64 kStudioSessionMaxBytes = 4LL * 1024 * 1024;
inline constexpr const char kStudioSessionLastGoodSuffix[] = ".last-good";

enum class StudioSessionLoadStatus
{
    Ok,
    Missing,
    Unreadable,
    Oversize,
    DamagedJson,
    ForeignSchema,
    InvalidFields,
    RecoveredLastGood,
};

const char *studioSessionLoadStatusName( StudioSessionLoadStatus status );

struct StudioSessionLoadResult
{
    StudioSessionLoadStatus status = StudioSessionLoadStatus::Missing;
    QString code;      ///< stable dotted code for logs/tests
    QString message;   ///< user-facing sentence (translatable at the call site)
    QStringList issues;
    StudioSessionState state;

    bool ok() const
    {
        return status == StudioSessionLoadStatus::Ok
               || status == StudioSessionLoadStatus::RecoveredLastGood;
    }
};

struct StudioSessionWriteResult
{
    bool ok = false;
    QString code;
    QString message;
    qint64 bytes = 0;
};

StudioSessionLoadResult loadStudioSessionFile( const QString &path,
                                               qint64 maxBytes = kStudioSessionMaxBytes );

StudioSessionWriteResult saveStudioSessionFile( const QString &path,
                                                const StudioSessionState &state,
                                                qint64 maxBytes = kStudioSessionMaxBytes );

/// Strict, typed conversion for the GUI's numeric restore. Refuses (returns
/// false and fills @p why) instead of casting a double with UB / clamping a
/// hostile value and pretending the field was restored.
bool studioSessionIntValue( const QJsonValue &value, int minValue, int maxValue,
                            int *out, QString *why );
bool studioSessionInt64Value( const QJsonValue &value, qint64 minValue, qint64 maxValue,
                              qint64 *out, QString *why );
bool studioSessionDoubleValue( const QJsonValue &value, double minValue, double maxValue,
                               double *out, QString *why );

} // namespace sicnu::experiment_studio
