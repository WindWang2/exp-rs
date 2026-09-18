/// mcp_workspace_policy.cpp — canonical SICNU_MCP_WORKSPACE containment.
/// Moved verbatim from mcp_server.cpp's anonymous namespace (#1033) so the
/// data-platform tool family and run_workflow recording args enforce the
/// exact same policy as the sibling branches instead of a re-derivation.
#include "mcp_workspace_policy.h"

#include "env_flag.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QUrl>
#include <QMetaType>
#include <QVariant>

namespace sicnu::agent
{

QString mcpWorkspaceRoot()
{
    return QProcessEnvironment::systemEnvironment().value(
        QStringLiteral( "SICNU_MCP_WORKSPACE" ) );
}

bool mcpPathOutsideWorkspace( const QString &pathValue, const QString &workspaceRoot,
                              QString *detail )
{
    if ( pathValue.isEmpty() )
        return false;

    // URL / VSI virtual-path awareness (#722-era remote policy): a network
    // data reference is NOT a filesystem path — treating one as relative
    // (QFileInfo::isAbsolute() == false for "https://host/x.tif") wrongly
    // ALLOWED any URL, while on Linux "/vsicurl/https://..." canonicalized
    // outside the workspace and was wrongly REJECTED. Policy: remote
    // http(s) data references (optionally /vsicurl/-prefixed) are allowed
    // read-only data inputs (bounded by GDAL HTTP timeouts); file:// maps to
    // its local path and falls through to the workspace check; every other
    // scheme is rejected. SICNU_MCP_ALLOW_REMOTE=0 restores strict local-only.
    {
        const QString trimmed = pathValue.trimmed();
        const QString lowered = trimmed.toLower();
        const bool vsiPrefixed = lowered.startsWith( QStringLiteral( "/vsicurl/" ) );
        const bool vsiOther = lowered.startsWith( QStringLiteral( "/vsi" ) ) && !vsiPrefixed;
        const bool httpUrl = lowered.startsWith( QStringLiteral( "http://" ) ) ||
                             lowered.startsWith( QStringLiteral( "https://" ) );
        const bool fileUrl = lowered.startsWith( QStringLiteral( "file://" ) );
        if ( vsiOther && !vsiPrefixed )
        {
            if ( detail )
                *detail = QStringLiteral( "Only /vsicurl/ remote sources are supported: %1" ).arg( pathValue );
            return true;
        }
        if ( httpUrl || vsiPrefixed )
        {
            if ( !envFlagEnabled( "SICNU_MCP_ALLOW_REMOTE" ) )
            {
                if ( detail )
                    *detail = QStringLiteral( "Remote data references are disabled "
                                              "(set SICNU_MCP_ALLOW_REMOTE=1): %1" ).arg( pathValue );
                return true;
            }
            return false; // scheme-validated remote reference, not a workspace path
        }
        if ( fileUrl )
        {
            const QUrl url( trimmed );
            // file:///abs/path -> local path; falls through to the workspace check.
            QString local = url.toLocalFile();
            if ( local.isEmpty() )
                local = trimmed.mid( 7 );
            if ( !mcpPathOutsideWorkspace( local, workspaceRoot, detail ) )
                return false;
            if ( detail && detail->isEmpty() )
                *detail = QStringLiteral( "Path outside SICNU_MCP_WORKSPACE: %1" ).arg( pathValue );
            return true;
        }
    }

    QString path = pathValue;
    if ( path.startsWith( QLatin1Char( '~' ) ) )
    {
        path = QDir::homePath() + path.mid( 1 );
    }

    QString workspaceCanon = QDir( workspaceRoot ).canonicalPath();
    if ( workspaceCanon.isEmpty() )
        workspaceCanon = QFileInfo( workspaceRoot ).absoluteFilePath();
    if ( workspaceCanon.isEmpty() )
        return false;

    const QFileInfo fi( path );
    QString resolved;
    if ( fi.isAbsolute() )
    {
        if ( fi.exists() )
        {
            resolved = fi.canonicalFilePath();
        }
        else
        {
            // Non-existent output path: resolve parent dir + filename
            QDir parent = fi.dir();
            QString parentCanon = parent.canonicalPath();
            if ( parentCanon.isEmpty() )
                parentCanon = parent.absolutePath();
            resolved = QDir( parentCanon ).filePath( fi.fileName() );
        }
    }
    else
    {
        const QString joined = QDir( workspaceCanon ).filePath( path );
        const QFileInfo fiJoined( joined );
        if ( fiJoined.exists() )
        {
            resolved = fiJoined.canonicalFilePath();
        }
        else
        {
            QDir parent = fiJoined.dir();
            QString parentCanon = parent.canonicalPath();
            if ( parentCanon.isEmpty() )
                parentCanon = parent.absolutePath();
            resolved = QDir( parentCanon ).filePath( fiJoined.fileName() );
        }
    }

    const QString normResolved = QDir::cleanPath( resolved );
    const QString normWorkspace = QDir::cleanPath( workspaceCanon );

    if ( normResolved == normWorkspace )
        return false;
    if ( normResolved.startsWith( normWorkspace + QLatin1Char( '/' ) ) )
        return false;

    if ( detail )
    {
        *detail = QStringLiteral( "Path outside SICNU_MCP_WORKSPACE: %1" ).arg( pathValue );
    }
    return true;
}

bool mcpCollectOutsideWorkspace( const QVariant &value, const QString &workspaceRoot,
                                 QString *detail )
{
    if ( value.userType() == QMetaType::QString )
    {
        return mcpPathOutsideWorkspace( value.toString(), workspaceRoot, detail );
    }
    if ( value.userType() == QMetaType::QVariantList )
    {
        const QVariantList list = value.toList();
        for ( const QVariant &item : list )
        {
            if ( mcpCollectOutsideWorkspace( item, workspaceRoot, detail ) )
                return true;
        }
        return false;
    }
    if ( value.userType() == QMetaType::QVariantMap )
    {
        const QVariantMap map = value.toMap();
        for ( auto it = map.constBegin(); it != map.constEnd(); ++it )
        {
            if ( mcpCollectOutsideWorkspace( it.value(), workspaceRoot, detail ) )
                return true;
        }
        return false;
    }
    return false;
}

QString mcpResolveWorkspacePath( const QString &path )
{
    if ( path.isEmpty() )
        return path;

    QString resolved = path;
    if ( resolved.startsWith( QLatin1Char( '~' ) ) )
        resolved = QDir::homePath() + resolved.mid( 1 );

    const QString workspace = mcpWorkspaceRoot();
    if ( workspace.isEmpty() || QFileInfo( resolved ).isAbsolute() )
        return resolved;

    // Same front half as mcpPathOutsideWorkspace: relative arguments are
    // read as workspace-relative. Resolving HERE (not just during the
    // check) is what keeps containment airtight — a relative path that
    // passed validation would otherwise still be opened CWD-relative by the
    // consuming handler, i.e. possibly outside the sandbox (#1033).
    return QDir::cleanPath( QDir( workspace ).filePath( resolved ) );
}

} // namespace sicnu::agent
