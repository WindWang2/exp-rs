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
#include <QToolBar>
#include <QToolButton>
#include <QSplitter>
#include <QDockWidget>
#include <iostream>
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

    // Diagnostic feedback loop (diagnosing-bugs): geometry dump for under-ribbon
    // toolbars. Run: SICNU_DUMP_CHROME=1 QT_QPA_PLATFORM=offscreen ./build/sicnu_geo_rs
    // Exit 0 = strip+map toolbar visible with height>0 under band rail; 1 = red.
    if ( qEnvironmentVariableIsSet( "SICNU_DUMP_CHROME" ) )
    {
        QPointer<QgisDesktopWindow> safeWindow( window.get() );
        QTimer::singleShot( 200, [safeWindow, app]() {
            if ( !safeWindow )
            {
                std::cerr << "[DEBUG-tb] FAIL: window gone\n";
                app->exit( 1 );
                return;
            }
            auto dumpW = []( const char *tag, QWidget *w ) {
                if ( !w )
                {
                    std::cerr << "[DEBUG-tb] " << tag << " = null\n";
                    return;
                }
                const QRect g = w->geometry();
                const QPoint tl = w->mapToGlobal( QPoint( 0, 0 ) );
                std::cerr << "[DEBUG-tb] " << tag
                          << " name=" << w->objectName().toStdString()
                          << " class=" << w->metaObject()->className()
                          << " visible=" << w->isVisible()
                          << " hidden=" << w->isHidden()
                          << " geom=" << g.x() << "," << g.y()
                          << " " << g.width() << "x" << g.height()
                          << " globalY=" << tl.y()
                          << " minH=" << w->minimumHeight()
                          << " maxH=" << w->maximumHeight()
                          << " parent=" << ( w->parentWidget()
                                               ? w->parentWidget()->objectName().toStdString()
                                               : std::string( "null" ) )
                          << " winFlags=0x" << std::hex << int( w->windowFlags() ) << std::dec
                          << "\n";
            };

            QWidget *chrome = safeWindow->findChild<QWidget *>( QStringLiteral( "rsTopChrome" ) );
            QWidget *strip = safeWindow->findChild<QWidget *>( QStringLiteral( "rsToolbarStrip" ) );
            QDockWidget *ribbonDock = safeWindow->findChild<QDockWidget *>( QStringLiteral( "rsRibbonDock" ) );
            QToolBar *mapTb = safeWindow->findChild<QToolBar *>( QStringLiteral( "mapToolsToolBar" ) );
            QToolBar *digTb = safeWindow->findChild<QToolBar *>( QStringLiteral( "digitizeToolBar" ) );
            QWidget *band = safeWindow->findChild<QWidget *>( QStringLiteral( "rsBandRail" ) );
            if ( !band )
            {
                // BandCompositionRail may use different object name
                const auto all = safeWindow->findChildren<QWidget *>();
                for ( QWidget *w : all )
                {
                    if ( w && w->metaObject()->className()
                         && QString::fromLatin1( w->metaObject()->className() ).contains( QLatin1String( "BandComposition" ) ) )
                    {
                        band = w;
                        break;
                    }
                }
            }

            dumpW( "window", safeWindow.data() );
            dumpW( "ribbonDock", ribbonDock );
            dumpW( "chrome", chrome );
            dumpW( "band", band );
            dumpW( "strip", strip );
            dumpW( "mapTools", mapTb );
            dumpW( "digitize", digTb );

            if ( mapTb && mapTb->toggleViewAction() )
            {
                std::cerr << "[DEBUG-tb] mapTools.toggleChecked="
                          << mapTb->toggleViewAction()->isChecked()
                          << " actions=" << mapTb->actions().size()
                          << "\n";
            }

            int toolBtnVisible = 0;
            int toolBtnWithIcon = 0;
            if ( mapTb )
            {
                const auto buttons = mapTb->findChildren<QToolButton *>();
                std::cerr << "[DEBUG-tb] mapTools.toolButtons=" << buttons.size() << "\n";
                for ( QToolButton *btn : buttons )
                {
                    if ( !btn )
                        continue;
                    const QRect bg = btn->geometry();
                    const bool vis = btn->isVisible();
                    const bool hasIcon = !btn->icon().isNull();
                    if ( vis )
                        ++toolBtnVisible;
                    if ( hasIcon )
                        ++toolBtnWithIcon;
                    if ( toolBtnVisible + toolBtnWithIcon < 8 ) // sample first few
                    {
                        std::cerr << "[DEBUG-tb]   btn text=" << btn->text().toStdString()
                                  << " vis=" << vis
                                  << " icon=" << hasIcon
                                  << " geom=" << bg.width() << "x" << bg.height()
                                  << "+" << bg.x() << "+" << bg.y()
                                  << "\n";
                    }
                }
            }

            // List all QToolBars and their parents
            for ( QToolBar *tb : safeWindow->findChildren<QToolBar *>() )
            {
                dumpW( "toolbar", tb );
            }

            bool ok = true;
            if ( !strip || !strip->isVisible() || strip->height() < 28 )
            {
                std::cerr << "[DEBUG-tb] FAIL: rsToolbarStrip not visible or height<28"
                          << " (height=" << ( strip ? strip->height() : -1 ) << ")\n";
                ok = false;
            }
            if ( !mapTb || !mapTb->isVisible() || mapTb->height() < 28 )
            {
                std::cerr << "[DEBUG-tb] FAIL: mapToolsToolBar not visible or height<28\n";
                ok = false;
            }
            if ( toolBtnVisible < 3 )
            {
                std::cerr << "[DEBUG-tb] FAIL: mapTools has too few visible toolbuttons ("
                          << toolBtnVisible << ")\n";
                ok = false;
            }
            if ( toolBtnWithIcon < 3 )
            {
                std::cerr << "[DEBUG-tb] FAIL: mapTools has too few icons ("
                          << toolBtnWithIcon << ")\n";
                ok = false;
            }
            if ( mapTb && strip && !strip->isAncestorOf( mapTb ) )
            {
                std::cerr << "[DEBUG-tb] FAIL: mapToolsToolBar is not under rsToolbarStrip"
                          << " (parent=" << ( mapTb->parentWidget()
                                                ? mapTb->parentWidget()->objectName().toStdString()
                                                : "null" )
                          << ")\n";
                ok = false;
            }
            // Ribbon 154 + one toolbar row 32 => expect ~186
            if ( ribbonDock && ribbonDock->height() < 180 )
            {
                std::cerr << "[DEBUG-tb] FAIL: rsRibbonDock height too small for toolbar row"
                          << " (height=" << ribbonDock->height() << ", expect>=186)\n";
                ok = false;
            }
            // Band composition rail must stay out of product chrome.
            if ( band && band->isVisible() && band->height() > 2 )
            {
                std::cerr << "[DEBUG-tb] FAIL: band composition rail still visible\n";
                ok = false;
            }
            // Flow host: map tools should live under rsToolbarFlowHost when visible.
            if ( mapTb && mapTb->isVisible() )
            {
                QWidget *flow = safeWindow->findChild<QWidget *>( QStringLiteral( "rsToolbarFlowHost" ) );
                if ( !flow || !flow->isAncestorOf( mapTb ) )
                {
                    std::cerr << "[DEBUG-tb] FAIL: mapToolsToolBar not under rsToolbarFlowHost\n";
                    ok = false;
                }
            }
            // Digitize default-off; when forced on, strip must grow to two rows.
            if ( digTb )
            {
                digTb->setProperty( "rsWantVisible", true );
                if ( digTb->toggleViewAction() )
                    digTb->toggleViewAction()->setChecked( true );
                if ( mapTb )
                {
                    mapTb->setProperty( "rsWantVisible", true );
                    if ( mapTb->toggleViewAction() )
                        mapTb->toggleViewAction()->setChecked( true );
                }
                safeWindow->layoutToolbarsUnderRibbon();
                QCoreApplication::processEvents();
                strip = safeWindow->findChild<QWidget *>( QStringLiteral( "rsToolbarStrip" ) );
                dumpW( "stripAfterDigitize", strip );
                dumpW( "digitizeForced", digTb );
                // Adaptive flow: both bars may share one row when widths fit —
                // strip height is 32 (1 row) or 64 (2 rows). Either is valid.
                if ( strip && strip->height() < 28 )
                {
                    std::cerr << "[DEBUG-tb] FAIL: strip collapsed when digitize forced on"
                              << " (height=" << strip->height() << ")\n";
                    ok = false;
                }
                if ( !digTb->isVisible() )
                {
                    std::cerr << "[DEBUG-tb] FAIL: digitizeToolBar not visible when forced on\n";
                    ok = false;
                }
                if ( mapTb && !mapTb->isVisible() )
                {
                    std::cerr << "[DEBUG-tb] FAIL: mapTools hidden after digitize forced on\n";
                    ok = false;
                }
            }

            // Product shell: empty Task Center should not be open by default.
            QDockWidget *jobDock = safeWindow->findChild<QDockWidget *>( QStringLiteral( "rsJobPanelDock" ) );
            QDockWidget *legacyTc = safeWindow->findChild<QDockWidget *>( QStringLiteral( "TaskCenterDock" ) );
            dumpW( "jobPanel", jobDock );
            dumpW( "legacyTaskCenterDock", legacyTc );
            if ( jobDock && jobDock->isVisible() )
            {
                std::cerr << "[DEBUG-tb] FAIL: rsJobPanelDock should be hidden by default\n";
                ok = false;
            }
            if ( legacyTc && legacyTc->isVisible() )
            {
                std::cerr << "[DEBUG-tb] FAIL: legacy TaskCenterDock is visible\n";
                ok = false;
            }

            if ( ok )
                std::cerr << "[DEBUG-tb] PASS: under-ribbon toolbar + task chrome defaults OK\n";
            else
                std::cerr << "[DEBUG-tb] RED: under-ribbon toolbar / task chrome failed\n";
            app->exit( ok ? 0 : 1 );
        } );
    }
    else
    {
        // Auto-load sample data if available
        QString samplePath = AppPaths::resolveDataPath( "data/sample_crops.tif" );
        if ( QFileInfo::exists( samplePath ) )
        {
            QPointer<QgisDesktopWindow> safeWindow( window.get() );
            QTimer::singleShot( 500, [safeWindow, samplePath]() {
                if ( !safeWindow )
                    return;
                ( void ) safeWindow->loadDataLayer( samplePath );
            } );
        }
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
