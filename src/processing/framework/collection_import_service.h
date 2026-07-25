#pragma once

#include <QObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>

#include "processing/algorithms/satellite_products.h"
#include "data/asset_types.h"
#include "data/collection_types.h"
#include "data/data_result.h"

namespace sicnu::data
{
class DataManager;
}

namespace sicnu
{

/// One band of a child candidate, normalized from a provider
/// `SatelliteProducts::BandFile`. The raw provider type is mapped into this
/// shape by the probe so the preview never exposes provider types through the
/// import-service boundary.
struct ChildBandInfo
{
  QString name;            ///< "B4", "sur_refl_b01"
  QString sourcePath;      ///< band's file path or GDAL subdataset string
  int sourceBand = 1;      ///< 1-based band index inside @a sourcePath
  int wavelengthNm = 0;    ///< Approximate centre wavelength (0 if unknown)

  friend bool operator==( const ChildBandInfo &, const ChildBandInfo & ) = default;
};

/// A candidate child asset: one distinct grid group of a product. Different
/// grids (a Sentinel-2 10 m vs 20 m group, or HDF subdatasets on independent
/// grids) are distinct child candidates - they are never merged into a single
/// multi-band raster at preview time.
struct ChildCandidate
{
  sicnu::data::AssetKind kind = sicnu::data::AssetKind::Raster;
  QString displayName;     ///< "10 m group", "Subdataset: ..."
  QString gridLabel;       ///< "10m", "20m", "default"
  QString sourcePath;      ///< Anchor source for kind/structure read (#52)
  QVector<ChildBandInfo> bands;

  friend bool operator==( const ChildCandidate &, const ChildCandidate & ) = default;
};

/// Read-only preview of a complex product before commit. Pure: holds no
/// catalog state. Probing the same source twice yields equal previews, and a
/// probe followed by no commit changes nothing in the catalog.
struct ImportPreview
{
  QString collectionDisplayName;
  sicnu::data::ProductMetadata metadata;
  QVector<ChildCandidate> children;

  friend bool operator==( const ImportPreview &, const ImportPreview & ) = default;
};

/// Request to atomically commit an import: register the collection from the
/// preview's metadata, then register each selected child as a full Data Asset
/// attached to the collection. All-or-nothing - any child registration failure
/// rolls back the collection and every previously-registered child so the
/// catalog never holds a half-imported product.
struct CommitImportRequest
{
  ImportPreview preview;
  /// Indices into `preview.children`. An empty selection registers the
  /// collection with no children (a valid degenerate case). Out-of-range or
  /// duplicate indices are rejected before any registration.
  QVector<int> selectedChildIndices;
  /// Persistence policy for the registered children. Collections themselves
  /// carry no persistence concept; this applies to the child assets. Defaults
  /// to `ProjectPersistent` - an imported product is meant to survive the
  /// session and round-trip into the `.qgz`. Tests may use `SessionTemporary`.
  sicnu::data::PersistencePolicy persistence =
    sicnu::data::PersistencePolicy::ProjectPersistent;
};

/// Outcome of an atomic import commit. `collectionId` is null on failure (and
/// no children remain). `childAssetIds` is ordered to match the request's
/// selected-child order; a reused (deduped) source appears once per selection
/// but registers a single underlying asset (per-child-source dedup).
struct CommitImportResult
{
  sicnu::data::CollectionId collectionId;
  QVector<sicnu::data::AssetId> childAssetIds;
  QVector<sicnu::data::Diagnostic> diagnostics;
};

/// One grid group of a discovered product, as emitted by a `ProductDiscoverer`.
/// A discoverer groups a product's band files by grid before returning, so the
/// probe is a pure mechanical mapper and never re-derives grids itself. (The
/// existing `SatelliteProducts::ProductInfo` flattens everything into one
/// list; grid grouping lives in the discoverer, not in the provider.)
struct DiscoveredGridGroup
{
  QString gridLabel;
  QString displayName;
  QString sourcePath;
  QVector<SatelliteProducts::BandFile> bands;
};

/// A discovered complex product, normalized and grid-grouped. This is the
/// discoverer's output and the probe's input - it carries normalized product
/// metadata plus per-grid child candidates, not the raw `ProductInfo`.
struct DiscoveredProduct
{
  QString productId;
  QString spacecraft;
  QString processingLevel;
  QString acquisitionDate;
  QMap<QString, QString> attributes;
  QVector<DiscoveredGridGroup> gridGroups;
};

/// Abstract discoverer: maps provider inputs (SatelliteProducts::ProductInfo,
/// GDAL subdatasets) to the normalized, grid-grouped `DiscoveredProduct`. The
/// probe is unit-tested with a stub implementation; the real adapter wraps
/// `SatelliteProducts::discoverProduct`.
class ProductDiscoverer
{
  public:
    virtual ~ProductDiscoverer() = default;

    /// Discover and normalize the product at @a source. Failures are returned
    /// via `Result::failure` (the probe forwards the diagnostics verbatim and
    /// registers nothing). There is no `errorMessage` out-param - `Result`
    /// already carries `QVector<Diagnostic>`, the house error type, mirroring
    /// `registerSource`/`OutputCommitter::commit`.
    virtual sicnu::data::Result<DiscoveredProduct>
    discover( const QString &source ) = 0;
};

/// Real adapter around `SatelliteProducts::discoverProduct`. Groups the
/// discovered bands by source path: bands in different files are DISTINCT
/// child candidates (so the user can select which bands to import, spec user
/// story 2), while bands sharing one file form a single child. This makes a
/// Landsat scene - where each band is its own file - import band-by-band. The
/// grid label comes from the discoverer's `resolution` attribute when present
/// (Sentinel-2 L2A), else "default". True multi-grid extraction for
/// Sentinel-2 L2A (separate 10 m / 20 m / 60 m groups, which `discoverProduct`
/// filters to one preferred resolution) is a deferred follow-up: calling
/// `discoverSentinel2` per resolution would emit one group per grid.
class SatelliteProductsDiscoverer : public ProductDiscoverer
{
  public:
    sicnu::data::Result<DiscoveredProduct>
    discover( const QString &source ) override;
};

/// Read-only discovery probe over a DataManager. Maps a `DiscoveredProduct`
/// into an `ImportPreview` without mutating the catalog. The `commit` step
/// (atomic collection + child registration) arrives in #52; the constructor
/// holds the DataManager so its signature is stable across both waves.
class CollectionImportService : public QObject
{
  // Q_OBJECT retained for parent-ownership consistency with OutputCommitter.
  // The service declares no signals of its own: probe is read-only, and commit
  // composes the DataManager's collectionAdded/assetAdded signals rather than
  // re-emitting them. A commit-progress signal, if ever needed, is a #53 concern.
  Q_OBJECT

  public:
    CollectionImportService( sicnu::data::DataManager *dataManager,
                             ProductDiscoverer *discoverer,
                             QObject *parent = nullptr );

    /// Probes @a source and returns a normalized preview. Does not register
    /// anything in the catalog - the probe is read-only. A discoverer failure
    /// is returned as `import.discover_failed` diagnostics.
    sicnu::data::Result<ImportPreview> probe( const QString &source );

    /// Atomically commits an import: registers the collection + each selected
    /// child. The DataManager fires one `collectionAdded`, then one
    /// `assetAdded` per child (the service composes those signals; it does not
    /// re-emit). On any child registration failure, rolls back the collection
    /// and every child THIS commit created, so the catalog never holds a
    /// half-imported product. Rollback never unloads a reused (deduped) asset -
    /// a source already imported as a child of another collection fails fast
    /// with `import.child_in_other_collection` (a child can belong to only one
    /// collection), and a standalone pre-existing asset is adopted (parented to
    /// this collection) and merely unparented on rollback. Returns a null
    /// `collectionId` and the failure diagnostics.
    CommitImportResult commit( const CommitImportRequest &request );

  private:
    sicnu::data::DataManager *m_dataManager; ///< Used by commit() (probe is read-only).
    ProductDiscoverer *m_discoverer;          ///< Not owned.
};

} // namespace sicnu
