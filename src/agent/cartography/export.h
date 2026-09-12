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

#include <string>
#include <vector>

namespace sicnu::agent::cartography {

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

/// Compact JSON projection for tool responses.
Json::Value mapExportResultToJson( const MapExportResult &result );

} // namespace sicnu::agent::cartography
