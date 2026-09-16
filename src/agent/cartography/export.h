// src/agent/cartography/export.h
#pragma once

//
// Governed map export (Cartography Platform 9.0).
//
// The single sanctioned path from a compiled QgsPrintLayout to a delivered
// file. Compilation never auto-exports (MapSpec v5 contract): export is an
// explicit, typed, bounded action with an honest capability surface —
//
//   * png — QgsLayoutExporter::exportToImage; supports declared page
//     selection (ImageExportSettings.pages on this QGIS build);
//   * pdf — exportToPdf; exports ALL pages (PdfExportSettings has no page
//     selection here): a request that declares pages is REFUSED, never
//     silently reinterpreted;
//   * svg — exportToSvg; same all-pages restriction as pdf.
//
// Every export is ATOMIC: bytes land in a temp file in the target
// directory, the exporter result is checked, the digest (SHA-256) and size
// are computed on the written file, and only then does the rename happen —
// a failed export leaves no partial file, a successful one leaves a file
// whose content matches the reported digest.
//
// Declared resources are checked honestly: label font families that QGIS
// substituted (missing glyph/family) are reported as diagnostics on the
// result — the map still exports (QGIS substituted rendering), but the
// report says so.
//

#include <json/json.h>
#include <qgslayoutexporter.h>
#include <qgsprintlayout.h>

#include <QString>
#include <QStringList>

#include <functional>
#include <string>
#include <vector>

namespace sicnu::agent::cartography {

inline constexpr int kMaxAtlasExportPages = 512;

struct MapExportRequest
{
    std::string format = "png";   ///< "png" | "pdf" | "svg"
    double dpi = 300.0;           ///< 72..1200 (validated)
    std::string directory;        ///< target dir (created when missing)
    std::string file_name;        ///< optional base name (default: layout name)
    std::vector<int> pages;       ///< 0-based page indices; empty = all
};

struct MapExportResult
{
    bool ok = false;
    QString error;                ///< human-readable refusal/failure cause
    std::string path;             ///< final file path (absolute) when ok
    std::string sha256;           ///< digest of the written bytes when ok
    long long bytes = 0;          ///< written file size when ok
    QgsLayoutExporter::ExportResult exporterResult =
      QgsLayoutExporter::Success; ///< raw exporter verdict when a write ran
    /// Font-substitution diagnostics: one entry per layout label family
    /// QGIS could not honor exactly (bounded, deduped).
    QStringList fontDiagnostics;
};

/// Validates `request` against the layout (format vocabulary, dpi range,
/// page indices in range, format/page-selection capability). Empty returned
/// vector = executable request.
std::vector<std::string> validateMapExportRequest( const QgsPrintLayout *layout,
                                                   const MapExportRequest &request );

/// Executes an atomic governed export. Never throws; failures are reported
/// in the result. `layout` must outlive the call only.
MapExportResult exportMapLayout( QgsPrintLayout *layout, const MapExportRequest &request );

//
// Atlas page delivery (Cartography Production 11.0).
//
// Closes the atlas-guide gap: a compiled, atlas-enabled layout can now be
// DELIVERED per coverage feature through the same governed write path as
// the single-page export — every page is temp → verify (sha256, size) →
// rename, cancellable on every page boundary, bounded by kMaxAtlasExportPages
// (a runaway coverage layer is a typed refusal, not an unbounded loop).
//
// Honest capability surface: png only. The QGIS build at hand exports
// pdf/svg atlas deliveries as single flattened documents; a declared
// png-format requirement keeps per-page semantics. A disabled or
// coverage-less atlas is a typed refusal — never a silent single-page
// fallback (that is exportMapLayout's job, at the caller's discretion).
//

struct MapAtlasExportRequest
{
    std::string format = "png";            ///< atlas delivery is png-only here
    double dpi = 300.0;                    ///< 72..1200 (validated)
    std::string directory;                 ///< target dir (created when missing)
    std::string file_name;                 ///< base name (required, bare)
    int max_pages = kMaxAtlasExportPages;  ///< runaway guard, clamped ≥ 1
};

/// One delivered atlas page: final file facts plus the coverage feature
/// provenance (stringified feature id, the raw filename-expression label,
/// and the feature's geometry bounding box in the coverage layer CRS).
struct MapAtlasPage
{
    std::string path;        ///< final absolute path (delivered)
    std::string file_name;   ///< bare name inside directory
    std::string sha256;
    long long bytes = 0;
    std::string feature_id;
    std::string label;
    Json::Value extent;      ///< {x_min,y_min,x_max,y_max,crs} | null
};

struct MapAtlasExportResult
{
    bool ok = false;
    bool cancelled = false;             ///< cancel probe fired on a page boundary
    QString error;                      ///< human-readable refusal/failure cause
    std::vector<MapAtlasPage> pages;    ///< delivered pages (only when fully ok)
    std::vector<std::string> problems;  ///< validation refusals (empty = attempted)
    QgsLayoutExporter::ExportResult exporterResult = QgsLayoutExporter::Success;
};

/// Validates an atlas delivery request (format vocabulary, dpi, bare base
/// name, atlas enabled with a coverage layer). Empty returned vector =
/// executable request.
std::vector<std::string> validateMapAtlasExportRequest( const QgsPrintLayout *layout,
                                                        const MapAtlasExportRequest &request );

/// Executes the per-feature governed delivery. Every delivered page passed
/// through the temp→verify→rename path; on cancellation or mid-flight
/// failure ALL delivered pages are removed again, so a non-ok result never
/// leaves atlas files behind. Never throws. `layout` must outlive the call.
MapAtlasExportResult exportMapAtlas( QgsPrintLayout *layout, const MapAtlasExportRequest &request,
                                     const std::function<bool()> &cancelProbe = {} );

/// Compact JSON projection for tool responses.
Json::Value mapExportResultToJson( const MapExportResult &result );

} // namespace sicnu::agent::cartography
