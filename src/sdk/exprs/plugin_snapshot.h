/***************************************************************************
 * exprs/plugin_snapshot.h — bounded, verifiable, cancellable plugin
 * directory snapshots (track 13.0: replaces the 12.0 ad-hoc recursive copy)
 *
 * Layout under the snapshot root (registry temp dir, deterministic subdir):
 *   <temp>/sicnu-plugin-snapshots/last-good-<id>      dev-mode hot reload
 *   <temp>/sicnu-plugin-snapshots/upgrade-<id>-<pid>  in-flight upgrade backup
 *   <temp>/sicnu-plugin-snapshots/<name>.staging-*    in-flight capture
 *   <temp>/sicnu-plugin-snapshots/<name>.old-*        dest parked during swap
 *
 * A snapshot is only ever published whole: capture writes into a staging
 * sibling, writes snapshot.marker.json LAST (schema, pluginId, file count,
 * byte total), then swaps staging -> dest through the same rename ladder the
 * package installer uses. Readers verify the marker before trusting bytes —
 * a partial copy can never look like a rollback source. Capture enforces a
 * byte budget and a file-count budget, refuses symlinks (never follows), and
 * honours a cancel predicate between files so shutdown cannot strand a
 * worker mid-tree.
 ***************************************************************************/
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace exprs {

/// Byte and file-count bounds for one snapshot capture. Over-budget is a
/// typed refusal (QuotaExceeded at the call site), never a partial publish.
struct PluginSnapshotBudget
{
    uint64_t maxBytes = 256ull * 1024ull * 1024ull;   ///< 256 MiB total
    uint64_t maxFileBytes = 64ull * 1024ull * 1024ull; ///< 64 MiB per file
    uint32_t maxFiles = 8192;
    /// Reads SICNU_PLUGIN_SNAPSHOT_MAX_BYTES / _MAX_FILE_BYTES / _MAX_FILES
    /// (0 or unset = default). Values are clamped to sane floors so a
    /// hostile env cannot turn the bound into an effective no-snapshot.
    static PluginSnapshotBudget fromEnvironment();
};

enum class PluginSnapshotStatus
{
    Ok,              ///< dest now holds a complete, marker-verified snapshot
    Cancelled,       ///< cancel predicate fired; staging removed
    BudgetExceeded,  ///< byte or file bound hit mid-walk; staging removed
    IoError,         ///< source unreadable or a filesystem op failed
    Unsafe,          ///< symlink/non-regular entry refused (fail closed)
};

struct PluginSnapshotResult
{
    PluginSnapshotStatus status = PluginSnapshotStatus::Ok;
    std::string message;
    uint64_t bytes = 0;   ///< payload bytes captured (marker excluded)
    uint32_t files = 0;   ///< payload files captured
    bool ok() const { return status == PluginSnapshotStatus::Ok; }
};

/// Name of the marker written last inside a published snapshot.
inline constexpr const char *kPluginSnapshotMarker = "snapshot.marker.json";

/// Captures @p sourceDir into @p destDir atomically: bounded walk (regular
/// files only, symlinks refused) into "<destDir>.staging-<pid>-<seq>",
/// marker written last, then dest swapped in. A crash mid-publish leaves
/// staging / .old residue the snapshot sweep reclaims — dest is never
/// half-written. Captures into the SAME dest serialize on a per-dest lock,
/// so a superseded job can never publish stale bytes over a newer swap.
/// Synchronous; use PluginSnapshotJob for the non-blocking path.
PluginSnapshotResult capturePluginSnapshot(
    const std::string &sourceDir, const std::string &destDir,
    const std::string &pluginId, const PluginSnapshotBudget &budget,
    const std::function<bool()> &cancel = {} );

/// A published snapshot is trustworthy only when its marker exists, parses,
/// names the same plugin, and a bounded re-walk reproduces the declared
/// file/byte counts. Anything else is residue, not a rollback source.
bool verifyPluginSnapshot( const std::string &snapshotDir,
                           const std::string &pluginId, std::string &error );

/// Deterministic snapshot root inside the registry temp directory.
std::string pluginSnapshotRoot( const std::string &tempDirectory );

/// Owning process id used in residue names (staging-.old-/upgrade-
/// suffixes) so the sweep can tell a dead process's residue from a live
/// own capture. Exposed for callers that compose the same names.
long snapshotOwnerPid();

/// Bounded GC over the snapshot root (WP3). Reclaims in-flight residue
/// (*.staging-*, upgrade-*, *.old-*) whose OWNING pid is dead — artifacts
/// of a live process (this one or a concurrent instance) are never
/// touched — restores a .old-* backup when its dest went missing mid-swap,
/// drops last-good-<id> directories whose id is not in @p liveIds
/// (abandoned dev trees, externally uninstalled plugins), and cleans the
/// legacy <temp>/plugin-last-good-<id> layout left by 12.0 builds.
/// Fails closed: a symlinked root is skipped, a non-directory root is
/// untouched. Returns the number of directories removed.
int sweepPluginSnapshots( const std::string &tempDirectory,
                          const std::vector<std::string> &liveIds,
                          const std::string &logContext = {} );

/// One asynchronous capture. Owns its worker thread: cancel() + join in the
/// destructor means a dropped job (registry teardown, a newer capture
/// superseding it) can never leak a thread or leave a half-published dest.
class PluginSnapshotJob
{
public:
    /// Starts the capture on a dedicated thread. The caller keeps the
    /// shared_ptr; dropping the last reference cancels and joins.
    static std::shared_ptr<PluginSnapshotJob> start(
        const std::string &sourceDir, const std::string &destDir,
        const std::string &pluginId, const PluginSnapshotBudget &budget );
    ~PluginSnapshotJob();

    PluginSnapshotJob( const PluginSnapshotJob & ) = delete;
    PluginSnapshotJob &operator=( const PluginSnapshotJob & ) = delete;

    /// Cooperative cancel: the worker checks between files. Always joins.
    void cancel();
    /// True when the worker has finished (call result()).
    bool finished() const;
    /// Blocks until the worker finishes or @p timeoutMs elapses. True =
    /// finished. On timeout the job keeps running; the caller may cancel()
    /// or simply drop the shared_ptr (dtor cancels + joins).
    bool wait( int timeoutMs );
    /// Final outcome; blocks until finished.
    PluginSnapshotResult result();

private:
    PluginSnapshotJob() = default;
    void run( std::string sourceDir, std::string destDir, std::string pluginId,
              PluginSnapshotBudget budget );

    std::thread mWorker;
    std::atomic<bool> mCancel{ false };
    std::atomic<bool> mFinished{ false };
    PluginSnapshotResult mResult;
    std::mutex mMutex;
    std::condition_variable mCv;
};

} // namespace exprs
