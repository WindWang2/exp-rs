#include <catch2/catch_test_macros.hpp>

#include "operators/runtime/python_environment.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace
{
using sicnu::operators::python::PythonEnvironmentManager;

void ensureApplication()
{
  if ( QCoreApplication::instance() )
    return;
  static int argc = 1;
  static char name[] = "test_python_runtime_selection";
  static char *argv[] = { name, nullptr };
  static auto *application = new QCoreApplication( argc, argv );
  ( void ) application;
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
  REQUIRE( QDir().mkpath( QFileInfo( path ).absolutePath() ) );
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly ) );
  REQUIRE( file.write( bytes ) == bytes.size() );
}

std::string applicationDirectory( const QTemporaryDir &prefix )
{
  const QString app = prefix.filePath( QStringLiteral( "bin" ) );
  REQUIRE( QDir().mkpath( app ) );
  return app.toUtf8().toStdString();
}

QByteArray fakeManifest( const char *version = "3.12.16" )
{
  Json::Value manifest( Json::objectValue );
  manifest["schema"] = "sicnu.python-runtime/1";
  manifest["python_version"] = version;
  Json::StreamWriterBuilder writer;
  return QByteArray::fromStdString( Json::writeString( writer, manifest ) );
}
} // namespace

TEST_CASE( "Python runtime: missing bundle never falls back to PATH", "[python][runtime][packaging]" )
{
  ensureApplication();
  QTemporaryDir prefix;
  REQUIRE( prefix.isValid() );
  const ScopedEnvironment bootstrap( "SICNU_PYTHON_EXECUTABLE", {} );
  const ScopedEnvironment root( "SICNU_PYTHON_ENV_ROOT", prefix.filePath( "managed" ).toUtf8() );
  const auto status = PythonEnvironmentManager( applicationDirectory( prefix ) ).probe( "geospatial-v1" );
  INFO( Json::writeString( Json::StreamWriterBuilder{}, status ) );
  REQUIRE_FALSE( status["ready"].asBool() );
  REQUIRE( status["error_code"].asString() == "python.runtime_missing" );
  REQUIRE_FALSE( QFileInfo::exists( prefix.filePath( "managed" ) ) );
}

TEST_CASE( "Python runtime: incomplete packaged payload overrides a development interpreter", "[python][runtime][packaging]" )
{
  ensureApplication();
  QTemporaryDir prefix;
  REQUIRE( prefix.isValid() );
  writeFile( prefix.filePath( "share/sicnu-geo-rs/python-runtime.json" ), fakeManifest() );
  const ScopedEnvironment bootstrap( "SICNU_PYTHON_EXECUTABLE", QCoreApplication::applicationFilePath().toUtf8() );
  const ScopedEnvironment root( "SICNU_PYTHON_ENV_ROOT", prefix.filePath( "managed" ).toUtf8() );
  const auto status = PythonEnvironmentManager( applicationDirectory( prefix ) ).prepare( "geospatial-v1" );
  INFO( Json::writeString( Json::StreamWriterBuilder{}, status ) );
  REQUIRE_FALSE( status["ready"].asBool() );
  REQUIRE( status["runtime_source"].asString() == "bundled" );
  REQUIRE( status["error_code"].asString() == "python.runtime_missing" );
  REQUIRE_FALSE( QFileInfo::exists( prefix.filePath( "managed" ) ) );
}

TEST_CASE( "Python runtime: payload directory without provenance is rejected", "[python][runtime][packaging]" )
{
  ensureApplication();
  QTemporaryDir prefix;
  REQUIRE( prefix.isValid() );
  REQUIRE( QDir().mkpath( prefix.filePath( "libexec/sicnu-geo-rs/python" ) ) );
  const ScopedEnvironment bootstrap( "SICNU_PYTHON_EXECUTABLE", QCoreApplication::applicationFilePath().toUtf8() );
  const ScopedEnvironment root( "SICNU_PYTHON_ENV_ROOT", prefix.filePath( "managed" ).toUtf8() );
  const auto status = PythonEnvironmentManager( applicationDirectory( prefix ) ).probe( "geospatial-v1" );
  INFO( Json::writeString( Json::StreamWriterBuilder{}, status ) );
  REQUIRE_FALSE( status["ready"].asBool() );
  REQUIRE( status["runtime_source"].asString() == "bundled" );
  REQUIRE( status["error_code"].asString() == "python.runtime_manifest_missing" );
  REQUIRE_FALSE( QFileInfo::exists( prefix.filePath( "managed" ) ) );
}

TEST_CASE( "Python runtime: packaged version is fixed before any bootstrap executes", "[python][runtime][packaging]" )
{
  ensureApplication();
  QTemporaryDir prefix;
  REQUIRE( prefix.isValid() );
  writeFile( prefix.filePath( "share/sicnu-geo-rs/python-runtime.json" ), fakeManifest( "3.13.9" ) );
  const ScopedEnvironment bootstrap( "SICNU_PYTHON_EXECUTABLE", QCoreApplication::applicationFilePath().toUtf8() );
  const ScopedEnvironment root( "SICNU_PYTHON_ENV_ROOT", prefix.filePath( "managed" ).toUtf8() );
  const auto status = PythonEnvironmentManager( applicationDirectory( prefix ) ).prepare( "geospatial-v1" );
  INFO( Json::writeString( Json::StreamWriterBuilder{}, status ) );
  REQUIRE_FALSE( status["ready"].asBool() );
  REQUIRE( status["error_code"].asString() == "python.runtime_version_unsupported" );
  REQUIRE_FALSE( QFileInfo::exists( prefix.filePath( "managed" ) ) );
}
