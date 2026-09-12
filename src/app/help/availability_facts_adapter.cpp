/***************************************************************************
 * availability_facts_adapter.cpp — fact composition over ContextRules
 ***************************************************************************/
#include "app/help/availability_facts_adapter.h"

#include <QHash>

#include <algorithm>

namespace sicnu::app
{
namespace
{

const QVector<AvailabilityFactsAdapter::Requirement> &requirementsFor( const QString &commandId )
{
    using R = AvailabilityFactsAdapter::Requirement;
    static const QVector<R> layerSelected = {
        { tr("layer selected"), &ContextRules::layerSelected },
    };
    static const QVector<R> vectorSelected = {
        { tr("layer selected"), &ContextRules::layerSelected },
        { tr("layer is vector data"), &ContextRules::vectorSelected },
    };
    static const QVector<R> editingAvailable = {
        { tr("layer selected"), &ContextRules::layerSelected },
        { tr("layer is vector data"), &ContextRules::vectorSelected },
        { tr("layer supports editing sessions"), &ContextRules::editingAvailable },
    };
    static const QVector<R> editingActive = {
        { tr("layer selected"), &ContextRules::layerSelected },
        { tr("layer is vector data"), &ContextRules::vectorSelected },
        { tr("Editing session enabled"), &ContextRules::editingActive },
    };
    static const QVector<R> rasterSelected = {
        { tr("raster layer selected"), &ContextRules::rasterSelected },
    };
    static const QVector<R> sarRasterSelected = {
        { tr("raster layer selected"), &ContextRules::rasterSelected },
        { tr("data is SAR imagery"), &ContextRules::sarSelected },
    };
    static const QVector<R> none;

    static const QHash<QString, QVector<R>> table = {
        { QStringLiteral( "layer.properties" ), layerSelected },
        { QStringLiteral( "layer.remove" ), layerSelected },
        { QStringLiteral( "layer.zoomTo" ), layerSelected },
        { QStringLiteral( "layer.toggleEditing" ), editingAvailable },
        { QStringLiteral( "layer.saveEdits" ), editingActive },
        { QStringLiteral( "layer.attributeTable" ), vectorSelected },
        { QStringLiteral( "rs.bandMath" ), rasterSelected },
        { QStringLiteral( "rs.spectralIndex" ), rasterSelected },
        { QStringLiteral( "rs.contrastStretch" ), rasterSelected },
        { QStringLiteral( "rs.spatialFilter" ), rasterSelected },
        { QStringLiteral( "rs.pca" ), rasterSelected },
        { QStringLiteral( "rs.bandRatio" ), rasterSelected },
        { QStringLiteral( "rs.mosaic" ), rasterSelected },
        { QStringLiteral( "rs.changeDetection" ), rasterSelected },
        { QStringLiteral( "rs.atmospheric" ), rasterSelected },
        { QStringLiteral( "rs.qaMask" ), rasterSelected },
        { QStringLiteral( "rs.applyMask" ), rasterSelected },
        { QStringLiteral( "rs.radiometric" ), rasterSelected },
        { QStringLiteral( "rs.ortho" ), rasterSelected },
        { QStringLiteral( "rs.terrain" ), rasterSelected },
        { QStringLiteral( "rs.fusion" ), rasterSelected },
        { QStringLiteral( "rs.temporal" ), rasterSelected },
        { QStringLiteral( "rs.speckle" ), sarRasterSelected },
        { QStringLiteral( "rs.extractBands" ), rasterSelected },
    };

    const auto it = table.constFind( commandId );
    return it == table.constEnd() ? none : it.value();
}

QString suggestedActionFor( const QString &commandId )
{
    // The natural unblocking command for the common failure mode per family.
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
    // Command ids that have fact rows. Kept in one place so the drift test
    // (test_help_coverage) can verify each id exists in the shell command
    // table; new availability-gated commands must add their rows here.
    return {
        QStringLiteral( "layer.properties" ),  QStringLiteral( "layer.remove" ),
        QStringLiteral( "layer.zoomTo" ),      QStringLiteral( "layer.toggleEditing" ),
        QStringLiteral( "layer.saveEdits" ),   QStringLiteral( "layer.attributeTable" ),
        QStringLiteral( "rs.bandMath" ),       QStringLiteral( "rs.spectralIndex" ),
        QStringLiteral( "rs.contrastStretch" ), QStringLiteral( "rs.spatialFilter" ),
        QStringLiteral( "rs.pca" ),            QStringLiteral( "rs.bandRatio" ),
        QStringLiteral( "rs.mosaic" ),         QStringLiteral( "rs.changeDetection" ),
        QStringLiteral( "rs.atmospheric" ),    QStringLiteral( "rs.qaMask" ),
        QStringLiteral( "rs.applyMask" ),      QStringLiteral( "rs.radiometric" ),
        QStringLiteral( "rs.ortho" ),          QStringLiteral( "rs.terrain" ),
        QStringLiteral( "rs.fusion" ),         QStringLiteral( "rs.temporal" ),
        QStringLiteral( "rs.speckle" ),        QStringLiteral( "rs.extractBands" ),
    };
}

sicnu::help::AvailabilityExplanation AvailabilityFactsAdapter::explain(
    const SelectionContextSnapshot &snapshot, const QString &commandId )
{
    sicnu::help::AvailabilityExplanation explanation;
    explanation.commandId = commandId;

    for ( const Requirement &requirement : requirementsFor( commandId ) ) {
        sicnu::help::AvailabilityFact fact;
        fact.label = QString::fromUtf8( requirement.label );
        fact.satisfied = requirement.predicate( snapshot );
        explanation.facts.append( fact );
    }
    explanation.available = std::all_of( explanation.facts.cbegin(), explanation.facts.cend(),
                                         []( const sicnu::help::AvailabilityFact &f ) {
                                             return f.satisfied;
                                         } );

    if ( !explanation.available ) {
        explanation.suggestedCommandId = suggestedActionFor( commandId );
        explanation.flatReason = ContextRules::unavailabilityReason( snapshot, commandId );
        // suggested title resolved by the caller from the command registry —
        // keep only the id here to avoid a registry dependency.
    }
    return explanation;
}

} // namespace sicnu::app
