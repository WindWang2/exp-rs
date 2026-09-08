/***************************************************************************
 * help_id.h — stable Help ID grammar for the Unified Help System 6.0
 *
 * One namespace addresses every helpable topic across all surfaces (tooltip,
 * What's This, disabled-control explanation, inline parameter help, workbench
 * guidance, Help Center, F1, CLI, MCP/Pi summaries, generated Markdown):
 *
 *   command.<domain>.<name>            command.layer.toggleEditing
 *   operator.<domain>.<name>           operator.rs.sar_speckle
 *   parameter.<operator id>.<param>    parameter.rs.sar_speckle.kernelSize
 *   workbench.<name>                   workbench.classification
 *   diagnostic.<family>.<code>         diagnostic.harness.dataset_not_found
 *   concept.<domain>.<name>            concept.dataset.spatial_leakage
 *   template.<domain>.<name>           template.report.scientific
 *   shortcut.<name>                    shortcut.map.zoomIn
 *
 * Minimum two segments (kind + name); commands/parameters are naturally
 * longer because the source registry id is embedded.
 *
 * IDs are English and immutable once released; segments are [A-Za-z0-9_]
 * (case-sensitive, verbatim from the authoritative registry — e.g.
 * "command.layer.toggleEditing" mirrors the registry id, schema parameter
 * names keep their authored camelCase). Diagnostic ids normalize SCREAMING_CASE
 * machine codes to snake_case via normalizeCode(). Renames go through alias
 * registration (deprecated id → target id), never by mutating an id.
 ***************************************************************************/
#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace sicnu::help
{

/// Descriptor kinds; the id prefix selects the kind (single source of truth).
enum class HelpKind
{
    Command,
    Operator,
    Parameter,
    Workbench,
    Diagnostic,
    Concept,
    Template,
    Shortcut,
};

/// Origin family of a machine failure code, for diagnostic id mapping.
enum class DiagnosticFamily
{
    Harness,    ///< sicnu::agent::harness HarnessError stable codes
    Operator,   ///< sicnu::operators RSOperatorError::ErrorCode
    GeoSpatial, ///< sicnu::geospatial GeoError codes
    Dataset,    ///< dataset quality / leakage finding codes
    Preflight,  ///< scientific preflight / MapSpec check codes
    Rs,         ///< curated remote-sensing issue pages (e.g. sar geometry)
};

/// Stable family name ↔ enum mapping shared by the catalog and content store
/// ("harness" | "operator" | "geospatial" | "dataset" | "preflight" | "rs").
QString diagnosticFamilyName( DiagnosticFamily family );
std::optional<DiagnosticFamily> diagnosticFamilyFromName( const QString &name );

/// Stable string for a HelpKind ("command", "operator", ...). Header-inline so
/// any consumer can render kinds without a heavier dependency.
inline const char *helpKindName( HelpKind kind )
{
    switch ( kind ) {
    case HelpKind::Command:
        return "command";
    case HelpKind::Operator:
        return "operator";
    case HelpKind::Parameter:
        return "parameter";
    case HelpKind::Workbench:
        return "workbench";
    case HelpKind::Diagnostic:
        return "diagnostic";
    case HelpKind::Concept:
        return "concept";
    case HelpKind::Template:
        return "template";
    case HelpKind::Shortcut:
        return "shortcut";
    }
    return "concept";
}

std::optional<HelpKind> helpKindFromName( const QString &name );

class HelpId
{
  public:
    /// True when @p id matches the grammar: 3+ dot-separated segments, each
    /// [a-z0-9_]+, first segment a known kind name. Parameter ids additionally
    /// carry the operator id with ':' replaced by '_'.
    static bool isValid( const QString &id );

    /// Kind implied by the id's first segment; nullopt when invalid/unknown.
    static std::optional<HelpKind> kindOf( const QString &id );

    /// Derived parameter help id: operator "rs:sar_speckle" + "kernelSize" →
    /// "parameter.rs.sar_speckle.kernelSize". The parameter name is preserved
    /// verbatim from the schema; derivation (not authoring) keeps parameter
    /// coverage mechanically checkable against operator schemas.
    static QString parameterId( const QString &operatorId, const QString &paramName );

    /// Diagnostic id for an origin failure code: family Harness +
    /// "DATASET_NOT_FOUND" → "diagnostic.harness.dataset_not_found". Dotted
    /// dataset/preflight codes keep their dots ("label.unknown_class" →
    /// "diagnostic.dataset.label.unknown_class").
    static QString diagnosticId( DiagnosticFamily family, const QString &code );

    /// "rs:sar_speckle" → "rs.sar_speckle" (operator id → id segments).
    static QString domainForOperatorId( const QString &operatorId );

    /// Snake/kebab → snake normalizer used by diagnosticId ("DATASET_NOT_FOUND",
    /// "dataset-not-found", "datasetNotFound" all normalize identically).
    static QString normalizeCode( const QString &code );

  private:
    static QStringList splitSegments( const QString &id );
};

} // namespace sicnu::help
