// help_guide_locale.h — locale-aware user-guide document resolution.
//
// R31-T03 WP18. Before this change HelpViewerDialog always loaded the Chinese
// docs/USER_GUIDE.md, so an English UI silently showed Chinese prose. This
// header keeps the locale -> document mapping a single, testable authority:
//   * the one UI-language authority is SicnuUi::currentUiLanguage() (#2064/#2065);
//   * machine IDs, parameter names, units and diagnostic codes come from the
//     existing help registry/data and are never translated here;
//   * a missing locale document falls back honestly, with a visible notice;
//   * no document at all falls back to the built-in summary, also with a notice.
//
// Header-only on purpose: the resolution is pure Qt Core (no QWidgets/QGIS),
// so it can be exercised by a small offline test target with real temp files.
#pragma once

#include "application_language.h"
#include "app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace sicnu::help
{

struct GuideResolution
{
  QString filePath;           ///< document actually to load (empty => built-in summary)
  QString requestedLanguage;  ///< "zh_CN" or "en", from the UI-language authority
  QString servedLanguage;     ///< language of the document actually served
  bool fallbackUsed = false;  ///< requested-locale document missing, another served
  bool embeddedFallback = false; ///< no on-disk document found at all
  QString notice;             ///< localized, visible explanation (empty on exact hit)
};

/// The checked-in document name for a resolved UI language.
inline QString guideDocumentName( const QString &language )
{
  return language == QLatin1String( "en" )
           ? QStringLiteral( "USER_GUIDE.en.md" )
           : QStringLiteral( "USER_GUIDE.md" );
}

/// Honest, localized fallback notice. The requested UI language drives the
/// language of the notice, so an English UI never silently receives Chinese
/// prose; machine identifiers are explicitly called out as unchanged.
inline QString guideFallbackNotice( const QString &requestedLanguage,
                                    const QString &servedLanguage,
                                    bool embedded )
{
  const QString served = servedLanguage == QLatin1String( "en" )
                           ? QStringLiteral( "English" ) : QStringLiteral( "中文" );
  if ( embedded )
    return requestedLanguage == QLatin1String( "en" )
             ? QStringLiteral( "> **Note — manual unavailable:** No installed user guide was found in this package; showing the built-in summary. Machine IDs, parameter names, units and diagnostic codes are unchanged.\n\n" )
             : QStringLiteral( "> **说明 — 手册不可用：** 本安装包未找到已安装的用户手册，现显示内置摘要。机器 ID、参数名、单位与诊断码保持不变。\n\n" );
  return requestedLanguage == QLatin1String( "en" )
           ? QStringLiteral( "> **Note — manual language:** The English manual is not installed in this package; showing the %1 manual instead. Machine IDs, parameter names, units and diagnostic codes are unchanged.\n\n" ).arg( served )
           : QStringLiteral( "> **说明 — 手册语言：** 本安装包未附带中文手册，现显示%1手册。机器 ID、参数名、单位与诊断码保持不变。\n\n" ).arg( served );
}

/// Visible notice for a manual that WAS resolved on disk but could not be
/// opened (permissions, replaced between resolve and read). R2: an exact hit
/// must never degrade to the built-in summary silently.
inline QString guideUnreadableNotice( const QString &requestedLanguage,
                                      const QString &filePath )
{
  return requestedLanguage == QLatin1String( "en" )
           ? QStringLiteral( "> **Note — manual unreadable:** %1 could not be opened; showing the built-in summary. Machine IDs, parameter names, units and diagnostic codes are unchanged.\n\n" ).arg( filePath )
           : QStringLiteral( "> **说明 — 手册不可读：** 无法打开 %1，现显示内置摘要。机器 ID、参数名、单位与诊断码保持不变。\n\n" ).arg( filePath );
}

/// Resolve a guide document against an explicit ordered list of search roots.
/// The explicit-roots overload is the deterministic test seam: the caller can
/// point it at a real temporary install tree (missing locale file, migrated
/// directory, Unicode path) without touching global state or sleeping.
inline GuideResolution resolveGuideDocument( const QString &requestedLanguage,
                                             const QStringList &searchRoots )
{
  GuideResolution result;
  result.requestedLanguage = requestedLanguage;
  const QString otherLanguage = requestedLanguage == QLatin1String( "en" )
                                  ? QStringLiteral( "zh_CN" ) : QStringLiteral( "en" );
  const QString requestedName = guideDocumentName( requestedLanguage );
  const QString otherName = guideDocumentName( otherLanguage );

  for ( const QString &root : searchRoots )
  {
    if ( root.isEmpty() )
      continue;
    const QString candidate = QDir( root ).filePath( requestedName );
    if ( QFileInfo( candidate ).isFile() )
    {
      result.filePath = candidate;
      result.servedLanguage = requestedLanguage;
      return result;
    }
  }

  for ( const QString &root : searchRoots )
  {
    if ( root.isEmpty() )
      continue;
    const QString candidate = QDir( root ).filePath( otherName );
    if ( QFileInfo( candidate ).isFile() )
    {
      result.filePath = candidate;
      result.servedLanguage = otherLanguage;
      result.fallbackUsed = true;
      result.notice = guideFallbackNotice( requestedLanguage, otherLanguage, false );
      return result;
    }
  }

  result.servedLanguage.clear();
  result.fallbackUsed = true;
  result.embeddedFallback = true;
  result.notice = guideFallbackNotice( requestedLanguage, requestedLanguage, true );
  return result;
}

/// Ordered search roots.
///
/// SICNU_GUIDE_DIR is an AUTHORITATIVE, explicit manual directory for a
/// relocated / install-only package (and the deterministic offline test seam).
/// When set, ONLY it is searched: R2 found that letting the source-tree docs/
/// root participate makes a package missing its English manual silently serve
/// the developer's in-tree manual with no fallback notice. That is worse than
/// an honest "manual unavailable" summary. SICNU_DATA_DIR (or the standard
/// install roots / in-tree fallback) is used when no explicit pin is set.
inline QStringList guideSearchRoots( const QString &language )
{
  const QString pinned = qEnvironmentVariable( "SICNU_GUIDE_DIR" );
  if ( !pinned.isEmpty() )
    return { pinned };

  QStringList roots;
  const QString explicitDataDir = qEnvironmentVariable( "SICNU_DATA_DIR" );
  if ( !explicitDataDir.isEmpty() )
    roots.append( QDir( explicitDataDir ).filePath( QStringLiteral( "docs" ) ) );

  const QString appDir = QCoreApplication::applicationDirPath();
  roots.append( QDir( appDir ).absoluteFilePath( QStringLiteral( "../docs" ) ) );
  roots.append( QDir( appDir ).absoluteFilePath( QStringLiteral( "docs" ) ) );
  roots.append( QDir( appDir ).absoluteFilePath( QStringLiteral( "../share/sicnu_geo_rs/docs" ) ) );
  roots.append( QDir( appDir ).absoluteFilePath( QStringLiteral( "share/sicnu_geo_rs/docs" ) ) );
  roots.append( QDir::current().absoluteFilePath( QStringLiteral( "docs" ) ) );

  // AppPaths resolves the source tree / install prefix root; keep its docs/
  // directory as the last chance so in-tree development still works.
  const QString resolved = AppPaths::resolveDataPath(
    QStringLiteral( "docs/" ) + guideDocumentName( language ) );
  roots.append( QFileInfo( resolved ).absolutePath() );
  return roots;
}

/// The language the VISIBLE UI is actually in. bootstrap records the language
/// really installed (which is "en" when a catalog is missing); before bootstrap
/// falls back to the saved preference. R1 P1-1: an English UI must never
/// silently resolve the Chinese manual just because a preference says zh_CN.
inline QString effectiveUiLanguageForGuide()
{
  const QString installed = SicnuUi::installedUiLanguage();
  return installed.isEmpty() ? SicnuUi::currentUiLanguage() : installed;
}

/// Production entry point: resolves from the effective (installed) UI language.
inline GuideResolution resolveGuideDocumentForUi()
{
  const QString language = effectiveUiLanguageForGuide();
  return resolveGuideDocument( language, guideSearchRoots( language ) );
}

} // namespace sicnu::help
