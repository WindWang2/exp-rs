// src/agent/tool_catalog/workspace_containment.h
//
// Workspace path containment — the ONE implementation of the sandbox policy
// shared by the MCP gate (surfacePathOutsideWorkspace / McpServer), the
// data-platform handlers and the headless CLI pipeline runner.
//
// Header-only on purpose: it depends on Qt Core alone, so translation units
// that are compiled straight into test executables (src/cli/
// rs_pipeline_runner.cpp) can use the exact same policy without linking the
// whole sicnu_agent library. A second, drifted copy of this logic in the CLI
// is what let https:// references bypass the remote-access default-deny.
//
// Policy summary:
//   * Relative paths resolve against the workspace root.
//   * Paths are weakly canonicalized: the deepest EXISTING ancestor is
//     resolved with symlinks followed, the remaining (not yet existing)
//     components are appended verbatim, and a ".." among them — or a
//     dangling symlink on the way — is rejected. This closes the escape
//     where "<ws>/link/newdir/file.tif" (link -> outside) was classified as
//     inside because only one missing parent level used to be resolved.
//   * http(s):// and /vsicurl/ references are remote data inputs: denied
//     unless SICNU_MCP_ALLOW_REMOTE=1. file:// maps to its local path. Every
//     other URL scheme and every other /vsi* prefix is rejected.
//   * A workspace root that cannot be resolved fails CLOSED.
#pragma once

#include "../env_flag.h" // shared SICNU_* flag parser (no drift)

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <utility>
#ifdef Q_OS_WIN
#include <filesystem>
#include <system_error>
#endif

namespace sicnu::agent::tool_catalog::containment
{

/// Canonical form of an EXISTING filesystem entry (the "deepest existing
/// ancestor" in weaklyCanonicalPath). QFileInfo::canonicalFilePath() does
/// not resolve Windows directory junctions — QFileInfo::isSymLink() reports
/// false for mount-point reparse points — while the OS still traverses them,
/// so a junction inside the workspace would silently escape the sandbox.
/// std::filesystem::canonical resolves every reparse point to its final
/// path on MSVC. Empty result = fail closed, same as a failed
/// canonicalFilePath().
inline QString canonicalExistingPath( const QFileInfo &info )
{
#ifdef Q_OS_WIN
    std::error_code ec;
    const std::filesystem::path resolved =
        std::filesystem::canonical( std::filesystem::path( info.filePath().toStdWString() ), ec );
    if ( ec )
        return QString();
    QString canon = QString::fromStdWString( resolved.lexically_normal().wstring() );
    // GetFinalPathNameByHandle (which canonical() is built on) may return a
    // verbatim \\?\ / \\?\UNC\ path; normalize back to the forward-slash,
    // drive-letter form the rest of this header uses.
    if ( canon.startsWith( QStringLiteral( "\\\\?\\UNC\\" ) ) )
        canon = QStringLiteral( "//" ) + canon.mid( 8 );
    else if ( canon.startsWith( QStringLiteral( "\\\\?\\" ) ) )
        canon = canon.mid( 4 );
    return QDir::fromNativeSeparators( canon );
#else
    return info.canonicalFilePath();
#endif
}

inline Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

/// Weakly canonical form of @p path (std::filesystem::weakly_canonical with
/// a stricter tail rule). Relative input is made absolute against the
/// process CWD — callers that want workspace-relative semantics must join
/// first. Returns an empty string when the path cannot be classified safely:
/// a ".." after the first missing component, or a dangling symlink.
inline QString weaklyCanonicalPath( const QString &path )
{
    if ( path.isEmpty() )
        return QString();
    QString head = QDir::fromNativeSeparators( path );
    if ( !QDir::isAbsolutePath( head ) )
        head = QDir::currentPath() + QLatin1Char( '/' ) + head; // no lexical cleaning

    const auto isRoot = []( const QString &p ) {
        return p == QLatin1String( "/" )
               || ( p.size() == 3 && p.at( 1 ) == QLatin1Char( ':' ) && p.at( 2 ) == QLatin1Char( '/' ) );
    };

    QStringList tail;
    for ( int guard = 0; guard < 4096; ++guard )
    {
        while ( head.size() > 1 && head.endsWith( QLatin1Char( '/' ) ) && !isRoot( head ) )
            head.chop( 1 );
        const QFileInfo info( head );
        if ( info.exists() )
        {
            QString base = canonicalExistingPath( info );
            if ( base.isEmpty() )
                return QString();
            for ( const QString &component : std::as_const( tail ) )
            {
                if ( component.isEmpty() || component == QLatin1String( "." ) )
                    continue;
                if ( component == QLatin1String( ".." ) )
                    return QString(); // cannot be resolved lexically without following links
                base = base.endsWith( QLatin1Char( '/' ) ) ? base + component
                                                           : base + QLatin1Char( '/' ) + component;
            }
            return base;
        }
        // exists() follows links; a symlink that does not resolve would be
        // followed by open(O_CREAT)/mkdir wherever it points — refuse it.
        if ( info.isSymLink() )
            return QString();
        if ( isRoot( head ) )
            return QString();
        const int slash = head.lastIndexOf( QLatin1Char( '/' ) );
        if ( slash < 0 )
            return QString();
        tail.prepend( head.mid( slash + 1 ) );
        QString parent = head.left( slash );
        if ( parent.isEmpty() )
            parent = QStringLiteral( "/" );
        else if ( parent.size() == 2 && parent.at( 1 ) == QLatin1Char( ':' ) )
            parent += QLatin1Char( '/' );
        if ( parent == head )
            return QString();
        head = parent;
    }
    return QString();
}

/// True when canonical @p candidate equals or lies below canonical @p root.
inline bool canonicalPathWithin( const QString &candidate, const QString &root )
{
    if ( candidate.isEmpty() || root.isEmpty() )
        return false;
    const Qt::CaseSensitivity cs = pathCaseSensitivity();
    if ( candidate.compare( root, cs ) == 0 )
        return true;
    const QString prefix = root.endsWith( QLatin1Char( '/' ) ) ? root : root + QLatin1Char( '/' );
    return candidate.startsWith( prefix, cs );
}

/// Canonical form of a workspace root; empty when it cannot be resolved.
inline QString canonicalWorkspaceRoot( const QString &workspaceRoot )
{
    if ( workspaceRoot.trimmed().isEmpty() )
        return QString();
    return weaklyCanonicalPath( QDir::fromNativeSeparators( workspaceRoot.trimmed() ) );
}

/// Absolute path a (possibly relative / ~-prefixed) filesystem argument
/// refers to when relative paths are anchored at @p workspaceRoot. Returns
/// @p path unchanged when it is already absolute or the root is empty.
inline QString resolveAgainstWorkspace( const QString &path, const QString &workspaceRoot )
{
    QString expanded = path;
    if ( expanded.startsWith( QLatin1Char( '~' ) ) )
        expanded = QDir::homePath() + expanded.mid( 1 );
    if ( QFileInfo( expanded ).isAbsolute() || workspaceRoot.trimmed().isEmpty() )
        return expanded;
    return QDir( workspaceRoot ).filePath( expanded );
}

enum class ReferenceKind
{
    LocalPath,     ///< ordinary filesystem path (possibly relative)
    FileUrl,       ///< file:// — @c localPath holds the mapped path
    RemoteAllowed, ///< http(s):// or /vsicurl/ with SICNU_MCP_ALLOW_REMOTE=1
    DriverPath,    ///< GDAL driver prefix with an embedded path — @c localPath holds it
    Denied         ///< disallowed scheme / vsi prefix / remote disabled
};

struct ReferenceVerdict
{
    ReferenceKind kind = ReferenceKind::LocalPath;
    QString localPath; ///< for FileUrl / DriverPath
    QString reason;    ///< for Denied
};

/// Driver names whose GDAL/OGR connection string carries a FILESYSTEM path
/// (CSV:/abs/x.csv, GPKG:/x.gpkg:layer, NETCDF:"x.nc":var, ...). The embedded
/// path is extracted and re-checked with the same containment policy as a
/// bare path, so in-workspace connection strings keep working (#560) while
/// absolute/remote embeddings cannot escape (#1704).
inline bool isFileBackedGdalDriver( const QString &driver )
{
    // R2: entries must be the driver's SHORT NAME as it appears in a
    // connection string. Space-bearing display names ("ESRI Shapefile",
    // "MapInfo File") can never match gdalDriverPrefixOf() and were removed;
    // "tiledbspatial" was a wrong guess — the real short name is "TileDB".
    static const QSet<QString> drivers = {
        QStringLiteral( "csv" ), QStringLiteral( "gpkg" ), QStringLiteral( "sqlite" ),
        QStringLiteral( "openfilegdb" ), QStringLiteral( "filegdb" ),
        QStringLiteral( "kml" ), QStringLiteral( "geojson" ), QStringLiteral( "geojsonseq" ),
        QStringLiteral( "georss" ), QStringLiteral( "gpx" ), QStringLiteral( "ogr_gmt" ),
        QStringLiteral( "ogr_pds" ), QStringLiteral( "ogr_sdts" ), QStringLiteral( "ogr_vrt" ),
        QStringLiteral( "vrt" ), QStringLiteral( "netcdf" ), QStringLiteral( "hdf5" ),
        QStringLiteral( "hdf4" ), QStringLiteral( "zarr" ), QStringLiteral( "tiledb" ),
        QStringLiteral( "pdf" ), QStringLiteral( "cad" ), QStringLiteral( "dxf" ),
        QStringLiteral( "dwg" ), QStringLiteral( "ogr_ods" ), QStringLiteral( "ogr_xlsx" ),
        QStringLiteral( "ogr_xls" ), QStringLiteral( "jml" ), QStringLiteral( "svgp" ),
        QStringLiteral( "vfk" ), QStringLiteral( "selafin" ), QStringLiteral( "ili" ),
        QStringLiteral( "nas" ),
    };
    return drivers.contains( driver.toLower() );
}

/// Driver names whose connection string makes GDAL open a NETWORK target
/// (WMS/WMTS/WCS tiles, PG/MySQL/MSSQL databases, ...). They follow the same
/// default-deny remote policy as http(s):// references (#1704).
inline bool isNetworkGdalDriver( const QString &driver )
{
    static const QSet<QString> drivers = {
        QStringLiteral( "wms" ), QStringLiteral( "wmts" ), QStringLiteral( "wcs" ),
        QStringLiteral( "wfs" ), QStringLiteral( "wfs3" ), QStringLiteral( "oapif" ),
        QStringLiteral( "ogcapi" ), QStringLiteral( "pg" ), QStringLiteral( "postgresql" ),
        QStringLiteral( "mysql" ), QStringLiteral( "mssql" ), QStringLiteral( "mssqlspatial" ),
        // R2-F1: "oracle" was a display-name guess — the connection-string
        // short name is "OCI" (OCI:usr/pwd@db), same class as PG:. Cloud/
        // service drivers verified by short name: EEDA, AmigoCloud,
        // GeoRaster.
        QStringLiteral( "oci" ), QStringLiteral( "eeda" ), QStringLiteral( "amigocloud" ),
        QStringLiteral( "georaster" ), QStringLiteral( "odbc" ), QStringLiteral( "elasticsearch" ),
        QStringLiteral( "carto" ), QStringLiteral( "cartodb" ), QStringLiteral( "gft" ),
        QStringLiteral( "mongodb" ), QStringLiteral( "mongodbv3" ), QStringLiteral( "plmos" ),
        QStringLiteral( "daas" ), QStringLiteral( "coap" ),
    };
    return drivers.contains( driver.toLower() );
}

/// Embedded path of a file-backed driver connection string. Returns an empty
/// string when no path can be extracted safely (fail closed). Handles the
/// quoted form (NETCDF:"/x.nc":var), nested scheme/vsi references
/// (GeoJSON:http://…, CSV:/vsicurl/…), a Windows drive inside the connection
/// (GPKG:C:/x.gpkg:layer) and the layer/subdataset ':' separator.
inline QString embeddedDriverPath( const QString &rest )
{
    if ( rest.isEmpty() )
        return QString();
    if ( rest.startsWith( QLatin1Char( '"' ) ) )
    {
        const int end = rest.indexOf( QLatin1Char( '"' ), 1 );
        if ( end <= 1 )
            return QString();
        return rest.mid( 1, end - 1 );
    }
    // R1-F1: ANY scheme:// payload (s3://, gs://, oss://, ftp://, ... — not
    // just the http(s)/file forms enumerated before) is a nested REMOTE or
    // URL reference, never a plain file segment: hand the whole payload back
    // so the recursive classifyReference() applies the URL-scheme policy
    // (Zarr:s3://bucket must not degrade to the relative name "s3").
    static const QRegularExpression kPayloadScheme(
        QStringLiteral( "^[a-z][a-z0-9+.-]+://" ) );
    const QString lowered = rest.toLower();
    if ( kPayloadScheme.match( lowered ).hasMatch()
         || lowered.startsWith( QStringLiteral( "/vsi" ) ) )
        return rest;
    // Skip a Windows drive letter so GPKG:C:/x.gpkg:layer cuts at the LAYER
    // separator, not at "C:".
    int scanFrom = 0;
    if ( rest.size() >= 2 && rest.at( 1 ) == QLatin1Char( ':' )
         && rest.at( 0 ).isLetter() )
        scanFrom = 2;
    const int sep = rest.indexOf( QLatin1Char( ':' ), scanFrom );
    const QString path = sep < 0 ? rest : rest.left( sep );
    return path.isEmpty() ? QString() : path;
}

/// True when a connection-string payload from an UNKNOWN driver is too
/// dangerous to fall back to workspace-relative treatment: absolute/remote/
/// home-anchored, quoted, inline-XML config, database key=value parameters,
/// or a relative path with ".." segments that would climb out of the process
/// CWD (the MCP server chdir()s into the workspace, so a bare relative
/// reference stays contained, but ".." does not).
inline bool unknownDriverPayloadIsDangerous( const QString &rest )
{
    const QString trimmedRest = rest.trimmed();
    if ( trimmedRest.isEmpty() )
        return false;
    const QChar first = trimmedRest.at( 0 );
    if ( first == QLatin1Char( '/' ) || first == QLatin1Char( '\\' )
         || first == QLatin1Char( '~' ) || first == QLatin1Char( '"' )
         || first == QLatin1Char( '<' ) )
        return true;
    const QString lowered = trimmedRest.toLower();
    // R1-F1: any scheme:// payload is a remote reference — an unknown driver
    // carrying one (TileDB:s3://bucket) must not fall back to relative-name
    // treatment.
    static const QRegularExpression kPayloadScheme(
        QStringLiteral( "^[a-z][a-z0-9+.-]+://" ) );
    if ( kPayloadScheme.match( lowered ).hasMatch()
         || lowered.startsWith( QStringLiteral( "/vsi" ) ) )
        return true;
    if ( trimmedRest.size() >= 2 && trimmedRest.at( 1 ) == QLatin1Char( ':' )
         && trimmedRest.at( 0 ).isLetter() )
        return true; // embedded Windows drive
    static const QStringList kvKeys = { QStringLiteral( "host=" ), QStringLiteral( "server=" ),
                                        QStringLiteral( "url=" ), QStringLiteral( "dbname=" ),
                                        QStringLiteral( "database=" ), QStringLiteral( "user=" ),
                                        QStringLiteral( "service=" ), QStringLiteral( "port=" ),
                                        QStringLiteral( "path=" ), QStringLiteral( "driver=" ) };
    for ( const QString &key : kvKeys )
        if ( lowered.startsWith( key ) || lowered.contains( QLatin1Char( ' ' ) + key ) )
            return true;
    const QStringList segments = trimmedRest.split( QLatin1Char( '/' ), Qt::SkipEmptyParts );
    return segments.contains( QStringLiteral( ".." ) );
}

/// Driver-prefix shape test for @p pathValue: a multi-letter identifier
/// directly followed by ':' with no whitespace or slash inside it, and not a
/// single-letter Windows drive. Returns the lower-cased prefix, or an empty
/// string when the value is not driver-shaped.
inline QString gdalDriverPrefixOf( const QString &pathValue )
{
    const int colon = pathValue.indexOf( QLatin1Char( ':' ) );
    if ( colon < 2 )
        return QString(); // no colon, ":x", or single-letter drive "C:…"
    const QString prefix = pathValue.left( colon );
    if ( !prefix.at( 0 ).isLetter() )
        return QString();
    for ( const QChar c : prefix )
    {
        const char latin = c.toLatin1();
        const bool ok = ( c.isLetter() && latin != 0 )
                        || ( c.isDigit() && c.unicode() < 128 )
                        || latin == '_' || latin == '.' || latin == '+' || latin == '-';
        if ( !ok )
            return QString(); // space, slash, quote, non-ASCII… → not a driver token
    }
    return prefix.toLower();
}

/// Scheme classification of a caller-supplied data reference. This runs
/// whether or not a workspace root is configured, so remote access stays
/// default-deny even for surfaces that have no filesystem sandbox.
inline ReferenceVerdict classifyReference( const QString &pathValue, int depth = 0 )
{
    ReferenceVerdict verdict;
    const QString trimmed = pathValue.trimmed();
    const QString lowered = trimmed.toLower();
    const bool vsiCurl = lowered.startsWith( QStringLiteral( "/vsicurl/" ) )
                         || lowered.startsWith( QStringLiteral( "/vsicurl?" ) );
    const bool vsiOther = !vsiCurl && lowered.startsWith( QStringLiteral( "/vsi" ) );
    const bool httpUrl = lowered.startsWith( QStringLiteral( "http://" ) )
                         || lowered.startsWith( QStringLiteral( "https://" ) );
    const bool fileUrl = lowered.startsWith( QStringLiteral( "file:" ) )
                         && ( lowered.startsWith( QStringLiteral( "file://" ) )
                              || lowered.startsWith( QStringLiteral( "file:/" ) ) );
    static const QRegularExpression kScheme( QStringLiteral( "^[a-z][a-z0-9+.-]+://" ) );
    const bool otherScheme = !httpUrl && !fileUrl && kScheme.match( lowered ).hasMatch();

    if ( vsiOther )
    {
        verdict.kind = ReferenceKind::Denied;
        verdict.reason = QStringLiteral( "Only /vsicurl/ remote sources are supported: %1" ).arg( pathValue );
        return verdict;
    }
    if ( otherScheme )
    {
        verdict.kind = ReferenceKind::Denied;
        verdict.reason = QStringLiteral( "Unsupported URL scheme in data reference: %1" ).arg( pathValue );
        return verdict;
    }
    if ( httpUrl || vsiCurl )
    {
        if ( !::envFlagEnabled( "SICNU_MCP_ALLOW_REMOTE" ) )
        {
            verdict.kind = ReferenceKind::Denied;
            verdict.reason = QStringLiteral( "Remote data references are disabled "
                                             "(set SICNU_MCP_ALLOW_REMOTE=1): %1" )
                               .arg( pathValue );
            return verdict;
        }
        verdict.kind = ReferenceKind::RemoteAllowed;
        return verdict;
    }
    if ( fileUrl )
    {
        verdict.kind = ReferenceKind::FileUrl;
        QString local = QUrl( trimmed ).toLocalFile();
        if ( local.isEmpty() )
            local = trimmed.mid( trimmed.indexOf( QLatin1Char( ':' ) ) + 1 );
        verdict.localPath = local;
        return verdict;
    }
    // #1704: GDAL/OGR driver-prefixed connection strings (CSV:, GPKG:, PG:,
    // WMS:, …) carry no "://", so none of the scheme checks above match and
    // the string used to fall through to LocalPath — where joining it onto
    // the workspace root made it look "inside" while GDAL opened the REAL
    // (workspace-external) target named after the prefix. Known file-backed
    // drivers have their embedded path re-contained; network drivers follow
    // the remote default-deny; unknown drivers are denied when their payload
    // references anything outside the workspace-anchored relative namespace.
    const QString driver = gdalDriverPrefixOf( trimmed );
    if ( !driver.isEmpty() )
    {
        const QString rest = trimmed.mid( trimmed.indexOf( QLatin1Char( ':' ) ) + 1 );
        if ( isFileBackedGdalDriver( driver ) )
        {
            // R1-F2: a payload that is ITSELF driver-shaped (CSV:GPKG:/abs,
            // CSV:s3://bucket) is a nested connection string — classify it
            // recursively instead of splitting it at the first ':' and
            // "verifying" a fragment GDAL will never open. R2-F2: the
            // recursion strips exactly one prefix per level but a crafted
            // "KML:"*100000 reference (a tenth of the 4 MiB frame cap)
            // overflowed the stack — cap the depth and fail closed beyond
            // it; legitimate nesting never exceeds a handful of levels.
            if ( !gdalDriverPrefixOf( rest ).isEmpty() )
            {
                if ( depth >= 8 )
                {
                    verdict.kind = ReferenceKind::Denied;
                    verdict.reason = QStringLiteral( "GDAL connection string nests more than "
                                                     "8 driver prefixes; rejecting: %1" )
                                         .arg( pathValue );
                    return verdict;
                }
                return classifyReference( rest, depth + 1 );
            }
            const QString embedded = embeddedDriverPath( rest );
            if ( embedded.isEmpty() )
            {
                verdict.kind = ReferenceKind::Denied;
                verdict.reason = QStringLiteral( "GDAL connection string '%1:' carries no "
                                                 "extractable path; rejecting" )
                                     .arg( pathValue );
                return verdict;
            }
            verdict.kind = ReferenceKind::DriverPath;
            verdict.localPath = embedded;
            return verdict;
        }
        if ( isNetworkGdalDriver( driver ) )
        {
            if ( !::envFlagEnabled( "SICNU_MCP_ALLOW_REMOTE" ) )
            {
                verdict.kind = ReferenceKind::Denied;
                verdict.reason = QStringLiteral( "Remote GDAL driver connection is disabled "
                                                 "(set SICNU_MCP_ALLOW_REMOTE=1): %1" )
                                     .arg( pathValue );
                return verdict;
            }
            verdict.kind = ReferenceKind::RemoteAllowed;
            return verdict;
        }
        if ( unknownDriverPayloadIsDangerous( rest ) )
        {
            verdict.kind = ReferenceKind::Denied;
            verdict.reason = QStringLiteral( "Unknown GDAL driver prefix in data reference "
                                             "(allowed drivers: file-backed with an in-workspace "
                                             "path): %1" )
                                 .arg( pathValue );
            return verdict;
        }
        // Benign unknown "word:fragment" — treat as the relative path it
        // always was (workspace files legitimately contain ':' in POSIX
        // names); it still resolves through the containment joiner below.
    }
    return verdict;
}

/// The containment decision. Returns true when @p pathValue must be
/// REJECTED (outside @p workspaceRoot, or a denied reference) and fills
/// @p detail. @p label names the controlling setting in messages.
/// An empty @p workspaceRoot means "no filesystem sandbox": only the URL
/// scheme policy is applied.
inline bool pathOutsideWorkspace( const QString &pathValue, const QString &workspaceRoot,
                                  QString *detail,
                                  const QString &label = QStringLiteral( "SICNU_MCP_WORKSPACE" ) )
{
    if ( pathValue.isEmpty() )
        return false;

    const ReferenceVerdict verdict = classifyReference( pathValue );
    switch ( verdict.kind )
    {
        case ReferenceKind::Denied:
            if ( detail )
                *detail = verdict.reason;
            return true;
        case ReferenceKind::RemoteAllowed:
            return false; // scheme-validated remote reference, not a workspace path
        case ReferenceKind::FileUrl:
            if ( pathOutsideWorkspace( verdict.localPath, workspaceRoot, detail, label ) )
            {
                if ( detail )
                    *detail = QStringLiteral( "Path outside %1: %2" ).arg( label, pathValue );
                return true;
            }
            return false;
        case ReferenceKind::DriverPath:
            // #1704: re-contain the embedded path of a file-backed GDAL
            // connection string with the SAME policy (scheme checks,
            // canonicalization, workspace join) as a bare path.
            if ( pathOutsideWorkspace( verdict.localPath, workspaceRoot, detail, label ) )
            {
                if ( detail )
                    *detail = QStringLiteral( "Path outside %1 (GDAL connection string): %2" )
                                  .arg( label, pathValue );
                return true;
            }
            return false;
        case ReferenceKind::LocalPath:
            break;
    }

    if ( workspaceRoot.trimmed().isEmpty() )
        return false;

    const QString rootCanon = canonicalWorkspaceRoot( workspaceRoot );
    // NOTE: never QFileInfo::absoluteFilePath()/QDir::cleanPath() here — both
    // collapse "link/.." lexically, while the OS resolves it through the link.
    const QString resolved = weaklyCanonicalPath(
        resolveAgainstWorkspace( pathValue, rootCanon.isEmpty() ? workspaceRoot : rootCanon ) );
    // Fail closed: an unresolvable root or path is never "inside".
    if ( !rootCanon.isEmpty() && canonicalPathWithin( resolved, rootCanon ) )
        return false;

    if ( detail )
    {
        if ( rootCanon.isEmpty() )
            *detail = QStringLiteral( "%1 cannot be resolved (%2); rejecting path: %3" )
                        .arg( label, workspaceRoot, pathValue );
        else
            *detail = QStringLiteral( "Path outside %1: %2" ).arg( label, pathValue );
    }
    return true;
}

/// Fallback sandbox root used when the MCP server would otherwise sandbox
/// against the filesystem root or the user's home directory (MCP clients
/// such as desktop apps commonly spawn servers with CWD "/" or $HOME).
inline QString defaultMcpFallbackWorkspace()
{
    return QDir::home().filePath( QStringLiteral( ".exp-rs/workspace" ) );
}

/// The workspace root the MCP surface sandboxes against (P1-1: default-deny).
///   1. SICNU_MCP_WORKSPACE when set (an explicit "/" is the documented way
///      to widen the sandbox to the whole filesystem);
///   2. otherwise the process working directory — the project directory for
///      the Pi bridge, which spawns the server from the project root;
///   3. unless that CWD is the filesystem root or $HOME, in which case the
///      dedicated ~/.exp-rs/workspace directory (created on demand).
/// Never returns an empty string: there is no implicit "unsandboxed" mode.
inline QString effectiveMcpWorkspaceRoot()
{
    const QString configured = qEnvironmentVariable( "SICNU_MCP_WORKSPACE" ).trimmed();
    if ( !configured.isEmpty() )
        return configured;

    const QString cwd = QDir::current().canonicalPath();
    const QString home = QDir::home().canonicalPath();
    const bool cwdUnsafe = cwd.isEmpty() || QDir( cwd ).isRoot()
                           || ( !home.isEmpty() && cwd.compare( home, pathCaseSensitivity() ) == 0 );
    if ( !cwdUnsafe )
        return cwd;

    const QString fallback = defaultMcpFallbackWorkspace();
    QDir().mkpath( fallback );
    const QString canon = QDir( fallback ).canonicalPath();
    return canon.isEmpty() ? fallback : canon;
}

} // namespace sicnu::agent::tool_catalog::containment
