/***************************************************************************
 * terminology_provider.cpp — unified RS glossary loader (D6)
 ***************************************************************************/
#include "help/terminology_provider.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

#include <json/json.h>

namespace sicnu::help
{

namespace
{

const char *kGlossaryResource = ":/help/terms/rs_glossary.json";

const char *const kCategories[] = {
    "radiometric", "atmospheric", "geometric", "spectral",
    "SAR",         "terrain",     "temporal",  "classification",
    "change",      "cartography", "OBIA",      "hyperspectral",
};

bool categoryKnown( const QString &category )
{
    for ( const char *c : kCategories )
        if ( category == QLatin1String( c ) )
            return true;
    return false;
}

QString slugify( const QString &en )
{
    QString lowered = en.toLower();
    QString out;
    out.reserve( lowered.size() );
    for ( const QChar &ch : lowered )
        out.append( ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) ? ch : QLatin1Char( '_' ) );
    static const QRegularExpression repeats( QStringLiteral( "_{2,}" ) );
    out = repeats.match( out ).hasMatch() ? QString( out ).replace( repeats, QStringLiteral( "_" ) ) : out;
    while ( out.startsWith( QLatin1Char( '_' ) ) )
        out.remove( 0, 1 );
    while ( out.endsWith( QLatin1Char( '_' ) ) )
        out.chop( 1 );
    return out;
}

} // namespace

QString termDescriptorId( const QString &en )
{
    return QStringLiteral( "concept.term.%1" ).arg( slugify( en ) );
}

TerminologyProvider::LoadResult TerminologyProvider::loadFromJson( const QByteArray &text,
                                                                   const QString &context )
{
    LoadResult result;
    Json::Value root;
    Json::CharReaderBuilder builder;
    builder[ "collectComments" ] = false;
    std::string jsonErrors;
    {
        const std::string raw = std::string( text.constData(), static_cast<size_t>( text.size() ) );
        std::istringstream stream( raw );
        if ( !Json::parseFromStream( builder, stream, &root, &jsonErrors ) )
        {
            result.errors << QStringLiteral( "%1: JSON parse error: %2" )
                                 .arg( context, QString::fromStdString( jsonErrors ) );
            return result;
        }
    }
    if ( !root.isArray() )
    {
        result.errors << QStringLiteral( "%1: glossary root must be an array" ).arg( context );
        return result;
    }

    for ( unsigned int i = 0; i < root.size(); ++i )
    {
        const Json::Value &entry = root[ i ];
        const QString where = QStringLiteral( "%1[%2]" ).arg( context ).arg( i );
        if ( !entry.isObject() )
        {
            result.errors << QStringLiteral( "%1: entry is not an object" ).arg( where );
            continue;
        }
        TermEntry term;
        term.en = QString::fromUtf8( entry.get( "en", "" ).asCString() );
        term.zh = QString::fromUtf8( entry.get( "zh", "" ).asCString() );
        term.definitionZh = QString::fromUtf8( entry.get( "definition_zh", "" ).asCString() );
        term.category = QString::fromUtf8( entry.get( "category", "" ).asCString() );
        for ( const Json::Value &alias : entry.get( "alias", Json::nullValue ) )
        {
            if ( alias.isString() && strlen( alias.asCString() ) > 0 )
                term.aliases << QString::fromUtf8( alias.asCString() );
        }
        for ( const Json::Value &rel : entry.get( "related", Json::nullValue ) )
        {
            if ( rel.isString() && strlen( rel.asCString() ) > 0 )
                term.related << QString::fromUtf8( rel.asCString() );
        }

        if ( term.en.isEmpty() || term.zh.isEmpty() || term.definitionZh.isEmpty() )
        {
            result.errors << QStringLiteral( "%1: en/zh/definition_zh must be non-empty" ).arg( where );
            continue;
        }
        if ( !categoryKnown( term.category ) )
        {
            result.errors << QStringLiteral( "%1: unknown category '%2'" ).arg( where, term.category );
            continue;
        }
        result.terms.append( term );
    }

    // Duplicate en keys are glossary bugs (schema demands uniqueness).
    QHash<QString, int> seen;
    for ( const TermEntry &term : result.terms )
    {
        const QString key = term.en.toLower();
        if ( seen.contains( key ) )
            result.errors << QStringLiteral( "%1: duplicate en term '%2'" ).arg( context, term.en );
        else
            seen.insert( key, 1 );
    }
    return result;
}

TerminologyProvider::LoadResult TerminologyProvider::loadFromFile( const QString &path )
{
    QFile file( path );
    LoadResult result;
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        result.errors << QStringLiteral( "cannot open glossary file: %1" ).arg( path );
        return result;
    }
    return loadFromJson( file.readAll(), path );
}

TerminologyProvider::LoadResult TerminologyProvider::loadFromResources()
{
    QFile file{ QString::fromUtf8( kGlossaryResource ) };
    LoadResult result;
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        // Tools/tests without the embedded resource simply get zero terms.
        return result;
    }
    return loadFromJson( file.readAll(), QLatin1String( kGlossaryResource ) );
}

void TerminologyProvider::appendDescriptors( const QVector<TermEntry> &terms,
                                             HelpRegistry &out, QStringList *errors )
{
    // id → en map for related[] resolution; slug dedup keeps ids unique.
    QHash<QString, QString> idByEnLower;
    {
        QSet<QString> used;
        for ( const TermEntry &term : terms )
        {
            QString id = termDescriptorId( term.en );
            QString base = id;
            int n = 2;
            while ( used.contains( id ) )
                id = QStringLiteral( "%1_%2" ).arg( base ).arg( n++ );
            used.insert( id );
            idByEnLower.insert( term.en.toLower(), id );
        }
    }

    for ( const TermEntry &term : terms )
    {
        HelpDescriptor d;
        d.id = termDescriptorId( term.en );
        d.kind = HelpKind::Concept;
        d.title = QStringLiteral( "%1（%2）" ).arg( term.zh, term.en );
        d.summary = term.definitionZh;
        d.category = QStringLiteral( "术语表" );
        d.keywords << term.en << term.zh << term.aliases;
        for ( const QString &rel : term.related )
        {
            const QString relatedId = idByEnLower.value( rel.toLower() );
            if ( relatedId.isEmpty() )
            {
                if ( errors )
                    *errors << QStringLiteral( "term '%1': related '%2' does not resolve" )
                                       .arg( term.en, rel );
                continue;
            }
            if ( relatedId != d.id )
                d.relatedIds << relatedId;
        }

        QString error;
        if ( !out.upsertDescriptor( d, &error ) && errors )
            *errors << QStringLiteral( "term '%1': %2" ).arg( term.en, error );
    }
}

TerminologyProvider::TerminologyProvider( QVector<TermEntry> terms )
    : m_terms( terms )
{
    std::sort( m_terms.begin(), m_terms.end(),
               []( const TermEntry &a, const TermEntry &b ) { return a.en < b.en; } );
    reindex();
}

void TerminologyProvider::reindex()
{
    m_index.clear();
    for ( int i = 0; i < m_terms.size(); ++i )
    {
        const TermEntry &term = m_terms[ i ];
        m_index.insert( term.en.toLower(), i );
        m_index.insert( term.zh.toLower(), i );
        for ( const QString &alias : term.aliases )
            m_index.insert( alias.toLower(), i );
    }
}

const TermEntry *TerminologyProvider::lookup( const QString &text ) const
{
    const int row = m_index.value( text.trimmed().toLower(), -1 );
    return row >= 0 ? &m_terms[ row ] : nullptr;
}

} // namespace sicnu::help
