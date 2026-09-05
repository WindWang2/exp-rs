// artifact_object_pool.h — Persistent content-addressed artifact cache
// (Data Plane 3.0, Phase E). Backs the in-memory ExecutionResultCache with a
// cross-session, cross-process tier:
//
//   <root>/store.sqlite3            ArtifactStore metadata (Phase D)
//   <root>/objects/<aa>/<digest>    immutable payload objects (SHA-256)
//
// Safety rules (scientific correctness over hit rate):
//   - an object is only addressable by its own SHA-256; put() stages to a
//     temp file and renames, so a partial copy never becomes an object;
//   - lookup re-verifies every object digest (conservative default; corruption
//     self-heals by dropping the execution) — FAILURE_MATRIX "cache corruption";
//   - executions are recorded atomically per object and looked up only when
//     COMPLETE (declared + every produced artifact present and verified);
//   - the pool never mutates objects; eviction removes whole executions whose
//     objects are unreferenced and stale (ArtifactStore reapable semantics).
#pragma once

#include "artifact_store.h"

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVector>

#include <optional>

namespace sicnu::data
{

struct PoolObject
{
    QString originalPath;   ///< path the producing run wrote (identity for mapping)
    QString digest;         ///< SHA-256 hex
    QString poolPath;       ///< object path in the pool
    qint64 size = 0;
    qint64 msecs = 0;       ///< pool object mtime at put time
    bool declared = false;  ///< true for the declared output, false for siblings
};

struct PoolExecution
{
    QString declaredOriginal;                 ///< producing run's declared output
    QVector<PoolObject> objects;              ///< declared first, then siblings
    QByteArray payloadJson;                   ///< producing run's full result payload
    QMap<QString, qint64> inputSizes;         ///< chained-input stats at store time
    QMap<QString, qint64> inputMsecs;
};

class ArtifactObjectPool
{
  public:
    ArtifactObjectPool() = default;
    ~ArtifactObjectPool();
    ArtifactObjectPool( const ArtifactObjectPool & ) = delete;
    ArtifactObjectPool &operator=( const ArtifactObjectPool & ) = delete;

    /// Enables the pool under @p rootDir (created on demand). Returns false
    /// when the backing store cannot be opened.
    bool enable( const QString &rootDir, QString *errorOut = nullptr );
    bool isEnabled() const { return m_enabled; }

    /// Copies @p filePath into the pool. Fail-closed: an object exists only
    /// after its bytes are fully staged and the digest recomputed from the
    /// staged copy matches the source.
    std::optional<PoolObject> put( const QString &filePath, bool declared );

    /// Records a complete execution (declared + produced objects + payload).
    /// Atomic per record; a partially-recorded execution is never returned by
    /// lookupExecution (completeness is part of the record contract).
    bool recordExecution( const QString &fingerprintHex, const PoolExecution &execution );

    /// Returns the recorded execution for @p fingerprintHex, or nullopt when
    /// absent, incomplete, or any object fails digest re-verification.
    std::optional<PoolExecution> lookupExecution( const QString &fingerprintHex );

    /// Drops the execution's records and removes objects that no other
    /// execution references. Returns false on metadata errors only.
    bool forgetExecution( const QString &fingerprintHex );

    /// Total bytes occupied by pool objects (from the metadata store).
    qint64 totalObjectBytes() const;

    /// Bytes beyond @p maxBytes evict whole stale, unreferenced executions
    /// (oldest lastTouch first). Returns bytes freed.
    qint64 evictToBytes( qint64 maxBytes );

  private:
    struct Counters;
    bool m_enabled = false;
    QString m_root;
    QString m_objectsDir;
    ArtifactStore m_store;
};

} // namespace sicnu::data
