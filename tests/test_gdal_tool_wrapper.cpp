// tests/test_gdal_tool_wrapper.cpp — Test GDAL tool wrapper error handling
#include <catch2/catch_test_macros.hpp>

#include "processing/providers/generic_cli/generic_cli_algorithm.h"
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>
#include "qgsexception.h"

#include "processing/providers/gdal_tools/gdal_tool_wrapper.h"
#include "processing/providers/otb_tools/otb_tool_wrapper.h"
#include <processing/qgsprocessingparameters.h>
#include <qgsapplication.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonArray>

// Test that MergedChannels mode captures stderr in readAllStandardOutput
TEST_CASE("QProcess MergedChannels captures stderr", "[gdal][tool][error]") {
    // Run a command that writes to stderr and fails
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
#ifdef _WIN32
    proc.start("cmd.exe", {"/c", "echo error message 1>&2 & exit 1"});
#else
    proc.start("bash", {"-c", "echo 'error message' >&2; exit 1"});
#endif

    REQUIRE(proc.waitForStarted(5000));
    proc.waitForFinished(5000);

    // With MergedChannels, stderr should be in readAllStandardOutput
    QByteArray stdoutOutput = proc.readAllStandardOutput();
    QByteArray stderrOutput = proc.readAllStandardError();

    // The error message should be in stdout, not stderr
    CHECK(stdoutOutput.contains("error message"));
    CHECK(stderrOutput.isEmpty()); // stderr is empty because channels are merged
}

// Test that SeparateChannels mode keeps stderr separate
TEST_CASE("QProcess SeparateChannels keeps stderr separate", "[gdal][tool][error]") {
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef _WIN32
    proc.start("cmd.exe", {"/c", "echo error message 1>&2 & exit 1"});
#else
    proc.start("bash", {"-c", "echo 'error message' >&2; exit 1"});
#endif

    REQUIRE(proc.waitForStarted(5000));
    proc.waitForFinished(5000);

    // With SeparateChannels, stderr should be in readAllStandardError
    QByteArray stdoutOutput = proc.readAllStandardOutput();
    QByteArray stderrOutput = proc.readAllStandardError();

    // The error message should be in stderr
    CHECK(stdoutOutput.isEmpty());
    CHECK(stderrOutput.contains("error message"));
}

TEST_CASE("GenericCliAlgorithm initializes parameters and preview", "[gdal][tool][error]") {
    QJsonObject config;
    config["id"] = "test_custom_gdal";
    config["name"] = "Custom GDAL Tool";
    config["command"] = "gdalinfo";
    QJsonArray params;
    QJsonObject p1;
    p1["name"] = "INPUT";
    p1["type"] = "raster";
    params.append(p1);
    config["parameters"] = params;
    config["args"] = QJsonArray{"-stats", "{INPUT}"};

    GenericCliAlgorithm alg(config, "gdal");
    REQUIRE(alg.name() == "test_custom_gdal");
    REQUIRE(alg.displayName() == "Custom GDAL Tool");

    QgsProcessingContext context;
    QVariantMap inputParams;
    inputParams["INPUT"] = "/tmp/fake_input.tif";
    QString preview = alg.commandLinePreview(inputParams, context);
    CHECK(preview.contains("gdalinfo"));
    CHECK(preview.contains("-stats"));
    CHECK(preview.contains("/tmp/fake_input.tif"));
}

TEST_CASE("GenericCliAlgorithm catches exit failures with QgsProcessingException", "[gdal][tool][error]") {
    QJsonObject config;
    config["id"] = "test_failing_cli";
    config["name"] = "Failing Tool";
    config["command"] = "bash";
    config["args"] = QJsonArray{"-c", "echo 'critical gdal failure' >&2; exit 3"};

    GenericCliAlgorithm alg(config, "gdal");
    QgsProcessingContext context;
    QgsProcessingFeedback feedback;
    QVariantMap params;
    bool ok = true;
    bool threw = false;
    QString caughtMessage;
    try {
        alg.run(params, context, &feedback, &ok, {}, false);
    } catch (const QgsProcessingException &e) {
        threw = true;
        caughtMessage = e.what();
    } catch (const std::exception &e) {
        threw = true;
        caughtMessage = QString::fromUtf8(e.what());
    }

    CHECK(threw);
    CHECK(caughtMessage.contains("3"));
}

namespace {
void ensureCliTestApplication()
{
    if ( !QCoreApplication::instance() ) {
        static int argc = 1;
        static char name[] = "cli-output-contracts";
        static char *argv[] = { name, nullptr };
        static auto *application = new QgsApplication( argc, argv, false );
        Q_UNUSED( application );
    }
}
class DiagnosticProbe : public GdalToolWrapper {
public:
    QString name() const override { return QStringLiteral( "diagnostic_probe" ); }
    QString displayName() const override { return name(); }
    QString toolName() const override { return QCoreApplication::applicationFilePath(); }
    void initAlgorithm( const QVariantMap & ) override {}
    QgsProcessingAlgorithm *createInstance() const override { return new DiagnosticProbe; }
    QStringList buildArgs( const QVariantMap &, QgsProcessingContext &, QgsProcessingFeedback * ) override {
        return { QStringLiteral( "--sicnu-intentionally-invalid-option" ) };
    }
};
class OutputProbe : public OtbToolWrapper {
public:
    QString name() const override { return QStringLiteral( "output_probe" ); }
    QString displayName() const override { return name(); }
    QString applicationName() const override {
#ifdef Q_OS_WIN
        return QStringLiteral( "SicnuOutputProbe.exe" );
#else
        return QStringLiteral( "SicnuOutputProbe" );
#endif
    }
    void initAlgorithm( const QVariantMap & ) override {
        addParameter( new QgsProcessingParameterFileDestination( QStringLiteral( "OUTPUT" ), QStringLiteral( "Output" ) ) );
    }
    QgsProcessingAlgorithm *createInstance() const override { return new OutputProbe; }
    QStringList buildArgs( const QVariantMap &, QgsProcessingContext &, QgsProcessingFeedback * ) override {
        return { QStringLiteral( "--help" ) };
    }
};
class GenericOutputProbe : public GenericCliAlgorithm {
public:
    using GenericCliAlgorithm::GenericCliAlgorithm;
    using GenericCliAlgorithm::initAlgorithm;
    using GenericCliAlgorithm::processAlgorithm;
};
struct CliEnvironment {
    QByteArray key, previous;
    bool existed;
    CliEnvironment( const QByteArray &name, const QByteArray &value )
        : key( name ), previous( qgetenv( name.constData() ) ), existed( qEnvironmentVariableIsSet( name.constData() ) ) {
        qputenv( key.constData(), value );
    }
    ~CliEnvironment() {
        if ( existed ) qputenv( key.constData(), previous );
        else qunsetenv( key.constData() );
    }
};
}

TEST_CASE( "GDAL wrapper preserves the child exit code and streamed diagnostic", "[gdal][wrapper-diagnostic]" )
{
    ensureCliTestApplication();
    DiagnosticProbe algorithm;
    QgsProcessingContext context;
    QgsProcessingFeedback feedback;
    const auto args = algorithm.buildArgs( {}, context, &feedback );
    QProcess control;
    control.start( QCoreApplication::applicationFilePath(), args );
    REQUIRE( control.waitForStarted() );
    REQUIRE( control.waitForFinished() );
    REQUIRE( control.exitStatus() == QProcess::NormalExit );
    REQUIRE( control.exitCode() != 0 );
    QString message;
    try { algorithm.processAlgorithm( {}, context, &feedback ); }
    catch ( const QgsProcessingException &error ) { message = error.what(); }
    CHECK( message.contains( QString::number( control.exitCode() ) ) );
    CHECK( message.contains( QStringLiteral( "--sicnu-intentionally-invalid-option" ) ) );
}

TEST_CASE( "CLI wrappers reject a sidecar when the declared output is missing", "[gdal][output-sidecar]" )
{
    ensureCliTestApplication();
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    OutputProbe otb;
    otb.initAlgorithm( {} );
    // The same test executable acts as a successful CLI by displaying help.
    // It never executes tests or generates the declared output.
    const QString child = temporary.filePath( QStringLiteral( "otbcli_" ) + otb.applicationName() );
    REQUIRE( QFile::copy( QCoreApplication::applicationFilePath(), child ) );
    REQUIRE( QFile::setPermissions( child, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther ) );
    CliEnvironment otbPath( "SICNU_OTB_PATH", temporary.path().toUtf8() );
    CliEnvironment executablePath( "PATH", QFileInfo( QCoreApplication::applicationFilePath() ).absolutePath().toUtf8()
        + QDir::listSeparator().toLatin1() + qgetenv( "PATH" ) );
    QProcess control;
    control.start( child, { QStringLiteral( "--help" ) } );
    REQUIRE( control.waitForStarted() );
    REQUIRE( control.waitForFinished() );
    REQUIRE( control.exitStatus() == QProcess::NormalExit );
    REQUIRE( control.exitCode() == 0 );
    const QString output = temporary.filePath( QStringLiteral( "result.shp" ) );
    QFile sidecar( output + QStringLiteral( ".xml" ) );
    REQUIRE( sidecar.open( QIODevice::WriteOnly ) );
    sidecar.close();
    QJsonObject config;
    config["id"] = "generic_output_probe";
    config["name"] = "Output probe";
    config["command"] = QCoreApplication::applicationFilePath();
    config["args"] = QJsonArray{ "--help" };
    config["parameters"] = QJsonArray{ QJsonObject{ { "name", "OUTPUT" }, { "type", "output_vector" } } };
    GenericOutputProbe generic( config, QStringLiteral( "test" ) );
    generic.initAlgorithm( {} );
    QgsProcessingContext context;
    QgsProcessingFeedback feedback;
    const QVariantMap params{ { QStringLiteral( "OUTPUT" ), output } };
    REQUIRE_FALSE( QFileInfo::exists( output ) );
    QString genericError, otbError;
    try { generic.processAlgorithm( params, context, &feedback ); }
    catch ( const QgsProcessingException &error ) { genericError = error.what(); }
    try { otb.processAlgorithm( params, context, &feedback ); }
    catch ( const QgsProcessingException &error ) { otbError = error.what(); }
    CHECK( genericError.contains( output ) );
    CHECK( otbError.contains( output ) );
    QFile mainOutput( output );
    REQUIRE( mainOutput.open( QIODevice::WriteOnly ) );
    mainOutput.close();
    CHECK( generic.processAlgorithm( params, context, &feedback ).value( QStringLiteral( "OUTPUT" ) ).toString() == output );
    CHECK( otb.processAlgorithm( params, context, &feedback ).value( QStringLiteral( "OUTPUT" ) ).toString() == output );
}
