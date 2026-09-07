// src/agent/cartography/style_compiler.h
#pragma once

//
// StyleSpec → QGIS renderer adapter (Platform 5.0, Milestone E).
//
// Applies a *token-resolved* StyleSpec (see style_spec.h) to a QGIS map
// layer using QGIS renderer primitives only. QGIS stays the single
// rendering truth: the compiler is a thin, honest adapter that maps the
// declarative knowledge onto QgsRasterRenderer / QgsFeatureRenderer objects
// and reports every mapping decision. Unknown combinations are errors —
// never silent fallbacks.
//
// Contract:
//   - `styleSpec` must already carry resolved token values (hex colors as
//     strings, no "token:" references left). Use resolveStyleTokens() first.
//   - Application is idempotent per layer: each call replaces the renderer.
//   - The layer object is only mutated on success paths; per-block problems
//     are reported and skipped (e.g. labels on a layer without fields).
//

#include <json/json.h>

#include <QString>
#include <QStringList>

class QgsMapLayer;

namespace sicnu::agent::cartography {

/// Applies the raster and/or vector blocks of `styleSpec` to `layer`
/// (raster blocks to QgsRasterLayer, vector blocks to QgsVectorLayer).
/// Returns false when nothing applicable could be applied, with *error set;
/// per-entry problems are collected in *problems while other entries still
/// apply.
bool applyStyleSpecToLayer( QgsMapLayer *layer, const Json::Value &styleSpec, QString *error = nullptr,
                            QStringList *problems = nullptr );

/// Builds a QGIS renderer for the raster block alone (no layer mutation).
/// Returns null with *error when the block is invalid/unsupported.
class QgsRasterRenderer;
QgsRasterRenderer *buildRasterRenderer( const Json::Value &rasterBlock, int bandCount,
                                        QString *error = nullptr );

/// Prepares a compact report of what applyStyleSpecToLayer did/changed:
/// {layer, applied: [...], problems: [...]} — for tool responses.
Json::Value styleApplicationReport( const QString &layerId, const QStringList &applied,
                                    const QStringList &problems );

} // namespace sicnu::agent::cartography
