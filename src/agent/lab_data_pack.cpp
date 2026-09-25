// src/agent/lab_data_pack.cpp — Qt façade delegating to sicnu::labpack.
//
// Zero pack semantics live here: every load/verify decision is made by the
// Qt-free leaf (src/lab_pack). This TU is pure boundary conversion between
// the historical sicnu::agent Qt API and sicnu::labpack std types.
#include "lab_data_pack.h"

#include "lab_pack/lab_pack.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace sicnu::agent {

namespace {

LabPackProvenance fromLeaf( sicnu::labpack::Provenance p )
{
  switch ( p )
  {
    case sicnu::labpack::Provenance::CommittedFixture:
      return LabPackProvenance::CommittedFixture;
    case sicnu::labpack::Provenance::GeneratedTmp:
      return LabPackProvenance::GeneratedTmp;
    case sicnu::labpack::Provenance::GeneratedSamples:
      return LabPackProvenance::GeneratedSamples;
  }
  return LabPackProvenance::GeneratedSamples;
}

sicnu::labpack::Provenance toLeaf( LabPackProvenance p )
{
  switch ( p )
  {
    case LabPackProvenance::CommittedFixture:
      return sicnu::labpack::Provenance::CommittedFixture;
    case LabPackProvenance::GeneratedTmp:
      return sicnu::labpack::Provenance::GeneratedTmp;
    case LabPackProvenance::GeneratedSamples:
      return sicnu::labpack::Provenance::GeneratedSamples;
  }
  return sicnu::labpack::Provenance::GeneratedSamples;
}

QString utf8ToQString( const std::string &utf8 )
{
  return QString::fromUtf8( utf8.data(), static_cast<qsizetype>( utf8.size() ) );
}

std::string qToUtf8( const QString &s )
{
  const QByteArray bytes = s.toUtf8();
  return std::string( bytes.constData(), static_cast<std::size_t>( bytes.size() ) );
}

sicnu::labpack::PackDocument toLeafPack( const LabDataPack &pack )
{
  sicnu::labpack::PackDocument leaf;
  leaf.labId = qToUtf8( pack.labId );
  leaf.packVersion = qToUtf8( pack.packVersion );
  leaf.license = qToUtf8( pack.license );
  leaf.generator = qToUtf8( pack.generator );
  leaf.notes = qToUtf8( pack.notes );
  leaf.declaredOfflineBytes = pack.declaredOfflineBytes;
  leaf.inputs.reserve( static_cast<std::size_t>( pack.inputs.size() ) );
  for ( const LabPackInput &input : pack.inputs )
  {
    sicnu::labpack::PackInput li;
    li.path = qToUtf8( input.path );
    li.role = qToUtf8( input.role );
    li.provenance = toLeaf( input.provenance );
    li.sha256 = qToUtf8( input.sha256 );
    li.declaredBytes = input.declaredBytes;
    li.sensorTruth = qToUtf8( input.sensorTruth );
    li.notes = qToUtf8( input.notes );
    leaf.inputs.push_back( std::move( li ) );
  }
  return leaf;
}

LabDataPack fromLeafPack( const sicnu::labpack::PackDocument &leaf )
{
  LabDataPack pack;
  pack.labId = utf8ToQString( leaf.labId );
  pack.packVersion = utf8ToQString( leaf.packVersion );
  pack.license = utf8ToQString( leaf.license );
  pack.generator = utf8ToQString( leaf.generator );
  pack.notes = utf8ToQString( leaf.notes );
  pack.declaredOfflineBytes = leaf.declaredOfflineBytes;
  pack.inputs.reserve( static_cast<int>( leaf.inputs.size() ) );
  for ( const sicnu::labpack::PackInput &li : leaf.inputs )
  {
    LabPackInput input;
    input.path = utf8ToQString( li.path );
    input.role = utf8ToQString( li.role );
    input.provenance = fromLeaf( li.provenance );
    input.sha256 = utf8ToQString( li.sha256 );
    input.declaredBytes = li.declaredBytes;
    input.sensorTruth = utf8ToQString( li.sensorTruth );
    input.notes = utf8ToQString( li.notes );
    pack.inputs.append( input );
  }
  return pack;
}

LabDataPackResult fromLeafResult( sicnu::labpack::PackLoadResult result )
{
  if ( !result.ok )
    return LabDataPackResult::fail( utf8ToQString( result.errorCode ),
                                    utf8ToQString( result.errorMessage ) );
  return LabDataPackResult::ok( fromLeafPack( result.pack ) );
}

} // namespace

LabPackProvenance labPackProvenanceFromString( const QString &provenance )
{
  return fromLeaf( sicnu::labpack::provenanceFromString( qToUtf8( provenance ) ) );
}

QString labPackProvenanceToString( LabPackProvenance provenance )
{
  return utf8ToQString( sicnu::labpack::provenanceToString( toLeaf( provenance ) ) );
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
  return fromLeafResult(
    sicnu::labpack::PackVerifier::load( sicnu::labpack::pathFromUtf8( qToUtf8( path ) ) ) );
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

LabPackVerification LabPackVerifier::verify( const LabDataPack &pack, const QString &root )
{
  const sicnu::labpack::PackVerification leaf = sicnu::labpack::PackVerifier::verify(
    toLeafPack( pack ), sicnu::labpack::pathFromUtf8( qToUtf8( root ) ) );
  LabPackVerification verification;
  verification.overall = utf8ToQString( leaf.overall );
  verification.verifiedBytes = leaf.verifiedBytes;
  verification.issues.reserve( static_cast<int>( leaf.issues.size() ) );
  for ( const Json::Value &issue : leaf.issues )
    verification.issues.append( issue );
  return verification;
}

QVector<LabDataPack> LabPackVerifier::loadPacksFromDir(
  const QString &dir, QVector<LabDataPackResult> *problems )
{
  std::vector<sicnu::labpack::PackLoadResult> leafProblems;
  const std::vector<sicnu::labpack::PackDocument> leafPacks =
    sicnu::labpack::PackVerifier::loadPacksFromDir(
      sicnu::labpack::pathFromUtf8( qToUtf8( dir ) ),
      problems ? &leafProblems : nullptr );
  QVector<LabDataPack> packs;
  packs.reserve( static_cast<int>( leafPacks.size() ) );
  for ( const sicnu::labpack::PackDocument &leafPack : leafPacks )
    packs.append( fromLeafPack( leafPack ) );
  if ( problems )
  {
    for ( sicnu::labpack::PackLoadResult &result : leafProblems )
      problems->append( fromLeafResult( std::move( result ) ) );
  }
  return packs;
}

} // namespace sicnu::agent
