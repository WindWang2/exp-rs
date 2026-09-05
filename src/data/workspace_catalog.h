// workspace_catalog.h — Persistent workspace catalog (Data Plane 3.0,
// Phase I). SQLite-backed metadata store for very large asset collections
// (design point: 100k+ live records; structure stays reasonable toward 1M):
//
//   - indexed point lookups (by id, by path alias — no stat storms);
//   - paged queries (LIMIT/OFFSET + total count) so GUI/agent surfaces never
//     materialize 100k widgets;
//   - batched, transactional mutation (bulk import = one transaction);
//   - mirroring bridge for the in-memory DataManager (the catalog is a
//     durable index, DataManager stays the runtime authority).
//
// Page size discipline (MEMORY_BUDGET.md): queries always carry LIMIT; the
// paged list model fetches on demand for the GUI.
#pragma once

#include "data_result.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace sicnu::data
{

struct CatalogAsset
{
    QString assetId;              ///< DataManager AssetId (string form)
    QString sourceKey;            ///< canonical identity key (dedup)
    QString canonicalSource;      ///< primary path/URL
    QString kind;                 ///< "raster" | "vector" | ...
    QString state;                ///< "Ready" | "Missing" | ...
    QString persistence;          ///< "project" | "session" | "task"
    QString displayName;
    QString parentCollectionId;
    qint64 acquisitionMs = 0;     ///< 0 = unknown
    quint64 revision = 1;
    QString metadataJson;         ///< opaque structured summary
    QStringList aliases;          ///< alternative path spellings (vsicurl etc.)
    QStringList tags;
};

struct CatalogQuery
{
    QString kind;                 ///< empty = any
    QString state;                ///< empty = any
    QString textPrefix;           ///< display name / path prefix (LIKE prefix%)
    QString parentCollectionId;   ///< empty = any
};

struct CatalogPage
{
    qint64 total = 0;             ///< total matching rows (for paging UI)
    QVector<CatalogAsset> items;
};

class WorkspaceCatalog
{
  public:
    WorkspaceCatalog() = default;
    ~WorkspaceCatalog();
    WorkspaceCatalog( const WorkspaceCatalog & ) = delete;
    WorkspaceCatalog &operator=( const WorkspaceCatalog & ) = delete;

    bool open( const QString &dbPath, QString *errorOut = nullptr );
    void close();
    bool isOpen() const { return m_impl != nullptr; }

    /// Upserts by assetId. @p aliases and @p tags replace the stored sets.
    Result<void> upsertAsset( const CatalogAsset &asset );

    /// Bulk upsert inside one transaction (bulk import path).
    Result<void> upsertAssets( const QVector<CatalogAsset> &assets );

    Result<void> removeAsset( const QString &assetId );

    std::optional<CatalogAsset> byId( const QString &assetId ) const;
    /// Alias lookup (indexed) — the O(1)-ish replacement for the O(N)-stat
    /// findByPath scan.
    std::optional<CatalogAsset> byPath( const QString &path ) const;

    /// Paged, filtered listing. @p offset >= 0, @p limit > 0 (clamped to
    /// kMaxPageSize). total = full match count for stable paging.
    CatalogPage page( const CatalogQuery &query, qint64 offset, qint64 limit ) const;

    qint64 count() const;
    QString schemaVersion() const;

    static constexpr qint64 kMaxPageSize = 500;

  private:
    struct Impl;
    Impl *m_impl = nullptr;
};

/// Mirrors DataManager mutations into a WorkspaceCatalog. The catalog is a
/// durable index; the DataManager stays the runtime authority — removal of an
/// in-memory asset removes the catalog row (assets are session-scoped truth),
/// while the catalog additionally survives restarts when fed on registration.
class DataManagerCatalogBridge
{
  public:
    DataManagerCatalogBridge( WorkspaceCatalog &catalog ) : m_catalog( catalog ) {}

    Result<void> mirrorUpsert( const CatalogAsset &asset );
    Result<void> mirrorRemove( const QString &assetId );

  private:
    WorkspaceCatalog &m_catalog;
};

} // namespace sicnu::data
