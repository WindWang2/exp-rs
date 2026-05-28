#include <QApplication>
#include <QgsApplication.h>
#include <QgsGui.h>
#include <QTimer>
#include <QFileInfo>

#include "gui/main_window.h"
#include "core/plugin_manager.h"

int main(int argc, char *argv[])
{
    qDebug() << "Starting SICNU GEO RS...";

    // Create QGIS application
    QgsApplication *app = new QgsApplication(argc, argv, true);
    app->setApplicationName("SICNU GEO RS");
    app->setApplicationVersion("2.0");
    app->setOrganizationName("SICNU");

    // Set prefix path and initialize
    qDebug() << "Setting prefix path...";
    QgsApplication::setPrefixPath("/home/kevin/projects/exp-rs", true);
    qDebug() << "Initializing QGIS...";
    QgsApplication::initQgis();
    qDebug() << "QGIS initialized";

    // Initialize QgsGui singleton
    qDebug() << "Initializing QgsGui...";
    QgsGui::instance();
    qDebug() << "QgsGui initialized";

    // Create main window
    qDebug() << "Creating window...";
    SicnuMainWindow window;
    window.initialize();

    qDebug() << "Showing window...";
    window.show();
    qDebug() << "Window shown";

    // Auto-load sample data if available
    QString samplePath = "/home/kevin/projects/exp-rs/data/sample_crops.tif";
    if (QFileInfo::exists(samplePath)) {
        QTimer::singleShot(500, [&window, samplePath]() {
            window.addRasterLayer();
        });
    }

    int result = app->exec();

    delete app;
    return result;
}
