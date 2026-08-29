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
#include <QJsonObject>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>

namespace sicnu::data
{

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

  // --- Output-path cache (#667, Workflow Engine 2.0 wiring) ------------------
  // Pipeline steps produce plain files, not registered assets, so the
  // AssetId store above has no producer on the pipeline path. This parallel
  // store maps a fingerprint to the output FILE PATH of an identical prior
  // step. Same enable gate, LRU bound, and thread-safety as the asset store.

  /// Look up the output path of a prior identical execution. Returns nullopt
  /// when disabled, absent, or the cached file no longer exists on disk
  /// (self-healing against external deletion). The existence check keeps a
  /// stale entry from ever being served.
  std::optional<QString> lookupOutputPath( const ExecutionFingerprint &fp ) const;

  /// Record the output path of a freshly-completed step. No-op when disabled
  /// or the path is empty. May evict the least-recently-used path entry.
  void storeOutputPath( const ExecutionFingerprint &fp, const QString &outputPath );

  /// Number of cached output paths (diagnostics / tests).
  int pathSize() const;

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

  struct PathEntry
  {
    QString path;
    qint64 lastUsedTick = 0;
  };
  QHash<QByteArray, PathEntry> m_pathEntries;

  mutable std::recursive_mutex m_mutex;
  // mutable: lookup() is logically const but touches LRU recency bookkeeping.
  mutable QHash<QByteArray, Entry> m_entries;
  bool m_enabled = false;
  size_t m_maxEntries = 4096;
  mutable qint64 m_clock = 0;
};

} // namespace sicnu::data
