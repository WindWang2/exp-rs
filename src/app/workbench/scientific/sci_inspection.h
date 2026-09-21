/***************************************************************************
 * sci_inspection.h — Scientific Inspection aggregate + versioned codec
 *
 * One inspection is the complete scientific projection of one inspection
 * target at one moment. It is a pure value: produced by the view-model
 * (SciInspectorModel), consumed by widgets (presentation) and by the codec
 * (machine-readable exit for future agent tools).
 *
 * Envelope: { "kind": "sci_inspection", "schema_version": "1.0", ... }.
 * The codec is strict at the boundary: foreign kinds/versions are refused
 * with typed diagnostics, never guessed (same contract as spatial_contracts).
 * Serialization is deterministic (sorted JSON keys) so agents can replay.
 ***************************************************************************/
#pragma once

#include "app/workbench/scientific/sci_fact.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QString>

#include <vector>

namespace sicnu::app::sci
{

inline constexpr const char *kSciInspectionKind = "sci_inspection";
inline constexpr const char *kSciInspectionSchemaVersion = "1.0";

/// What was inspected. kind is a stable wire token: "asset" | "layer" |
/// "dataset" | "experiment_run" | "none". Slice B's resolver produces it.
struct SciTargetRef
{
    QString kind;
    QString id;
    QString displayLabel;

    friend bool operator==( const SciTargetRef &, const SciTargetRef & ) = default;
};

struct ScientificInspection
{
    QString schemaVersion = QString::fromLatin1( kSciInspectionSchemaVersion );
    QDateTime generatedAtUtc;  ///< must be valid + UTC before serialization
    SciTargetRef target;
    std::vector<SciSectionReport> sections;

    friend bool operator==( const ScientificInspection &, const ScientificInspection & ) = default;
};

/// Deterministic JSON projection. `inspection.generatedAtUtc` must be a valid
/// moment; it is normalized to UTC in the envelope.
QJsonDocument inspectionToJson( const ScientificInspection &inspection );

/// Strict parse. Fails with typed diagnostics ("sci.codec.*") on foreign
/// kind/version, missing fields, unparseable timestamps or unknown wire
/// statuses — the boundary never guesses.
sicnu::data::Result<ScientificInspection> inspectionFromJson( const QJsonDocument &doc );

} // namespace sicnu::app::sci
