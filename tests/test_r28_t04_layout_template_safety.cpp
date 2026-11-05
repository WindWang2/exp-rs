// test_r28_t04_layout_template_safety.cpp — #1805 F12: a template load
// failure must leave the working layout untouched.
//
// The old onLoadFromTemplate fed QgsLayout::loadFromTemplate(clearExisting=
// true) directly: the layout was cleared BEFORE the template XML was
// restored, so a template that parsed as XML but failed to restore (wrong
// root, foreign namespace, truncated item DOM) emptied every item with no
// undo path. The fix probes the template on a QgsPrintLayout clone first;
// this suite pins that contract headlessly through the extracted
// loadTemplateFromFile() (the QFileDialog wrapper stays untested by design).
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDomDocument>
#include <QFile>
#include <QPointer>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgslayoutitemlabel.h>
#include <qgsproject.h>
#include <qgsprintlayout.h>
#include <qgsreadwritecontext.h>

#include "app/layout/qgslayoutdesignerdialog.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
  static int argc = 1;
  static char appName[] = "test_r28_t04_layout_template";
  static char *argv[] = { appName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

/// Writes @p contents to a temp .qpt file.
QString writeTemplate( const QTemporaryDir &dir, const QString &name,
                       const QByteArray &contents )
{
  const QString path = dir.filePath( name );
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly ) );
  file.write( contents );
  file.close();
  return path;
}

int itemCount( QgsPrintLayout *layout )
{
  QList<QgsLayoutItem *> items;
  layout->layoutItems( items );
  return items.count();
}

} // namespace

TEST_CASE( "template load failure preserves the working layout items (F12)",
           "[r28t04][layout][template][f12]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  auto *project = QgsProject::instance();
  QgsPrintLayout *layout = new QgsPrintLayout( project );
  layout->initializeDefaults();
  layout->setName( QStringLiteral( "f12_safety_case" ) );

  auto *label = new QgsLayoutItemLabel( layout );
  label->setText( QStringLiteral( "working content" ) );
  layout->addLayoutItem( label );
  REQUIRE( itemCount( layout ) >= 2 ); // page + label

  auto *designer = new QgsLayoutDesignerDialog( layout, nullptr, nullptr );
  REQUIRE( designer->layout() == layout );

  SECTION( "well-formed XML that is not a layout template refuses and preserves" )
  {
    const QString path = writeTemplate(
      dir, QStringLiteral( "foreign.qpt" ),
      QByteArray( "<?xml version=\"1.0\"?><html><body>not a template</body></html>" ) );
    QString error;
    CHECK_FALSE( designer->loadTemplateFromFile( path, &error ) );
    CHECK_FALSE( error.isEmpty() );
    CHECK( itemCount( designer->layout() ) >= 2 );
    // The working label itself survived, not just the page count.
    bool labelSurvived = false;
    QList<QgsLayoutItem *> items;
    designer->layout()->layoutItems( items );
    for ( QgsLayoutItem *item : items )
    {
      auto *survivor = dynamic_cast<QgsLayoutItemLabel *>( item );
      if ( survivor && survivor->text() == QLatin1String( "working content" ) )
        labelSurvived = true;
    }
    CHECK( labelSurvived );
  }

  SECTION( "garbage bytes refuse with a readable error and preserve" )
  {
    const QString path = writeTemplate( dir, QStringLiteral( "garbage.qpt" ),
                                        QByteArray( "\x00\x01 not xml at all" ) );
    QString error;
    CHECK_FALSE( designer->loadTemplateFromFile( path, &error ) );
    CHECK_FALSE( error.isEmpty() );
    CHECK( itemCount( designer->layout() ) >= 2 );
  }

  SECTION( "a valid saved template replaces the items" )
  {
    // Save the current layout as a template, then load it back: this is the
    // success path — items are replaced, ok reported.
    const QString saved = dir.filePath( QStringLiteral( "good.qpt" ) );
    QgsReadWriteContext ctx;
    REQUIRE( layout->saveAsTemplate( saved, ctx ) );

    QString error;
    CHECK( designer->loadTemplateFromFile( saved, &error ) );
    CHECK( error.isEmpty() );
    CHECK( itemCount( designer->layout() ) >= 2 );
  }

  SECTION( "unreadable path refuses typed" )
  {
    QString error;
    CHECK_FALSE( designer->loadTemplateFromFile(
      dir.filePath( QStringLiteral( "does-not-exist.qpt" ) ), &error ) );
    CHECK_FALSE( error.isEmpty() );
  }

  // Mirror the suite's teardown discipline (WA_DeleteOnClose-safe).
  QPointer<QgsLayoutDesignerDialog> guard( designer );
  designer->close();
  QApplication::processEvents();
  delete guard.data();
  QApplication::processEvents();
  QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  delete layout;
}
