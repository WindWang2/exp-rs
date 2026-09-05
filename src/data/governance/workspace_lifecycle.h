// workspace_lifecycle.h — snapshot / cleanup / transactions
// (Platform 3.0 Phases P, Q, O).
//
// Snapshot: lightweight project metadata snapshots (project file + governance
// DB + workspace JSON). Never copies source imagery.
//
// Cleanup: distinguishes source assets / managed artifacts / temporary
// artifacts / cache / exports. A plan is always non-destructive; execution
// only removes CATALOG ROWS whose payload is already gone and that nothing
// references. Physical file deletion is never implicit.
//
// Transactions: undo/redo over logical governance mutations. The invariant
// "remove from project != delete physical file" is structural: no command in
// this stack deletes payload bytes.
#pragma once

#include "data/governance/governance_types.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace sicnu::workspace
{

class WorkspaceService;

// ---------------------------------------------------------------------------
// Snapshot (Phase P)
// ---------------------------------------------------------------------------

struct SnapshotReport
{
    QString snapshotPath;
    bool ok = false;
    QString error;
    int pruned = 0;
};

class SnapshotService
{
  public:
    explicit SnapshotService( WorkspaceService &service );

    /// Copies the project file + governance DB into
    /// @p snapshotDir/<base>-<timestamp>.snapshot/ and prunes older snapshots
    /// beyond @p keep. WAL is checkpointed first so the DB copy is consistent.
    SnapshotReport createSnapshot( const QString &projectFile, const QString &snapshotDir, int keep = 10 );

  private:
    WorkspaceService &m_service;
};

// ---------------------------------------------------------------------------
// Cleanup (Phase Q)
// ---------------------------------------------------------------------------

struct CleanupOptions
{
    /// Also propose results whose artifacts are all gone.
    bool includeStaleResults = true;
    /// Rows older than this (ms epoch) are considered; 0 = all.
    qint64 olderThanMs = 0;
};

struct CleanupReport
{
    /// Non-destructive listing of what would be cleaned and why.
    QVector<GovernanceDiagnostic> candidates;
    int protectedCount = 0;
    int removedRows = 0;
    /// Executes the removal of catalog rows whose payloads are missing and
    /// that no lineage/results reference. NEVER deletes physical files.
    int execute( WorkspaceService &service );
    QJsonObject toJson() const;
};

class CleanupService
{
  public:
    CleanupService( WorkspaceService &service );

    CleanupReport plan( const CleanupOptions &options = CleanupOptions() ) const;

  private:
    WorkspaceService &m_service;
};

// ---------------------------------------------------------------------------
// Transactions / undo (Phase O)
// ---------------------------------------------------------------------------

class WorkspaceCommand
{
  public:
    virtual ~WorkspaceCommand() = default;
    /// Applies the change; false means "not applied" (the stack stays intact).
    virtual bool apply( WorkspaceService &service ) = 0;
    /// Restores the pre-apply state.
    virtual bool revert( WorkspaceService &service ) = 0;
    virtual QString describe() const = 0;
};

class WorkspaceTransactionStack : public QObject
{
    Q_OBJECT

  public:
    explicit WorkspaceTransactionStack( WorkspaceService &service, QObject *parent = nullptr );

    bool execute( std::unique_ptr<WorkspaceCommand> command );
    bool undo();
    bool redo();
    void clear();

    int undoDepth() const { return ( int ) m_undo.size(); }
    int redoDepth() const { return ( int ) m_redo.size(); }

  signals:
    void changed();

  private:
    WorkspaceService &m_service;
    std::vector<std::unique_ptr<WorkspaceCommand>> m_undo;
    std::vector<std::unique_ptr<WorkspaceCommand>> m_redo;
};

/// Tag mutation (undoable via full tag-set snapshots).
class SetTagsCommand : public WorkspaceCommand
{
  public:
    SetTagsCommand( QString entityKind, QString entityId, QStringList newTags, QString actor );
    bool apply( WorkspaceService &service ) override;
    bool revert( WorkspaceService &service ) override;
    QString describe() const override;

  private:
    bool setTags( WorkspaceService &service, const QStringList &tags );
    QString m_entityKind;
    QString m_entityId;
    QStringList m_newTags;
    QStringList m_previousTags;
    bool m_captured = false;
    QString m_actor;
};

/// Dataset membership removal (undoable re-add; never touches assets).
class RemoveDatasetMemberCommand : public WorkspaceCommand
{
  public:
    RemoveDatasetMemberCommand( QString datasetId, QString assetId, QString actor );
    bool apply( WorkspaceService &service ) override;
    bool revert( WorkspaceService &service ) override;
    QString describe() const override;

  private:
    QString m_datasetId;
    QString m_assetId;
    QString m_actor;
    int m_previousPosition = -1;
    bool m_wasMember = false;
};

} // namespace sicnu::workspace
