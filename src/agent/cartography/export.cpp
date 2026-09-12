// src/agent/cartography/export.cpp
#include "export.h"

#include <qgslayoutitemlabel.h>
#include <qgslayoutmanager.h>

#include <qgslayoutpagecollection.h>

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
