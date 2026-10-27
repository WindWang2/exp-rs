// test_r24_a_help_drift.cpp — R24-A science track (WP-A7).
//
// The algorithm help catalog (src/processing/algorithm_help_catalog.cpp)
// is a hand-maintained name → text table looked up by each algorithm's
// name(). #1646-5 found 14 dead keys (help text unreachable because the
// key matched no registered algorithm); the key set was corrected, but
// nothing kept it honest — "the table rots silently" was the second half
// of that issue. This test is the rot gate: every catalog key must be
// answered by at least one built-in provider's registered algorithm name.

#include "processing/algorithm_help_catalog.h"
#include "processing/providers/gdal_tools/provider.h"
#include "processing/providers/otb_tools/provider.h"
#include "processing/providers/qgis_algorithms/provider.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>

#include <QList>
#include <QSet>
#include <QString>

#include "qgsprocessingalgorithm.h"
#include "qgsprocessingprovider.h"
#include "qgsapplication.h"
#include "processing/qgsprocessingregistry.h"

namespace
{
QCoreApplication *ensureCoreApp()
{
    static QCoreApplication *app = nullptr;
    if ( !app )
    {
        static int argc = 1;
        static char name[] = "test_r24_a_help_drift";
        static char *argv[] = { name, nullptr };
        app = new QCoreApplication( argc, argv );
    }
    return app;
}

QSet<QString> registeredAlgorithmNames()
{
    QSet<QString> names;
    // The three built-in providers that own every catalog key family
    // (gdal*/ogr*, otb_*, raster_*/vector_*/native/rs_ panel names),
    // activated through the real processing registry (same route the
    // toolbox uses; addProvider triggers each provider's loadAlgorithms).
    QgsProcessingRegistry *registry = QgsApplication::processingRegistry();
    REQUIRE( registry != nullptr );
    if ( !registry->providerById( QStringLiteral( "gdal_tools" ) ) )
        registry->addProvider( new GdalToolsProvider() );
    if ( !registry->providerById( QStringLiteral( "qgis_algorithms" ) ) )
        registry->addProvider( new QgisAlgorithmsProvider() );
    if ( !registry->providerById( QStringLiteral( "otb_tools" ) ) )
        registry->addProvider( new OtbToolsProvider() );
    const auto algorithms = registry->algorithms();
    for ( const QgsProcessingAlgorithm *algorithm : algorithms )
        names.insert( algorithm->name() );
    return names;
}
} // namespace

TEST_CASE( "Help catalog keys all resolve to a registered algorithm name",
           "[processing][help][drift][r24_a]" )
{
    ensureCoreApp();
    const QStringList keys = SicnuAlgorithmHelp::catalogKeys();
    REQUIRE( keys.size() >= 80 ); // the shipped catalog is substantial
    const QSet<QString> names = registeredAlgorithmNames();
    REQUIRE( names.size() >= 80 );

    QStringList dead;
    for ( const QString &key : keys )
        if ( !names.contains( key ) )
            dead.append( key );
    INFO( "dead keys: " << dead.join( ", " ).toStdString() );
    CHECK( dead.isEmpty() );
}

TEST_CASE( "Help catalog lookups answer for a representative live key",
           "[processing][help][drift][r24_a]" )
{
    ensureCoreApp();
    // A registered name must get its catalog text, not the generic
    // fallback (shortDescription returns the displayName when the key is
    // unknown — pass a sentinel displayName to detect the fallback).
    const QString text = SicnuAlgorithmHelp::shortDescription(
        QStringLiteral( "gdalwarp" ), QStringLiteral( "SENTINEL_FALLBACK" ) );
    CHECK( text != QStringLiteral( "SENTINEL_FALLBACK" ) );
    CHECK( !text.isEmpty() );
}
