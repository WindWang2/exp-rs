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
#include <QString>
#include <QStringList>
#include <QUrl>

#include <utility>

namespace sicnu::agent::tool_catalog::containment
{

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
            QString base = info.canonicalFilePath();
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
    Denied         ///< disallowed scheme / vsi prefix / remote disabled
};

struct ReferenceVerdict
{
    ReferenceKind kind = ReferenceKind::LocalPath;
    QString localPath; ///< for FileUrl
    QString reason;    ///< for Denied
};

/// Scheme classification of a caller-supplied data reference. This runs
/// whether or not a workspace root is configured, so remote access stays
/// default-deny even for surfaces that have no filesystem sandbox.
inline ReferenceVerdict classifyReference( const QString &pathValue )
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
