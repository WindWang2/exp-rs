/***************************************************************************
 * help_cli_projections.cpp — Unified Help 6.0 CLI projection handlers
 ***************************************************************************/
#include "help_cli_projections.h"

#include "help/adapters/operator_help_source.h"
#include "help/help_composition.h"
#include "help/help_markdown_writer.h"
#include "help/help_registry.h"
#include "help/help_topic_text.h"
#include "help/operator_help_provider.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>

#include <iostream>
#include <utility>

namespace sicnu::cli
{
namespace
{

/// Composes the shared knowledge base (idempotent; embedded content plus the
/// live operator registry facts — the command catalog is GUI-only).
sicnu::help::CompositionReport composeHelp()
{
    sicnu::help::adapters::OperatorHelpSource operatorSource;
    return sicnu::help::composeHelpSystem( sicnu::help::globalHelpRegistry(),
                                           nullptr, &operatorSource );
}

/// One schema-derived line per operator parameter (type/range/default from
/// the authoritative schema) plus the curated knowledge note when present.
void printOperatorParameters( const QString &opName )
{
    using namespace sicnu::help;
    adapters::OperatorHelpSource source;
    const QVector<OperatorFact> facts = source.operators();
    for ( const OperatorFact &fact : facts ) {
        if ( fact.id != opName )
            continue;
        for ( const ParameterFact &param : OperatorHelpProvider::parameterFacts( fact ) ) {
            QString line = QStringLiteral( "  %1 [%2]" ).arg( param.name, param.type );
            if ( param.hasRange )
                line += QStringLiteral( " range %1..%2" ).arg( param.minimum ).arg( param.maximum );
            if ( param.hasDefault )
                line += QStringLiteral( " default: %1" ).arg( param.defaultText );
            if ( param.required )
                line += QStringLiteral( " (required)" );
            if ( !param.description.isEmpty() )
                line += QStringLiteral( "  -- %1" ).arg( param.description );
            std::cout << line.toStdString() << std::endl;

            const ParameterHelpEntry entry = OperatorHelpProvider::parameterHelp(
                fact, param.name, globalHelpRegistry() );
            if ( entry.knowledge.has_value() && entry.knowledge->curated ) {
                QString note;
                if ( !entry.knowledge->unit.isEmpty() )
                    note += QStringLiteral( "unit %1. " ).arg( entry.knowledge->unit );
                if ( !entry.knowledge->recommended.isEmpty() )
                    note += QStringLiteral( "recommended: %1. " ).arg( entry.knowledge->recommended );
                if ( !entry.knowledge->tradeOff.isEmpty() )
                    note += entry.knowledge->tradeOff;
                if ( !note.isEmpty() )
                    std::cout << "      >> " << note.toStdString() << std::endl;
            }
        }
        break;
    }
}

int runListTopics()
{
    const auto report = composeHelp();
    if ( !report.ok() )
        std::cerr << "help composition warnings: "
                  << report.errors.join( QStringLiteral( ";" ) ).toStdString() << std::endl;
    std::cout << "Help topics (" << report.descriptors << "):" << std::endl;
    for ( const auto *d : sicnu::help::globalHelpRegistry().all() ) {
        // headless composition has no CommandRegistry titles: fall back to id
        const QString title = d->title.isEmpty() ? d->id : d->title;
        std::cout << "  " << d->id.toStdString() << " | " << title.toStdString() << std::endl;
    }
    return 0;
}

int runHelpTopic( const QString &id )
{
    composeHelp();
    const auto *d = sicnu::help::globalHelpRegistry().find( id );
    if ( !d ) {
        std::cerr << "Unknown help topic: " << id.toStdString() << std::endl;
        return 1;
    }
    std::cout << sicnu::help::HelpTopicText::render( *d ).toStdString() << std::endl;
    return 0;
}

int runOperatorHelp( const QString &opName )
{
    composeHelp();
    const QString helpId = QStringLiteral( "operator.%1" ).arg( QString( opName ).replace( ':', '.' ) );
    const auto *d = sicnu::help::globalHelpRegistry().find( helpId );
    if ( !d ) {
        std::cerr << "Unknown operator: " << opName.toStdString() << std::endl;
        return 1;
    }
    std::cout << sicnu::help::HelpTopicText::render( *d ).toStdString() << std::endl;
    printOperatorParameters( opName );
    return 0;
}

int runExportHelpDocs( const QString &dirPath )
{
    composeHelp();
    if ( !QDir().mkpath( dirPath ) ) {
        std::cerr << "Cannot create directory: " << dirPath.toStdString() << std::endl;
        return 1;
    }
    const QDir outDir( dirPath );
    const sicnu::help::HelpRegistry &registry = sicnu::help::globalHelpRegistry();
    const std::pair<const char *, QString> pages[] = {
        { "commands.md", sicnu::help::HelpMarkdownWriter::commandReference( registry ) },
        { "operators.md", sicnu::help::HelpMarkdownWriter::operatorReference( registry ) },
        { "parameters.md", sicnu::help::HelpMarkdownWriter::parameterReference( registry ) },
        { "diagnostics.md", sicnu::help::HelpMarkdownWriter::diagnosticReference( registry ) },
        { "index.md", sicnu::help::HelpMarkdownWriter::index( registry ) },
    };
    for ( const auto &page : pages ) {
        QFile out( outDir.filePath( QString::fromLatin1( page.first ) ) );
        if ( !out.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
            std::cerr << "Cannot write: " << page.first << std::endl;
            return 1;
        }
        out.write( page.second.toUtf8() );
        std::cout << "wrote " << page.first << std::endl;
    }
    return 0;
}

} // namespace

int runHelpProjections( const QStringList &arguments )
{
    QCommandLineParser parser;
    parser.setApplicationDescription( QStringLiteral(
        "Unified Help 6.0 CLI projections (same metadata as the GUI)." ) );

    QCommandLineOption operatorHelp( QStringList{ QStringLiteral( "operator-help" ) },
                                     QStringLiteral( "Rich help for an operator (units, recommended "
                                                     "values, schema ranges/defaults)." ),
                                     QStringLiteral( "operator" ) );
    QCommandLineOption helpTopic( QStringList{ QStringLiteral( "help-topic" ) },
                                  QStringLiteral( "Print the help topic with the given Help ID." ),
                                  QStringLiteral( "id" ) );
    QCommandLineOption listTopics( QStringList{ QStringLiteral( "list-topics" ) },
                                   QStringLiteral( "List all help topic ids and exit." ) );
    QCommandLineOption exportDocs( QStringList{ QStringLiteral( "export-help-docs" ) },
                                   QStringLiteral( "Generate the Markdown reference into <dir>." ),
                                   QStringLiteral( "dir" ) );
    parser.addHelpOption();
    parser.addOption( operatorHelp );
    parser.addOption( helpTopic );
    parser.addOption( listTopics );
    parser.addOption( exportDocs );
    // parse(), NOT process(): CLI 3.0 subcommands legitimately carry their
    // own global flags (--json, --quiet, …) that are unknown to this parser.
    // process() would exit the whole program with "unknown option" before
    // the subcommand dispatch ever ran; a failed parse just means this is
    // not a help projection and the caller must continue.
    if ( !parser.parse( arguments ) )
        return -1;
    if ( parser.isSet( QStringLiteral( "help" ) ) )
    {
        parser.showHelp( 0 );
        return 0;
    }

    if ( parser.isSet( listTopics ) )
        return runListTopics();
    if ( parser.isSet( helpTopic ) )
        return runHelpTopic( parser.value( helpTopic ) );
    if ( parser.isSet( operatorHelp ) )
        return runOperatorHelp( parser.value( operatorHelp ) );
    if ( parser.isSet( exportDocs ) )
        return runExportHelpDocs( parser.value( exportDocs ) );
    return -1; // no help option set — caller continues
}

} // namespace sicnu::cli
