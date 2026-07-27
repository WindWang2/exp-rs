// main_window_misc.cpp — Settings, help, processing UI, and panel layout
#include "main_window.h"

#include "app_paths.h"
#include "dialogs/preferences_dialog.h"
#include "processing/tools/tool_path_manager.h"

#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <QDebug>

#include <qgsbrowserdockwidget.h>
#include <qgsdockwidget.h>
#include <qgsgui.h>
#include <history/qgshistoryproviderregistry.h>
#include <history/qgshistorywidget.h>

// ── Settings Actions ──────────────────────────────────────────────────────
namespace {

bool loadThemeQss( const QString &relativePath )
{
    const QString qssPath = AppPaths::resolveDataPath( relativePath );
    QFile styleFile( qssPath );
    if ( !styleFile.open( QFile::ReadOnly ) )
        return false;
    qApp->setStyleSheet( QString::fromUtf8( styleFile.readAll() ) );
    styleFile.close();
    return true;
}

} // namespace

void QgisDesktopWindow::applyDarkPalette()
{
    // Fusion base palette aligned with Canopy Lab Night surfaces
    QPalette darkPalette;
    darkPalette.setColor( QPalette::Window, QColor( 0x1A, 0x1D, 0x23 ) );
    darkPalette.setColor( QPalette::WindowText, QColor( 0xE8, 0xEC, 0xF1 ) );
    darkPalette.setColor( QPalette::Base, QColor( 0x1E, 0x22, 0x29 ) );
    darkPalette.setColor( QPalette::AlternateBase, QColor( 0x25, 0x2A, 0x33 ) );
    darkPalette.setColor( QPalette::ToolTipBase, QColor( 0x25, 0x2A, 0x33 ) );
    darkPalette.setColor( QPalette::ToolTipText, QColor( 0xE8, 0xEC, 0xF1 ) );
    darkPalette.setColor( QPalette::Text, QColor( 0xE8, 0xEC, 0xF1 ) );
    darkPalette.setColor( QPalette::Button, QColor( 0x25, 0x2A, 0x33 ) );
    darkPalette.setColor( QPalette::ButtonText, QColor( 0xE8, 0xEC, 0xF1 ) );
    darkPalette.setColor( QPalette::BrightText, QColor( 0xF0, 0x71, 0x67 ) );
    darkPalette.setColor( QPalette::Link, QColor( 0x4D, 0xA3, 0xE0 ) );
    darkPalette.setColor( QPalette::Highlight, QColor( 0x2B, 0xB6, 0x73 ) );
    darkPalette.setColor( QPalette::HighlightedText, QColor( 0x1A, 0x1D, 0x23 ) );
    darkPalette.setColor( QPalette::PlaceholderText, QColor( 0xA8, 0xB0, 0xBC ) );
    qApp->setPalette( darkPalette );
}

void QgisDesktopWindow::applyUiTheme( const QString &theme )
{
    if ( QStyle *fusion = QStyleFactory::create( QStringLiteral( "Fusion" ) ) )
        qApp->setStyle( fusion );

    if ( theme == QLatin1String( "dark" ) )
    {
        applyDarkPalette();
        if ( !loadThemeQss( QStringLiteral( "resources/styles-dark.qss" ) ) )
            qWarning( "Could not load dark theme QSS" );
    }
    else
    {
        qApp->setPalette( QApplication::style()->standardPalette() );
        if ( !loadThemeQss( QStringLiteral( "resources/styles.qss" ) ) )
            qWarning( "Could not load light theme QSS" );
    }
}

void QgisDesktopWindow::options()
{
    PreferencesDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
    {
        ToolPathManager::instance().setGdalPath( dialog.gdalPath() );
        ToolPathManager::instance().setOtbPath( dialog.otbPath() );

        applyUiTheme( dialog.theme() );

        // Log-to-file takes effect after restart (sink is opened in main.cpp at startup).
        if ( dialog.logToFile() )
            statusBar()->showMessage( tr( "Preferences saved (log-to-file applies on next launch)" ), 4000 );
        else
            statusBar()->showMessage( tr( "Preferences saved" ), 3000 );
    }
}

// ── Processing Actions ─────────────────────────────────────────────────────
void QgisDesktopWindow::showProcessingToolbox()
{
    // Find and raise the processing dock
    for (QDockWidget *dock : findChildren<QDockWidget*>()) {
        if (dock->objectName() == "processingDock") {
            dock->show();
            dock->raise();
            break;
        }
    }
}

void QgisDesktopWindow::showProcessingHistory()
{
    // Create a dialog to show processing history
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Processing History"));
    dialog.setMinimumSize(600, 400);

    auto *layout = new QVBoxLayout(&dialog);

    // Get history entries from the registry
    QgsHistoryProviderRegistry *historyReg = QgsGui::historyProviderRegistry();
    if (!historyReg) {
        auto *label = new QLabel(tr("History registry not available."), &dialog);
        layout->addWidget(label);
        dialog.exec();
        return;
    }

    // Create a history widget
    QgsHistoryWidget *historyWidget = new QgsHistoryWidget(
        QString(), Qgis::HistoryProviderBackend::LocalProfile, historyReg, QgsHistoryWidgetContext(), &dialog);
    layout->addWidget(historyWidget);

    // Add close button
    auto *closeButton = new QPushButton(tr("Close"), &dialog);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(closeButton);

    dialog.exec();
}

// ── Help Actions ──────────────────────────────────────────────────────────
void QgisDesktopWindow::helpContents() { QMessageBox::information(this, "Help", "QGIS Help"); }
void QgisDesktopWindow::checkVersion() { QMessageBox::information(this, "Version", "SICNU GEO RS v0.9.2-dev"); }
void QgisDesktopWindow::about()
{
    QMessageBox::about(this, "About",
        "SICNU GEO RS\n\n"
        "Professional Remote Sensing Analysis Platform\n"
        "Built with QGIS C++ Libraries\n\n"
        "Version v0.9.2-dev\n\n"
        "Features:\n"
        "- Raster and vector layer support\n"
        "- QGIS-compatible layer properties\n"
        "- CRS/Projection selection\n"
        "- Native QGIS rendering performance");
}

void QgisDesktopWindow::loadSampleData()
{
    const QString samplesDir = AppPaths::samplesDataDir();
    QDir dir( samplesDir );
    if ( !dir.exists() )
    {
        QMessageBox::information( this, tr( "Sample Data" ),
                                  tr( "Sample data directory not found.\n"
                                      "Expected data/samples/ at the project root." ) );
        return;
    }

    // Load all sample raster files
    QStringList filters;
    filters << "*.tif" << "*.tiff";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);

    int loaded = 0;
    for (const QFileInfo &file : files) {
        loadRasterLayer(file.absoluteFilePath());
        loaded++;
    }

    // Load shapefiles
    filters.clear();
    filters << "*.shp";
    files = dir.entryInfoList(filters, QDir::Files);
    for (const QFileInfo &file : files) {
        loadVectorLayer(file.absoluteFilePath());
        loaded++;
    }

    statusBar()->showMessage(tr("Loaded %1 sample datasets").arg(loaded), 5000);
}

void QgisDesktopWindow::showGuidedWorkflows()
{
    if (m_workflowDock) {
        m_workflowDock->show();
        m_workflowDock->raise();
    }
}
void QgisDesktopWindow::restorePanelState()
{
    QSettings settings;
    // v2: product shell (Ribbon + Task panel). Old state restored classic toolbars
    // and a crowded right dock stack that fought the new chrome.
    // v7: full-width top ribbon dock (setCorner Top*→TopDock) — drop prior states.
    constexpr int kShellLayoutVersion = 7;
    const int savedVersion = settings.value( QStringLiteral( "mainwindow/shellLayoutVersion" ), 0 ).toInt();

    if ( savedVersion >= kShellLayoutVersion )
    {
        const QByteArray state = settings.value( QStringLiteral( "mainwindow/state" ) ).toByteArray();
        if ( !state.isEmpty() )
            restoreState( state );
    }
    else
    {
        // Drop pre-shell layout so applyProductShellLayout becomes the baseline.
        settings.remove( QStringLiteral( "mainwindow/state" ) );
        settings.setValue( QStringLiteral( "mainwindow/shellLayoutVersion" ), kShellLayoutVersion );
    }

    const QByteArray geometry = settings.value( QStringLiteral( "mainwindow/geometry" ) ).toByteArray();
    if ( !geometry.isEmpty() )
        restoreGeometry( geometry );
}

void QgisDesktopWindow::resetPanelLayout()
{
    QSettings settings;
    settings.remove( QStringLiteral( "mainwindow/state" ) );
    settings.remove( QStringLiteral( "mainwindow/geometry" ) );
    settings.setValue( QStringLiteral( "mainwindow/shellLayoutVersion" ), 7 );

    // Dock areas back to defaults, then apply product shell visibility.
    if ( m_layersDock )
        addDockWidget( Qt::LeftDockWidgetArea, m_layersDock );
    if ( m_browserDock )
        addDockWidget( Qt::LeftDockWidgetArea, m_browserDock );
    if ( m_layersDock && m_browserDock )
        tabifyDockWidget( m_layersDock, m_browserDock );
    if ( m_taskPanelDock )
        addDockWidget( Qt::RightDockWidgetArea, m_taskPanelDock );
    if ( m_processingDock )
        addDockWidget( Qt::RightDockWidgetArea, m_processingDock );
    if ( m_overviewDock )
        addDockWidget( Qt::RightDockWidgetArea, m_overviewDock );
    if ( m_identifyDock )
        addDockWidget( Qt::RightDockWidgetArea, m_identifyDock );
    if ( m_spectralDock )
        addDockWidget( Qt::RightDockWidgetArea, m_spectralDock );
    if ( m_histogramStretchDock )
        addDockWidget( Qt::RightDockWidgetArea, m_histogramStretchDock );
    if ( m_workflowDock )
        addDockWidget( Qt::RightDockWidgetArea, m_workflowDock );
    if ( m_logDock )
        addDockWidget( Qt::BottomDockWidgetArea, m_logDock );

    applyProductShellLayout();
    statusBar()->showMessage( tr( "布局已重置为 Ribbon + 任务面板模式" ), 3000 );
}

void QgisDesktopWindow::savePanelState()
{
    QSettings settings;
    settings.setValue( QStringLiteral( "mainwindow/state" ), saveState() );
    settings.setValue( QStringLiteral( "mainwindow/geometry" ), saveGeometry() );
    settings.setValue( QStringLiteral( "mainwindow/shellLayoutVersion" ), 7 );
}

void QgisDesktopWindow::closeEvent( QCloseEvent *event )
{
    if (!checkUnsavedChanges())
    {
        event->ignore();
        return;
    }
    savePanelState();
    event->accept();
}
