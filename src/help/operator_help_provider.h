/***************************************************************************
 * operator_help_provider.h — derives operator/parameter help from schemas
 *
 * The authoritative parameter facts (type, required, range, enum, default)
 * are ALWAYS read from the operator's schema JSON at query time; this
 * provider only composes the presentation record:
 *
 *   base tier     — auto-derived for every schema parameter (exists checkably
 *                   for 100% of schema-visible parameters, zero authoring);
 *   curated tier  — additive ParameterKnowledge from data/help JSON
 *                   (unit, scientific meaning, recommended value, trade-off),
 *                   registered as parameter.<operator>.<param> descriptors.
 *
 * Operator descriptors merge schema identity (name/displayName/group/
 * description/determinism/memory policy) with the knowledge layer's algorithm
 * page and summary. No scientific fact is retyped from schema into content.
 ***************************************************************************/
#pragma once

#include "help/help_descriptor.h"
#include "help/help_catalog_source.h"
#include "help/help_registry.h"

#include <QString>
#include <QStringList>
#include <json/json.h>

#include <optional>
#include <QVector>

namespace sicnu::help
{

class OperatorCatalogSource;

/// One schema-visible parameter fact (base tier, derived).
struct ParameterFact
{
    QString name;         ///< schema param name (camelCase as authored)
    QString helpId;       ///< derived parameter.<operator>.<param> id
    QString type;         ///< "string"|"number"|"integer"|"boolean"|"enum"|"raster"|"vector"|"output"|"array"|...
    QString description;  ///< schema description (English, authoritative)
    bool required = false;
    bool hasDefault = false;
    QString defaultText;  ///< human-readable default
    double minimum = 0.0;
    double maximum = 0.0;
    bool hasRange = false;
    QStringList enumValues;
    QString unit;         ///< curated unit (knowledge tier)
    /// Curated knowledge pointer is resolved by the provider at composition.
};

/// Fully merged presentation record for one parameter.
struct ParameterHelpEntry
{
    QString operatorId;
    QString operatorHelpId;
    ParameterFact fact;
    std::optional<ParameterKnowledge> knowledge;
};

class OperatorHelpProvider
{
  public:
    /// Composes operator + parameter descriptors into @p out.
    /// @p knowledge holds the additive entries (operators/*.json). Operators
    /// listed in knowledge but absent from @p source are reported as errors
    /// (drift signal), and vice versa at the coverage tier checked by tests.
    static void compose( const OperatorCatalogSource &source, const HelpRegistry &knowledge,
                         HelpRegistry &out, QStringList *errors = nullptr );

    /// Derives the base-tier fact list for @p fact.schema (pure function).
    static QVector<ParameterFact> parameterFacts( const OperatorFact &fact );

    /// Merged help entry for one parameter (base + curated tiers).
    static ParameterHelpEntry parameterHelp( const OperatorFact &fact, const QString &paramName,
                                             const HelpRegistry &knowledge );
};

} // namespace sicnu::help
