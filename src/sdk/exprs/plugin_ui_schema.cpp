/***************************************************************************
 * exprs/plugin_ui_schema.cpp
 ***************************************************************************/
#include "exprs/plugin_ui_schema.h"

#include <set>

namespace exprs {

namespace {

const char *kControlTypes[] = {
    "label", "text", "number", "checkbox", "combo", "slider", "button", "group",
};

bool knownControlType( const std::string &type )
{
    for ( const char *candidate : kControlTypes )
        if ( type == candidate )
            return true;
    return false;
}

bool validId( const std::string &id )
{
    if ( id.empty() )
        return false;
    for ( const char c : id )
    {
        const bool ok = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' )
                        || ( c >= '0' && c <= '9' ) || c == '_' || c == '.' || c == '-'
                        || c == ':';
        if ( !ok )
            return false;
    }
    return true;
}

/// Appends "path: message" diagnostics.
void fail( std::vector<std::string> &errors, const std::string &path, const std::string &message )
{
    errors.push_back( path + ": " + message );
}

bool boundedString( const Json::Value &value, size_t maxLength )
{
    return value.isString() && value.asString().size() <= maxLength;
}

/// Validates one control (recursively for groups). Returns false when the
/// control is structurally invalid (fatal). @p ids accumulates sibling ids.
bool validateControl( const Json::Value &control, const PluginUiSchemaLimits &limits,
                      size_t depth, std::set<std::string> &ids,
                      std::vector<std::string> &errors, const std::string &path )
{
    if ( !control.isObject() )
    {
        fail( errors, path, "control must be an object" );
        return false;
    }
    const Json::Value id = control.get( "id", Json::Value() );
    const Json::Value type = control.get( "type", Json::Value() );
    if ( !boundedString( id, limits.maxStringLength ) || !validId( id.asString() ) )
    {
        fail( errors, path, "control id must be a non-empty bounded identifier" );
        return false;
    }
    if ( !ids.insert( id.asString() ).second )
    {
        fail( errors, path, "duplicate control id '" + id.asString() + "'" );
        return false;
    }
    if ( !boundedString( type, limits.maxStringLength ) || !knownControlType( type.asString() ) )
    {
        fail( errors, path, "unknown control type (host cannot render it)" );
        return false;
    }
    const std::string typeString = type.asString();
    const Json::Value label = control.get( "label", Json::Value() );
    if ( !label.isNull() && !boundedString( label, limits.maxStringLength ) )
    {
        fail( errors, path, "label must be a bounded string" );
        return false;
    }
    const Json::Value helpId = control.get( "helpId", Json::Value() );
    if ( !helpId.isNull() && !boundedString( helpId, limits.maxStringLength ) )
    {
        fail( errors, path, "helpId must be a bounded string" );
        return false;
    }
    // Plugin-platform 9.0: optional accessibility metadata (additive,
    // validated, capped). The renderer may surface it; the schema contract
    // only guarantees it is a bounded string.
    const Json::Value description = control.get( "description", Json::Value() );
    if ( !description.isNull() && !boundedString( description, limits.maxStringLength ) )
    {
        fail( errors, path, "description must be a bounded string" );
        return false;
    }
    const Json::Value accessibilityLabel = control.get( "accessibilityLabel", Json::Value() );
    if ( !accessibilityLabel.isNull()
         && !boundedString( accessibilityLabel, limits.maxStringLength ) )
    {
        fail( errors, path, "accessibilityLabel must be a bounded string" );
        return false;
    }

    if ( typeString == "group" )
    {
        if ( depth >= limits.maxGroupDepth )
        {
            fail( errors, path, "group nesting exceeds the depth cap" );
            return false;
        }
        const Json::Value &children = control[ "controls" ];
        if ( !children.isArray() || children.empty() )
        {
            fail( errors, path, "group requires a non-empty controls array" );
            return false;
        }
        if ( children.size() > limits.maxControlsPerPage )
        {
            fail( errors, path, "controls array exceeds the per-page cap" );
            return false;
        }
        std::set<std::string> childIds;
        bool ok = true;
        for ( const Json::Value &child : children )
            ok = validateControl( child, limits, depth + 1, childIds, errors,
                                  path + "/" + id.asString() )
                 && ok;
        return ok;
    }

    if ( typeString == "number" || typeString == "slider" )
    {
        const Json::Value minimum = control.get( "minimum", 0.0 );
        const Json::Value maximum = control.get( "maximum", 100.0 );
        const Json::Value step = control.get( "step", 1.0 );
        if ( !minimum.isNumeric() || !maximum.isNumeric() || !step.isNumeric() )  // already guards
        {
            fail( errors, path, "minimum/maximum/step must be numbers" );
            return false;
        }
        if ( minimum.asDouble() > maximum.asDouble() )
        {
            fail( errors, path, "minimum exceeds maximum" );
            return false;
        }
        if ( step.asDouble() <= 0.0 )
        {
            fail( errors, path, "step must be positive" );
            return false;
        }
        const Json::Value defaultValue = control.get( "defaultValue", Json::Value() );
        if ( !defaultValue.isNull()
             && ( !defaultValue.isNumeric() || defaultValue.asDouble() < minimum.asDouble()
                  || defaultValue.asDouble() > maximum.asDouble() ) )
        {
            fail( errors, path, "defaultValue must be a number within [minimum, maximum]" );
            return false;
        }
    }
    else if ( typeString == "combo" )
    {
        const Json::Value &options = control[ "options" ];
        if ( !options.isArray() || options.empty() )
        {
            fail( errors, path, "combo requires a non-empty options array" );
            return false;
        }
        if ( options.size() > limits.maxComboOptions )
        {
            fail( errors, path, "combo options exceed the cap" );
            return false;
        }
        std::set<std::string> values;
        for ( const Json::Value &option : options )
        {
            if ( !option.isObject() || !boundedString( option.get( "value", Json::Value() ),
                                                       limits.maxStringLength )
                  || !boundedString( option.get( "label", Json::Value() ), limits.maxStringLength ) )
            {
                fail( errors, path, "each option needs bounded 'value' and 'label' strings" );
                return false;
            }
            if ( !values.insert( option[ "value" ].asString() ).second )
            {
                fail( errors, path, "duplicate option value" );
                return false;
            }
        }
        const Json::Value defaultValue = control.get( "defaultValue", Json::Value() );
        if ( !defaultValue.isNull() && ( !boundedString( defaultValue, limits.maxStringLength )
                                         || !values.count( defaultValue.asString() ) ) )
        {
            fail( errors, path, "defaultValue must be one of the option values" );
            return false;
        }
    }
    else if ( typeString == "checkbox" )
    {
        const Json::Value defaultValue = control.get( "defaultValue", Json::Value() );
        if ( !defaultValue.isNull() && !defaultValue.isBool() )
        {
            fail( errors, path, "checkbox defaultValue must be a boolean" );
            return false;
        }
    }
    else if ( typeString == "text" )
    {
        const Json::Value defaultValue = control.get( "defaultValue", Json::Value() );
        if ( !defaultValue.isNull() && !boundedString( defaultValue, limits.maxStringLength ) )
        {
            fail( errors, path, "text defaultValue must be a bounded string" );
            return false;
        }
        const Json::Value multiline = control.get( "multiline", Json::Value() );
        if ( !multiline.isNull() && !multiline.isBool() )
        {
            fail( errors, path, "multiline must be a boolean" );
            return false;
        }
    }
    // "label" and "button" carry only id/type/label/helpId.
    return true;
}

/// Validates one contribution array of simple {id,title,...} entries.
bool validateEntries( const Json::Value &entries, const PluginUiSchemaLimits &limits,
                      const std::set<std::string> &referencedCommands, bool commandsReferenced,
                      std::vector<std::string> &errors, const std::string &path,
                      size_t &budget )
{
    if ( entries.isNull() )
        return true;
    if ( !entries.isArray() )
    {
        fail( errors, path, "must be an array" );
        return false;
    }
    budget += entries.size();
    if ( budget > limits.maxContributions )
    {
        fail( errors, path, "total entries exceed the contribution cap" );
        return false;
    }
    std::set<std::string> ids;
    bool ok = true;
    for ( const Json::Value &entry : entries )
    {
        if ( !entry.isObject() )
        {
            fail( errors, path, "entry must be an object" );
            return false;
        }
        const Json::Value id = entry.get( "id", Json::Value() );
        if ( !boundedString( id, limits.maxStringLength ) || !validId( id.asString() ) )
        {
            fail( errors, path, "entry id must be a non-empty bounded identifier" );
            return false;
        }
        if ( !ids.insert( id.asString() ).second )
        {
            fail( errors, path, "duplicate entry id '" + id.asString() + "'" );
            return false;
        }
        const Json::Value title = entry.get( "title", Json::Value() );
        if ( !boundedString( title, limits.maxStringLength ) )
        {
            fail( errors, path, "entry title must be a bounded string" );
            return false;
        }
        if ( commandsReferenced )
        {
            const Json::Value commandId = entry.get( "commandId", Json::Value() );
            if ( !boundedString( commandId, limits.maxStringLength )
                 || !referencedCommands.count( commandId.asString() ) )
            {
                fail( errors, path, "commandId '" + commandId.asString()
                                        + "' does not reference a declared command" );
                ok = false;
            }
        }
    }
    return ok;
}

} // namespace

PluginUiSchemaParseResult validatePluginUiSchema( const Json::Value &schema,
                                                  const PluginUiSchemaLimits &limits )
{
    PluginUiSchemaParseResult result;
    std::vector<std::string> &errors = result.errors;

    if ( !schema.isObject() )
    {
        fail( errors, "schema", "must be an object" );
        return result;
    }
    const Json::Value &version = schema[ "version" ];
    if ( !version.isInt() || version.asInt() != 1 )
    {
        // Type-checked BEFORE any cast: a hostile schema ("version": "1")
        // must fail VALIDATION, never throw through the worker (P2-1).
        fail( errors, "schema.version", "must be the integer 1" );
        return result;
    }

    // Commands are the reference targets; validate them first.
    std::set<std::string> commandIds;
    const Json::Value &commands = schema[ "commands" ];
    if ( !commands.isNull() )
    {
        if ( !commands.isArray() )
        {
            fail( errors, "commands", "must be an array" );
            return result;
        }
        for ( const Json::Value &command : commands )
        {
            if ( !command.isObject() )
            {
                fail( errors, "commands", "command must be an object" );
                return result;
            }
            const Json::Value id = command.get( "id", Json::Value() );
            if ( !boundedString( id, limits.maxStringLength ) || !validId( id.asString() ) )
            {
                fail( errors, "commands", "command id must be a bounded identifier" );
                return result;
            }
            if ( !commandIds.insert( id.asString() ).second )
            {
                fail( errors, "commands", "duplicate command id '" + id.asString() + "'" );
                return result;
            }
        }
    }

    size_t budget = commands.isNull() ? 0 : commands.size();
    std::set<std::string> nothing; // entries without command refs
    const Json::Value &menuItems = schema[ "menuItems" ];
    const Json::Value &contextActions = schema[ "contextActions" ];

    // menuItems and contextActions must reference DECLARED commands.
    if ( !menuItems.isNull()
         && !validateEntries( menuItems, limits, commandIds, true, errors, "menuItems", budget ) )
    {
        result.normalized = Json::Value();
        return result;
    }
    if ( !contextActions.isNull()
         && !validateEntries( contextActions, limits, commandIds, true, errors,
                              "contextActions", budget ) )
    {
        result.normalized = Json::Value();
        return result;
    }

    // Controls-bearing surfaces.
    for ( const char *surface : { "settingsPages", "dockPanels" } )
    {
        const Json::Value &pages = schema[ surface ];
        if ( pages.isNull() )
            continue;
        if ( !pages.isArray() )
        {
            fail( errors, surface, "must be an array" );
            result.normalized = Json::Value();
            return result;
        }
        budget += pages.size();
        if ( budget > limits.maxContributions )
        {
            fail( errors, surface, "total entries exceed the contribution cap" );
            result.normalized = Json::Value();
            return result;
        }
        for ( const Json::Value &page : pages )
        {
            if ( !page.isObject() )
            {
                fail( errors, surface, "page must be an object" );
                result.normalized = Json::Value();
                return result;
            }
            const Json::Value id = page.get( "id", Json::Value() );
            if ( !boundedString( id, limits.maxStringLength ) || !validId( id.asString() ) )
            {
                fail( errors, surface, "page id must be a bounded identifier" );
                result.normalized = Json::Value();
                return result;
            }
            const Json::Value title = page.get( "title", Json::Value() );
            if ( !boundedString( title, limits.maxStringLength ) )
            {
                fail( errors, surface, "page title must be a bounded string" );
                result.normalized = Json::Value();
                return result;
            }
            const Json::Value &controls = page[ "controls" ];
            if ( !controls.isArray() || controls.empty() )
            {
                fail( errors, surface, "page requires a non-empty controls array" );
                result.normalized = Json::Value();
                return result;
            }
            if ( controls.size() > limits.maxControlsPerPage )
            {
                fail( errors, surface, "controls array exceeds the per-page cap" );
                result.normalized = Json::Value();
                return result;
            }
            std::set<std::string> ids;
            for ( const Json::Value &control : controls )
                validateControl( control, limits, 1, ids, errors,
                                 std::string( surface ) + "/" + id.asString() );
        }
    }

    const Json::Value &helpTopics = schema[ "helpTopics" ];
    if ( !helpTopics.isNull()
         && !validateEntries( helpTopics, limits, nothing, false, errors, "helpTopics", budget ) )
    {
        result.normalized = Json::Value();
        return result;
    }

    if ( !errors.empty() )
    {
        result.normalized = Json::Value();
        return result;
    }
    result.normalized = schema; // bounded deep copy through jsoncpp value copy
    return result;
}

PluginUiEventParseResult validateUiEvent( const Json::Value &event,
                                          const PluginUiSchemaLimits &limits )
{
    PluginUiEventParseResult result;
    if ( !event.isObject() )
    {
        fail( result.errors, "event", "must be an object" );
        return result;
    }
    // The vocabulary is the HOST renderer's actual event vocabulary
    // (plugin_ui_schema_host.cpp emits "clicked"/"changed"/"command") plus
    // documented headroom for additive evolution ("submit", "custom").
    static const char *kEventTypes[] = { "clicked", "changed", "command", "submit", "custom" };
    const Json::Value contributionId = event.get( "contributionId", Json::Value() );
    if ( !boundedString( contributionId, limits.maxStringLength )
         || !validId( contributionId.asString() ) )
    {
        fail( result.errors, "event.contributionId", "must be a non-empty bounded identifier" );
    }
    const Json::Value controlId = event.get( "controlId", Json::Value() );
    if ( !boundedString( controlId, limits.maxStringLength ) || !validId( controlId.asString() ) )
    {
        fail( result.errors, "event.controlId", "must be a non-empty bounded identifier" );
    }
    const Json::Value eventType = event.get( "eventType", Json::Value() );
    if ( !boundedString( eventType, limits.maxStringLength ) )
    {
        fail( result.errors, "event.eventType", "must be a bounded string" );
    }
    else
    {
        bool known = false;
        for ( const char *candidate : kEventTypes )
            if ( eventType.asString() == candidate )
                known = true;
        if ( !known )
            fail( result.errors, "event.eventType",
                  "unknown event type '" + eventType.asString() + "'" );
    }
    const Json::Value &value = event[ "value" ];
    if ( !value.isNull() )
    {
        // Fast path: a plain oversized string is refused without paying the
        // JSON serialization cost. (General bound: the serialized size is
        // the true transport cost; the work is O(full value size) — the cap
        // bounds what is ACCEPTED, not the work spent rejecting.)
        if ( value.isString() && value.asString().size() > limits.maxEventValueBytes )
        {
            fail( result.errors, "event.value",
                  "string value exceeds the event cap ("
                      + std::to_string( limits.maxEventValueBytes ) + " bytes)" );
            return result;
        }
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        const std::string serialized = Json::writeString( builder, value );
        if ( serialized.size() > limits.maxEventValueBytes )
        {
            fail( result.errors, "event.value",
                  "serialized value exceeds the event cap ("
                      + std::to_string( limits.maxEventValueBytes ) + " bytes)" );
        }
    }
    return result;
}

} // namespace exprs
