// src/agent/harness/tool_shortlist.h
#pragma once

//
// Scientific Workflow Compiler 10.0 (ADR 0149): knowledge token budgeting.
//
// The capability manifest (64 KiB) and error catalog (8 KiB) already have
// hard budgets (capability_catalog.h). This module adds the missing piece:
// a DETERMINISTIC, provenance-carrying shortlist of tools/operators for one
// intent — so a prompt never needs all 111 operators, and every inclusion
// says WHY it is there.
//
// Scoring is a closed rule table (intent membership, family match, tag
// match, name/description evidence), order is deterministic (score desc,
// then id), the rendered page is bounded by a hard byte budget with visible
// truncation — no silent cuts, no model calls.
//

#include <json/json.h>
#include <string>

#include "harness_error.h"

namespace sicnu::agent::harness {

/// Hard shortlist page budget in bytes (the error-catalog convention).
static constexpr size_t kShortlistBudgetBytes = 8 * 1024;
/// Maximum page size regardless of budget (drift anchor).
static constexpr int kShortlistMaxLimit = 24;

/// Deterministic shortlist for one intent and/or family/tag filters.
/// Rendered shape:
/// {
///   "intent": ..., "families": [...], "tags": [...],
///   "budget_bytes": <hard cap>, "rendered_bytes": <measured>,
///   "total_unfiltered": N, "truncated": bool,
///   "items": [ { "id", "surface": "spatial_tool"|"operator",
///                "summary", "score",
///                "why_included": [ {reason, detail} ] } ]
/// }
/// Empty result is honest (items: []) — never a padded guess.
Json::Value toolShortlist( const std::string &intent, const Json::Value &families,
                           const Json::Value &tags, int limit );

/// The knowledge-budget report: every bounded knowledge surface, its hard
/// budget, and its CURRENT measured size (deterministic for the loaded data).
/// {surfaces: [{surface, budget_bytes, measured_bytes, within_budget}],
///  all_within_budget: bool}.
Json::Value knowledgeBudgetReport();

/// Registers `harness:tool_shortlist` and `harness:knowledge_budget`.
/// Idempotent.
void registerToolShortlistTools();

} // namespace sicnu::agent::harness
