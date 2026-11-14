#include "operators/runtime/python_environment.h"

#include "exprs/external_process.h"
#include "exprs/path_policy.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QResource>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <chrono>
#include <memory>

// Keep the resource registration outside the C++ namespace. This also pulls
// the qrc object into static-library consumers of the environment manager.
static void initializePythonEnvironmentResources()
{
  static const bool initialized = [] {
    Q_INIT_RESOURCE( python_environments );
    return true;
  }();
  ( void ) initialized;
}

namespace sicnu::operators::python
{
namespace
{
using Clock = std::chrono::steady_clock;
using Cancel = std::function<bool()>;

constexpr auto kProfile = "geospatial-v1";
constexpr auto kBundledVersion = "3.12.16";
constexpr qint64 kJsonLimit = 128 * 1024;
constexpr qint64 kRuntimeManifestLimit = 8 * 1024 * 1024;

struct Environment
{
  Json::Value status;
  QString root;
  QString directory;
  QString interpreter;
  QString lockPath;
  QString markerPath;
  QString bootstrap;
  QString bundlePrefix;
  QString wheelhouse;
  QByteArray lock;
};

std::string utf8( const QString &value )
{
  return value.toUtf8().toStdString();
}

std::string sha256( const QByteArray &bytes )
{
  return QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex().toStdString();
}

Json::Value failure( Json::Value status, const char *code, const std::string &message )
{
  status["ready"] = false;
  status["success"] = false;
  status["error_code"] = code;
  status["error_message"] = message;
  return status;
}

bool stopped( Json::Value &status, const Cancel &cancelled, Clock::time_point deadline )
{
  if ( cancelled && cancelled() )
  {
    status = failure( status, "python.cancelled", "Python environment operation was cancelled." );
    status["cancelled"] = true;
    return true;
  }
  if ( Clock::now() >= deadline )
  {
    status = failure( status, "python.timeout", "Python environment operation exceeded its time budget." );
    status["timed_out"] = true;
    return true;
  }
  return false;
}

bool parseJson( const QByteArray &bytes, Json::Value &value )
{
  Json::CharReaderBuilder builder;
  builder["allowComments"] = false;
  builder["rejectDupKeys"] = true;
  builder["failIfExtra"] = true;
  const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  std::string error;
  return reader->parse( bytes.constData(), bytes.constData() + bytes.size(), &value, &error )
         && value.isObject();
}

QByteArray jsonBytes( const Json::Value &value )
{
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  return QByteArray::fromStdString( Json::writeString( writer, value ) );
}

bool saveFile( const QString &path, const QByteArray &bytes )
{
  QSaveFile file( path );
  return file.open( QIODevice::WriteOnly ) && file.write( bytes ) == bytes.size() && file.commit();
}

QString bundledInterpreterRelativePath()
{
#ifdef Q_OS_WIN
  return QStringLiteral( "libexec/sicnu-geo-rs/python/python.exe" );
#else
  return QStringLiteral( "libexec/sicnu-geo-rs/python/bin/python3" );
#endif
}

QString wheelhouseRelativePath()
{
  return QStringLiteral( "share/sicnu-geo-rs/python-wheels/geospatial-v1" );
}

bool bundlePresent( const QString &prefix )
{
  for ( const QString &relative : { QStringLiteral( "share/sicnu-geo-rs/python-runtime.json" ),
                                    QStringLiteral( "libexec/sicnu-geo-rs/python" ) } )
  {
    const QFileInfo info( QDir( prefix ).filePath( relative ) );
    if ( info.exists() || info.isSymLink() )
      return true;
  }
  return false;
}

bool selectBundle( Environment &env, const QString &prefix )
{
  env.bundlePrefix = QDir::cleanPath( prefix );
  env.bootstrap = QDir( prefix ).filePath( bundledInterpreterRelativePath() );
  env.wheelhouse = QDir( prefix ).filePath( wheelhouseRelativePath() );
  env.status["runtime_source"] = "bundled";
  env.status["bootstrap_interpreter"] = utf8( env.bootstrap );
  env.status["runtime_required_version"] = kBundledVersion;
  env.status["wheelhouse"] = utf8( env.wheelhouse );
  env.status["offline"] = true;
  const QString manifestPath = QDir( prefix ).filePath( QStringLiteral( "share/sicnu-geo-rs/python-runtime.json" ) );
  QFile manifestFile( manifestPath );
  if ( !QFileInfo( manifestPath ).isFile() || !manifestFile.open( QIODevice::ReadOnly ) )
  {
    env.status = failure( env.status, "python.runtime_manifest_missing", "The packaged Python runtime provenance is missing; system Python will not be used." );
    return false;
  }
  const QByteArray manifestBytes = manifestFile.read( kRuntimeManifestLimit + 1 );
  Json::Value manifest;
  if ( manifestBytes.size() > kRuntimeManifestLimit || !parseJson( manifestBytes, manifest )
       || manifest["schema"] != "sicnu.python-runtime/1" )
  {
    env.status = failure( env.status, "python.runtime_manifest_invalid", "The packaged Python runtime provenance is invalid." );
    return false;
  }
  if ( !manifest["python_version"].isString() || manifest["python_version"].asString() != kBundledVersion )
  {
    env.status = failure( env.status, "python.runtime_version_unsupported", "The packaged runtime must be CPython 3.12.16." );
    return false;
  }
  if ( !QFileInfo( env.bootstrap ).isFile()
       || !exprs::PathPolicy::resolvesInsideRoot( utf8( prefix ), utf8( env.bootstrap ) ) )
  {
    env.status = failure( env.status, "python.runtime_missing", "The packaged Python interpreter is missing or escapes its installation; system Python will not be used." );
    return false;
  }
  if ( manifest["interpreter"] != utf8( bundledInterpreterRelativePath() )
       || manifest["wheelhouse"] != utf8( wheelhouseRelativePath() )
       || manifest["requirements_sha256"] != env.status["lock_sha256"] )
  {
    env.status = failure( env.status, "python.runtime_manifest_invalid", "The packaged runtime does not match the fixed interpreter layout and dependency lock." );
    return false;
  }
  QFile wheelLock( QDir( env.wheelhouse ).filePath( QStringLiteral( "requirements.lock" ) ) );
  if ( !QFileInfo( env.wheelhouse ).isDir()
       || !exprs::PathPolicy::resolvesInsideRoot( utf8( prefix ), utf8( env.wheelhouse ) )
       || !wheelLock.open( QIODevice::ReadOnly ) || wheelLock.read( env.lock.size() + 1 ) != env.lock )
  {
    env.status = failure( env.status, "python.wheelhouse_invalid", "The packaged offline wheelhouse is missing or has a different dependency lock." );
    return false;
  }
  env.status["runtime_manifest_sha256"] = sha256( manifestBytes );
  return true;
}

bool selectBootstrap( Environment &env, const std::string &applicationDirectory )
{
  const QString app = applicationDirectory.empty()
                        ? ( QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString() )
                        : QString::fromUtf8( applicationDirectory.data(), static_cast<qsizetype>( applicationDirectory.size() ) );
  if ( !app.isEmpty() && !QDir::isAbsolutePath( app ) )
  {
    env.status = failure( env.status, "python.application_directory_invalid", "The host executable directory must be absolute." );
    return false;
  }
  if ( !app.isEmpty() )
  {
    const QStringList prefixes = { QDir::cleanPath( app ), QDir::cleanPath( QDir( app ).absoluteFilePath( QStringLiteral( ".." ) ) ) };
    for ( const QString &prefix : prefixes )
      if ( bundlePresent( prefix ) )
        return selectBundle( env, prefix );
  }

  // A developer must select an interpreter explicitly. PATH is consulted
  // only to resolve that explicit name, never as an automatic fallback.
  const QString configured = qEnvironmentVariable( "SICNU_PYTHON_EXECUTABLE" );
  if ( configured.isEmpty() )
  {
    env.status = failure( env.status, "python.runtime_missing", "The bundled CPython 3.12 runtime is missing. A source-build host may explicitly configure SICNU_PYTHON_EXECUTABLE to CPython 3.12." );
    return false;
  }
  const QString interpreter = QDir::isAbsolutePath( configured ) ? QDir::cleanPath( configured )
                                                               : QStandardPaths::findExecutable( configured );
  if ( interpreter.isEmpty() || !QFileInfo( interpreter ).isFile() )
  {
    env.status = failure( env.status, "python.runtime_missing", "The explicitly configured development Python interpreter is missing." );
    return false;
  }
  // Source builds can point at an explicitly staged install-prefix payload;
  // finding its receipt retains the same offline checks as an installed app.
  QString candidate = QFileInfo( interpreter ).absolutePath();
  for ( int level = 0; level < 5; ++level )
  {
    if ( bundlePresent( candidate ) )
      return selectBundle( env, candidate );
    const QString parent = QDir( candidate ).absoluteFilePath( QStringLiteral( ".." ) );
    if ( QDir::cleanPath( parent ) == QDir::cleanPath( candidate ) )
      break;
    candidate = QDir::cleanPath( parent );
  }
  env.bootstrap = interpreter;
  env.status["runtime_source"] = "development_override";
  env.status["bootstrap_interpreter"] = utf8( interpreter );
  env.status["runtime_required_version"] = "3.12";
  return true;
}

void bindEnvironmentPaths( Environment &env, const Json::Value &bootstrap )
{
  Json::Value identity( Json::objectValue );
  identity["profile_id"] = env.status["profile_id"];
  identity["lock_sha256"] = env.status["lock_sha256"];
  identity["bootstrap_interpreter"] = env.status["bootstrap_interpreter"];
  identity["runtime_manifest_sha256"] = env.status["runtime_manifest_sha256"];
  identity["bootstrap"] = bootstrap;
  const std::string digest = sha256( jsonBytes( identity ) );
  env.status["runtime_identity_sha256"] = digest;
  // A moved installation gets a fresh venv: pyvenv.cfg retains the absolute
  // base-interpreter location and cannot safely follow a package relocation.
  env.directory = QDir( env.root ).filePath( QStringLiteral( "geospatial-v1-py312-" )
                                            + QString::fromStdString( digest ).left( 16 ) );
#ifdef Q_OS_WIN
  env.interpreter = QDir( env.directory ).filePath( QStringLiteral( "Scripts/python.exe" ) );
#else
  // Keep the venv launch symlink rather than canonicalizing to the base.
  env.interpreter = QDir( env.directory ).filePath( QStringLiteral( "bin/python" ) );
#endif
  env.lockPath = QDir( env.directory ).filePath( QStringLiteral( "requirements.lock" ) );
  env.markerPath = QDir( env.directory ).filePath( QStringLiteral( "ready.json" ) );
  env.status["interpreter"] = utf8( env.interpreter );
  env.status["environment_directory"] = utf8( env.directory );
  for ( const QString &path : { env.directory, env.lockPath, env.markerPath } )
  {
    const QFileInfo info( path );
    if ( info.isSymLink()
         || ( info.exists() && !exprs::PathPolicy::resolvesInsideRoot( utf8( env.root ), utf8( path ) ) ) )
    {
      env.status = failure( env.status, "python.environment_path_escape", "A managed environment path escapes its host-owned root." );
      break;
    }
  }
}

Environment environment( const std::string &profileId, const std::string &applicationDirectory )
{
  Environment env;
  env.status["ready"] = false;
  env.status["success"] = false;
  env.status["profile_id"] = profileId;
  env.status["interpreter"] = "";
  env.status["python_version"] = "";
  env.status["lock_sha256"] = "";
  env.status["environment_sha256"] = "";
  env.status["packages"] = Json::Value( Json::objectValue );
  env.status["error_code"] = "";
  env.status["error_message"] = "";
  env.status["cancelled"] = false;
  env.status["timed_out"] = false;
  env.status["runtime_source"] = "missing";
  env.status["runtime_required_version"] = "3.12";
  env.status["bootstrap_interpreter"] = "";
  env.status["runtime_manifest_sha256"] = "";
  env.status["wheelhouse"] = "";
  env.status["offline"] = false;
  if ( profileId != kProfile )
  {
    env.status = failure( env.status, "python.profile_unknown", "Unknown Python environment profile: " + profileId );
    return env;
  }

  initializePythonEnvironmentResources();
  QFile resource( QStringLiteral( ":/python/geospatial-v1/requirements.lock" ) );
  if ( !resource.open( QIODevice::ReadOnly ) || ( env.lock = resource.readAll() ).isEmpty() )
  {
    env.status = failure( env.status, "python.profile_unavailable", "The bundled Python dependency lock is unavailable." );
    return env;
  }
  env.status["lock_sha256"] = sha256( env.lock );
  env.root = qEnvironmentVariable( "SICNU_PYTHON_ENV_ROOT" );
  if ( env.root.isEmpty() )
    env.root = QDir( QStandardPaths::writableLocation( QStandardPaths::GenericDataLocation ) )
                 .filePath( QStringLiteral( "sicnu-geo-rs/python-environments" ) );
  if ( !QDir::isAbsolutePath( env.root ) )
  {
    env.status = failure( env.status, "python.environment_root_invalid", "The host Python environment root must be absolute." );
    return env;
  }
  env.root = QDir::cleanPath( env.root );
  if ( QFileInfo::exists( env.root ) && !QFileInfo( env.root ).isDir() )
  {
    env.status = failure( env.status, "python.environment_root_invalid", "The host Python environment root is not a directory." );
    return env;
  }
  selectBootstrap( env, applicationDirectory );
  return env;
}

bool run( const Environment &env, std::vector<std::string> argv, const Cancel &cancelled,
          Clock::time_point deadline, Json::Value &status, std::string *output = nullptr )
{
  if ( stopped( status, cancelled, deadline ) )
    return false;
  exprs::ExternalProcessRequest request;
  request.argv = std::move( argv );
  const std::string workspace = exprs::PathPolicy::workspaceRoot();
  request.workingDirectory = env.directory.isEmpty()
                               ? ( workspace.empty() ? utf8( QDir::currentPath() ) : workspace )
                               : utf8( env.directory );
  request.additionalAllowedRoots = { utf8( env.root ) };
  if ( !env.bundlePrefix.isEmpty() )
    request.additionalAllowedRoots.push_back( utf8( env.bundlePrefix ) );
  request.timeoutSeconds = std::max( 1, static_cast<int>( std::chrono::duration_cast<std::chrono::seconds>( deadline - Clock::now() ).count() ) );
  request.stdoutLimitBytes = kJsonLimit;
  request.stderrLimitBytes = 64 * 1024;
  request.isCancelled = [cancelled, deadline] {
    return ( cancelled && cancelled() ) || Clock::now() >= deadline;
  };
  // Both ensurepip and pip use a task-owned temporary directory. The
  // interpreter is isolated with -I; no PYTHONPATH/HOME injection is needed.
  if ( !env.directory.isEmpty() )
  {
    const std::string temp = utf8( QDir( env.directory ).filePath( QStringLiteral( ".tmp" ) ) );
    for ( const char *key : { "TEMP", "TMP", "TMPDIR" } )
      request.environment[key] = temp;
  }
  for ( const char *key : { "OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "MKL_NUM_THREADS" } )
    request.environment[key] = "1";
  const exprs::ExternalProcessResult result = exprs::ExternalProcess::run( request );
  if ( stopped( status, cancelled, deadline ) )
    return false;
  if ( result.timedOut || result.cancelled )
  {
    status = failure( status, result.timedOut ? "python.timeout" : "python.cancelled",
                      "Python environment child process did not complete." );
    status["timed_out"] = result.timedOut;
    status["cancelled"] = result.cancelled;
    return false;
  }
  if ( !result.exitedCleanly() || result.truncatedStdout )
  {
    std::string detail = result.error.empty()
                                 ? ( result.stdErr.empty() ? result.stdOut : result.stdErr )
                                 : result.error;
    if ( detail.empty() )
      detail = "Python environment child failed (exit code " + std::to_string( result.exitCode )
               + ", signal " + std::to_string( result.exitSignal ) + ").";
    status = failure( status, "python.process_failed", detail.substr( 0, 4096 ) );
    return false;
  }
  if ( output )
    *output = result.stdOut;
  return true;
}

const char *kBootstrapProbe = R"PY(
import json, sys
value = {'python_version': '.'.join(str(v) for v in sys.version_info[:3]),
         'implementation': sys.implementation.name, 'python_build': sys.version,
         'interpreter_sha256': ''}
if sys.version_info[:2] == (3, 12):
    import hashlib
    with open(sys.executable, 'rb') as binary:
        value['interpreter_sha256'] = hashlib.file_digest(binary, 'sha256').hexdigest()
print(json.dumps(value, sort_keys=True))
)PY";

bool verifyBootstrap( Environment &env, const Cancel &cancelled, Clock::time_point deadline )
{
  if ( !env.status["error_code"].asString().empty() )
    return false;
  std::string output;
  if ( !run( env, { utf8( env.bootstrap ), "-I", "-B", "-X", "utf8", "-c", kBootstrapProbe },
             cancelled, deadline, env.status, &output ) )
    return false;
  Json::Value bootstrap;
  if ( !parseJson( QByteArray::fromStdString( output ), bootstrap )
       || !bootstrap["python_version"].isString() || !bootstrap["implementation"].isString() )
  {
    env.status = failure( env.status, "python.runtime_probe_invalid", "The selected Python runtime did not return valid version information." );
    return false;
  }
  const std::string version = bootstrap["python_version"].asString();
  env.status["python_version"] = version;
  env.status["bootstrap_python_version"] = version;
  if ( bootstrap["implementation"].asString() != "cpython" || version.rfind( "3.12.", 0 ) != 0
       || ( !env.bundlePrefix.isEmpty() && version != kBundledVersion ) )
  {
    env.status = failure( env.status, "python.runtime_version_unsupported",
      "The managed profile requires CPython " + env.status["runtime_required_version"].asString()
        + "; the selected interpreter reported " + version + "." );
    return false;
  }
  if ( !bootstrap["interpreter_sha256"].isString() || bootstrap["interpreter_sha256"].asString().size() != 64 )
  {
    env.status = failure( env.status, "python.runtime_probe_invalid", "The selected Python runtime did not return its interpreter identity." );
    return false;
  }
  env.status["bootstrap_sha256"] = bootstrap["interpreter_sha256"];
  bindEnvironmentPaths( env, bootstrap );
  return env.status["error_code"].asString().empty();
}

// Read the exact universal lock (including platform markers), import native
// modules, and report all installed distributions for the environment identity.
const char *kProbe = R"PY(
import hashlib, importlib.metadata as metadata, json, platform, re, sys
from pathlib import Path
if sys.version_info[:2] != (3, 12):
    raise RuntimeError('geospatial-v1 requires CPython 3.12')
import numpy, rasterio
from packaging.requirements import Requirement
from packaging.utils import canonicalize_name
if numpy.__version__ != metadata.version('numpy') or rasterio.__version__ != metadata.version('rasterio'):
    raise RuntimeError('Imported native package versions differ from their distribution metadata')
text = Path(sys.argv[1]).read_text(encoding='utf-8')
logical = re.sub(r'\\\r?\n', '', text)
expected = set()
for line in logical.splitlines():
    line = line.split('--hash=', 1)[0].strip()
    if not line or line.startswith('#') or line.startswith('--'):
        continue
    requirement = Requirement(line)
    if requirement.marker and not requirement.marker.evaluate():
        continue
    version = metadata.version(requirement.name)
    if not requirement.specifier.contains(version, prereleases=True):
        raise RuntimeError(f'{requirement.name}=={version} does not match the managed lock')
    expected.add(canonicalize_name(requirement.name))
packages = {}
for distribution in metadata.distributions():
    name = canonicalize_name(distribution.metadata['Name'])
    if name in packages:
        raise RuntimeError('Duplicate installed distribution: ' + name)
    packages[name] = distribution.version
unexpected = set(packages) - expected - {'pip', 'setuptools'}
if unexpected:
    raise RuntimeError('Unmanaged packages in environment: ' + ', '.join(sorted(unexpected)))
with open(sys.executable, 'rb') as binary:
    interpreter_hash = hashlib.file_digest(binary, 'sha256').hexdigest()
print(json.dumps({'python_version': platform.python_version(), 'python_build': sys.version,
    'platform': sys.platform + '-' + platform.machine(), 'packages': packages,
    'interpreter_sha256': interpreter_hash}, sort_keys=True))
)PY";

Json::Value probeEnvironment( const Environment &env, const Cancel &cancelled,
                              Clock::time_point deadline, bool requireMarker )
{
  Json::Value status = env.status;
  if ( !status["error_code"].asString().empty() || stopped( status, cancelled, deadline ) )
    return status;
  QFile markerFile( env.markerPath );
  Json::Value marker;
  if ( requireMarker && ( !markerFile.open( QIODevice::ReadOnly ) || markerFile.size() > kJsonLimit
                         || !parseJson( markerFile.readAll(), marker ) ) )
    return failure( status, "python.environment_missing", "The managed Python environment is not prepared; call python:environment with action=prepare." );
  QFile lockFile( env.lockPath );
  if ( !lockFile.open( QIODevice::ReadOnly ) || lockFile.size() != env.lock.size() || lockFile.readAll() != env.lock )
    return failure( status, "python.environment_invalid", "The environment dependency lock does not match the bundled profile." );
  if ( !QFileInfo( env.interpreter ).isFile() )
    return failure( status, "python.interpreter_missing", "The managed Python interpreter is missing." );
  std::string output;
  if ( !run( env, { utf8( env.interpreter ), "-I", "-B", "-X", "utf8", "-c", kProbe, utf8( env.lockPath ) },
             cancelled, deadline, status, &output ) )
    return status;
  Json::Value snapshot;
  if ( !parseJson( QByteArray::fromStdString( output ), snapshot ) || !snapshot["packages"].isObject()
       || !snapshot["python_version"].isString() || !snapshot["interpreter_sha256"].isString() )
    return failure( status, "python.probe_invalid", "The Python interpreter returned an invalid environment probe." );
  if ( snapshot["python_version"] != status["bootstrap_python_version"] )
    return failure( status, "python.environment_changed", "The managed environment no longer uses the selected CPython 3.12 runtime." );
  status["python_version"] = snapshot["python_version"];
  status["packages"] = snapshot["packages"];
  snapshot["profile_id"] = kProfile;
  snapshot["lock_sha256"] = status["lock_sha256"];
  snapshot["interpreter"] = status["interpreter"];
  snapshot["runtime_identity_sha256"] = status["runtime_identity_sha256"];
  status["environment_sha256"] = sha256( jsonBytes( snapshot ) );
  if ( requireMarker && ( marker["profile_id"] != status["profile_id"]
                          || marker["lock_sha256"] != status["lock_sha256"]
                          || marker["interpreter"] != status["interpreter"]
                          || marker["environment_sha256"] != status["environment_sha256"] ) )
    return failure( status, "python.environment_changed", "The prepared Python environment changed; its recorded identity no longer matches." );
  status["ready"] = true;
  status["success"] = true;
  return status;
}

} // namespace

Json::Value PythonEnvironmentManager::probe( const std::string &profileId, const Cancel &cancelled ) const
{
  const auto deadline = Clock::now() + std::chrono::seconds( 30 );
  Environment env = environment( profileId, m_applicationDirectory );
  if ( !verifyBootstrap( env, cancelled, deadline ) )
    return env.status;
  return probeEnvironment( env, cancelled, deadline, true );
}

Json::Value PythonEnvironmentManager::prepare( const std::string &profileId, const Cancel &cancelled,
                                              int timeoutSeconds ) const
{
  Environment env = environment( profileId, m_applicationDirectory );
  Json::Value status = env.status;
  if ( !status["error_code"].asString().empty() )
    return status;
  if ( timeoutSeconds <= 0 || timeoutSeconds > 3600 )
    return failure( status, "python.timeout_invalid", "Environment preparation timeout must be between 1 and 3600 seconds." );
  const auto deadline = Clock::now() + std::chrono::seconds( timeoutSeconds );
  if ( stopped( status, cancelled, deadline ) )
    return status;
  if ( !verifyBootstrap( env, cancelled, deadline ) )
    return env.status;
  status = env.status;
  if ( !QDir().mkpath( env.root ) )
    return failure( status, "python.environment_root_invalid", "Cannot create the host Python environment root." );
  QLockFile lock( env.directory + QStringLiteral( ".prepare.lock" ) );
  lock.setStaleLockTime( 0 );
  if ( !lock.tryLock( 0 ) )
    return failure( status, "python.environment_busy", "This Python environment is already being prepared." );
  if ( QFileInfo::exists( env.markerPath ) )
  {
    // Ready environments are immutable. Never pip-install into an environment
    // which another script task may already be using, even when it drifted.
    return probeEnvironment( env, cancelled, deadline, true );
  }
  if ( !QDir().mkpath( env.directory )
       || !QDir().mkpath( QDir( env.directory ).filePath( QStringLiteral( ".tmp" ) ) ) )
    return failure( status, "python.environment_root_invalid", "Cannot create the managed Python environment directory." );
  if ( !run( env, { utf8( env.bootstrap ), "-I", "-B", "-X", "utf8", "-m", "venv", "--copies", utf8( env.directory ) },
             cancelled, deadline, status ) )
    return status;
  if ( !saveFile( env.lockPath, env.lock ) )
    return failure( status, "python.lock_write_failed", "Cannot write the bundled lock into the managed environment." );
  std::vector<std::string> install = { utf8( env.interpreter ), "-I", "-B", "-X", "utf8", "-m", "pip", "--isolated", "install",
                                      "--disable-pip-version-check", "--no-input", "--no-cache-dir", "--require-hashes",
                                      "--only-binary=:all:" };
  if ( !env.wheelhouse.isEmpty() )
  {
    install.push_back( "--no-index" );
    install.push_back( "--find-links" );
    install.push_back( utf8( env.wheelhouse ) );
  }
  install.push_back( "-r" );
  install.push_back( utf8( env.lockPath ) );
  if ( !run( env, std::move( install ), cancelled, deadline, status ) )
    return status;
  status = probeEnvironment( env, cancelled, deadline, false );
  if ( !status["ready"].asBool() || stopped( status, cancelled, deadline ) )
    return status;
  if ( !saveFile( env.markerPath, jsonBytes( status ) ) )
    return failure( status, "python.marker_write_failed", "Cannot record the completed Python environment." );
  return status;
}

} // namespace sicnu::operators::python
