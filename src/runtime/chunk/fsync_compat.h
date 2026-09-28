// fsync_compat.h — portable file/directory durability for the chunk family.
//
// Thin adapter over platform/portable.h, the repo's single authority for the
// claim/fsync syscalls: this header keeps the chunk family's own error
// contract (file flush failure throws std::runtime_error; directory flush is
// best-effort) without hand-rolling a third copy of the per-platform
// branches. File flush failure throws; directory flush is best-effort
// (BACKUP_SEMANTICS open can fail on some volumes). Real durability on both
// platforms — a silent no-op previously allowed the journal to outlive
// unflushed tile bytes (#1228 / #1186 item 33).
//
// Semantic alignment with the atomic publish lane (R5 core/platform): a file
// is opened FOR WRITING (O_WRONLY / GENERIC_WRITE), so a durability claim on
// a file that cannot be written fails closed exactly like
// geospatial/util/atomic_fs::fsyncFile — an O_RDONLY fsync used to "succeed"
// here on read-only files while the atomic lane refused the same state.
#pragma once

#include <string>
#include <stdexcept>

#include "platform/portable.h"

namespace sicnu::runtime::chunk
{

inline void fsyncPathCompat( const std::string &path, bool directory )
{
    if ( directory )
    {
        sicnu::portable::syncDirectoryBestEffortUtf8( path, /*pathIsDirectory=*/true );
        return;
    }
    sicnu::portable::SyncFailure failure = sicnu::portable::SyncFailure::OpenFailed;
    if ( sicnu::portable::syncFileUtf8( path, &failure ) )
        return;
    throw std::runtime_error( failure == sicnu::portable::SyncFailure::OpenFailed
                                ? "fsync: cannot open " + path
                                : "fsync failed for " + path );
}

/// Best-effort wrapper: never throws (legacy call sites that tolerate miss).
inline void fsyncPathBestEffort( const std::string &path, bool directory = false )
{
    try
    {
        fsyncPathCompat( path, directory );
    }
    catch ( ... )
    {
    }
}

} // namespace sicnu::runtime::chunk
