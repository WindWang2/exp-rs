// src/agent/cartography/produce.h
#pragma once

//
// Governed cartography production (Cartography Production 11.0).
//
// `produce` is the ONE sanctioned production chain over the existing
// cartography engine — it orchestrates the exact free functions the agent
// tools and RSOperators already run, in the fixed order
//
//   upgrade → validate → conditions → composition → compile →
//   preflight → bounded repair → export (single or atlas) → manifest
//
// and adds nothing the engine does not already do: no second compile, no
// second export path, no solver of its own. What it DOES add is the
// production contract the individual steps never had:
//
//   * one call = one deliverable directory; artifacts + manifest land
//     together or a rollback leaves the directory as it was (atomic
//     publish across the whole delivery);
//   * progress + cooperative cancellation between every stage and on
//     every atlas page boundary;
//   * an honest result envelope (quality verdict, repair ledger,
//     digests, manifest path) for GUI, agent tool, operator and tests.
//
// TaskCenter stays the only executor: the dock submits this through the
// operator surface, the CLI through a pipeline step, the agent through
// the cartography:produce tool — all the same function below.
//

#include <json/json.h>
#include <qgslayoutexporter.h>
#include <qgsprintlayout.h>

#include <functional>
#include <string>
#include <vector>

namespace sicnu::agent::cartography {

/// Cancellation + progress seam. Return false to cancel the production;
/// `stage` is a stable short token ("validate", "compose", "repair",
/// "export", "manifest"), progress in [0,1] across the whole chain.
using ProduceReporter = std::function<bool( const char *stage, double progress, const std::string &detail )>;

struct ProduceRequest
{
    Json::Value mapspec;            ///< MapSpec document (any spec_version; upgraded)
    std::string format = "png";     ///< "png" | "pdf" | "svg"
    double dpi = 300.0;             ///< 72..1200 (validated)
    std::string directory;          ///< delivery directory (created when missing)
    std::string file_name;          ///< optional base name (default: layout name)
    bool write_manifest = true;     ///< manifest sidecar (default on)
    int max_repair_iterations = 3;  ///< clamped to [1,10], matches cartography:repair
    /// When true, a preflight that still reports repairable findings after
    /// the bounded repair loop fails the production (typed refusal) instead
    /// of shipping a document that declares its own defects. Errors are
    /// always fatal regardless.
    bool require_preflight_pass = false;
};

struct ProduceResult
{
    bool ok = false;
    std::string error_code;   ///< typed refusal vocabulary, empty when ok
    std::string error;        ///< human-readable cause
    std::string mode;         ///< "single" | "atlas"
    Json::Value quality;      ///< final preflight report
    Json::Value mapspec;      ///< the delivered (post-repair) document
    int repairs_applied = 0;
    int repair_iterations = 0;
    Json::Value repair_ledger;
    Json::Value composition;
    std::string structural_digest;
    Json::Value provenance;
    int page_count = 0;                  ///< delivered pages (atlas: features rendered)
    std::string artifact_path;           ///< single-mode artifact (absolute)
    std::string manifest_path;           ///< manifest sidecar (absolute) when written
    Json::Value manifest;                ///< the manifest document when written
    QStringList font_diagnostics;        ///< font substitutions observed at export
    QgsLayoutExporter::ExportResult exporterResult = QgsLayoutExporter::Success;

    bool cancelled() const { return error_code == "PRODUCE_CANCELLED"; }
};

/// Runs the governed production chain. Never throws; failures are reported
/// in the result. The returned directory content is either exactly the
/// declared delivery or exactly what it was before the call.
ProduceResult produceMap( const ProduceRequest &request,
                          const ProduceReporter &reporter = {} );

/// JSON projection of the result envelope (tool/operator output contract;
/// "output" carries the artifact path or the manifest path when present).
Json::Value produceResultToJson( const ProduceResult &result );

} // namespace sicnu::agent::cartography
