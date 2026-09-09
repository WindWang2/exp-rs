/***************************************************************************
 * help_content_store.h — data/help JSON knowledge loader/validator
 *
 * The additive human/scientific knowledge layer lives as JSON under
 * data/help/** (embedded via Qt resource for GUI, readable from disk for
 * tools/tests). Every entry becomes a HelpDescriptor; unknown ids are
 * allowed at load time only when the composition step back-fills derived
 * descriptors (commands/operators) — final composition must pass
 * registry.validateReferences() with zero problems (drift-tested).
 *
 * C++ contains no help prose: text comes exclusively from this store, so
 * content cannot fork between binaries and sources.
 ***************************************************************************/
#pragma once

#include "help/help_descriptor.h"
#include "help/help_registry.h"

#include <QString>
#include <QStringList>

#include <json/json.h>

namespace sicnu::help
{

class HelpContentStore
{
  public:
    struct LoadResult
    {
        HelpRegistry registry;
        QStringList errors;   ///< non-empty ⇒ content is invalid (tests fail)
        int descriptors = 0;
        int aliases = 0;
    };

    /// Reads every *.json under @p directory (recursive). Files that fail to
    /// parse append to errors and are skipped — never partially applied.
    static LoadResult loadFromDirectory( const QString &directory );

    /// Reads the embedded resource prefix ":/help" (startup composition).
    static LoadResult loadFromResources();

    /// Parses one JSON array-of-entries document into @p out.
    static void parseDocument( const Json::Value &document, HelpRegistry &out, QStringList &errors,
                               const QString &context );

    /// Parses a single entry object. On malformed input returns a descriptor
    /// with empty id and appends to @p errors.
    static HelpDescriptor parseEntry( const Json::Value &entry, QStringList &errors,
                                      const QString &context );

  private:
    static void parseCommon( HelpDescriptor &d, const Json::Value &entry, QStringList &errors,
                             const QString &context );
    static void loadJsonFile( const QString &path, HelpRegistry &out, QStringList &errors );
};

} // namespace sicnu::help
