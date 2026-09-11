// test_context_facts_8.cpp — Professional Workbench 8.0 (package C)
//
// Pins the widened context facts (broken layer, in-flight task) and the
// deterministic suggestedNextAction projection. Every suggested command id
// must be a registered registry command (pinned here against the literal
// ids in command_defs.cpp) — a suggestion that cannot execute is noise.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCoreApplication>

#include <qgsmaplayer.h>
#include <qgsvectorlayer.h>

#include "app/workbench/selection_context.h"

namespace
{

QApplication &testApp()
{
    static int argc = 0;
    static QApplication app( argc, nullptr );
    return app;
}

namespace ContextRules = sicnu::app::ContextRules;
using sicnu::app::SelectionContextSnapshot;

/// An editable memory vector layer (valid, writable) for edit predicates.
QgsVectorLayer *memoryLayer()
{
    auto *layer = new QgsVectorLayer( QStringLiteral( "Point?crs=EPSG:4326" ),
                                      QStringLiteral( "points" ), QStringLiteral( "memory" ) );
    return layer;
}

} // namespace

TEST_CASE( "prerequisiteFacts exposes the 8.0 fields", "[wb8][context]" )
{
    testApp();
    SelectionContextSnapshot snap;
    snap.hasBroken = true;
    snap.hasInFlightTask = true;

    const sicnu::app::ContextRules::ContextFacts facts =
      ContextRules::prerequisiteFacts( snap );
    REQUIRE( facts.hasBrokenLayer );
    REQUIRE( facts.hasInFlightTask );
    REQUIRE_FALSE( facts.hasRaster );
}

TEST_CASE( "suggestedNextAction follows the documented priority", "[wb8][context]" )
{
    testApp();

    // 1. Empty workspace → import.
    SelectionContextSnapshot empty;
    REQUIRE( ContextRules::suggestedNextAction( empty ).commandId
             == QLatin1String( "project.importLayer" ) );

    // 2. Raster selected → processing suggestion.
    SelectionContextSnapshot raster;
    raster.hasRaster = true;
    raster.layerCount = 1;
    REQUIRE( ContextRules::suggestedNextAction( raster ).commandId
             == QLatin1String( "rs.spectralIndex" ) );

    // 3. Vector selected and editable → start editing (needs a real layer).
    QgsVectorLayer *layer = memoryLayer();
    REQUIRE( layer->isValid() );
    SelectionContextSnapshot vector;
    vector.selectedLayers.append( layer );
    REQUIRE( ContextRules::suggestedNextAction( vector ).commandId
             == QLatin1String( "layer.toggleEditing" ) );

    // 4. Temporal beats in-flight; both beat the empty-state import.
    SelectionContextSnapshot temporal;
    temporal.hasTemporal = true;
    temporal.layerCount = 2;
    temporal.hasInFlightTask = true;
    REQUIRE( ContextRules::suggestedNextAction( temporal ).commandId
             == QLatin1String( "workbench.temporal" ) );

    // 5. In-flight task → processing history.
    SelectionContextSnapshot busy;
    busy.layerCount = 2;
    busy.hasInFlightTask = true;
    REQUIRE( ContextRules::suggestedNextAction( busy ).commandId
             == QLatin1String( "workbench.processingHistory" ) );

    // 6. Priority: editing beats raster when both hold.
    QgsVectorLayer *editLayer = memoryLayer();
    editLayer->startEditing();
    SelectionContextSnapshot both;
    both.hasRaster = true;
    both.selectedLayers.append( editLayer );
    both.activeLayer = editLayer;
    both.activeEditable = true;
    REQUIRE( ContextRules::suggestedNextAction( both ).commandId
             == QLatin1String( "layer.saveEdits" ) );

    // Suggestions always name text for the UI.
    REQUIRE_FALSE( ContextRules::suggestedNextAction( raster ).text.isEmpty() );

    delete layer;
    delete editLayer;
}

TEST_CASE( "SelectionContext picks up the injected in-flight predicate",
           "[wb8][context]" )
{
    testApp();
    sicnu::app::SelectionContext context;
    REQUIRE_FALSE( context.snapshot().hasInFlightTask );

    bool inFlight = true;
    context.setInFlightTaskPredicate( [&inFlight] { return inFlight; } );
    context.refreshNow();
    REQUIRE( context.snapshot().hasInFlightTask );

    inFlight = false;
    context.refreshNow();
    REQUIRE_FALSE( context.snapshot().hasInFlightTask );
}
