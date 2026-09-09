/***************************************************************************
 * command_help_source.h — CommandCatalogSource over the app CommandRegistry
 *
 * Thin read-only adapter: copies user-facing facts out of CommandRegistry at
 * query time. Lives in the app layer because it needs the registry type; the
 * help layer only ever sees the source interface.
 ***************************************************************************/
#pragma once

#include "help/help_catalog_source.h"

namespace sicnu::app
{

class CommandRegistry;

class CommandHelpSource : public sicnu::help::CommandCatalogSource
{
  public:
    explicit CommandHelpSource( const CommandRegistry &registry );

    QVector<sicnu::help::CommandFact> commands() const override;

  private:
    const CommandRegistry &m_registry;
};

} // namespace sicnu::app
