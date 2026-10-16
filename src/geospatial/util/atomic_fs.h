/***************************************************************************
  geospatial/util/atomic_fs.h
  Geospatial I/O Foundation 4.0 — atomic filesystem publication primitives.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Contract (temp → write → flush → validate → publish):
  * staging always happens in the TARGET directory (rename stays on one volume)
  * staged files are fsynced before publish (crash leaves old or new, not junk)
  * single files publish through an atomic-or-best-effort rename; multi-file
    dataset groups publish main-file-LAST (main file presence = complete group)
  * every failure path has a cleanup call; nothing half-published is left
    silently behind
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_ATOMIC_FS_H
#define SICNU_GEOSPATIAL_ATOMIC_FS_H

#include "geospatial/common.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::geo::atomic_fs
{

/// Known sibling sidecars for a dataset main file (shapefile family, ENVI,
/// world/PAM files). Used for group moves and cleanup. Returned without the
/// main path itself.
std::vector<std::string> sidecarsFor( const std::string &mainPath );

/// True when the path exists as a regular file (no exception on weird input).
bool fileExists( const std::string &path );

/// Generates a unique staging path in the same directory as target:
/// "<name>.<pid>.<counter>.<rand>.tmp<ext>", claimed with O_EXCL / CREATE_NEW.
std::string stagedPathFor( const std::string &targetPath );

/// Generates the same unique staging-path shape WITHOUT creating the file.
/// For creators that hand the path to a library which creates the dataset
/// itself and refuses an existing target (GDALCreate / GDALTranslate /
/// GDALWarp — most GDAL/OGR drivers fail closed on an existing file, so the
/// O_EXCL-pre-created empty file of stagedPathFor makes them fail before
/// writing anything). Uniqueness comes from pid + counter + random_device
/// entropy; a same-name collision is then resolved by whichever creator
/// gets there first — the loser's create fails and the error surfaces
/// through the caller's cleanup path, never a silent cross-writer merge.
std::string reservedStagedPathFor( const std::string &targetPath );

/// Flushes file contents + metadata to stable storage. Throws GeoError(IoError)
/// when the file cannot be opened or flushed.
void fsyncFile( const std::string &path );

/// Publishes a staged file to its target.
/// POSIX: rename(2) (atomic replacement; a read-only TARGET is replaced —
/// the gate is the directory's write permission, and Windows clears a stale
/// READONLY attribute to answer with the same contract).
/// Windows: ReplaceFileW when the target exists (transactional with backup
/// metadata), MoveFileExW(MOVEFILE_REPLACE_EXISTING) otherwise; its
/// durability rides MOVEFILE_WRITE_THROUGH inside that rename. When the
/// target is locked the function fails with GeoError(IoError) — callers roll
/// back (staged file is left for `discardStaged` by the caller). The POSIX
/// rename and the POSIX-lane post-publish directory sync ride
/// platform/portable.h, the single authority for these syscalls; the
/// Windows rename branches remain here (ReplaceFileW has no write-through
/// flag on its fast path — pre-existing, unchanged).
void publishStagedFile( const std::string &stagedPath, const std::string &targetPath );

/// Best-effort rename that replaces an existing destination.
/// Windows: MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) —
/// a cross-VOLUME move silently degrades to copy+delete (NOT atomic);
/// POSIX: rename(2) fails false across volumes (EXDEV). Same-volume
/// behavior is atomic-or-false on both platforms.
/// Returns false on any failure (locked source/target, missing source, etc.).
/// Prefer publishStagedFile for publication paths that must fail closed with
/// GeoError — this helper is for GC / quarantine / soft paths, and it is
/// also the backup-move primitive behind publishStagedGroup /
/// publishStagedMembers (MoveFileExW replaces an existing backup name the
/// way publishStagedFile does, #1178 — fs::rename refuses that).
bool renameReplaceQuiet( const std::string &from, const std::string &to );

/// Removes a staged/stray file; missing files are not an error. Returns false
/// when the file exists but could not be removed (locked).
bool removeFileQuiet( const std::string &path );

/// Removes a staged file plus any sidecar siblings that were created for it.
void discardStaged( const std::string &stagedMainPath );

/// Group publish for multi-file dataset groups (e.g. shapefile .shp/.shx/.dbf/
/// .prj/.cpg): copies the staged group to its final names, sidecars FIRST,
/// main file LAST (presence of the main file is the completeness marker).
/// Existing targets are replaced. On failure, already-published targets are
/// restored/removed per the backup set and GeoError carries the failed name.
void publishStagedGroup( const std::string &stagedMainPath, const std::string &targetMainPath );

/// Publishes an ordered list of (staged → target) pairs. Callers put dependents
/// FIRST and the completeness-marker (main) LAST — same contract as
/// publishStagedGroup. Existing targets are moved to ".bak" before replacement;
/// on failure already-published members are removed and backups restored.
/// Empty `members` is a no-op. Missing staged files are skipped (except that
/// an empty list after filtering is still success).
void publishStagedMembers( const std::vector<std::pair<std::string, std::string>> &members );

/// Runs writer(stagedPath), then fsync + publish. On any exception the staged
/// file is discarded and the exception rethrown — target stays untouched.
void writeFileAtomic( const std::string &targetPath, const std::function<void( const std::string &stagedPath )> &writer );

/// File size in bytes; 0 when unavailable.
std::uintmax_t fileSize( const std::string &path );

/// Checks whether an uncommitted journal or committed marker exists for the given main target.
bool hasPendingJournal( const std::string &targetMainPath );

/// R15-B (B13): true only for an UNCOMMITTED journal (".publish-journal" /
/// ".sicnu-journal" without the ".committed" suffix). Such a group may be a
/// mix of generations (a crash between sidecar and main renames); readers
/// must refuse it instead of serving it. A committed marker means the new
/// group is complete (only backup sweeping was interrupted).
bool hasUncommittedPublishJournal( const std::string &targetMainPath );

/// R15-B (B13): reader-side gate. Throws GeoError(IoError) when @p localPath
/// carries an uncommitted publish journal. Side-effect free: recovery is the
/// writer's (publishStagedGroup/publishStagedMembers auto-recover) or an
/// explicit recoverDatasetGroup() call — a reader must never roll back a
/// publish that another thread/process may still be performing. /vsi*
/// and remote paths are ignored.
void throwIfInterruptedPublish( const std::string &localPath );

/// R15-B (item 7): non-throwing form of the same predicate (same /vsi* and
/// remote filter). Readers re-check it AFTER their GDALOpenEx so a publish
/// that started while the dataset was being opened is refused too — this
/// narrows the check→open window to a publish that both starts AND commits
/// inside one open call (not closable without a reader/writer lock).
bool interruptedPublishPending( const std::string &localPath );

/// Throws the interrupted-publish GeoError unconditionally (used after a
/// post-open re-check; never returns).
[[noreturn]] void throwInterruptedPublish( const std::string &localPath );

/// Recovers an interrupted multi-file publication for targetMainPath.
/// If an uncommitted journal is found, rolls back published targets and restores backups.
/// If a committed marker is found, cleans up leftover backups.
/// Returns true if recovery was performed, false if no journal was present.
bool recoverDatasetGroup( const std::string &targetMainPath );

/// Compatibility alias for recoverDatasetGroup.
inline bool recoverInterruptedPublish( const std::string &targetMainPath ) { return recoverDatasetGroup( targetMainPath ); }

/// Scans a directory for any .publish-journal or .publish-journal.committed files
/// and recovers all found groups. Returns the count of recovered groups.
std::size_t recoverDirectoryJournals( const std::string &directoryPath );

} // namespace sicnu::geo::atomic_fs

#if __has_include(<QString>)
#include <QString>
namespace sicnu::geo::atomic_fs
{
inline bool hasPendingJournal( const QString &targetMainPath ) { return hasPendingJournal( targetMainPath.toStdString() ); }
inline bool recoverDatasetGroup( const QString &targetMainPath ) { return recoverDatasetGroup( targetMainPath.toStdString() ); }
inline bool recoverInterruptedPublish( const QString &targetMainPath ) { return recoverDatasetGroup( targetMainPath ); }
inline std::size_t recoverDirectoryJournals( const QString &directoryPath ) { return recoverDirectoryJournals( directoryPath.toStdString() ); }
} // namespace sicnu::geo::atomic_fs
#endif


#endif // SICNU_GEOSPATIAL_ATOMIC_FS_H
