// src/agent/cartography/quality.h
#pragma once

//
// Cartography quality gates (Design System 4.0, Milestone E / ADR 0131).
//
// Owns the spec-level preflight rule catalog and the deterministic repair
// pass behind cartography:preflight / cartography:repair. The public API is
// the one ADR 0127 introduced (preflightMapSpec / repairMapSpec); the
// implementations live here so the tool file stays tool-only.
//
// Rule contract (documented in docs/cartography/preflight-rules.md):
//   - every issue carries code/severity/message/repairable/item_id and, for
//     repairable findings, a suggested_action;
//   - repairs are deterministic, bounded (the repair loop iterates at most
//     max_iterations times), never delete meaningful content — the only
//     removals are byte-identical duplicates and dangling component
//     references that resolved to nothing — and leave a passing or
//     explicitly failing report (residual issues stay listed).
//

#include <json/json.h>

#include <string>

namespace sicnu::agent::cartography {

/// Spec-level cartography preflight: returns a MapQualityReport envelope
/// (kind "map_quality_report") with code/severity/item_id/repairable/
/// suggested_action issues and a 0-100 quality score. `compiledReport` may
/// carry the layout:preflight report of the compiled layout to merge.
/// The report is a pure function of the input spec.
Json::Value preflightMapSpec( const Json::Value &spec,
                              const Json::Value &compiledReport = Json::Value() );

/// Applies one deterministic repair pass for every repairable issue of the
/// current report. Callers run the composition solver once BEFORE looping
/// repairs (cartography:repair does); re-solving anchored geometry every
/// pass would un-do clamps and prevent convergence. Returns the number of
/// repairs applied.
int repairMapSpec( Json::Value &spec, const Json::Value &report );

/// Deterministic single-line text-width estimate in millimeters (CJK
/// fullwidth glyphs count one em, other glyphs 0.55 em, spaces 0.35 em;
/// 1 pt = 0.3528 mm). Platform-independent by design: the preflight must
/// not depend on locally installed fonts.
double estimateTextWidthMm( const std::string &text, double sizePt );

/// The machine-readable catalog of preflight rule codes
/// ({code, severity, repairable, description}), in catalog order.
Json::Value preflightRuleCatalog();

/// Platform 7.0 visual-regression substrate: a deterministic structural
/// digest of the RESOLVED spec geometry — one SHA-256 hex string over the
/// canonical, id-sorted item entries (collection, id, rect rounded to
/// 0.01 mm, page, z_index). Rendering-free: identical resolved geometry
/// produces the identical digest on every platform, so tests can pin
/// known-answer layouts and detect drift without QgsLayoutExporter (whose
/// PNG path is environmentally fragile headless — see
/// docs/cartography/visual-regression.md). `spec` is not modified.
std::string structuralDigest( const Json::Value &spec );

} // namespace sicnu::agent::cartography
