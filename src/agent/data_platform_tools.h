// data_platform_tools.h — MCP surface for the Dataset/Experiment platform
// (goal 7.0 §A). Thin, stateless adapters over the authoritative
// DatasetStore/ExperimentStore and the existing library engines (split,
// leakage, comparison, reproduction bundles). No business logic lives here:
// every handler opens the referenced store, calls one library API (or a
// short bounded composition of them), and projects a paged, bounded
// QVariantMap for the MCP envelope.
//
// Tool ids are namespaced `dataset:`, `experiment:`, `reproducibility:`
// (distinct from the legacy `data:` geometry probes). Both layers stay
// read-only except `reproducibility:export` (writes an explicit output
// directory) and `dataset:leakage_audit` in run mode (persists the audit
// evidence its report cites).
//
// Provenance keys this surface reads on SampleRecord::provenance() (the
// free-form document): "scene_id", "parent_polygon_id", "source_object_id",
// "parent_sample_id", "pseudo_derived", "event_group", "content_digest".
// Absent keys mean unknown evidence — the audit reports the gap, it never
// invents a value.
#pragma once

#include <QString>
#include <QVariantMap>

#include <QList>

namespace sicnu::agent
{

struct DataPlatformToolInput
{
    const char *name;
    const char *type;
    const char *description;
    bool required;
};

struct DataPlatformToolDef
{
    const char *name;
    const char *description;
    QList<DataPlatformToolInput> inputs;
};

/// Tool catalog for tools/list discovery (dataset:/experiment:/reproducibility:).
const QList<DataPlatformToolDef> &dataPlatformToolDefs();

/// True when @p toolId belongs to this surface ("dataset:x", "experiment:x",
/// "reproducibility:x").
bool isDataPlatformTool( const QString &toolId );

/// Dispatches one tool call. Errors are thrown as std::runtime_error with a
/// user-readable message (the MCP layer converts them to isError results);
/// store/library diagnostics ride inside the result as a `diagnostics`
/// array when a call fails softly (e.g. validate reporting failures).
QVariantMap handleDataPlatformTool( const QString &toolId, const QVariantMap &arguments );

} // namespace sicnu::agent
