/***************************************************************************
 * test_ux_guidance_corpus.cpp — F20 work package H
 *
 * Scenario corpus for the guidance system: for each canonical UX failure
 * surface (error code, uncatalogued code, disabled command, empty workspace,
 * missing model, offline provider) the corpus asserts what the user is
 * told — a resolvable topic, a non-empty Chinese explanation, a concrete
 * next step, and no credential-shaped or machine-path leakage. Expectations
 * are hard-coded here; nothing derives them from the code under test.
 ***************************************************************************/
#include "app/help/contextual_help_resolver.h"

#include "help/adapters/operator_help_source.h"
#include "help/diagnostic_catalog.h"
#include "help/error_diagnostics_bridge.h"
#include "help/help_composition.h"
#include "help/help_id.h"
#include "help/help_registry.h"

#include "agent/harness/harness_error.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "workbench/selection_context.h"

#include <QRegularExpression>
#include <QStringList>

#include <catch2/catch_test_macros.hpp>

using namespace sicnu::help;
using sicnu::app::ContextualGuidance;
using sicnu::app::ContextualHelpResolver;

namespace
{

const CompositionReport &composed()
{
    static CompositionReport report = [] {
        sicnu::help::adapters::OperatorHelpSource source;
        return composeHelpSystem( globalHelpRegistry(), nullptr, &source );
    }();
    return report;
}

/// Credential/machine-path shapes that must never surface in user guidance.
bool leaksSecretShape( const QString &text )
{
    static const QRegularExpression re(
        QStringLiteral( "password|passwd|secret|api[_-]?key|token=|Bearer |C:\\\\Users|/home/" ),
        QRegularExpression::CaseInsensitiveOption );
    return re.match( text ).hasMatch();
}

void checkNoLeak( const QStringList &texts, const char *scenario )
{
    for ( const QString &t : texts ) {
        INFO( scenario << " text: " << t.toStdString() );
        CHECK_FALSE( leaksSecretShape( t ) );
    }
}

sicnu::app::SelectionContextSnapshot emptyWorkspace()
{
    sicnu::app::SelectionContextSnapshot s;
    s.layerCount = 0;
    return s;
}

} // namespace

// ── scenario: known harness error code ─────────────────────────────────────

TEST_CASE( "corpus: CRS_MISMATCH resolves to a curated, actionable page",
           "[ux-corpus][diagnostics]" )
{
    composed();
    DiagnosticCatalog catalog( globalHelpRegistry() );
    const HelpDescriptor d = catalog.resolve( DiagnosticFamily::Harness, "CRS_MISMATCH" );

    CHECK_FALSE( d.diagnostic->technicalNote.contains( QStringLiteral( "fallback" ) ) );
    CHECK( d.diagnostic->severity == sicnu::data::DiagnosticSeverity::Error );
    CHECK_FALSE( d.diagnostic->whatHappened.isEmpty() );
    CHECK_FALSE( d.diagnostic->whyItMatters.isEmpty() );
    REQUIRE_FALSE( d.diagnostic->remediation.isEmpty() );
    // the wire code survives byte-identical for copy/paste into reports
    CHECK( d.diagnostic->originCode == QStringLiteral( "CRS_MISMATCH" ) );
    checkNoLeak( QStringList{ d.title, d.summary, d.diagnostic->whatHappened,
                              d.diagnostic->whyItMatters }
                     << d.diagnostic->remediation,
                 "CRS_MISMATCH" );
}

// ── scenario: uncatalogued error code (honest fallback) ────────────────────

TEST_CASE( "corpus: unknown codes get the honest fallback, not a guess",
           "[ux-corpus][diagnostics]" )
{
    composed();
    DiagnosticCatalog catalog( globalHelpRegistry() );
    const HelpDescriptor d = catalog.resolve( DiagnosticFamily::Harness, "TOTALLY_UNKNOWN_CODE" );

    REQUIRE( d.diagnostic.has_value() );
    CHECK( d.diagnostic->originCode == QStringLiteral( "TOTALLY_UNKNOWN_CODE" ) );
    CHECK( d.diagnostic->technicalNote.contains( QStringLiteral( "fallback" ) ) );
    // honest generic remediation, still non-empty
    CHECK_FALSE( d.diagnostic->remediation.isEmpty() );
    checkNoLeak( QStringList{ d.diagnostic->whatHappened, d.diagnostic->whyItMatters }
                     << d.diagnostic->remediation,
                 "unknown code" );
}

// ── scenario: disabled command over an empty workspace ─────────────────────

TEST_CASE( "corpus: rs.spectralIndex disabled over empty workspace explains and suggests",
           "[ux-corpus][availability]" )
{
    composed();
    const ContextualGuidance g = ContextualHelpResolver::resolve(
        emptyWorkspace(), nullptr, QStringLiteral( "rs.spectralIndex" ) );

    CHECK_FALSE( g.available );
    CHECK_FALSE( g.unavailableReason.isEmpty() );
    CHECK( g.unavailableReasonCode == QStringLiteral( "raster.selected" ) );
    CHECK( g.suggestedCommandId == QStringLiteral( "layer.addRaster" ) );
    CHECK_FALSE( g.detailHelpId.isEmpty() );
    CHECK( g.detailHelpId == QStringLiteral( "command.rs.spectralIndex" ) );
    checkNoLeak( QStringList{ g.shortTip, g.unavailableReason }, "rs.spectralIndex disabled" );
}

// ── scenario: command without an availability predicate ────────────────────

TEST_CASE( "corpus: workbench.obia declares no predicate, so facts report available",
           "[ux-corpus][availability]" )
{
    composed();
    // OBIA opens its own window and declares no availability predicate in
    // command_defs.cpp — empty facts must honestly mean "available", never a
    // fabricated reason.
    const ContextualGuidance g = ContextualHelpResolver::resolve(
        emptyWorkspace(), nullptr, QStringLiteral( "workbench.obia" ) );

    CHECK( g.available );
    CHECK( g.unavailableReason.isEmpty() );
    CHECK( g.unavailableReasonCode.isEmpty() );
    CHECK( g.detailHelpId == QStringLiteral( "command.workbench.obia" ) );
    checkNoLeak( QStringList{ g.shortTip }, "workbench.obia available" );
}

// ── scenario: disabled command yields topic + machine reason ───────────────

TEST_CASE( "corpus: layer.zoomTo without a selection carries reason and topic",
           "[ux-corpus][availability]" )
{
    composed();
    const ContextualGuidance g = ContextualHelpResolver::resolve(
        emptyWorkspace(), nullptr, QStringLiteral( "layer.zoomTo" ) );

    CHECK_FALSE( g.available );
    CHECK( g.unavailableReasonCode == QStringLiteral( "layer.selected" ) );
    CHECK_FALSE( g.unavailableReason.isEmpty() );
    CHECK( g.detailHelpId == QStringLiteral( "command.layer.zoomTo" ) );
}

// ── scenario: empty workspace next step ────────────────────────────────────

TEST_CASE( "corpus: empty workspace surface guidance suggests importing data",
           "[ux-corpus][surface]" )
{
    composed();
    const ContextualGuidance g = ContextualHelpResolver::resolveForSurface(
        emptyWorkspace(), QStringLiteral( "workbench.map" ) );

    CHECK( g.suggestedCommandId == QStringLiteral( "project.importLayer" ) );
    CHECK_FALSE( g.suggestedCommandText.isEmpty() );
    CHECK_FALSE( g.detailHelpId.isEmpty() );
    checkNoLeak( QStringList{ g.shortTip, g.suggestedCommandText }, "empty workspace" );
}

// ── scenario: model not ready / offline provider ───────────────────────────

TEST_CASE( "corpus: MODEL_NOT_READY has curated guidance for the offline case",
           "[ux-corpus][diagnostics]" )
{
    composed();
    DiagnosticCatalog catalog( globalHelpRegistry() );
    const HelpDescriptor d = catalog.resolve( DiagnosticFamily::Harness, "MODEL_NOT_READY" );
    CHECK_FALSE( d.diagnostic->technicalNote.contains( QStringLiteral( "fallback" ) ) );
    REQUIRE_FALSE( d.diagnostic->remediation.isEmpty() );
    checkNoLeak( QStringList{ d.title, d.summary } << d.diagnostic->remediation,
                 "MODEL_NOT_READY" );
}

// ── scenario: failure text → help topic bridge ─────────────────────────────

TEST_CASE( "corpus: failure messages link to curated topics without guessing",
           "[ux-corpus][bridge]" )
{
    composed();
    ErrorDiagnosticsBridge bridge( globalHelpRegistry() );

    CHECK( bridge.helpIdFromMessage(
               QStringLiteral( "执行失败: CRS_MISMATCH (EPSG:4326 vs EPSG:32650)" ) )
           == QStringLiteral( "diagnostic.harness.crs_mismatch" ) );
    CHECK( bridge.helpIdFromMessage(
               QStringLiteral( "operator FileNotFound while opening D:\\data\\x.tif" ) )
           == QStringLiteral( "diagnostic.operator.file_not_found" ) );
    // no curated token in the message → no topic (never a guess)
    CHECK( bridge.helpIdFromMessage( QStringLiteral( "完全无法理解的消息" ) ).isEmpty() );
    CHECK( bridge.helpIdFromMessage(
               QStringLiteral( "unknown thing TOTALLY_UNKNOWN_CODE happened" ) )
           .isEmpty() );
}

// ── scenario: no help text in the whole corpus leaks secret shapes ─────────

TEST_CASE( "corpus: every diagnostic page stays free of secret-shaped text",
           "[ux-corpus][secrets]" )
{
    composed();
    DiagnosticCatalog catalog( globalHelpRegistry() );
    for ( const HelpDescriptor *d : globalHelpRegistry().all() ) {
        if ( d->kind != HelpKind::Diagnostic || !d->diagnostic.has_value() )
            continue;
        QStringList texts{ d->title, d->summary, d->diagnostic->whatHappened,
                           d->diagnostic->whyItMatters, d->diagnostic->technicalNote };
        texts << d->keywords << d->diagnostic->remediation;
        checkNoLeak( texts, d->id.toUtf8().constData() );
    }
}
