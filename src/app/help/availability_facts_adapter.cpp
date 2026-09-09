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
        { "已选中图层", &ContextRules::layerSelected },
    };
    static const QVector<R> vectorSelected = {
        { "已选中图层", &ContextRules::layerSelected },
        { "图层为矢量数据", &ContextRules::vectorSelected },
    };
    static const QVector<R> editingAvailable = {
        { "已选中图层", &ContextRules::layerSelected },
        { "图层为矢量数据", &ContextRules::vectorSelected },
        { "图层支持编辑会话", &ContextRules::editingAvailable },
    };
    static const QVector<R> editingActive = {
        { "已选中图层", &ContextRules::layerSelected },
        { "图层为矢量数据", &ContextRules::vectorSelected },
        { "编辑会话已开启", &ContextRules::editingActive },
    };
    static const QVector<R> rasterSelected = {
        { "已选中栅格图层", &ContextRules::rasterSelected },
    };
    static const QVector<R> sarRasterSelected = {
        { "已选中栅格图层", &ContextRules::rasterSelected },
        { "数据为 SAR 影像", &ContextRules::sarSelected },
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
