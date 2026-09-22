#pragma once

// suitability_teaching.h — the human-readable teaching narrative over a
// report ("why is this scene set unfit for my experiment?").
//
// Both functions are pure: they only read the report and derive English
// narrative lines (the repository's documentation language; a GUI layer owns
// any future i18n). The narrative is never stored and never contradicts the
// JSON: every paragraph carries the criterion's exact level string, and an
// unsuitable narrative names every gap id. Consistency with the serialized
// report is locked by test_suitability_teaching.

#include "suitability_report.h"
#include "suitability_types.h"

#include <QString>
#include <QStringList>

namespace sicnu::suitability
{

/// The full teaching explanation: an overview line first (overall level,
/// total gap count, the most pressing 1-3 problems), then one paragraph per
/// criterion in the report's canonical id order. The overview is
/// lines.first(); the paragraph for criteria().at(i) is lines.at(i + 1).
QStringList teachingExplanation( const SuitabilityReport &report );

/// One criterion's standalone paragraph ("why not suitable") — the exact
/// text teachingExplanation uses for that criterion, for GUI/agent reuse.
/// Opens with the human criterion name and the level; Unsuitable/Marginal
/// quote every gap (description and machine id); applicable Unknown quotes
/// the notes as reasons ("cannot judge, because ..."); a not-applicable
/// criterion gets one sentence saying it was skipped and why.
QString explainCriterion( const SuitabilityCriterion &criterion );

} // namespace sicnu::suitability
