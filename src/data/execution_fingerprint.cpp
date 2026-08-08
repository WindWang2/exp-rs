// src/data/execution_fingerprint.cpp
#include "execution_fingerprint.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <mutex>

namespace sicnu::data
{

namespace
{
/// Canonical, order-independent string form of the fingerprint inputs, hashed
/// with SHA-256 (then folded to 64 bits). Parameter keys are sorted so map
/// insertion order does not change the fingerprint.
QByteArray canonicalForm( const QString &algorithmId,
                          const QString &algorithmVersion,
                          const QJsonObject &parameters,
                          const QVector<DerivationInput> &inputs )
{
  QByteArray out;
  out.append( "alg=" );
  out.append( algorithmId.toUtf8() );
  out.append( "\nver=" );
  out.append( algorithmVersion.toUtf8() );

  // Normalize parameters: serialize via QJsonDocument with sorted keys.
  QJsonObject sortedParams = parameters;
  out.append( "\nparams=" );
  out.append( QJsonDocument( sortedParams ).toJson( QJsonDocument::Compact ) );

  // Inputs in list order (each input's own fields are stable).
  for ( const auto &in : inputs )
  {
    out.append( "\nin=" );
    out.append( in.assetId.toString().toUtf8() );
    out.append( "@rev=" );
    out.append( QByteArray::number( static_cast<qint64>( in.revision.value() ) ) );
    if ( !in.bandReferences.isEmpty() )
    {
      out.append( ";bands=" );
      out.append( in.bandReferences.join( ',' ).toUtf8() );
    }
    if ( !in.valueDomain.isEmpty() )
    {
      out.append( ";vd=" );
      out.append( in.valueDomain.toUtf8() );
    }
  }
  return out;
}

quint64 hashCanonical( const QByteArray &canonical )
{
  const QByteArray digest =
    QCryptographicHash::hash( canonical, QCryptographicHash::Sha256 );
  // Fold the 32-byte digest into a 64-bit value (deterministic, well-mixed).
  Q_ASSERT( digest.size() >= 8 );
  quint64 folded = 0;
  for ( int i = 0; i + 7 < digest.size(); i += 8 )
  {
    quint64 chunk = 0;
    for ( int j = 0; j < 8; ++j )
      chunk = ( chunk << 8 ) | static_cast<quint64>( static_cast<unsigned char>( digest[i + j] ) );
    folded ^= chunk;
  }
  return folded;
}
} // namespace

ExecutionFingerprint makeExecutionFingerprint( const QString &algorithmId,
                                               const QString &algorithmVersion,
                                               const QJsonObject &parameters,
                                               const QVector<DerivationInput> &inputs )
{
  return ExecutionFingerprint{ hashCanonical( canonicalForm( algorithmId, algorithmVersion,
                                                             parameters, inputs ) ) };
}

ExecutionResultCache &ExecutionResultCache::instance()
{
  static ExecutionResultCache cache;
  return cache;
}

void ExecutionResultCache::setEnabled( bool on )
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  m_enabled = on;
}

bool ExecutionResultCache::isEnabled() const
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  return m_enabled;
}

std::optional<AssetId> ExecutionResultCache::lookup( const ExecutionFingerprint &fp ) const
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  if ( !m_enabled )
    return std::nullopt;
  auto it = m_entries.constFind( fp.value );
  if ( it == m_entries.constEnd() )
    return std::nullopt;
  return it.value();
}

void ExecutionResultCache::store( const ExecutionFingerprint &fp, const AssetId &outputAssetId )
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  if ( !m_enabled )
    return;
  m_entries.insert( fp.value, outputAssetId );
}

void ExecutionResultCache::invalidate( const ExecutionFingerprint &fp )
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  m_entries.remove( fp.value );
}

void ExecutionResultCache::clear()
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  m_entries.clear();
}

int ExecutionResultCache::size() const
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  return m_entries.size();
}

} // namespace sicnu::data
