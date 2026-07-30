#include "collection_import_service.h"

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/source_descriptor.h"

using sicnu::data::AssetId;
using sicnu::data::AssetKind;
using sicnu::data::AssetSnapshot;
using sicnu::data::CollectionCreateRequest;
using sicnu::data::CollectionCreateResult;
using sicnu::data::CollectionId;
using sicnu::data::DataManager;
using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::PersistencePolicy;
using sicnu::data::ProductMetadata;
using sicnu::data::RegisterRequest;
using sicnu::data::RegisterResult;
using sicnu::data::Result;
using sicnu::data::SourceDescriptor;
using sicnu::data::UnloadPlan;
using SatelliteProducts::BandFile;
using SatelliteProducts::ProductInfo;

namespace sicnu
{

namespace
{

/// Builds the normalized ProductMetadata from a DiscoveredProduct's fields.
/// The discoverer's normalized values (spacecraft, processing level, date) are
/// mapped directly; the raw `ProductInfo` is never exposed.
ProductMetadata buildMetadata( const DiscoveredProduct &product )
{
  ProductMetadata metadata;
  metadata.platform = product.spacecraft;
  metadata.sensor = product.spacecraft;
  metadata.processingLevel = product.processingLevel;
  metadata.acquisitionDate = product.acquisitionDate;
  metadata.attributes = product.attributes;
  return metadata;
}

/// A human-readable label for a discovered grid group's display name.
/// Falls back to "Grid: <label>" when the discoverer left it empty.
QString groupDisplayName( const DiscoveredGridGroup &group )
{
  if ( !group.displayName.isEmpty() )
    return group.displayName;
  return QStringLiteral( "Grid: %1" ).arg( group.gridLabel );
}

/// A band-file anchor for the child candidate's source path. Uses the grid
/// group's declared sourcePath when present (the common case); otherwise the
/// first band's path. Empty bands fall back to the grid label.
QString childSourcePath( const DiscoveredGridGroup &group )
{
  if ( !group.sourcePath.isEmpty() )
    return group.sourcePath;
  if ( !group.bands.isEmpty() )
    return group.bands.first().path;
  return group.gridLabel;
}

/// Provider key a registered source uses for `kind`. A single point mapping
/// AssetKind to its registration key, so the kind cannot be dispatched
/// inconsistently across the commit (mirrors OutputCommitter).
QString providerKeyFor( AssetKind kind )
{
  switch ( kind )
  {
    case AssetKind::Raster:
    case AssetKind::VirtualRaster:
    case AssetKind::RemoteMap:
      return QStringLiteral( "gdal" );
    case AssetKind::Vector:
      return QStringLiteral( "ogr" );
  }
  Q_UNREACHABLE();
}

/// Build an error Diagnostic, mirroring OutputCommitter's helper.
Diagnostic diagnostic( const QString &code, const QString &message )
{
  return Diagnostic{ code, message, DiagnosticSeverity::Error };
}

} // namespace

CollectionImportService::CollectionImportService( DataManager *dataManager,
                                                  ProductDiscoverer *discoverer,
                                                  QObject *parent )
  : QObject( parent )
  , m_dataManager( dataManager )
  , m_discoverer( discoverer )
{
  Q_ASSERT( m_dataManager != nullptr );
  Q_ASSERT( m_discoverer != nullptr );
}

Result<ImportPreview> CollectionImportService::probe( const QString &source )
{
  const Result<DiscoveredProduct> discovered = m_discoverer->discover( source );

  if ( !discovered )
  {
    // Forward the discoverer's diagnostics verbatim. Fall back to a single
    // generic diagnostic when the discoverer returned an empty vector
    // (mirrors OutputCommitter's failure-forwarding pattern). The probe
    // registered nothing - the catalog is untouched.
    const QVector<Diagnostic> detail = discovered.diagnostics().isEmpty()
      ? QVector<Diagnostic>{ { QStringLiteral( "import.discover_failed" ),
                                QStringLiteral( "Product discovery failed." ) } }
      : discovered.diagnostics();
    return Result<ImportPreview>::failure( detail );
  }

  const DiscoveredProduct &product = discovered.value();

  ImportPreview preview;
  preview.collectionDisplayName = product.productId;
  preview.metadata = buildMetadata( product );

  for ( const DiscoveredGridGroup &group : product.gridGroups )
  {
    ChildCandidate candidate;
    candidate.kind = AssetKind::Raster;
    candidate.gridLabel = group.gridLabel;
    candidate.displayName = groupDisplayName( group );
    candidate.sourcePath = childSourcePath( group );
    for ( const BandFile &band : group.bands )
    {
      ChildBandInfo info;
      info.name = band.name;
      info.sourcePath = band.path;
      info.sourceBand = band.sourceBand;
      info.wavelengthNm = band.wavelengthNm;
      candidate.bands.append( info );
    }
    preview.children.append( candidate );
  }

  return Result<ImportPreview>::success( std::move( preview ) );
}

CommitImportResult CollectionImportService::commit( const CommitImportRequest &request )
{
  const ImportPreview &preview = request.preview;
  const QVector<int> &selection = request.selectedChildIndices;

  // Validate the selection before any registration so a bad request registers
  // nothing - "all-or-nothing" must be provable from the entry point.
  QSet<int> seen;
  for ( const int index : selection )
  {
    if ( index < 0 || index >= preview.children.size() || seen.contains( index ) )
    {
      return CommitImportResult{ {}, {},
        { diagnostic( QStringLiteral( "import.invalid_selection" ),
                      QStringLiteral( "The selected child index %1 is out of range or "
                                      "duplicated" ).arg( index ) ) } };
    }
    seen.insert( index );
  }

  // Create the collection from the preview's metadata. On failure nothing has
  // been registered yet, so no rollback is needed.
  CollectionCreateRequest collectionRequest;
  collectionRequest.displayName = preview.collectionDisplayName;
  collectionRequest.metadata = preview.metadata;
  const CollectionCreateResult created =
    m_dataManager->createCollection( collectionRequest );
  if ( created.collectionId.isNull() )
  {
    return CommitImportResult{ {}, {}, created.diagnostics };
  }

  CommitImportResult result;
  result.collectionId = created.collectionId;

  // Track only the children THIS commit created (registered fresh). Rollback
  // must unload exactly those - never a reused asset, which another collection
  // or component may own.
  QVector<AssetId> createdChildren;

  // Rolls back the commit: unloads every child this commit created, then
  // removes the collection node non-cascading so any adopted (standalone,
  // pre-existing) children survive as standalone assets with no parent. A
  // reused asset owned by a different collection is never touched.
  auto rollback = [this, &result, &createdChildren]() {
    for ( const AssetId &childId : createdChildren )
    {
      const UnloadPlan plan = m_dataManager->planUnload( childId ).confirmedCascade();
      ( void ) m_dataManager->unload( plan );
    }
    // Non-cascade: the collection node is removed and any remaining (adopted,
    // standalone) children are kept, their parent pointer cleared.
    ( void ) m_dataManager->unloadCollection( result.collectionId, /*cascade=*/false );
    result.collectionId = CollectionId();
    result.childAssetIds.clear();
  };

  for ( const int index : selection )
  {
    const ChildCandidate &child = preview.children[index];

    // Build the source descriptor from the child's first band (or its anchor
    // sourcePath when it carries no bands). The provider resolves kind,
    // structure, and capabilities - exactly as OutputCommitter builds its
    // registration.
    SourceDescriptor source;
    source.providerKey = providerKeyFor( child.kind );
    source.canonicalSource = child.bands.isEmpty() ? child.sourcePath
                                                   : child.bands.first().sourcePath;

    RegisterRequest registration;
    registration.source = source;
    registration.persistence = request.persistence;

    const RegisterResult registered = m_dataManager->registerSource( registration );
    if ( registered.assetId.isNull() )
    {
      // Mid-commit failure: roll back only what this commit created. The
      // catalog never holds a half-imported product.
      rollback();
      const QVector<Diagnostic> detail = registered.diagnostics.isEmpty()
        ? QVector<Diagnostic>{ diagnostic(
            QStringLiteral( "import.child_register_failed" ),
            QStringLiteral( "Registering a child asset failed; the import was "
                            "rolled back" ) ) }
        : registered.diagnostics;
      result.diagnostics = detail;
      return result;
    }

    if ( registered.reusedExisting )
    {
      // A deduped source maps to a pre-existing asset. It is owned by whichever
      // collection already parents it - this commit does not own it.
      const std::optional<AssetSnapshot> snapshot =
        m_dataManager->asset( registered.assetId );
      Q_ASSERT( snapshot.has_value() );
      if ( snapshot->parentCollectionId() == result.collectionId )
      {
        // Same commit, earlier selection of the same source: already added.
        // Record the id once more per selection; per-child-source dedup.
        result.childAssetIds.append( registered.assetId );
        continue;
      }
      if ( snapshot->parentCollectionId().has_value() )
      {
        // The source is already a child of a DIFFERENT collection (a child can
        // belong to only one collection). Fail fast with an import-scoped
        // diagnostic; the reused asset is left untouched.
        rollback();
        result.diagnostics = { diagnostic(
          QStringLiteral( "import.child_in_other_collection" ),
          QStringLiteral( "The source %1 is already imported as a child of another "
                          "collection" ).arg( source.canonicalSource ) ) };
        return result;
      }
      // A standalone pre-existing asset: adopt it (set this collection as its
      // parent). Rollback only unparents it (non-cascade unload), never unloads.
      const Result<void> added =
        m_dataManager->addChildToCollection( result.collectionId, registered.assetId );
      if ( !added )
      {
        rollback();
        result.diagnostics = added.diagnostics();
        return result;
      }
      result.childAssetIds.append( registered.assetId );
      continue;
    }

    // Freshly registered by this commit: attach it as a child. This cannot
    // fail (both just created, child has no parent), but handle defensively.
    const Result<void> added =
      m_dataManager->addChildToCollection( result.collectionId, registered.assetId );
    if ( !added )
    {
      createdChildren.append( registered.assetId );
      rollback();
      result.diagnostics = added.diagnostics();
      return result;
    }

    createdChildren.append( registered.assetId );
    result.childAssetIds.append( registered.assetId );
  }

  return result;
}

Result<CommitImportResult> CollectionImportService::importCollection( const QString &sourcePath,
                                                                         sicnu::data::PersistencePolicy persistence,
                                                                         bool autoLoad,
                                                                         std::function<void(const QString &path)> pathOpener )
{
  auto probeRes = probe( sourcePath );
  if ( !probeRes )
  {
    return Result<CommitImportResult>::failure( probeRes.diagnostics() );
  }

  const ImportPreview preview = probeRes.value();
  CommitImportRequest request;
  request.preview = preview;
  request.persistence = persistence;

  for ( int i = 0; i < preview.children.size(); ++i )
  {
    request.selectedChildIndices.append( i );
  }

  CommitImportResult commitResult = commit( request );
  if ( commitResult.collectionId.isNull() )
  {
    return Result<CommitImportResult>::failure( commitResult.diagnostics );
  }

  if ( autoLoad && pathOpener )
  {
    // Prefer the committed Data Asset's canonical source (post-register path)
    // so autoLoad tracks catalog identity, not only the probe preview string.
    QString primaryPath;
    if ( !commitResult.childAssetIds.isEmpty() && m_dataManager )
    {
      const auto snapshot = m_dataManager->asset( commitResult.childAssetIds.first() );
      if ( snapshot.has_value() )
        primaryPath = snapshot->source().canonicalSource;
    }
    if ( primaryPath.isEmpty() && !preview.children.isEmpty() )
    {
      const ChildCandidate &primary = preview.children.first();
      primaryPath = primary.sourcePath;
      if ( primaryPath.isEmpty() && !primary.bands.isEmpty() )
        primaryPath = primary.bands.first().sourcePath;
    }
    if ( !primaryPath.isEmpty() )
      pathOpener( primaryPath );
  }

  return Result<CommitImportResult>::success( commitResult );
}

// --- SatelliteProductsDiscoverer ---

Result<DiscoveredProduct> SatelliteProductsDiscoverer::discover( const QString &source )
{
  ProductInfo info;
  QString discoverError;
  if ( !SatelliteProducts::discoverProduct(
         source, &info, QStringLiteral( "10m" ), &discoverError ) )
  {
    return Result<DiscoveredProduct>::failure(
      { { QStringLiteral( "import.discover_failed" ), discoverError } } );
  }

  DiscoveredProduct product;
  product.productId = info.productId;
  product.spacecraft = info.spacecraft;
  product.processingLevel = info.processingLevel;
  product.acquisitionDate = info.acquisitionDate;
  product.attributes = info.attributes;

  // Group bands by source path: bands that live in different files are
  // DISTINCT child candidates (so the user can select which bands to import,
  // spec user story 2), while bands sharing one file (a multi-band raster, or
  // one MODIS/HDF container) form a single child. This is what makes a Landsat
  // scene - where each band is its own file - import band-by-band rather than
  // collapsing to a single first-band child. Grid-label comes from the
  // discoverer's `resolution` attribute when present (Sentinel-2 L2A sets it),
  // else "default".
  const QString gridLabel = info.attributes.value(
    QStringLiteral( "resolution" ), QStringLiteral( "default" ) );

  QMap<QString, int> groupIndexByPath; // source path -> index in gridGroups
  for ( const BandFile &band : info.bands )
  {
    const QString path = band.path.isEmpty() ? info.metadataPath : band.path;
    const auto it = groupIndexByPath.constFind( path );
    if ( it == groupIndexByPath.constEnd() )
    {
      DiscoveredGridGroup group;
      group.gridLabel = gridLabel;
      group.displayName = QStringLiteral( "%1 (%2)" ).arg( band.name, gridLabel );
      group.sourcePath = path;
      product.gridGroups.append( group );
      groupIndexByPath.insert( path, product.gridGroups.size() - 1 );
      product.gridGroups.last().bands.append( band );
    }
    else
    {
      // A band in an already-seen file: same child candidate (same grid).
      product.gridGroups[it.value()].bands.append( band );
    }
  }

  return Result<DiscoveredProduct>::success( std::move( product ) );
}

} // namespace sicnu
