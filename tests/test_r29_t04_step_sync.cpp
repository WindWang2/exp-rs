// test_r29_t04_step_sync.cpp — D6 (R29-T04).
//
// Step identity drives the why pane, the parameter preview and the manual
// draft: navigating forward/back/clicking a step (or switching labs) must
// never leave the PREVIOUS step's explanation or answer on screen, and an
// author-less step says so instead of inheriting its neighbour's hint.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>

#include "guided_lab_workspace.h"

#include <json/json.h>

using namespace sicnu::app::teaching;

namespace
{

QApplication *ensureApp()
{
    if ( !QApplication::instance() )
    {
        static int argc = 1;
        static char name[] = "test_r29_t04_step_sync";
        static char *argv[] = { name, nullptr };
        static QApplication app( argc, argv );
        return &app;
    }
    return static_cast<QApplication *>( QApplication::instance() );
}

sicnu::teaching::LabStepTimeline twoStepTimeline( const std::string &labId )
{
    // Minimal LabSpec: two steps, DIFFERENT why hints and params so any
    // leakage across steps is observable in the panes.
    Json::Value doc( Json::objectValue );
    doc["id"] = labId;
    doc["title_zh"] = labId;
    Json::Value steps( Json::arrayValue );
    {
        Json::Value s( Json::objectValue );
        s["id"] = "step_one";
        s["title_zh"] = "第一步";
        s["teaching_note"] = "解释属于第一步";
        s["operator_id"] = "rs:threshold_raster";
        Json::Value params( Json::objectValue );
        params["threshold"] = 0.3;
        s["params"] = params;
        steps.append( s );
    }
    {
        Json::Value s( Json::objectValue );
        s["id"] = "step_two";
        s["title_zh"] = "第二步";
        // Deliberately NO teaching_note: the pane must say so, not inherit.
        steps.append( s );
    }
    doc["steps"] = steps;
    return sicnu::teaching::LabStepTimeline::fromLabDocument( doc, 0 );
}

QTextEdit *whyViewOf( GuidedLabWorkspace *ws )
{
    // Stable locator: the why pane is the QTextEdit whose placeholder names
    // it (three QTextEdits live in this workspace).
    for ( QTextEdit *edit : ws->findChildren<QTextEdit *>() )
        if ( edit->placeholderText().contains( QStringLiteral( "Why-this-step" ) ) )
            return edit;
    return nullptr;
}

QPlainTextEdit *humanInputOf( GuidedLabWorkspace *ws )
{
    // The manual draft is the only QPlainTextEdit child.
    return ws->findChild<QPlainTextEdit *>();
}

QListWidget *stepListOf( GuidedLabWorkspace *ws )
{
    return ws->findChild<QListWidget *>();
}

} // namespace

TEST_CASE( "D6: why pane follows the CURRENT step, empty hints are explicit",
           "[r29_t04][teaching][steps][why]" )
{
    ensureApp();
    GuidedLabWorkspace ws;
    ws.setTimeline( twoStepTimeline( "lab_sync" ) );
    auto *why = whyViewOf( &ws );
    auto *list = stepListOf( &ws );
    REQUIRE( why != nullptr );
    REQUIRE( list != nullptr );
    REQUIRE( list->count() == 2 );

    // Step 1 selected: its own explanation is on screen.
    REQUIRE( list->currentRow() == 0 );
    CHECK( why->toPlainText().contains( QStringLiteral( "解释属于第一步" ) ) );

    // Next: step 2 has NO author hint — the pane must not keep step 1's
    // text, and must say the author provided none.
    auto *next = [&]() -> QPushButton * {
        for ( QPushButton *button : ws.findChildren<QPushButton *>() )
            if ( button->text() == QLatin1String( "Next" ) )
                return button;
        return nullptr;
    }();
    REQUIRE( next != nullptr );
    next->click();
    REQUIRE( list->currentRow() == 1 );
    const QString why2 = why->toPlainText();
    CHECK_FALSE( why2.contains( QStringLiteral( "解释属于第一步" ) ) );
    CHECK( why2.contains( QStringLiteral( "author provided no explanation" ) ) );

    // Back: step 1's explanation RETURNS (not cached-away forever).
    for ( QPushButton *button : ws.findChildren<QPushButton *>() )
        if ( button->text() == QLatin1String( "Previous" ) )
            button->click();
    REQUIRE( list->currentRow() == 0 );
    CHECK( why->toPlainText().contains( QStringLiteral( "解释属于第一步" ) ) );

    // Clicking the step list row directly synchronizes too.
    list->setCurrentRow( 1 );
    CHECK_FALSE( why->toPlainText().contains( QStringLiteral( "解释属于第一步" ) ) );
}

TEST_CASE( "D6: manual drafts are per-step and survive navigation",
           "[r29_t04][teaching][steps][drafts]" )
{
    ensureApp();
    GuidedLabWorkspace ws;
    ws.setTimeline( twoStepTimeline( "lab_sync" ) );
    auto *list = stepListOf( &ws );
    auto *human = humanInputOf( &ws );
    REQUIRE( list != nullptr );
    REQUIRE( human != nullptr );

    human->setPlainText( QStringLiteral( "第一题的作答" ) );
    list->setCurrentRow( 1 );
    // Step 2 starts from ITS OWN (empty) draft — the previous step's answer
    // must not sit in the form.
    CHECK( human->toPlainText().isEmpty() );

    human->setPlainText( QStringLiteral( "第二题的作答" ) );
    list->setCurrentRow( 0 );
    CHECK( human->toPlainText() == QStringLiteral( "第一题的作答" ) );
    list->setCurrentRow( 1 );
    CHECK( human->toPlainText() == QStringLiteral( "第二题的作答" ) );
}

TEST_CASE( "D6: switching labs never carries the previous lab's draft or why",
           "[r29_t04][teaching][steps][labswitch]" )
{
    ensureApp();
    GuidedLabWorkspace ws;
    ws.setTimeline( twoStepTimeline( "lab_sync" ) );
    auto *human = humanInputOf( &ws );
    auto *why = whyViewOf( &ws );
    REQUIRE( human != nullptr );
    REQUIRE( why != nullptr );
    human->setPlainText( QStringLiteral( "lab_sync 的作答" ) );

    // Same step ids in ANOTHER lab (alias id scenario): the draft keys by
    // lab+step, and a different lab starts clean.
    ws.setTimeline( twoStepTimeline( "lab_other" ) );
    CHECK( human->toPlainText().isEmpty() );

    // Back to the first lab: the draft is NOT resurrected (a lab switch is
    // a fresh sheet), and the why pane reflects the current lab's step 1.
    ws.setTimeline( twoStepTimeline( "lab_sync" ) );
    CHECK( human->toPlainText().isEmpty() );
    CHECK( why->toPlainText().contains( QStringLiteral( "解释属于第一步" ) ) );
}
