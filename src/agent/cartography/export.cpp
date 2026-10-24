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
#include <set>

#include "geospatial/util/atomic_fs.h"

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

namespace
{
// The vendored QgsLayoutExporter derives non-first-page file names itself
// (base_2.png siblings, generateFileName): redirecting a per-page export to a
// caller-chosen temp path needs the virtual hook, not the settings object.
class SingleNamePageExporter : public QgsLayoutExporter
{
  public:
    using QgsLayoutExporter::QgsLayoutExporter;

  protected:
    QString generateFileName( const QgsLayoutExporter::PageExportDetails &details ) const override
    {
      return details.directory + '/' + details.baseName + '.' + details.extension;
    }
};
} // namespace

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

QString sanitizeFileStem( const QString &raw, const QString &fallback )
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
    // R21-D: a repeated index used to be accepted, so the delivery wrote
    // the same deterministic file name twice (second pass overwriting the
    // first) while the result and the manifest claimed TWO pages — a
    // manifest that cannot describe the files it delivered. Per-page
    // delivery names are deterministic, so duplicates are a self-
    // contradictory request: refuse typed instead.
    std::set<int> unique( request.pages.cbegin(), request.pages.cend() );
    if ( unique.size() != request.pages.size() )
      problems.push_back( "page selection lists a repeated index; per-page delivery names "
                          "are deterministic, so duplicates cannot describe distinct files" );
  }
  return problems;
}

MapExportResult exportMapLayout( QgsPrintLayout *layout, const MapExportRequest &request,
                                 const std::function<bool()> &cancelProbe )
{
  MapExportResult result;
  // R21-D: the single/multi-page raster path had no cancel seam, so a user
  // pressing Stop mid-export got a task marked Canceled while the files had
  // already landed in the delivery directory. The atlas path polls on every
  // page boundary; this path now does the same (probe invoked per page,
  // before the page's bytes are published).
  const auto cancelled = [ &cancelProbe ]() {
    return cancelProbe && cancelProbe();
  };
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

  // Deterministic per-page delivery names, mirroring the exporter's own
  // generateFileName contract (page 0 → base.<fmt>, page N → base_<N+1>.<fmt>).
  const auto pageFinalPath = [ &dir, &baseName, &suffix ]( int page ) {
    return page == 0 ? dir.filePath( baseName + "." + suffix )
                     : dir.filePath( baseName + "_" + QString::number( page + 1 ) + "." + suffix );
  };

  // CARTO-1: an all-pages png export of a multi-page layout used to hand the
  // exporter ONE temp path — it wrote page 0 there and pages 1..N-1 as
  // randomly-named SIBLINGS in the delivery directory (QTemporaryFile only
  // removes its own file), so the delivery missed pages 2..N and carried
  // orphan junk. Multi-page png is exported one page at a time through the
  // same temp → verify → publish discipline, each under its deterministic
  // name; any failure rolls back every page already delivered.
  const bool rasterFormat = ( request.format == "png" || request.format == "svg" );
  if ( rasterFormat )
  {
    std::vector<int> pages = request.pages;
    if ( pages.empty() )
    {
      const int pageCount = std::max( 1, layout->pageCollection()->pageCount() );
      for ( int page = 0; page < pageCount; ++page )
        pages.push_back( page );
    }
    if ( pages.size() > 1 )
    {
      QStringList delivered;
      for ( const int page : pages )
      {
        // R21-D: page-boundary cancel, same discipline as the atlas path —
        // before this page's bytes are written, and the pages already
        // delivered by THIS run are removed so a cancelled delivery leaves
        // no trace.
        if ( cancelled() )
        {
          result.error = QStringLiteral( "export cancelled" );
          for ( const QString &published : delivered )
            QFile::remove( published );
          result.cancelled = true;
          return result;
        }
        QTemporaryFile temp( dir.filePath( baseName + ".XXXXXX." + suffix ) );
        temp.setAutoRemove( true );
        if ( !temp.open() )
        {
          result.error = QStringLiteral( "cannot create a temporary export file in '%1'" )
                           .arg( dir.path() );
          break;
        }
        const QString tempPath = temp.fileName();
        temp.close(); // the exporter writes its own handle

        // Per-page export. png: the settings page filter. svg: no filter
        // exists, so every OTHER page item is excluded for this pass and the
        // flags restored right after — a page the design itself excludes
        // (saved flag true) is skipped like png's conditional pages.
        result.exporterResult = QgsLayoutExporter::Success;
        if ( request.format == "png" )
        {
          SingleNamePageExporter exporter( layout );
          QgsLayoutExporter::ImageExportSettings settings;
          settings.dpi = request.dpi;
          settings.pages << page;
          result.exporterResult = exporter.exportToImage( tempPath, settings );
        }
        else
        {
          const int pageCountAll = layout->pageCollection()->pageCount();
          QList< QgsLayoutItemPage * > pageItems = layout->pageCollection()->pages();
          if ( page >= pageCountAll || pageItems.at( page )->excludeFromExports() )
            continue;
          // Review2 N3: an ACTIVE data-defined exclude expression overrides
          // the static flag at evaluation time, so the exclusion technique
          // could deliver the wrong page's bytes under a page's name. Refuse
          // typed when any page carries one (reachable only through user
          // .qpt templates; no in-repo spec path sets it).
          for ( int probe = 0; probe < pageCountAll; ++probe )
          {
            if ( pageItems.at( probe )
                   ->dataDefinedProperties()
                   .property( QgsLayoutObject::DataDefinedProperty::ExcludeFromExports )
                   .isActive() )
            {
              result.error = QStringLiteral(
                               "multi-page svg export refuses layouts with a data-defined "
                               "exclude-from-exports expression on a page item (ambiguous "
                               "per-page delivery)" );
              for ( const QString &published : delivered )
                QFile::remove( published );
              return result;
            }
          }
          std::vector< bool > saved( pageCountAll, false );
          for ( int other = 0; other < pageCountAll; ++other )
          {
            saved[ other ] = pageItems.at( other )->excludeFromExports();
            pageItems.at( other )->setExcludeFromExports( other != page );
          }
          SingleNamePageExporter exporter( layout );
          QgsLayoutExporter::SvgExportSettings settings;
          settings.dpi = request.dpi;
          result.exporterResult = exporter.exportToSvg( tempPath, settings );
          for ( int other = 0; other < pageCountAll; ++other )
            pageItems.at( other )->setExcludeFromExports( saved[ other ] );
        }
        if ( result.exporterResult != QgsLayoutExporter::Success )
        {
          result.error = QStringLiteral( "exporter failed on page %1 with result %2" )
                           .arg( page + 1 )
                           .arg( static_cast<int>( result.exporterResult ) );
          break;
        }
        if ( !QFile::exists( tempPath ) )
          continue; // page excluded from export (conditional page / null size)
        long long bytes = 0;
        const std::string sha = sha256OfFile( tempPath, &bytes );
        if ( bytes <= 0 || sha.empty() )
        {
          result.error = QStringLiteral( "exported page %1 is empty or unreadable" ).arg( page + 1 );
          break;
        }
        const QString finalPagePath = pageFinalPath( page );
        try
        {
          sicnu::geo::atomic_fs::fsyncFile( tempPath.toStdString() );
          sicnu::geo::atomic_fs::publishStagedFile( tempPath.toStdString(),
                                                    finalPagePath.toStdString() );
        }
        catch ( const sicnu::geo::GeoError &ex )
        {
          result.error = QStringLiteral( "cannot publish page %1 at '%2': %3" )
                           .arg( page + 1 )
                           .arg( finalPagePath, QString::fromUtf8( ex.what() ) );
          break;
        }
        delivered << finalPagePath;
        result.pagePaths.push_back( QFileInfo( finalPagePath ).absoluteFilePath().toStdString() );
        if ( result.path.empty() )
        {
          // The primary artifact describes the first delivered page.
          result.path = QFileInfo( finalPagePath ).absoluteFilePath().toStdString();
          result.sha256 = sha;
          result.bytes = bytes;
        }
      }
      if ( !result.error.isEmpty() )
      {
        for ( const QString &published : delivered )
          QFile::remove( published );
        return result;
      }
      if ( result.path.empty() )
      {
        result.error = QStringLiteral( "export produced no pages (all pages excluded?)" );
        return result;
      }
      result.ok = true;
      result.fontDiagnostics = fontSubstitutionDiagnostics( layout );
      return result;
    }
  }

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
    // R18-D: a single selected page (or a one-page layout) must deliver
    // exactly the requested page's bytes at the declared name. The base
    // exporter's generateFileName renames every non-zero page to a
    // "_<page+1>" SIBLING of the temp path — the single-page form of the
    // CARTO-1 orphan bug: the temp stayed empty, the export failed with
    // "file is empty", and the sibling survived in the delivery directory.
    SingleNamePageExporter singleNameExporter( layout );
    QgsLayoutExporter::ImageExportSettings settings;
    settings.dpi = request.dpi;
    for ( const int page : request.pages )
      settings.pages << page;
    result.exporterResult = singleNameExporter.exportToImage( tempPath, settings );
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
  // #1178: publish via atomic_fs (ReplaceFileW / MoveFileExW). The verified
  // temp bytes stay put until publish succeeds; a locked target fails closed.
  // Durability gate (atomic_fs.h contract): flush the staged bytes before the
  // rename — a crash must leave the old export or the new one, not junk.
  try
  {
    sicnu::geo::atomic_fs::fsyncFile( tempPath.toStdString() );
    sicnu::geo::atomic_fs::publishStagedFile( tempPath.toStdString(), finalPath.toStdString() );
  }
  catch ( const sicnu::geo::GeoError &ex )
  {
    result.error = QStringLiteral( "cannot publish the export at '%1': %2" )
                     .arg( finalPath, QString::fromUtf8( ex.what() ) );
    return result;
  }

  result.ok = true;
  result.path = QFileInfo( finalPath ).absoluteFilePath().toStdString();
  result.pagePaths.push_back( result.path );
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
      sanitizeFileStem( label, QStringLiteral( "p%1" ).arg( index + 1 ) );
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
    // One delivery file per coverage feature: page 0 only. Without the
    // filter the vendored exporter writes every layout page, leaving
    // base_2.png siblings in the delivery directory that no manifest
    // lists (generateFileName contract, see SingleNamePageExporter note).
    settings.pages << 0;
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
    try
    {
      sicnu::geo::atomic_fs::fsyncFile( tempPath.toStdString() );
      sicnu::geo::atomic_fs::publishStagedFile( tempPath.toStdString(), finalPath.toStdString() );
    }
    catch ( const sicnu::geo::GeoError &ex )
    {
      result.error = QStringLiteral( "cannot publish atlas page %1 at '%2': %3" )
                       .arg( index + 1 )
                       .arg( finalPath )
                       .arg( QString::fromUtf8( ex.what() ) );
      break;
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
