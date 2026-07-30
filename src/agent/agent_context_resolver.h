// src/agent/agent_context_resolver.h
#pragma once

#include "workspace_snapshot.h"
#include <QJsonObject>
#include <QString>

namespace sicnu::data
{
class DataManager;
}

class ActiveViewHost;

namespace sicnu::agent
{

class AgentContextResolver
{
  public:
    /**
     * Generates a concise JSON workspace context snapshot using WorkspaceSnapshot::capture().
     */
    static QJsonObject buildContextSnapshot( data::DataManager *dataManager, ActiveViewHost *viewHost = nullptr );

    /**
     * Formats the context snapshot into a concise string suitable for LLM System Prompt header.
     */
    static QString formatSystemContextPrompt( const QJsonObject &snapshot );
    static QString formatSystemContextPrompt( const WorkspaceSnapshot &snapshot );
};

} // namespace sicnu::agent
