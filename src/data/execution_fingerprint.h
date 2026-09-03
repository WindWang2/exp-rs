// src/data/execution_fingerprint.h
//
// ADR (perf/architecture goal 2026-08-08, Phase F) — revision-aware execution
// cache vertical slice.
//
// Deterministic operators (spectral index, band math, …) produce identical
// output for identical (algorithm + version, normalized parameters, input
// AssetId + AssetRevision). An ExecutionFingerprint hashes exactly those inputs
// so a cache can reuse a prior materialized result without re-running the
// operator. Because AssetRevision changes whenever an input is re-derived, the
// fingerprint is automatically revision-sensitive: a stale input yields a
// different fingerprint → cache miss → re-run. The cache NEVER keys on a file
// path (which can be reused across revisions); it keys on the immutable
// identity + revision.
//
// This is the contract + lookup/store vertical slice, OFF by default; deterministic
// operators opt in. Full producer wiring (intercepting runOperatorTask to consult
// the cache before execution) is a tracked follow-up — the slice here proves the
// contract with unit tests and gives producers a stable API to adopt.
#pragma once

#include "asset_types.h"
#include "derivation_record.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>

namespace sicnu::data
{

/// Version of the fingerprint CONTRACT itself (#726): mixed into the
/// implementation-version hash by every producer, so any semantic change to
/// what a fingerprint covers (v2: chained producer identity, key-based
/// destination exclusion, remote input identity) invalidates every entry
/// computed under the old contract instead of silently comparing
/// incomparable digests. Bump together with the platform version below.
inline constexpr int kExecutionFingerprintContractVersion = 2;

/// Platform software version participating in the implementation identity
/// (matches the CMake project() version). Deliberately NOT a build path,
/// timestamp, or VCS hash: those change per machine/commit and would split
/// the cache without any behavioral meaning.
inline constexpr const char *kExecutionFingerprintPlatformVersion = "1.0";

/// A deterministic hash of an execution's identity: algorithm + version +
/// normalized parameters + input asset identities/revisions. Two executions
/// with the same fingerprint are guaranteed to produce identical output for a
/// deterministic operator, so the output AssetId can be reused.
///
/// The fingerprint stores the FULL 256-bit SHA-256 digest of the canonical
/// form (see makeExecutionFingerprint), NOT a truncated/folded key. A
/// truncated key would make a collision between two *different* executions
/// (e.g. different inputs/revisions) return another execution's output AssetId
/// — a silent correctness failure that a truncated hash cannot detect. With the
/// full digest, reuse happens only for byte-identical canonical inputs.
/// Revision sensitivity makes the common invalidation case (input changed) a
/// guaranteed miss.
struct ExecutionFingerprint
{
  /// The full 256-bit SHA-256 digest of the canonical input form (32 bytes).
  /// A default-constructed ExecutionFingerprint (empty digest) is a valid,
  /// well-defined fingerprint distinct from any produced by hashing real
  /// inputs (a 32-byte zero digest is never the SHA-256 of a real canonical
  /// input).
  QByteArray digest;

  QString toHex() const { return QString::fromUtf8( digest.toHex() ); }
  std::string toStdString() const { return digest.toHex().toStdString(); }
  bool isValid() const { return !digest.isEmpty(); }

  bool operator==( const ExecutionFingerprint & ) const = default;
};

/// Tagged derivation input for canonical execution fingerprints (Workflow Engine 2.0).
/// @a revision has no default on purpose: a forgotten revision would hash two
/// materially different inputs identically and silently reuse a wrong cached
/// result. Callers must state it explicitly (use AssetRevision::initial() for
/// genuinely-initial assets).
struct TaggedDerivationInput
{
  AssetId assetId;
  AssetRevision revision;
  QString fromPort;
  QString toPort;
  QStringList bandReferences;
  QString valueDomain;
  QString lazyContentDigest;
  /// Chained in-pipeline producer identity (#726): for an input that is the
  /// output of an upstream workflow step, the producer step's own execution
  /// fingerprint hex. A pipeline-produced intermediate must not key on its
  /// file's catalog revision (the file is (re-)registered only after the
  /// producing run finishes, and revision bumps between identical runs would
  /// defeat convergence); the producer's fingerprint is exactly as strong —
  /// the determinism gate guarantees identical fingerprint ⇒ byte-identical
  /// output — and is computable before the producer's file is registered.
  /// When set, @a assetId/@a revision identify the producer edge, not a file.
  QString producerFingerprint;

  bool operator==( const TaggedDerivationInput & ) const = default;
};

/// Canonical JSON serializer with strictly sorted keys and shortest-round-trip
/// number formatting, suitable for hashing. RFC 8785 / JCS COMPATIBLE, NOT
/// byte-identical: distinct doubles always serialize distinctly (and -0
/// serializes as "0" per §3.2.2.3), but the exponent form follows Qt's 'g'
/// format — two-digit exponents (1e-07, not 1e-7) and a precision-dependent
/// fixed/exponential switch instead of the ES6 1e21/1e-6 thresholds — so the
/// bytes may differ from a strict JCS serializer. Self-consistent hashing is
/// what the cache relies on; cross-implementation byte equality is not claimed.
QByteArray canonicalizeJsonRfc8785( const QJsonObject &obj );

/// Build a fingerprint from the components. @a parameters is normalized (sorted
/// by key) before hashing so parameter-map insertion order does not affect the
/// fingerprint. Inputs are hashed as (assetId, revision) pairs in list order.
ExecutionFingerprint makeExecutionFingerprint( const QString &algorithmId,
                                               const QString &algorithmVersion,
                                               const QJsonObject &parameters,
                                               const QVector<DerivationInput> &inputs );

/// Workflow Engine 2.0: Canonical RFC 8785 fingerprint with TaggedDerivationInputs.
ExecutionFingerprint makeExecutionFingerprintV2( const QString &algorithmId,
                                                 const QString &algorithmVersion,
                                                 const QJsonObject &parameters,
                                                 const QVector<TaggedDerivationInput> &inputs );

/// Convenience: build a fingerprint directly from a DerivationRecord (carries
/// algorithmId/version/parameters/inputs).
inline ExecutionFingerprint fingerprintFromDerivation( const DerivationRecord &record )
{
  return makeExecutionFingerprint( record.algorithmId, record.algorithmVersion,
                                   record.parameters, record.inputs );
}

/// A minimal in-memory revision-aware result cache: fingerprint → output AssetId.
/// OFF by default (isEnabled() == false); call setEnabled(true) to activate.
/// Producers that opt in consult lookup() before running; on a hit they reuse
/// the stored output AssetId instead of re-executing. Thread-safe (recursive_mutex).
///
/// Bounded: the cache evicts least-recently-used entries once it exceeds
/// maxEntries() (default 4096), so it cannot grow without bound. Keeping the
/// map bounded also keeps any residual hashing risk negligible.
///
/// This is the vertical slice; persistence and producer wiring are follow-ups.
class ExecutionResultCache
{
public:
  static ExecutionResultCache &instance();

  /// Enable/disable the cache. Disabled (the default): lookup() always returns
  /// nullopt and store() is a no-op. Deterministic operators opt in here.
  void setEnabled( bool on );
  bool isEnabled() const;

  /// Set the maximum number of cached entries. Entries beyond this are evicted
  /// (least-recently-used first) on the next store(). Must be >= 1.
  void setMaxEntries( size_t maxEntries );
  size_t maxEntries() const;

  /// Look up a prior result by fingerprint. Returns nullopt when disabled or
  /// absent. The returned AssetId is the output asset of a prior identical run.
  std::optional<AssetId> lookup( const ExecutionFingerprint &fp ) const;

  /// Record a freshly-produced output for a fingerprint. No-op when disabled.
  /// May evict the least-recently-used entry if at capacity.
  void store( const ExecutionFingerprint &fp, const AssetId &outputAssetId );

  /// Remove one entry (e.g. when an output asset is deleted). No-op when absent.
  void invalidate( const ExecutionFingerprint &fp );

  // --- Execution result cache (#667/#726, Workflow Engine 2.0 wiring) -------
  // Pipeline steps produce plain files, not registered assets, so the
  // AssetId store above has no producer on the pipeline path. This parallel
  // store maps a fingerprint to the full RESULT of an identical prior step
  // (#726): the declared output path, every produced artifact (multi-output /
  // grouped shapes ride in the payload), and the JSON result payload itself.
  // Same enable gate, LRU bound, and thread-safety as the asset store.

  /// One cached execution result. @a producedArtifacts lists every file the
  /// producing run left behind (declared output + payload-referenced
  /// artifacts such as grouped period rasters). @a artifactSizes /
  /// @a artifactMsecs snapshot each produced artifact at store time so a
  /// lookup can refuse to serve an entry whose files were replaced by a
  /// different execution. @a resultPayload is the producing run's full
  /// result document, restored verbatim (with paths rewritten) on a hit.
  struct CachedExecution
  {
    QString declaredOutputPath;
    QStringList producedArtifacts;
    QMap<QString, qint64> artifactSizes; // path → byte size
    QMap<QString, qint64> artifactMsecs; // path → lastModified (ms epoch)
    /// Chained in-pipeline INPUT paths this execution consumed, with the
    /// stats observed at store time (#726 review): an intermediate corrupted
    /// between producer completion and consumer execution must invalidate the
    /// consumer's entry, exactly like a corrupted output would.
    QMap<QString, qint64> inputSizes; // path → byte size
    QMap<QString, qint64> inputMsecs; // path → lastModified (ms epoch)
    QJsonDocument resultPayload;
  };

  /// Look up a prior identical execution's result. Returns nullopt when
  /// disabled, absent, or the entry no longer validates (any produced file
  /// missing, or the declared output's size/mtime no longer match what was
  /// stored — a stale/poisoned entry self-heals by being erased). Deliberately
  /// NON-const: the self-heal erase and LRU recency touch mutate the cache.
  std::optional<CachedExecution> lookupExecution( const ExecutionFingerprint &fp );

  /// Record the result of a freshly-completed step. No-op when disabled or
  /// @a execution has no declared output. Evicting semantics: when another
  /// fingerprint already claims the same declared output path, that claim is
  /// removed — a path can only vouch for the bytes most recently written to
  /// it, so an older fingerprint's claim must never serve after a different
  /// execution overwrote the file.
  void storeExecution( const ExecutionFingerprint &fp, const CachedExecution &execution );

  /// Every file currently claimed by cached executions (declared outputs and
  /// produced artifacts, no particular order). Consumers (ArtifactGC) treat
  /// these as protected: a cached artifact is what a future identical
  /// execution reuses.
  QStringList cachedArtifacts() const;

  /// Number of cached executions (diagnostics / tests).
  int pathSize() const;

  /// Convenience wrappers over the execution store for single-artifact
  /// callers: store with a bare declared path and empty payload, look up the
  /// declared path only.
  void storeOutputPath( const ExecutionFingerprint &fp, const QString &outputPath );
  std::optional<QString> lookupOutputPath( const ExecutionFingerprint &fp );

  /// Clear all entries (test isolation / cache reset).
  void clear();

  /// Number of cached entries (diagnostics / tests).
  int size() const;

private:
  ExecutionResultCache() = default;
  struct Entry
  {
    AssetId asset;
    qint64 lastUsedTick = 0;
  };
  void evictIfNeededLocked();

  struct ExecutionEntry
  {
    CachedExecution execution;
    qint64 lastUsedTick = 0;
  };
  QHash<QByteArray, ExecutionEntry> m_executions;
  /// declared-output path → digest currently claiming it (path ownership,
  /// C7 of the cache contract). At most one claim per path.
  QHash<QString, QByteArray> m_pathOwners;

  mutable std::recursive_mutex m_mutex;
  // mutable: lookup() is logically const but touches LRU recency bookkeeping.
  mutable QHash<QByteArray, Entry> m_entries;
  bool m_enabled = false;
  size_t m_maxEntries = 4096;
  mutable qint64 m_clock = 0;
};

/// True when SICNU_EXECUTION_CACHE requests the execution cache for this
/// process ("1"/"true"/"yes"/"on"). Applied once, on the cache singleton's
/// first use; an explicit setEnabled() call always wins over the env (#667).
bool executionCacheEnabledFromEnv();

} // namespace sicnu::data
