/***************************************************************************
 * availability_facts_adapter.cpp — fact composition over ContextRules
 *
 * F20 (work package C): the facts channel derives from the SAME source as
 * the enabled state — ContextRules::requirementFacts, the single derivation
 * behind ContextRules::unavailabilityReason and the CommandRegistry
 * availability predicates. Every command that declares a predicate therefore
 * gets structured facts (with stable machine codes), and a disabled command
 * can never report empty facts (= silently "available") again. The old
 * hand-maintained 24-row table missed workbench.obia entirely and drifted
 * from the predicate definitions.
 ***************************************************************************/
#include "app/help/availability_facts_adapter.h"

#include <QHash>

#include <algorithm>

namespace sicnu::app
{
namespace
{

/// Natural unblocking command for the common failure mode per command
/// family. Deliberately small: ContextRules::suggestedNextAction covers the
/// workspace-level guidance; this table only names the direct antidote.
QString suggestedActionFor( const QString &commandId )
{
    static const QHash<QString, QString> suggestions = {
        { QStringLiteral( "layer.saveEdits" ), QStringLiteral( "layer.toggleEditing" ) },
        { QStringLiteral( "layer.attributeTable" ), QStringLiteral( "layer.addVector" ) },
        { QStringLiteral( "layer.toggleEditing" ), QStringLiteral( "layer.addVector" ) },
    };
    if ( commandId.startsWith( QLatin1String( "rs." ) ) )
        return QStringLiteral( "layer.addRaster" );
    return suggestions.value( commandId );
}

} // namespace

QStringList AvailabilityFactsAdapter::coveredCommandIds()
{
    // Command ids whose availability derives from requirementFacts rows (the
    // exact-command and family cases in ContextRules). Kept in one place so
    // the drift test (test_help_coverage) can verify each id exists in the
    // shell command table; must stay in sync with the d.availability
    // declarations in command_defs.cpp.
    return {
        QStringLiteral( "layer.properties" ),   QStringLiteral( "layer.remove" ),
        QStringLiteral( "layer.zoomTo" ),       QStringLiteral( "layer.toggleEditing" ),
        QStringLiteral( "layer.saveEdits" ),    QStringLiteral( "layer.attributeTable" ),
        QStringLiteral( "rs.bandMath" ),        QStringLiteral( "rs.spectralIndex" ),
        QStringLiteral( "rs.contrastStretch" ), QStringLiteral( "rs.spatialFilter" ),
        QStringLiteral( "rs.pca" ),             QStringLiteral( "rs.bandRatio" ),
        QStringLiteral( "rs.mosaic" ),          QStringLiteral( "rs.changeDetection" ),
        QStringLiteral( "rs.atmospheric" ),     QStringLiteral( "rs.qaMask" ),
        QStringLiteral( "rs.applyMask" ),       QStringLiteral( "rs.radiometric" ),
        QStringLiteral( "rs.ortho" ),           QStringLiteral( "rs.terrain" ),
        QStringLiteral( "rs.fusion" ),          QStringLiteral( "rs.temporal" ),
        QStringLiteral( "rs.speckle" ),         QStringLiteral( "rs.extractBands" ),
    };
}

sicnu::help::AvailabilityExplanation AvailabilityFactsAdapter::explain(
    const SelectionContextSnapshot &snapshot, const QString &commandId )
{
    sicnu::help::AvailabilityExplanation explanation;
    explanation.commandId = commandId;

    const QVector<ContextRules::RequirementFact> rows =
        ContextRules::requirementFacts( snapshot, commandId );
    for ( const ContextRules::RequirementFact &row : rows ) {
        sicnu::help::AvailabilityFact fact;
        fact.code = row.code;
        fact.label = row.label;
        fact.satisfied = row.satisfied;
        explanation.facts.append( fact );
        if ( !fact.satisfied && explanation.reasonCode.isEmpty() )
            explanation.reasonCode = fact.code;
    }
    explanation.available = std::all_of( explanation.facts.cbegin(), explanation.facts.cend(),
                                         []( const sicnu::help::AvailabilityFact &f ) {
                                             return f.satisfied;
                                         } );

    if ( !explanation.available ) {
        // same derivation as unavailabilityReason — identical text by design
        explanation.flatReason = ContextRules::unavailabilityReason( snapshot, commandId );
        explanation.suggestedCommandId = suggestedActionFor( commandId );
        // suggested title resolved by the caller from the command registry —
        // keep only the id here to avoid a registry dependency.
    }
    return explanation;
}

} // namespace sicnu::app
