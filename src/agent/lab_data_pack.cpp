// src/agent/lab_data_pack.cpp — sicnu.lab-pack/1 loader + verifier.
#include "lab_data_pack.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSet>

#include <algorithm>
#include <memory>
#include <utility>

namespace sicnu::agent {

namespace {

constexpr const char *kSchemaId = "sicnu.lab-pack/1";

bool isValidSha256Hex( const QString &s )
{
  if ( s.size() != 64 )
    return false;
  for ( const QChar c : s )
  {
    if ( !( ( c >= QLatin1Char( '0' ) && c <= QLatin1Char( '9' ) ) ||
            ( c >= QLatin1Char( 'a' ) && c <= QLatin1Char( 'f' ) ) ) )
      return false;
  }
  return true;
}

QString requireString( const Json::Value &object, const char *key, QString *error )
{
  const Json::Value value = object[key];
  if ( !value.isString() || value.asString().empty() )
  {
    *error = QStringLiteral( "missing or non-string field \"%1\"" ).arg( key );
    return QString();
  }
  return QString::fromStdString( value.asString() );
}

} // namespace

LabPackProvenance labPackProvenanceFromString( const QString &provenance )
{
  if ( provenance == QLatin1String( "committed-fixture" ) )
    return LabPackProvenance::CommittedFixture;
  if ( provenance == QLatin1String( "generated-tmp" ) )
    return LabPackProvenance::GeneratedTmp;
  return LabPackProvenance::GeneratedSamples;
}

QString labPackProvenanceToString( LabPackProvenance provenance )
{
  switch ( provenance )
  {
    case LabPackProvenance::CommittedFixture:
      return QStringLiteral( "committed-fixture" );
    case LabPackProvenance::GeneratedTmp:
      return QStringLiteral( "generated-tmp" );
    case LabPackProvenance::GeneratedSamples:
      return QStringLiteral( "generated-samples" );
  }
  return QStringLiteral( "generated-samples" );
}

Json::Value LabPackInput::toJson() const
{
  Json::Value json( Json::objectValue );
  json["path"] = path.toStdString();
  json["role"] = role.toStdString();
  json["provenance"] = labPackProvenanceToString( provenance ).toStdString();
  json["sha256"] = sha256.isEmpty() ? Json::Value( Json::nullValue )
                                    : Json::Value( sha256.toStdString() );
  json["declared_bytes"] = static_cast<Json::Int64>( declaredBytes );
  if ( !sensorTruth.isEmpty() )
    json["sensor_truth"] = sensorTruth.toStdString();
  if ( !notes.isEmpty() )
    json["notes"] = notes.toStdString();
  return json;
}

LabDataPackResult LabDataPack::load( const QString &path )
{
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) )
    return LabDataPackResult::fail( QStringLiteral( "lab.pack_unreadable" ),
                         QStringLiteral( "cannot open %1" ).arg( path ) );

  Json::Value root;
  Json::CharReaderBuilder builder;
  const QString errorsContext = path;
  const std::string text = file.readAll().toStdString();
  std::string parseErrors;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  if ( !reader->parse( text.data(), text.data() + text.size(), &root, &parseErrors ) )
    return LabDataPackResult::fail( QStringLiteral( "lab.pack_schema" ),
                         QStringLiteral( "%1: %2" ).arg( errorsContext,
                                                         QString::fromStdString( parseErrors ) ) );

  if ( !root.isObject() || !root["schema_version"].isString()
       || root["schema_version"].asString() != kSchemaId )
    return LabDataPackResult::fail( QStringLiteral( "lab.pack_schema" ),
                         QStringLiteral( "%1: schema_version must be the string \"%2\"" )
                           .arg( errorsContext, QString::fromLatin1( kSchemaId ) ) );

  LabDataPack pack;
  QString error;
  pack.labId = requireString( root, "lab_id", &error );
  if ( !error.isEmpty() )
    return LabDataPackResult::fail( QStringLiteral( "lab.pack_field" ),
                         QStringLiteral( "%1: %2" ).arg( errorsContext, error ) );
  pack.packVersion = requireString( root, "pack_version", &error );
  if ( !error.isEmpty() )
    return LabDataPackResult::fail( QStringLiteral( "lab.pack_field" ),
                         QStringLiteral( "%1: %2" ).arg( errorsContext, error ) );
  pack.license = requireString( root, "license", &error );
  if ( !error.isEmpty() )
    return LabDataPackResult::fail( QStringLiteral( "lab.pack_field" ),
                         QStringLiteral( "%1: %2" ).arg( errorsContext, error ) );

  if ( root.isMember( "generator" ) )
  {
    if ( !root["generator"].isString() )
      return LabDataPackResult::fail( QStringLiteral( "lab.pack_field" ),
                           QStringLiteral( "%1: generator must be a string" ).arg( errorsContext ) );
    pack.generator = QString::fromStdString( root["generator"].asString() );
  }
  if ( root.isMember( "notes" ) )
  {
    if ( !root["notes"].isString() )
      return LabDataPackResult::fail( QStringLiteral( "lab.pack_field" ),
                           QStringLiteral( "%1: notes must be a string" ).arg( errorsContext ) );
    pack.notes = QString::fromStdString( root["notes"].asString() );
  }
  if ( root.isMember( "declared_offline_bytes" ) && root["declared_offline_bytes"].isInt64() )
    pack.declaredOfflineBytes = root["declared_offline_bytes"].asInt64();

  const Json::Value inputs = root["inputs"];
  if ( !inputs.isArray() || inputs.empty() )
    return LabDataPackResult::fail( QStringLiteral( "lab.pack_field" ),
                         QStringLiteral( "%1: \"inputs\" must be a non-empty array" )
                           .arg( errorsContext ) );

  QSet<QString> seenPaths;
  for ( const Json::Value &entry : inputs )
  {
    if ( !entry.isObject() )
      return LabDataPackResult::fail( QStringLiteral( "lab.pack_input" ),
                           QStringLiteral( "%1: input entries must be objects" )
                             .arg( errorsContext ) );

    LabPackInput input;
    input.path = requireString( entry, "path", &error );
    if ( !error.isEmpty() )
      return LabDataPackResult::fail( QStringLiteral( "lab.pack_input" ),
                           QStringLiteral( "%1: %2" ).arg( errorsContext, error ) );
    if ( input.path.contains( QLatin1Char( '\\' ) ) )
      return LabDataPackResult::fail( QStringLiteral( "lab.pack_input" ),
                           QStringLiteral( "%1: input paths must use forward slashes (%2)" )
                             .arg( errorsContext, input.path ) );
    if ( seenPaths.contains( input.path ) )
      return LabDataPackResult::fail( QStringLiteral( "lab.pack_input" ),
                           QStringLiteral( "%1: duplicate input path %2" )
                             .arg( errorsContext, input.path ) );
    seenPaths.insert( input.path );

    input.role = requireString( entry, "role", &error );
    if ( !error.isEmpty() )
      return LabDataPackResult::fail( QStringLiteral( "lab.pack_input" ),
                           QStringLiteral( "%1: %2" ).arg( errorsContext, error ) );

    if ( !entry["provenance"].isString() )
      return LabDataPackResult::fail( QStringLiteral( "lab.pack_input" ),
                           QStringLiteral( "%1: provenance must be a string (%2)" )
                             .arg( errorsContext, input.path ) );
    const QString provenance = QString::fromStdString( entry["provenance"].asString() );
    input.provenance = labPackProvenanceFromString( provenance );
    if ( labPackProvenanceToString( input.provenance ) != provenance )
      return LabDataPackResult::fail( QStringLiteral( "lab.pack_input" ),
                           QStringLiteral( "%1: unknown provenance \"%2\" (%3)" )
                             .arg( errorsContext, provenance, input.path ) );

    if ( entry.isMember( "sha256" ) && entry["sha256"].isString() )
      input.sha256 = QString::fromStdString( entry["sha256"].asString() );
    if ( !input.sha256.isEmpty() && !isValidSha256Hex( input.sha256 ) )
      return LabDataPackResult::fail( QStringLiteral( "lab.pack_input" ),
                           QStringLiteral( "%1: sha256 must be 64 lowercase hex chars (%2)" )
                             .arg( errorsContext, input.path ) );
    if ( input.provenance == LabPackProvenance::CommittedFixture && input.sha256.isEmpty() )
      return LabDataPackResult::fail( QStringLiteral( "lab.pack_input" ),
                           QStringLiteral( "%1: committed-fixture input requires sha256 (%2)" )
                             .arg( errorsContext, input.path ) );

    if ( entry.isMember( "bytes" ) && entry["bytes"].isInt64() )
      input.declaredBytes = entry["bytes"].asInt64();
    if ( input.provenance == LabPackProvenance::CommittedFixture && input.declaredBytes < 0 )
      return LabDataPackResult::fail( QStringLiteral( "lab.pack_input" ),
                           QStringLiteral( "%1: committed-fixture input requires bytes (%2)" )
                             .arg( errorsContext, input.path ) );

    if ( entry.isMember( "sensor_truth" ) && entry["sensor_truth"].isString() )
      input.sensorTruth = QString::fromStdString( entry["sensor_truth"].asString() );
    if ( entry.isMember( "notes" ) && entry["notes"].isString() )
      input.notes = QString::fromStdString( entry["notes"].asString() );

    pack.inputs.append( input );
  }

  return LabDataPackResult::ok( std::move( pack ) );
}

Json::Value LabPackVerification::toJson() const
{
  Json::Value json( Json::objectValue );
  json["overall"] = overall.toStdString();
  json["verified_bytes"] = static_cast<Json::Int64>( verifiedBytes );
  Json::Value issueArray( Json::arrayValue );
  for ( const Json::Value &issue : issues )
    issueArray.append( issue );
  json["issues"] = issueArray;
  return json;
}

namespace {

/// Streaming sha256 in bounded chunks. Returns an empty string when the file
/// cannot be opened (caller reports the typed issue).
QString fileSha256( const QString &path, qint64 *bytesOut )
{
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) )
    return QString();
  QCryptographicHash hash( QCryptographicHash::Sha256 );
  qint64 total = 0;
  char buffer[65536];
  while ( !file.atEnd() )
  {
    const qint64 read = file.read( buffer, sizeof( buffer ) );
    if ( read < 0 )
      return QString();
    hash.addData( buffer, static_cast<int>( read ) );
    total += read;
  }
  *bytesOut = total;
  return QString::fromLatin1( hash.result().toHex() );
}

} // namespace

LabPackVerification LabPackVerifier::verify( const LabDataPack &pack, const QString &root )
{
  LabPackVerification verification;
  bool failed = false;
  bool degraded = false;

  const QString canonicalRoot = QFileInfo( root ).canonicalFilePath();
  for ( const LabPackInput &input : pack.inputs )
  {
    // #1186: confine input paths under the pack root — absolute / ../ paths
    // used to yield an existence+size+sha256 oracle outside the pack.
    if ( QFileInfo( input.path ).isAbsolute()
         || input.path.split( QLatin1Char( '/' ) ).contains( QStringLiteral( ".." ) ) )
    {
      Json::Value issue( Json::objectValue );
      issue["code"] = "lab.pack_input_outside_root";
      issue["path"] = input.path.toStdString();
      issue["detail"] = "input path must be pack-relative without '..'";
      verification.issues.append( issue );
      failed = true;
      continue;
    }
    const QString absolute = QDir( root ).filePath( input.path );
    const QFileInfo info( absolute );
    if ( !canonicalRoot.isEmpty() )
    {
      const QString canonical = info.canonicalFilePath();
      if ( !canonical.isEmpty()
           && !canonical.startsWith( canonicalRoot + QLatin1Char( '/' ) )
           && canonical != canonicalRoot )
      {
        Json::Value issue( Json::objectValue );
        issue["code"] = "lab.pack_input_outside_root";
        issue["path"] = input.path.toStdString();
        issue["detail"] = "resolved input escapes the pack root";
        verification.issues.append( issue );
        failed = true;
        continue;
      }
    }

    if ( !info.isFile() )
    {
      Json::Value issue( Json::objectValue );
      issue["code"] = "lab.pack_input_missing";
      issue["path"] = input.path.toStdString();
      issue["provenance"] = labPackProvenanceToString( input.provenance ).toStdString();
      if ( input.provenance == LabPackProvenance::CommittedFixture )
      {
        issue["detail"] = "committed fixture is absent from this deployment";
        failed = true;
      }
      else
      {
        issue["detail"] = "regenerable input absent — run the pack generator";
        degraded = true;
      }
      verification.issues.append( issue );
      continue;
    }

    if ( input.provenance == LabPackProvenance::CommittedFixture )
    {
      qint64 actualBytes = -1;
      const QString actualHash = fileSha256( absolute, &actualBytes );
      if ( actualHash.isEmpty() )
      {
        Json::Value issue( Json::objectValue );
        issue["code"] = "lab.pack_unreadable";
        issue["path"] = input.path.toStdString();
        issue["detail"] = "file exists but cannot be read";
        verification.issues.append( issue );
        failed = true;
        continue;
      }
      if ( actualBytes != input.declaredBytes )
      {
        Json::Value issue( Json::objectValue );
        issue["code"] = "lab.pack_size_mismatch";
        issue["path"] = input.path.toStdString();
        issue["declared_bytes"] = static_cast<Json::Int64>( input.declaredBytes );
        issue["actual_bytes"] = static_cast<Json::Int64>( actualBytes );
        verification.issues.append( issue );
        failed = true;
        continue;
      }
      if ( actualHash != input.sha256 )
      {
        Json::Value issue( Json::objectValue );
        issue["code"] = "lab.pack_checksum_mismatch";
        issue["path"] = input.path.toStdString();
        issue["declared_sha256"] = input.sha256.toStdString();
        issue["actual_sha256"] = actualHash.toStdString();
        verification.issues.append( issue );
        failed = true;
        continue;
      }
      verification.verifiedBytes += actualBytes;
      continue;
    }

    // Regenerable inputs: presence verified; declared size is informative.
    const qint64 actualBytes = info.size();
    if ( input.declaredBytes >= 0 && actualBytes != input.declaredBytes )
    {
      Json::Value issue( Json::objectValue );
      issue["code"] = "lab.pack_regenerated_size_drift";
      issue["path"] = input.path.toStdString();
      issue["declared_bytes"] = static_cast<Json::Int64>( input.declaredBytes );
      issue["actual_bytes"] = static_cast<Json::Int64>( actualBytes );
      issue["detail"] = "informative: regenerated by a different toolchain";
      verification.issues.append( issue );
      degraded = true;
    }
    verification.verifiedBytes += actualBytes;
  }

  verification.overall = failed ? QStringLiteral( "failed" )
                         : degraded ? QStringLiteral( "degraded" )
                                    : QStringLiteral( "verified" );
  return verification;
}

QVector<LabDataPack> LabPackVerifier::loadPacksFromDir(
  const QString &dir, QVector<LabDataPackResult> *problems )
{
  QVector<LabDataPack> packs;
  const QFileInfoList entries =
    QDir( dir ).entryInfoList( QStringList() << QStringLiteral( "*.pack.json" ),
                               QDir::Files, QDir::Name | QDir::LocaleAware );
  for ( const QFileInfo &entry : entries )
  {
    LabDataPackResult result = LabDataPack::load( entry.absoluteFilePath() );
    if ( result )
      packs.append( result.pack() );
    else if ( problems )
      problems->append( std::move( result ) );
  }
  return packs;
}

} // namespace sicnu::agent
