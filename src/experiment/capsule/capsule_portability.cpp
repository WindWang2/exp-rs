// capsule_portability.cpp — see capsule_portability.h.
#include "capsule_portability.h"

namespace sicnu::experiment::capsule
{

namespace
{

QString withForwardSlashes( QString path )
{
    path.replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) );
    return path;
}

QString fileNameOf( const QString &posixPath )
{
    const qsizetype slash = posixPath.lastIndexOf( QLatin1Char( '/' ) );
    return slash >= 0 ? posixPath.mid( slash + 1 ) : posixPath;
}

} // namespace

QString toPortableRef( const QString &path, const QString &workspaceRoot )
{
    const QString normalized = withForwardSlashes( path );
    const QString root = withForwardSlashes( workspaceRoot );
    if ( !root.isEmpty() )
    {
        QString stripped = root;
        while ( stripped.endsWith( QLatin1Char( '/' ) ) )
            stripped.chop( 1 );
        if ( !stripped.isEmpty() )
        {
            if ( normalized == stripped )
                return QStringLiteral( "workspace:" );
            if ( normalized.startsWith( stripped + QLatin1Char( '/' ) ) )
                return QStringLiteral( "workspace:" )
                       + normalized.mid( stripped.length() + 1 );
        }
    }
    // Outside the workspace: the file NAME stays as a human hint; the
    // identity is the digest recorded beside the reference.
    return QStringLiteral( "external:" ) + fileNameOf( normalized );
}

bool isPortableRef( const QString &ref )
{
    return ref.startsWith( QLatin1String( "workspace:" ) )
           || ref.startsWith( QLatin1String( "external:" ) );
}

} // namespace sicnu::experiment::capsule
