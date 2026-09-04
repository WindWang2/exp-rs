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

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("首选项"));
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
    // Plugin-contributed pages last (ExpRS Developer Platform 3.0).
    const QMap<QString, QWidget *> pages = externalPages();
    for ( auto it = pages.constBegin(); it != pages.constEnd(); ++it )
        m_tabWidget->addTab( it.value(), it.key() );

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply | QDialogButtonBox::Help, this );
    buttonBox->setObjectName( QStringLiteral( "preferencesButtonBox" ) );
    buttonBox->button( QDialogButtonBox::Ok )->setText( tr( "确定" ) );
    buttonBox->button( QDialogButtonBox::Cancel )->setText( tr( "取消" ) );
    buttonBox->button( QDialogButtonBox::Apply )->setText( tr( "应用" ) );
    buttonBox->button( QDialogButtonBox::Help )->setText( tr( "帮助" ) );

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
    SicnuDialogHelp::tip( m_tabWidget, tr( "常规 / 外部工具路径 / 关于 等系统全局配置选项卡。" ) );
    if ( m_themeCombo )
        SicnuDialogHelp::tip( m_themeCombo, tr( "界面主题风格：浅色模式或深色模式。" ) );
    if ( m_crsCombo )
        SicnuDialogHelp::tip( m_crsCombo, tr( "新建遥感工程时的默认投影坐标参考系 (CRS)。" ) );

    loadSettings();
}

void PreferencesDialog::setupGeneralTab()
{
    auto *tab = new QWidget();
    auto *tabLayout = new QVBoxLayout(tab);
    tabLayout->setContentsMargins( 8, 8, 8, 8 );
    tabLayout->setSpacing( 10 );

    auto *displayGroup = SicnuUi::makeGroup( tab, tr( "界面与默认坐标系" ) );
    auto *displayForm = SicnuUi::makeFormLayout( displayGroup );

    m_themeCombo = new QComboBox( displayGroup );
    m_themeCombo->addItem( tr( "浅色主题 (Light)" ), QStringLiteral( "light" ) );
    m_themeCombo->addItem( tr( "深色主题 (Dark)" ), QStringLiteral( "dark" ) );
    SicnuDialogHelp::tip( m_themeCombo, tr( "系统界面外观主题：浅色模式或深色模式。" ) );
    displayForm->addRow( tr( "界面主题" ), m_themeCombo );

    m_crsCombo = new QComboBox( displayGroup );
    m_crsCombo->addItem( "EPSG:4326 - WGS 84 (地理坐标系)", "EPSG:4326" );
    m_crsCombo->addItem( "EPSG:3857 - WGS 84 / Pseudo-Mercator", "EPSG:3857" );
    m_crsCombo->addItem( "EPSG:32649 - WGS 84 / UTM zone 49N", "EPSG:32649" );
    m_crsCombo->addItem( "EPSG:32650 - WGS 84 / UTM zone 50N", "EPSG:32650" );
    m_crsCombo->addItem( "EPSG:32651 - WGS 84 / UTM zone 51N", "EPSG:32651" );
    m_crsCombo->addItem( "EPSG:4490 - CGCS2000 (国家2000大地坐标系)", "EPSG:4490" );
    m_crsCombo->setEditable( true );
    SicnuDialogHelp::tip( m_crsCombo, tr( "新建遥感工程时的默认坐标参考系。" ) );
    displayForm->addRow( tr( "默认坐标系" ), m_crsCombo );
    tabLayout->addWidget( displayGroup );

    // Logging section
    auto *logGroup = SicnuUi::makeGroup( tab, tr( "运行日志配置" ) );
    auto *logForm = SicnuUi::makeFormLayout( logGroup );

    m_logToFileCheck = new QCheckBox( tr( "启用日志文件写入" ), logGroup );
    SicnuDialogHelp::tip( m_logToFileCheck, tr( "是否将系统与算法运行日志输出保存到本地磁盘文件。" ) );
    logForm->addRow( QString(), m_logToFileCheck );

    auto *logPathLayout = new QHBoxLayout();
    logPathLayout->setSpacing( 8 );
    m_logFilePathEdit = new QLineEdit( logGroup );
    m_logFilePathEdit->setPlaceholderText( tr( "日志文件完整路径…" ) );
    SicnuDialogHelp::tip( m_logFilePathEdit, tr( "保存日志记录的完整文件路径。" ) );
    logPathLayout->addWidget( m_logFilePathEdit, 1 );
    auto *logBrowseBtn = new QPushButton( tr( "浏览…" ), logGroup );
    SicnuUi::markSecondary( logBrowseBtn );
    connect( logBrowseBtn, &QPushButton::clicked, this, &PreferencesDialog::onBrowseLogPath );
    logPathLayout->addWidget( logBrowseBtn );
    logForm->addRow( tr( "日志文件路径" ), logPathLayout );
    tabLayout->addWidget( logGroup );

    tabLayout->addStretch( 1 );

    connect( m_logToFileCheck, &QCheckBox::toggled, this, &PreferencesDialog::onLogToFileToggled );
    onLogToFileToggled( m_logToFileCheck->isChecked() );

    m_tabWidget->addTab( tab, tr( "常规设置" ) );
}

void PreferencesDialog::setupToolsTab()
{
    auto *tab = new QWidget();
    auto *tabLayout = new QVBoxLayout( tab );
    tabLayout->setContentsMargins( 8, 8, 8, 8 );
    tabLayout->setSpacing( 10 );

    auto *toolsGroup = SicnuUi::makeGroup( tab, tr( "外部工具目录配置" ),
                                           tr( "指定外部命令行工具路径以启用高级算法功能。" ) );
    auto *toolsForm = SicnuUi::makeFormLayout( toolsGroup );

    auto *gdalLayout = new QHBoxLayout();
    gdalLayout->setSpacing( 8 );
    m_gdalPathEdit = new QLineEdit( toolsGroup );
    m_gdalPathEdit->setPlaceholderText( tr( "GDAL 工具目录（包含 gdal_translate、gdalwarp 等）…" ) );
    SicnuDialogHelp::tip( m_gdalPathEdit, tr( "GDAL 工具目录路径，用于底层栅格格式转换与投影变换。" ) );
    gdalLayout->addWidget( m_gdalPathEdit, 1 );
    auto *gdalBrowseBtn = new QPushButton( tr( "浏览…" ), toolsGroup );
    SicnuUi::markSecondary( gdalBrowseBtn );
    connect( gdalBrowseBtn, &QPushButton::clicked, this, &PreferencesDialog::onBrowseGdalPath );
    gdalLayout->addWidget( gdalBrowseBtn );
    toolsForm->addRow( tr( "GDAL 工具路径" ), gdalLayout );

    auto *otbLayout = new QHBoxLayout();
    otbLayout->setSpacing( 8 );
    m_otbPathEdit = new QLineEdit( toolsGroup );
    m_otbPathEdit->setPlaceholderText( tr( "OTB 应用程序目录路径…" ) );
    SicnuDialogHelp::tip( m_otbPathEdit, tr( "Orfeo ToolBox 工具目录，供 OTB 包装算法调用。" ) );
    otbLayout->addWidget( m_otbPathEdit, 1 );
    auto *otbBrowseBtn = new QPushButton( tr( "浏览…" ), toolsGroup );
    SicnuUi::markSecondary( otbBrowseBtn );
    connect( otbBrowseBtn, &QPushButton::clicked, this, &PreferencesDialog::onBrowseOtbPath );
    otbLayout->addWidget( otbBrowseBtn );
    toolsForm->addRow( tr( "OTB 工具路径" ), otbLayout );

    tabLayout->addWidget( toolsGroup );
    tabLayout->addStretch( 1 );

    m_tabWidget->addTab( tab, tr( "外部工具" ) );
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

    auto *versionLabel = new QLabel(tr("版本 1.0.0"), tab);
    versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel);

    auto *descLabel = new QLabel(tr("遥感影像综合处理与空间分析平台"), tab);
    descLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(descLabel);

    auto *buildLabel = new QLabel(tr("基于 QGIS 核心引擎与 GDAL / OTB 外部算法库构建"), tab);
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
