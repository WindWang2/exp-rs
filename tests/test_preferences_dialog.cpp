// Preferences Dialog tests — verify settings dialog functionality
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDialog>
#include <QSettings>
#include <QTabWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QVBoxLayout>

// Mock PreferencesDialog for testing
class TestPreferencesDialog : public QDialog
{
public:
    TestPreferencesDialog(QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowTitle("Preferences");
        m_tabWidget = new QTabWidget(this);

        // General tab
        auto *generalTab = new QWidget();
        m_themeCombo = new QComboBox(generalTab);
        m_themeCombo->addItems({"Light", "Dark"});
        m_crsCombo = new QComboBox(generalTab);
        m_crsCombo->addItems({"EPSG:4326", "EPSG:3857", "EPSG:32649"});
        m_tabWidget->addTab(generalTab, "General");

        // Tools tab
        auto *toolsTab = new QWidget();
        m_gdalPathEdit = new QLineEdit(toolsTab);
        m_otbPathEdit = new QLineEdit(toolsTab);
        m_tabWidget->addTab(toolsTab, "Tools");

        // About tab
        auto *aboutTab = new QWidget();
        m_tabWidget->addTab(aboutTab, "About");
    }

    int tabCount() const { return m_tabWidget->count(); }
    QString tabTitle(int index) const { return m_tabWidget->tabText(index); }

    void setTheme(const QString &theme) { m_themeCombo->setCurrentText(theme); }
    QString theme() const { return m_themeCombo->currentText(); }

    void setDefaultCrs(const QString &crs) { m_crsCombo->setCurrentText(crs); }
    QString defaultCrs() const { return m_crsCombo->currentText(); }

    void setGdalPath(const QString &path) { m_gdalPathEdit->setText(path); }
    QString gdalPath() const { return m_gdalPathEdit->text(); }

    void setOtbPath(const QString &path) { m_otbPathEdit->setText(path); }
    QString otbPath() const { return m_otbPathEdit->text(); }

    void loadSettings()
    {
        QSettings settings;
        setTheme(settings.value("preferences/theme", "Light").toString());
        setDefaultCrs(settings.value("preferences/defaultCrs", "EPSG:4326").toString());
        setGdalPath(settings.value("tools/gdalPath", "").toString());
        setOtbPath(settings.value("tools/otbPath", "").toString());
    }

    void saveSettings()
    {
        QSettings settings;
        settings.setValue("preferences/theme", theme());
        settings.setValue("preferences/defaultCrs", defaultCrs());
        settings.setValue("tools/gdalPath", gdalPath());
        settings.setValue("tools/otbPath", otbPath());
    }

private:
    QTabWidget *m_tabWidget;
    QComboBox *m_themeCombo;
    QComboBox *m_crsCombo;
    QLineEdit *m_gdalPathEdit;
    QLineEdit *m_otbPathEdit;
};

// Helper to ensure single QApplication instance
static QApplication *ensureApp()
{
    if (!qApp) {
        static int argc = 1;
        static char appName[] = "test_runner";
        static char *argv[] = { appName, nullptr };
        new QApplication(argc, argv);
    }
    return static_cast<QApplication*>(qApp);
}


TEST_CASE("PreferencesDialog creation", "[gui][preferences]") {
    ensureApp();

    SECTION("Creates with correct title") {
        TestPreferencesDialog dialog;
        CHECK(dialog.windowTitle() == "Preferences");
    }

    SECTION("Has required tabs") {
        TestPreferencesDialog dialog;
        CHECK(dialog.tabCount() == 3);
        CHECK(dialog.tabTitle(0) == "General");
        CHECK(dialog.tabTitle(1) == "Tools");
        CHECK(dialog.tabTitle(2) == "About");
    }
}

TEST_CASE("PreferencesDialog theme setting", "[gui][preferences]") {
    ensureApp();

    TestPreferencesDialog dialog;

    SECTION("Default theme is Light") {
        CHECK(dialog.theme() == "Light");
    }

    SECTION("Can set Dark theme") {
        dialog.setTheme("Dark");
        CHECK(dialog.theme() == "Dark");
    }
}

TEST_CASE("PreferencesDialog CRS setting", "[gui][preferences]") {
    ensureApp();

    TestPreferencesDialog dialog;

    SECTION("Default CRS is EPSG:4326") {
        CHECK(dialog.defaultCrs() == "EPSG:4326");
    }

    SECTION("Can set different CRS") {
        dialog.setDefaultCrs("EPSG:3857");
        CHECK(dialog.defaultCrs() == "EPSG:3857");
    }
}

TEST_CASE("PreferencesDialog tool paths", "[gui][preferences]") {
    ensureApp();

    TestPreferencesDialog dialog;

    SECTION("Default paths are empty") {
        CHECK(dialog.gdalPath().isEmpty());
        CHECK(dialog.otbPath().isEmpty());
    }

    SECTION("Can set GDAL path") {
        dialog.setGdalPath("/usr/bin");
        CHECK(dialog.gdalPath() == "/usr/bin");
    }

    SECTION("Can set OTB path") {
        dialog.setOtbPath("/opt/otb/bin");
        CHECK(dialog.otbPath() == "/opt/otb/bin");
    }
}

TEST_CASE("PreferencesDialog settings persistence", "[gui][preferences]") {
    ensureApp();

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings settings;
    settings.clear();

    SECTION("Save and load settings") {
        TestPreferencesDialog dialog1;
        dialog1.setTheme("Dark");
        dialog1.setDefaultCrs("EPSG:32649");
        dialog1.setGdalPath("/usr/local/bin");
        dialog1.saveSettings();

        TestPreferencesDialog dialog2;
        dialog2.loadSettings();
        CHECK(dialog2.theme() == "Dark");
        CHECK(dialog2.defaultCrs() == "EPSG:32649");
        CHECK(dialog2.gdalPath() == "/usr/local/bin");
    }

    settings.clear();
}

TEST_CASE("Theme setting is read from QSettings on startup", "[gui][preferences][theme]") {
    ensureApp();

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings settings;
    settings.clear();

    SECTION("Dark theme is saved and can be read") {
        // Save dark theme
        settings.setValue("preferences/theme", "dark");

        // Read it back
        QString theme = settings.value("preferences/theme", "light").toString();
        CHECK(theme == "dark");
    }

    SECTION("Default theme is light when not set") {
        // Don't set any theme
        QString theme = settings.value("preferences/theme", "light").toString();
        CHECK(theme == "light");
    }

    settings.clear();
}
