// src/workflow/workflow_registry.h
#pragma once

#include "workflow_definition.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::workflow {

class WorkflowRegistry
{
  public:
    void registerDefinition( WorkflowDefinition def );
    bool has( const std::string &id ) const;
    const WorkflowDefinition *find( const std::string &id ) const;
    std::vector<std::string> ids() const;

  private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, WorkflowDefinition> m_defs;
};

} // namespace sicnu::workflow
