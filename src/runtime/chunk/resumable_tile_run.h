// resumable_tile_run.h — crash-safe resumable tile execution driver
// (Execution Runtime Convergence 11.0, WP-C).
//
// Promotes the 10.0 tile-checkpoint LIBRARY capability into a runnable
// protocol that survives a real process crash and resumes verifiably:
//
//   per tile:  compute → write <runDir>/tile-<idx>.tl (.part + rename,
//              self-describing header + payload digest) → append commit line
//              to the journal → consume (idempotent output rebuild)
//   at end:    save final checkpoint → publish() → write PUBLISHED marker
//              (tmp + rename)
//
// Resume laws (all fail-closed, tested in tests/test_chunk_resume_11.cpp
// including REAL child-process crashes):
//   R1 identity gates — operatorIdentity / inputIdentity (checkpoint) and the
//      full identity key incl. partition digest (journal header) must match;
//      any drift wipes prior state and re-executes; stale tiles are NEVER
//      reused across identity drift.
//   R2 committed tiles are not recomputed — a committed tile is re-READ from
//      disk with full digest verification and handed to consume() again
//      (output rebuild); only missing/corrupt tiles invoke compute().
//   R3 a torn journal tail (crash mid-append) truncates to the last complete
//      commit line; corruption ANYWHERE else is a typed ChunkCorruptTile —
//      never silently interpreted.
//   R4 a corrupt/missing tile file with an intact journal self-heals by
//      recomputing that tile (compute is the truth source).
//   R5 exactly-once publication — the PUBLISHED marker is the run-level
//      commit: after it exists, execute() succeeds with zero compute calls;
//      a crash before the marker replays consume()+publish() (both must be
//      idempotent-rebuildable; the adoption guide spells out the .part→
//      rename pattern for the final artifact).
//   R6 cancellation throws ChunkCancelled and leaves resumable state behind
//      (crash semantics); abandon() is the explicit user-cancel wipe.
//
// Memory is O(committed-set bitmap + 1 tile): the journal streams, the tile
// grid is arithmetic (tileSpecAt), never materialized.
#pragma once

#include "disk_tile_store.h"
#include "tile_checkpoint.h"
#include "tile_run_contract.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::runtime::chunk
{

class ResumableTileRun
{
  public:
    struct Config
    {
        /// Root directory for run scratch: tiles live in
        /// `<scratchRoot>/<runKey>/tile-<idx>.tl`. Non-empty.
        std::string scratchRoot;
        /// Base path for state sidecars: `<statePath>.journal`,
        /// `<statePath>.ckpt`, `<statePath>.published`. Non-empty. SHOULD be
        /// on durable storage next to the final artifact (not inside the
        /// scratch root that sweeps clean).
        std::string statePath;
        /// Checkpoint cadence in committed tiles (0 = only at completion).
        std::uint64_t checkpointIntervalTiles = 64;
    };

    struct Callbacks
    {
        /// The expensive kernel: produces one tile's payload for the given
        /// spec (driver validates the buffer against the spec).
        std::function<TilePayload( const TileSpec & )> compute;
        /// Idempotent output rebuild step: receives every tile exactly once
        /// per execution, in index order (from disk on resume, from compute
        /// otherwise).
        std::function<void( const TilePayload & )> consume;
        /// Final publication — must be atomic-rebuildable (e.g. write
        /// artifact to `.part`, rename). Called at most once per execution.
        std::function<void()> publish;
        /// Progress 0..1 (done/totalTiles), monotonic.
        std::function<void( double )> progress;
    };

    struct Result
    {
        std::uint64_t totalTiles = 0;
        std::uint64_t tilesReused = 0;  ///< verified disk attach (R2)
        std::uint64_t tilesComputed = 0;
        bool alreadyPublished = false;  ///< marker hit: zero work done (R5)
    };

    ResumableTileRun( TileRunSpec spec, Config config );

    /// Drives the run to publication. Throws ChunkCancelled (cancel),
    /// ChunkCorruptTile (journal corruption), std::runtime_error (compute /
    /// I/O failures and injected crash faults) — all leaving resumable
    /// state behind.
    Result execute( const TileRunCancelSource &cancel, Callbacks cb );

    /// User-cancel wipe: this run must NEVER resume (TileCheckpoint
    /// contract). Removes journal, checkpoint and tile directory; a PUBLISHED
    /// marker is kept (the run did complete once).
    void abandon();

    /// Post-success cleanup: drop tile directory, journal and checkpoint.
    /// The PUBLISHED marker stays as the exactly-once evidence.
    void cleanupAfterPublish();

    const TileRunSpec &spec() const { return m_spec; }
    const std::string &runKey() const { return m_runKey; }

  private:
    struct JournalState
    {
        bool fresh = true;                  ///< no usable prior state
        std::vector<bool> committed;        ///< per-index commit flags
        std::uint64_t committedCount = 0;
    };

    std::filesystem::path tilePath( std::uint64_t index ) const;
    const std::string &journalPath() const { return m_journalPath; }
    const std::string &checkpointPath() const { return m_checkpointPath; }
    const std::string &markerPath() const { return m_markerPath; }

    /// Loads + validates the journal (R3): identity gate, torn-tail
    /// truncation, typed corruption. Returns the commit set.
    JournalState loadJournal() const;

    /// Appends one commit line (and the identity header when the journal is
    /// new).
    void appendCommit( std::uint64_t index );

    void saveCheckpoint( std::uint64_t committedCount ) const;

    /// Reads + validates the PUBLISHED marker. Returns true when it proves
    /// THIS run's identity.
    bool markerMatches() const;
    void writeMarker() const;

    /// Removes tile dir + journal + checkpoint (+ marker when requested) —
    /// identity drift / fresh start. const: it only touches on-disk state.
    void wipeState( bool includeMarker ) const;

    TileRunSpec m_spec;
    Config m_config;
    std::string m_runKey;
    std::string m_runDir;
    std::string m_journalPath;
    std::string m_checkpointPath;
    std::string m_markerPath;
    std::string m_identityKey;
    std::uint64_t m_totalTiles = 0;
};

} // namespace sicnu::runtime::chunk
