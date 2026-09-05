// relink_service.h — project relocation / portable project / bulk relink
// (Platform 3.0 Phase E).
//
// Paths are storage locators: moving a project or its data changes locators,
// never identities. This service detects missing locators, proposes repairs
// (root moves, fingerprint matches, explicit remaps) and applies them through
// DataManager::relocate so structure-compatibility gating stays authoritative.
#pragma once

#include "data/governance/governance_types.h"

#include <QMap>
#include <QString>
#include <QVector>

class QJsonObject;

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::workspace
{

class WorkspaceService;

struct MissingAsset
{
    QString assetId;
    QString path;
    QString displayName;
    QString kind;
};

struct RelinkOutcome
{
    int relinked = 0;
    int failed = 0;
    int skipped = 0;
    QVector<Diagnostic> diagnostics;
    QJsonObject toJson() const;
};

class RelinkService
{
  public:
    RelinkService( sicnu::data::DataManager &dataManager, WorkspaceService &service );

    /// Assets whose local locator no longer stats (remote locators excluded).
    QVector<MissingAsset> scanMissing() const;

    /// Asset ids whose stored fingerprint matches the content digest of
    /// @p candidatePath (dedup/repair lookup). At most @p limit ids.
    QStringList fingerprintCandidates( const QString &candidatePath, qint64 limit = 8 ) const;

    /// Relinks one asset to a new locator. Fails with a diagnostic when the
    /// DataManager rejects the replacement (structure incompatibility).
    bool relinkAsset( const QString &assetId, const QString &newPath, QVector<Diagnostic> *diagnostics = nullptr );

    /// Applies an external-root move: every missing local asset under
    /// @p fromRoot is remapped to @p toRoot + relative suffix when the target
    /// exists (and, when @p verifyFingerprint, matches the stored digest).
    RelinkOutcome applyRootMove( const QString &fromRoot, const QString &toRoot,
                                 bool verifyFingerprint = false );

    /// Explicit bulk remap assetId -> new path.
    RelinkOutcome relinkBulk( const QMap<QString, QString> &remap );

  private:
    bool applyRelink( const MissingAsset &missing, const QString &newPath, RelinkOutcome *outcome,
                      bool verifyFingerprint );

    sicnu::data::DataManager &m_dataManager;
    WorkspaceService &m_service;
};

} // namespace sicnu::workspace
