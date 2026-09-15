#include "app/workbench/mission_context_store.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace sicnu::app
{

QString missionSidecarPathForProject( const QString &projectFilePath )
{
    const QFileInfo fi( projectFilePath );
    if ( projectFilePath.isEmpty() )
        return {};
    return fi.dir().filePath( fi.completeBaseName() + QStringLiteral( ".mission.json" ) );
}

bool saveMissionContextToSidecar( const QString &projectFilePath, const MissionContext &ctx,
                                  QString *error )
{
    const QString path = missionSidecarPathForProject( projectFilePath );
    if ( path.isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "empty project path" );
        return false;
    }
    MissionContext toWrite = ctx;
    ensureMissionId( toWrite );
    if ( toWrite.projectRef.isEmpty() )
        toWrite.projectRef = projectFilePath;

    const QJsonObject doc = missionContextToJson( toWrite );
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        if ( error )
            *error = QStringLiteral( "cannot open sidecar for write: %1" ).arg( path );
        return false;
    }
    const QByteArray bytes = QJsonDocument( doc ).toJson( QJsonDocument::Indented );
    if ( file.write( bytes ) != bytes.size() )
    {
        if ( error )
            *error = QStringLiteral( "short write to sidecar: %1" ).arg( path );
        file.cancelWriting();
        return false;
    }
    if ( !file.commit() )
    {
        if ( error )
            *error = QStringLiteral( "commit failed for sidecar: %1" ).arg( path );
        return false;
    }
    return true;
}

bool loadMissionContextFromSidecar( const QString &projectFilePath, MissionContext &out,
                                    QString *error )
{
    const QString path = missionSidecarPathForProject( projectFilePath );
    if ( path.isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "empty project path" );
        return false;
    }
    QFile file( path );
    if ( !file.exists() )
    {
        if ( error )
            *error = QStringLiteral( "sidecar missing: %1" ).arg( path );
        return false;
    }
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        if ( error )
            *error = QStringLiteral( "cannot open sidecar: %1" ).arg( path );
        return false;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &pe );
    if ( pe.error != QJsonParseError::NoError || !doc.isObject() )
    {
        if ( error )
            *error = QStringLiteral( "sidecar JSON parse error: %1" ).arg( pe.errorString() );
        return false;
    }
    return missionContextFromJson( doc.object(), out, error );
}

} // namespace sicnu::app
