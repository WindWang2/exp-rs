// src/agent/agent_context_resolver.h
#pragma once

#include <QJsonArray>
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
     * Generates a concise JSON workspace context snapshot containing:
     * 1. Active Data Assets in DataManager (Asset ID, display name, file path, kind, CRS, band count / layer count).
     * 2. Active selected layer in ActiveViewHost.
     * 3. Current map canvas CRS and extent.
     */
    static QJsonObject buildContextSnapshot( data::DataManager *dataManager, ActiveViewHost *viewHost = nullptr );

    /**
     * Formats the context snapshot into a concise string suitable for LLM System Prompt header.
     */
    static QString formatSystemContextPrompt( const QJsonObject &snapshot );
};

} // namespace sicnu::agent
