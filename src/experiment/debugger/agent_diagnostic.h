// agent_diagnostic.h — exp.diag.v1 adapter for planner consumption
// (RS14-06, ADR 0174). Slice F.
//
// One machine-readable object per analysis outcome, aligned to the platform
// diagnostic envelope (src/runtime/observability/diagnostic_report.h — the
// SAME schema, codes carried verbatim, no second taxonomy) plus a closed
// `details` extension carrying the divergence semantics a planner needs to
// replan: kind, confidence, exact step ids, differing parameter keys.
#pragma once

#include "../../data/data_result.h"
#include "first_divergence.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace sicnu::experiment::debugger
{

/// Closed diagnostic codes for the debugger's outcomes.
inline constexpr char kDiagNoDivergence[] = "experiment.debugger.no_divergence";
inline constexpr char kDiagFirstDivergence[] = "experiment.debugger.first_divergence";
inline constexpr char kDiagAlternativePathOnly[] = "experiment.debugger.alternative_path_only";
inline constexpr char kDiagInsufficientEvidence[] = "experiment.debugger.insufficient_evidence";
inline constexpr char kDiagNonComparable[] = "experiment.debugger.non_comparable";
inline constexpr char kDiagUnknownRun[] = "experiment.debugger.unknown_run";
inline constexpr char kDiagEvidenceTooLarge[] = "experiment.debugger.evidence_too_large";
inline constexpr char kDiagMalformedEvidence[] = "experiment.debugger.malformed_evidence";

struct AgentDiagnostic
{
    QString code;            ///< one of the kDiag* closed set
    QString component = QStringLiteral( "experiment.debugger" );
    QString studentRunId;
    QString referenceRunId;
    /// none | manual | transient | unknown (exp.diag.v1 vocabulary)
    QString recoverability;
    QString suggestedAction;
    QStringList causeChain;
    /// Closed details extension (divergence_kind, confidence, step ids,
    /// differing parameter keys when available).
    QJsonObject details;

    /// exp.diag.v1-aligned JSON object (schema marker included).
    QJsonObject toJson() const;
    bool operator==( const AgentDiagnostic & ) const = default;
};

class AgentDiagnosticAdapter
{
  public:
    /// Adapter over a completed analysis.
    static AgentDiagnostic forReplan( const FirstDivergenceReport &report );

    /// Adapter over a typed analysis FAILURE (unknown run, oversized or
    /// malformed evidence) — the planner sees the same envelope shape for
    /// failures as for outcomes.
    static AgentDiagnostic forFailure( const QVector<sicnu::data::Diagnostic> &diagnostics,
                                       const QString &studentRunId );
};

} // namespace sicnu::experiment::debugger
