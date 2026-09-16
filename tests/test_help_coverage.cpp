/***************************************************************************
  tests/test_help_coverage.cpp
  Unified Help System 6.0 — coverage & drift contracts against the LIVE
  registries (not stubs):

    - every registered rs:* operator has an operator help descriptor;
    - every schema-visible parameter has a parameter descriptor, and every
      curated parameter knowledge entry matches a real schema parameter
      (compose errors must be empty — schema/help consistency);
    - command ids present in the shell command table (source-scan of
      command_defs.cpp — the CommandRegistry cannot be instantiated without
      the GUI shell) all have command knowledge entries;
    - every HarnessError stable code and every RSOperatorError code resolves
      to a diagnostic descriptor whose retry sense agrees with the origin
      taxonomy (drift check);
    - no help content leaks author-machine paths or credential-shaped
      strings (secret scan);
    - generated Markdown reference is stable and complete.
 ***************************************************************************/

#include "app/help/availability_facts_adapter.h"
#include "contracts/command_ref_scanner.h"
#include "help/adapters/operator_help_source.h"
#include "help/command_help_provider.h"
#include "help/diagnostic_catalog.h"
#include "help/help_composition.h"
#include "help/help_id.h"
#include "help/help_markdown_writer.h"
#include "help/help_registry.h"
#include "help/operator_help_provider.h"

#include "agent/harness/harness_error.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QTextStream>

#include <catch2/catch_test_macros.hpp>

using namespace sicnu::help;

namespace
{

/// Composes the full knowledge base against the real operator registry once.
const CompositionReport &composed()
{
    static CompositionReport report = [] {
        adapters::OperatorHelpSource source;
        return composeHelpSystem( globalHelpRegistry(), nullptr, &source );
    }();
    return report;
}

/// Command ids as registered by the shell — extracted with the SHARED
/// contracts scanner (the same oracle as test_command_contract_9). This test
/// used to keep its own prefix whitelist, which had already gone stale
/// (workflow.*/cartography.* families were invisible to it), so the two
/// command censuses could disagree. One scanner, one truth.
QStringList scanShellCommandIds()
{
    const auto readFile = []( const QString &relative ) {
        QFile f( QStringLiteral( CMAKE_SOURCE_DIR "/" ) + relative );
        return f.open( QIODevice::ReadOnly )
                   ? QString::fromUtf8( f.readAll() ).toStdString()
                   : std::string();
    };

    sicnu::contracts::CommandRefScanner scanner;
    sicnu::contracts::CommandRefReport report;
    scanner.scanRegistered( readFile( QStringLiteral( "src/app/workbench/command_defs.cpp" ) ),
                            QStringLiteral( "command_defs.cpp" ).toStdString(), report );
    scanner.scanRegistered( readFile( QStringLiteral( "src/app/main_window_workbench.cpp" ) ),
                            QStringLiteral( "main_window_workbench.cpp" ).toStdString(), report );

    QStringList ids;
    for ( const std::string &id : report.registeredIds )
        ids << QString::fromStdString( id );
    return ids;
}

bool containsMachinePath( const QString &text )
{
    return text.contains( QStringLiteral( "C:\\Users" ), Qt::CaseInsensitive )
           || text.contains( QStringLiteral( "wangj" ), Qt::CaseInsensitive )
           || text.contains( QStringLiteral( "/home/" ) )
           || text.contains( QStringLiteral( "password" ), Qt::CaseInsensitive )
           || text.contains( QStringLiteral( "api_key" ), Qt::CaseInsensitive )
           || text.contains( QStringLiteral( "token=" ), Qt::CaseInsensitive );
}

} // namespace

TEST_CASE( "Help composition against live registries is clean", "[help][coverage]" )
{
    const CompositionReport &report = composed();
    INFO( "errors: " << report.errors.join( QStringLiteral( " | " ) ).toStdString() );
    INFO( "dangling: " << report.dangling.join( QStringLiteral( " | " ) ).toStdString() );
    CHECK( report.ok() );
    CHECK( report.descriptors > 200 ); // commands + 105 operators + their parameters + diagnostics + concepts
}

TEST_CASE( "Every registered rs:* operator is covered", "[help][coverage][operators]" )
{
    composed();
    sicnu::operators::rs::initBuiltinRsOperators();
    const std::vector<std::string> names =
        sicnu::operators::RSOperatorRegistry::instance().operatorNames();

    int covered = 0;
    for ( const std::string &name : names ) {
        if ( name.rfind( "rs:", 0 ) != 0 )
            continue;
        const QString helpId = QStringLiteral( "operator.%1" ).arg( HelpId::domainForOperatorId(
            QString::fromStdString( name ) ) );
        INFO( "operator: " << name );
        CHECK( globalHelpRegistry().find( helpId ) != nullptr );
        if ( globalHelpRegistry().find( helpId ) )
            ++covered;
    }
    CHECK( covered >= 105 );
}

TEST_CASE( "Every schema parameter has a descriptor; knowledge matches schemas", "[help][coverage][parameters]" )
{
    composed();
    adapters::OperatorHelpSource source;
    const QVector<OperatorFact> facts = source.operators();

    int parameterCount = 0;
    int curatedCount = 0;
    for ( const OperatorFact &fact : facts ) {
        for ( const ParameterFact &param : OperatorHelpProvider::parameterFacts( fact ) ) {
            ++parameterCount;
            INFO( "parameter: " << param.helpId.toStdString() );
            CHECK( globalHelpRegistry().find( param.helpId ) != nullptr );
            const HelpDescriptor *d = globalHelpRegistry().find( param.helpId );
            if ( d && d->parameter.has_value() && d->parameter->curated )
                ++curatedCount;
        }
    }
    CHECK( parameterCount > 500 );
    CHECK( curatedCount >= 50 ); // flagship deep-parameter knowledge tier (measured)

    // deep tier sanity: the SAR speckle window must carry unit + recommendation
    const HelpDescriptor *kernel = globalHelpRegistry().find(
        QStringLiteral( "parameter.rs.sar_speckle.kernelSize" ) );
    REQUIRE( kernel );
    REQUIRE( kernel->parameter.has_value() );
    CHECK( kernel->parameter->unit == QStringLiteral( "像素" ) );
    CHECK_FALSE( kernel->parameter->recommended.isEmpty() );
    CHECK_FALSE( kernel->parameter->tradeOff.isEmpty() );
}

TEST_CASE( "Shell command ids all have help knowledge", "[help][coverage][commands]" )
{
    composed();
    const QStringList ids = scanShellCommandIds();
    REQUIRE( ids.size() >= 45 ); // sanity: the scanner still sees the command table

    for ( const QString &id : ids ) {
        INFO( "command id: " << id.toStdString() );
        CHECK( globalHelpRegistry().find( QStringLiteral( "command.%1" ).arg( id ) ) != nullptr );
    }

    // reverse drift: every knowledge entry for a command must correspond to
    // a real command in the shell source (catches stale entries after
    // commands are removed/renamed)
    QFile commandsJson( QStringLiteral( CMAKE_SOURCE_DIR "/data/help/commands.json" ) );
    REQUIRE( commandsJson.open( QIODevice::ReadOnly ) );
    const QJsonDocument doc = QJsonDocument::fromJson( commandsJson.readAll() );
    REQUIRE( doc.isArray() );
    for ( const QJsonValue &entry : doc.array() ) {
        const QString helpId = entry.toObject().value( QStringLiteral( "id" ) ).toString();
        if ( !helpId.startsWith( QLatin1String( "command." ) ) )
            continue;
        const QString commandId = helpId.mid( QString( "command." ).size() );
        INFO( "knowledge id: " << helpId.toStdString() );
        CHECK( ids.contains( commandId ) );
    }
}

TEST_CASE( "Availability fact tables only reference real commands", "[help][coverage][availability]" )
{
    composed();
    const QStringList known = scanShellCommandIds();
    for ( const QString &commandId : sicnu::app::AvailabilityFactsAdapter::coveredCommandIds() ) {
        INFO( "availability table id: " << commandId.toStdString() );
        CHECK( known.contains( commandId ) );
    }
}

TEST_CASE( "Harness and operator error codes all resolve to diagnostics", "[help][coverage][diagnostics]" )
{
    composed();
    DiagnosticCatalog catalog( globalHelpRegistry() );

    // HarnessError stable taxonomy: the census source is the RUNTIME table
    // (allErrorCodes), not a hand-copied literal list — the old list had
    // already drifted (missing INTENT_AMBIGUOUS, IDENTITY_MISMATCH and the
    // seven compiler codes). Codes without a curated page must be explicitly
    // allow-listed here, mirroring test_diagnostics_contract_9.
    const std::vector<std::string> harnessCodes = sicnu::agent::harness::allErrorCodes();
    REQUIRE( harnessCodes.size() >= 30 );

    // Contract refusal, explained in-band by the lab copilot; no curated page
    // by design (same entry as kAllowedHarnessCodesWithoutPage there).
    const QStringList kAllowedWithoutPage = { QStringLiteral( "TEACHING_REFUSAL" ) };

    for ( const std::string &code : harnessCodes ) {
        const QString qcode = QString::fromStdString( code );
        INFO( "harness code: " << code );
        if ( kAllowedWithoutPage.contains( qcode ) )
            continue;
        const HelpDescriptor *d = catalog.find( DiagnosticFamily::Harness, qcode );
        REQUIRE( d != nullptr );
        REQUIRE( d->diagnostic.has_value() );
        CHECK_FALSE( d->diagnostic->remediation.isEmpty() );
        CHECK( d->diagnostic->originCode == qcode );

        // retry drift: curated sense must match the origin retry class
        using sicnu::agent::harness::RetryClass;
        using sicnu::agent::harness::retryClassForCode;
        const RetryClass origin = retryClassForCode( code );
        if ( d->diagnostic->retrySense != RetrySense::Derived ) {
            switch ( origin ) {
            case RetryClass::None:
                CHECK( d->diagnostic->retrySense == RetrySense::None );
                break;
            case RetryClass::Manual:
                CHECK( d->diagnostic->retrySense == RetrySense::Manual );
                break;
            case RetryClass::Transient:
                CHECK( d->diagnostic->retrySense == RetrySense::Transient );
                break;
            }
        }
    }

    // RSOperatorError enum — every non-success code resolves (curated or
    // fallback that preserves the code)
    const sicnu::operators::ErrorCode operatorCodes[] = {
        sicnu::operators::ErrorCode::InvalidParameter,   sicnu::operators::ErrorCode::MissingRequiredParameter,
        sicnu::operators::ErrorCode::TypeMismatch,       sicnu::operators::ErrorCode::OutOfRange,
        sicnu::operators::ErrorCode::InvalidEnumValue,   sicnu::operators::ErrorCode::FileNotFound,
        sicnu::operators::ErrorCode::FileNotReadable,    sicnu::operators::ErrorCode::FileNotWritable,
        sicnu::operators::ErrorCode::DirectoryNotFound,  sicnu::operators::ErrorCode::InvalidInputData,
        sicnu::operators::ErrorCode::GdalError,          sicnu::operators::ErrorCode::OpenCvError,
        sicnu::operators::ErrorCode::OtbError,           sicnu::operators::ErrorCode::QgisProcessingError,
        sicnu::operators::ErrorCode::ComputationError,   sicnu::operators::ErrorCode::Cancelled,
        sicnu::operators::ErrorCode::AlreadyRunning,     sicnu::operators::ErrorCode::NotInitialized,
        sicnu::operators::ErrorCode::ExternalProcessTimeout, sicnu::operators::ErrorCode::ExternalProcessFailed,
        sicnu::operators::ErrorCode::DeviceUnavailable,     sicnu::operators::ErrorCode::RuntimeProviderFailed,
        sicnu::operators::ErrorCode::Unknown,
    };
    for ( const sicnu::operators::ErrorCode code : operatorCodes ) {
        const QString origin = QString::fromLatin1( sicnu::operators::errorCodeToString( code ) );
        const HelpDescriptor resolved = catalog.resolve( DiagnosticFamily::Operator, origin );
        INFO( "operator code: " << origin.toStdString() );
        REQUIRE( resolved.diagnostic.has_value() );
        CHECK( resolved.diagnostic->originCode == origin );
        CHECK_FALSE( resolved.diagnostic->remediation.isEmpty() );
    }
}

TEST_CASE( "Help content carries no machine paths or credential-shaped text", "[help][secrets]" )
{
    composed();
    for ( const HelpDescriptor *d : globalHelpRegistry().all() ) {
        QStringList texts{ d->id, d->title, d->summary, d->category };
        texts << d->keywords << d->relatedIds << d->docRefs;
        if ( d->diagnostic.has_value() ) {
            texts << d->diagnostic->whatHappened << d->diagnostic->whyItMatters;
            texts << d->diagnostic->remediation << d->diagnostic->technicalNote;
        }
        if ( d->algorithm.has_value() ) {
            texts << d->algorithm->inputs << d->algorithm->outputs;
            texts << d->algorithm->assumptions << d->algorithm->limitations;
            texts << d->algorithm->failureModes;
        }
        for ( const QString &text : texts ) {
            INFO( "descriptor: " << d->id.toStdString() );
            CHECK_FALSE( containsMachinePath( text ) );
        }
    }
}

TEST_CASE( "Generated Markdown reference is complete", "[help][markdown-generation]" )
{
    composed();
    const QString operators = HelpMarkdownWriter::operatorReference( globalHelpRegistry() );
    const QString parameters = HelpMarkdownWriter::parameterReference( globalHelpRegistry() );
    const QString diagnostics = HelpMarkdownWriter::diagnosticReference( globalHelpRegistry() );
    const QString commands = HelpMarkdownWriter::commandReference( globalHelpRegistry() );

    CHECK( operators.contains( QStringLiteral( "operator.rs.sar_speckle" ) ) );
    CHECK( parameters.contains( QStringLiteral( "parameter.rs.sar_speckle.kernelSize" ) ) );
    CHECK( diagnostics.contains( QStringLiteral( "DATASET_NOT_FOUND" ) ) );
    CHECK( commands.contains( QStringLiteral( "command.layer.toggleEditing" ) ) );

    // deterministic output (committed pages under docs/generated/help can be
    // diffed byte-for-byte)
    CHECK( operators == HelpMarkdownWriter::operatorReference( globalHelpRegistry() ) );
}

TEST_CASE( "Committed reference pages match regeneration byte-for-byte (zero-diff)",
           "[help][markdown-generation][drift]" )
{
    composed();
    struct Page
    {
        const char *file;
        QString ( *generate )( const HelpRegistry & );
    };
    const Page pages[] = {
        { "commands.md", HelpMarkdownWriter::commandReference },
        { "operators.md", HelpMarkdownWriter::operatorReference },
        { "parameters.md", HelpMarkdownWriter::parameterReference },
        { "diagnostics.md", HelpMarkdownWriter::diagnosticReference },
        { "index.md", HelpMarkdownWriter::index },
    };

    const QString dir = QStringLiteral( CMAKE_SOURCE_DIR "/docs/generated/help" );
    const bool regen = qEnvironmentVariableIsEmpty( "SICNU_REGEN_HELP_DOCS" ) ? false : true;

    // Both sides are normalized to a single trailing newline so the gate
    // stays byte-exact about CONTENT while tolerating the writer's blank
    // EOF lines (git diff --check flags those in committed pages).
    const auto normalized = []( QString text ) {
        while ( text.endsWith( u'\n' ) )
            text.chop( 1 );
        return text + u'\n';
    };

    for ( const Page &page : pages ) {
        const QString generated = normalized( page.generate( globalHelpRegistry() ) );
        const QString path = dir + QLatin1Char( '/' ) + QLatin1String( page.file );

        if ( regen ) {
            // opt-in maintenance mode: SICNU_REGEN_HELP_DOCS=1 rewrites the
            // committed pages (plain build/test runs NEVER write the repo)
            QFile out( path );
            REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
            out.write( generated.toUtf8() );
            continue;
        }

        QFile in( path );
        INFO( "cannot open committed page: " << path.toStdString() );
        REQUIRE( in.open( QIODevice::ReadOnly ) );
        const QString committed = normalized( QString::fromUtf8( in.readAll() ) );
        if ( committed != generated ) {
            // first divergent line, so the drift is actionable without reruns
            const QStringList a = committed.split( QLatin1Char( '\n' ) );
            const QStringList b = generated.split( QLatin1Char( '\n' ) );
            int line = 0;
            while ( line < a.size() && line < b.size() && a.at( line ) == b.at( line ) )
                ++line;
            INFO( "page drifts at line " << ( line + 1 ) << " of " << page.file );
            INFO( "committed: " << ( line < a.size() ? a.at( line ) : QString() ).left( 120 ).toStdString() );
            INFO( "generated: " << ( line < b.size() ? b.at( line ) : QString() ).left( 120 ).toStdString() );
        }
        CHECK( committed == generated );
    }
}
