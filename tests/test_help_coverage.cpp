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

/// Command ids as registered by the shell (source scan; see file header).
QStringList scanShellCommandIds()
{
    QFile file( QStringLiteral( CMAKE_SOURCE_DIR "/src/app/workbench/command_defs.cpp" ) );
    if ( !file.open( QIODevice::ReadOnly ) )
        return {};
    const QString text = QString::fromUtf8( file.readAll() );

    QStringList ids;
    QRegularExpression re( QStringLiteral( "\"((?:project|layer|map|workbench|rs|app)\\.[A-Za-z0-9_.]+)\"" ) );
    auto it = re.globalMatch( text );
    while ( it.hasNext() ) {
        const auto match = it.next();
        const QString id = match.captured( 1 );
        if ( !ids.contains( id ) )
            ids << id;
    }
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

    // HarnessError stable taxonomy: every code needs a curated descriptor
    // whose retry sense agrees with (or defers to) the origin taxonomy.
    // The wire strings must stay identical to
    // sicnu::agent::harness::error_codes (drift-checked in test_help_diagnostics
    // via isKnownErrorCode and here by exact spelling).
    const std::pair<const char *, std::string> harnessCodes[] = {
        { "DATASET_NOT_FOUND", "DATASET_NOT_FOUND" },
        { "BAND_ROLE_UNRESOLVED", "BAND_ROLE_UNRESOLVED" },
        { "CRS_MISMATCH", "CRS_MISMATCH" },
        { "GRID_MISMATCH", "GRID_MISMATCH" },
        { "INVALID_RADIOMETRY", "INVALID_RADIOMETRY" },
        { "INSUFFICIENT_MEMORY", "INSUFFICIENT_MEMORY" },
        { "MODEL_INCOMPATIBLE", "MODEL_INCOMPATIBLE" },
        { "MODEL_NOT_READY", "MODEL_NOT_READY" },
        { "EXECUTION_FAILED", "EXECUTION_FAILED" },
        { "CANCELLED", "CANCELLED" },
        { "OUTPUT_INVALID", "OUTPUT_INVALID" },
        { "MAP_PREFLIGHT_FAILED", "MAP_PREFLIGHT_FAILED" },
        { "PREFLIGHT_BLOCKED", "PREFLIGHT_BLOCKED" },
        { "ENTITY_AMBIGUOUS", "ENTITY_AMBIGUOUS" },
        { "INVALID_PLAN", "INVALID_PLAN" },
        { "INVALID_PARAMETER", "INVALID_PARAMETER" },
        { "TRANSIENT_FAILURE", "TRANSIENT_FAILURE" },
        { "PATH_OUTSIDE_WORKSPACE", "PATH_OUTSIDE_WORKSPACE" },
        { "WORKFLOW_NOT_FOUND", "WORKFLOW_NOT_FOUND" },
        { "TOOL_NOT_FOUND", "TOOL_NOT_FOUND" },
        { "TIME_ORDER_INVALID", "TIME_ORDER_INVALID" },
        { "MODALITY_MISMATCH", "MODALITY_MISMATCH" },
        { "POLARIZATION_MISMATCH", "POLARIZATION_MISMATCH" },
        { "CALIBRATION_MISMATCH", "CALIBRATION_MISMATCH" },
        { "TRAINING_INVALID", "TRAINING_INVALID" },
        { "NOT_SUPPORTED", "NOT_SUPPORTED" }
    };
    for ( const auto &code : harnessCodes ) {
        const HelpDescriptor *d = catalog.find( DiagnosticFamily::Harness,
                                                QString::fromLatin1( code.second.c_str() ) );
        INFO( "harness code: " << code.second );
        REQUIRE( d != nullptr );
        REQUIRE( d->diagnostic.has_value() );
        CHECK_FALSE( d->diagnostic->remediation.isEmpty() );
        CHECK( d->diagnostic->originCode == QString::fromLatin1( code.second.c_str() ) );

        // retry drift: curated sense must match the origin retry class
        using sicnu::agent::harness::RetryClass;
        using sicnu::agent::harness::retryClassForCode;
        const RetryClass origin = retryClassForCode( code.second );
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
