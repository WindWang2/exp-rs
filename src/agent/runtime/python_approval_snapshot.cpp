#include "python_approval_snapshot.h"

#include "exprs/path_policy.h"
#include "operators/runtime/python_environment.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>

namespace sicnu::agent::runtime
{
namespace
{
constexpr qsizetype kMaxScriptBytes = 128 * 1024;

QString sha256( const QByteArray &bytes )
{
  return QString::fromLatin1( QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() );
}

bool digest( const QString &value )
{
  if ( value.size() != 64 )
    return false;
  for ( const QChar c : value )
    if ( !( c >= QLatin1Char( '0' ) && c <= QLatin1Char( '9' ) )
         && !( c >= QLatin1Char( 'a' ) && c <= QLatin1Char( 'f' ) ) )
      return false;
  return true;
}

QString statusString( const Json::Value &status, const char *key )
{
  if ( !status[key].isString() )
    return {};
  const std::string &value = status[key].asString();
  return QString::fromUtf8( value.data(), static_cast<qsizetype>( value.size() ) );
}

QString captureScript( const QJsonObject &arguments, PythonApprovalSnapshot &snapshot )
{
  if ( arguments.contains( QStringLiteral( "script" ) )
       == arguments.contains( QStringLiteral( "script_path" ) ) )
    return QStringLiteral( "python.approval_script_invalid" );

  QByteArray bytes;
  if ( arguments.contains( QStringLiteral( "script_path" ) ) )
  {
    const QJsonValue pathValue = arguments.value( QStringLiteral( "script_path" ) );
    if ( !pathValue.isString() )
      return QStringLiteral( "python.approval_script_path_invalid" );
    const QString path = pathValue.toString();
    if ( path.isEmpty() || path.contains( QChar::Null ) || !QDir::isAbsolutePath( path )
         || QFileInfo( path ).suffix().compare( QStringLiteral( "py" ), Qt::CaseInsensitive ) != 0 )
      return QStringLiteral( "python.approval_script_path_invalid" );

    // Match the native script operator's workspace gate before opening a file.
    std::string workspace;
    if ( !qEnvironmentVariable( "SICNU_MCP_WORKSPACE" ).isEmpty() )
    {
      workspace = exprs::PathPolicy::workspaceRoot();
      if ( workspace.empty()
           || !exprs::PathPolicy::resolvesInsideRoot( workspace, path.toUtf8().toStdString() ) )
        return QStringLiteral( "python.approval_script_path_refused" );
    }
    const QFileInfo info( path );
    const QString canonical = info.canonicalFilePath();
    if ( !info.isFile() || !info.isReadable() || canonical.isEmpty()
         || QFileInfo( canonical ).suffix().compare( QStringLiteral( "py" ), Qt::CaseInsensitive ) != 0 )
      return QStringLiteral( "python.approval_script_unreadable" );
    if ( !workspace.empty()
         && !exprs::PathPolicy::resolvesInsideRoot( workspace, canonical.toUtf8().toStdString() ) )
      return QStringLiteral( "python.approval_script_path_refused" );
    QFile file( canonical );
    if ( !file.open( QIODevice::ReadOnly ) )
      return QStringLiteral( "python.approval_script_unreadable" );
    bytes = file.read( kMaxScriptBytes + 1 );
    if ( file.error() != QFileDevice::NoError )
      return QStringLiteral( "python.approval_script_unreadable" );
    snapshot.identity.insert( QStringLiteral( "script_path" ), path );
    snapshot.identity.insert( QStringLiteral( "script_canonical_path" ), canonical );
  }
  else
  {
    const QJsonValue source = arguments.value( QStringLiteral( "script" ) );
    if ( !source.isString() )
      return QStringLiteral( "python.approval_script_invalid" );
    bytes = source.toString().toUtf8();
  }
  if ( bytes.isEmpty() || bytes.size() > kMaxScriptBytes )
    return QStringLiteral( "python.approval_script_size_invalid" );
  // Stateless conversion rejects an incomplete trailing UTF-8 sequence too.
  // Preserve a UTF-8 BOM so the approved bytes equal the worker's inline bytes.
  QStringDecoder decoder( QStringDecoder::Utf8,
    QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInitialBom );
  const QString source = decoder.decode( bytes );
  if ( decoder.hasError() || source.toUtf8() != bytes
       || ( arguments.contains( QStringLiteral( "script" ) )
            && source != arguments.value( QStringLiteral( "script" ) ).toString() ) )
    return QStringLiteral( "python.approval_script_utf8_invalid" );
  snapshot.identity.insert( QStringLiteral( "script_sha256" ), sha256( bytes ) );
  snapshot.args.remove( QStringLiteral( "script_path" ) );
  snapshot.args.insert( QStringLiteral( "script" ), source );
  return {};
}
} // namespace

PythonApprovalSnapshot capturePythonApprovalSnapshot(
  const QString &canonicalToolId, const QJsonObject &arguments,
  const std::function<bool()> &cancelled )
{
  PythonApprovalSnapshot snapshot;
  snapshot.canonicalToolId = canonicalToolId;
  snapshot.args = arguments;
  try
  {
    if ( cancelled && cancelled() )
    {
      snapshot.error = QStringLiteral( "python.approval_cancelled" );
      return snapshot;
    }
    const bool script = canonicalToolId == QLatin1String( "python:run_script" );
    if ( !script && canonicalToolId != QLatin1String( "python:environment" ) )
    {
      snapshot.error = QStringLiteral( "python.approval_tool_invalid" );
      return snapshot;
    }
    const QString profile = arguments.value( QStringLiteral( "profile_id" ) )
                              .toString( QStringLiteral( "geospatial-v1" ) );
    if ( profile != QLatin1String( "geospatial-v1" )
         || ( arguments.contains( QStringLiteral( "profile_id" ) )
              && !arguments.value( QStringLiteral( "profile_id" ) ).isString() ) )
    {
      snapshot.error = QStringLiteral( "python.approval_profile_invalid" );
      return snapshot;
    }
    snapshot.args.insert( QStringLiteral( "profile_id" ), profile );
    bool preparing = false;
    if ( script )
    {
      snapshot.error = captureScript( arguments, snapshot );
      if ( !snapshot.error.isEmpty() )
        return snapshot;
    }
    else
    {
      const QString action = arguments.value( QStringLiteral( "action" ) )
                               .toString( QStringLiteral( "probe" ) );
      if ( ( action != QLatin1String( "probe" ) && action != QLatin1String( "prepare" ) )
           || ( arguments.contains( QStringLiteral( "action" ) )
                && !arguments.value( QStringLiteral( "action" ) ).isString() ) )
      {
        snapshot.error = QStringLiteral( "python.approval_action_invalid" );
        return snapshot;
      }
      snapshot.args.insert( QStringLiteral( "action" ), action );
      preparing = action == QLatin1String( "prepare" );
    }

    const Json::Value status = operators::python::PythonEnvironmentManager().probe(
      profile.toUtf8().toStdString(), cancelled );
    if ( ( cancelled && cancelled() ) || status["cancelled"].asBool() )
      snapshot.error = QStringLiteral( "python.approval_cancelled" );
    else if ( status["timed_out"].asBool() )
      snapshot.error = QStringLiteral( "python.approval_probe_timeout" );
    else if ( !status["ready"].asBool()
              && !( preparing && statusString( status, "error_code" ) == QLatin1String( "python.environment_missing" ) ) )
      snapshot.error = QStringLiteral( "python.approval_environment_not_ready" );
    if ( !snapshot.error.isEmpty() )
      return snapshot;

    snapshot.identity.insert( QStringLiteral( "profile_id" ), profile );
    for ( const char *key : { "lock_sha256", "bootstrap_sha256", "runtime_identity_sha256" } )
    {
      const QString value = statusString( status, key );
      if ( !digest( value ) )
      {
        snapshot.error = QStringLiteral( "python.approval_identity_invalid" );
        return snapshot;
      }
      snapshot.identity.insert( QString::fromLatin1( key ), value );
    }
    const QString manifest = statusString( status, "runtime_manifest_sha256" );
    const QString environment = statusString( status, "environment_sha256" );
    const QString directory = statusString( status, "environment_directory" );
    if ( ( !manifest.isEmpty() && !digest( manifest ) )
         || ( status["ready"].asBool() ? !digest( environment ) : !environment.isEmpty() )
         || directory.isEmpty() )
    {
      snapshot.error = QStringLiteral( "python.approval_identity_invalid" );
      return snapshot;
    }
    snapshot.identity.insert( QStringLiteral( "runtime_manifest_sha256" ), manifest );
    snapshot.identity.insert( QStringLiteral( "environment_sha256" ), environment );
    snapshot.identity.insert( QStringLiteral( "environment_location_sha256" ), sha256( directory.toUtf8() ) );
    if ( script )
      snapshot.args.insert( QStringLiteral( "expected_environment_sha256" ), environment );
    else
    {
      snapshot.args.insert( QStringLiteral( "expected_runtime_identity_sha256" ),
        snapshot.identity.value( QStringLiteral( "runtime_identity_sha256" ) ) );
      snapshot.args.insert( QStringLiteral( "expected_environment_location_sha256" ),
        snapshot.identity.value( QStringLiteral( "environment_location_sha256" ) ) );
    }
  }
  catch ( ... )
  {
    // Probe diagnostics may contain provider output or local profile paths.
    snapshot.error = QStringLiteral( "python.approval_probe_failed" );
  }
  return snapshot;
}

QString recheckPythonApprovalSnapshot( const PythonApprovalSnapshot &snapshot,
                                       const std::function<bool()> &cancelled )
{
  if ( !snapshot.error.isEmpty() || snapshot.identity.isEmpty() )
    return QStringLiteral( "python.approval_snapshot_invalid" );
  QJsonObject arguments = snapshot.args;
  if ( snapshot.canonicalToolId == QLatin1String( "python:run_script" ) )
  {
    const QJsonValue source = arguments.value( QStringLiteral( "script" ) );
    if ( !source.isString()
         || sha256( source.toString().toUtf8() ) != snapshot.identity.value( QStringLiteral( "script_sha256" ) ).toString()
         || arguments.value( QStringLiteral( "expected_environment_sha256" ) )
              != snapshot.identity.value( QStringLiteral( "environment_sha256" ) ) )
      return QStringLiteral( "python.approval_identity_changed" );
    if ( snapshot.identity.contains( QStringLiteral( "script_path" ) ) )
    {
      arguments.remove( QStringLiteral( "script" ) );
      arguments.insert( QStringLiteral( "script_path" ), snapshot.identity.value( QStringLiteral( "script_path" ) ) );
    }
  }
  else if ( arguments.value( QStringLiteral( "expected_runtime_identity_sha256" ) )
              != snapshot.identity.value( QStringLiteral( "runtime_identity_sha256" ) )
            || arguments.value( QStringLiteral( "expected_environment_location_sha256" ) )
              != snapshot.identity.value( QStringLiteral( "environment_location_sha256" ) ) )
    return QStringLiteral( "python.approval_identity_changed" );

  const PythonApprovalSnapshot current = capturePythonApprovalSnapshot(
    snapshot.canonicalToolId, arguments, cancelled );
  if ( !current.error.isEmpty() )
    return current.error;
  return current.identity == snapshot.identity ? QString()
                                               : QStringLiteral( "python.approval_identity_changed" );
}

} // namespace sicnu::agent::runtime
