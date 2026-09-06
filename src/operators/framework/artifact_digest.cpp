// src/operators/framework/artifact_digest.cpp
#include "artifact_digest.h"

#include <QCryptographicHash>
#include <QFile>
#include <QString>
#include <QByteArrayView>

#include <cctype>
#include <vector>

namespace sicnu::operators {

std::string artifactSha256Hex( const std::string &path )
{
  QFile file( QString::fromStdString( path ) );
  if ( !file.open( QIODevice::ReadOnly ) )
    return std::string();
  QCryptographicHash hash( QCryptographicHash::Sha256 );
  std::vector<char> buffer( 1024 * 1024 );
  while ( true )
  {
    const qint64 read = file.read( buffer.data(), static_cast<qint64>( buffer.size() ) );
    if ( read < 0 )
      return std::string();
    if ( read == 0 )
      break;
    hash.addData( QByteArrayView( buffer.data(), static_cast<qsizetype>( read ) ) );
  }
  return QString::fromLatin1( hash.result().toHex() ).toStdString();
}

bool isSha256Hex( const std::string &digest )
{
  if ( digest.size() != 64 )
    return false;
  for ( char c : digest )
  {
    if ( !std::isxdigit( static_cast<unsigned char>( c ) ) )
      return false;
  }
  return true;
}

} // namespace sicnu::operators
