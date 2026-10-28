// tests/test_r25_d_result_open_link.cpp — R25-D Loop 6 (Oracle 2b)
//
// Result→layer linkage honesty: task outputs include VECTOR artifacts
// (rs:detect decodes to GPKG/GeoJSON/SHP, polygonize writes SHP/GPKG), but
// every manual "open result on the map" surface (job panel 结果 tab, task
// panel, workspace governance rows) bound its signal to loadRasterLayer —
// a forced-gdal open. Vector artifacts failed to open from exactly the
// surfaces meant to open them, while the AUTOMATIC post-task load path
// (TaskCenter layerAutoLoadRequested → loadDataLayer/openPath) already
// infers the kind. One seam, both kinds.
//
// Source-scan pin: the three production connections must route through
// loadDataLayer (kind-inferring openPath), never loadRasterLayer.
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QTextStream>

namespace
{

QString readSource( const QString &relativePath )
{
    QFile file( QStringLiteral( CMAKE_SOURCE_DIR ) + QLatin1Char( '/' ) + relativePath );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
        return QString();
    return QTextStream( &file ).readAll();
}

} // namespace

TEST_CASE( "result-open surfaces route through the kind-inferring seam", "[r25d][result_open][source_scan]" )
{
    const QString docks = readSource( QStringLiteral( "src/app/main_window_docks.cpp" ) );
    REQUIRE_FALSE( docks.isEmpty() );

    SECTION( "job panel result tab opens any artifact kind" )
    {
        const QRegularExpression re(
          QStringLiteral( "connect\\( m_jobPanel, &RsJobPanel::resultOpenRequested,\\s*"
                          "this, &QgisDesktopWindow::(\\w+) \\);" ) );
        const auto match = re.match( docks );
        REQUIRE( match.hasMatch() );
        CHECK( match.captured( 1 ) == QStringLiteral( "loadDataLayer" ) );
    }

    SECTION( "task panel result summary opens any artifact kind" )
    {
        const QRegularExpression re(
          QStringLiteral( "connect\\( m_taskPanel, &TaskPanelHost::resultOpenRequested,\\s*"
                          "this, &QgisDesktopWindow::(\\w+) \\);" ) );
        const auto match = re.match( docks );
        REQUIRE( match.hasMatch() );
        CHECK( match.captured( 1 ) == QStringLiteral( "loadDataLayer" ) );
    }

    SECTION( "governance rows open any artifact kind" )
    {
        const QRegularExpression re(
          QStringLiteral( R"(connect\( m_workspaceBrowserPanel, &sicnu::app::WorkspaceBrowserPanel::openPathRequested,\s*this, \[this\]\( const QString &path \) \{ (\w+)\( path \); \} \);)" ) );
        const auto match = re.match( docks );
        REQUIRE( match.hasMatch() );
        CHECK( match.captured( 1 ) == QStringLiteral( "loadDataLayer" ) );
    }

    SECTION( "processing history keeps its guarded open (already honest)" )
    {
        const QString workbench =
          readSource( QStringLiteral( "src/app/main_window_workbench.cpp" ) );
        REQUIRE_FALSE( workbench.isEmpty() );
        const QRegularExpression re(
          QStringLiteral( "connect\\( m_historyPanel, &sicnu::app::ProcessingHistoryPanel::resultOpenRequested,\\s*"
                          "this, \\[this\\]\\( const QString &path \\) \\{\\s*"
                          "if \\( !(\\w+)\\( path \\) \\)" ) );
        const auto match = re.match( workbench );
        REQUIRE( match.hasMatch() );
        CHECK( match.captured( 1 ) == QStringLiteral( "loadDataLayer" ) );
    }
}
