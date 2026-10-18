/***************************************************************************
 * help_search_index.h — deterministic, bounded search over the help registry
 *
 * Inverted token index built once from the registry (title + summary +
 * keywords + id segments). Latin text tokenizes on non-alphanumeric
 * boundaries; CJK runs contribute character bigrams. Ranking is a fixed
 * field-weight sum, ties broken by id — identical queries always return
 * identical results (contract-tested).
 *
 * Bounds (Milestone M): build O(topics × tokens); query returns at most
 * maxResults (default 20) in O(matches); no Markdown parsing, no allocation
 * per hover beyond the result vector.
 ***************************************************************************/
#pragma once

#include "help/help_descriptor.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <QHash>

namespace sicnu::help
{

struct SearchHit
{
    QString id;      ///< descriptor id
    QString title;   ///< display title
    QString summary; ///< display summary
    double score = 0.0;
};

class HelpSearchIndex
{
  public:
    /// Builds the index from @p descriptors (registry.all() or a subset).
    void build( const QVector<const HelpDescriptor *> &descriptors );

    /// True after a successful build().
    bool isValid() const { return m_built; }
    int topicCount() const { return m_topics; }

    /// Ranked hits for @p query. Empty/whitespace query → empty result.
    /// @p maxResults is clamped to [1, 100].
    QVector<SearchHit> search( const QString &query, int maxResults = 20 ) const;

    /// Tokenizes text exactly like the index does (exposed for tests).
    static QStringList tokenize( const QString &text );

  private:
    // token → (dense document index → weight in that document). Dense int
    // keys keep the per-query score join off QString hashing entirely.
    QHash<QString, QHash<int, double>> m_index;
    QVector<const HelpDescriptor *> m_documents; // dense index → descriptor
    int m_topics = 0;
    bool m_built = false;
};

} // namespace sicnu::help
