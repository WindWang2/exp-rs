#include <catch2/catch_test_macros.hpp>

#include "operators/runtime/python_environment.h"
#include "operators/runtime/python_managed_site_path.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
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

TEST_CASE( "Python runtime: Windows probe identity uses the interpreter ABI with a strict legacy binding",
           "[python][runtime][packaging][integration][abi]" )
{
#ifndef Q_OS_WIN
  SKIP( "Windows interpreter ABI regression." );
#else
  ensureApplication();
  const QString root = qEnvironmentVariable( "SICNU_TEST_PYTHON_ENV_ROOT" );
  const QString bootstrap = qEnvironmentVariable( "SICNU_PYTHON_EXECUTABLE" );
  if ( root.isEmpty() || bootstrap.isEmpty() )
    SKIP( "Requires an explicitly prepared environment and selected bundled interpreter; no installation." );
  QJsonObject ready;
  QString readyPath;
  for ( const QString &directory : QDir( root ).entryList( { QStringLiteral( "geospatial-v1-py312-*" ) }, QDir::Dirs | QDir::NoDotAndDotDot ) )
  {
    const QString path = QDir( root ).filePath( directory + QStringLiteral( "/ready.json" ) );
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
      continue;
    const auto candidate = QJsonDocument::fromJson( file.readAll() ).object();
    if ( QDir::cleanPath( candidate.value( QStringLiteral( "bootstrap_interpreter" ) ).toString() ) != QDir::cleanPath( bootstrap ) )
      continue;
    REQUIRE( readyPath.isEmpty() );
    ready = candidate;
    readyPath = path;
  }
  REQUIRE_FALSE( readyPath.isEmpty() );
  REQUIRE( ready.value( QStringLiteral( "ready" ) ).toBool() );
  const QString interpreter = ready.value( QStringLiteral( "interpreter" ) ).toString();
  REQUIRE( QFileInfo::exists( interpreter ) );
  // Execute the actual production payload, rather than a second copy of the
  // ABI selection. Public manager/approval tests cover its C++ binding gate.
  QFile source( QDir( QFileInfo( QString::fromUtf8( __FILE__ ) ).absolutePath() )
                  .filePath( QStringLiteral( "../src/operators/runtime/python_environment.cpp" ) ) );
  REQUIRE( source.open( QIODevice::ReadOnly ) );
  const QByteArray bytes = source.readAll();
  const QByteArray opening = "const std::string kProbe = std::string( kManagedSitePathBootstrap ) + R\"PY(";
  const auto start = bytes.indexOf( opening );
  REQUIRE( start >= 0 );
  const auto end = bytes.indexOf( ")PY\";", start + opening.size() );
  REQUIRE( end > start );
  QTemporaryDir scratch;
  REQUIRE( scratch.isValid() );
  const QString payload = scratch.filePath( QStringLiteral( "production-probe.py" ) );
  writeFile( payload, QByteArray( sicnu::operators::python::kManagedSitePathBootstrap )
                      + bytes.mid( start + opening.size(), end - start - opening.size() ) );
  const QByteArray harness = R"PY(
import contextlib, hashlib, io, json, os, sys, sysconfig, types
from pathlib import Path
ready = json.loads(Path(sys.argv[2]).read_text(encoding='utf-8'))
code = compile(Path(sys.argv[1]).read_text(encoding='utf-8'), '<production-python-probe>', 'exec')
sys.argv = ['<production-python-probe>', str(Path(sys.argv[2]).parent / 'requirements.lock')]
def reject_wmi(query):
    raise AssertionError('A cold managed environment probe queried Windows Management Instrumentation')
sys.modules['_wmi'] = types.SimpleNamespace(exec_query=reject_wmi)
def probe():
    capture = io.StringIO()
    with contextlib.redirect_stdout(capture):
        exec(code, {})
    return json.loads(capture.getvalue())
def bind(snapshot):
    value = dict(snapshot)
    for key in ('profile_id', 'lock_sha256', 'interpreter', 'runtime_identity_sha256'):
        value[key] = ready[key]
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode('utf-8')).hexdigest()
os.environ['PROCESSOR_ARCHITECTURE'] = 'untrusted-first'
os.environ['PROCESSOR_ARCHITEW6432'] = 'untrusted-second'
os.environ['PROJ_DATA'] = 'Z:/untrusted-host-proj'
os.environ['PROJ_LIB'] = 'Z:/untrusted-host-proj-legacy'
first = probe()
import platform
platform._uname_cache = platform.uname_result('Windows', 'probe-host', 'release', 'version', 'decoy-host')
def guarded_machine():
    if sys._getframe(1).f_code.co_filename == '<production-python-probe>':
        raise RuntimeError('unstable machine metadata must not be queried for identity')
    return 'decoy-host'
platform.machine = guarded_machine
os.environ['PROCESSOR_ARCHITECTURE'] = 'different-first'
os.environ['PROCESSOR_ARCHITEW6432'] = 'different-second'
second = probe()
assert first == second, 'CPU environment changed the identity'
assert bind(first) == ready['environment_sha256'], 'Valid legacy environment fingerprint changed'
for abi, expected in (('win-amd64', 'AMD64'), ('win-arm64', 'ARM64'), ('win32', 'x86')):
    sysconfig.get_platform = lambda: abi
    assert probe()['platform'] == 'win32-' + expected
sysconfig.get_platform = lambda: 'win-unknown'
try:
    probe()
except RuntimeError as error:
    assert 'Unsupported Windows interpreter ABI' in str(error)
else:
    raise AssertionError('Unknown interpreter ABI was accepted')
sysconfig.get_platform = lambda: 'win-amd64'
from importlib import metadata
real_version = metadata.version
metadata.version = lambda name: '0.0.0' if name == 'numpy' else real_version(name)
try:
    probe()
except RuntimeError as error:
    assert 'does not match the managed lock' in str(error)
else:
    raise AssertionError('Locked package identity drift was accepted')
print(json.dumps({'stable': True, 'legacy_marker_compatible': True,
    'known_abis': 3, 'unknown_abi_rejected': True, 'locked_package_drift_rejected': True}))
)PY";
  QProcess child;
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert( QStringLiteral( "PYTHONDONTWRITEBYTECODE" ), QStringLiteral( "1" ) );
  environment.insert( QStringLiteral( "MPLBACKEND" ), QStringLiteral( "Agg" ) );
  environment.insert( QStringLiteral( "MPLCONFIGDIR" ), scratch.filePath( QStringLiteral( "matplotlib" ) ) );
  for ( const auto &name : { "OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "MKL_NUM_THREADS" } )
    environment.insert( QString::fromLatin1( name ), QStringLiteral( "1" ) );
  child.setProcessEnvironment( environment );
  child.setWorkingDirectory( scratch.path() );
  child.start( interpreter, { QStringLiteral( "-I" ), QStringLiteral( "-B" ), QStringLiteral( "-X" ), QStringLiteral( "utf8" ),
                            QStringLiteral( "-c" ), QString::fromUtf8( harness ), payload, readyPath } );
  REQUIRE( child.waitForStarted( 5000 ) );
  const bool finished = child.waitForFinished( 25000 );
  if ( !finished )
  {
    child.kill();
    child.waitForFinished( 5000 );
  }
  INFO( child.readAllStandardError().toStdString() );
  REQUIRE( finished );
  REQUIRE( child.exitStatus() == QProcess::NormalExit );
  REQUIRE( child.exitCode() == 0 );
  const auto report = QJsonDocument::fromJson( child.readAllStandardOutput() ).object();
  REQUIRE( report.value( QStringLiteral( "stable" ) ).toBool() );
  REQUIRE( report.value( QStringLiteral( "legacy_marker_compatible" ) ).toBool() );
  REQUIRE( report.value( QStringLiteral( "known_abis" ) ).toInt() == 3 );
  REQUIRE( report.value( QStringLiteral( "unknown_abi_rejected" ) ).toBool() );
  REQUIRE( report.value( QStringLiteral( "locked_package_drift_rejected" ) ).toBool() );
#endif
}
