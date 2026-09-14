// tests/test_guided_workflow_sync.cpp — guided workbench dual projection (D17 Package G)
//
// Ground truth: data/labs/lab02_spectral_analysis.lab.json — 5 steps, of
// which exactly 2 are operator-bound (rs:spectral_index "计算 NDVI 植被指数"
// with params {"index":"NDVI","red":4,"nir":5,...}, rs:band_math
// "自定义波段比值" with {"expression":"b5 / b4",...}); hand-verified with
// python3 -c json before writing these expectations.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <QApplication>
#include <QSignalSpy>
#include <QJsonObject>

#include "app/pipeline/guided_workflow_workbench.h"
#include "workflow/workflow_ir_v2.h"

using namespace sicnu::app::workbench;
using sicnu::workflow::WorkflowIR;

namespace {

QApplication *ensureApp()
{
    static QApplication *app = nullptr;
    if ( !app )
    {
        static int fake_argc = 1;
        static char fake_argv[] = "test_guided_workflow_sync";
        static char *fake_argv_ptr[] = { fake_argv };
        app = new QApplication( fake_argc, fake_argv_ptr );
    }
    return app;
}

QString lab02Path()
{
    return QString( "%1/data/labs/lab02_spectral_analysis.lab.json" ).arg( CMAKE_SOURCE_DIR );
}

} // namespace

TEST_CASE( "lab02 lifts into exactly two lab-step cards in topology order", "[d17][workflow][sync]" )
{
    ensureApp();
    GuidedWorkflowWidget widget;
    REQUIRE( widget.loadLabSpec( lab02Path() ) );
    REQUIRE( widget.loadError().isEmpty() );

    // Hand-verified lift: steps 0/1 (manual/UI) attach forward, steps 2/3
    // are operator nodes, step 4 (manual) attaches backward.
    const auto cards = widget.cards();
    REQUIRE( cards.size() == 2 );
    REQUIRE( cards[0].targetNodeId == QStringLiteral( "lab_step_1" ) );
    REQUIRE( cards[0].stepIndex == 0 );
    REQUIRE( cards[0].title == QStringLiteral( "计算 NDVI 植被指数" ) );
    REQUIRE( cards[1].targetNodeId == QStringLiteral( "lab_step_2" ) );
    REQUIRE( cards[1].stepIndex == 1 );
    REQUIRE( cards[1].title == QStringLiteral( "自定义波段比值" ) );

    // Forward attachment: the first card carries the two manual steps' titles.
    REQUIRE( cards[0].guidanceText.contains( QStringLiteral( "加载样本数据" ) ) );
    REQUIRE( cards[0].guidanceText.contains( QStringLiteral( "观察光谱曲线" ) ) );
    // Backward attachment: the last manual step lands on the final card.
    REQUIRE( cards[1].guidanceText.contains( QStringLiteral( "结果对比" ) ) );

    // Parameters lifted from the LabSpec.
    REQUIRE( cards[0].activeParameters["index"].toString() == QStringLiteral( "NDVI" ) );
    REQUIRE( cards[0].activeParameters["nir"].toInt() == 5 );
    REQUIRE( cards[1].activeParameters["expression"].toString() == QStringLiteral( "b5 / b4" ) );

    // The document underpinning both views is valid and serializes.
    const auto &def = widget.underlyingWorkflow();
    REQUIRE( WorkflowIR::validateSemantics( def ) );
    REQUIRE( def.nodes.size() == 2 );
    REQUIRE( def.edges.size() == 1 );
    REQUIRE( def.workflowId == QStringLiteral( "lab02_spectral_analysis" ) );
}

TEST_CASE( "Card-side edit lands in the document and emits exactly once", "[d17][workflow][sync]" )
{
    ensureApp();
    GuidedWorkflowWidget widget;
    REQUIRE( widget.loadLabSpec( lab02Path() ) );

    QSignalSpy spy( &widget, &GuidedWorkflowWidget::parameterChanged );
    widget.syncParameterToTopology( QStringLiteral( "lab_step_1" ), QStringLiteral( "threshold" ), 0.45 );

    REQUIRE( spy.count() == 1 );
    const auto arguments = spy.takeFirst();
    REQUIRE( arguments.at( 0 ).toString() == QStringLiteral( "lab_step_1" ) );
    REQUIRE( arguments.at( 1 ).toString() == QStringLiteral( "threshold" ) );
    REQUIRE( arguments.at( 2 ).value<QJsonValue>().toDouble() == Catch::Approx( 0.45 ).margin( 1e-6 ) );
    REQUIRE( widget.underlyingWorkflow().findNode( "lab_step_1" )->parameters["threshold"].toDouble()
             == Catch::Approx( 0.45 ).margin( 1e-6 ) );

    // The card projection followed the document.
    REQUIRE( widget.cards()[0].activeParameters["threshold"].toDouble() == Catch::Approx( 0.45 ).margin( 1e-6 ) );
}

TEST_CASE( "Reentrant parameter edits are suppressed (guard depth == 1)", "[d17][workflow][sync]" )
{
    ensureApp();
    GuidedWorkflowWidget widget;
    REQUIRE( widget.loadLabSpec( lab02Path() ) );

    // A listener that echoes the edit back — the classic card ⇄ canvas loop.
    int emissions = 0;
    int echoAttempts = 0;
    QObject::connect( &widget, &GuidedWorkflowWidget::parameterChanged, &widget,
                      [&]( const QString &nodeId, const QString &key, const QJsonValue &value ) {
                          ++emissions;
                          if ( emissions == 1 )
                          {
                              // Reentrant write triggered by the first emission.
                              ++echoAttempts;
                              widget.syncParameterToTopology( nodeId, key, value );
                          }
                      } );

    widget.syncParameterToTopology( QStringLiteral( "lab_step_1" ), QStringLiteral( "index" ), QStringLiteral( "NDVI" ) );

    REQUIRE( echoAttempts == 1 );
    REQUIRE( emissions == 1 ); // no echo loop: the guard swallowed the reentry
    REQUIRE( widget.underlyingWorkflow().findNode( "lab_step_1" )->parameters["index"].toString()
             == QStringLiteral( "NDVI" ) );
}

TEST_CASE( "Unknown node ids are no-ops and do not emit", "[d17][workflow][sync]" )
{
    ensureApp();
    GuidedWorkflowWidget widget;
    REQUIRE( widget.loadLabSpec( lab02Path() ) );

    QSignalSpy spy( &widget, &GuidedWorkflowWidget::parameterChanged );
    widget.syncParameterToTopology( QStringLiteral( "ghost_node" ), QStringLiteral( "x" ), 1 );
    REQUIRE( spy.count() == 0 );
}

TEST_CASE( "View mode flips state and the invalid LabSpec fails closed", "[d17][workflow][sync]" )
{
    ensureApp();
    GuidedWorkflowWidget widget;
    REQUIRE( widget.viewMode() == ViewMode::CardWizard );
    widget.setViewMode( ViewMode::TopologyCanvas );
    REQUIRE( widget.viewMode() == ViewMode::TopologyCanvas );

    REQUIRE_FALSE( widget.loadLabSpec( QStringLiteral( "/nonexistent/lab09_does_not_exist.lab.json" ) ) );
    REQUIRE_FALSE( widget.loadError().isEmpty() );
    REQUIRE( widget.cards().isEmpty() );
}

TEST_CASE( "Adopting a pre-built document works and validates fail-closed", "[d17][workflow][sync]" )
{
    ensureApp();
    GuidedWorkflowWidget widget;

    sicnu::workflow::WorkflowDefinition def;
    def.workflowId = QStringLiteral( "wf-manual" );
    sicnu::workflow::NodeFact node;
    node.nodeId = QStringLiteral( "only" );
    node.operatorId = QStringLiteral( "rs:step" );
    node.outputPorts = { sicnu::workflow::PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ),
                                                    QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 1, false } };
    def.nodes = { node };

    REQUIRE( widget.setUnderlyingWorkflow( def ) );
    REQUIRE( widget.cards().isEmpty() ); // no lab flags -> no cards

    sicnu::workflow::WorkflowDefinition broken = def;
    broken.nodes.append( sicnu::workflow::NodeFact{} ); // empty nodeId/operatorId
    REQUIRE_FALSE( widget.setUnderlyingWorkflow( broken ) );
    REQUIRE_FALSE( widget.loadError().isEmpty() );
}
