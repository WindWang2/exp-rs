// test_help_system.cpp — 4-Tier verification suite for RS Studio Help System
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "app_paths.h"
#include "dialogs/dialog_help_catalog.h"
#include "dialogs/help_viewer_dialog.h"

namespace
{
QApplication *ensureApp()
{
  static int argc = 1;
  static char a[] = "help_test";
  static char *v[] = { a, nullptr };
  return qApp ? qApp : new QApplication( argc, v );
}
} // namespace

TEST_CASE( "HelpSystem: USER_GUIDE.md exists and contains all 10 chapters", "[help][guide]" )
{
  ensureApp();
  const QString docPath = AppPaths::resolveDataPath( QStringLiteral( "docs/USER_GUIDE.md" ) );
  
  QFile file( docPath );
  bool exists = file.exists();
  if ( !exists )
  {
    file.setFileName( QStringLiteral( "docs/USER_GUIDE.md" ) );
    exists = file.exists();
  }
  
  REQUIRE( exists );
  REQUIRE( file.open( QIODevice::ReadOnly | QIODevice::Text ) );
  const QString content = QString::fromUtf8( file.readAll() );
  file.close();

  REQUIRE( content.size() > 10000 );

  REQUIRE( content.contains( "第 1 章：系统概述与快速入门" ) );
  REQUIRE( content.contains( "第 2 章：遥感数据加载与管理" ) );
  REQUIRE( content.contains( "第 3 章：视口可视化与多源联动" ) );
  REQUIRE( content.contains( "第 4 章：像素级遥感分类全流程" ) );
  REQUIRE( content.contains( "第 5 章：面向对象影像分析" ) );
  REQUIRE( content.contains( "第 6 章：波谱分析与高光谱工具" ) );
  REQUIRE( content.contains( "第 7 章：遥感预处理与图像增强" ) );
  REQUIRE( content.contains( "第 8 章：AI Copilot 智能助手" ) );
  REQUIRE( content.contains( "第 9 章：常见问题排查与诊断" ) );
  REQUIRE( content.contains( "第 10 章：快捷键与操作速查表" ) );
}

TEST_CASE( "HelpSystem: USER_GUIDE.md contains domain terminology", "[help][guide][terminology]" )
{
  ensureApp();
  QString docPath = AppPaths::resolveDataPath( QStringLiteral( "docs/USER_GUIDE.md" ) );
  QFile file( docPath );
  if ( !file.exists() )
    file.setFileName( QStringLiteral( "docs/USER_GUIDE.md" ) );

  REQUIRE( file.open( QIODevice::ReadOnly | QIODevice::Text ) );
  const QString content = QString::fromUtf8( file.readAll() );
  file.close();

  CHECK( ( content.contains( "Jeffries-Matusita" ) || content.contains( "JM" ) ) );
  CHECK( content.contains( "混淆矩阵" ) );
  CHECK( content.contains( "总体精度" ) );
  CHECK( content.contains( "Kappa" ) );
  CHECK( content.contains( "MeanShift" ) );
  CHECK( content.contains( "Watershed" ) );
  CHECK( content.contains( "GLCM" ) );
  CHECK( content.contains( "多尺度分割" ) );
  CHECK( content.contains( "连续统去除" ) );
  CHECK( content.contains( "DOS1" ) );
  CHECK( content.contains( "QUAC" ) );
  CHECK( content.contains( "几何配准" ) );
  CHECK( content.contains( "AI Copilot" ) );
}

TEST_CASE( "HelpSystem: SicnuDialogHelp catalog integrity", "[help][catalog]" )
{
  const QStringList tools = {
    QStringLiteral( "spectral_index" ),
    QStringLiteral( "terrain" ),
    QStringLiteral( "extract_band" ),
    QStringLiteral( "mosaic" ),
    QStringLiteral( "atmospheric_correction" ),
    QStringLiteral( "contrast_stretch" ),
    QStringLiteral( "fusion" ),
    QStringLiteral( "change_detection" ),
    QStringLiteral( "classification" ),
    QStringLiteral( "preferences" ),
    QStringLiteral( "stac_browser" ),
    QStringLiteral( "batch_processing" )
  };

  for ( const QString &tool : tools )
  {
    const QString shortSummary = SicnuDialogHelp::shortForTool( tool );
    const QString htmlBody = SicnuDialogHelp::htmlForTool( tool );
    REQUIRE( false == shortSummary.isEmpty() );
    REQUIRE( false == htmlBody.isEmpty() );
    REQUIRE( htmlBody.contains( "<h3>" ) );
  }
}

TEST_CASE( "HelpSystem: HelpViewerDialog UI lifecycle and TOC tree extraction", "[help][viewer][ui]" )
{
  ensureApp();
  HelpViewerDialog viewer;

  REQUIRE( viewer.tocTree() != nullptr );
  REQUIRE( viewer.textBrowser() != nullptr );
  REQUIRE( viewer.filterEdit() != nullptr );
  REQUIRE( viewer.searchEdit() != nullptr );
  REQUIRE( viewer.zoomInButton() != nullptr );
  REQUIRE( viewer.zoomOutButton() != nullptr );
  REQUIRE( viewer.zoomResetButton() != nullptr );
  REQUIRE( viewer.externalBrowserButton() != nullptr );

  REQUIRE( viewer.tocTree()->topLevelItemCount() > 0 );
  REQUIRE( false == viewer.textBrowser()->toPlainText().isEmpty() );
}

TEST_CASE( "HelpSystem: HelpViewerDialog search and fallback resilience", "[help][viewer][fallback]" )
{
  ensureApp();
  HelpViewerDialog viewer;

  const bool loaded = viewer.loadDocument( QStringLiteral( "/non/existent/path/never_existed.md" ) );
  REQUIRE( false == loaded );
  REQUIRE( false == viewer.currentMarkdown().isEmpty() );
  REQUIRE( viewer.tocTree()->topLevelItemCount() >= 10 );
  REQUIRE( viewer.currentMarkdown().contains( "RS Studio (exp-rs)" ) );

  viewer.searchContent( QStringLiteral( "遥感" ), true );
  REQUIRE( viewer.textBrowser()->textCursor().hasSelection() );
}
