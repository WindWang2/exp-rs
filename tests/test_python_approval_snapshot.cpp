#include <catch2/catch_test_macros.hpp>

#include "agent/runtime/python_approval_snapshot.h"
#include "operators/runtime/python_environment.h"
#include "operators/runtime/python_script_operator.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

using namespace sicnu::agent::runtime;

namespace
{
void application()
{
  if ( QCoreApplication::instance() )
    return;
  static int argc = 1;
  static char name[] = "test_python_approval_snapshot";
  static char *argv[] = { name, nullptr };
  new QCoreApplication( argc, argv );
}

class ScopedEnvironment
{
  public:
    ScopedEnvironment( const char *name, const QByteArray &value )
      : m_name( name ), m_previous( qgetenv( name ) ), m_wasSet( qEnvironmentVariableIsSet( name ) )
    {
      qputenv( name, value );
    }
    ~ScopedEnvironment()
    {
      if ( m_wasSet )
        qputenv( m_name.constData(), m_previous );
      else
        qunsetenv( m_name.constData() );
    }

  private:
    QByteArray m_name;
    QByteArray m_previous;
    bool m_wasSet;
};

void writeFile( const QString &path, const QByteArray &bytes )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
  REQUIRE( file.write( bytes ) == bytes.size() );
}

QJsonObject fileArguments( const QString &path )
{
  return { { QStringLiteral( "script_path" ), path } };
}

QString sha256( const QByteArray &bytes )
{
  return QString::fromLatin1( QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() );
}

QString configuredEnvironmentRoot()
{
  const QString root = qEnvironmentVariable( "SICNU_TEST_PYTHON_ENV_ROOT" );
  if ( root.isEmpty() )
    SKIP( "Real approval identity checks require SICNU_TEST_PYTHON_ENV_ROOT; no implicit installation." );
  return QDir( root ).absolutePath();
}
} // namespace

TEST_CASE( "Python approval rejects invalid script sources before probing", "[agent][python][approval]" )
{
  application();
  QTemporaryDir workspace;
  REQUIRE( workspace.isValid() );
  const ScopedEnvironment policy( "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() );
  const QString tool = QStringLiteral( "python:run_script" );

  SECTION( "Source is required and exclusive" )
  {
    REQUIRE( capturePythonApprovalSnapshot( tool, {} ).error == QLatin1String( "python.approval_script_invalid" ) );
    REQUIRE( capturePythonApprovalSnapshot( tool,
      { { QStringLiteral( "script" ), QStringLiteral( "pass" ) },
        { QStringLiteral( "script_path" ), workspace.filePath( QStringLiteral( "script.py" ) ) } } ).error
      == QLatin1String( "python.approval_script_invalid" ) );
  }
  SECTION( "Empty and oversized source fail" )
  {
    for ( const QString &source : { QString(), QString( 128 * 1024 + 1, QLatin1Char( 'x' ) ) } )
      REQUIRE( capturePythonApprovalSnapshot( tool, { { QStringLiteral( "script" ), source } } ).error
               == QLatin1String( "python.approval_script_size_invalid" ) );
  }
  SECTION( "File requires an absolute Python path" )
  {
    REQUIRE( capturePythonApprovalSnapshot( tool, fileArguments( QStringLiteral( "script.py" ) ) ).error
             == QLatin1String( "python.approval_script_path_invalid" ) );
    REQUIRE( capturePythonApprovalSnapshot( tool, fileArguments( workspace.filePath( QStringLiteral( "script.txt" ) ) ) ).error
             == QLatin1String( "python.approval_script_path_invalid" ) );
  }
  SECTION( "Invalid and incomplete UTF-8 files fail" )
  {
    const QString path = workspace.filePath( QStringLiteral( "script.py" ) );
    for ( const QByteArray &source : { QByteArray::fromHex( "ff" ), QByteArray::fromHex( "706173730ae282" ) } )
    {
      writeFile( path, source );
      REQUIRE( capturePythonApprovalSnapshot( tool, fileArguments( path ) ).error
               == QLatin1String( "python.approval_script_utf8_invalid" ) );
    }
  }
}

TEST_CASE( "Python approval gates script reads by the workspace", "[agent][python][approval]" )
{
  application();
  QTemporaryDir workspace;
  QTemporaryDir outside;
  REQUIRE( workspace.isValid() );
  REQUIRE( outside.isValid() );
  const ScopedEnvironment policy( "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() );
  const QString path = outside.filePath( QStringLiteral( "private.py" ) );
  // An unreadable or malformed outside file must still hit the path gate first.
  writeFile( path, QByteArray::fromHex( "ff" ) );
  REQUIRE( capturePythonApprovalSnapshot( QStringLiteral( "python:run_script" ), fileArguments( path ) ).error
           == QLatin1String( "python.approval_script_path_refused" ) );
}

TEST_CASE( "Python approval cancellation and profile errors are fixed codes", "[agent][python][approval]" )
{
  application();
  const QString secret = QStringLiteral( "private-profile-do-not-echo" );
  const auto invalid = capturePythonApprovalSnapshot( QStringLiteral( "python:environment" ),
    { { QStringLiteral( "profile_id" ), secret } } );
  REQUIRE( invalid.error == QLatin1String( "python.approval_profile_invalid" ) );
  REQUIRE_FALSE( invalid.identity.contains( QStringLiteral( "profile_id" ) ) );
  REQUIRE_FALSE( invalid.error.contains( secret ) );
  REQUIRE( capturePythonApprovalSnapshot( QStringLiteral( "python:environment" ), {}, [] { return true; } ).error
           == QLatin1String( "python.approval_cancelled" ) );
  REQUIRE( recheckPythonApprovalSnapshot( invalid ) == QLatin1String( "python.approval_snapshot_invalid" ) );
}

TEST_CASE( "Python approval freezes real script bytes and refuses subsequent file edits", "[agent][python][approval][integration]" )
{
  application();
  const ScopedEnvironment environment( "SICNU_PYTHON_ENV_ROOT", configuredEnvironmentRoot().toUtf8() );
  QTemporaryDir workspace;
  REQUIRE( workspace.isValid() );
  const ScopedEnvironment policy( "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() );
  const QString path = workspace.filePath( QStringLiteral( "analysis.py" ) );
  const QByteArray approved = QByteArray::fromHex( "efbbbf" ) + "result = {'value': 7}\n";
  writeFile( path, approved );
  QJsonObject arguments = fileArguments( path );
  arguments.insert( QStringLiteral( "expected_environment_sha256" ), QString( 64, QLatin1Char( '0' ) ) );
  const auto snapshot = capturePythonApprovalSnapshot( QStringLiteral( "python:run_script" ), arguments );
  INFO( snapshot.error.toStdString() );
  REQUIRE( snapshot.error.isEmpty() );
  REQUIRE_FALSE( snapshot.args.contains( QStringLiteral( "script_path" ) ) );
  REQUIRE( snapshot.args.value( QStringLiteral( "script" ) ).toString().toUtf8() == approved );
  REQUIRE( snapshot.identity.value( QStringLiteral( "script_sha256" ) ).toString() == sha256( approved ) );
  REQUIRE( snapshot.identity.value( QStringLiteral( "script_path" ) ).toString() == path );
  REQUIRE( snapshot.identity.value( QStringLiteral( "script_canonical_path" ) ).toString() == QFileInfo( path ).canonicalFilePath() );
  REQUIRE( snapshot.args.value( QStringLiteral( "expected_environment_sha256" ) )
           == snapshot.identity.value( QStringLiteral( "environment_sha256" ) ) );
  REQUIRE_FALSE( snapshot.identity.contains( QStringLiteral( "error_message" ) ) );
  REQUIRE_FALSE( snapshot.identity.contains( QStringLiteral( "interpreter" ) ) );
  REQUIRE_FALSE( snapshot.identity.contains( QStringLiteral( "packages" ) ) );
  REQUIRE( recheckPythonApprovalSnapshot( snapshot ).isEmpty() );

  writeFile( path, "result = {'value': 8}\n" );
  REQUIRE( recheckPythonApprovalSnapshot( snapshot ) == QLatin1String( "python.approval_identity_changed" ) );
  REQUIRE( snapshot.args.value( QStringLiteral( "script" ) ).toString().toUtf8() == approved );
}

TEST_CASE( "Python prepare approval binds bootstrap identity without installing", "[agent][python][approval][integration]" )
{
  application();
  // An explicitly configured prepared fixture also confirms that the test host
  // has its bundled/development runtime configured. Never prepare it here.
  const ScopedEnvironment preparedRoot( "SICNU_PYTHON_ENV_ROOT", configuredEnvironmentRoot().toUtf8() );
  const auto ready = capturePythonApprovalSnapshot( QStringLiteral( "python:run_script" ),
    { { QStringLiteral( "script" ), QStringLiteral( "pass\n" ) } } );
  INFO( ready.error.toStdString() );
  REQUIRE( ready.error.isEmpty() );

  QTemporaryDir parent;
  REQUIRE( parent.isValid() );
  const QString missingRoot = parent.filePath( QStringLiteral( "unprepared" ) );
  const ScopedEnvironment missingEnvironment( "SICNU_PYTHON_ENV_ROOT", missingRoot.toUtf8() );
  const auto prepare = capturePythonApprovalSnapshot( QStringLiteral( "python:environment" ),
    { { QStringLiteral( "action" ), QStringLiteral( "prepare" ) },
      { QStringLiteral( "expected_runtime_identity_sha256" ), QString( 64, QLatin1Char( '0' ) ) },
      { QStringLiteral( "expected_environment_location_sha256" ), QString( 64, QLatin1Char( '0' ) ) } } );
  INFO( prepare.error.toStdString() );
  REQUIRE( prepare.error.isEmpty() );
  REQUIRE( prepare.identity.value( QStringLiteral( "environment_sha256" ) ).toString().isEmpty() );
  REQUIRE( prepare.args.value( QStringLiteral( "expected_runtime_identity_sha256" ) )
           == prepare.identity.value( QStringLiteral( "runtime_identity_sha256" ) ) );
  REQUIRE( prepare.args.value( QStringLiteral( "expected_environment_location_sha256" ) )
           == prepare.identity.value( QStringLiteral( "environment_location_sha256" ) ) );
  REQUIRE( recheckPythonApprovalSnapshot( prepare ).isEmpty() );
  REQUIRE_FALSE( QFileInfo::exists( missingRoot ) );
  REQUIRE( capturePythonApprovalSnapshot( QStringLiteral( "python:run_script" ),
    { { QStringLiteral( "script" ), QStringLiteral( "pass\n" ) } } ).error
    == QLatin1String( "python.approval_environment_not_ready" ) );
  REQUIRE_FALSE( QFileInfo::exists( missingRoot ) );

  // A different valid, unprepared destination has the same runtime and lock;
  // it still invalidates approval of the original environment location.
  const QString changedRoot = parent.filePath( QStringLiteral( "other-unprepared" ) );
  const ScopedEnvironment changedEnvironment( "SICNU_PYTHON_ENV_ROOT", changedRoot.toUtf8() );
  REQUIRE( recheckPythonApprovalSnapshot( prepare ) == QLatin1String( "python.approval_identity_changed" ) );
  REQUIRE_FALSE( QFileInfo::exists( changedRoot ) );
}

TEST_CASE( "Python prepare rejects stale approval identities before creating its root", "[agent][python][approval][integration]" )
{
  application();
  const ScopedEnvironment preparedRoot( "SICNU_PYTHON_ENV_ROOT", configuredEnvironmentRoot().toUtf8() );
  const auto ready = capturePythonApprovalSnapshot( QStringLiteral( "python:run_script" ),
    { { QStringLiteral( "script" ), QStringLiteral( "pass\n" ) } } );
  INFO( ready.error.toStdString() );
  REQUIRE( ready.error.isEmpty() );

  QTemporaryDir parent;
  REQUIRE( parent.isValid() );
  const QString root = parent.filePath( QStringLiteral( "must-not-be-created" ) );
  const ScopedEnvironment missingEnvironment( "SICNU_PYTHON_ENV_ROOT", root.toUtf8() );
  const auto snapshot = capturePythonApprovalSnapshot( QStringLiteral( "python:environment" ),
    { { QStringLiteral( "action" ), QStringLiteral( "prepare" ) } } );
  INFO( snapshot.error.toStdString() );
  REQUIRE( snapshot.error.isEmpty() );
  REQUIRE_FALSE( QFileInfo::exists( root ) );

  std::string runtime = snapshot.identity.value( QStringLiteral( "runtime_identity_sha256" ) ).toString().toStdString();
  std::string location = snapshot.identity.value( QStringLiteral( "environment_location_sha256" ) ).toString().toStdString();
  SECTION( "Runtime identity changed" )
  {
    runtime[0] = runtime[0] == '0' ? '1' : '0';
  }
  SECTION( "Environment location changed" )
  {
    location[0] = location[0] == '0' ? '1' : '0';
  }
  const Json::Value status = sicnu::operators::python::PythonEnvironmentManager().prepare(
    "geospatial-v1", {}, 30, runtime, location );
  REQUIRE( status["error_code"].asString() == "python.approval_identity_changed" );
  REQUIRE_FALSE( status["ready"].asBool() );
  REQUIRE_FALSE( QFileInfo::exists( root ) );
}

TEST_CASE( "Python script rejects a stale environment before executing any source", "[agent][python][approval][integration]" )
{
  application();
  const ScopedEnvironment environment( "SICNU_PYTHON_ENV_ROOT", configuredEnvironmentRoot().toUtf8() );
  const ScopedEnvironment execution( "SICNU_PYTHON_EXECUTION", QByteArrayLiteral( "trusted_local" ) );
  QTemporaryDir workspace;
  REQUIRE( workspace.isValid() );
  const ScopedEnvironment policy( "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() );
  const auto snapshot = capturePythonApprovalSnapshot( QStringLiteral( "python:run_script" ),
    { { QStringLiteral( "script" ), QStringLiteral( "pass\n" ) } } );
  INFO( snapshot.error.toStdString() );
  REQUIRE( snapshot.error.isEmpty() );

  std::string wrongIdentity = snapshot.identity.value( QStringLiteral( "environment_sha256" ) ).toString().toStdString();
  REQUIRE( wrongIdentity.size() == 64 );
  wrongIdentity[0] = wrongIdentity[0] == '0' ? '1' : '0';
  const QString marker = workspace.filePath( QStringLiteral( "source-executed.txt" ) );
  const QString outputDirectory = workspace.filePath( QStringLiteral( "uncreated-output" ) );
  Json::Value arguments( Json::objectValue );
  arguments["script"] = "from pathlib import Path\nPath(parameters['marker']).write_text('executed', encoding='utf-8')\nresult = {}\n";
  arguments["parameters"]["marker"] = marker.toUtf8().toStdString();
  arguments["output"] = QDir( outputDirectory ).filePath( QStringLiteral( "result.tif" ) ).toUtf8().toStdString();
  arguments["expected_environment_sha256"] = wrongIdentity;
  arguments["timeout_seconds"] = 30;
  sicnu::operators::RSOperatorContext context( workspace.path().toUtf8().toStdString() );
  sicnu::operators::python::PythonScriptOperator script;
  bool refused = false;
  try
  {
    script.run( arguments, context );
  }
  catch ( const sicnu::operators::RSOperatorError &error )
  {
    refused = true;
    REQUIRE( error.code() == sicnu::operators::ErrorCode::PolicyRefused );
  }
  REQUIRE( refused );
  REQUIRE_FALSE( QFileInfo::exists( marker ) );
  REQUIRE_FALSE( QFileInfo::exists( outputDirectory ) );
  REQUIRE( QDir( workspace.path() ).entryList( QDir::AllEntries | QDir::NoDotAndDotDot ).isEmpty() );
}
