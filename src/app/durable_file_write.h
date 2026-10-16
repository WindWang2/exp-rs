// src/app/durable_file_write.h — shared crash-or-valid file publish authority
// (R14-D WP-B; issue #1487 remainder: destination permissions clobbered, symlink
// target replaced instead of followed, Windows non-atomic backup-then-rename —
// review threads T-d / T-e / T-a-Windows against the #1501 pipeline fix).
//
// CONTRACT (the oracle every call site buys): after writeBytesDurably() returns,
// the target is either COMPLETELY the new bytes (file fsynced, rename published,
// containing directory fsynced on POSIX) or COMPLETELY its previous content. A
// crash, ENOSPC or power loss mid-publish can never leave a truncated/partial
// file at the target path, and no temp litter survives either outcome.
//
// Mechanics:
//   1. Symbolic links in the target path are RESOLVED (bounded to 8 hops; a
//      longer chain or a loop is an honest failure). The publish therefore
//      replaces the REFERENT of a symlink, never the link itself (T-e). Windows
//      junctions are handled by the same path when QFileInfo reports them as
//      symbolic links (Qt 6 treats junctions as symlinks for isSymbolicLink()).
//   2. The destination's existing permissions are captured before writing and
//      re-applied to the published file (T-d: the old pipeline lane published a
//      mkstemp 0600 inode over a 0644 destination). A NEW file is created with
//      0644-equivalent (owner read/write, group/other read) — deliberately NOT
//      umask-relative so the behavior is deterministic and assertable.
//   3. The payload lands through QSaveFile: temp file in the TARGET's directory,
//      explicit fd-level fsync before commit on POSIX, then commit()'s atomic
//      overwrite-rename — a single publish syscall on both POSIX and Windows
//      (QSaveFile uses ReplaceFile/MoveFileEx semantics on Windows, which fixes
//      the old backup-then-rename target-absent window of T-a-Windows).
//      Verified against the installed Qt 6.12 (QSaveFile::commit() flushes,
//      syncs to disk, then renames); the explicit fsync below is kept as a
//      belt-and-braces guarantee in the spirit of #1501, whose audits distrusted
//      QSaveFile-only lanes ("rename-atomic but never fsynced").
//   4. POSIX: after a successful commit the containing DIRECTORY is fsynced so
//      the rename itself cannot be lost to power failure (mirrors the #1501
//      pipeline dir-fsync, best-effort: a dir-fsync failure is not reported as
//      a write failure — the file content is already durable at that point).
//      Windows: no directory fsync (the platform offers no portable equivalent;
//      documented residual).
//   5. Any failure returns {false, descriptive error incl. target path}; the
//      target file is left untouched (QSaveFile cancels and removes the temp).
//      A mid-write ENOSPC injection is NOT simulated in tests; the short-write
//      branch is covered by construction (every write is size-checked) and by
//      the failure paths that are injectable (read-only dir, directory target,
//      symlink loop) — see tests/test_r14_D_durable_writes.cpp.
//
// Dependencies: Qt6 Core only. The implementation is HEADER-ONLY INLINE on
// purpose: src/app/georeferencer/qgsgcplist.cpp (compiled into the
// qgis_app_georef static library) routes through this helper, and ~28 test
// targets link qgis_app_georef WITHOUT sicnu_geo_rs_shell — an out-of-line
// definition would leave sicnu::writeBytesDurably unresolved for them (GNU ld
// does not re-scan archives that precede the referencing archive on the link
// line, and georef precedes shell in dependency order). Inline linkage makes
// the helper position-independent. durable_file_write.cpp stays as the shell's
// self-containment/ODR guard TU.
#ifndef SICNU_APP_DURABLE_FILE_WRITE_H
#define SICNU_APP_DURABLE_FILE_WRITE_H

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QObject>
#include <QSaveFile>
#include <QString>

#include <cerrno>
#include <cstring>

#if defined( Q_OS_UNIX )
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sicnu {

/// Result of a durable write: ok == true on success (error empty), otherwise
/// error carries a human-readable, target-path-qualified reason.
struct DurableWriteResult
{
  bool ok = false;
  QString error;
};

/// Durably publishes @p bytes to @p targetPath (crash-or-valid oracle; see the
/// file header). Thread-safe as a pure file operation; no QObject parent.
inline DurableWriteResult writeBytesDurably( const QString &targetPath, const QByteArray &bytes )
{
  if ( targetPath.isEmpty() )
    return { false, QObject::tr( "Target file path is empty." ) };

  // --- 1. Resolve symbolic links (bounded; loop or over-deep chain fails). ---
  QString resolved = QFileInfo( targetPath ).absoluteFilePath();
  for ( int hop = 0; hop <= 8; ++hop )
  {
    QFileInfo info( resolved );
    if ( !info.isSymbolicLink() )
      break;
    if ( hop == 8 )
      return { false, QObject::tr( "Symbolic link chain is too deep (more than 8 hops): %1" )
                             .arg( targetPath ) };
    // symLinkTarget() returns the referent resolved against the link's own
    // directory (absolute); it does not require the referent to exist.
    const QString referent = info.symLinkTarget();
    if ( referent.isEmpty() || referent == resolved )
      return { false, QObject::tr( "Cannot resolve symbolic link target for %1 (at %2)" )
                             .arg( targetPath, resolved ) };
    resolved = referent;
  }

  const QFileInfo resolvedInfo( resolved );
  if ( resolvedInfo.isDir() )
    return { false, QObject::tr( "Target is a directory: %1" ).arg( targetPath ) };

  const QDir parentDir( resolvedInfo.absolutePath() );
  if ( !parentDir.exists() )
    return { false, QObject::tr( "Directory does not exist: %1" ).arg( parentDir.absolutePath() ) };

  // --- 2. Capture destination permissions (T-d). Existing file → keep its
  //        mode across the publish; new file → 0644-equivalent, NOT umask-
  //        relative (documented deterministic default). ---
  const QFileDevice::Permissions permissions = resolvedInfo.exists()
    ? QFile::permissions( resolved )
    : ( QFileDevice::ReadOwner | QFileDevice::WriteOwner
        | QFileDevice::ReadGroup | QFileDevice::ReadOther );

  // --- 3. Write via QSaveFile: temp in the target's directory, size-checked
  //        writes, explicit POSIX fsync, single-syscall atomic publish. ---
  QSaveFile file( resolved );
  if ( !file.open( QIODevice::WriteOnly ) )
    return { false, QObject::tr( "Cannot create temporary file: %1" ).arg( file.errorString() ) };

  // Applies to the temp file while open; the rename publish preserves the mode
  // (POSIX rename never rewrites modes; QSaveFile re-applies on Windows).
  if ( !file.setPermissions( permissions ) )
    return { false, QObject::tr( "Cannot set permissions on temporary file for: %1" ).arg( targetPath ) };

  const qint64 written = file.write( bytes );
  if ( written != static_cast<qint64>( bytes.size() ) )
    return { false, QObject::tr( "Failed to write complete data: %1" ).arg( file.errorString() ) };

  if ( !file.flush() )
    return { false, QObject::tr( "Failed to flush data: %1" ).arg( file.errorString() ) };

#if defined( Q_OS_UNIX )
  // Explicit fd-level fsync BEFORE commit (belt-and-braces; see file header).
  if ( ::fsync( file.handle() ) != 0 )
    return { false, QObject::tr( "Failed to fsync temporary file: %1" )
                             .arg( QString::fromLocal8Bit( std::strerror( errno ) ) ) };
#endif

  if ( !file.commit() )
    return { false, QObject::tr( "Failed to atomically publish file: %1" ).arg( file.errorString() ) };

  // --- 4. POSIX: best-effort directory fsync so the rename itself survives
  //        power loss (mirrors the #1501 pipeline dir-fsync; a failure here is
  //        not a write failure — the content is already durable). ---
#if defined( Q_OS_UNIX )
  const QByteArray parentDirBytes = QFile::encodeName( resolvedInfo.absolutePath() );
#if defined( O_DIRECTORY )
  const int dirFd = ::open( parentDirBytes.constData(), O_RDONLY | O_DIRECTORY );
#else
  const int dirFd = ::open( parentDirBytes.constData(), O_RDONLY );
#endif
  if ( dirFd >= 0 )
  {
    ::fsync( dirFd );
    ::close( dirFd );
  }
#endif

  return { true, QString() };
}

} // namespace sicnu

#endif // SICNU_APP_DURABLE_FILE_WRITE_H
