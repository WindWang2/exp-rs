// src/workflow/path_containment.h — run-directory containment helpers.
#pragma once

//
// Shared by the IR2 registry node executor (artifact authorship,
// F-1032-P1-artifact) and the PipelineRunCoordinator resume path (stale
// artifact references, #1056). An artifact is trustworthy only when it
// resolves INSIDE the run directory that produced it.
//
// Header-only, Qt Core only — both consumers are compiled standalone in
// several test targets, so this file must stay free of link dependencies.
//
// Review note (F-1032-P1 residual, accepted): the LEXICAL pre-execute check
// cannot see a symlink that already exists inside the run directory and
// points outside, so such a path passes the pre-check and the operator may
// write outside before the post-execute RESOLVED check refuses publication.
// Publication — not the write — is the boundary that creates a false
// success, and it is enforced fail-closed.
//

#include <QDir>
#include <QFileInfo>
#include <QString>

namespace sicnu::workflow {
namespace path_containment {

/// Absolute, cleaned form of @p candidate (relative candidates resolve
/// against @p runDir, matching how a workflow document names outputs).
/// Symlinks are NOT resolved.
inline QString absolutePathFor( const QString &candidate, const QDir &runDir )
{
    QString absolute = candidate;
    if ( QDir::isRelativePath( absolute ) )
        absolute = runDir.filePath( absolute );
    return QDir::cleanPath( QFileInfo( absolute ).absoluteFilePath() );
}

/// Lexical containment: @p candidate, cleaned, must stay under the run
/// directory's CLEANED ABSOLUTE path. Both sides use the same (non-canonical)
/// spelling so this is independent of symlinked ancestors (e.g. macOS
/// /var -> /private/var). No filesystem access to the candidate — usable
/// before an operator has run (the artifact may not exist yet).
inline bool lexicallyInsideDirectory( const QString &candidate, const QDir &runDir )
{
    if ( candidate.trimmed().isEmpty() )
        return false;
    const QString cleanRunDir = QDir::cleanPath( runDir.absolutePath() );
    if ( cleanRunDir.isEmpty() )
        return false;

    const QString absolute = absolutePathFor( candidate, runDir );
    if ( absolute == cleanRunDir )
        return false; // the run directory itself is not an artifact
    return absolute.startsWith( cleanRunDir + QLatin1Char( '/' ) );
}

/// Resolved containment: additionally proves an EXISTING artifact does not
/// reach outside the run directory through a symlink. Both sides are
/// canonicalized, so the comparison stays spelling-independent.
inline bool resolvedInsideDirectory( const QString &candidate, const QDir &runDir )
{
    if ( !lexicallyInsideDirectory( candidate, runDir ) )
        return false;
    const QString canonicalRunDir = QFileInfo( runDir.absolutePath() ).canonicalFilePath();
    if ( canonicalRunDir.isEmpty() )
        return false;
    const QString canonical = QFileInfo( absolutePathFor( candidate, runDir ) ).canonicalFilePath();
    if ( canonical.isEmpty() || canonical == canonicalRunDir )
        return false;
    return canonical.startsWith( canonicalRunDir + QLatin1Char( '/' ) );
}

} // namespace path_containment
} // namespace sicnu::workflow
