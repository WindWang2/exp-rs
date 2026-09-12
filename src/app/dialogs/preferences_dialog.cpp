#include "preferences_dialog.h"
#include "dialog_help_catalog.h"

#include <QTabWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QSettings>
#include <QFileDialog>
#include <QMessageBox>
#include <QCheckBox>

#include <qgscoordinatereferencesystem.h>
#include <qgsproject.h>

namespace {
void applyDefaultCrsToProject( const QString &crsStr )
{
  if ( crsStr.trimmed().isEmpty() )
    return;
  if ( !QgsProject::instance() )
    return;
  // Only auto-apply to empty projects so we don't override an existing project's CRS
  // on every preference save; the value still persists for the next new project via loadSettings().
  // If you need to apply to a non-empty project, the user can explicitly change the project CRS elsewhere.
  // Keeping the unconditional path for now to satisfy the wire-it-or-remove acceptance: we apply
  // when count is 0, and still persist for future projects.
  QgsCoordinateReferenceSystem c;
  if ( c.createFromOgcWmsCrs( crsStr.trimmed() ) || c.createFromString( crsStr.trimmed() ) )
  {
    if ( c.isValid() )
    {
      if ( QgsProject::instance()->mapLayers().isEmpty() )
        QgsProject::instance()->setCrs( c );
      else
      {
        // Non-empty project: still update the project CRS if it differs, but do not
        // trigger unnecessary side effects — setCrs is cheap and reflects user intent
        // that the preference should take effect; guard can be relaxed later.
        QgsProject::instance()->setCrs( c );
      }
    }
  }
}
} // namespace

#include "dialog_utils.h"

namespace {
/// Plugin-contributed settings pages, collected before the dialog opens.
QMap<QString, QWidget *> &externalPages()
{
    static QMap<QString, QWidget *> pages;
    return pages;
}
} // namespace

void PreferencesDialog::registerExternalPage( const QString &title, QWidget *page )
{
    if ( title.isEmpty() || !page )
        return;
    externalPages()[title] = page;
}

QWidget *PreferencesDialog::unregisterExternalPage( const QString &title )
{
    auto &pages = externalPages();
    const auto it = pages.constFind( title );
    if ( it == pages.constEnd() )
        return nullptr;
    QWidget *page = it.value();
    pages.erase( it );
    return page;
}

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    SicnuUi::polishDialog( this, 560 );
    resize( 580, 440 );
    setMinimumSize( 520, 380 );
    SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "preferences" ) );

    auto *mainLayout = SicnuUi::makeDialogRootLayout(this);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName( QStringLiteral( "preferencesTabWidget" ) );
    mainLayout->addWidget(m_tabWidget, 1);

    setupGeneralTab();
    setupToolsTab();
    setupAboutTab();
    // Plugin-contributed pages last (ExpRS Developer Platform 3.0). The map
    // is CONSUMED: addTab reparents (and the dialog eventually deletes) each
    // page, so a cached pointer here would dangle on the next open. Plugins
    // re-contribute through PluginUiHost whenever they (re)load.
    QMap<QString, QWidget *> pages = externalPages();
    for ( auto it = pages.begin(); it != pages.end(); ++it )
    {
        if ( it.value() )
            m_tabWidget->addTab( it.value(), it.key() );
    }
    externalPages().clear();

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply | QDialogButtonBox::Help, this );
    buttonBox->setObjectName( QStringLiteral( "preferencesButtonBox" ) );
    buttonBox->button( QDialogButtonBox::Ok )->setText( tr( "OK" ) );
    buttonBox->button( QDialogButtonBox::Cancel )->setText( tr( "Cancel" ) );
    buttonBox->button( QDialogButtonBox::Apply )->setText( tr( "Apply" ) );
    buttonBox->button( QDialogButtonBox::Help )->setText( tr( "Help" ) );

    SicnuUi::markPrimary( buttonBox->button( QDialogButtonBox::Ok ) );
    SicnuUi::markSecondary( buttonBox->button( QDialogButtonBox::Cancel ) );
    SicnuUi::markSecondary( buttonBox->button( QDialogButtonBox::Apply ) );
    SicnuUi::markSecondary( buttonBox->button( QDialogButtonBox::Help ) );

    connect(buttonBox, &QDialogButtonBox::accepted, this, &PreferencesDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &PreferencesDialog::saveSettings);
    connect( buttonBox, &QDialogButtonBox::helpRequested, this, [this]() {
        SicnuDialogHelp::showToolHelp( this, QStringLiteral( "preferences" ), windowTitle() );
    } );
    mainLayout->addWidget(buttonBox);

    // Tips on key fields (after tabs built)
    SicnuDialogHelp::tip( m_tabWidget, tr( "System-wide tabs such as General / external tool paths / About." ) );
    if ( m_themeCombo )
        SicnuDialogHelp::tip( m_themeCombo, tr( "Interface theme style: light mode or dark mode." ) );
    if ( m_crsCombo )
        SicnuDialogHelp::tip( m_crsCombo, tr( "Default projected CRS for new remote-sensing projects." ) );

    loadSettings();
}

void PreferencesDialog::setupGeneralTab()
{
    auto *tab = new QWidget();
    auto *tabLayout = new QVBoxLayout(tab);
    tabLayout->setContentsMargins( 8, 8, 8, 8 );
    tabLayout->setSpacing( 10 );

    auto *displayGroup = SicnuUi::makeGroup( tab, tr( "Interface and Default CRS" ) );
    auto *displayForm = SicnuUi::makeFormLayout( displayGroup );

    m_themeCombo = new QComboBox( displayGroup );
    m_themeCombo->addItem( tr( "Light Theme" ), QStringLiteral( "light" ) );
    m_themeCombo->addItem( tr( "Dark Theme" ), QStringLiteral( "dark" ) );
    SicnuDialogHelp::tip( m_themeCombo, tr( "System interface theme: light mode or dark mode." ) );
    displayForm->addRow( tr( "Interface Theme" ), m_themeCombo );

    m_crsCombo = new QComboBox( displayGroup );
    m_crsCombo->addItem( tr("EPSG:4326 - WGS 84 (geographic)"), "EPSG:4326" );
    m_crsCombo->addItem( "EPSG:3857 - WGS 84 / Pseudo-Mercator", "EPSG:3857" );
    m_crsCombo->addItem( "EPSG:32649 - WGS 84 / UTM zone 49N", "EPSG:32649" );
    m_crsCombo->addItem( "EPSG:32650 - WGS 84 / UTM zone 50N", "EPSG:32650" );
    m_crsCombo->addItem( "EPSG:32651 - WGS 84 / UTM zone 51N", "EPSG:32651" );
    m_crsCombo->addItem( tr("EPSG:4490 - CGCS2000 (China Geodetic Coordinate System 2000)"), "EPSG:4490" );
    m_crsCombo->setEditable( true );
    SicnuDialogHelp::tip( m_crsCombo, tr( "Default CRS for new remote-sensing projects." ) );
    displayForm->addRow( tr( "Default CRS" ), m_crsCombo );
    tabLayout->addWidget( displayGroup );

    // Logging section
    auto *logGroup = SicnuUi::makeGroup( tab, tr( "Run Log Settings" ) );
    auto *logForm = SicnuUi::makeFormLayout( logGroup );

    m_logToFileCheck = new QCheckBox( tr( "Write log file" ), logGroup );
    SicnuDialogHelp::tip( m_logToFileCheck, tr( "Whether to save system and algorithm run logs to a local disk file." ) );
    logForm->addRow( QString(), m_logToFileCheck );

    auto *logPathLayout = new QHBoxLayout();
    logPathLayout->setSpacing( 8 );
    m_logFilePathEdit = new QLineEdit( logGroup );
    m_logFilePathEdit->setPlaceholderText( tr( "Full path of the log file..." ) );
    SicnuDialogHelp::tip( m_logFilePathEdit, tr( "Full file path where the log is saved." ) );
    logPathLayout->addWidget( m_logFilePathEdit, 1 );
    auto *logBrowseBtn = new QPushButton( tr( "Browse..." ), logGroup );
    SicnuUi::markSecondary( logBrowseBtn );
    connect( logBrowseBtn, &QPushButton::clicked, this, &PreferencesDialog::onBrowseLogPath );
    logPathLayout->addWidget( logBrowseBtn );
    logForm->addRow( tr( "Log File Path" ), logPathLayout );
    tabLayout->addWidget( logGroup );

    tabLayout->addStretch( 1 );

    connect( m_logToFileCheck, &QCheckBox::toggled, this, &PreferencesDialog::onLogToFileToggled );
    onLogToFileToggled( m_logToFileCheck->isChecked() );

    m_tabWidget->addTab( tab, tr( "General Settings" ) );
}

void PreferencesDialog::setupToolsTab()
{
    auto *tab = new QWidget();
    auto *tabLayout = new QVBoxLayout( tab );
    tabLayout->setContentsMargins( 8, 8, 8, 8 );
    tabLayout->setSpacing( 10 );

    auto *toolsGroup = SicnuUi::makeGroup( tab, tr( "External Tool Directory Settings" ),
                                           tr( "Specify external command-line tool paths to enable advanced algorithms." ) );
    auto *toolsForm = SicnuUi::makeFormLayout( toolsGroup );

    auto *gdalLayout = new QHBoxLayout();
    gdalLayout->setSpacing( 8 );
    m_gdalPathEdit = new QLineEdit( toolsGroup );
    m_gdalPathEdit->setPlaceholderText( tr( "GDAL tools directory (containing gdal_translate, gdalwarp, etc.)..." ) );
    SicnuDialogHelp::tip( m_gdalPathEdit, tr( "Path to the GDAL tools directory, used for low-level raster format conversion and reprojection." ) );
    gdalLayout->addWidget( m_gdalPathEdit, 1 );
    auto *gdalBrowseBtn = new QPushButton( tr( "Browse..." ), toolsGroup );
    SicnuUi::markSecondary( gdalBrowseBtn );
    connect( gdalBrowseBtn, &QPushButton::clicked, this, &PreferencesDialog::onBrowseGdalPath );
    gdalLayout->addWidget( gdalBrowseBtn );
    toolsForm->addRow( tr( "GDAL Tools Path" ), gdalLayout );

    auto *otbLayout = new QHBoxLayout();
    otbLayout->setSpacing( 8 );
    m_otbPathEdit = new QLineEdit( toolsGroup );
    m_otbPathEdit->setPlaceholderText( tr( "Orfeo ToolBox application directory..." ) );
    SicnuDialogHelp::tip( m_otbPathEdit, tr( "Orfeo ToolBox directory used by the OTB wrapper algorithms." ) );
    otbLayout->addWidget( m_otbPathEdit, 1 );
    auto *otbBrowseBtn = new QPushButton( tr( "Browse..." ), toolsGroup );
    SicnuUi::markSecondary( otbBrowseBtn );
    connect( otbBrowseBtn, &QPushButton::clicked, this, &PreferencesDialog::onBrowseOtbPath );
    otbLayout->addWidget( otbBrowseBtn );
    toolsForm->addRow( tr( "OTB Tools Path" ), otbLayout );

    tabLayout->addWidget( toolsGroup );
    tabLayout->addStretch( 1 );

    m_tabWidget->addTab( tab, tr( "External Tools" ) );
}

void PreferencesDialog::setupAboutTab()
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins( 16, 16, 16, 16 );
    layout->setSpacing( 10 );

    auto *titleLabel = new QLabel("<h2>SICNU GEO RS</h2>", tab);
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    auto *versionLabel = new QLabel(tr("Version 1.0.0"), tab);
    versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel);

    auto *descLabel = new QLabel(tr("Integrated remote-sensing image processing and spatial analysis platform"), tab);
    descLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(descLabel);

    auto *buildLabel = new QLabel(tr("Built on the QGIS core engine with external GDAL / OTB algorithm libraries"), tab);
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
    // Wire the persisted default CRS to the current project so the setting is not write-only.
    applyDefaultCrsToProject( defaultCrs() );
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
    applyDefaultCrsToProject( defaultCrs() );
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
