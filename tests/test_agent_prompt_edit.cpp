#include "agent/widgets/agent_prompt_edit.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QTest>
#include <QTextBlock>
#include <QTextFragment>

using sicnu::agent::AgentPromptEdit;

class AgentPromptEditTest : public QObject
{
    Q_OBJECT
  private slots:
    void explicitSendAndLiteralNewlines()
    {
      AgentPromptEdit edit;
      int submissions = 0;
      edit.setSubmitHandler( [&] { ++submissions; } );
      edit.setPlainText( QStringLiteral( "NDVI" ) );
      edit.moveCursor( QTextCursor::End );
      QTest::keyClick( &edit, Qt::Key_Return );
      QTest::keyClick( &edit, Qt::Key_Return, Qt::ShiftModifier );
      QCOMPARE( edit.toPlainText(), QStringLiteral( "NDVI\n\n" ) );
      QCOMPARE( submissions, 0 );
      QTest::keyClick( &edit, Qt::Key_Return, Qt::ControlModifier );
      QTest::keyClick( &edit, Qt::Key_Enter, Qt::ControlModifier | Qt::KeypadModifier );
      QCOMPARE( submissions, 2 );
      QCOMPARE( edit.toPlainText(), QStringLiteral( "NDVI\n\n" ) );
      QKeyEvent repeat( QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier, QString(), true );
      QApplication::sendEvent( &edit, &repeat );
      QCOMPARE( submissions, 2 );
    }

    void clipboardPastesOnlyLiteralText()
    {
      AgentPromptEdit edit;
      auto *mime = new QMimeData;
      mime->setHtml( QStringLiteral( "<b>NDVI</b><img src='https://invalid.example/track'>" ) );
      mime->setText( QStringLiteral( "NDVI" ) );
      QApplication::clipboard()->setMimeData( mime );
      edit.paste();
      QCOMPARE( edit.toPlainText(), QStringLiteral( "NDVI" ) );
      QVERIFY( !edit.acceptRichText() );
      QVERIFY( edit.tabChangesFocus() );
      QVERIFY( !edit.document()->begin().begin().fragment().charFormat().font().bold() );
    }

    void heightIsBoundedAndReflowsWithFontAndWidth()
    {
      AgentPromptEdit edit;
      edit.resize( 400, 60 );
      edit.show();
      QCoreApplication::processEvents();
      const int compact = edit.height();
      edit.setPlainText( QString( 10000, QLatin1Char( 'x' ) ) );
      QCoreApplication::processEvents();
      QVERIFY( edit.height() > compact );
      QVERIFY( edit.height() <= 6 * edit.fontMetrics().lineSpacing() + 2 * edit.frameWidth()
                                  + 2 * edit.document()->documentMargin() + 1 );
      edit.clear();
      QCoreApplication::processEvents();
      QCOMPARE( edit.height(), compact );
      auto font = edit.font();
      font.setPointSize( 24 );
      edit.setFont( font );
      QCoreApplication::processEvents();
      QVERIFY( edit.height() > compact );
      edit.setPlainText( QStringLiteral( "Compute NDVI on the active Landsat image and verify every output pixel." ) );
      edit.resize( 160, edit.height() );
      QCoreApplication::processEvents();
      const int narrow = edit.height();
      edit.resize( 800, edit.height() );
      QCoreApplication::processEvents();
      QVERIFY( edit.height() < narrow );
    }
};

QTEST_MAIN( AgentPromptEditTest )
#include "test_agent_prompt_edit.moc"
