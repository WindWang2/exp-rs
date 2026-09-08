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
/// "<name>.<pid>.<counter>.tmp".
std::string stagedPathFor( const std::string &targetPath );

/// Flushes file contents + metadata to stable storage. Throws GeoError(IoError)
/// when the file cannot be opened or flushed.
void fsyncFile( const std::string &path );

/// Publishes a staged file to its target.
/// POSIX: rename() (atomic replacement).
/// Windows: ReplaceFileW when the target exists (transactional with backup
/// metadata), MoveFileExW(MOVEFILE_REPLACE_EXISTING) otherwise. When the
/// target is locked the function fails with GeoError(IoError) — callers roll
/// back (staged file is left for `discardStaged` by the caller).
void publishStagedFile( const std::string &stagedPath, const std::string &targetPath );

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

/// Runs writer(stagedPath), then fsync + publish. On any exception the staged
/// file is discarded and the exception rethrown — target stays untouched.
void writeFileAtomic( const std::string &targetPath, const std::function<void( const std::string &stagedPath )> &writer );

/// File size in bytes; 0 when unavailable.
std::uintmax_t fileSize( const std::string &path );

} // namespace sicnu::geo::atomic_fs

#endif // SICNU_GEOSPATIAL_ATOMIC_FS_H
