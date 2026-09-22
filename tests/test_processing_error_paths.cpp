// tests/test_processing_error_paths.cpp — provider error-path regression lane.
//
// Contract under test (the #1043 family): a provider algorithm must FAIL
// TRUTHFULLY — throw QgsProcessingException carrying the tool's real failure
// — instead of reporting success with an empty result map, a partial output
// file, or a signal-killed tool's exitCode()==0. Every scenario drives the
// real production code with a deterministic fake tool via ToolPathManager;
// nothing here depends on GDAL/OTB binaries being installed.
#include <catch2/catch_test_macros.hpp>

#include "processing/providers/gdal_tools/algorithms/gdaltransform.h"
#include "processing/providers/otb_tools/algorithms/otb_pixel_info.h"
#include "processing/providers/otb_tools/algorithms/otb_read_image_info.h"
#include "processing/algorithms/gcp_manager.h"
#include "processing/tools/tool_path_manager.h"

#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>
#include <qgsexception.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantMap>

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
/// Exposes the protected processAlgorithm for direct invocation.
class ExposedGdalTransform : public GdalTransformAlgorithm
{
  public:
    using GdalTransformAlgorithm::processAlgorithm;
};
class ExposedOtbPixelInfo : public OtbPixelInfoAlgorithm
{
  public:
    using OtbPixelInfoAlgorithm::processAlgorithm;
};
class ExposedOtbReadImageInfo : public OtbReadImageInfoAlgorithm
{
  public:
    using OtbReadImageInfoAlgorithm::processAlgorithm;
};

/// Creates an executable fake tool inside a fresh temp directory and points
/// the ToolPathManager custom path at it; resets the manager on destruction.
class FakeToolDir
{
  public:
    explicit FakeToolDir( const QString &toolName, const QString &body )
    {
      REQUIRE( m_dir.isValid() );
      const QString path = m_dir.filePath( toolName );
      QFile script( path );
      REQUIRE( script.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
      script.write( QStringLiteral( "#!/usr/bin/env bash\n%1\n" ).arg( body ).toUtf8() );
      script.close();
      REQUIRE( script.setPermissions( QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther
                                      | QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther ) );
      ToolPathManager::instance().setGdalPath( m_dir.path() );
      ToolPathManager::instance().setOtbPath( m_dir.path() );
    }
    ~FakeToolDir()
    {
      ToolPathManager::instance().setGdalPath( QString() );
      ToolPathManager::instance().setOtbPath( QString() );
    }
    FakeToolDir( const FakeToolDir & ) = delete;
    FakeToolDir &operator=( const FakeToolDir & ) = delete;

    QString path() const { return m_dir.path(); }

  private:
    QTemporaryDir m_dir;
};

QVariantMap transformParams( const QString &outputPath )
{
    QVariantMap params;
    params[QStringLiteral( "SOURCE_CRS" )] = QStringLiteral( "EPSG:4326" );
    params[QStringLiteral( "TARGET_CRS" )] = QStringLiteral( "EPSG:3857" );
    params[QStringLiteral( "X" )] = 1.0;
    params[QStringLiteral( "Y" )] = 2.0;
    params[QStringLiteral( "OUTPUT" )] = outputPath;
    return params;
}
} // namespace

TEST_CASE( "gdaltransform: a signal-killed tool fails the run instead of "
           "publishing partial stdout",
           "[provider][error][gdaltransform]" )
{
    // A killed tool reports exitCode()==0 with CrashExit; the historical bug
    // treated that as success and wrote whatever partial stdout had arrived
    // to the OUTPUT file.
    FakeToolDir tools( QStringLiteral( "gdaltransform" ),
                       QStringLiteral( "echo \"1.0 2.0\"\nkill -9 $$" ) );
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString output = dir.filePath( QStringLiteral( "out.txt" ) );

    ExposedGdalTransform alg;
    alg.initAlgorithm();
    QgsProcessingContext context;
    QgsProcessingFeedback feedback;

    bool threw = false;
    QString message;
    try
    {
        ( void )alg.processAlgorithm( transformParams( output ), context, &feedback );
    }
    catch ( const QgsProcessingException &e )
    {
        threw = true;
        message = e.what();
    }
    INFO( "thrown message: " << message.toStdString() );
    REQUIRE( threw );
    CHECK( message.contains( QLatin1String( "crashed" ), Qt::CaseInsensitive ) );
    // No apparently-valid output may survive the failed run.
    CHECK( !QFileInfo::exists( output ) );
}

TEST_CASE( "gdaltransform: a canceled run throws instead of returning an "
           "empty (successful-looking) result",
           "[provider][error][gdaltransform]" )
{
    FakeToolDir tools( QStringLiteral( "gdaltransform" ), QStringLiteral( "sleep 30" ) );
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString output = dir.filePath( QStringLiteral( "out.txt" ) );

    ExposedGdalTransform alg;
    alg.initAlgorithm();
    QgsProcessingContext context;
    QgsProcessingFeedback feedback;

    std::atomic<bool> cancelFired{ false };
    std::thread canceller( [&feedback, &cancelFired] {
        std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
        feedback.cancel();
        cancelFired.store( true );
    } );

    bool threw = false;
    QString message;
    try
    {
        ( void )alg.processAlgorithm( transformParams( output ), context, &feedback );
    }
    catch ( const QgsProcessingException &e )
    {
        threw = true;
        message = e.what();
    }
    canceller.join();
    REQUIRE( cancelFired.load() );
    REQUIRE( threw );
    CHECK( message.contains( QLatin1String( "cancel" ), Qt::CaseInsensitive ) );
    CHECK( !QFileInfo::exists( output ) );
}

TEST_CASE( "gdaltransform: a healthy tool still produces its output file",
           "[provider][error][gdaltransform]" )
{
    // Anti-over-correction: the failure gates above must not break the
    // healthy path.
    FakeToolDir tools( QStringLiteral( "gdaltransform" ), QStringLiteral( "echo \"3.0 4.0\"" ) );
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString output = dir.filePath( QStringLiteral( "out.txt" ) );

    ExposedGdalTransform alg;
    alg.initAlgorithm();
    QgsProcessingContext context;
    QgsProcessingFeedback feedback;

    bool threw = false;
    QVariantMap results;
    try
    {
        results = alg.processAlgorithm( transformParams( output ), context, &feedback );
    }
    catch ( const QgsProcessingException &e )
    {
        threw = true;
        FAIL( e.what().toStdString() );
    }
    CHECK( !threw );
    CHECK( results.value( QStringLiteral( "OUTPUT" ) ).toString() == output );
    CHECK( QFile::exists( output ) );
}

TEST_CASE( "otb pixel_info / read_image_info: a failing tool throws instead "
           "of completing the task as an empty success",
           "[provider][error][otb]" )
{
    // The historical fail-open bug: every failure path returned {} — an
    // empty processAlgorithm result reads as a successful run with no
    // output. Each tool that exits non-zero (or is killed, or is canceled)
    // must now throw.
    for ( const QString &tool : { QStringLiteral( "otbcli_PixelValue" ),
                                  QStringLiteral( "otbcli_ReadImageInfo" ) } )
    {
        FakeToolDir tools( tool, QStringLiteral( "echo \"boom\" >&2\nexit 1" ) );
        QTemporaryDir dir;
        REQUIRE( dir.isValid() );

        QgsProcessingContext context;
        QgsProcessingFeedback feedback;
        QVariantMap params;
        params[QStringLiteral( "INPUT" )] = dir.filePath( QStringLiteral( "in.tif" ) );
        params[QStringLiteral( "X" )] = 0;
        params[QStringLiteral( "Y" )] = 0;

        bool threw = false;
        QString message;
        try
        {
            if ( tool == QLatin1String( "otbcli_PixelValue" ) )
            {
                ExposedOtbPixelInfo alg;
                alg.initAlgorithm();
                ( void )alg.processAlgorithm( params, context, &feedback );
            }
            else
            {
                ExposedOtbReadImageInfo alg;
                alg.initAlgorithm();
                QVariantMap noXY = params;
                noXY.remove( QStringLiteral( "X" ) );
                noXY.remove( QStringLiteral( "Y" ) );
                ( void )alg.processAlgorithm( noXY, context, &feedback );
            }
        }
        catch ( const QgsProcessingException &e )
        {
            threw = true;
            message = e.what();
        }
        INFO( tool.toStdString() );
        REQUIRE( threw );
        CHECK( message.contains( QLatin1String( "exit code 1" ) ) );
        CHECK( message.contains( QLatin1String( "boom" ) ) );
    }
}

TEST_CASE( "otb pixel_info / read_image_info: a killed tool throws a crash "
           "error, and a healthy tool still reports success",
           "[provider][error][otb]" )
{
    // Killed: exitCode()==0 + CrashExit must not read as success.
    {
        FakeToolDir tools( QStringLiteral( "otbcli_PixelValue" ),
                           QStringLiteral( "echo \"bootstrap\"\nkill -9 $$" ) );
        QgsProcessingContext context;
        QgsProcessingFeedback feedback;
        QVariantMap params;
        params[QStringLiteral( "INPUT" )] = QStringLiteral( "/nonexistent/in.tif" );
        params[QStringLiteral( "X" )] = 0;
        params[QStringLiteral( "Y" )] = 0;

        ExposedOtbPixelInfo alg;
        alg.initAlgorithm();
        bool threw = false;
        QString message;
        try
        {
            ( void )alg.processAlgorithm( params, context, &feedback );
        }
        catch ( const QgsProcessingException &e )
        {
            threw = true;
            message = e.what();
        }
        REQUIRE( threw );
        CHECK( message.contains( QLatin1String( "crashed" ), Qt::CaseInsensitive ) );
    }

    // Healthy: exit 0 still produces the success payload (anti-over-correction).
    {
        FakeToolDir tools( QStringLiteral( "otbcli_ReadImageInfo" ),
                           QStringLiteral( "echo '{\"size\": [16, 16]}'" ) );
        QgsProcessingContext context;
        QgsProcessingFeedback feedback;
        QVariantMap params;
        params[QStringLiteral( "INPUT" )] = QStringLiteral( "/nonexistent/in.tif" );

        ExposedOtbReadImageInfo alg;
        alg.initAlgorithm();
        QVariantMap results;
        try
        {
            results = alg.processAlgorithm( params, context, &feedback );
        }
        catch ( const QgsProcessingException &e )
        {
            FAIL( e.what().toStdString() );
        }
        CHECK( results.value( QStringLiteral( "OUTPUT" ) ).toString()
               .contains( QLatin1String( "successfully" ) ) );
    }
}

TEST_CASE( "GCP CSV export fails on a full device instead of reporting success",
           "[provider][error][shortwrite]" )
{
#ifdef Q_OS_UNIX
    // /dev/full fails every write with ENOSPC: with zero points the old
    // implementation (no flush/status check) returned true for this call.
    rs::core::GcpManager manager;
    CHECK( !manager.saveToCsv( QStringLiteral( "/dev/full" ) ) );
#else
    SUCCEED( "POSIX-only oracle (/dev/full)" );
#endif
}
