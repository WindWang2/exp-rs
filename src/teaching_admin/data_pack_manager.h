// data_pack_manager.h — Data Pack inventory, checksum, traversal, byte budget.
// Read-only source; no network download. Calls pack scripts via structured adapter.
#pragma once

#include "admin_types.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sicnu::teaching_admin {

struct PackInventoryEntry
{
    QString packId;
    QString path;
    QString labId;
    QString packVersion;
    qint64 declaredBytes = -1;
    qint64 computedBytes = -1;
    QString digest; ///< sha256 of pack JSON canonical bytes (document identity)
    bool offlineAvailable = false;
    QString generator;
    QString crsGridSummary;
    QVector<AdminIssue> issues;

    QJsonObject toJson() const;
};

struct PackInventory
{
    QVector<PackInventoryEntry> packs;
    qint64 totalDeclaredBytes = 0;
    qint64 byteBudget = 0;
    bool withinBudget = true;
    QJsonObject toJson() const;
};

/// Inventory packs under packsDir. Enforces path traversal prevention on every
/// input path inside each pack. @p repoRoot used to resolve relative inputs.
PackInventory inventoryPacks( const QString &packsDir, const QString &repoRoot,
                              qint64 byteBudgetBytes = 250LL * 1024 * 1024 );

/// Validate a single pack document object (schema + traversal + duplicate paths).
ValidationResult validatePackDocument( const QJsonObject &pack, const QString &repoRoot );

} // namespace sicnu::teaching_admin
