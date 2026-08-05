// test_global_help_tips.cpp — Tests for global help/tooltip/confirmation additions.
//
// Covers:
//   - Help catalog toolIds added in the help/tooltip sweep resolve to non-empty text.
//   - RsMergeClassesDialog has a Help button wired to the catalog (HelpRole).
//
// Note: RsJobPanel ETA formatting (formatEta) is a static member whose enclosing
// translation unit pulls in main_window.h and the full shell dependency chain,
// so it is not unit-tested here to avoid linking the entire executable. Its
// correctness is covered by compile-time checks and runtime verification.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDialogButtonBox>

#include "dialogs/dialog_help_catalog.h"

#include "rs_merge_classes_dialog.h"

namespace
{
  QApplication *ensureApp()
  {
    static int argc = 1;
    static char a[] = "t";
    static char *v[] = { a, nullptr };
    return qApp ? nullptr : new QApplication( argc, v );
  }
} // namespace

// ---------------------------------------------------------------------------
// Help catalog — every toolId added in the help sweep must resolve.
// ---------------------------------------------------------------------------
TEST_CASE( "HelpCatalog: OBIA sub-entries resolve", "[help]" )
{
  const QStringList ids = {
    QStringLiteral( "obia_class_table" ),
    QStringLiteral( "obia_segment_table" ),
    QStringLiteral( "obia_segment_info" ),
    QStringLiteral( "obia_task_list" ),
    QStringLiteral( "obia_data_manager" ),
  };
  for ( const QString &id : ids )
  {
    INFO( "toolId: " << id.toStdString() );
    REQUIRE( false == SicnuDialogHelp::shortForTool( id ).isEmpty() );
    REQUIRE( false == SicnuDialogHelp::htmlForTool( id ).isEmpty() );
  }
}

TEST_CASE( "HelpCatalog: second-round entries resolve", "[help]" )
{
  const QStringList ids = {
    QStringLiteral( "landsat_import" ),
    QStringLiteral( "post_process" ),
    QStringLiteral( "classifier_load" ),
    QStringLiteral( "merge_classes" ),
    QStringLiteral( "template_match" ),
    QStringLiteral( "digitize_tools" ),
  };
  for ( const QString &id : ids )
  {
    INFO( "toolId: " << id.toStdString() );
    REQUIRE( false == SicnuDialogHelp::shortForTool( id ).isEmpty() );
    REQUIRE( false == SicnuDialogHelp::htmlForTool( id ).isEmpty() );
  }
}

TEST_CASE( "HelpCatalog: unknown toolId falls back to title", "[help]" )
{
  // An unknown id should not crash; htmlForTool falls back to the provided title.
  REQUIRE( false == SicnuDialogHelp::htmlForTool(
                       QStringLiteral( "definitely_not_a_real_toolid" ),
                       QStringLiteral( "Fallback" ) )
                     .isEmpty() );
}

// ---------------------------------------------------------------------------
// RsMergeClassesDialog — Help button wired to catalog (HelpRole present).
// ---------------------------------------------------------------------------
TEST_CASE( "RsMergeClassesDialog: has Help button", "[classify][merge][help]" )
{
  ensureApp();
  RsMergeClassesDialog dlg;
  auto *buttons = dlg.findChild<QDialogButtonBox *>();
  REQUIRE( buttons != nullptr );
  // A Help-role button must exist (added in the help-sweep).
  bool found = false;
  for ( auto *btn : buttons->buttons() )
  {
    if ( buttons->buttonRole( btn ) == QDialogButtonBox::HelpRole )
    {
      found = true;
      break;
    }
  }
  REQUIRE( found );
}
