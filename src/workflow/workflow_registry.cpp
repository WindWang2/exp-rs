// src/workflow/workflow_registry.cpp
#include "workflow_registry.h"

namespace sicnu::workflow {

void WorkflowRegistry::registerDefinition( WorkflowDefinition def )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const std::string id = def.id;
  m_defs.insert_or_assign( id, std::move( def ) );
}

bool WorkflowRegistry::has( const std::string &id ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_defs.find( id ) != m_defs.end();
}

const WorkflowDefinition *WorkflowRegistry::find( const std::string &id ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_defs.find( id );
  if ( it == m_defs.end() )
    return nullptr;
  return &it->second;
}

std::vector<std::string> WorkflowRegistry::ids() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  std::vector<std::string> out;
  out.reserve( m_defs.size() );
  for ( const auto &kv : m_defs )
    out.push_back( kv.first );
  return out;
}

} // namespace sicnu::workflow
