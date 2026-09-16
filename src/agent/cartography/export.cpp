// src/agent/cartography/export.cpp
#include "export.h"

#include <qgslayoutatlas.h>
#include <qgslayoutitemlabel.h>
#include <qgslayoutmanager.h>

#include <qgslayoutpagecollection.h>
#include <qgsfeature.h>
#include <qgsvectorlayer.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QSet>
#include <QTemporaryFile>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace sicnu::agent::cartography {

namespace {

bool formatSupported( const std::string &format )
{
  return format == "png" || format == "pdf" || format == "svg";
}

bool formatSupportsPageSelection( const std::string &format )
{
  // This QGIS build: only ImageExportSettings carries `pages`.
  return format == "png";
}

/// Label families declared across the layout (bounded, deduped) paired with
/// the family QGIS actually resolved — a mismatch is a substitution.
QStringList fontSubstitutionDiagnostics( QgsPrintLayout *layout )
{
  QStringList diagnostics;
  QSet<QString> reported;
  const QList<QGraphicsItem *> items = layout->items();
  for ( QGraphicsItem *sceneItem : items )
  {
    auto *label = dynamic_cast<QgsLayoutItemLabel *>( sceneItem );
    if ( !label )
      continue;
    const QFont declared = label->font();
    const QString requested = declared.family();
    if ( requested.isEmpty() || reported.contains( requested ) )
      continue;
    QFontInfo resolved( declared );
    if ( !resolved.exactMatch() )
    {
      reported.insert( requested );
      diagnostics << QStringLiteral( "font '%1' substituted with '%2'" )
                       .arg( requested, resolved.family() );
    }
    if ( reported.size() >= 16 )
      break; // bounded diagnostics
  }
  return diagnostics;
}

std::string sha256OfFile( const QString &path, long long *bytes )
{
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) )
    return std::string();
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
  if ( bytes )
    *bytes = total;
  return std::string( hash.result().toHex().constData() );
}

/// Bare, filesystem-safe page file stem: the filename expression may
/// evaluate to anything, so characters outside [A-Za-z0-9._- ] collapse to
/// '_' (empty/unsafe results fall back to the caller-provided fallback).
QString sanitizePageStem( const QString &raw, const QString &fallback )
{
  QString stem = raw.trimmed();
  QString safe;
  safe.reserve( stem.size() );
  for ( const QChar ch : stem )
  {
    const ushort u = ch.unicode();
    const bool keep = ( u >= 'a' && u <= 'z' ) || ( u >= 'A' && u <= 'Z' ) ||
                      ( u >= '0' && u <= '9' ) || u == '_' || u == '-' || u == '.' || u == ' ';
    safe.append( keep ? ch : QChar( '_' ) );
  }
  safe = safe.trimmed();
  // Dots make tracing decisions ambiguous; keep only inner ones in names.
  if ( safe.size() > 64 )
    safe = safe.left( 64 );
  if ( safe.isEmpty() || safe == "." || safe == ".." || safe.startsWith( QLatin1String( ".." ) ) )
    return fallback;
  return safe;
}

/// Feature geometry bounding box in the coverage layer CRS (null when the
/// feature has no geometry).
Json::Value featureExtentJson( const QgsFeature &feature, const QgsVectorLayer *layer )
{
  Json::Value extent( Json::nullValue );
  if ( !feature.hasGeometry() || feature.geometry().isEmpty() )
    return extent;
  const QgsRectangle box = feature.geometry().boundingBox();
  extent = Json::Value( Json::objectValue );
  extent["x_min"] = box.xMinimum();
  extent["y_min"] = box.yMinimum();
  extent["x_max"] = box.xMaximum();
  extent["y_max"] = box.yMaximum();
  extent["crs"] = layer ? layer->crs().authid().toStdString() : std::string();
  return extent;
}

} // namespace

std::vector<std::string> validateMapExportRequest( const QgsPrintLayout *layout,
                                                   const MapExportRequest &request )
{
  std::vector<std::string> problems;
  if ( layout == nullptr )
    problems.push_back( "no layout to export" );
  if ( !formatSupported( request.format ) )
    problems.push_back( "format must be png|pdf|svg ('" + request.format + "' is not a " +
                        "declared export capability)" );
  if ( !( request.dpi >= 72.0 && request.dpi <= 1200.0 ) )
    problems.push_back( "dpi must be within [72, 1200]" );
  if ( request.directory.empty() )
    problems.push_back( "directory is required" );
  // A governed export writes INSIDE the declared directory: a bare file
  // name is required — no separators, no traversal, no dot shenanigans.
  if ( !request.file_name.empty() )
  {
    if ( request.file_name.find( '/' ) != std::string::npos ||
         request.file_name.find( '\\' ) != std::string::npos )
      problems.push_back( "file_name must be a bare name (no path separators)" );
    else if ( request.file_name == "." || request.file_name == ".." ||
              request.file_name.rfind( "..", 0 ) == 0 )
      problems.push_back( "file_name must not be or start with '..'" );
  }
  if ( !request.pages.empty() && !formatSupportsPageSelection( request.format ) )
    problems.push_back( "page selection is only supported for png on this QGIS build; '" +
                        request.format + "' always exports all pages" );
  if ( layout != nullptr && !request.pages.empty() )
  {
    const int pageCount = std::max( 1, layout->pageCollection()->pageCount() );
    for ( const int page : request.pages )
      if ( page < 0 || page >= pageCount )
        problems.push_back( "page index " + std::to_string( page ) +
                            " is out of range (layout has " + std::to_string( pageCount ) +
                            " page(s))" );
  }
  return problems;
}

MapExportResult exportMapLayout( QgsPrintLayout *layout, const MapExportRequest &request )
{
  MapExportResult result;
  const std::vector<std::string> problems = validateMapExportRequest( layout, request );
  if ( !problems.empty() )
  {
    result.error = QString::fromStdString( problems.front() );
    for ( const std::string &problem : problems )
      result.fontDiagnostics << QString::fromStdString( problem );
    return result;
  }

  QDir dir( QString::fromStdString( request.directory ) );
  if ( !dir.exists() && !dir.mkpath( "." ) )
  {
    result.error = QStringLiteral( "cannot create export directory '%1'" ).arg( dir.path() );
    return result;
  }

  const QString baseName =
    request.file_name.empty()
      ? layout->name()
      : QString::fromStdString( request.file_name );
  const QString suffix = QString::fromStdString( request.format );
  const QString finalPath = dir.filePath( baseName + "." + suffix );

  // Atomic write: exporter writes the temp file; only a verified write is
  // renamed onto the declared path.
  QTemporaryFile temp( dir.filePath( baseName + ".XXXXXX." + suffix ) );
  temp.setAutoRemove( true );
  if ( !temp.open() )
  {
    result.error = QStringLiteral( "cannot create a temporary export file in '%1'" )
                     .arg( dir.path() );
    return result;
  }
  const QString tempPath = temp.fileName();
  temp.close(); // the exporter writes its own handle

  QgsLayoutExporter exporter( layout );
  if ( request.format == "png" )
  {
    QgsLayoutExporter::ImageExportSettings settings;
    settings.dpi = request.dpi;
    for ( const int page : request.pages )
      settings.pages << page;
    result.exporterResult = exporter.exportToImage( tempPath, settings );
  }
  else if ( request.format == "pdf" )
  {
    QgsLayoutExporter::PdfExportSettings settings;
    settings.dpi = request.dpi;
    result.exporterResult = exporter.exportToPdf( tempPath, settings );
  }
  else
  {
    QgsLayoutExporter::SvgExportSettings settings;
    settings.dpi = request.dpi;
    result.exporterResult = exporter.exportToSvg( tempPath, settings );
  }

  if ( result.exporterResult != QgsLayoutExporter::Success )
  {
    // Temp removal is automatic (autoRemove); the declared path is untouched.
    result.error = QStringLiteral( "exporter failed with result %1" )
                     .arg( static_cast<int>( result.exporterResult ) );
    return result;
  }

  long long bytes = 0;
  result.sha256 = sha256OfFile( tempPath, &bytes );
  result.bytes = bytes;
  if ( result.bytes <= 0 || result.sha256.empty() )
  {
    result.error = QStringLiteral( "exported file is empty or unreadable" );
    return result;
  }
  // Swap into place: POSIX rename(2) replaces an existing target
  // atomically; Windows cannot, so fall back to remove+rename there. The
  // temp file keeps verified bytes until the very end either way.
  if ( !QFile::rename( tempPath, finalPath ) )
  {
    if ( !QFile::exists( finalPath ) )
    {
      result.error = QStringLiteral( "cannot move the export into place at '%1'" ).arg( finalPath );
      return result;
    }
    if ( !QFile::remove( finalPath ) || !QFile::rename( tempPath, finalPath ) )
    {
      result.error = QStringLiteral( "cannot replace the existing export at '%1'" ).arg( finalPath );
      return result;
    }
  }

  result.ok = true;
  result.path = QFileInfo( finalPath ).absoluteFilePath().toStdString();
  result.fontDiagnostics = fontSubstitutionDiagnostics( layout );
  return result;
}

std::vector<std::string> validateMapAtlasExportRequest( QgsPrintLayout *layout,
                                                        const MapAtlasExportRequest &request )
{
  std::vector<std::string> problems;
  if ( layout == nullptr )
    problems.push_back( "no layout to export" );
  if ( request.format != "png" )
    problems.push_back( "atlas delivery supports png only on this QGIS build; '" +
                        request.format +
                        "' would flatten every feature into one static document" );
  if ( !( request.dpi >= 72.0 && request.dpi <= 1200.0 ) )
    problems.push_back( "dpi must be within [72, 1200]" );
  if ( request.directory.empty() )
    problems.push_back( "directory is required" );
  if ( request.file_name.empty() || request.file_name.find( '/' ) != std::string::npos ||
       request.file_name.find( '\\' ) != std::string::npos ||
       request.file_name == "." || request.file_name == ".." ||
       request.file_name.rfind( "..", 0 ) == 0 )
    problems.push_back( "file_name is required and must be a bare name (no path separators)" );
  if ( request.max_pages < 1 )
    problems.push_back( "max_pages must be >= 1" );
  if ( layout != nullptr )
  {
    QgsLayoutAtlas *atlas = layout->atlas();
    if ( atlas == nullptr || !atlas->enabled() )
      problems.push_back( "layout atlas is not enabled; enable it in the MapSpec page.atlas "
                          "and re-compose" );
    else if ( atlas->coverageLayer() == nullptr )
      problems.push_back( "layout atlas has no coverage layer; declare page.atlas.coverage_layer" );
  }
  return problems;
}

MapAtlasExportResult exportMapAtlas( QgsPrintLayout *layout, const MapAtlasExportRequest &request,
                                     const std::function<bool()> &cancelProbe )
{
  MapAtlasExportResult result;
  result.problems = validateMapAtlasExportRequest( layout, request );
  if ( !result.problems.empty() )
  {
    result.error = QString::fromStdString( result.problems.front() );
    return result;
  }

  QDir dir( QString::fromStdString( request.directory ) );
  if ( !dir.exists() && !dir.mkpath( "." ) )
  {
    result.error = QStringLiteral( "cannot create export directory '%1'" ).arg( dir.path() );
    return result;
  }

  QgsLayoutAtlas *atlas = layout->atlas();
  // count() only reports the last updateFeatures() pass (0 before
  // beginRender on this QGIS build): refresh the feature list up front so
  // the bounds checks reason about the REAL delivery size.
  atlas->updateFeatures();
  const int featureCount = atlas->count();
  if ( featureCount <= 0 )
  {
    result.error = QStringLiteral( "layout atlas selects no features (filter excludes "
                                   "everything or the coverage layer is empty)" );
    return result;
  }
  const int maxPages = std::clamp( request.max_pages, 1, kMaxAtlasExportPages );
  if ( featureCount > maxPages )
  {
    result.error = QStringLiteral( "layout atlas would deliver %1 pages, above the %2-page "
                                   "production bound" )
                     .arg( featureCount )
                     .arg( maxPages );
    return result;
  }

  const QString baseName = QString::fromStdString( request.file_name );
  const QString suffix = QString::fromStdString( request.format );
  const auto rollback = [ &result ]() {
    // A non-ok atlas delivery leaves NOTHING behind: every page already
    // renamed into the directory is removed again (temp files remove
    // themselves; the final rename is the delivery commit point).
    for ( const MapAtlasPage &page : result.pages )
      QFile::remove( QString::fromStdString( page.path ) );
    result.pages.clear();
  };

  if ( !atlas->beginRender() )
  {
    result.error = QStringLiteral( "atlas render could not start (coverage layer unreadable?)" );
    return result;
  }

  // This QGIS build exposes the current feature only through the
  // featureChanged signal (no currentFeature() accessor): capture it per
  // iteration. Scope the connection to the layout so it drops with it.
  QgsFeature currentFeature;
  const QMetaObject::Connection featureCapture = QObject::connect(
    atlas, &QgsLayoutAtlas::featureChanged, layout,
    [ &currentFeature ]( const QgsFeature &feature ) { currentFeature = feature; },
    Qt::DirectConnection );

  int index = 0;
  QSet<QString> usedStems;
  while ( atlas->next() )
  {
    if ( cancelProbe && cancelProbe() )
    {
      result.cancelled = true;
      result.error = QStringLiteral( "atlas export cancelled at page %1 of %2" )
                       .arg( index + 1 )
                       .arg( featureCount );
      break;
    }
    const QgsFeature feature = currentFeature;
    const QString label = atlas->currentFilename();
    const QString stem =
      sanitizePageStem( label, QStringLiteral( "p%1" ).arg( index + 1 ) );
    QString unique = stem;
    int disambiguator = 2;
    while ( usedStems.contains( unique.toLower() ) )
      unique = QStringLiteral( "%1_%2" ).arg( stem ).arg( disambiguator++ );
    usedStems.insert( unique.toLower() );
    const QString finalPath = dir.filePath( baseName + "_" + unique + "." + suffix );

    // Per-page atomic write: exactly the single-export discipline, one
    // page at a time (fresh exporter per page, mirroring QGIS's own
    // iterator-driven export loop).
    QTemporaryFile temp( dir.filePath( baseName + "_" + unique + ".XXXXXX." + suffix ) );
    temp.setAutoRemove( true );
    if ( !temp.open() )
    {
      result.error = QStringLiteral( "cannot create a temporary export file in '%1'" )
                       .arg( dir.path() );
      break;
    }
    const QString tempPath = temp.fileName();
    temp.close();

    QgsLayoutExporter exporter( layout );
    QgsLayoutExporter::ImageExportSettings settings;
    settings.dpi = request.dpi;
    result.exporterResult = exporter.exportToImage( tempPath, settings );
    if ( result.exporterResult != QgsLayoutExporter::Success )
    {
      result.error = QStringLiteral( "atlas page %1 export failed with result %2" )
                       .arg( index + 1 )
                       .arg( static_cast<int>( result.exporterResult ) );
      break;
    }

    MapAtlasPage page;
    page.feature_id = feature.isValid()
                        ? std::to_string( feature.id() )
                        : "index-" + std::to_string( atlas->currentFeatureNumber() );
    page.label = label.toStdString();
    page.extent = featureExtentJson( feature, atlas->coverageLayer() );
    page.sha256 = sha256OfFile( tempPath, &page.bytes );
    if ( page.bytes <= 0 || page.sha256.empty() )
    {
      result.error = QStringLiteral( "atlas page %1 export is empty or unreadable" ).arg( index + 1 );
      break;
    }
    if ( !QFile::rename( tempPath, finalPath ) )
    {
      if ( !QFile::exists( finalPath ) )
      {
        result.error = QStringLiteral( "cannot move atlas page %1 into place at '%2'" )
                         .arg( index + 1 )
                         .arg( finalPath );
        break;
      }
      if ( !QFile::remove( finalPath ) || !QFile::rename( tempPath, finalPath ) )
      {
        result.error = QStringLiteral( "cannot replace atlas page at '%1'" ).arg( finalPath );
        break;
      }
    }
    page.path = QFileInfo( finalPath ).absoluteFilePath().toStdString();
    page.file_name = baseName.toStdString() + "_" + unique.toStdString() + "." +
                     suffix.toStdString();
    result.pages.push_back( page );
    ++index;
    if ( index >= maxPages && atlas->next() )
    {
      // Belt-and-braces: count() raced with the actual iteration. The
      // runaway guard wins — delivered pages are rolled back below.
      result.error = QStringLiteral( "atlas iteration exceeded the %1-page production bound" )
                       .arg( maxPages );
      break;
    }
  }
  QObject::disconnect( featureCapture );
  atlas->endRender();

  if ( result.cancelled || !result.error.isEmpty() )
  {
    rollback();
    return result;
  }
  result.ok = true;
  return result;
}

Json::Value mapExportResultToJson( const MapExportResult &result )
{
  Json::Value out( Json::objectValue );
  out["ok"] = result.ok;
  if ( !result.error.isEmpty() )
    out["error"] = result.error.toStdString();
  if ( result.ok )
  {
    out["path"] = result.path;
    out["sha256"] = result.sha256;
    out["bytes"] = static_cast<Json::Int64>( result.bytes );
  }
  Json::Value diagnostics( Json::arrayValue );
  for ( const QString &diagnostic : result.fontDiagnostics )
    diagnostics.append( diagnostic.toStdString() );
  out["diagnostics"] = diagnostics;
  return out;
}

} // namespace sicnu::agent::cartography
