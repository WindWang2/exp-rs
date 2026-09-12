/***************************************************************************
 * exprs/plugin_ui_schema.h — declarative out-of-process UI contributions
 *
 * Out-of-process plugins can NEVER hand the host raw QWidget pointers: the
 * host owns every widget, the plugin describes what it wants and answers
 * bounded state events. This header defines the declarative contract
 * (protocol 1.1 methods "ui.describe" / "ui.invoke"):
 *
 *   describeUi() -> schema JSON (validated here, fail closed)
 *   handleUiEvent(event JSON) -> response JSON (bounded state update)
 *
 * Schema shape (v1):
 * {
 *   "version": 1,
 *   "commands":       [ { "id", "title", "helpId"? } ],
 *   "menuItems":      [ { "id", "title", "commandId", "path"? } ],
 *   "settingsPages":  [ { "id", "title", "controls": [ CONTROL... ] } ],
 *   "dockPanels":     [ { "id", "title", "controls": [ CONTROL... ] } ],
 *   "contextActions": [ { "id", "title", "commandId" } ],
 *   "helpTopics":     [ { "id", "title" } ]
 * }
 *
 * CONTROL: { "id", "type", "label", "helpId"?,
 *            "defaultValue"?,                      (text/number/checkbox)
 *            "minimum"?, "maximum"?, "step"?,       (number/slider)
 *            "options"?: [ { "value", "label" } ], (combo)
 *            "multiline"?: bool }                  (text)
 *   type: "label" | "text" | "number" | "checkbox" | "combo" | "slider" | "button"
 *         | "group" (nested "controls", depth-bounded)
 *
 * HONEST BOUNDARY: unknown CONTROL TYPES fail validation (the host cannot
 * render what it does not know); unknown FIELDS are ignored (additive
 * evolution). Everything is hard-capped (entries, controls per page, combo
 * options, string length, group depth) so a plugin cannot exhaust host
 * resources through its schema. The host renders native widgets; the
 * plugin never sees any Qt type.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <cstddef>
#include <string>
#include <vector>

namespace exprs {

struct PluginUiSchemaLimits
{
    size_t maxContributions = 32;     ///< total entries across all arrays
    size_t maxControlsPerPage = 64;   ///< controls per settings page / dock / group tree
    size_t maxComboOptions = 32;
    size_t maxStringLength = 256;     ///< labels, titles, ids, option strings
    size_t maxGroupDepth = 4;
    size_t maxEventValueBytes = 4096; ///< serialized "value" of one ui event (9.0)
};

struct PluginUiSchemaParseResult
{
    /// Canonical schema (deep copy of the input; a valid input echoes back).
    Json::Value normalized;
    std::vector<std::string> errors;   ///< fatal: the schema is refused whole
    bool ok() const { return errors.empty(); }
};

/// Result of host-side ui-event validation (9.0).
struct PluginUiEventParseResult
{
    std::vector<std::string> errors;   ///< fatal: the event is refused
    bool ok() const { return errors.empty(); }
};

/// Validates ONE host-rendered event before it travels to the plugin
/// (plugin-platform 9.0). Event shape:
///   { "contributionId", "controlId", "eventType", "value"? }
///   - contributionId / controlId: bounded valid identifiers
///   - eventType: "clicked" | "changed" | "command" (the host renderer's
///     vocabulary) | "submit" | "custom" (documented additive headroom)
///   - value: optional; any JSON within maxEventValueBytes (serialized)
/// The describe-side schema was already hard-capped in 8.0; this closes the
/// invoke-side hole where plugin-controlled event JSON traveled to the
/// plugin unchecked.
PluginUiEventParseResult validateUiEvent( const Json::Value &event,
                                          const PluginUiSchemaLimits &limits =
                                              PluginUiSchemaLimits() );

/// Validates @p schema against the v1 contract and @p limits. Returns a
/// result whose normalized value is only meaningful when ok().
PluginUiSchemaParseResult validatePluginUiSchema(
    const Json::Value &schema, const PluginUiSchemaLimits &limits = PluginUiSchemaLimits() );

/// The interface an out-of-process plugin may export through the optional
/// entry point EXPRS_createUiSchemaProviderV1 (declared Qt-free; the
/// worker probes it after plugin.load). Absent entry point = the plugin
/// offers no declarative UI; present but throwing = typed diagnostic.
class UiSchemaProviderV1
{
public:
    virtual ~UiSchemaProviderV1() = default;

    /// Returns the schema JSON (see validatePluginUiSchema). The worker
    /// validates BEFORE answering ui.describe; an invalid schema is a
    /// typed error, never a rendered surprise.
    virtual Json::Value describeUi() = 0;

    /// Handles one bounded event:
    ///   { "contributionId", "controlId", "eventType", "value" }
    /// Returns a JSON object; "state" (object controlId -> value) is
    /// applied by the host to the rendered controls when present.
    virtual Json::Value handleUiEvent( const Json::Value &event ) = 0;
};

} // namespace exprs

/// Optional second-face entry point (same export discipline as
/// EXPRS_createUiContributionV1 — see exprs/plugin_ui.h). The worker
/// resolves it with dlsym/GetProcAddress after plugin.load; absence is
/// normal (the plugin has no declarative UI).
#define EXPRS_EXPORT_UI_SCHEMA_PROVIDER(ProviderClass)                          \
    EXPRS_PLUGIN_ENTRY_EXPORT ::exprs::UiSchemaProviderV1 *EXPRS_createUiSchemaProviderV1() \
    {                                                                          \
        try                                                                    \
        {                                                                      \
            return new ProviderClass();                                        \
        }                                                                      \
        catch ( ... )                                                          \
        {                                                                      \
            return nullptr;                                                    \
        }                                                                      \
    }
