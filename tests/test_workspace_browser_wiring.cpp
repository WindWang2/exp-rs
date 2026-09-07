// test_workspace_browser_wiring.cpp — Desktop Workbench UX 4.0 (Milestone A/D)
//
// Pins the shell contract for the 工作区治理 panel: the dock body must be
// backed by the ProjectContext WorkspaceService (before 4.0 the panel was
// created but never received a service, leaving an inert dock), and the panel
// must follow store open/reopen via refresh() (the shell calls it after
// new/open/save-as, where the backing index is replaced).
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gdal.h>

#include "data/data_manager.h"
#include "data/data_asset.h"
#include "data/governance/workspace_service.h"
#include "panels/workspace_browser_panel.h"

using sicnu::data::DataManager;
using sicnu::data::RegisterRequest;
using sicnu::data::RegisterResult;
using sicnu::data::SourceDescriptor;
using sicnu::workspace::WorkspaceService;

namespace {

QApplication &testApp()
{
    static int argc = 0;
    static QApplication app( argc, nullptr );
    return app;
}

QString makeRaster( const QString &name )
{
    static QTemporaryDir dir;
    const QString path = QDir( dir.path() ).filePath( name );
    if ( QFileInfo::exists( path ) )
        return path;
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    constexpr int W = 8, H = 8;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    GDALClose( ds );
    return path;
}

struct Fixture
{
    DataManager manager;
    WorkspaceService service;
    QString storePath;

    Fixture()
    {
        testApp();
        service.bindDataManager( &manager );
        static int instance = 0;
        const QString storeDir = QDir::temp().filePath(
            QStringLiteral( "ux4-browser-wiring-%1-%2" ).arg( QCoreApplication::applicationPid() ).arg( ++instance ) );
        REQUIRE( QDir().mkpath( storeDir ) );
        storePath = QDir( storeDir ).filePath( QStringLiteral( "gov.db" ) );
        REQUIRE( service.openStore( storePath ) );
    }
    ~Fixture() { service.closeStore(); }
};

RegisterResult registerFile( DataManager &manager, const QString &path )
{
    SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = path;
    return manager.registerSource( RegisterRequest{ source } );
}

int assetRowCount( sicnu::app::WorkspaceBrowserPanel &panel )
{
    // The default entity set is "assets"; rows come from the paged model.
    return panel.findChild<sicnu::app::WorkspaceGovernanceModel *>()
        ? panel.findChild<sicnu::app::WorkspaceGovernanceModel *>()->rowCount()
        : -1;
}

} // namespace

TEST_CASE( "WorkspaceBrowserPanel populates from the injected WorkspaceService", "[ux4][workspace][shell]" )
{
    Fixture fx;
    const QString raster = makeRaster( QStringLiteral( "wiring-a.tif" ) );
    const RegisterResult registered = registerFile( fx.manager, raster );
    REQUIRE( !registered.assetId.isNull() );
    REQUIRE( fx.service.mirrorAllAssets() >= 1 );

    sicnu::app::WorkspaceBrowserPanel panel;
    panel.setWorkspaceService( &fx.service );

    const sicnu::app::WorkspaceGovernanceModel *model =
        panel.findChild<sicnu::app::WorkspaceGovernanceModel *>();
    REQUIRE( model != nullptr );
    REQUIRE( model->rowCount() >= 1 );
    REQUIRE( !model->entityId( 0 ).isEmpty() );
}

TEST_CASE( "WorkspaceBrowserPanel refresh() follows a store reopen", "[ux4][workspace][shell]" )
{
    Fixture fx;
    const QString raster = makeRaster( QStringLiteral( "wiring-b.tif" ) );
    REQUIRE( !registerFile( fx.manager, raster ).assetId.isNull() );
    REQUIRE( fx.service.mirrorAllAssets() >= 1 );

    sicnu::app::WorkspaceBrowserPanel panel;
    panel.setWorkspaceService( &fx.service );
    sicnu::app::WorkspaceGovernanceModel *model =
        panel.findChild<sicnu::app::WorkspaceGovernanceModel *>();
    REQUIRE( model != nullptr );
    REQUIRE( model->rowCount() >= 1 );

    // Shell "Save As" / "Open" replaces the backing index; a fresh store has
    // no mirrored rows until the project read repopulates it.
    QTemporaryDir freshDir;
    REQUIRE( fx.service.openStore( QDir( freshDir.path() ).filePath( QStringLiteral( "fresh.db" ) ) ) );
    panel.refresh();
    INFO( "fresh store must show an empty asset list" );
    REQUIRE( model->rowCount() == 0 );

    // Reopening the original store restores its rows (project switch back).
    REQUIRE( fx.service.openStore( fx.storePath ) );
    panel.refresh();
    REQUIRE( model->rowCount() >= 1 );
}

TEST_CASE( "Shell wiring stays in place (source-scan guard)", "[ux4][shell][guardrail]" )
{
    // The 4.0 regression this guards against: a dock created without its
    // backing service — inert UI that renders an empty governance view.
    const QString docks = QStringLiteral( CMAKE_SOURCE_DIR "/src/app/main_window_docks.cpp" );
    QFile docksFile( docks );
    REQUIRE( docksFile.open( QIODevice::ReadOnly ) );
    const QString docksSource = QString::fromUtf8( docksFile.readAll() );
    INFO( docks.toStdString() );
    REQUIRE( docksSource.contains( QStringLiteral( "setWorkspaceService" ) ) );
    REQUIRE( docksSource.contains( QStringLiteral( "workspaceService()" ) ) );

    // Project lifecycle re-queries the governance panel when the store moves.
    const QString project = QStringLiteral( CMAKE_SOURCE_DIR "/src/app/main_window_project.cpp" );
    QFile projectFile( project );
    REQUIRE( projectFile.open( QIODevice::ReadOnly ) );
    const QString projectSource = QString::fromUtf8( projectFile.readAll() );
    REQUIRE( projectSource.contains( QStringLiteral( "refreshWorkspaceBrowser()" ) ) );
    REQUIRE( projectSource.contains( QStringLiteral( "updateWindowTitle()" ) ) );
}
