// src/agent/commands/workspace_commands.cpp
#include "workspace_commands.h"

#include "../spatial_tools/spatial_tool.h"

#include <algorithm>
#include <memory>

namespace sicnu::agent::commands {

namespace {
constexpr size_t kMaxHistory = 100;
}

WorkspaceCommandStack &WorkspaceCommandStack::instance()
{
  static WorkspaceCommandStack stack;
  return stack;
}

QString WorkspaceCommandStack::beginTransaction( const QString &label )
{
  QMutexLocker lock( &mMutex );
  const QString id = QStringLiteral( "txn-%1" ).arg( mNextTransactionId++ );
  Transaction transaction;
  transaction.label = label;
  mOpenTransactions.insert( id, transaction );
  return id;
}

bool WorkspaceCommandStack::addCommand( const QString &transactionId, WorkspaceCommand command )
{
  QMutexLocker lock( &mMutex );
  auto it = mOpenTransactions.find( transactionId );
  if ( it == mOpenTransactions.end() )
    return false;
  if ( !command.redo || !command.undo )
    return false;
  it->commands.push_back( std::move( command ) );
  return true;
}

bool WorkspaceCommandStack::commit( const QString &transactionId, QString *error )
{
  Transaction transaction;
  {
    QMutexLocker lock( &mMutex );
    auto it = mOpenTransactions.find( transactionId );
    if ( it == mOpenTransactions.end() )
    {
      if ( error )
        *error = QStringLiteral( "unknown transaction '%1'" ).arg( transactionId );
      return false;
    }
    transaction = it.value();
    mOpenTransactions.erase( it );
  }

  for ( auto commandIt = transaction.commands.begin(); commandIt != transaction.commands.end();
        ++commandIt )
  {
    if ( !commandIt->redo() )
    {
      // Best-effort rollback of the commands already applied (reverse order).
      auto applied = commandIt;
      while ( applied != transaction.commands.begin() )
      {
        --applied;
        applied->undo();
      }
      if ( error )
        *error = QStringLiteral( "transaction '%1' failed at '%2'" ).arg( transactionId, commandIt->label );
      return false;
    }
  }

  QMutexLocker lock( &mMutex );
  mRedoStack.clear();
  mUndoStack.push_back( std::move( transaction ) );
  while ( mUndoStack.size() > kMaxHistory )
    mUndoStack.pop_front();
  return true;
}

bool WorkspaceCommandStack::undo()
{
  QMutexLocker lock( &mMutex );
  if ( mUndoStack.empty() )
    return false;
  Transaction transaction = mUndoStack.back();
  mUndoStack.pop_back();
  bool ok = true;
  for ( auto it = transaction.commands.rbegin(); it != transaction.commands.rend(); ++it )
    ok = it->undo() && ok;
  mRedoStack.push_back( std::move( transaction ) );
  return ok;
}

bool WorkspaceCommandStack::redo()
{
  QMutexLocker lock( &mMutex );
  if ( mRedoStack.empty() )
    return false;
  Transaction transaction = mRedoStack.back();
  mRedoStack.pop_back();
  bool ok = true;
  for ( auto &command : transaction.commands )
    ok = command.redo() && ok;
  mUndoStack.push_back( std::move( transaction ) );
  return ok;
}

Json::Value WorkspaceCommandStack::history( int limit ) const
{
  QMutexLocker lock( &mMutex );
  Json::Value history( Json::arrayValue );
  const int count = static_cast<int>( mUndoStack.size() );
  const int start = std::max( 0, count - std::clamp( limit, 1, static_cast<int>( kMaxHistory ) ) );
  for ( int i = count - 1; i >= start; --i )
  {
    Json::Value entry( Json::objectValue );
    entry["label"] = mUndoStack[i].label.toStdString();
    entry["commands"] = static_cast<Json::Int>( mUndoStack[i].commands.size() );
    history.append( entry );
  }
  return history;
}

void WorkspaceCommandStack::clear()
{
  QMutexLocker lock( &mMutex );
  mOpenTransactions.clear();
  mUndoStack.clear();
  mRedoStack.clear();
}

// ---------------------------------------------------------------------------
// workspace:* tools
// ---------------------------------------------------------------------------

namespace {

using sicnu::agent::spatial_tools::SpatialTool;
using sicnu::agent::spatial_tools::SpatialToolRegistry;
using sicnu::agent::spatial_tools::SpatialToolResult;

class WorkspaceUndoTool final : public SpatialTool
{
  public:
    std::string name() const override { return "workspace:undo"; }
    std::string displayName() const override { return "Undo workspace mutation"; }
    std::string description() const override
    {
      return "Undoes the most recent agent/symbology workspace mutation (renderer changes; "
             "layout edits keep their own QGIS-native undo stacks).";
    }
    std::vector<std::string> tags() const override { return { "workspace", "undo", "command" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["undone"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value & ) override
    {
      const bool ok = WorkspaceCommandStack::instance().undo();
      if ( !ok )
        return SpatialToolResult::failure( "Nothing to undo", "EMPTY", "runtime", false );
      Json::Value out( Json::objectValue );
      out["undone"] = true;
      return SpatialToolResult::ok( out );
    }
};

class WorkspaceRedoTool final : public SpatialTool
{
  public:
    std::string name() const override { return "workspace:redo"; }
    std::string displayName() const override { return "Redo workspace mutation"; }
    std::string description() const override { return "Re-applies the most recently undone mutation."; }
    std::vector<std::string> tags() const override { return { "workspace", "redo", "command" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["redone"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value & ) override
    {
      const bool ok = WorkspaceCommandStack::instance().redo();
      if ( !ok )
        return SpatialToolResult::failure( "Nothing to redo", "EMPTY", "runtime", false );
      Json::Value out( Json::objectValue );
      out["redone"] = true;
      return SpatialToolResult::ok( out );
    }
};

class WorkspaceHistoryTool final : public SpatialTool
{
  public:
    std::string name() const override { return "workspace:history"; }
    std::string displayName() const override { return "Workspace mutation history"; }
    std::string description() const override { return "Recent undoable mutation labels (bounded)."; }
    std::vector<std::string> tags() const override { return { "workspace", "history" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value limit( Json::objectValue );
      limit["type"] = "integer";
      props["limit"] = limit;
      schema["properties"] = props;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["entries"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const int limit = input.isMember( "limit" ) && input["limit"].isInt() ? input["limit"].asInt() : 20;
      Json::Value out( Json::objectValue );
      out["entries"] = WorkspaceCommandStack::instance().history( limit );
      return SpatialToolResult::ok( out );
    }
};

} // namespace

void registerWorkspaceCommandTools()
{
  static const bool registered = [] {
    auto &registry = SpatialToolRegistry::instance();
    registry.registerTool( std::make_shared<WorkspaceUndoTool>() );
    registry.registerTool( std::make_shared<WorkspaceRedoTool>() );
    registry.registerTool( std::make_shared<WorkspaceHistoryTool>() );
    return true;
  }();
  Q_UNUSED( registered );
}

} // namespace sicnu::agent::commands
