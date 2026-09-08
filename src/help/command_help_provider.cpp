/***************************************************************************
 * command_help_provider.cpp — command descriptor composition
 ***************************************************************************/
#include "help/command_help_provider.h"

#include "help/help_catalog_source.h"

#include <QSet>

namespace sicnu::help
{
namespace
{

/// Applies additive knowledge (from commands.json) onto the derived descriptor.
/// Knowledge never overrides registry-owned identity fields (id/kind); title/
/// summary fall back to knowledge only when the registry has none.
void applyKnowledge( HelpDescriptor &derived, const HelpDescriptor &knowledge )
{
    if ( derived.title.isEmpty() )
        derived.title = knowledge.title;
    if ( derived.summary.isEmpty() )
        derived.summary = knowledge.summary;
    if ( derived.category.isEmpty() )
        derived.category = knowledge.category;
    for ( const QString &keyword : knowledge.keywords ) {
        if ( !derived.keywords.contains( keyword ) )
            derived.keywords << keyword;
    }
    for ( const QString &related : knowledge.relatedIds ) {
        if ( !derived.relatedIds.contains( related ) )
            derived.relatedIds << related;
    }
    for ( const QString &diag : knowledge.diagnosticIds ) {
        if ( !derived.diagnosticIds.contains( diag ) )
            derived.diagnosticIds << diag;
    }
    for ( const QString &doc : knowledge.docRefs ) {
        if ( !derived.docRefs.contains( doc ) )
            derived.docRefs << doc;
    }
    if ( knowledge.command.has_value() )
        derived.command = knowledge.command;
}

} // namespace

void CommandHelpProvider::compose( const CommandCatalogSource &source, const HelpRegistry &knowledge,
                                   HelpRegistry &out, QStringList *errors )
{
    auto report = [errors]( const QString &message ) {
        if ( errors )
            *errors << message;
    };

    QSet<QString> covered;
    const QVector<CommandFact> facts = source.commands();
    for ( const CommandFact &fact : facts ) {
        HelpDescriptor d;
        d.id = QStringLiteral( "command.%1" ).arg( fact.id );
        if ( !HelpId::isValid( d.id ) ) {
            report( QStringLiteral( "command id not help-grammar compatible: %1" ).arg( d.id ) );
            continue;
        }
        d.kind = HelpKind::Command;
        d.title = fact.title;
        d.summary = fact.description;
        d.category = fact.category;
        d.keywords = fact.keywords;

        if ( const HelpDescriptor *k = knowledge.find( d.id ) )
            applyKnowledge( d, *k );
        covered.insert( d.id );

        QString error;
        if ( !out.upsertDescriptor( std::move( d ), &error ) )
            report( error );
    }

    // knowledge entries without a real command = stale content (drift signal)
    for ( const HelpDescriptor *k : knowledge.byKind( HelpKind::Command ) ) {
        if ( !covered.contains( k->id ) )
            report( QStringLiteral( "command knowledge without registered command: %1" ).arg( k->id ) );
    }
}

} // namespace sicnu::help
