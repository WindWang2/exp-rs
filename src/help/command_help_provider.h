/***************************************************************************
 * command_help_provider.h — composes command help descriptors
 *
 * Derives the base tier of every command descriptor from the authoritative
 * CommandCatalogSource (id, title, description→summary, category, keywords,
 * shortcut) and merges the additive knowledge layer (purpose, prerequisites,
 * suggested next action, related, docs) from the content store. The provider
 * never *decides* availability — that remains ContextRules' job; it only
 * presents it (see availability_facts.h).
 ***************************************************************************/
#pragma once

#include "help/help_descriptor.h"
#include "help/help_registry.h"

#include <QString>
#include <QStringList>

#include <QVector>

namespace sicnu::help
{

class CommandCatalogSource;

class CommandHelpProvider
{
  public:
    /// Composes descriptors for every command in @p source and registers them
    /// into @p out. @p knowledge holds additive entries (commands.json);
    /// knowledge entries without a matching command are reported into
    /// @p errors (drift signal: renamed/removed commands).
    static void compose( const CommandCatalogSource &source, const HelpRegistry &knowledge,
                         HelpRegistry &out, QStringList *errors = nullptr );
};

} // namespace sicnu::help
