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
#include <QScrollArea>
#include <QScrollBar>
#include <QLabel>
#include <QScopeGuard>

#include <qgsapplication.h>

#include "dialogs/preferences_dialog.h"
#include "support/qsettings_redirect.h"

namespace {

exprs_test::QSettingsUserRootRedirect settingsRedirect;

void ensureQgisApp()
{
    QSettings::setDefaultFormat( QSettings::IniFormat );
    static int argc = 1;
    static char appName[] = "test_preferences_dialog";
    static char *argv[] = { appName, nullptr };
    if ( !QApplication::instance() )
    {
        auto *app = new QgsApplication( argc, argv, true );
        ( void ) app;
        QgsApplication::initQgis();
    }
    QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-tests" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "preferences-dialog" ) );
    // Every case starts with defaults in the process-isolated settings root.
    QSettings().clear();
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

TEST_CASE( "PreferencesDialog - Text scale setting", "[gui][preferences][a11y]" )
{
    ensureQgisApp();
    PreferencesDialog dialog;
    auto *scale = dialog.findChild<QComboBox *>( QStringLiteral( "preferencesUiScale" ) );
    REQUIRE( scale != nullptr );
    CHECK( scale->count() == 5 );
    dialog.setUiScale( 1.5 );
    CHECK( dialog.uiScale() == 1.5 );
}

TEST_CASE( "PreferencesDialog - Language choices and invalid saved preference", "[gui][preferences][i18n]" )
{
    ensureQgisApp();
    QSettings settings;
    SECTION( "Default follows the system" )
    {
        PreferencesDialog dialog;
        auto *language = dialog.findChild<QComboBox *>( QStringLiteral( "preferencesUiLanguage" ) );
        REQUIRE( language != nullptr );
        REQUIRE( language->count() == 3 );
        CHECK( language->itemData( 0 ).toString() == QStringLiteral( "system" ) );
        CHECK( language->itemData( 1 ).toString() == QStringLiteral( "zh_CN" ) );
        CHECK( language->itemData( 2 ).toString() == QStringLiteral( "en" ) );
        CHECK( language->itemText( 1 ) == QStringLiteral( "简体中文" ) );
        CHECK( language->itemText( 2 ) == QStringLiteral( "English" ) );
        CHECK_FALSE( language->accessibleName().isEmpty() );
        CHECK( dialog.uiLanguage() == QStringLiteral( "system" ) );
    }
    SECTION( "Invalid saved preference falls back without rewriting settings" )
    {
        settings.setValue( "preferences/uiLanguage", QStringLiteral( "unsupported" ) );
        PreferencesDialog dialog;
        CHECK( dialog.uiLanguage() == QStringLiteral( "system" ) );
        CHECK( settings.value( "preferences/uiLanguage" ).toString() == QStringLiteral( "unsupported" ) );
        dialog.setUiLanguage( QStringLiteral( "zh_CN" ) );
        dialog.setUiLanguage( QStringLiteral( "invalid" ) );
        CHECK( dialog.uiLanguage() == QStringLiteral( "system" ) );
    }
}

TEST_CASE( "PreferencesDialog - Apply persists language while Cancel discards later edits", "[gui][preferences][i18n]" )
{
    ensureQgisApp();
    QSettings settings;
    PreferencesDialog dialog;
    dialog.show();
    auto *buttons = dialog.findChild<QDialogButtonBox *>( QStringLiteral( "preferencesButtonBox" ) );
    auto *notice = dialog.findChild<QLabel *>( QStringLiteral( "preferencesUiLanguageNotice" ) );
    REQUIRE( buttons != nullptr );
    REQUIRE( notice != nullptr );
    const QString originalTitle = dialog.windowTitle();
    bool accepted = false;
    QObject::connect( &dialog, &QDialog::accepted, &dialog, [&] { accepted = true; } );
    dialog.setUiLanguage( QStringLiteral( "zh_CN" ) );
    dialog.setTheme( QStringLiteral( "dark" ) );
    CHECK_FALSE( settings.contains( "preferences/uiLanguage" ) );
    buttons->button( QDialogButtonBox::Apply )->click();
    CHECK( settings.value( "preferences/uiLanguage" ).toString() == QStringLiteral( "zh_CN" ) );
    CHECK( settings.value( "preferences/theme" ).toString() == QStringLiteral( "dark" ) );
    CHECK( dialog.isVisible() );
    CHECK_FALSE( accepted );
    CHECK_FALSE( notice->isHidden() );
    CHECK( notice->text() == QCoreApplication::translate(
        "PreferencesDialog", "Language changes take effect after restarting the application." ) );
    CHECK( dialog.windowTitle() == originalTitle );
    dialog.setUiLanguage( QStringLiteral( "en" ) );
    dialog.setTheme( QStringLiteral( "light" ) );
    buttons->button( QDialogButtonBox::Cancel )->click();
    CHECK( dialog.result() == QDialog::Rejected );
    CHECK_FALSE( accepted );
    PreferencesDialog reopened;
    CHECK( reopened.uiLanguage() == QStringLiteral( "zh_CN" ) );
    CHECK( reopened.theme() == QStringLiteral( "dark" ) );
}

TEST_CASE( "PreferencesDialog - OK saves the future language despite a developer override", "[gui][preferences][i18n]" )
{
    ensureQgisApp();
    const bool hadOverride = qEnvironmentVariableIsSet( "SICNU_LANG" );
    const QByteArray previousOverride = qgetenv( "SICNU_LANG" );
    const auto restoreOverride = qScopeGuard( [&] {
        hadOverride ? qputenv( "SICNU_LANG", previousOverride ) : qunsetenv( "SICNU_LANG" );
    } );
    qputenv( "SICNU_LANG", "zh_CN" );
    PreferencesDialog dialog;
    auto *buttons = dialog.findChild<QDialogButtonBox *>( QStringLiteral( "preferencesButtonBox" ) );
    auto *notice = dialog.findChild<QLabel *>( QStringLiteral( "preferencesUiLanguageOverrideNotice" ) );
    REQUIRE( buttons != nullptr );
    REQUIRE( notice != nullptr );
    CHECK_FALSE( notice->text().isEmpty() );
    dialog.setUiLanguage( QStringLiteral( "en" ) );
    buttons->button( QDialogButtonBox::Ok )->click();
    CHECK( dialog.result() == QDialog::Accepted );
    CHECK( QSettings().value( "preferences/uiLanguage" ).toString() == QStringLiteral( "en" ) );
    CHECK( qgetenv( "SICNU_LANG" ) == QByteArray( "zh_CN" ) );
    qunsetenv( "SICNU_LANG" );
    PreferencesDialog reopened;
    CHECK( reopened.uiLanguage() == QStringLiteral( "en" ) );
    CHECK( reopened.findChild<QLabel *>( QStringLiteral( "preferencesUiLanguageOverrideNotice" ) ) == nullptr );
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
        dialog1.setUiScale( 1.5 );
        dialog1.setUiLanguage( QStringLiteral( "zh_CN" ) );
        dialog1.setDefaultCrs( "EPSG:32649" );
        dialog1.setGdalPath( "/usr/local/bin" );
        dialog1.setOtbPath( "/opt/otb/bin" );
        dialog1.setLogToFile( true );
        dialog1.setLogFilePath( "/var/log/rs_test.log" );
        dialog1.saveSettings();

        PreferencesDialog dialog2;
        dialog2.loadSettings();
        CHECK( dialog2.theme() == "dark" );
        CHECK( dialog2.uiScale() == 1.5 );
        CHECK( dialog2.uiLanguage() == QStringLiteral( "zh_CN" ) );
        CHECK( dialog2.defaultCrs().contains( "EPSG:32649" ) );
        CHECK( dialog2.gdalPath() == "/usr/local/bin" );
        CHECK( dialog2.otbPath() == "/opt/otb/bin" );
        CHECK( dialog2.logToFile() == true );
        CHECK( dialog2.logFilePath() == "/var/log/rs_test.log" );
    }

    settings.clear();
}

TEST_CASE( "PreferencesDialog - Large text keeps CRS readable", "[gui][preferences][a11y]" )
{
    ensureQgisApp();
    const QFont previous = QApplication::font();
    struct FontScope {
      QFont f;
      ~FontScope() { QApplication::setFont( f ); }
    } scope{ previous };
    QFont enlarged = previous;
    enlarged.setPointSizeF( previous.pointSizeF() * 2.0 );
    QApplication::setFont( enlarged );
    {
        PreferencesDialog dialog;
        dialog.show();
        QCoreApplication::processEvents();
        auto *crs = dialog.findChild<QComboBox *>( QStringLiteral( "preferencesCrs" ) );
        REQUIRE( crs != nullptr );
        // Pin the change this case claims to cover: without AdjustToContents
        // the CRS combo would be truncated at 200% text scale (#1820).
        CHECK( crs->sizeAdjustPolicy() == QComboBox::AdjustToContents );
        REQUIRE( crs->lineEdit() != nullptr );
        for ( int i = 0; i < crs->count(); ++i )
        {
            crs->setCurrentIndex( i );
            const QLineEdit *editor = crs->lineEdit();
            const QMargins margins = editor->textMargins();
            const int textWidth = editor->contentsRect().width() - margins.left() - margins.right();
            CHECK( editor->fontMetrics().horizontalAdvance( crs->currentText() ) <= textWidth );
        }

        auto *tabs = dialog.findChild<QTabWidget *>( QStringLiteral( "preferencesTabWidget" ) );
        REQUIRE( tabs != nullptr );
        dialog.resize( dialog.width(), 360 );
        for ( int i = 0; i < tabs->count(); ++i )
        {
            INFO( "tab " << i );
            tabs->setCurrentIndex( i );
            QCoreApplication::processEvents();
            auto *scroll = qobject_cast<QScrollArea *>( tabs->widget( i ) );
            REQUIRE( scroll != nullptr );
            REQUIRE( scroll->widget() != nullptr );
            CHECK( scroll->widgetResizable() );
            CHECK( scroll->widget()->height() >= scroll->widget()->minimumSizeHint().height() );
            if ( i == 0 )
                CHECK( scroll->verticalScrollBar()->maximum() > 0 );
            if ( scroll->verticalScrollBar()->maximum() > 0 )
            {
                scroll->verticalScrollBar()->setValue( scroll->verticalScrollBar()->maximum() );
                CHECK( scroll->widget()->geometry().bottom() < scroll->viewport()->height() );
            }
        }
    }
}
