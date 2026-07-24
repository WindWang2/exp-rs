#include "processing_asset_resolver.h"

#include <QtGlobal>

#include "data_manager.h"

namespace sicnu::data
{

ResolvedAsset::ResolvedAsset( ResolvedAssetSnapshot snapshot, AssetLease lease )
  : m_snapshot( std::move( snapshot ) )
  , m_lease( std::move( lease ) )
{
}

bool ResolvedAsset::isValid() const
{
  return m_lease.has_value() && m_lease->isValid();
}

ProcessingAssetResolver::ProcessingAssetResolver( DataManager *dataManager )
  : m_dataManager( dataManager )
{
  // A resolver without a Data Manager is a programming error, not a runtime
  // condition to defend against.
  Q_ASSERT( m_dataManager != nullptr );
}

Result<ResolvedAsset> ProcessingAssetResolver::resolve(
  const AssetRef &asset,
  const QString &purpose,
  AssetCapability requiredCapability ) const
{
  const std::optional<AssetSnapshot> snapshot = m_dataManager->asset( asset.id );
  if ( !snapshot )
  {
    return Result<ResolvedAsset>::failure(
      Diagnostic{ QStringLiteral( "asset.unknown" ),
                  QStringLiteral( "No registered asset matches the requested id" ),
                  DiagnosticSeverity::Error } );
  }

  // The input must be in a resolvable state to be read by an algorithm. Missing,
  // offline, error, and still-resolving assets are rejected before execution.
  if ( snapshot->state() != AssetState::Ready )
  {
    return Result<ResolvedAsset>::failure(
      Diagnostic{ QStringLiteral( "asset.not_resolvable" ),
                  QStringLiteral( "The asset is not ready to be read as an algorithm input" ),
                  DiagnosticSeverity::Error } );
  }

  if ( requiredCapability != AssetCapability::None &&
       !snapshot->capabilities().testFlag( requiredCapability ) )
  {
    return Result<ResolvedAsset>::failure(
      Diagnostic{ QStringLiteral( "asset.capability_missing" ),
                  QStringLiteral( "The asset does not declare the capability the algorithm requires" ),
                  DiagnosticSeverity::Error } );
  }

  // Acquire the Task lease first: it pins the asset and atomically validates the
  // expected revision. Reading the snapshot only afterwards guarantees the
  // returned snapshot is exactly the state the lease validated (no relocation
  // can slip between validation and the lease).
  Result<AssetLease> lease = m_dataManager->acquire(
    asset, AssetUse{ LeaseKind::Task, purpose } );
  if ( !lease )
    return Result<ResolvedAsset>::failure( lease.diagnostics() );

  const std::optional<AssetSnapshot> pinned = m_dataManager->asset( asset.id );
  if ( !pinned )
  {
    // The asset vanished between validation and the lease; the lease releases on
    // destruction, so there is nothing to clean up beyond reporting the failure.
    return Result<ResolvedAsset>::failure(
      Diagnostic{ QStringLiteral( "asset.unknown" ),
                  QStringLiteral( "The asset was unloaded while it was being resolved" ),
                  DiagnosticSeverity::Error } );
  }

  ResolvedAssetSnapshot resolvedSnapshot{ pinned->id(),
                                          pinned->revision(),
                                          pinned->kind(),
                                          pinned->capabilities(),
                                          pinned->source() };
  return Result<ResolvedAsset>::success(
    ResolvedAsset{ std::move( resolvedSnapshot ), lease.take() } );
}

} // namespace sicnu::data

