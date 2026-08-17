#include "preferences_dialog.h"
#include "dialog_help_catalog.h"

#include <QTabWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QSettings>
#include <QFileDialog>
#include <QMessageBox>
#include <QCheckBox>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    
    SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "preferences" ) );
    setMinimumSize(500, 400);

    auto *mainLayout = new QVBoxLayout(this);

    m_tabWidget = new QTabWidget(this);
    mainLayout->addWidget(m_tabWidget);

    setupGeneralTab();
    setupToolsTab();
    setupAboutTab();

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply | QDialogButtonBox::Help, this );
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PreferencesDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &PreferencesDialog::saveSettings);
    connect( buttonBox, &QDialogButtonBox::helpRequested, this, [this]() {
        SicnuDialogHelp::showToolHelp( this, QStringLiteral( "preferences" ), windowTitle() );
    } );
    mainLayout->addWidget(buttonBox);

    // Tips on key fields (after tabs built)
    SicnuDialogHelp::tip( m_tabWidget, tr( "常规 / 工具路径 / 关于 分页设置。" ) );
    if ( m_themeCombo )
        SicnuDialogHelp::tip( m_themeCombo, tr( "界面主题：浅色或深色。" ) );
    if ( m_crsCombo )
        SicnuDialogHelp::tip( m_crsCombo, tr( "新建工程时的默认坐标系。" ) );

    loadSettings();
}

void PreferencesDialog::setupGeneralTab()
{
    auto *tab = new QWidget();
    auto *layout = new QFormLayout(tab);

    m_themeCombo = new QComboBox(tab);
    m_themeCombo->addItem(tr("Light"), "light");
    m_themeCombo->addItem(tr("Dark"), "dark");
    SicnuDialogHelp::tip( m_themeCombo, tr( "界面主题：浅色或深色。" ) );
    layout->addRow(tr("Theme:"), m_themeCombo);

    m_crsCombo = new QComboBox(tab);
    m_crsCombo->addItem("EPSG:4326 - WGS 84", "EPSG:4326");
    m_crsCombo->addItem("EPSG:3857 - Web Mercator", "EPSG:3857");
    m_crsCombo->addItem("EPSG:32649 - UTM Zone 49N", "EPSG:32649");
    m_crsCombo->addItem("EPSG:32650 - UTM Zone 50N", "EPSG:32650");
    m_crsCombo->addItem("EPSG:32651 - UTM Zone 51N", "EPSG:32651");
    m_crsCombo->setEditable(true);
    SicnuDialogHelp::tip( m_crsCombo, tr( "新建工程时的默认坐标系。" ) );
    layout->addRow(tr("Default CRS:"), m_crsCombo);

    // Logging section
    auto *logLabel = new QLabel(tr("<b>Logging</b>"), tab);
    layout->addRow(logLabel);

    m_logToFileCheck = new QCheckBox(tr("Enable log to file"), tab);
    SicnuDialogHelp::tip( m_logToFileCheck, tr( "是否将日志写入文件。" ) );
    layout->addRow(m_logToFileCheck);

    auto *logPathLayout = new QHBoxLayout();
    m_logFilePathEdit = new QLineEdit(tab);
    m_logFilePathEdit->setPlaceholderText(tr("Log file path"));
    SicnuDialogHelp::tip( m_logFilePathEdit, tr( "日志文件完整路径。" ) );
    logPathLayout->addWidget(m_logFilePathEdit);
    auto *logBrowseBtn = new QPushButton(tr("Browse..."), tab);
    connect(logBrowseBtn, &QPushButton::clicked, this, &PreferencesDialog::onBrowseLogPath);
    logPathLayout->addWidget(logBrowseBtn);
    layout->addRow(tr("Log File:"), logPathLayout);

    connect(m_logToFileCheck, &QCheckBox::toggled, this, &PreferencesDialog::onLogToFileToggled);
    onLogToFileToggled(m_logToFileCheck->isChecked());

    m_tabWidget->addTab(tab, tr("General"));
}

void PreferencesDialog::setupToolsTab()
{
    auto *tab = new QWidget();
    auto *layout = new QFormLayout(tab);

    auto *gdalLayout = new QHBoxLayout();
    m_gdalPathEdit = new QLineEdit(tab);
    m_gdalPathEdit->setPlaceholderText(tr("Path to GDAL tools directory"));
    SicnuDialogHelp::tip( m_gdalPathEdit, tr( "GDAL 工具目录（gdal_translate 等所在路径）。" ) );
    gdalLayout->addWidget(m_gdalPathEdit);
    auto *gdalBrowseBtn = new QPushButton(tr("Browse..."), tab);
    connect(gdalBrowseBtn, &QPushButton::clicked, this, &PreferencesDialog::onBrowseGdalPath);
    gdalLayout->addWidget(gdalBrowseBtn);
    layout->addRow(tr("GDAL Path:"), gdalLayout);

    auto *otbLayout = new QHBoxLayout();
    m_otbPathEdit = new QLineEdit(tab);
    m_otbPathEdit->setPlaceholderText(tr("Path to OTB tools directory"));
    SicnuDialogHelp::tip( m_otbPathEdit, tr( "OTB 工具目录，供 OTB 包装算法调用。" ) );
    otbLayout->addWidget(m_otbPathEdit);
    auto *otbBrowseBtn = new QPushButton(tr("Browse..."), tab);
    connect(otbBrowseBtn, &QPushButton::clicked, this, &PreferencesDialog::onBrowseOtbPath);
    otbLayout->addWidget(otbBrowseBtn);
    layout->addRow(tr("OTB Path:"), otbLayout);

    m_tabWidget->addTab(tab, tr("Tools"));
}

void PreferencesDialog::setupAboutTab()
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    auto *titleLabel = new QLabel("<h2>SICNU GEO RS</h2>", tab);
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    auto *versionLabel = new QLabel(tr("Version 1.0.0"), tab);
    versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel);

    auto *descLabel = new QLabel(tr("Remote Sensing Analysis Platform"), tab);
    descLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(descLabel);

    auto *buildLabel = new QLabel(tr("Built on QGIS Engine with GDAL/OTB"), tab);
    buildLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(buildLabel);

    layout->addStretch();

    m_tabWidget->addTab(tab, tr("About"));
}

QString PreferencesDialog::theme() const
{
    return m_themeCombo->currentData().toString();
}

void PreferencesDialog::setTheme(const QString &theme)
{
    int index = m_themeCombo->findData(theme);
    if (index >= 0)
        m_themeCombo->setCurrentIndex(index);
}

QString PreferencesDialog::defaultCrs() const
{
    const QString data = m_crsCombo->currentData().toString();
    if ( !data.isEmpty() )
        return data;
    return m_crsCombo->currentText().trimmed();
}

void PreferencesDialog::setDefaultCrs(const QString &crs)
{
    int index = m_crsCombo->findData(crs);
    if (index >= 0)
        m_crsCombo->setCurrentIndex(index);
    else
        m_crsCombo->setCurrentText(crs);
}

QString PreferencesDialog::gdalPath() const
{
    return m_gdalPathEdit->text();
}

void PreferencesDialog::setGdalPath(const QString &path)
{
    m_gdalPathEdit->setText(path);
}

QString PreferencesDialog::otbPath() const
{
    return m_otbPathEdit->text();
}

void PreferencesDialog::setOtbPath(const QString &path)
{
    m_otbPathEdit->setText(path);
}

bool PreferencesDialog::logToFile() const
{
    return m_logToFileCheck->isChecked();
}

void PreferencesDialog::setLogToFile(bool enabled)
{
    m_logToFileCheck->setChecked(enabled);
}

QString PreferencesDialog::logFilePath() const
{
    return m_logFilePathEdit->text();
}

void PreferencesDialog::setLogFilePath(const QString &path)
{
    m_logFilePathEdit->setText(path);
}

void PreferencesDialog::loadSettings()
{
    QSettings settings;
    setTheme(settings.value("preferences/theme", "light").toString());
    setDefaultCrs(settings.value("preferences/defaultCrs", "EPSG:4326").toString());
    setGdalPath(settings.value("tools/gdalPath", "").toString());
    setOtbPath(settings.value("tools/otbPath", "").toString());
    setLogToFile(settings.value("logging/logToFile", false).toBool());
    setLogFilePath(settings.value("logging/logFilePath", "").toString());
}

void PreferencesDialog::saveSettings()
{
    QSettings settings;
    settings.setValue("preferences/theme", theme());
    settings.setValue("preferences/defaultCrs", defaultCrs());
    settings.setValue("tools/gdalPath", gdalPath());
    settings.setValue("tools/otbPath", otbPath());
    settings.setValue("logging/logToFile", logToFile());
    settings.setValue("logging/logFilePath", logFilePath());
}

void PreferencesDialog::onAccept()
{
    saveSettings();
    accept();
}

void PreferencesDialog::onBrowseGdalPath()
{
    QString path = QFileDialog::getExistingDirectory(this, tr("Select GDAL Tools Directory"));
    if (!path.isEmpty())
        m_gdalPathEdit->setText(path);
}

void PreferencesDialog::onBrowseOtbPath()
{
    QString path = QFileDialog::getExistingDirectory(this, tr("Select OTB Tools Directory"));
    if (!path.isEmpty())
        m_otbPathEdit->setText(path);
}

void PreferencesDialog::onBrowseLogPath()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Select Log File"), QString(),
                                                tr("Log files (*.log);;All files (*)"));
    if (!path.isEmpty())
        m_logFilePathEdit->setText(path);
}

void PreferencesDialog::onLogToFileToggled(bool checked)
{
    m_logFilePathEdit->setEnabled(checked);
}
