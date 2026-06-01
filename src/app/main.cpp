/*
 * SICNU GEO RS - Professional QGIS Desktop Interface (Pure C++)
 *
 * Full QGIS-compatible interface with:
 * - Layer tree with right-click context menu
 * - Raster/Vector layer properties dialogs
 * - CRS/Projection selection
 * - Native QGIS rendering performance
 */

#include <QApplication>
#include <QTimer>
#include <QFileInfo>
#include <QFile>
#include <QFontDatabase>

// QGIS C++ includes
#include <qgsapplication.h>
#include <qgsmessagelog.h>
#include <qgis.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgsstyle.h>
#include <processing/qgsprocessingregistry.h>

// QgsGui singleton
#include <qgsgui.h>

// App includes
#include "app/app_paths.h"
#include "app/main_window.h"

// Processing providers
#include "processing/providers/gdal_tools/provider.h"
#include "processing/providers/otb_tools/provider.h"
#include "processing/providers/qgis_algorithms/provider.h"

// Python embedding (disabled — Python runtime removed, pybind11 console deferred)
// #include "python/qgis_python.h"

// Qt message handler — routes qDebug/qWarning/qCritical to QgsMessageLog
static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Qgis::MessageLevel level;
    switch (type) {
        case QtDebugMsg:    level = Qgis::MessageLevel::Info; break;
        case QtWarningMsg:  level = Qgis::MessageLevel::Warning; break;
        case QtCriticalMsg: level = Qgis::MessageLevel::Critical; break;
        case QtFatalMsg:    level = Qgis::MessageLevel::Critical; break;
        case QtInfoMsg:     level = Qgis::MessageLevel::Info; break;
    }
    // Use context category as tag, fall back to "qt"
    QString tag = context.category && context.category[0]
        ? QString::fromUtf8(context.category) : QStringLiteral("qt");
    QgsMessageLog::logMessage(msg, tag, level);
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(messageHandler);
    qDebug() << "Starting SICNU GEO RS...";

    // Create QGIS application (inherits QApplication, handles all Qt + QGIS init)
    // Heap-allocated to avoid destructor crash during DSO cleanup
    QgsApplication *app = new QgsApplication(argc, argv, true);
    app->setApplicationName("SICNU GEO RS");
    app->setApplicationVersion("1.0");
    app->setOrganizationName("SICNU");

    // Set prefix path and initialize providers (GDAL, PROJ, etc.)
    qDebug() << "Setting prefix path...";
    QgsApplication::setPrefixPath(AppPaths::prefixPath(), true);
    qDebug() << "Initializing QGIS...";
    QgsApplication::initQgis();
    qDebug() << "QGIS initialized";

    // Import predefined color ramps (Viridis, Magma, Spectral, etc.)
    QgsStyle *style = QgsStyle::defaultStyle();
    if (style->colorRampNames().isEmpty()) {
        QString xmlPath = AppPaths::resolveDataPath("qgis_ref/resources/symbology-style.xml");
        if (QFileInfo::exists(xmlPath)) {
            style->importXml(xmlPath);
            qDebug() << "Imported color ramps from:" << xmlPath;
        } else {
            qWarning() << "symbology-style.xml not found at:" << xmlPath;
        }
    }

    // Register processing algorithms (sicnu_native merged into qgis_algorithms)

    // Python embedding disabled — Python runtime removed
    // QgisPython::instance().initialize();
    // QgisPython::instance().loadBindings();

    // Initialize QgsGui singleton (required for QGIS dialogs)
    qDebug() << "Initializing QgsGui...";
    QgsGui::instance();
    qDebug() << "QgsGui initialized";

    // Register processing providers
    QgsApplication::processingRegistry()->addProvider(new GdalToolsProvider());
    QgsApplication::processingRegistry()->addProvider(new OtbToolsProvider());
    QgsApplication::processingRegistry()->addProvider(new QgisAlgorithmsProvider());
    qDebug() << "Processing providers registered";

    // Register fonts
    QString fontDir = AppPaths::resolveDataPath("resources/fonts");
    QFontDatabase::addApplicationFont(fontDir + "/IBMPlexSans.ttf");
    QFontDatabase::addApplicationFont(fontDir + "/IBMPlexMono-Regular.ttf");

    // Load QSS theme
    QString qssPath = AppPaths::resolveDataPath("resources/styles.qss");
    QFile styleFile(qssPath);
    if (styleFile.open(QFile::ReadOnly)) {
        app->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
        styleFile.close();
        qDebug() << "Theme loaded:" << qssPath;
    } else {
        qWarning() << "Could not load theme:" << qssPath;
    }

    // Create and initialize layer tree
    qDebug() << "Creating window...";
    QgisDesktopWindow window;
    qDebug() << "Initializing layer tree...";
    window.initLayerTree();

    qDebug() << "Showing window...";
    window.show();
    qDebug() << "Window shown";

    // Auto-load sample data if available
    QString samplePath = AppPaths::resolveDataPath("data/sample_crops.tif");
    if (QFileInfo::exists(samplePath)) {
        QTimer::singleShot(500, [&window, samplePath]() {
            auto *layer = new QgsRasterLayer(samplePath, "sample_crops");
            if (layer->isValid()) {
                QgsProject::instance()->addMapLayer(layer);
                QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
                QgsLayerTreeGroup *group = root->findGroup("Raster Layers");
                if (!group) group = root->addGroup("Raster Layers");
                group->addLayer(layer);
                window.mapCanvas()->setExtent(layer->extent());
                window.mapCanvas()->setLayers(root->layerOrder());
                window.mapCanvas()->refresh();
            }
        });
    }

    int result = app->exec();

    // Python embedding disabled
    // QgisPython::instance().finalize();

    return result;
}

#include "main.moc"
