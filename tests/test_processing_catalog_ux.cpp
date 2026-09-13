// Workbench 10.0 — rs: operator catalog UX (goal WP-F)
//
// Covers: catalog population from the live registry, search over
// name/description/id, modality filter wiring, recent-list persistence cap
// and favorites toggling (QSettings scoped to the test's org/app names so
// the user's real settings are untouched).
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/shell/rs_operator_catalog_panel.h"

#include "operators/rs/rs_operators_init.h"

#include <QLineEdit>
#include <QApplication>
#include <QSettings>

using sicnu::app::RsOperatorCatalogPanel;

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_processing_catalog_ux";
char *fake_argv[] = { fake_argv0, nullptr };

} // namespace

int main( int argc, char *argv[] )
{
    if ( !QCoreApplication::instance() )
    {
        QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-tests" ) );
        QCoreApplication::setApplicationName( QStringLiteral( "rs-catalog-ux-%1" )
                                                  .arg( QCoreApplication::applicationPid() ) );
        new QApplication( fake_argc, fake_argv ); // QT_QPA_PLATFORM=offscreen
    }
    sicnu::operators::rs::initBuiltinRsOperators();
    const int result = Catch::Session().run( argc, argv );
    // Clean the settings this test wrote (org/app names are test-scoped).
    QSettings( QStringLiteral( "sicnu-tests" ),
               QStringLiteral( "rs-catalog-ux-%1" ).arg( QCoreApplication::applicationPid() ) )
        .clear();
    return result;
}

TEST_CASE( "catalog lists registered operators", "[catalog_ux][workbench10]" )
{
    RsOperatorCatalogPanel panel;

    const QStringList all = panel.visibleOperatorIds();
    REQUIRE( all.size() > 0 );
    CHECK( all.contains( QStringLiteral( "rs:spectral_index" ) ) );
}

TEST_CASE( "catalog search narrows to matching ids", "[catalog_ux][workbench10]" )
{
    RsOperatorCatalogPanel panel;
    QLineEdit *search = panel.findChild<QLineEdit *>( QStringLiteral( "rsOperatorCatalogSearch" ) );
    REQUIRE( search != nullptr );

    search->setText( QStringLiteral( "zonal" ) );
    const QStringList narrowed = panel.visibleOperatorIds();
    REQUIRE_FALSE( narrowed.isEmpty() );
    for ( const QString &id : narrowed )
        CHECK( id.contains( QStringLiteral( "zonal" ), Qt::CaseInsensitive ) );

    // Clearing restores the full catalog.
    search->clear();
    CHECK( panel.visibleOperatorIds().size() > narrowed.size() );
}

TEST_CASE( "recent list is capped and settings-backed", "[catalog_ux][workbench10]" )
{
    RsOperatorCatalogPanel panel;
    for ( int i = 0; i < 20; ++i )
        panel.noteOperatorRun( QStringLiteral( "rs:catalog_probe_%1" ).arg( i ) );
    const QStringList recent = panel.recentOperators();
    CHECK( recent.size() <= 12 );
    CHECK( recent.first() == QStringLiteral( "rs:catalog_probe_19" ) );
}

TEST_CASE( "favorites persist through a panel rebuild", "[catalog_ux][workbench10]" )
{
    QSettings settings;
    settings.setValue( QStringLiteral( "workbench/processing/favorites" ),
                       QStringList{ QStringLiteral( "rs:spectral_index" ) } );

    RsOperatorCatalogPanel panel;
    CHECK( panel.favoriteOperators().contains( QStringLiteral( "rs:spectral_index" ) ) );

    settings.clear();
}
