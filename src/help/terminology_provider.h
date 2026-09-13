/***************************************************************************
 * terminology_provider.h — unified RS glossary loader (D6)
 *
 * Single load path for data/terms/rs_glossary.json: the bilingual terminology
 * layer shared by the zh_CN translation QA, the help system and the future
 * agent (D9). The provider never forks help content — it derives regular
 * concept.term.* descriptors from the same glossary entries and appends them
 * to the one HelpRegistry, so Help Center search and tooltips speak the same
 * terms as the UI translation.
 *
 * Layer guard: Qt Core + jsoncpp only (no Widgets/Network/app deps).
 ***************************************************************************/
#pragma once

#include "help/help_descriptor.h"
#include "help/help_registry.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::help
{

/// One glossary entry (schema: data/terms/rs_glossary.schema.json).
struct TermEntry
{
    QString en;            ///< canonical English term (unique key)
    QString zh;            ///< standard Simplified Chinese term
    QString definitionZh;  ///< one-sentence classroom-usable definition
    QString category;      ///< one of the 12 fixed glossary categories
    QStringList aliases;   ///< alternative spellings / abbreviations
    QStringList related;   ///< other entries' en values
};

/// Canonical descriptor id for an en term: concept.term.<slug>.
QString termDescriptorId( const QString &en );

class TerminologyProvider
{
  public:
    struct LoadResult
    {
        QVector<TermEntry> terms;
        QStringList errors;  ///< non-empty ⇒ malformed input (skipped entries noted)
    };

    /// Parses one glossary JSON document (array of term objects).
    static LoadResult loadFromJson( const QByteArray &text, const QString &context );
    static LoadResult loadFromFile( const QString &path );
    /// Reads ":/help/terms/rs_glossary.json" (embedded via help_content.qrc).
    static LoadResult loadFromResources();

    /// Derives concept.term.* descriptors and appends them to @p out.
    /// related[] references map onto sibling term ids; unresolvable ones are
    /// reported through @p errors and dropped from the descriptor.
    static void appendDescriptors( const QVector<TermEntry> &terms,
                                   HelpRegistry &out, QStringList *errors = nullptr );

    TerminologyProvider() = default;
    explicit TerminologyProvider( QVector<TermEntry> terms );

    /// Case-insensitive lookup by en, zh or any alias; nullptr when unknown.
    const TermEntry *lookup( const QString &text ) const;
    /// Entries sorted by en (stable for tests/tools).
    const QVector<TermEntry> &terms() const { return m_terms; }

  private:
    void reindex();
    QVector<TermEntry> m_terms;
    QHash<QString, int> m_index;  ///< lowercased en/zh/alias → row
};

} // namespace sicnu::help
