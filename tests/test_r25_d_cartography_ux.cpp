// tests/test_r25_d_cartography_ux.cpp — R25-D Loop 1
//
// Cartography surface honesty (Oracle 1/2):
//  1. Pure ContextRules: the cartography.* command family declares a
//     "map layers present" requirement — unavailabilityReason() must name
//     it when the active view has no layers and stay silent otherwise.
//  2. Source scan: the shell wires that predicate onto every cartography.*
//     command definition (command_defs.cpp), so palette/menu/ribbon
//     projections disable with a reason instead of failing on click.
//  3. Source scan: the Ribbon 制图 tab projects the cartography commands
//     (incl. the Cartography Workbench entry) through addCommandButton —
//     the registry projection path, not a second direct connect.
//
// Light link surface: same direct-TU embed as test_command_registry.
#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <QFile>
#include <QTextStream>

#include "app/workbench/selection_context.h"

using sicnu::app::ContextRules;
using sicnu::app::SelectionContextSnapshot;

namespace
{

QString readSource( const QString &relativePath )
{
    QFile file( QStringLiteral( CMAKE_SOURCE_DIR ) + QLatin1Char( '/' ) + relativePath );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
        return QString();
    QTextStream stream( &file );
    return stream.readAll();
}

/// The span of one RS_CMD registration block: from the id literal to the
/// registerCommand call that closes it.
QString commandBlock( const QString &source, const QString &commandId )
{
    const int idPos = source.indexOf( QStringLiteral( "RS_CMD( d, \"" ) + commandId + QLatin1Char( '"' ) );
    if ( idPos < 0 )
        return QString();
    const int end = source.indexOf( QStringLiteral( "registry->registerCommand( d );" ), idPos );
    if ( end < 0 )
        return QString();
    return source.mid( idPos, end - idPos );
}

} // namespace

TEST_CASE( "cartography commands require map layers (pure rule)", "[r25d][cartography][context_rules]" )
{
    SelectionContextSnapshot empty;
    empty.layerCount = 0;

    SelectionContextSnapshot loaded;
    loaded.layerCount = 3;
    loaded.hasRaster = true;

    for ( const char *id :
          { "cartography.compose", "cartography.preflight", "cartography.repair", "cartography.export" } )
    {
        const QString commandId = QString::fromUtf8( id );

        // Requirement fact: exactly one unsatisfied row names the missing
        // prerequisite deterministically (the help layer consumes the same
        // derivation).
        const auto facts = ContextRules::requirementFacts( empty, commandId );
        REQUIRE( facts.size() == 1 );
        CHECK( facts[0].code == QStringLiteral( "map.layers_present" ) );
        CHECK_FALSE( facts[0].satisfied );
        CHECK_FALSE( facts[0].label.isEmpty() );

        const auto loadedFacts = ContextRules::requirementFacts( loaded, commandId );
        REQUIRE( loadedFacts.size() == 1 );
        CHECK( loadedFacts[0].satisfied );

        // Text projection agrees with the facts by construction.
        CHECK_FALSE( ContextRules::unavailabilityReason( empty, commandId ).isEmpty() );
        CHECK( ContextRules::unavailabilityReason( loaded, commandId ).isEmpty() );

        // The predicate itself is the layerCount fact — a view with layers
        // but no selection still satisfies it (compose reads canvas order,
        // not the selection).
        CHECK( ContextRules::hasMapLayers( loaded ) );
        CHECK_FALSE( ContextRules::hasMapLayers( empty ) );
    }

    // Unknown-family isolation: the workbench entry itself stays always
    // available (opening a dock needs no layers).
    CHECK( ContextRules::requirementFacts( empty, QStringLiteral( "workbench.cartography" ) ).isEmpty() );
    CHECK( ContextRules::unavailabilityReason( empty, QStringLiteral( "workbench.cartography" ) ).isEmpty() );
}

TEST_CASE( "shell wires availability onto every cartography command", "[r25d][cartography][source_scan]" )
{
    const QString source = readSource( QStringLiteral( "src/app/workbench/command_defs.cpp" ) );
    REQUIRE_FALSE( source.isEmpty() );

    for ( const char *id :
          { "cartography.compose", "cartography.preflight", "cartography.repair", "cartography.export" } )
    {
        const QString block = commandBlock( source, QString::fromUtf8( id ) );
        INFO( "command block for " << id );
        REQUIRE_FALSE( block.isEmpty() );
        CHECK( block.contains( QStringLiteral( "ContextRules::hasMapLayers" ) ) );
        CHECK( block.contains( QStringLiteral( "ContextRules::unavailabilityReason" ) ) );
    }
}

TEST_CASE( "ribbon cartography tab projects the registry commands", "[r25d][cartography][source_scan]" )
{
    const QString source = readSource( QStringLiteral( "src/app/shell/ribbon_controller.cpp" ) );
    REQUIRE_FALSE( source.isEmpty() );

    // The 制图 tab section: from its banner comment to its addTab call.
    const int sectionStart = source.indexOf( QStringLiteral( "── 制图" ) );
    REQUIRE( sectionStart >= 0 );
    const int tabPos = source.indexOf( QStringLiteral( "addTab( tr( \"Cartography\"" ), sectionStart );
    REQUIRE( tabPos > sectionStart );
    const QString section = source.mid( sectionStart, tabPos - sectionStart );

    // The workbench entry (F1: the dock was unreachable from its own ribbon
    // tab) and the four operator commands ride the registry projection —
    // no direct-connect second path inside this section.
    CHECK( section.contains( QStringLiteral( "addCommandButton" ) ) );
    CHECK( section.contains( QStringLiteral( "workbench.cartography" ) ) );
    CHECK( section.contains( QStringLiteral( "cartography.compose" ) ) );
    CHECK( section.contains( QStringLiteral( "cartography.preflight" ) ) );
    CHECK( section.contains( QStringLiteral( "cartography.repair" ) ) );
    CHECK( section.contains( QStringLiteral( "cartography.export" ) ) );
}
