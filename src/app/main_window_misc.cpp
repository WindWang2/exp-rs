// main_window_misc.cpp — Settings, help, processing UI, and panel layout
#include "main_window.h"

#include "app_paths.h"
#include "dialogs/preferences_dialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QStyleFactory>
#include <QVBoxLayout>

#include <qgsbrowserdockwidget.h>
#include <qgsdockwidget.h>
#include <qgsgui.h>
#include <history/qgshistoryproviderregistry.h>
#include <history/qgshistorywidget.h>

// ── Settings Actions ──────────────────────────────────────────────────────
void QgisDesktopWindow::applyDarkPalette()
{
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    qApp->setPalette(darkPalette);
}

void QgisDesktopWindow::options()
{
    PreferencesDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
    {
        // Apply theme if changed
        QString theme = dialog.theme();
        if (theme == "dark")
        {
            qApp->setStyle(QStyleFactory::create("Fusion"));
            applyDarkPalette();
        }
        else
        {
            // Reset to default light theme
            qApp->setPalette(QApplication::style()->standardPalette());
        }

        statusBar()->showMessage(tr("Preferences saved"), 3000);
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
    // Find the samples_data directory relative to the application
    QString samplesDir = AppPaths::dataDir() + "/samples_data";
    QDir dir(samplesDir);
    if (!dir.exists()) {
        // Try relative to executable
        samplesDir = QCoreApplication::applicationDirPath() + "/../samples_data";
        dir.setPath(samplesDir);
    }
    if (!dir.exists()) {
        QMessageBox::information(this, tr("Sample Data"),
                                 tr("Sample data directory not found.\n"
                                    "Please ensure samples_data/ exists in the application directory."));
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
void QgisDesktopWindow::savePanelState()
{
    QSettings settings;
    settings.setValue( "mainwindow/state", saveState() );
    settings.setValue( "mainwindow/geometry", saveGeometry() );
}

void QgisDesktopWindow::restorePanelState()
{
    QSettings settings;
    QByteArray state = settings.value( "mainwindow/state" ).toByteArray();
    if ( !state.isEmpty() )
    {
        restoreState( state );
    }
    QByteArray geometry = settings.value( "mainwindow/geometry" ).toByteArray();
    if ( !geometry.isEmpty() )
    {
        restoreGeometry( geometry );
    }
}

void QgisDesktopWindow::resetPanelLayout()
{
    QSettings settings;
    settings.remove( "mainwindow/state" );
    settings.remove( "mainwindow/geometry" );

    // Reset to default layout
    if ( m_layersDock )
        addDockWidget( Qt::LeftDockWidgetArea, m_layersDock );
    if ( m_browserDock )
        addDockWidget( Qt::LeftDockWidgetArea, m_browserDock );
    if ( m_processingDock )
        addDockWidget( Qt::RightDockWidgetArea, m_processingDock );
    if ( m_overviewDock )
        addDockWidget( Qt::RightDockWidgetArea, m_overviewDock );
    if ( m_identifyDock )
        addDockWidget( Qt::RightDockWidgetArea, m_identifyDock );
    if ( m_spectralDock )
        addDockWidget( Qt::RightDockWidgetArea, m_spectralDock );

    statusBar()->showMessage( tr( "Layout reset to defaults" ), 3000 );
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
