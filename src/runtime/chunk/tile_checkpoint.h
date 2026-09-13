// tile_checkpoint.h — Long-task (tile-loop) checkpoint/resume primitives
// (LSEE 10.0, ADR 0148 §5).
//
// Workflow Engine 2.0 checkpoints whole STEPS; a single long streaming task
// (hours of tiles) had no mid-task recovery — a crash re-ran every tile.
// TileCheckpointWriter records an operator's in-progress position every N
// tiles with the SAME durability family as the workflow checkpoint
// (unique tmp + fsync + atomic rename), and TileCheckpoint::load re-proves
// everything fail-closed:
//
//   - format version gate (unknown version → refuse, re-execute)
//   - operator identity hash (implementation changed → refuse)
//   - parameter/input drift hash (params or inputs changed → refuse)
//   - header truncation / digest mismatch (crash mid-write → refuse)
//
// A refused checkpoint is indistinguishable from "no checkpoint": the task
// simply starts over. A user cancellation and a crash are distinguishable by
// WHO moves the checkpoint away: cancellation deletes it (the task was told
// to stop, never resume); a crash leaves it on disk for the restart.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::runtime::chunk
{

/// One serialized checkpoint's payload (pure value; JSON-free for the
/// Qt-free runtime layer — callers project it into task state themselves).
struct TileCheckpoint
{
    std::uint32_t formatVersion = 0;
    /// Hash of the operator implementation identity at save time.
    std::uint64_t operatorIdentity = 0;
    /// Hash of the params + input identities at save time.
    std::uint64_t inputIdentity = 0;
    /// Number of tiles fully completed (and durable in scratch) at save time.
    std::uint64_t completedTiles = 0;
    /// Run-scoped scratch directory the completed tiles live in (opaque to
    /// this layer; the operator's own tile store re-verifies digests).
    std::string scratchRunId;
    /// Digest of the payload bytes above (corruption tripwire).
    std::uint64_t payloadDigest = 0;

    bool operator==( const TileCheckpoint & ) const = default;
};

/// Current serialization format version. A file whose version differs (older
/// or newer) is refused — checkpoint compatibility is fail-closed, never
/// best-effort parsed.
inline constexpr std::uint32_t kTileCheckpointFormatVersion = 1;

class TileCheckpointWriter
{
  public:
    /// Serializes @p checkpoint to @p path atomically (unique tmp → fsync →
    /// rename). Returns false when the write could not complete; the previous
    /// checkpoint (if any) stays intact.
    static bool save( const std::string &path, const TileCheckpoint &checkpoint );

    /// Loads and validates a checkpoint. nullopt on: missing file, unknown
    /// format version, truncation, digest mismatch, operator identity
    /// mismatch, or input identity mismatch (any of which must re-execute
    /// the task from scratch — never serve partial state).
    static std::optional<TileCheckpoint> load( const std::string &path,
                                               std::uint64_t expectedOperatorIdentity,
                                               std::uint64_t expectedInputIdentity );

    /// Deletes the checkpoint (user-cancellation path: the task must never
    /// resume). Missing file is not an error.
    static void remove( const std::string &path );
};

/// FNV-1a 64-bit — the identity/digest primitive for checkpoint fields.
/// Match the scratch-store digest family: corruption tripwire, not crypto.
std::uint64_t tileCheckpointHash( const void *data, std::size_t size );

/// Convenience: hash of params+inputs (canonical bytes the caller decides —
/// e.g. the RFC 8785 canonical parameter JSON + fingerprint hex).
inline std::uint64_t tileCheckpointInputIdentity( const std::string &canonicalInputs )
{
    return tileCheckpointHash( canonicalInputs.data(), canonicalInputs.size() );
}

} // namespace sicnu::runtime::chunk
