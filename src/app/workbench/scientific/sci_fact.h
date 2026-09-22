/***************************************************************************
 * sci_fact.h — Scientific Inspector value objects (ADR 0172)
 *
 * The inspector's fact vocabulary. Facts are PROJECTIONS of authoritative
 * sources (DataManager / WorkspaceService / provable files) — this module
 * owns no store and never re-derives science. Every value carries an explicit
 * status so the UI can render unknown/inferred/assumed/conflict semantics
 * instead of pretending completeness.
 *
 * Status wire strings align with the harness fact vocabulary
 * (src/agent/harness/workflow_facts.h): observed / derived / assumed /
 * unknown. The presentation-level enum renames derived → Inferred (the
 * inspector speaks of inferred facts); mapping is explicit in
 * factStatusFromAuthoritative(). Conflict is a presentation-level state for
 * mutually disagreeing sources — it is never produced by this mapping alone.
 ***************************************************************************/
#pragma once

#include <QString>

#include "data/data_result.h"

#include <optional>
#include <vector>

namespace sicnu::app::sci
{

enum class FactStatus
{
  Observed,  ///< authoritative source states the value
  Inferred,  ///< closed documented rule applied to observed facts (harness "derived")
  Assumed,   ///< heuristic default, may be wrong
  Unknown,   ///< absent/unparseable — never fabricated
  Conflict,  ///< two sources disagree; both values kept in detail
};

/// Wire string for a status ("observed"/"inferred"/"assumed"/"unknown"/"conflict").
QString factStatusToWire( FactStatus status );

/// Maps an authoritative-source status string (harness fact_status vocabulary,
/// or our own wire strings) to a FactStatus. Unrecognized/empty input degrades
/// truthfully to FactStatus::Unknown — it never returns a stronger claim.
FactStatus factStatusFromAuthoritative( const QString &status );

/// Strict parse of a wire status; std::nullopt when the text is not part of
/// the vocabulary (used where silent degradation would be wrong, e.g. the
/// codec boundary).
std::optional<FactStatus> factStatusFromWireStrict( const QString &status );

/// One rendered fact row. `value` is human-readable; `detail` carries the
/// expert-mode raw representation; `explanation` carries the teaching-mode
/// sentence (renderers may fall back to a status-based default).
struct SciFact
{
    QString key;        ///< stable machine key, e.g. "passport.driver"
    QString label;      ///< user-facing label (中文)
    QString value;      ///< rendered value (empty when Unknown)
    FactStatus status = FactStatus::Unknown;
    QString source;     ///< provider id, e.g. "data.manager"
    QString detail;     ///< expert-mode raw evidence (optional)
    QString explanation;  ///< teaching-mode explanation (optional)

    friend bool operator==( const SciFact &, const SciFact & ) = default;
};

/// A typed, machine-readable finding. Codes are namespaced "sci.<domain>.<what>"
/// so agents can act on them without parsing prose.
struct SciFinding
{
    QString code;  ///< e.g. "sci.grid.no_geotransform"
    sicnu::data::DiagnosticSeverity severity = sicnu::data::DiagnosticSeverity::Info;
    QString message;  ///< user-facing text (中文)
    QString evidence;  ///< which observed fact backs this (empty = none)

    friend bool operator==( const SciFinding &, const SciFinding & ) = default;
};

/// Report of one inspector section. An unavailable service is typed state —
/// available=false plus a reason — never silently missing rows.
struct SciSectionReport
{
    QString id;     ///< stable id: "passport"|"quality"|"geometry"|"radiometry"|"temporal"|"preflight"|"verification"
    QString title;  ///< tab-facing title (中文)
    bool available = true;
    QString unavailableReason;
    std::vector<SciFact> facts;
    std::vector<SciFinding> findings;
    bool truncated = false;
    QString truncationNote;

    friend bool operator==( const SciSectionReport &, const SciSectionReport & ) = default;
};

} // namespace sicnu::app::sci
