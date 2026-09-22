/***************************************************************************
 * exprs/plugin_snapshot.cpp
 ***************************************************************************/
#include "exprs/plugin_snapshot.h"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include <json/json.h>

#ifndef _WIN32
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

namespace exprs {

long snapshotOwnerPid()
{
#ifdef _WIN32
    return static_cast<long>( ::GetCurrentProcessId() );
#else
    return static_cast<long>( ::getpid() );
#endif
}

namespace {

/// Best-effort "is this pid a live process" probe: the sweep must not
/// reclaim same-named residue while its OWNING process (a concurrent app
/// or CLI instance sharing the temp dir) is still running — only a dead
/// pid's artifacts are crash residue. Conservative on failure (treated as
/// alive → residue kept).
bool pidAlive( long pid )
{
    // A pid outside int's range cannot name a live process on any
    // platform — the static_cast below would TRUNCATE (LONG_MAX -> -1 ->
    // kill() reports EPERM), so bound first and treat it as dead.
    if ( pid <= 0
         || static_cast<unsigned long>( pid )
                > static_cast<unsigned long>(
                    std::numeric_limits<int>::max() ) )
        return false;
#ifdef _WIN32
    HANDLE handle = ::OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                   static_cast<DWORD>( pid ) );
    if ( !handle )
        // Access denied = the process EXISTS but belongs to a higher
        // privilege level — it is alive, not collectible residue.
        return ::GetLastError() == ERROR_ACCESS_DENIED;
    ::CloseHandle( handle );
    return true;
#else
    if ( ::kill( static_cast<pid_t>( pid ), 0 ) == 0 )
        return true;
    return errno == EPERM; // exists but owned by another user
#endif
}

/// The snapshot root is a per-user trust boundary: on a shared temp dir a
/// root pre-created by another user must never receive our bytes, be
/// trusted as a restore source, or be swept by us. Windows relies on the
/// per-user %TEMP% layout, where this class does not apply.
bool dirOwnedByUs( const std::filesystem::path &dir )
{
#ifdef _WIN32
    ( void )dir;
    return true;
#else
    struct stat st {};
    return ::lstat( dir.c_str(), &st ) == 0 && st.st_uid == ::geteuid();
#endif
}

} // namespace

namespace {

namespace fs = std::filesystem;

uint64_t envUint64( const char *name, uint64_t fallback, uint64_t floor )
{
    const char *raw = std::getenv( name );
    if ( !raw || !*raw )
        return fallback;
    // strtoull silently wraps a leading '-' into a huge value — an env set
    // to "-1" would otherwise MAX OUT the byte budget instead of failing.
    if ( raw[0] == '-' || raw[0] == '+' )
        return fallback;
    const unsigned long long parsed = std::strtoull( raw, nullptr, 10 );
    if ( parsed == 0 )
        return fallback;
    return std::max<uint64_t>( parsed, floor );
}

bool allSafeNameChars( const std::string &name )
{
    return !name.empty()
           && std::all_of( name.begin(), name.end(), []( char c ) {
                  return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' )
                         || ( c >= '0' && c <= '9' ) || c == '.' || c == '_' || c == '-' || c == '~';
              } );
}

/// Splits "<base><marker><digits...>" names ("~staging-123-4" and
/// "~old-123" both match: the tail is digits/dashes only). Used for the
/// publish-ladder residue classes; anything else is Foreign.
bool splitSuffix( const std::string &name, const char *marker,
                  std::string &base )
{
    const std::string needle = marker;
    const size_t at = name.rfind( needle );
    if ( at == std::string::npos || at == 0 )
        return false;
    const std::string tail = name.substr( at + needle.size() );
    if ( tail.empty()
         || !std::all_of( tail.begin(), tail.end(), []( char c ) {
                return ( c >= '0' && c <= '9' ) || c == '-';
            } ) )
        return false;
    base = name.substr( 0, at );
    return true;
}

/// Leading digits of a residue tail = the owning process id
/// ("123-4" -> 123, "123" -> 123). -1 when malformed.
long tailOwnerPid( const std::string &name, const char *marker )
{
    const size_t at = name.rfind( marker );
    if ( at == std::string::npos )
        return -1;
    const std::string tail = name.substr( at + std::string( marker ).size() );
    size_t digits = 0;
    while ( digits < tail.size() && tail[digits] >= '0' && tail[digits] <= '9' )
        ++digits;
    if ( digits == 0 )
        return -1;
    return std::strtol( tail.substr( 0, digits ).c_str(), nullptr, 10 );
}

/// Digits after the LAST '-' ("upgrade-org.foo-4242" -> 4242). -1 when the
/// tail is not all digits or no '-' exists.
long trailingPid( const std::string &name )
{
    const size_t dash = name.rfind( '-' );
    if ( dash == std::string::npos || dash + 1 >= name.size() )
        return -1;
    const std::string tail = name.substr( dash + 1 );
    if ( !std::all_of( tail.begin(), tail.end(), []( char c ) {
             return c >= '0' && c <= '9';
         } ) )
        return -1;
    return std::strtol( tail.c_str(), nullptr, 10 );
}

/// Monotonic instance tag so two captures into the SAME dest (an async job
/// superseded mid-flight by a newer one) never share a staging or park dir.
std::atomic<long> gCaptureSeq{ 0 };

/// One mutex per snapshot destination: captures into the same dest run
/// strictly serially (walk AND publish), so a superseded job that was
/// mid-publish can never land stale bytes after the newer capture's swap.
/// The lock map is keyed by the literal dest string — callers always pass
/// the deterministic last-good-/upgrade- paths, so keys stay canonical.
std::mutex gDestLocksMutex;
// Weak pointers: a finished capture drops the last strong ref and the
// mutex frees itself — the map cannot grow past destinations that have a
// live or queued capture. Expired entries are pruned on every lookup.
std::map<std::string, std::weak_ptr<std::mutex>> gDestLocks;

std::shared_ptr<std::mutex> destLockFor( const std::string &destDir )
{
    std::lock_guard<std::mutex> lock( gDestLocksMutex );
    for ( auto it = gDestLocks.begin(); it != gDestLocks.end(); )
        it = it->second.expired() ? gDestLocks.erase( it ) : std::next( it );
    std::shared_ptr<std::mutex> mutex = gDestLocks[ destDir ].lock();
    if ( !mutex )
    {
        mutex = std::make_shared<std::mutex>();
        gDestLocks[ destDir ] = mutex;
    }
    return mutex;
}

} // namespace

PluginSnapshotBudget PluginSnapshotBudget::fromEnvironment()
{
    // Byte budgets get a ceiling too: a hostile/absurd env must not turn
    // the bound into an effective no-snapshot. 16 GiB is far past any
    // legitimate plugin tree while still a finite bound.
    constexpr uint64_t kByteCeiling = 16ull * 1024ull * 1024ull * 1024ull;
    PluginSnapshotBudget budget;
    budget.maxBytes = std::min<uint64_t>(
        envUint64( "SICNU_PLUGIN_SNAPSHOT_MAX_BYTES", budget.maxBytes, 4096 ),
        kByteCeiling );
    budget.maxFileBytes = std::min<uint64_t>(
        envUint64( "SICNU_PLUGIN_SNAPSHOT_MAX_FILE_BYTES",
                   budget.maxFileBytes, 1024 ),
        kByteCeiling );
    budget.maxFiles = static_cast<uint32_t>( std::min<uint64_t>(
        envUint64( "SICNU_PLUGIN_SNAPSHOT_MAX_FILES", budget.maxFiles, 8 ),
        1u << 22 ) );
    return budget;
}

std::string pluginSnapshotRoot( const std::string &tempDirectory )
{
    std::string base = tempDirectory;
    if ( base.empty() )
    {
        std::error_code ec;
        base = fs::temp_directory_path( ec ).generic_string();
        if ( ec || base.empty() )
            base = "/tmp";
    }
    return base + "/sicnu-plugin-snapshots";
}

PluginSnapshotResult capturePluginSnapshot(
    const std::string &sourceDir, const std::string &destDir,
    const std::string &pluginId, const PluginSnapshotBudget &budget,
    const std::function<bool()> &cancel )
{
    namespace fsn = std::filesystem;
    PluginSnapshotResult result;
    const fs::path source( sourceDir );
    const fs::path dest( destDir );
    const std::string instanceSuffix =
        std::to_string( snapshotOwnerPid() ) + "-"
        + std::to_string( gCaptureSeq.fetch_add( 1 ) );
    const std::string stagingDir = destDir + "~staging-" + instanceSuffix;
    const std::string parkedDir = destDir + "~old-" + instanceSuffix;

    // Serialize captures into the same dest end-to-end: a superseded
    // in-flight capture can never race this one's publish ladder. The wait
    // is cancel-aware — a cancelled job must not sit behind a long
    // same-dest capture; its destructor's join would block for a whole
    // bounded walk otherwise.
    const std::shared_ptr<std::mutex> destLock = destLockFor( destDir );
    while ( !destLock->try_lock() )
    {
        if ( cancel && cancel() )
        {
            result.status = PluginSnapshotStatus::Cancelled;
            result.message = "cancelled while waiting for the destination lock";
            return result;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    const std::lock_guard<std::mutex> destGuard( *destLock, std::adopt_lock );

    const auto fail = [&]( PluginSnapshotStatus status, const std::string &why ) {
        result.status = status;
        result.message = why;
        std::error_code ec;
        fsn::remove_all( fs::path( stagingDir ), ec );
        return result;
    };

    std::error_code ec;
    if ( !fsn::is_directory( source, ec ) || ec )
        return fail( PluginSnapshotStatus::IoError,
                     "snapshot source is not a readable directory" );

    // A payload entry named like the completeness marker would be dropped
    // by restore (the root-level marker is excluded as metadata) — refuse
    // it at capture time instead of silently losing a plugin file.
    if ( fsn::exists( source / kPluginSnapshotMarker, ec ) )
        return fail( PluginSnapshotStatus::Unsafe,
                     std::string( "snapshot source contains reserved name '" )
                         + kPluginSnapshotMarker + "'" );
    if ( ec )
        return fail( PluginSnapshotStatus::IoError,
                     "cannot inspect the snapshot source: " + ec.message() );

    // The snapshot root is a per-user trust boundary on shared temp dirs:
    // a foreign-owned pre-created root must never receive our bytes.
    const fs::path destParent = dest.parent_path();
    fsn::create_directories( destParent, ec );
    if ( ec )
        return fail( PluginSnapshotStatus::IoError,
                     "cannot create the snapshot root: " + ec.message() );
    if ( !dirOwnedByUs( destParent ) )
        return fail( PluginSnapshotStatus::Unsafe,
                     "snapshot root '" + destParent.generic_string()
                         + "' is not owned by this user (shared-temp hijack refused)" );

    // Fresh staging: a leftover from a crashed attempt is dropped, never
    // merged (a stale file set would lie about completeness).
    fsn::remove_all( fs::path( stagingDir ), ec );
    fsn::create_directories( fs::path( stagingDir ), ec );
    if ( ec )
        return fail( PluginSnapshotStatus::IoError,
                     "cannot create snapshot staging: " + ec.message() );

    uint64_t bytes = 0;
    uint32_t files = 0;
    bool cancelled = false;
    // Fail closed on unreadable entries: skipping one would produce an
    // incomplete yet marker-valid snapshot that a later rollback trusts.
    for ( fsn::recursive_directory_iterator it(
              source, fsn::directory_options::none, ec ),
          end;
          !ec && it != end; it.increment( ec ) )
    {
        if ( cancel && cancel() )
        {
            cancelled = true;
            break;
        }
        const fs::path &entry = it->path();
        const fs::path relative = fsn::relative( entry, source, ec );
        if ( ec )
            return fail( PluginSnapshotStatus::IoError,
                         "cannot relativize snapshot entry: " + ec.message() );
        std::error_code statusError;
        const fs::file_status status = fsn::symlink_status( entry, statusError );
        if ( statusError )
            return fail( PluginSnapshotStatus::IoError,
                         "cannot inspect snapshot entry: " + statusError.message() );
        if ( status.type() == fs::file_type::symlink )
            return fail( PluginSnapshotStatus::Unsafe,
                         "symlink '" + entry.generic_string()
                             + "' refused in snapshot (never followed)" );
        if ( status.type() == fs::file_type::directory )
        {
            fsn::create_directories( fs::path( stagingDir ) / relative, ec );
            if ( ec )
                return fail( PluginSnapshotStatus::IoError,
                             "cannot create snapshot subdirectory: " + ec.message() );
            continue;
        }
        if ( status.type() != fs::file_type::regular )
            return fail( PluginSnapshotStatus::Unsafe,
                         "non-regular entry '" + entry.generic_string()
                             + "' refused in snapshot" );
        const uint64_t size = fsn::file_size( entry, ec );
        if ( ec )
            return fail( PluginSnapshotStatus::IoError,
                         "cannot size snapshot entry: " + ec.message() );
        if ( files + 1 > budget.maxFiles )
            return fail( PluginSnapshotStatus::BudgetExceeded,
                         "file-count budget exceeded (" + std::to_string( files + 1 )
                             + " > " + std::to_string( budget.maxFiles ) + ")" );
        if ( size > budget.maxFileBytes )
            return fail( PluginSnapshotStatus::BudgetExceeded,
                         "file '" + entry.generic_string() + "' exceeds the per-file "
                             "budget (" + std::to_string( size ) + " > "
                             + std::to_string( budget.maxFileBytes ) + ")" );
        if ( bytes + size > budget.maxBytes )
            return fail( PluginSnapshotStatus::BudgetExceeded,
                         "byte budget exceeded (" + std::to_string( bytes + size )
                             + " > " + std::to_string( budget.maxBytes ) + ")" );
        const fs::path destination = fs::path( stagingDir ) / relative;
        fsn::create_directories( destination.parent_path(), ec );
        if ( ec )
            return fail( PluginSnapshotStatus::IoError,
                         "cannot create snapshot parent: " + ec.message() );
        fsn::copy_file( entry, destination, fsn::copy_options::overwrite_existing, ec );
        if ( ec )
            return fail( PluginSnapshotStatus::IoError,
                         "cannot copy '" + entry.generic_string()
                             + "' into snapshot: " + ec.message() );
        bytes += size;
        ++files;
    }
    if ( cancelled )
        return fail( PluginSnapshotStatus::Cancelled, "snapshot capture cancelled" );
    if ( ec )
        return fail( PluginSnapshotStatus::IoError,
                     "cannot walk the plugin directory: " + ec.message() );

    // Marker LAST: its presence is the completeness proof — a partial copy
    // never carries it.
    {
        Json::Value marker( Json::objectValue );
        marker["schema"] = 1;
        marker["pluginId"] = pluginId;
        marker["files"] = static_cast<Json::UInt64>( files );
        marker["bytes"] = static_cast<Json::UInt64>( bytes );
        const std::string markerPath = stagingDir + "/" + kPluginSnapshotMarker;
        std::ofstream out( markerPath, std::ios::trunc );
        if ( !out )
            return fail( PluginSnapshotStatus::IoError,
                         "cannot write the snapshot marker" );
        Json::StreamWriterBuilder writer;
        out << Json::writeString( writer, marker );
        out.flush();
        if ( !out )
            return fail( PluginSnapshotStatus::IoError,
                         "cannot write the snapshot marker" );
    }

    result.bytes = bytes;
    result.files = files;

    // Publish: park the previous dest, swap staging in, drop the park. A
    // crash inside the ladder leaves either the old dest (pre-swap) or
    // parked residue the sweep restores — never a half-written dest.
    std::error_code removeError;
    fsn::remove_all( fs::path( parkedDir ), removeError );
    if ( fsn::exists( dest, ec ) && !ec )
    {
        fsn::rename( dest, fs::path( parkedDir ), ec );
        if ( ec )
            return fail( PluginSnapshotStatus::IoError,
                         "cannot park the previous snapshot: " + ec.message() );
    }
    fsn::rename( fs::path( stagingDir ), dest, ec );
    if ( ec )
    {
        // Restore the parked dest if there was one; either way report.
        std::error_code restoreError;
        fsn::rename( fs::path( parkedDir ), dest, restoreError );
        return fail( PluginSnapshotStatus::IoError,
                     "cannot publish the snapshot: " + ec.message() );
    }
    fsn::remove_all( fs::path( parkedDir ), removeError );
    result.status = PluginSnapshotStatus::Ok;
    result.message.clear();
    return result;
}

bool verifyPluginSnapshot( const std::string &snapshotDir,
                           const std::string &pluginId, std::string &error )
{
    namespace fsn = std::filesystem;
    std::error_code ec;
    if ( !fsn::is_directory( fs::path( snapshotDir ), ec ) || ec )
    {
        error = "snapshot directory is missing";
        return false;
    }
    if ( !dirOwnedByUs( fs::path( snapshotDir ) ) )
    {
        error = "snapshot directory is not owned by this user (untrusted)";
        return false;
    }
    const std::string markerPath = snapshotDir + "/" + kPluginSnapshotMarker;
    if ( !fsn::is_regular_file( fs::path( markerPath ), ec ) || ec )
    {
        error = "snapshot has no completion marker (partial or legacy copy)";
        return false;
    }
    std::ifstream input( markerPath );
    if ( !input )
    {
        error = "snapshot marker is unreadable";
        return false;
    }
    Json::Value marker;
    Json::CharReaderBuilder builder;
    builder["stackLimit"] = 32;
    std::string parseError;
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    try
    {
        std::stringstream buffer;
        buffer << input.rdbuf();
        const std::string doc = buffer.str();
        if ( !reader->parse( doc.data(), doc.data() + doc.size(), &marker, &parseError )
             || !marker.isObject() )
        {
            error = "snapshot marker does not parse";
            return false;
        }
    }
    catch ( ... )
    {
        error = "snapshot marker does not parse";
        return false;
    }
    if ( !marker["pluginId"].isString() || marker["pluginId"].asString() != pluginId )
    {
        error = "snapshot marker names a different plugin";
        return false;
    }
    if ( !marker["files"].isUInt64() || !marker["bytes"].isUInt64() )
    {
        error = "snapshot marker has malformed counts";
        return false;
    }
    const uint64_t declaredFiles = marker["files"].asUInt64();
    const uint64_t declaredBytes = marker["bytes"].asUInt64();

    // Bounded re-walk: recount payload files (marker excluded) and compare.
    const fs::path rootNorm = fs::path( snapshotDir ).lexically_normal();
    uint64_t files = 0;
    uint64_t bytes = 0;
    for ( fsn::recursive_directory_iterator it(
              fs::path( snapshotDir ), fsn::directory_options::skip_permission_denied, ec ),
          end;
          !ec && it != end; it.increment( ec ) )
    {
        const fs::path &entry = it->path();
        // Only the ROOT-level marker is metadata; a payload file that merely
        // shares the name still counts. lexically_normal both sides: a
        // trailing-slash snapshotDir would otherwise defeat the exclusion.
        if ( entry.filename() == kPluginSnapshotMarker
             && entry.parent_path().lexically_normal() == rootNorm )
            continue;
        if ( !fsn::is_regular_file( entry, ec ) || ec )
        {
            if ( ec )
                break;
            continue;
        }
        bytes += fsn::file_size( entry, ec );
        if ( ec )
            break;
        ++files;
        if ( files > declaredFiles || bytes > declaredBytes )
        {
            error = "snapshot payload exceeds its marker (tampered or partial)";
            return false;
        }
    }
    if ( ec )
    {
        error = "cannot re-walk the snapshot: " + ec.message();
        return false;
    }
    if ( files != declaredFiles || bytes != declaredBytes )
    {
        error = "snapshot payload does not match its marker (" +
                std::to_string( files ) + "/" + std::to_string( bytes )
                + " vs declared " + std::to_string( declaredFiles ) + "/"
                + std::to_string( declaredBytes ) + ")";
        return false;
    }
    return true;
}

bool restorePluginSnapshot( const std::string &snapshotDir,
                            const std::string &pluginDir,
                            const std::string &pluginId, std::string &error )
{
    namespace fsn = std::filesystem;
    // A restore is only ever fed by a VERIFIED snapshot: the marker gate is
    // folded in here so no caller can accidentally trust a partial tree.
    if ( !verifyPluginSnapshot( snapshotDir, pluginId, error ) )
        return false;
    std::error_code ec;
    const fs::path source( snapshotDir );
    const fs::path target( pluginDir );
    // Replace the payload: remove the target's entries, copy back. Files
    // present in the target but absent from the snapshot are REMOVED (the
    // new version may have added files).
    for ( fsn::directory_iterator iterator( target, ec ), end; !ec && iterator != end; )
    {
        const fs::path entry = iterator->path();
        iterator.increment( ec );
        if ( ec )
            break;
        fsn::remove_all( entry, ec );
        if ( ec )
        {
            error = "cannot clear the plugin directory for rollback: " + ec.message();
            return false;
        }
    }
    if ( ec )
    {
        error = "cannot walk the plugin directory for rollback: " + ec.message();
        return false;
    }
    // Copy the snapshot tree back — symlinks refused exactly like capture
    // (a verified snapshot contains none, but a hand-built one could).
    for ( fsn::recursive_directory_iterator iterator( source, fsn::directory_options::skip_permission_denied, ec ), end;
          !ec && iterator != end; iterator.increment( ec ) )
    {
        const fs::path &entry = iterator->path();
        const fs::path relative = fsn::relative( entry, source, ec );
        if ( ec )
        {
            error = "cannot relativize snapshot entry: " + ec.message();
            return false;
        }
        const fs::path destination = target / relative;
        if ( fsn::is_symlink( entry, ec ) )
        {
            error = "symlink '" + entry.generic_string()
                    + "' refused in the snapshot payload";
            return false;
        }
        if ( ec )
        {
            error = "cannot inspect snapshot entry: " + ec.message();
            return false;
        }
        if ( fsn::is_directory( entry, ec ) )
        {
            fsn::create_directories( destination, ec );
            if ( ec )
            {
                error = "cannot create restored subdirectory: " + ec.message();
                return false;
            }
            continue;
        }
        fsn::create_directories( destination.parent_path(), ec );
        if ( ec )
        {
            error = "cannot create restored parent directory: " + ec.message();
            return false;
        }
        fsn::copy_file( entry, destination, fsn::copy_options::overwrite_existing, ec );
        if ( ec )
        {
            error = "cannot restore '" + entry.generic_string() + "': " + ec.message();
            return false;
        }
    }
    if ( ec )
    {
        error = "cannot walk the snapshot for restore: " + ec.message();
        return false;
    }
    // snapshot.marker.json is snapshot metadata, not plugin payload — the
    // plain copy above carried it over; drop it from the restored tree so
    // the plugin dir only ever holds real package bytes.
    {
        std::error_code markerEc;
        fsn::remove( target / kPluginSnapshotMarker, markerEc );
    }
    // The manifest index cache (plugin_discovery) is keyed by mtime, and a
    // file copy PRESERVES the source timestamps — so a byte-identical restore
    // would keep the failed version's cache entry alive and the rollback
    // would rescan the very manifest it just replaced. Stamping the restored
    // files with the current time is both honest (this IS new content on
    // disk) and what makes the cache re-parse.
    for ( fsn::recursive_directory_iterator iterator( target, fsn::directory_options::skip_permission_denied, ec ), end; !ec && iterator != end; iterator.increment( ec ) )
    {
        if ( fsn::is_regular_file( iterator->path(), ec ) && !ec )
            fsn::last_write_time( iterator->path(), fs::file_time_type::clock::now(), ec );
    }
    if ( ec )
    {
        error = "cannot stamp the restored files: " + ec.message();
        return false;
    }
    fsn::remove_all( source, ec );
    if ( ec )
    {
        error = "cannot remove the consumed snapshot: " + ec.message();
        return false;
    }
    return true;
}

int sweepPluginSnapshots( const std::string &tempDirectory,
                          const std::vector<std::string> &liveIds,
                          const std::vector<std::string> &pluginRoots )
{
    namespace fsn = std::filesystem;
    const std::string root = pluginSnapshotRoot( tempDirectory );
    std::error_code ec;
    const fs::path rootPath( root );
    // Fail closed: a symlinked root is never traversed (remove_all on a
    // symlink only drops the link, but iterating one would look inside a
    // foreign tree — skip the sweep entirely instead).
    if ( fsn::is_symlink( rootPath, ec ) )
        return 0;
    if ( !fsn::is_directory( rootPath, ec ) || ec )
        return 0;
    // A foreign-owned root is outside our trust boundary — reconciling
    // names inside it would let us delete another user's files.
    if ( !dirOwnedByUs( rootPath ) )
        return 0;

    int removed = 0;
    const long ownPid = snapshotOwnerPid();
    for ( fsn::directory_iterator it( rootPath, ec ), end; !ec && it != end;
          it.increment( ec ) )
    {
        const fs::path entry = it->path();
        const std::string name = entry.filename().generic_string();
        if ( !allSafeNameChars( name ) )
            continue;
        std::string base;
        bool remove = false;
        // Fail closed: no artifact this process or a sibling instance
        // publishes is ever a symlink — a link in a collectible name slot
        // is forged residue, never something to promote or inspect.
        // remove_all on a symlink drops only the link, never the target.
        if ( fsn::is_symlink( entry, ec ) )
        {
            std::error_code linkError;
            if ( fsn::remove( entry, linkError ) )
                ++removed;
            continue;
        }
        if ( splitSuffix( name, "~staging-", base ) )
        {
            // In-flight capture staging is collectible only when its owning
            // process is DEAD. A same-pid dir is this process's live capture
            // (every capture exit path removes or renames its own staging),
            // and a live foreign-pid dir is a concurrent instance mid-copy —
            // deleting either would pull files from under a running walker.
            const long owner = tailOwnerPid( name, "~staging-" );
            remove = owner <= 0 || ( owner != ownPid && !pidAlive( owner ) );
        }
        else if ( splitSuffix( name, "~old-", base ) )
        {
            // Parked dest from an interrupted publish ladder. A live owner
            // (this process or a concurrent one mid-swap) cleans its own
            // park — never touch it. A dead owner's park is crash residue:
            // if the real dest vanished, restore it (crash between the two
            // renames); if the dest exists, the park is pure residue.
            const long owner = tailOwnerPid( name, "~old-" );
            if ( owner == ownPid || ( owner > 0 && pidAlive( owner ) ) )
                continue;
            const fs::path dest = rootPath / base;
            std::error_code destError;
            const bool destPresent = fsn::exists( dest, destError );
            if ( destError )
                continue; // indeterminate — keep the only backup, retry next sweep
            if ( !destPresent )
            {
                std::error_code renameError;
                fsn::rename( entry, dest, renameError );
                if ( renameError )
                    remove = true; // unrestorable residue — drop it
            }
            else
            {
                remove = true;
            }
        }
        else if ( name.rfind( "upgrade-", 0 ) == 0 )
        {
            // upgrade-<id>-<pid>: collectible when its owner is dead (crash
            // residue) or the name is malformed. Same-pid is this process's
            // live upgrade or a deliberately kept failed-rollback artifact;
            // a live foreign pid is a concurrent instance's in-flight
            // upgrade whose rollback source must not be pulled.
            const long owner = trailingPid( name );
            remove = owner <= 0 || ( owner != ownPid && !pidAlive( owner ) );
            // #1157: a dead-owner snapshot of a still-live plugin is the
            // last complete copy, not residue — the owner may have died
            // mid-rollback, after restorePluginSnapshot had already begun
            // clearing the live directory file-by-file. Deleting it (the
            // old rule) stranded a partial install with no recovery path.
            // Restore it into <root>/<id> like reconcileStaging restores a
            // parked dest; an unrestorable snapshot still falls back to
            // residue removal (and stays collectible when roots are not
            // supplied or the plugin is no longer live).
            if ( remove && owner > 0 && !pluginRoots.empty() )
            {
                std::string pluginId = name.substr( std::string( "upgrade-" ).size() );
                const size_t lastDash = pluginId.rfind( '-' );
                if ( lastDash != std::string::npos )
                    pluginId = pluginId.substr( 0, lastDash );
                const bool stillLive =
                    std::find( liveIds.begin(), liveIds.end(), pluginId ) != liveIds.end();
                if ( !pluginId.empty() && stillLive )
                {
                    for ( const std::string &rootDir : pluginRoots )
                    {
                        const fs::path target = fs::path( rootDir ) / pluginId;
                        std::error_code probeError;
                        if ( !fsn::is_directory( target, probeError ) )
                            continue;
                        std::string restoreError;
                        if ( restorePluginSnapshot( entry.generic_string(),
                                                    target.generic_string(), pluginId,
                                                    restoreError ) )
                        {
                            remove = false;
                            ++removed; // the snapshot dir was consumed by the restore
                            break;
                        }
                    }
                }
            }
        }
        else if ( name.rfind( "last-good-", 0 ) == 0 )
        {
            const std::string id = name.substr( std::string( "last-good-" ).size() );
            remove = std::find( liveIds.begin(), liveIds.end(), id ) == liveIds.end();
        }
        if ( remove )
        {
            std::error_code removeError;
            fsn::remove_all( entry, removeError );
            if ( !removeError )
                ++removed;
        }
    }

    // Legacy 12.0 residue: dev snapshots used to live directly in the temp
    // root as plugin-last-good-<id> (pre snapshot-root layout). Apply the
    // same liveness rule — an id with no live record is orphaned either
    // way. Same-pid protection is not needed here (the legacy path is
    // write-once at the old code path, long dead).
    const fs::path tempPath( tempDirectory.empty()
                                 ? fsn::temp_directory_path( ec )
                                 : fs::path( tempDirectory ) );
    if ( fsn::is_directory( tempPath, ec ) && !ec )
    {
        for ( fsn::directory_iterator it( tempPath, ec ), end; !ec && it != end;
              it.increment( ec ) )
        {
            const std::string name = it->path().filename().generic_string();
            const std::string legacyPrefix = "plugin-last-good-";
            if ( name.rfind( legacyPrefix, 0 ) != 0 || !allSafeNameChars( name ) )
                continue;
            const std::string id = name.substr( legacyPrefix.size() );
            if ( std::find( liveIds.begin(), liveIds.end(), id ) != liveIds.end() )
                continue;
            // Same fail-closed rule as the new-grammar branches: a
            // foreign-owned dir in a shared temp root is outside our trust
            // boundary — never reconciled by us.
            if ( !dirOwnedByUs( it->path() ) )
                continue;
            std::error_code removeError;
            fsn::remove_all( it->path(), removeError );
            if ( !removeError )
                ++removed;
        }
    }
    return removed;
}

// ---- PluginSnapshotJob ------------------------------------------------------

std::shared_ptr<PluginSnapshotJob> PluginSnapshotJob::start(
    const std::string &sourceDir, const std::string &destDir,
    const std::string &pluginId, const PluginSnapshotBudget &budget )
{
    auto job = std::shared_ptr<PluginSnapshotJob>( new PluginSnapshotJob() );
    // The worker captures the RAW pointer, not the shared_ptr: a job that
    // owned a reference to itself could have its destructor run INSIDE the
    // worker thread when the last external ref dropped mid-capture, and
    // join() on the current thread throws std::system_error — the same
    // untyped-throw class the channel close race produced (WP4). The
    // lifetime contract is instead the destructor's cancel+join: whoever
    // drops the last ref blocks (bounded by one file's copy time, since
    // cancel is checked per entry) until the worker has exited.
    PluginSnapshotJob *raw = job.get();
    job->mWorker = std::thread( [raw, sourceDir, destDir, pluginId, budget] {
        raw->run( sourceDir, destDir, pluginId, budget );
    } );
    return job;
}

PluginSnapshotJob::~PluginSnapshotJob()
{
    // cancel() first so the worker exits at its next file boundary; join()
    // then bounds the wait to at most one in-flight file copy.
    cancel();
    if ( mWorker.joinable() )
        mWorker.join();
}

void PluginSnapshotJob::cancel()
{
    mCancel.store( true );
}

bool PluginSnapshotJob::wait( int timeoutMs )
{
    std::unique_lock<std::mutex> lock( mMutex );
    return mCv.wait_for( lock, std::chrono::milliseconds( timeoutMs ),
                         [this] { return mFinished.load(); } );
}

PluginSnapshotResult PluginSnapshotJob::result()
{
    std::unique_lock<std::mutex> lock( mMutex );
    mCv.wait( lock, [this] { return mFinished.load(); } );
    return mResult;
}

void PluginSnapshotJob::run( std::string sourceDir, std::string destDir,
                             std::string pluginId, PluginSnapshotBudget budget )
{
    mResult = capturePluginSnapshot( sourceDir, destDir, pluginId, budget,
                                     [this] { return mCancel.load(); } );
    {
        std::lock_guard<std::mutex> lock( mMutex );
        mFinished.store( true );
    }
    mCv.notify_all();
}

} // namespace exprs
