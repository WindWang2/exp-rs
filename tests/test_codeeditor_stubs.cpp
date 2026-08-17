#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QWidget>
#include "codeeditors/qgscodeeditorexpression.h"
#include "codeeditors/qgscodeeditorsql.h"
#include "codeeditors/qgscodeeditorhtml.h"
#include "qgsexpressionlineedit.h"
#include "qgsexpressionbuilderwidget.h"
#include "qgsrichtexteditor.h"
#include "qgshistogramwidget.h"
#include "symbology/qgsgraduatedhistogramwidget.h"
#include "qgscurveeditorwidget.h"
#include "qgsgradientcolorrampdialog.h"
#include "editorwidgets/qgsjsoneditwidget.h"
#include "providers/sensorthings/qgssensorthingssubseteditor.h"

static QApplication *getOrCreateApp()
{
  if ( !qApp )
  {
    static int argc = 1;
    static char appName[] = "test_codeeditor_stubs";
    static char *argv[] = { appName, nullptr };
    new QApplication( argc, argv );
  }
  return qApp;
}

TEST_CASE( "QgsCodeEditor stubs text retention and manipulation (#294)", "[codeeditor][stubs]" )
{
  getOrCreateApp();

  SECTION( "QgsCodeEditorExpression retains text" )
  {
    QgsCodeEditorExpression editor;
    REQUIRE( editor.text().isEmpty() );
    editor.setText( QStringLiteral( "$area > 1000 and \"density\" < 5.0" ) );
    REQUIRE( editor.text() == QStringLiteral( "$area > 1000 and \"density\" < 5.0" ) );
    REQUIRE( editor.length() == 32 );
    REQUIRE( editor.lines() == 1 );

    editor.append( QStringLiteral( "\n# second line" ) );
    REQUIRE( editor.lines() == 2 );
    REQUIRE( editor.text( 0 ) == QStringLiteral( "$area > 1000 and \"density\" < 5.0" ) );
    REQUIRE( editor.text( 1 ) == QStringLiteral( "# second line" ) );

    editor.clear();
    REQUIRE( editor.text().isEmpty() );
  }

  SECTION( "QgsCodeEditorSQL retains text" )
  {
    QgsCodeEditorSQL editor;
    editor.setText( QStringLiteral( "SELECT id, geom FROM layers WHERE status = 'active'" ) );
    REQUIRE( editor.text() == QStringLiteral( "SELECT id, geom FROM layers WHERE status = 'active'" ) );
    REQUIRE( editor.length() == 51 );
  }

  SECTION( "QgsCodeEditorHTML retains text" )
  {
    QgsCodeEditorHTML editor;
    editor.setText( QStringLiteral( "<b>bold</b>" ) );
    REQUIRE( editor.text() == QStringLiteral( "<b>bold</b>" ) );
  }
}

TEST_CASE( "Trimmed GUI widget stubs construct and operate safely (#290)", "[gui][stubs]" )
{
  getOrCreateApp();
  QWidget parent;

  SECTION( "QgsExpressionLineEdit" )
  {
    QgsExpressionLineEdit edit( &parent );
    edit.setObjectName( QStringLiteral( "testEdit" ) );
    REQUIRE( edit.objectName() == QStringLiteral( "testEdit" ) );
    edit.setExpression( QStringLiteral( "sqrt(\"elev\") > 100" ) );
    REQUIRE( edit.expression() == QStringLiteral( "sqrt(\"elev\") > 100" ) );
    edit.setExpressionDialogTitle( QStringLiteral( "Select Expression" ) );
    REQUIRE( edit.expressionDialogTitle() == QStringLiteral( "Select Expression" ) );
  }

  SECTION( "QgsExpressionBuilderWidget" )
  {
    QgsExpressionBuilderWidget builder( &parent );
    builder.setExpressionText( QStringLiteral( "concat('val:', \"name\")" ) );
    REQUIRE( builder.expressionText() == QStringLiteral( "concat('val:', \"name\")" ) );
  }

  SECTION( "QgsRichTextEditor" )
  {
    QgsRichTextEditor rich( &parent );
    rich.setText( QStringLiteral( "hello world" ) );
    REQUIRE( rich.toPlainText() == QStringLiteral( "hello world" ) );
  }

  SECTION( "QgsHistogramWidget and QgsGraduatedHistogramWidget" )
  {
    QgsHistogramWidget hw( &parent );
    QgsGraduatedHistogramWidget ghw( &parent );
    hw.refresh();
    ghw.refresh();
  }

  SECTION( "QgsCurveEditorWidget" )
  {
    QgsCurveEditorWidget cw( &parent );
    cw.setMinHistogramValueRange( 0.0 );
    cw.setMaxHistogramValueRange( 100.0 );
    REQUIRE( cw.minHistogramValueRange() == 0.0 );
    REQUIRE( cw.maxHistogramValueRange() == 100.0 );
  }

  SECTION( "QgsGradientColorRampDialog" )
  {
    QgsGradientColorRamp ramp( QColor( 255, 0, 0 ), QColor( 0, 255, 0 ) );
    QgsGradientColorRampDialog dlg( ramp, &parent );
    REQUIRE( dlg.ramp().color1() == QColor( 255, 0, 0 ) );
    REQUIRE( dlg.ramp().color2() == QColor( 0, 255, 0 ) );
  }

  SECTION( "QgsJsonEditWidget" )
  {
    QgsJsonEditWidget jw( &parent );
    jw.setJsonText( QStringLiteral( "{\"key\": 42}" ) );
    REQUIRE( jw.jsonText() == QStringLiteral( "{\"key\": 42}" ) );
  }

  SECTION( "QgsSensorThingsSubsetEditor" )
  {
    QgsSensorThingsSubsetEditor st( nullptr, QgsFields(), &parent );
    st.setSubsetString( QStringLiteral( "thing/id eq 1" ) );
    REQUIRE( st.subsetString() == QStringLiteral( "thing/id eq 1" ) );
  }
}
