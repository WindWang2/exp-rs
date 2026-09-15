/***************************************************************************
 * rs_product_import_plan.h — standardized CN product import plan service
 * (ADR 0147).
 *
 * One seam shared by every surface (GUI / CLI / MCP / per-family operators):
 *
 *   identify → inspect → validate → resolve constituents → role map
 *   → optional calibration → stack → stamp → provenance
 *
 * The plan stage never touches pixels and never writes files; execution
 * stacks through the unchanged `SatelliteProducts::stackToGeoTiff` contract
 * (#676/#634 fail-closed band resolution) and records full provenance:
 * identity, sensor profile id + registry version, sidecar generation,
 * declared-field flags, missing constituents, numeric-domain state and the
 * applied calibration per band.
 *
 * Absence policy (ADR 0157): declared values are copied verbatim; fields the
 * sidecar does not declare are reported as missing — never defaulted, and
 * calibration is applied only when EVERY requested band declares both
 * coefficients (partial coverage is a typed refusal naming the bands).
 ***************************************************************************/
#pragma once

#include "geospatial/products/cn_product_metadata.h"
#include "geospatial/products/product_adapters.h"
#include "geospatial/products/product_registry.h"
#include "geospatial/products/sensor_profile.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <QString>
#include <QStringList>

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::operators::rs {

/// Resolved, validated import plan for one CN product.
struct ProductImportPlan
{
    sicnu::geo::CnProductIdentity identity;
    sicnu::geo::ProductMetadata metadata;   ///< declared fields + parse diagnostics
    sicnu::geo::SensorProfileRecord profile; ///< sensor truth (registry-backed)
    std::string sensorKey;                   ///< refined key (pan/MS resolved)
    QString sidecarPath;
    QString imagePath;
    QString rpcPath;          ///< declared RPC document ("" when absent)
    QString siblingImagePath; ///< PMS-pair sibling image ("" when absent/none)
    QString siblingImageRole; ///< "pan" | "ms" | ""
    /// Declared band ids in sidecar order = TIFF band order; falls back to
    /// the registry layout when the sidecar declares no inventory (reported
    /// via bandOrderDeclared=false, bandOrderUnverified in the result, a
    /// warning, and SICNU_BAND_ORDER_UNVERIFIED metadata — never hidden).
    QStringList bandNames;
    bool bandOrderDeclared = false;
    /// Completeness verdict over the resolved constituents.
    sicnu::geo::ProductCompleteness completeness = sicnu::geo::ProductCompleteness::Invalid;
    QStringList missingConstituents;
    QStringList warnings;

    /// Stable inspect/plan payload (agents and callers read this instead of
    /// re-parsing sidecars).
    Json::Value toJson() const;
};

/// identify → inspect → validate → resolve constituents → role map.
/// @p familyFilter pins the expected family kindName ("" accepts any
/// supported CN family). Throws RSOperatorError(InvalidInputData) with the
/// identity diagnosis for unsupported CN families and FileNotFound /
/// InvalidInputData when the sidecar/image cannot be resolved.
ProductImportPlan planCnProductImport( const std::string &input, const char *familyFilter );

/// One resolved constituent of a dry-run import (ADR 0159): presence,
/// readability and verbatim content identity. Everything is opened READ-ONLY;
/// a dry run never writes.
struct ConstituentReport
{
    QString role;         ///< "sidecar" | "image" | "rpc" | "sibling_image"
    QString path;
    bool exists = false;
    bool readable = false; ///< opened read-only and at least one block read
    qint64 bytes = 0;      ///< full file size on disk
    std::string sha256Hex; ///< lowercase hex digest (see digestScope)
    bool hashComplete = false; ///< true only when the digest covers the WHOLE file
    qint64 hashedBytes = 0;    ///< bytes actually hashed
    QString digestScope;       ///< "file" or "first <N> bytes (declared budget cap)"
    QString note;              ///< verbatim OS/driver reason when unreadable
};

/// Dry-run import: the full plan plus a per-constituent graph with checksums.
/// Purely read-only over the source; never touches pixels, never writes.
/// Files larger than @p hashBudgetBytes are hashed over their declared
/// prefix only — the report says so explicitly (digestScope/hashedBytes)
/// instead of claiming a whole-file digest. @p context (optional) receives
/// progress and is honoured as a cancellation checkpoint between files.
struct ProductImportDryRun
{
    ProductImportPlan plan;
    std::vector<ConstituentReport> constituents;
    QString checksumAlgorithm = QStringLiteral( "sha256" );
    qint64 hashBudgetBytes = 0;

    /// Stable inspect payload: { plan, constituents[], checksum{} }.
    Json::Value toJson() const;
};

ProductImportDryRun dryRunCnProductImport( const std::string &input, const char *familyFilter,
                                           qint64 hashBudgetBytes = 268435456 /* 256 MiB */,
                                           RSOperatorContext *context = nullptr );

/// Whether declared-coefficient calibration can be applied to the requested
/// bands (radiance = DN × gain + bias). Applicable only when every requested
/// band declares BOTH gain and bias; a requested-but-not-applicable decision
/// names the bands missing coefficients (typed refusal, no partial state).
struct CalibrationDecision
{
    bool requested = false;
    bool applicable = false;
    QStringList bandsMissingCoefficients;
    const char *formula = "radiance = DN * gain + bias";
};

CalibrationDecision evaluateCalibration( const ProductImportPlan &plan,
                                         bool applyCalibration,
                                         const QStringList &requestedBands );

/// stack → optional calibration → stamp → provenance. @p requestedBands must
/// resolve against the plan (fail-closed #676 contract). Applies calibration
/// only when @p applyCalibration and the decision is applicable; throws
/// RSOperatorError with a typed reason otherwise. Returns the provenance
/// result JSON.
Json::Value executeCnProductImport( const ProductImportPlan &plan,
                                    const std::string &outputPath,
                                    const QStringList &requestedBands,
                                    bool applyCalibration,
                                    RSOperatorContext &context );

} // namespace sicnu::operators::rs
