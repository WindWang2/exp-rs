// test_r29_t04_step_sync.cpp — D6 (R29-T04).
//
// Step identity drives the why pane, the parameter preview and the manual
// draft: navigating forward/back/clicking a step (or switching labs) must
// never leave the PREVIOUS step's explanation or answer on screen, and an
// author-less step says so instead of inheriting its neighbour's hint.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QFile>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTranslator>

#include "guided_lab_workspace.h"
#include "teaching/lab_draft_store.h"

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

QString seedRefusingDraftStore( const QTemporaryDir &dir, bool publicationFails )
{
    // A 255-byte final leaf is valid on NTFS/ext4, but the authority's
    // required unique staged leaf exceeds NAME_MAX: a real write refusal
    // after the same document was successfully read, without permission or
    // platform-specific fault injection.
    const QString leaf = publicationFails
                           ? QString( 250, QLatin1Char( 'd' ) ) + QStringLiteral( ".json" )
                           : QStringLiteral( "drafts.json" );
    const QString path = dir.filePath( leaf );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    const QByteArray bytes = R"({"schema":"sicnu.teaching.lab_drafts/1","drafts":[{"course_id":"course-A","lab_id":"lab_sync","step_id":"step_one","text":"saved old answer"}]})";
    REQUIRE( file.write( bytes ) == bytes.size() );
    file.close();
    REQUIRE( sicnu::teaching::loadLabDrafts( path.toStdString() ).ok );
    return path;
}

void damageDraftStore( const QString &path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( file.write( "{ broken document" ) > 0 );
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

TEST_CASE( "R30-T07: restoring a draft does not erase its stale provenance",
           "[r30_t07][teaching][steps][draft_provenance]" )
{
    ensureApp();
    QTranslator translator;
    SECTION( "English" ) {}
    SECTION( "Chinese with the shipped catalog" )
    {
        REQUIRE( translator.load( QStringLiteral( SICNU_TEST_TRANSLATIONS_DIR "/sicnu_zh_CN.qm" ) ) );
        REQUIRE( QCoreApplication::installTranslator( &translator ) );
        const char *source = "Restored draft for step %1 is stale: %2";
        CHECK( QCoreApplication::translate( GuidedLabWorkspace::staticMetaObject.className(), source )
               != QString::fromUtf8( source ) );
    }
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( "drafts.json" );
    sicnu::teaching::LabDraftRecord original;
    original.courseId = "course-A";
    original.labId = "lab_sync";
    original.stepId = "step_one";
    original.text = "answer written against old rules and artifact";
    original.rulesVersion = "rules-v1";
    original.artifactPath = "old-artifact.tif";
    std::string error;
    REQUIRE( sicnu::teaching::saveLabDrafts( path.toStdString(), { original }, &error ) );

    GuidedLabWorkspace ws;
    // This is the real cockpit's construction/openLab order: configure
    // store, restore artifact + timeline, THEN resolve the current rules.
    ws.setDraftStore( path, QStringLiteral( "course-A" ) );
    ws.setArtifactPath( QStringLiteral( "replacement-artifact.tif" ) );
    ws.setTimeline( twoStepTimeline( "lab_sync" ) );
    REQUIRE( humanInputOf( &ws ) );
    CHECK( humanInputOf( &ws )->toPlainText() == QString::fromStdString( original.text ) );
    CHECK( ws.refreshDraftValidity( QStringLiteral( "rules-v2" ) ) );
    CHECK( ws.draftStatusText().contains( QStringLiteral( "rules_changed" ) ) );

    // Merely opening, hiding, or navigating an unchanged restored draft
    // must not re-stamp its old answer as authored under the new context.
    ws.flushDrafts();
    const auto retained = sicnu::teaching::loadLabDrafts( path.toStdString() );
    REQUIRE( retained.ok );
    REQUIRE( retained.drafts.size() == 1 );
    CHECK( retained.drafts.front().rulesVersion == original.rulesVersion );
    CHECK( retained.drafts.front().artifactPath == original.artifactPath );
    const auto validity = sicnu::teaching::checkLabDraftValidity(
        retained.drafts.front(), "rules-v2", "replacement-artifact.tif" );
    CHECK_FALSE( validity.valid );
    CHECK( validity.reasons.size() == 2 );
    QCoreApplication::removeTranslator( &translator );
}

TEST_CASE( "R30-T07: same-lab reprojection preserves edits newer than the saved draft",
           "[r30_t07][teaching][steps][draft_reprojection]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( "drafts.json" );
    GuidedLabWorkspace ws;
    ws.setDraftStore( path, QStringLiteral( "course-A" ) );
    const auto timeline = twoStepTimeline( "lab_sync" );
    ws.setTimeline( timeline );
    auto *human = humanInputOf( &ws );
    REQUIRE( human );
    human->setPlainText( QStringLiteral( "previously saved answer" ) );
    ws.flushDrafts();
    human->setPlainText( QStringLiteral( "new live answer, not flushed yet" ) );
    ws.setTimeline( timeline );
    CHECK( human->toPlainText() == QStringLiteral( "new live answer, not flushed yet" ) );
    ws.flushDrafts();
    const auto persisted = sicnu::teaching::loadLabDrafts( path.toStdString() );
    REQUIRE( persisted.ok );
    REQUIRE( persisted.drafts.size() == 1 );
    CHECK( persisted.drafts.front().text == "new live answer, not flushed yet" );
}

TEST_CASE( "R30-T07: changing draft context parks the old course and starts clean",
           "[r30_t07][teaching][steps][draft_context]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString oldPath = dir.filePath( "drafts.json" );
    QString newPath = oldPath;
    QString newCourse = QStringLiteral( "course-B" );
    SECTION( "same storage and lab, different course" ) {}
    SECTION( "same course and lab, different storage" )
    {
        newPath = dir.filePath( "another-draft-store.json" );
        newCourse = QStringLiteral( "course-A" );
    }
    GuidedLabWorkspace ws;
    ws.setDraftStore( oldPath, QStringLiteral( "course-A" ) );
    ws.setTimeline( twoStepTimeline( "lab_sync" ) );
    auto *human = humanInputOf( &ws );
    REQUIRE( human );
    human->setPlainText( QStringLiteral( "course-A unsaved answer" ) );
    ws.setDraftStore( newPath, newCourse );
    CHECK( human->toPlainText().isEmpty() );
    ws.flushDrafts();
    const auto oldStore = sicnu::teaching::loadLabDrafts( oldPath.toStdString() );
    REQUIRE( oldStore.ok );
    bool parkedA = false;
    for ( const auto &record : oldStore.drafts )
        if ( record.courseId == "course-A" && record.stepId == "step_one"
             && record.text == "course-A unsaved answer" )
            parkedA = true;
    CHECK( parkedA );
    const auto newStore = sicnu::teaching::loadLabDrafts( newPath.toStdString() );
    REQUIRE( newStore.ok );
    for ( const auto &record : newStore.drafts )
        if ( record.courseId == newCourse.toStdString()
             && ( newPath != oldPath || newCourse != QStringLiteral( "course-A" ) ) )
            CHECK( record.text != "course-A unsaved answer" );

    // Repeating the same context is not a switch and must preserve live text.
    human->setPlainText( QStringLiteral( "new context live answer" ) );
    ws.setDraftStore( newPath, newCourse );
    CHECK( human->toPlainText() == QStringLiteral( "new context live answer" ) );
}

TEST_CASE( "R30-T07: a refused draft save keeps the old context and its only live text",
           "[r30_t07][teaching][steps][draft_save_refusal]" )
{
    ensureApp();
    bool publicationFails = false;
    SECTION( "existing store is damaged" ) {}
    SECTION( "readable store cannot publish a staged replacement" ) { publicationFails = true; }
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString oldPath = seedRefusingDraftStore( dir, publicationFails );
    const QString newPath = dir.filePath( "other-course.json" );
    GuidedLabWorkspace ws;
    ws.setDraftStore( oldPath, QStringLiteral( "course-A" ) );
    ws.setTimeline( twoStepTimeline( "lab_sync" ) );
    auto *human = humanInputOf( &ws );
    REQUIRE( human );
    human->setPlainText( QStringLiteral( "only live course-A answer" ) );
    if ( !publicationFails ) damageDraftStore( oldPath );
    ws.setDraftStore( newPath, QStringLiteral( "course-B" ) );
    CHECK( human->toPlainText() == QStringLiteral( "only live course-A answer" ) );
    CHECK_FALSE( ws.draftStatusText().isEmpty() );
    ws.flushDrafts();
    // Still bound to the failed old context: retry cannot leak A into B.
    CHECK_FALSE( QFile::exists( newPath ) );
    CHECK( human->toPlainText() == QStringLiteral( "only live course-A answer" ) );
    if ( publicationFails )
        CHECK( ws.draftStatusText().contains( QStringLiteral( "lab_draft_store.write_failed" ) ) );
}

TEST_CASE( "R30-T07: a refused draft save prevents switching to another lab",
           "[r30_t07][teaching][steps][draft_lab_save_refusal]" )
{
    ensureApp();
    bool publicationFails = false;
    SECTION( "existing store is damaged" ) {}
    SECTION( "readable store cannot publish a staged replacement" ) { publicationFails = true; }
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = seedRefusingDraftStore( dir, publicationFails );
    GuidedLabWorkspace ws;
    ws.setDraftStore( path, QStringLiteral( "course-A" ) );
    ws.setTimeline( twoStepTimeline( "lab_sync" ) );
    auto *human = humanInputOf( &ws );
    REQUIRE( human );
    human->setPlainText( QStringLiteral( "only live lab-A answer" ) );
    ws.setArtifactPath( QStringLiteral( "old-artifact.tif" ) );
    if ( !publicationFails ) damageDraftStore( path );
    ws.setTimeline( twoStepTimeline( "lab_other" ) );
    CHECK( ws.timeline().labId == "lab_sync" );
    CHECK( human->toPlainText() == QStringLiteral( "only live lab-A answer" ) );
    CHECK( ws.artifactPath() == QStringLiteral( "old-artifact.tif" ) );
    CHECK_FALSE( ws.draftStatusText().isEmpty() );
}
