/***************************************************************************
 * mission_timeline_store.cpp — atomic sidecar persistence for the task space
 ***************************************************************************/

#include "app/workbench/mission_timeline_store.h"

#include "app/workbench/mission_context_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace sicnu::app
{

QString missionTimelineSidecarPathForProject( const QString &projectFilePath )
{
    if ( projectFilePath.isEmpty() )
        return QString();

    QFileInfo info( projectFilePath );
    QString base = info.path();
    if ( base.isEmpty() )
        base = QStringLiteral( "." );

    QString stem = info.completeBaseName(); // strips .qgz / .qgs
    if ( stem.isEmpty() )
        stem = info.fileName();

    return base + QLatin1Char( '/' ) + stem + QStringLiteral( ".mission-timeline.json" );
}

bool loadMissionTimelineFromSidecar( const QString &projectFilePath,
                                     MissionTimeline &out,
                                     bool *loaded,
                                     QString *error )
{
    if ( loaded )
        *loaded = false;

    const QString path = missionTimelineSidecarPathForProject( projectFilePath );
    if ( path.isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "empty_project_path" );
        return false;
    }

    if ( !QFileInfo::exists( path ) )
    {
        // A fresh project simply has no timeline yet — not an error.
        return true;
    }

    // Bounded read (#WP08): an oversized legacy residue is refused before
    // allocation instead of slurped — the write authority never published
    // above its byte ceiling, so a larger file is hostile or torn input.
    QByteArray bytes;
    QString readErr;
    if ( !readMissionSidecarBytes( path, bytes, &readErr ) )
    {
        if ( error )
            *error = QStringLiteral( "read_failed:%1" )
                         .arg( readErr.isEmpty() ? QStringLiteral( "unknown" ) : readErr );
        return false;
    }
    if ( bytes.isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "empty_artifact" );
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson( bytes, &parseError );
    if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
    {
        if ( error )
            *error = QStringLiteral( "invalid_json:%1" ).arg( parseError.errorString() );
        return false;
    }

    MissionTimeline decoded;
    QString decodeError;
    if ( !decoded.fromJson( doc.object(), &decodeError ) )
    {
        if ( error )
            *error = decodeError.isEmpty() ? QStringLiteral( "decode_failed" ) : decodeError;
        return false;
    }

    out = std::move( decoded );
    if ( loaded )
        *loaded = true;
    return true;
}

} // namespace sicnu::app
