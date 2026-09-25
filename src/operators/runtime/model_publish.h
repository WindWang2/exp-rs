// src/operators/runtime/model_publish.h — the shared provenance publish
// authority for the model runtime lanes (Platform 8.0/13.0; completion
// 13/15 moves the per-lane copies here so ONE writer contract exists).
//
// Every model product lane (single-model raster / multi-input / detection,
// ensemble raster / detection) publishes its provenance sidecar through
// publishProvenanceSidecar and rolls detection products back through
// DetectionPublishGuard — the verifier (provenance_verify.h) and the crash
// invariants both lean on that being one implementation, not one per lane.
#pragma once

#include "operators/runtime/detection_tile_engine.h"
#include "operators/runtime/model_runtime.h"

#include <QString>

#include <json/json.h>

#include <string>

namespace sicnu::operators::runtime {

/// Compact CRS display string for payloads/sidecars: "EPSG:32633" when the
/// SRS carries an authority code, else a truncated WKT; "" for undeclared.
std::string crsDisplayName( const QString &wkt );

/// Publishes the provenance sidecar (<finalPath>.prov.json) next to an
/// already-published product — same-directory staged write + rename. The
/// CALLER parks any previous sidecar BEFORE the product rename, so the
/// on-disk states possible across a crash are: product+matching sidecar
/// (full success), product without sidecar (crash before the sidecar rename
/// — detectable absence, never a stale mismatched one), or the previous
/// product untouched. There is no consumer-side detection for that crash
/// window; the next successful run rewrites both.
/// @param faultPoint test-only injection name routed through the REAL
///   failure branch (Verification Platform 8.0 pattern); nullptr disables.
bool publishProvenanceSidecar( const QString &finalPath, const Json::Value &provenance,
                               const char *faultPoint, std::string *error );

/// Owns the PREVIOUS detection product across the vector publish and the
/// sidecar write: the previous product (main + shapefile companions +
/// provenance sidecar) is moved aside on construction and restored on unwind
/// — a throw from the writer OR the sidecar publish leaves the previous
/// product exactly as it was, never a torn one and never a hidden backup.
/// Disarmed after a successful sidecar publish (the backup is then removed).
///
/// The backup suffix is deliberately NOT any of the vector writer's own
/// publish-family names (the writer publishes main + companions through the
/// atomic_fs group ladder, which manages its own ".bak" set): a shared name
/// would have the writer delete this guard's backup. Each lane passes its
/// own unique suffix — the ensemble lane ".ensemble-prev~", the single-
/// model lane ".det-prev~".
class DetectionPublishGuard
{
  public:
    DetectionPublishGuard( const QString &finalPath, const QString &backupSuffix );
    ~DetectionPublishGuard();
    void disarm();

    DetectionPublishGuard( const DetectionPublishGuard & ) = delete;
    DetectionPublishGuard &operator=( const DetectionPublishGuard & ) = delete;

  private:
    /// Removes the whole backup family (main + companions + prov backup).
    void removeBackupFamily();

    QString m_final;
    QString m_backup;
    QString m_backupSuffix;
    bool m_hadExisting = false;
    bool m_hadProv = false;
    bool m_disarmed = false;
};

/// Owns the PREVIOUS single-file product (main + provenance sidecar) across
/// the staged publish and the sidecar write — the raster / scene lanes'
/// counterpart of DetectionPublishGuard (which adds shapefile companions on
/// top of the same invariants). The previous pair is parked sidecar-FIRST,
/// main-LAST; a throw from the swap OR the sidecar publish restores it
/// exactly. The two crash windows the ladder can leave behind are adopted
/// back on the next publish of the same path:
///   * final ABSENT, backup present (crash between main park and swap)
///     -> restore sidecar first, main last;
///   * final present WITHOUT its sidecar, backup present WITH one (crash
///     between swap and sidecar publish — the new product never completed
///     publication) -> the parked pair is the last PUBLISHED product and is
///     adopted back, replacing the unpublished file.
/// A crash AFTER the sidecar publish only leaves backup litter, which the
/// park's pre-clean drops — the new pair was complete, so nothing is
/// adopted. Every fault point routes through the REAL failure branch
/// (Verification Platform 8.0 pattern); nullptr disables.
class ProductPublishGuard
{
  public:
    /// @param stageForCleanup freshly written staged file removed when the
    /// constructor throws (a throwing constructor never runs the destructor);
    /// empty when the caller has no stage yet.
    ProductPublishGuard( const QString &finalPath, const QString &backupSuffix,
                         const char *parkFaultPoint,
                         const QString &stageForCleanup = QString() );
    ~ProductPublishGuard();

    /// Atomically swaps a fully-written staged file onto the final path.
    /// On a fired fault or a failed rename the stage is removed and the
    /// error is thrown — the destructor then restores the parked pair.
    void publishStaged( const QString &stagePath, const char *swapFaultPoint,
                        const std::string &errorWhat );

    /// Drops the whole backup family after a successful publish (the parked
    /// pair — an adopted crash orphan is cleaned exactly like a live backup).
    void disarm();

    ProductPublishGuard( const ProductPublishGuard & ) = delete;
    ProductPublishGuard &operator=( const ProductPublishGuard & ) = delete;

  private:
    /// Removes the backup family (main + prov sidecar backup).
    void removeBackupFamily();

    QString m_final;
    QString m_backup;
    QString m_stageForCleanup;
    bool m_hadExisting = false;
    bool m_hadProv = false;
    bool m_disarmed = false;
};

/// Builds the single-model detection provenance document (schema
/// exp-rs-prov/1): model identity (including the task intent — a detection
/// product names itself a detection product, mirroring the scene artifact),
/// execution identity (backend/device/execution provider), tile stats, the
/// detection semantics (effective thresholds + class vocabulary + counts),
/// the fed input grid and the vector output block. Fields the run cannot
/// know stay absent — truthful provenance, never placeholders.
Json::Value buildDetectionProvenance( const ModelInfo &model, const ModelRuntimePtr &runtime,
                                      const DetectionTileStats &stats );

} // namespace sicnu::operators::runtime
