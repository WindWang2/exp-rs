/***************************************************************************
 * command_help_source.cpp — CommandRegistry → CommandFact adapter
 ***************************************************************************/
#include "app/help/command_help_source.h"

#include "workbench/command_registry.h"

namespace sicnu::app
{

CommandHelpSource::CommandHelpSource( const CommandRegistry &registry )
    : m_registry( registry )
{
}

QVector<sicnu::help::CommandFact> CommandHelpSource::commands() const
{
    QVector<sicnu::help::CommandFact> out;
    const QList<const CommandDefinition *> definitions = m_registry.definitions();
    out.reserve( definitions.size() );
    for ( const CommandDefinition *def : definitions ) {
        sicnu::help::CommandFact fact;
        fact.id = def->id;
        fact.title = def->title;
        fact.description = def->description;
        fact.category = def->category;
        fact.keywords = def->keywords;
        fact.shortcut = def->shortcut.toString( QKeySequence::NativeText );
        fact.destructive = def->destructive;
        fact.checkable = def->checkable;
        out.push_back( fact );
    }
    return out;
}

} // namespace sicnu::app
