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
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QSettings>
#include <QStandardPaths>
#include <QDateTime>
#include <QStyleFactory>
#include <memory>

// QGIS C++ includes
#include <qgsapplication.h>
#include <qgsmessagelog.h>
#include <qgis.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgsstyle.h>
#include <qgsvectorlayer.h>
#include <qgsvectordataprovider.h>
#include <processing/qgsprocessingregistry.h>

// QgsGui singleton
#include <qgsgui.h>

// App includes
#include "app/app_paths.h"
#include "app/main_window.h"
#include "agent/mcp_server.h"
#include "processing/framework/algorithm_engine.h"

// Processing providers
#include "processing/providers/gdal_tools/provider.h"
#include "processing/providers/otb_tools/provider.h"
#include "processing/providers/qgis_algorithms/provider.h"
#include "processing/providers/generic_cli/provider.h"
#include "processing/tools/tool_path_manager.h"

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

    bool mcpMode = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--mcp") == 0) {
            mcpMode = true;
            break;
        }
    }

    // Create QGIS application (inherits QApplication, handles all Qt + QGIS init)
    // Heap-allocated to avoid destructor crash during DSO cleanup
    QgsApplication *app = new QgsApplication(argc, argv, !mcpMode);
    app->setApplicationName("SICNU GEO RS");
    app->setApplicationDisplayName(QStringLiteral("RS Studio"));
    app->setApplicationVersion("1.0");
    app->setOrganizationName("SICNU");
    // Application / window icon (resources/icons/app_icon.svg via icons.qrc)
    {
        const QIcon appIcon(QStringLiteral(":/icons/app_icon"));
        if (!appIcon.isNull())
            app->setWindowIcon(appIcon);
    }

    // Set prefix path and initialize providers (GDAL, PROJ, etc.)
    qDebug() << "Setting prefix path...";
    QgsApplication::setPrefixPath(AppPaths::prefixPath(), true);
    qDebug() << "Initializing QGIS...";
    QgsApplication::initQgis();
    qDebug() << "QGIS initialized";

    // Import predefined color ramps (Viridis, Magma, Spectral, etc.)
    QgsStyle *style = QgsStyle::defaultStyle();
    if (style->colorRampNames().isEmpty()) {
        const QString resDir = AppPaths::qgisRefResourcesDir();
        const QString xmlPath = resDir.isEmpty()
            ? QString()
            : QDir( resDir ).filePath( QStringLiteral( "symbology-style.xml" ) );
        if ( !xmlPath.isEmpty() && QFileInfo::exists( xmlPath ) ) {
            style->importXml(xmlPath);
            qDebug() << "Imported color ramps from:" << xmlPath;
        } else {
            qWarning() << "symbology-style.xml not found (tried refs/qgis and qgis_ref resources)";
        }
    }

    // Initialize AlgorithmEngine facade (registers providers and tool paths)
    sicnu::AlgorithmEngine::instance().initialize();
    qDebug() << "AlgorithmEngine initialized with" << sicnu::AlgorithmEngine::instance().registeredAlgorithms().size() << "algorithms";

    if (mcpMode) {
        std::cerr << "Initializing MCP Mode..." << std::endl;
        McpServer server;
        server.start(app);
        int result = app->exec();
        delete app;
        return result;
    }

    // Initialize QgsGui singleton (required for QGIS dialogs)
    qDebug() << "Initializing QgsGui...";
    QgsGui::instance();
    qDebug() << "QgsGui initialized";

    // Log-to-file support
    QSettings appSettings;
    QFile *logFile = nullptr;
    if (appSettings.value("logging/logToFile", false).toBool()) {
        QString logPath = appSettings.value("logging/logFilePath").toString();
        if (logPath.isEmpty()) {
            logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + "/sicnu_geo.log";
        }
        logFile = new QFile(logPath);
        if (logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QObject::connect(QgsApplication::messageLog(), &QgsMessageLog::messageReceivedWithFormat,
                             logFile, [logFile](const QString &message, const QString &tag,
                                                Qgis::MessageLevel level, Qgis::StringFormat) {
                                 static const char *levelNames[] = {"INFO", "WARNING", "CRITICAL", "SUCCESS", ""};
                                 const char *lvl = (static_cast<int>(level) >= 0 && static_cast<int>(level) < 5)
                                                    ? levelNames[static_cast<int>(level)] : "INFO";
                                 QString line = QDateTime::currentDateTime().toString(Qt::ISODate)
                                                + " [" + QString::fromUtf8(lvl) + "] " + tag + ": " + message + "\n";
                                 logFile->write(line.toUtf8());
                             });
            qDebug() << "Logging to file:" << logPath;
        } else {
            qWarning() << "Failed to open log file:" << logPath;
            delete logFile;
            logFile = nullptr;
        }
    }

    // Register fonts
    QString fontDir = AppPaths::resolveDataPath("resources/fonts");
    QFontDatabase::addApplicationFont(fontDir + "/IBMPlexSans.ttf");
    QFontDatabase::addApplicationFont(fontDir + "/IBMPlexMono-Regular.ttf");

    // Light theme: Fusion + Canopy Lab QSS
    if ( QStyle *fusion = QStyleFactory::create( QStringLiteral( "Fusion" ) ) )
        app->setStyle( fusion );
    QString qssPath = AppPaths::resolveDataPath("resources/styles.qss");
    QFile styleFile(qssPath);
    if (styleFile.open(QFile::ReadOnly)) {
        app->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
        styleFile.close();
        qDebug() << "Theme loaded:" << qssPath;
    } else {
        qWarning() << "Could not load theme:" << qssPath;
    }

    // Heap-allocated: must be destroyed before QgsApplication teardown
    qDebug() << "Creating window...";
    auto window = std::make_unique<QgisDesktopWindow>();
    window->setWindowIcon(app->windowIcon());
    qDebug() << "Initializing layer tree...";
    window->initLayerTree();

    qDebug() << "Showing window...";
    window->show();
    qDebug() << "Window shown";

    // Auto-load sample data if available
    QString samplePath = AppPaths::resolveDataPath("data/sample_crops.tif");
    if (QFileInfo::exists(samplePath)) {
        QPointer<QgisDesktopWindow> safeWindow(window.get());
        QTimer::singleShot(500, [safeWindow, samplePath]() {
            if (!safeWindow) return;
            // Load through the project Data Context so the sample raster is
            // registered as a Data Asset and displayed via a Display Layer.
            ( void ) safeWindow->loadDataLayer( samplePath );
        });
    }

    const int result = app->exec();

    // Python embedding disabled
    // QgisPython::instance().finalize();

    window.reset();
    delete logFile;
    delete app;
    return result;
}

#include "main.moc"
