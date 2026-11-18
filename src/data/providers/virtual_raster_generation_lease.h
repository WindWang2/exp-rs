/***************************************************************************
  providers/virtual_raster_generation_lease.h — R33-T03: managed retention
  of a published virtual raster's warp-intermediate generation.
  ---------------------------
  Begin                : 2026-11
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#ifndef SICNU_DATA_PROVIDERS_VIRTUAL_RASTER_GENERATION_LEASE_H
#define SICNU_DATA_PROVIDERS_VIRTUAL_RASTER_GENERATION_LEASE_H

#include <QString>
#include <QStringList>

#include <memory>

namespace sicnu::data
{

/// RAII retention for the warp-intermediate generation(s) the CURRENTLY
/// published VRT references. While at least one lease for a
/// (VRT, generation) pair is alive, the post-publish sweep
/// (`sweepStaleWarpIntermediates`) does NOT delete that generation's
/// staged warp files — a managed reader that opened the VRT keeps every
/// file it may still lazily read. The last release re-sweeps the released
/// generation's files (deferred deletion) unless the current VRT bytes
/// still reference them.
///
/// Scope, stated honestly: the lease retains FILES the provider itself
/// staged under `<vrt>.warp-<gen8>-<identity>.tif`. It does not version the
/// VRT XML itself (a re-open through the VRT path after a new publish
/// serves the new generation — the close/reopen contract for the artifact
/// is unchanged), and it cannot resurrect bytes an EXTERNAL writer
/// overwrote in place. Non-warp VRTs retain nothing (no intermediates).
class VirtualRasterGenerationLease
{
  public:
    VirtualRasterGenerationLease() = default;
    VirtualRasterGenerationLease( const VirtualRasterGenerationLease & ) = delete;
    VirtualRasterGenerationLease &operator=( const VirtualRasterGenerationLease & ) = delete;
    /// Move TRANSFER only (a retention has exactly one owner): the
    /// move-assignment releases the currently held retention BEFORE taking
    /// the incoming one (a defaulted operator would silently drop the old
    /// Control through unique_ptr and leak the registry registration).
    VirtualRasterGenerationLease( VirtualRasterGenerationLease &&other ) noexcept;
    VirtualRasterGenerationLease &operator=( VirtualRasterGenerationLease &&other ) noexcept;
    ~VirtualRasterGenerationLease();

    /// False for a default-constructed, released, moved-from, or degraded
    /// lease (no retention is held).
    bool isValid() const { return m_control != nullptr; }
    /// The VRT path the retention was acquired for (absolute).
    QString vrtPath() const;
    /// The primary retained 8-hex generation token (first referenced;
    /// empty when invalid). Multi-generation VRTs (unchanged identities
    /// reused across publishes) retain every referenced token.
    QString generationToken() const;

  private:
    friend VirtualRasterGenerationLease acquireVirtualRasterGenerationLease( const QString & );
    struct Control
    {
      QString vrtPath;        // absolute
      QStringList tokens;     // every retained 8-hex generation
    };
    explicit VirtualRasterGenerationLease( Control control );
    void release();

    std::unique_ptr<Control> m_control;
};

/// Acquires retention for every warp-intermediate generation the CURRENTLY
/// published VRT references (normally exactly one — all of one layout's
/// inputs share the recipe digest). A VRT without warp intermediates, a
/// missing/unreadable VRT, or a full retention registry yields an INVALID
/// lease: the caller proceeds under the close/reopen contract (documented
/// degradation, never a read refusal).
VirtualRasterGenerationLease acquireVirtualRasterGenerationLease( const QString &vrtPath );

/// Number of active generation leases for `vrtPath` (observability for
/// tests and operators; process-wide).
int virtualRasterGenerationLeaseCount( const QString &vrtPath );

/// True while `generationToken` of `vrtPath` is held by at least one active
/// lease, for diagnostics and tests.
bool virtualRasterGenerationRetained( const QString &vrtPath, const QString &generationToken );

/// Performs the sweep's retention check and removal atomically with lease
/// registration. The caller has already excluded the published live files.
void removeUnretainedVirtualRasterIntermediate( const QString &vrtPath, const QString &filePath,
                                               const QString &generationToken );

/// The warp-intermediate FILENAMES the currently published VRT references
/// (its live set). Empty when the VRT is missing or references none.
QStringList virtualRasterReferencedIntermediates( const QString &vrtPath );

/// The 8-hex generation token of a staged warp-intermediate `fileName`
/// relative to `vrtPath`'s base name (`<vrt>.warp-<gen8>-<identity>.tif`);
/// empty for spellings the provider never staged (foreign files are never
/// retainable). Shared by the sweep and the lease scan.
QString virtualRasterIntermediateGenerationToken( const QString &vrtPath,
                                                  const QString &fileName );

} // namespace sicnu::data

#endif // SICNU_DATA_PROVIDERS_VIRTUAL_RASTER_GENERATION_LEASE_H
