/***************************************************************************
  rs_classifier_labels_sidecar.h — fail-closed {model, .labels.json} pair (#1175).
  A NEW-format save always stamps `<model>.labels.required`. Load fails closed
  when the stamp is present but the sidecar is missing/malformed; absence of
  both files remains the legacy-tolerant path.
 ***************************************************************************/
#pragma once

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>

namespace sicnu::classification::labels_sidecar
{

inline QString sidecarPath( const QString &modelPath )
{
  return modelPath + QStringLiteral( ".labels.json" );
}

inline QString requiredStampPath( const QString &modelPath )
{
  return modelPath + QStringLiteral( ".labels.required" );
}

/// Writes labels JSON + required stamp. On any failure removes both sidelings
/// (caller is responsible for rolling back the model file).
inline bool writePair( const QString &modelPath, const QJsonDocument &doc )
{
  const QString side = sidecarPath( modelPath );
  const QString stamp = requiredStampPath( modelPath );
  QFile::remove( side );
  QFile::remove( stamp );
  {
    QFile f( side );
    if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
      return false;
    const QByteArray bytes = doc.toJson( QJsonDocument::Compact );
    if ( f.write( bytes ) != bytes.size() || !f.flush() )
    {
      f.close();
      QFile::remove( side );
      return false;
    }
  }
  {
    QFile s( stamp );
    if ( !s.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
      QFile::remove( side );
      return false;
    }
    if ( s.write( "1", 1 ) != 1 || !s.flush() )
    {
      s.close();
      QFile::remove( side );
      QFile::remove( stamp );
      return false;
    }
  }
  return true;
}

enum class LoadStatus
{
  Ok,            ///< sidecar parsed into @p outArray
  LegacyMissing, ///< no stamp and no sidecar — pre-#1175 model
  Failed,        ///< stamp present but sidecar missing/malformed, or I/O error
};

inline LoadStatus loadArray( const QString &modelPath, QJsonArray &outArray )
{
  outArray = QJsonArray();
  const QString side = sidecarPath( modelPath );
  const QString stamp = requiredStampPath( modelPath );
  const bool stamped = QFile::exists( stamp );
  QFile f( side );
  if ( !f.exists() )
    return stamped ? LoadStatus::Failed : LoadStatus::LegacyMissing;
  if ( !f.open( QIODevice::ReadOnly ) )
    return LoadStatus::Failed;
  const QJsonDocument doc = QJsonDocument::fromJson( f.readAll() );
  if ( !doc.isArray() || doc.array().isEmpty() )
    return stamped ? LoadStatus::Failed : LoadStatus::LegacyMissing;
  outArray = doc.array();
  return LoadStatus::Ok;
}

} // namespace sicnu::classification::labels_sidecar
