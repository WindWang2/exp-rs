// tests/test_preferences_dialog.cpp — Comprehensive tests for real PreferencesDialog
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QSettings>
#include <QTabWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>

#include <qgsapplication.h>

#include "dialogs/preferences_dialog.h"

namespace {

void ensureQgisApp()
{
    if ( QApplication::instance() )
        return;

    static int argc = 1;
    static char appName[] = "test_preferences_dialog";
    static char *argv[] = { appName, nullptr };
    static auto *app = new QgsApplication( argc, argv, true );
    ( void ) app;
    QgsApplication::initQgis();
}

} // namespace

TEST_CASE( "PreferencesDialog - Creation and UI Structure", "[gui][preferences]" )
{
    ensureQgisApp();

    PreferencesDialog dialog;
    CHECK( ( dialog.windowTitle().contains( "Preferences", Qt::CaseInsensitive ) || dialog.windowTitle().contains( "首选项" ) ) );

    auto *tabWidget = dialog.findChild<QTabWidget *>( QStringLiteral( "preferencesTabWidget" ) );
    REQUIRE( tabWidget != nullptr );
    CHECK( tabWidget->count() == 3 );
    CHECK( ( tabWidget->tabText( 0 ).contains( "General" ) || tabWidget->tabText( 0 ).contains( "常规" ) ) );
    CHECK( ( tabWidget->tabText( 1 ).contains( "Tools" ) || tabWidget->tabText( 1 ).contains( "外部工具" ) ) );
    CHECK( ( tabWidget->tabText( 2 ).contains( "About" ) || tabWidget->tabText( 2 ).contains( "关于" ) ) );

    auto *buttonBox = dialog.findChild<QDialogButtonBox *>( QStringLiteral( "preferencesButtonBox" ) );
    REQUIRE( buttonBox != nullptr );
    CHECK( buttonBox->button( QDialogButtonBox::Ok ) != nullptr );
    CHECK( buttonBox->button( QDialogButtonBox::Cancel ) != nullptr );
    CHECK( buttonBox->button( QDialogButtonBox::Apply ) != nullptr );
    CHECK( buttonBox->button( QDialogButtonBox::Help ) != nullptr );
}

TEST_CASE( "PreferencesDialog - Theme Setting", "[gui][preferences]" )
{
    ensureQgisApp();

    PreferencesDialog dialog;

    SECTION( "Default theme is light" ) {
        CHECK( dialog.theme() == "light" );
    }

    SECTION( "Can set dark theme" ) {
        dialog.setTheme( "dark" );
        CHECK( dialog.theme() == "dark" );
    }

    SECTION( "Can toggle back to light theme" ) {
        dialog.setTheme( "dark" );
        CHECK( dialog.theme() == "dark" );
        dialog.setTheme( "light" );
        CHECK( dialog.theme() == "light" );
    }
}

TEST_CASE( "PreferencesDialog - CRS Setting", "[gui][preferences]" )
{
    ensureQgisApp();

    PreferencesDialog dialog;

    SECTION( "Default CRS is EPSG:4326" ) {
        CHECK( dialog.defaultCrs().contains( "EPSG:4326" ) );
    }

    SECTION( "Can set different CRS" ) {
        dialog.setDefaultCrs( "EPSG:3857" );
        CHECK( dialog.defaultCrs().contains( "EPSG:3857" ) );

        dialog.setDefaultCrs( "EPSG:32649" );
        CHECK( dialog.defaultCrs().contains( "EPSG:32649" ) );
    }
}

TEST_CASE( "PreferencesDialog - Tool Paths Configuration", "[gui][preferences]" )
{
    ensureQgisApp();

    PreferencesDialog dialog;

    SECTION( "Default paths" ) {
        // Can be empty or preloaded from settings
        dialog.setGdalPath( "" );
        dialog.setOtbPath( "" );
        CHECK( dialog.gdalPath().isEmpty() );
        CHECK( dialog.otbPath().isEmpty() );
    }

    SECTION( "Can set GDAL path" ) {
        dialog.setGdalPath( "/usr/bin" );
        CHECK( dialog.gdalPath() == "/usr/bin" );
    }

    SECTION( "Can set OTB path" ) {
        dialog.setOtbPath( "/opt/otb/bin" );
        CHECK( dialog.otbPath() == "/opt/otb/bin" );
    }
}

TEST_CASE( "PreferencesDialog - Logging Configuration", "[gui][preferences]" )
{
    ensureQgisApp();

    PreferencesDialog dialog;

    SECTION( "Toggle log to file" ) {
        dialog.setLogToFile( true );
        CHECK( dialog.logToFile() == true );

        dialog.setLogToFile( false );
        CHECK( dialog.logToFile() == false );
    }

    SECTION( "Set log file path" ) {
        dialog.setLogFilePath( "/tmp/rs_studio.log" );
        CHECK( dialog.logFilePath() == "/tmp/rs_studio.log" );
    }
}

TEST_CASE( "PreferencesDialog - Settings Persistence via saveSettings and loadSettings", "[gui][preferences]" )
{
    ensureQgisApp();

    QSettings::setDefaultFormat( QSettings::IniFormat );
    QSettings settings;
    settings.clear();

    SECTION( "Save and load round-trip" ) {
        PreferencesDialog dialog1;
        dialog1.setTheme( "dark" );
        dialog1.setDefaultCrs( "EPSG:32649" );
        dialog1.setGdalPath( "/usr/local/bin" );
        dialog1.setOtbPath( "/opt/otb/bin" );
        dialog1.setLogToFile( true );
        dialog1.setLogFilePath( "/var/log/rs_test.log" );
        dialog1.saveSettings();

        PreferencesDialog dialog2;
        dialog2.loadSettings();
        CHECK( dialog2.theme() == "dark" );
        CHECK( dialog2.defaultCrs().contains( "EPSG:32649" ) );
        CHECK( dialog2.gdalPath() == "/usr/local/bin" );
        CHECK( dialog2.otbPath() == "/opt/otb/bin" );
        CHECK( dialog2.logToFile() == true );
        CHECK( dialog2.logFilePath() == "/var/log/rs_test.log" );
    }

    settings.clear();
}
