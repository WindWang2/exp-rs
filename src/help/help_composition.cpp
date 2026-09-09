/***************************************************************************
 * help_composition.cpp — composition root implementation
 ***************************************************************************/
#include "help/help_composition.h"

#include "help/command_help_provider.h"
#include "help/help_catalog_source.h"
#include "help/help_content_store.h"
#include "help/operator_help_provider.h"

namespace sicnu::help
{

CompositionReport composeHelpSystem( HelpRegistry &target,
                                     const CommandCatalogSource *commandSource,
                                     const OperatorCatalogSource *operatorSource,
                                     const QString &extraContentDir )
{
    // First composition wins: re-running on a populated target would report
    // duplicate-id errors for identical content. The early-out makes the
    // operation genuinely idempotent.
    if ( target.count() > 0 || target.aliasCount() > 0 ) {
        CompositionReport noop;
        noop.descriptors = target.count();
        noop.aliases = target.aliasCount();
        noop.dangling = target.validateReferences();
        return noop;
    }

    CompositionReport report;

    // 1. Embedded knowledge layer (content authored under data/help/**).
    HelpContentStore::LoadResult embedded = HelpContentStore::loadFromResources();
    report.errors += embedded.errors;
    // merge into target; duplicates across content files are content bugs
    {
        QStringList mergeErrors;
        target.mergeFrom( embedded.registry, &mergeErrors );
        for ( const QString &error : mergeErrors )
            report.errors << QStringLiteral( "embedded content: %1" ).arg( error );
    }

    // 2. Optional extra content directory (lab/local overrides, tests).
    if ( !extraContentDir.isEmpty() ) {
        HelpContentStore::LoadResult extra = HelpContentStore::loadFromDirectory( extraContentDir );
        report.errors += extra.errors;
        QStringList mergeErrors;
        target.mergeFrom( extra.registry, &mergeErrors );
        for ( const QString &error : mergeErrors )
            report.errors << QStringLiteral( "extra content: %1" ).arg( error );
    }

    // 3. Derived descriptors from the authoritative catalogs, merged with the
    //    additive knowledge above. Providers read the knowledge snapshot and
    //    register derived descriptors into the live registry — no mutation of
    //    the container being iterated.
    const HelpRegistry knowledgeSnapshot = target;
    QStringList providerErrors;
    if ( commandSource )
        CommandHelpProvider::compose( *commandSource, knowledgeSnapshot, target, &providerErrors );
    if ( operatorSource )
        OperatorHelpProvider::compose( *operatorSource, knowledgeSnapshot, target, &providerErrors );
    report.errors += providerErrors;

    report.descriptors = target.count();
    report.aliases = target.aliasCount();
    report.dangling = target.validateReferences();
    return report;
}

} // namespace sicnu::help
