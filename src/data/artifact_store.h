// artifact_store.h — Artifact identity store for the Data Plane 3.0 (Phase D).
//
// Weakens the implicit "path is identity" pattern: an artifact is a logical
// identity (ArtifactId + monotonic version per logical key) that points at a
// physical storage path, with content digest, producer fingerprint, lineage,
// references (what pins it), and lifecycle state bookkept in SQLite.
// QGIS/GDAL consumers keep receiving real paths — the store is metadata only
// and never unlinks payload bytes itself (ArtifactGC keeps that job).
//
// Storage model:
//   - SQLite database (WAL journal, schema-versioned) for all metadata.
//   - Payloads stay wherever their producer wrote them; the store records
//     (path, size, mtime, digest) so cache/GC/resume layers can validate.
//
// Invariants (see .planning/execution-data-plane-3/DATA_MODEL.md):
//   - registerArtifact() on an existing logical key bumps the version; the
//     ArtifactId stays stable across versions.
//   - A version row is immutable after registration except for lifecycle
//     state, lastTouch, digest backfill and ref bookkeeping.
//   - reapable() only returns trash-state artifacts with zero references and
//     a stale lastTouch — active artifacts can never be reaped (invariant I7).
#pragma once

#include "data_result.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

class Result;

namespace sicnu::data
{

struct ArtifactRegistration
{
  /// Stable identity of the LOGICAL artifact across versions. Callers choose
  /// the scheme (canonical path for path-anchored artifacts, a uuid for
  /// path-free artifacts). Required.
  QString logicalKey;
  /// Absolute filesystem path of the payload. Required.
  QString storagePath;
  /// Payload family ("raster", "vector", "virtual", "sidecar", …). Required.
  QString kind;
  /// Execution fingerprint hex of the producing run, when known.
  QString producerFingerprint;
  /// SHA-256 content digest hex, when known (0-cost to backfill later via
  /// updateContentDigest).
  QString contentDigest;
  /// Payload stat snapshot; 0 values are filled from the filesystem.
  qint64 sizeBytes = 0;
  qint64 mtimeMs = 0;
  /// Free-form caller metadata.
  QJsonObject metadata;
  /// Structured lineage (inputs, algorithm, refs to upstream artifacts).
  QJsonObject lineage;
  /// Metadata-only registrations (bookkeeping rows with no payload bytes,
  /// e.g. remote-source validator records) set this false: the payload
  /// existence check is skipped and size/mtime fall back to 0.
  bool requireExistingPayload = true;
};

struct ArtifactRecord
{
  QString artifactId;
  QString logicalKey;
  int version = 0;
  QString producerFingerprint;
  QString contentDigest;
  QString storagePath;
  QString kind;
  qint64 sizeBytes = 0;
  qint64 mtimeMs = 0;
  QJsonObject metadata;
  QJsonObject lineage;
  QString state;  ///< "live" | "retained" | "trash"
  qint64 createdAtMs = 0;
  qint64 lastTouchMs = 0;
};

struct ArtifactRef
{
  QString artifactId;
  QString refKind;  ///< "data_asset" | "cache_lease" | "checkpoint" | "run" | …
  QString refId;
  qint64 createdAtMs = 0;
};

/// Content digest helper: streaming SHA-256 over file bytes (hex, lowercase).
/// Empty string on failure; @p errorOut receives a diagnostic when set.
QString artifactContentDigest( const QString &path, QString *errorOut = nullptr );

class ArtifactStore
{
  public:
    ArtifactStore() = default;
    ~ArtifactStore();
    ArtifactStore( const ArtifactStore & ) = delete;
    ArtifactStore &operator=( const ArtifactStore & ) = delete;

    /// Opens (creating or migrating) the metadata database at @p dbPath.
    /// WAL journaling; safe against process crash. Refuses databases with a
    /// NEWER schema version than this build knows (forward-compat guard).
    bool open( const QString &dbPath, QString *errorOut = nullptr );
    void close();
    bool isOpen() const;

    /// Registers a new version of the logical artifact @p reg.logicalKey.
    /// Transactional: either a complete version row exists or nothing.
    Result<ArtifactRecord> registerArtifact( const ArtifactRegistration &reg );

    /// Backfills the content digest of an existing (latest) version.
    Result<void> updateContentDigest( const QString &artifactId, const QString &digest );

    std::optional<ArtifactRecord> artifactById( const QString &artifactId ) const;
    std::optional<ArtifactRecord> latestByLogicalKey( const QString &logicalKey ) const;
    std::optional<ArtifactRecord> latestByPath( const QString &storagePath ) const;
    QVector<ArtifactRecord> versionsByLogicalKey( const QString &logicalKey ) const;
    QVector<ArtifactRecord> byProducerFingerprint( const QString &fingerprint ) const;
    /// Any live artifact carrying this exact content digest (first match).
    std::optional<ArtifactRecord> liveByContentDigest( const QString &digest ) const;

    // -- References (what pins an artifact) --------------------------------
    Result<void> attachRef( const QString &artifactId, const QString &refKind,
                            const QString &refId );
    Result<void> detachRef( const QString &artifactId, const QString &refKind,
                            const QString &refId );
    int refCount( const QString &artifactId ) const;
    QVector<ArtifactRef> refsOf( const QString &artifactId ) const;

    // -- Lifecycle ----------------------------------------------------------
    /// live → retained (checked-in result that must survive beyond the run).
    Result<void> retain( const QString &artifactId );
    /// live/retained → trash (candidate for reclamation once refs drain).
    Result<void> markTrash( const QString &artifactId );
    /// Refreshes lastTouch (cache/GC liveness signal).
    bool touch( const QString &artifactId );

    /// Trash-state artifacts with zero references and lastTouch older than
    /// @p lastTouchCutoffMs (epoch ms). These are safe to delete physically;
    /// after the caller deletes payload bytes it should call forget() to drop
    /// the metadata row.
    QVector<ArtifactRecord> reapable( qint64 lastTouchCutoffMs ) const;
    /// Removes the metadata row (call AFTER deleting payload bytes).
    Result<void> forget( const QString &artifactId );

    // -- Maintenance ----------------------------------------------------------
    qint64 count() const;
    QString schemaVersion() const;

  private:
    struct Impl;
    Impl *m_impl = nullptr;
};

} // namespace sicnu::data
