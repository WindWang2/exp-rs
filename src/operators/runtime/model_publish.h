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
/// The backup suffix is deliberately NOT the vector writer's own ".prev~"
/// (which it unconditionally cleans up at the end of a successful publish,
/// companions included): a shared name would have the writer delete this
/// guard's backup. Each lane passes its own unique suffix — the ensemble
/// lane ".ensemble-prev~", the single-model lane ".det-prev~".
class DetectionPublishGuard
{
  public:
    DetectionPublishGuard( const QString &finalPath, const QString &backupSuffix );
    ~DetectionPublishGuard();
    void disarm();

    DetectionPublishGuard( const DetectionPublishGuard & ) = delete;
    DetectionPublishGuard &operator=( const DetectionPublishGuard & ) = delete;

  private:
    QString m_final;
    QString m_backup;
    QString m_backupSuffix;
    bool m_hadExisting = false;
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
