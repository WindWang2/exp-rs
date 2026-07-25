#include "collection_import_service.h"

#include "data/data_manager.h"

using sicnu::data::AssetKind;
using sicnu::data::DataManager;
using sicnu::data::Diagnostic;
using sicnu::data::ProductMetadata;
using sicnu::data::Result;
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

  // One grid group for the discovered (preferred-resolution) band set. The
  // grid label comes from the discoverer's `resolution` attribute when present
  // (Sentinel-2 L2A sets it), else "default". Real multi-grid extraction
  // (separate groups for S2 10m/20m/60m, or MODIS subdatasets on independent
  // grids) is a deferred follow-up; the stub discoverer already proves the
  // grid-splitting contract this adapter will eventually honor.
  DiscoveredGridGroup group;
  group.gridLabel = info.attributes.value(
    QStringLiteral( "resolution" ), QStringLiteral( "default" ) );
  group.displayName = QStringLiteral( "%1 (%2)" ).arg( info.productId, group.gridLabel );
  group.sourcePath = info.bands.isEmpty() ? info.metadataPath : info.bands.first().path;
  group.bands = info.bands;
  product.gridGroups.append( group );

  return Result<DiscoveredProduct>::success( std::move( product ) );
}

} // namespace sicnu
