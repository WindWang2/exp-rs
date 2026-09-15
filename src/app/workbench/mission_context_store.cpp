#include "app/workbench/mission_context_store.h"

#include <QDomDocument>
#include <QDomElement>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace sicnu::app
{

namespace
{
constexpr const char *kXmlElement = "sicnuMissionContext";
constexpr const char *kXmlVersion = "1";
} // namespace

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

bool writeMissionContextToProjectXml( QDomDocument &document, const MissionContext &ctx,
                                      QString *error )
{
    QDomElement root = document.documentElement();
    if ( root.isNull() )
    {
        if ( error )
            *error = QStringLiteral( "project XML missing root" );
        return false;
    }

    // Replace any prior mission blocks.
    for ( QDomElement existing = root.firstChildElement( QString::fromLatin1( kXmlElement ) );
          !existing.isNull(); )
    {
        const QDomElement next =
            existing.nextSiblingElement( QString::fromLatin1( kXmlElement ) );
        root.removeChild( existing );
        existing = next;
    }

    MissionContext toWrite = ctx;
    ensureMissionId( toWrite );
    const QByteArray bytes =
        QJsonDocument( missionContextToJson( toWrite ) ).toJson( QJsonDocument::Compact );

    QDomElement el = document.createElement( QString::fromLatin1( kXmlElement ) );
    el.setAttribute( QStringLiteral( "version" ), QString::fromLatin1( kXmlVersion ) );
    el.appendChild( document.createTextNode( QString::fromUtf8( bytes ) ) );
    root.appendChild( el );
    return true;
}

bool readMissionContextFromProjectXml( const QDomDocument &document, MissionContext &out,
                                       QString *error )
{
    const QDomElement root = document.documentElement();
    if ( root.isNull() )
    {
        if ( error )
            *error = QStringLiteral( "project XML missing root" );
        return false;
    }
    const QDomElement el = root.firstChildElement( QString::fromLatin1( kXmlElement ) );
    if ( el.isNull() )
    {
        if ( error )
            *error = QStringLiteral( "sicnuMissionContext element missing" );
        return false;
    }
    const QString ver = el.attribute( QStringLiteral( "version" ) );
    if ( !ver.isEmpty() && ver != QLatin1String( kXmlVersion ) )
    {
        if ( error )
            *error = QStringLiteral( "unsupported sicnuMissionContext version: %1" ).arg( ver );
        return false;
    }
    const QByteArray bytes = el.text().trimmed().toUtf8();
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson( bytes, &pe );
    if ( pe.error != QJsonParseError::NoError || !doc.isObject() )
    {
        if ( error )
            *error = QStringLiteral( "mission XML JSON parse error: %1" ).arg( pe.errorString() );
        return false;
    }
    return missionContextFromJson( doc.object(), out, error );
}

bool persistMissionContextWithProject( const QString &projectFilePath, QDomDocument &document,
                                       const MissionContext &ctx, QString *error )
{
    QStringList problems;
    bool sideWriteOk = false;
    bool xmlWriteOk = false;

    if ( !projectFilePath.isEmpty() )
    {
        QString sideErr;
        if ( saveMissionContextToSidecar( projectFilePath, ctx, &sideErr ) )
        {
            sideWriteOk = true;
        }
        else
        {
            problems.append( QStringLiteral( "sidecar: %1" ).arg( sideErr ) );
            // Restore prefers an existing sidecar over XML. A failed write that
            // leaves a prior .mission.json in place would shadow the XML channel
            // on the next open — remove the stale file so a successful XML write
            // is not silently discarded. If removal also fails, refuse success.
            const QString side = missionSidecarPathForProject( projectFilePath );
            if ( QFileInfo::exists( side ) )
            {
                if ( !QFile::remove( side ) )
                {
                    problems.append(
                        QStringLiteral( "sidecar: failed write left irremovable stale file: %1" )
                            .arg( side ) );
                    if ( error )
                        *error = problems.join( QStringLiteral( "; " ) );
                    // Still attempt XML so the in-memory document stays consistent,
                    // but do not claim persist success while stale sidecar remains.
                    QString xmlErr;
                    if ( writeMissionContextToProjectXml( document, ctx, &xmlErr ) )
                        xmlWriteOk = true;
                    else
                        problems.append( QStringLiteral( "xml: %1" ).arg( xmlErr ) );
                    if ( error )
                        *error = problems.join( QStringLiteral( "; " ) );
                    return false;
                }
            }
        }
    }

    QString xmlErr;
    if ( writeMissionContextToProjectXml( document, ctx, &xmlErr ) )
        xmlWriteOk = true;
    else
        problems.append( QStringLiteral( "xml: %1" ).arg( xmlErr ) );

    if ( !problems.isEmpty() && error )
        *error = problems.join( QStringLiteral( "; " ) );

    // Success only when at least one channel's *this* write succeeded — never
    // infer success from QFileInfo::exists() (stale sidecar after QSaveFile fail).
    return sideWriteOk || xmlWriteOk;
}

bool restoreMissionContextWithProject( const QString &projectFilePath, const QDomDocument &document,
                                       MissionContext &out, bool *loaded, QString *error )
{
    if ( loaded )
        *loaded = false;

    // Prefer sidecar when present (explicit file next to project).
    if ( !projectFilePath.isEmpty() )
    {
        const QString side = missionSidecarPathForProject( projectFilePath );
        if ( QFileInfo::exists( side ) )
        {
            QString sideErr;
            if ( loadMissionContextFromSidecar( projectFilePath, out, &sideErr ) )
            {
                if ( loaded )
                    *loaded = true;
                return true;
            }
            if ( error )
                *error = sideErr;
            // Fall through to XML rather than failing the project open.
        }
    }

    QString xmlErr;
    if ( readMissionContextFromProjectXml( document, out, &xmlErr ) )
    {
        if ( loaded )
            *loaded = true;
        return true;
    }

    // Neither present — fresh project / older projects without mission.
    if ( error && error->isEmpty() )
        *error = xmlErr;
    return true;
}

} // namespace sicnu::app
