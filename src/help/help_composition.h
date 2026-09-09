/***************************************************************************
 * help_composition.h — startup composition of the help knowledge base
 *
 * One call composes a HelpRegistry from:
 *   1. embedded JSON knowledge (data/help/** via :/help resources) —
 *      commands additive knowledge, operator/parameter knowledge, workbench
 *      guidance, concepts, diagnostics;
 *   2. derived descriptors from the authoritative catalogs (commands,
 *      operators + their schemas) via the catalog source interfaces.
 *
 * After composition the caller should assert registry.validateReferences()
 * is empty (drift tests enforce this for the shipped content).
 ***************************************************************************/
#pragma once

#include "help/help_registry.h"

#include <QString>
#include <QStringList>

namespace sicnu::help
{

class CommandCatalogSource;
class OperatorCatalogSource;

struct CompositionReport
{
    int descriptors = 0;
    int aliases = 0;
    QStringList errors;     ///< content/provider errors (composition continues)
    QStringList dangling;   ///< unresolved references after composition
    bool ok() const { return errors.isEmpty() && dangling.isEmpty(); }
};

/// Composes @p target from embedded content plus optional catalog sources.
/// Sources may be null (e.g. CLI runs without the GUI registry); only the
/// embedded knowledge is composed in that case.
CompositionReport composeHelpSystem( HelpRegistry &target,
                                     const CommandCatalogSource *commandSource = nullptr,
                                     const OperatorCatalogSource *operatorSource = nullptr,
                                     const QString &extraContentDir = QString() );

} // namespace sicnu::help
