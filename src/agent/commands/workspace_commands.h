// src/agent/commands/workspace_commands.h
#pragma once

//
// Phase N — unified Command/Transaction model for agent workspace mutations
// (visibility, symbology, chart registry changes). Layout mutations keep
// using QgsLayoutUndoStack (QGIS-native, shared with the designer GUI) and
// are deliberately NOT mirrored here.
//
// User actions and agent actions converge on the same mutation helpers, so
// agent edits are undoable exactly like GUI edits.
//

#include <json/json.h>

#include <QMap>
#include <QMutex>
#include <QString>
#include <vector>

#include <deque>
#include <functional>

namespace sicnu::agent::commands {

struct WorkspaceCommand
{
  QString label;
  std::function<bool()> redo; ///< Applies the mutation. False = failed.
  std::function<bool()> undo; ///< Restores the previous state.
};

class WorkspaceCommandStack
{
  public:
    static WorkspaceCommandStack &instance();

    /// Begins a transaction; returns its id ("txn-<n>").
    QString beginTransaction( const QString &label );

    /// Adds one command to an open transaction. All commands must be added
    /// before commit.
    bool addCommand( const QString &transactionId, WorkspaceCommand command );

    /// Executes the transaction's redo functions and pushes it onto the undo
    /// stack if all succeeded. False when any redo failed (transaction is
    /// dropped, partial effects are rolled back best-effort).
    bool commit( const QString &transactionId, QString *error = nullptr );

    bool undo();
    bool redo();

    Json::Value history( int limit = 20 ) const;
    void clear();

  private:
    WorkspaceCommandStack() = default;

    struct Transaction
    {
      QString label;
      std::vector<WorkspaceCommand> commands;
    };

    mutable QMutex mMutex;
    int mNextTransactionId = 1;
    QMap<QString, Transaction> mOpenTransactions;
    std::deque<Transaction> mUndoStack;
    std::deque<Transaction> mRedoStack;
};

/// Registers the workspace:* tools (workspace:undo / workspace:redo /
/// workspace:history). Idempotent; called from SpatialToolRegistry::registerBuiltinTools().
void registerWorkspaceCommandTools();

} // namespace sicnu::agent::commands
