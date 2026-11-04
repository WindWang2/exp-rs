// src/app/shell/operator_preflight.h — R25-D pre-run preflight line
#pragma once

#include <QString>

#include <string>

namespace sicnu::app
{

/// One-line pre-run preflight summary for the task panel's estimate seam.
///
/// Derives EXCLUSIVELY from the authoritative AlgorithmMetaStore sidecars
/// (data/processing/algorithm_meta/*.json — the same knowledge agents select
/// algorithms with). Operators without a sidecar return an empty string:
/// absence stays honest, no heuristics invent facts. GPU wording follows the
/// #707 drift rule — an undeclared `gpu` never surfaces as a confident
/// CPU-only claim. Notes are surrogate-safe truncated (bounded line).
QString operatorPreflightSummary( const std::string &operatorId );

} // namespace sicnu::app
