// src/agent/cartography/produce.cpp
#include "produce.h"

#include "../mapspec/mapspec.h"
#include "../mapspec/mapspec_compiler.h"
#include "composition.h"
#include "export.h"
#include "export_manifest.h"
#include "quality.h"

#include <qgis.h>
#include <qgslayoutatlas.h>
#include <qgslayoutitem.h>
#include <qgslayoutitemlegend.h>
#include <qgslayoutitemmap.h>
#include <qgslayoutpagecollection.h>
#include <qgsproject.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>

#include <algorithm>

namespace sicnu::agent::cartography {

namespace {

/// Progress through the chain: stage fractions are fixed so a reporter can
/// render one coherent bar; every report returning false cancels.
bool report( const ProduceReporter &reporter, const char *stage, double progress,
             const std::string &detail )
{
    if ( !reporter )
        return true;
    return reporter( stage, std::clamp( progress, 0.0, 1.0 ), detail );
}

ProduceResult refused( const char *code, const QString &message )
{
    ProduceResult result;
    result.error_code = code;
    result.error = message.toStdString();
    return result;
}

QStringList environmentFontDiagnostics( const QStringList &fontDiagnostics )
{
    // Bounded the same way the exporter bounds its diagnostics (16).
    QStringList bounded = fontDiagnostics;
    if ( bounded.size() > 16 )
        bounded = bounded.mid( 0, 16 );
    return bounded;
}

/// SHA-256 + size of a delivered file, recomputed from disk (CARTO-1: the
/// multi-page manifest describes every page, and exportMapLayout's result
/// struct reports only the primary artifact).
bool sha256OfPath( const QString &path, std::string *sha256, long long *bytes )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
        return false;
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    long long total = 0;
    while ( true )
    {
        const QByteArray chunk = file.read( 1 << 20 );
        if ( chunk.isEmpty() )
            break;
        hash.addData( chunk );
        total += chunk.size();
    }
    file.close();
    *sha256 = std::string( hash.result().toHex().constData() );
    *bytes = total;
    return total > 0 && !sha256->empty();
}

/// Production hazard guard (observed as a REAL hang in this environment):
/// QgsLayoutItemLegend::paint loops forever when an auto-update legend
/// mirrors an EMPTY layer set — linked to a layer-less map frame, or
/// unlinked inside an empty project. The check runs on the DECLARED
/// legends BEFORE the repair loop (repair-added furniture is engine
/// canonical; its render safety is a host concern, covered by the render
/// gate). Legends taken out of auto-update (columns > 1) are always safe.
std::string legendAutoUpdateHazard( const Json::Value &spec )
{
    if ( !spec.isObject() || !spec.isMember( "legends" ) || !spec["legends"].isArray() )
        return std::string();
    QgsProject *project = QgsProject::instance();
    const bool projectHasLayers = project ? !project->mapLayers().isEmpty() : false;
    for ( const auto &legend : spec["legends"] )
    {
        if ( !legend.isObject() || !legend.isMember( "id" ) || !legend["id"].isString() )
            continue;
        if ( legend.isMember( "columns" ) && legend["columns"].isIntegral() &&
             legend["columns"].asInt() > 1 )
            continue; // explicit columns take the legend out of auto-update
        bool linkedEmpty = false;
        if ( legend.isMember( "map_ref" ) && legend["map_ref"].isString() )
        {
            const std::string mapRef = legend["map_ref"].asString();
            if ( spec.isMember( "map_frames" ) && spec["map_frames"].isArray() )
                for ( const auto &frame : spec["map_frames"] )
                {
                    if ( !frame.isObject() || !frame.isMember( "id" ) ||
                         !frame["id"].isString() || frame["id"].asString() != mapRef )
                        continue;
                    linkedEmpty = !( frame.isMember( "layers" ) &&
                                     frame["layers"].isArray() && !frame["layers"].empty() );
                    break;
                }
        }
        const bool unlinkedEmpty =
          ( !legend.isMember( "map_ref" ) || !legend["map_ref"].isString() ) &&
          !projectHasLayers;
        if ( linkedEmpty || unlinkedEmpty )
            return "legend '" + legend["id"].asString() +
                   "' is auto-update and mirrors an empty layer set ("
                   "linked map without layers, or an empty project); "
                   "QgsLayoutItemLegend::paint loops forever in this configuration. "
                   "Fix: add data layers to the project/map, or declare legend "
                   "columns > 1 to take it out of auto-update.";
    }
    return std::string();
}
} // namespace

ProduceResult produceMap( const ProduceRequest &request, const ProduceReporter &reporter )
{
    // --- request shape -------------------------------------------------------
    if ( !request.mapspec.isObject() )
        return refused( "INVALID_PARAMETER", QStringLiteral( "mapspec (object) is required" ) );
    if ( request.format != "png" && request.format != "pdf" && request.format != "svg" )
        return refused( "INVALID_PARAMETER", QStringLiteral( "format must be png|pdf|svg" ) );
    if ( !( request.dpi >= 72.0 && request.dpi <= 1200.0 ) )
        return refused( "INVALID_PARAMETER", QStringLiteral( "dpi must be within [72, 1200]" ) );
    if ( request.directory.empty() )
        return refused( "INVALID_PARAMETER", QStringLiteral( "directory is required" ) );
    if ( !request.file_name.empty() &&
         ( request.file_name.find( '/' ) != std::string::npos ||
           request.file_name.find( '\\' ) != std::string::npos ||
           request.file_name == "." || request.file_name == ".." ||
           request.file_name.rfind( "..", 0 ) == 0 ) )
        return refused( "INVALID_PARAMETER", QStringLiteral( "file_name must be a bare name" ) );

    const int maxRepairIterations =
      std::clamp( request.max_repair_iterations, 1, 10 );

    ProduceResult result;

    // --- upgrade + validate (typed refusal before any side effect) ----------
    if ( !report( reporter, "validate", 0.05, "upgrading and validating MapSpec" ) )
    {
        result.error_code = "PRODUCE_CANCELLED";
        result.error = "cancelled before validation";
        return result;
    }
    const Json::Value spec = mapspec::upgradeMapSpec( request.mapspec );
    const std::vector<std::string> validationProblems = mapspec::validateMapSpec( spec );
    if ( !validationProblems.empty() )
    {
        Json::Value problems( Json::arrayValue );
        for ( const auto &problem : validationProblems )
            problems.append( problem );
        ProduceResult invalid = refused( "VALIDATION_FAILED", QStringLiteral(
            "MapSpec validation failed (see problems)" ) );
        invalid.quality = problems;
        return invalid;
    }

    // --- conditions + composition + compile ---------------------------------
    if ( !report( reporter, "compose", 0.2, "resolving conditions and composition" ) )
        return refused( "PRODUCE_CANCELLED", QStringLiteral( "cancelled before composition" ) );
    Json::Value working = spec;
    result.composition = resolveCompositionPass( working );

    QString compileError;
    QgsPrintLayout *layout = mapspec::MapSpecCompiler::compile( working, &compileError );
    Json::Value quality = preflightMapSpec( working );
    if ( !layout )
    {
        ProduceResult failed = refused( "COMPILE_FAILED", compileError );
        failed.quality = quality;
        return failed;
    }
    result.structural_digest = structuralDigest( working );
    result.provenance = composeProvenance( working );
    result.quality = quality;

    // Declared-legend render hazard (see above) fires BEFORE any repair so
    // the refusal names the author's declaration, not repair-added furniture.
    const std::string legendHazard = legendAutoUpdateHazard( working );
    if ( !legendHazard.empty() )
        return refused( "UNSAFE_LEGEND_AUTO_UPDATE", QString::fromStdString( legendHazard ) );

    // --- bounded repair loop (tool-identical) --------------------------------
    if ( !quality["passed"].asBool() )
    {
        int iterations = 0;
        while ( iterations < maxRepairIterations && !quality["passed"].asBool() )
        {
            // R21-D: the fraction used to be 0.3 + 0.1*iterations, which
            // passed the export stage's fixed 0.6 from the 4th pass on —
            // a progress bar that jumps 70%→100% and falls back to 60%.
            // The repair stage now spans [0.30, 0.55]: the whole loop
            // stays strictly below the export stage's floor and the bar
            // remains monotone across every pass.
            const double repairProgress =
                maxRepairIterations > 1
                  ? 0.30 + 0.25 * ( iterations / static_cast<double>( maxRepairIterations - 1 ) )
                  : 0.30;
            if ( !report( reporter, "repair", std::min( repairProgress, 0.55 ), "repair pass" ) )
                return refused( "PRODUCE_CANCELLED", QStringLiteral( "cancelled during repair" ) );
            Json::Value passLedger;
            const int repairs = repairMapSpecWithLedger( working, quality, &passLedger );
            if ( repairs == 0 )
                break;
            result.repairs_applied += repairs;
            for ( const auto &entry : passLedger )
            {
                Json::Value record = entry;
                record["pass"] = iterations + 1;
                result.repair_ledger.append( record );
            }
            ++iterations;
            resolveCompositionPass( working );
            layout = mapspec::MapSpecCompiler::compile( working, &compileError );
            quality = preflightMapSpec( working );
            if ( !layout )
            {
                ProduceResult failed = refused( "COMPILE_FAILED", compileError );
                failed.quality = quality;
                return failed;
            }
        }
        result.repair_iterations = iterations;
        result.quality = quality;
        result.structural_digest = structuralDigest( working );
        if ( request.require_preflight_pass && !quality["passed"].asBool() )
        {
            ProduceResult notPassed =
              refused( "PREFLIGHT_NOT_PASSED",
                       QStringLiteral( "preflight still reports findings after %1 bounded "
                                       "repair iteration(s); delivery refused "
                                       "(require_preflight_pass)" )
                         .arg( iterations ) );
            notPassed.quality = quality;
            notPassed.repair_iterations = iterations;
            return notPassed;
        }
    }

    result.mapspec = working;

    // R14-D review2 N1: refs the compiler accepted only through a spec-level
    // declaration (no backing layer) render nothing. Appended AFTER the
    // repair loop from the FINAL layout — repair passes reassign the whole
    // quality document, and the MAP_LAYER_UNREFERENCED repair deliberately
    // attaches such ids, so a pre-repair append would be wiped exactly in
    // the flow it exists for.
    {
        const QVariant unbacked =
          layout->customProperty( QStringLiteral( "sicnu/declaredUnbackedLayerRefs" ) );
        if ( !unbacked.isNull() )
        {
            Json::Value issue( Json::objectValue );
            issue["code"] = "MAP_DECLARED_LAYER_UNBACKED";
            issue["severity"] = "warning";
            issue["message"] = "frame layer reference(s) accepted via spec declaration with "
                               "no backing layer; the composition renders without them";
            issue["repairable"] = false;
            Json::Value refs( Json::arrayValue );
            const QStringList list = unbacked.toStringList();
            for ( const QString &ref : list )
                refs.append( ref.toStdString() );
            issue["refs"] = refs;
            Json::Value &qualityFinal = result.quality;
            if ( !qualityFinal.isMember( "issues" ) || !qualityFinal["issues"].isArray() )
                qualityFinal["issues"] = Json::Value( Json::arrayValue );
            qualityFinal["issues"].append( issue );
        }
    }

    // --- export --------------------------------------------------------------
    if ( !report( reporter, "export", 0.6, "exporting" ) )
        return refused( "PRODUCE_CANCELLED", QStringLiteral( "cancelled before export" ) );
    QDir dir( QString::fromStdString( request.directory ) );
    const QString baseName = request.file_name.empty()
                               ? QString::fromStdString( working["layout_name"].asString() )
                               : QString::fromStdString( request.file_name );

    // Atlas delivery activates for png only: the per-feature page semantics
    // exist for images. A pdf/svg request on an atlas-enabled layout behaves
    // exactly like cartography:export — one static document of all pages —
    // which is the honest capability surface (the manifest records mode
    // "single" for those).
    // Atlas completeness is structural: refuse before the delivery starts
    // (repair cannot add a coverage layer).
    if ( layout->atlas() && layout->atlas()->enabled() &&
         layout->atlas()->coverageLayer() == nullptr )
        return refused( "EXPORT_FAILED", QStringLiteral(
            "layout atlas is enabled but has no coverage layer; declare "
            "page.atlas.coverage_layer and re-compose" ) );

    // Repair-added furniture can itself introduce the render hazard (the
    // add_legend repair links to the main map frame): re-check AFTER the
    // quality/structural gates so those refusals keep their codes, and so
    // produce refuses instead of hanging the executor in legend paint.
    const std::string postRepairHazard = legendAutoUpdateHazard( working );
    if ( !postRepairHazard.empty() )
        return refused( "UNSAFE_LEGEND_AUTO_UPDATE",
                        QString::fromStdString( postRepairHazard ) );

    const bool atlasMode = request.format == "png" && layout->atlas() &&
                           layout->atlas()->enabled() &&
                           layout->atlas()->coverageLayer() != nullptr;
    QString manifestPath;
    if ( atlasMode )
    {
        MapAtlasExportRequest atlasRequest;
        atlasRequest.format = "png";
        atlasRequest.dpi = request.dpi;
        atlasRequest.directory = request.directory;
        atlasRequest.file_name = baseName.toStdString();
        MapAtlasExportResult atlas = exportMapAtlas(
          layout, atlasRequest,
          [ &reporter ] { return !report( reporter, "export", 0.7, "atlas page" ); } );
        if ( atlas.cancelled )
            return refused( "PRODUCE_CANCELLED", atlas.error );
        if ( !atlas.ok )
        {
            ProduceResult failed = refused( "EXPORT_FAILED", atlas.error );
            failed.quality = quality;
            return failed;
        }
        result.mode = "atlas";
        result.page_count = static_cast<int>( atlas.pages.size() );
        // Chainable artifact path when no manifest is requested: the first
        // delivered page (the manifest is the canonical handle otherwise).
        if ( !atlas.pages.empty() )
            result.artifact_path = atlas.pages.front().path;
        QStringList deliveredPaths;
        for ( const MapAtlasPage &page : atlas.pages )
            deliveredPaths << QString::fromStdString( page.path );
        // The manifest carries the authoritative page records; the envelope
        // names the manifest (the single file every delivery can be
        // re-verified from).
        ExportManifest manifest;
        manifest.layout_name = working["layout_name"].asString();
        manifest.format = "png";
        manifest.dpi = request.dpi;
        manifest.provenance = result.provenance;
        manifest.structural_digest = result.structural_digest;
        for ( const MapAtlasPage &page : atlas.pages )
        {
            ExportManifestPage entry;
            entry.file_name = page.file_name;
            entry.sha256 = page.sha256;
            entry.bytes = page.bytes;
            entry.feature_id = page.feature_id;
            entry.label = page.label;
            entry.extent = page.extent;
            manifest.pages.push_back( entry );
        }
        Json::Value environment( Json::objectValue );
        environment["qt_runtime"] = qVersion();
        environment["qgis_version"] = Qgis::version().toStdString();
        environment["os"] = QSysInfo::prettyProductName().toStdString();
        // exportMapAtlas keeps per-page font diagnostics out of scope: the
        // substitutions the layout declares are page-independent, reported
        // by the single-page path (see below).
        environment["font_substitutions"] = Json::Value( Json::arrayValue );
        manifest.environment = environment;

        if ( request.write_manifest )
        {
            if ( !report( reporter, "manifest", 0.9, "writing manifest" ) )
            {
                // The pages were already delivered: a cancel at the last
                // commit step rolls them back like any other failure.
                for ( const QString &path : deliveredPaths )
                    QFile::remove( path );
                return refused( "PRODUCE_CANCELLED",
                                QStringLiteral( "cancelled before manifest" ) );
            }
            std::string manifestError;
            if ( !writeExportManifest( request.directory, baseName.toStdString(), "png", manifest,
                                       &manifestError ) )
            {
                ProduceResult failed =
                  refused( "MANIFEST_FAILED", QString::fromStdString( manifestError ) );
                // The pages were already delivered by exportMapAtlas; the
                // manifest was the last commit step, so a failure here rolls
                // THEM back too — the directory keeps its pre-call content.
                for ( const QString &path : deliveredPaths )
                    QFile::remove( path );
                return failed;
            }
            result.manifest = manifest.toJson();
            manifestPath = QDir( QString::fromStdString( request.directory ) )
                             .filePath( baseName + ".png.manifest.json" );
            result.manifest_path = QFileInfo( manifestPath ).absoluteFilePath().toStdString();
        }
        result.ok = true;
        return result;
    }

    MapExportRequest exportRequest;
    exportRequest.format = request.format;
    exportRequest.dpi = request.dpi;
    exportRequest.directory = request.directory;
    exportRequest.file_name = baseName.toStdString();
    // R21-D: the cancel probe for the single/multi-page export. NOTE: the
    // probe must NOT route through report(): a report() call also emits a
    // progress event, so every page-boundary poll would push a detail line
    // ("exporting") that a non-cancelled export never asked for. The
    // operator seam already passes a pure isCancelled() probe; here the
    // reporter doubles as the cancel channel only through its return value,
    // so the probe reports progress ONLY on the cancel branch.
    bool exportCancelled = false;
    const MapExportResult exported =
        exportMapLayout( layout, exportRequest,
                         [ &reporter, &exportCancelled ]() {
                             if ( report( reporter, "export", 0.6, "export cancelled" ) )
                                 return false; // keep going
                             exportCancelled = true;
                             return true;
                         } );
    result.font_diagnostics = exported.fontDiagnostics;
    result.exporterResult = exported.exporterResult;
    if ( exported.cancelled )
        return refused( "PRODUCE_CANCELLED",
                        exported.error.isEmpty() ? QStringLiteral( "cancelled during export" )
                                                 : exported.error );
    if ( !exported.ok )
    {
        ProduceResult failed = refused( "EXPORT_FAILED", exported.error );
        failed.quality = quality;
        return failed;
    }
    // CARTO-1: page_count reports the REAL delivery size — a multi-page
    // "single" produce used to claim 1 page while the layout has N. Raster
    // deliveries are one file per page (pagePaths); a pdf carries every page
    // inside one flattened document, so its page count is the layout's.
    const int layoutPages = std::max( 1, layout->pageCollection()->pageCount() );
    result.mode = "single";
    result.page_count = ( request.format == "pdf" )
                          ? layoutPages
                          : std::max( 1, static_cast<int>( exported.pagePaths.size() ) );
    result.artifact_path = exported.path;

    // Every delivered file, for the manifest and the failure rollbacks.
    QStringList deliveredPaths;
    std::vector<ExportManifestPage> pageEntries;
    if ( request.format != "pdf" && exported.pagePaths.size() > 1 )
    {
        // CARTO-1: a multi-page raster delivery is N deterministic files; the
        // manifest lists and hashes exactly the set exportMapLayout delivered
        // this run — re-walking the directory would re-manifest stale page
        // files left over from earlier deliveries as fresh pages.
        bool complete = true;
        for ( const std::string &delivered : exported.pagePaths )
        {
            const QString pagePath = QString::fromStdString( delivered );
            std::string sha;
            long long bytes = 0;
            if ( !QFile::exists( pagePath ) || !sha256OfPath( pagePath, &sha, &bytes ) )
            {
                complete = false;
                break;
            }
            ExportManifestPage entry;
            entry.file_name = QFileInfo( pagePath ).fileName().toStdString();
            entry.sha256 = sha;
            entry.bytes = bytes;
            pageEntries.push_back( entry );
            deliveredPaths << pagePath;
        }
        if ( complete && pageEntries.empty() )
            complete = false;
        if ( !complete )
        {
            for ( const QString &stale : deliveredPaths )
                QFile::remove( stale );
            ProduceResult failed = refused(
                "EXPORT_FAILED",
                QStringLiteral( "multi-page delivery incomplete: page files missing or "
                                "unreadable under '%1'" )
                  .arg( dir.path() ) );
            failed.quality = quality;
            return failed;
        }
    }
    else
    {
        ExportManifestPage page;
        page.file_name = QFileInfo( QString::fromStdString( exported.path ) ).fileName().toStdString();
        page.sha256 = exported.sha256;
        page.bytes = exported.bytes;
        pageEntries.push_back( page );
        deliveredPaths << QString::fromStdString( exported.path );
    }

    if ( request.write_manifest )
    {
        if ( !report( reporter, "manifest", 0.9, "writing manifest" ) )
        {
            for ( const QString &path : deliveredPaths )
                QFile::remove( path );
            return refused( "PRODUCE_CANCELLED", QStringLiteral( "cancelled before manifest" ) );
        }
        ExportManifest manifest;
        manifest.layout_name = working["layout_name"].asString();
        manifest.format = request.format;
        manifest.dpi = request.dpi;
        manifest.provenance = result.provenance;
        manifest.structural_digest = result.structural_digest;
        for ( const ExportManifestPage &entry : pageEntries )
            manifest.pages.push_back( entry );
        Json::Value environment( Json::objectValue );
        environment["qt_runtime"] = qVersion();
        environment["qgis_version"] = Qgis::version().toStdString();
        environment["os"] = QSysInfo::prettyProductName().toStdString();
        Json::Value substitutions( Json::arrayValue );
        for ( const QString &diagnostic : environmentFontDiagnostics( exported.fontDiagnostics ) )
            substitutions.append( diagnostic.toStdString() );
        environment["font_substitutions"] = substitutions;
        manifest.environment = environment;

        std::string manifestError;
        if ( !writeExportManifest( request.directory, baseName.toStdString(), request.format,
                                   manifest, &manifestError ) )
        {
            ProduceResult failed =
              refused( "MANIFEST_FAILED", QString::fromStdString( manifestError ) );
            for ( const QString &path : deliveredPaths )
                QFile::remove( path );
            failed.artifact_path.clear();
            return failed;
        }
        result.manifest = manifest.toJson();
        manifestPath = QDir( QString::fromStdString( request.directory ) )
                         .filePath( baseName + "." + QString::fromStdString( request.format ) +
                                    ".manifest.json" );
        result.manifest_path = QFileInfo( manifestPath ).absoluteFilePath().toStdString();
    }
    result.ok = true;
    return result;
}

Json::Value produceResultToJson( const ProduceResult &result )
{
    Json::Value out( Json::objectValue );
    out["ok"] = result.ok;
    out["mode"] = result.mode;
    out["page_count"] = result.page_count;
    if ( !result.error_code.empty() )
        out["error_code"] = result.error_code;
    if ( !result.error.empty() )
        out["error"] = result.error;
    if ( result.quality.isObject() )
        out["quality"] = result.quality;
    if ( result.mapspec.isObject() )
        out["mapspec"] = result.mapspec;
    if ( result.repair_iterations > 0 || result.repairs_applied > 0 )
    {
        out["repairs_applied"] = result.repairs_applied;
        out["repair_iterations"] = result.repair_iterations;
        out["repair_ledger"] = result.repair_ledger;
    }
    if ( !result.structural_digest.empty() )
        out["structural_digest"] = result.structural_digest;
    if ( result.provenance.isObject() )
        out["provenance"] = result.provenance;
    if ( !result.artifact_path.empty() )
        out["artifact"] = result.artifact_path;
    if ( !result.manifest_path.empty() )
        out["manifest"] = result.manifest;
    const std::string delivered = !result.manifest_path.empty()
                                    ? result.manifest_path
                                    : result.artifact_path;
    if ( result.ok && !delivered.empty() )
        out["output"] = delivered;
    return out;
}

} // namespace sicnu::agent::cartography
