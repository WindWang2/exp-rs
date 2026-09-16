// src/agent/mapspec/series_planner.cpp
#include "series_planner.h"

#include "mapspec.h"

#include <qgsexpression.h>
#include <qgsfeature.h>
#include <qgsfeaturerequest.h>
#include <qgsproject.h>
#include <qgsrectangle.h>
#include <qgsvectorlayer.h>

#include <QVariant>

#include <algorithm>
#include <map>
#include <set>

namespace sicnu::agent::mapspec {

namespace {

void addProblem( std::vector<std::string> *problems, const std::string &message )
{
    if ( problems )
        problems->push_back( message );
}

bool isScalar( const Json::Value &value )
{
    return value.isString() || value.isNumeric() || value.isBool() || value.isNull();
}

/// Scalar → substitution string (null renders as the empty string).
std::string scalarToString( const Json::Value &value )
{
    if ( value.isString() )
        return value.asString();
    if ( value.isNumeric() )
    {
        if ( value.isIntegral() )
            return std::to_string( value.asInt64() );
        char buffer[32];
        snprintf( buffer, sizeof( buffer ), "%.10g", value.asDouble() );
        return buffer;
    }
    if ( value.isBool() )
        return value.asBool() ? "true" : "false";
    return std::string();
}

/// Resolves {{token}} occurrences: page variables by name, page_number,
/// page_total. Unknown tokens stay literal and are reported once per token
/// name per page.
std::string substituteTokens( const std::string &text, const Json::Value &variables,
                              int pageNumber, int pageTotal,
                              std::set<std::string> *unknownTokens )
{
    std::string out;
    out.reserve( text.size() );
    size_t pos = 0;
    while ( pos < text.size() )
    {
        const size_t open = text.find( "{{", pos );
        if ( open == std::string::npos )
        {
            out.append( text, pos, std::string::npos );
            break;
        }
        const size_t close = text.find( "}}", open + 2 );
        if ( close == std::string::npos )
        {
            out.append( text, pos, std::string::npos );
            break;
        }
        out.append( text, pos, open - pos );
        const std::string token = text.substr( open + 2, close - open - 2 );
        std::string trimmed = token;
        const auto notSpace = []( char c ) { return c != ' ' && c != '\t'; };
        trimmed.erase( trimmed.begin(),
                       std::find_if( trimmed.begin(), trimmed.end(), notSpace ) );
        trimmed.erase( std::find_if( trimmed.rbegin(), trimmed.rend(), notSpace ).base(),
                       trimmed.end() );
        if ( trimmed == "page_number" )
            out.append( std::to_string( pageNumber ) );
        else if ( trimmed == "page_total" )
            out.append( std::to_string( pageTotal ) );
        else if ( variables.isMember( trimmed ) && isScalar( variables[trimmed] ) )
            out.append( scalarToString( variables[trimmed] ) );
        else
        {
            out.append( "{{" + token + "}}" );
            if ( unknownTokens )
                unknownTokens->insert( trimmed );
        }
        pos = close + 2;
    }
    return out;
}

/// Collections carrying free text that participates in substitution.
bool isTextCollection( const std::string &collection )
{
    return collection == "titles" || collection == "labels" ||
           collection == "annotations" || collection == "source_notes";
}

void appendTextSubstitution( Json::Value &clone, const std::string &collection,
                             const SeriesRow &row, int pageIndex, int pageTotal,
                             std::set<std::string> *unknownTokens )
{
    if ( !clone.isMember( "text" ) || !clone["text"].isString() )
        return;
    const bool mainTitle = collection == "titles" && clone.isMember( "semantic_role" ) &&
                           clone["semantic_role"].isString() &&
                           clone["semantic_role"].asString() == "title.main";
    if ( mainTitle && !row.title.empty() )
    {
        clone["text"] = row.title;
        return;
    }
    clone["text"] = substituteTokens( clone["text"].asString(), row.variables, pageIndex + 1,
                                      pageTotal, unknownTokens );
}

/// Deep-copies the template's items to page `pageIndex`, applying the row's
/// substitutions and extent. `idMap` translates every cloned id (empty for
/// page 0 where ids are kept); reference members are remapped through it.
void appendPageItems( Json::Value &spec, const Json::Value &templateSpec, int pageIndex,
                      const SeriesRow &row, int pageTotal,
                      const std::map<std::string, std::string> &idMap,
                      std::vector<std::string> *problems )
{
    std::set<std::string> unknownTokens;
    for ( int c = 0; c < kCollectionCount; ++c )
    {
        const char *collection = kCollections[c];
        if ( !templateSpec.isMember( collection ) || !templateSpec[collection].isArray() )
            continue;
        for ( const auto &item : templateSpec[collection] )
        {
            if ( !item.isObject() )
                continue;
            Json::Value clone = item;
            if ( !clone.isMember( "id" ) || !clone["id"].isString() )
                continue; // validateMapSpec owns the malformed-item report
            const std::string oldId = clone["id"].asString();
            const std::string newId =
              pageIndex == 0 ? oldId : oldId + "-p" + std::to_string( pageIndex );
            clone["id"] = newId;
            clone["page"] = pageIndex;

            // Per-row extent lands on the page's map frame.
            if ( collection == std::string( "map_frames" ) && row.extent.isArray() &&
                 row.extent.size() == 4 )
                clone["extent"] = row.extent;

            if ( isTextCollection( collection ) )
                appendTextSubstitution( clone, collection, row, pageIndex, pageTotal,
                                        &unknownTokens );

            // Reference remapping (clones reference their page siblings).
            if ( pageIndex != 0 )
            {
                if ( clone.isMember( "map_ref" ) && clone["map_ref"].isString() )
                {
                    const auto it = idMap.find( clone["map_ref"].asString() );
                    if ( it != idMap.end() )
                        clone["map_ref"] = it->second;
                }
                if ( clone.isMember( "locator" ) && clone["locator"].isObject() &&
                     clone["locator"].isMember( "target" ) &&
                     clone["locator"]["target"].isString() )
                {
                    const auto it = idMap.find( clone["locator"]["target"].asString() );
                    if ( it != idMap.end() )
                        clone["locator"]["target"] = it->second;
                }
                if ( collection == std::string( "constraints" ) &&
                     clone.isMember( "items" ) && clone["items"].isArray() )
                {
                    Json::Value remapped( Json::arrayValue );
                    for ( const auto &reference : clone["items"] )
                    {
                        if ( !reference.isString() )
                        {
                            remapped.append( reference );
                            continue;
                        }
                        const auto it = idMap.find( reference.asString() );
                        remapped.append( it != idMap.end() ? it->second : reference );
                    }
                    clone["items"] = remapped;
                }
            }
            spec[collection].append( clone );
        }
    }
    for ( const std::string &token : unknownTokens )
        addProblem( problems, "page " + std::to_string( pageIndex + 1 ) + ": unknown token {{" +
                                token + "}} kept literal" );
}

Json::Value buildIndexText( const std::vector<SeriesRow> &rows, const std::string &title )
{
    std::string text = title.empty() ? std::string( "Index" ) : title;
    text.append( ":\n" );
    for ( size_t index = 0; index < rows.size(); ++index )
    {
        const std::string label =
          rows[index].title.empty() ? std::string( "Page " ) : rows[index].title;
        text.append( std::to_string( index + 1 ) ).append( ". " ).append( label );
        if ( index + 1 < rows.size() )
            text.append( "\n" );
    }
    return text;
}

} // namespace

Json::Value seriesDefinitionToRows( const Json::Value &definition,
                                    std::vector<std::string> *problems )
{
    if ( !definition.isObject() || !definition.isMember( "rows" ) ||
         !definition["rows"].isArray() )
    {
        addProblem( problems, "table series definition needs a rows array" );
        return Json::Value( Json::nullValue );
    }
    if ( definition["rows"].empty() )
    {
        addProblem( problems, "table series definition has zero rows" );
        return Json::Value( Json::nullValue );
    }
    if ( static_cast<int>( definition["rows"].size() ) > kMaxSeriesPages )
    {
        addProblem( problems, "table series exceeds the " + std::to_string( kMaxSeriesPages ) +
                                "-page materialized bound; use the atlas delivery path" );
        return Json::Value( Json::nullValue );
    }
    Json::Value rows( Json::arrayValue );
    for ( const auto &entry : definition["rows"] )
    {
        if ( !entry.isObject() )
        {
            addProblem( problems, "every series row must be an object" );
            return Json::Value( Json::nullValue );
        }
        Json::Value row( Json::objectValue );
        if ( entry.isMember( "title" ) && entry["title"].isString() )
            row["title"] = entry["title"];
        if ( entry.isMember( "extent" ) )
        {
            const Json::Value &extent = entry["extent"];
            if ( !extent.isArray() || extent.size() != 4 || !extent[0].isNumeric() ||
                 !extent[1].isNumeric() || !extent[2].isNumeric() || !extent[3].isNumeric() )
            {
                addProblem( problems, "row extent must be [xmin, ymin, xmax, ymax] numbers" );
                return Json::Value( Json::nullValue );
            }
            row["extent"] = extent;
        }
        if ( entry.isMember( "crs" ) && entry["crs"].isString() )
            row["crs"] = entry["crs"];
        if ( entry.isMember( "feature_id" ) && entry["feature_id"].isString() )
            row["feature_id"] = entry["feature_id"];
        if ( entry.isMember( "variables" ) )
        {
            const Json::Value &variables = entry["variables"];
            if ( !variables.isObject() ||
                 static_cast<int>( variables.size() ) > kMaxSeriesVariables )
            {
                addProblem( problems, "row variables must be an object with at most " +
                                        std::to_string( kMaxSeriesVariables ) + " members" );
                return Json::Value( Json::nullValue );
            }
            for ( const auto &name : variables.getMemberNames() )
            {
                if ( !isScalar( variables[name] ) )
                {
                    addProblem( problems, "variable '" + name + "' must be scalar" );
                    return Json::Value( Json::nullValue );
                }
            }
            row["variables"] = variables;
        }
        rows.append( row );
    }
    return rows;
}

Json::Value seriesDefinitionToRowsVector( const Json::Value &definition,
                                          std::vector<std::string> *problems )
{
    if ( !definition.isObject() || !definition.isMember( "layer" ) ||
         !definition["layer"].isString() || definition["layer"].asString().empty() )
    {
        addProblem( problems, "vector series definition needs a layer name or id" );
        return Json::Value( Json::nullValue );
    }
    if ( QgsProject::instance() == nullptr )
    {
        addProblem( problems, "no QGIS project is loaded; vector series need a project layer" );
        return Json::Value( Json::nullValue );
    }
    const QString layerRef = QString::fromStdString( definition["layer"].asString() );
    QList<QgsMapLayer *> matches = QgsProject::instance()->mapLayersByName( layerRef );
    if ( matches.isEmpty() )
    {
        if ( QgsMapLayer *byId = QgsProject::instance()->mapLayer( layerRef ) )
            matches.append( byId );
    }
    if ( matches.isEmpty() )
    {
        addProblem( problems, "coverage layer '" + definition["layer"].asString() +
                                "' does not resolve in the current project" );
        return Json::Value( Json::nullValue );
    }
    auto *layer = qobject_cast<QgsVectorLayer *>( matches.first() );
    if ( !layer )
    {
        addProblem( problems, "coverage layer '" + definition["layer"].asString() +
                                "' is not a vector layer" );
        return Json::Value( Json::nullValue );
    }

    std::vector<std::string> variableFields;
    if ( definition.isMember( "variable_fields" ) )
    {
        const Json::Value &fields = definition["variable_fields"];
        if ( !fields.isArray() )
        {
            addProblem( problems, "variable_fields must be an array of field names" );
            return Json::Value( Json::nullValue );
        }
        if ( static_cast<int>( fields.size() ) > 8 )
        {
            addProblem( problems, "variable_fields exceeds the 8-field budget" );
            return Json::Value( Json::nullValue );
        }
        for ( const auto &field : fields )
        {
            if ( !field.isString() || field.asString().empty() )
            {
                addProblem( problems, "variable_fields entries must be non-empty strings" );
                return Json::Value( Json::nullValue );
            }
            variableFields.push_back( field.asString() );
        }
    }
    const std::string titleField =
      definition.isMember( "title_field" ) && definition["title_field"].isString()
        ? definition["title_field"].asString()
        : std::string();
    const int maxRows =
      definition.isMember( "max_rows" ) && definition["max_rows"].isIntegral()
        ? std::clamp( definition["max_rows"].asInt(), 1, kMaxSeriesPages )
        : kMaxSeriesPages;

    QgsFeatureRequest featureRequest;
    if ( definition.isMember( "filter" ) && definition["filter"].isString() &&
         !definition["filter"].asString().empty() )
    {
        const QString filter = QString::fromStdString( definition["filter"].asString() );
        // Validate the syntax up front (setFilterExpression has no error
        // out-param on this QGIS build): a parser error is a typed problem,
        // never a silently empty series.
        QgsExpression parsed( filter );
        if ( parsed.hasParserError() )
        {
            addProblem( problems, "filter expression rejected: " +
                                    parsed.parserErrorString().toStdString() );
            return Json::Value( Json::nullValue );
        }
        featureRequest.setFilterExpression( filter );
    }
    const bool ascending =
      !( definition.isMember( "order" ) && definition["order"].isString() &&
         definition["order"].asString() == "desc" );
    if ( definition.isMember( "sort_by" ) && definition["sort_by"].isString() &&
         !definition["sort_by"].asString().empty() )
    {
        QgsFeatureRequest::OrderBy orderBy;
        orderBy.append( QgsFeatureRequest::OrderByClause(
          QString::fromStdString( definition["sort_by"].asString() ), ascending ) );
        featureRequest.setOrderBy( orderBy );
    }

    Json::Value rows( Json::arrayValue );
    QgsFeature feature;
    QgsFeatureIterator iterator = layer->getFeatures( featureRequest );
    while ( iterator.nextFeature( feature ) )
    {
        if ( static_cast<int>( rows.size() ) >= maxRows )
        {
            addProblem( problems, "vector series exceeds the " + std::to_string( maxRows ) +
                                    "-page materialized bound (max_rows); use the atlas "
                                    "delivery path for larger products" );
            iterator.close();
            return Json::Value( Json::nullValue );
        }
        Json::Value row( Json::objectValue );
        row["feature_id"] = std::to_string( feature.id() );
        if ( !titleField.empty() && feature.attribute( titleField.c_str() ).isValid() )
            row["title"] =
              feature.attribute( titleField.c_str() ).toString().toStdString();
        Json::Value variables( Json::objectValue );
        for ( const std::string &field : variableFields )
        {
            const QVariant attribute = feature.attribute( field.c_str() );
            if ( !attribute.isValid() )
            {
                addProblem( problems, "variable field '" + field + "' not present on layer '" +
                                        definition["layer"].asString() + "'" );
                iterator.close();
                return Json::Value( Json::nullValue );
            }
            switch ( attribute.type() )
            {
                case QVariant::Int:
                case QVariant::LongLong:
                    variables[field] = static_cast<Json::Int64>( attribute.toLongLong() );
                    break;
                case QVariant::Double:
                    variables[field] = attribute.toDouble();
                    break;
                case QVariant::Bool:
                    variables[field] = attribute.toBool();
                    break;
                default:
                    variables[field] = attribute.toString().toStdString();
                    break;
            }
        }
        row["variables"] = variables;
        row["crs"] = layer->crs().authid().toStdString();
        if ( feature.hasGeometry() && !feature.geometry().isEmpty() )
        {
            const QgsRectangle box = feature.geometry().boundingBox();
            Json::Value extent( Json::arrayValue );
            extent.append( box.xMinimum() );
            extent.append( box.yMinimum() );
            extent.append( box.xMaximum() );
            extent.append( box.yMaximum() );
            row["extent"] = extent;
        }
        rows.append( row );
    }
    iterator.close();
    if ( rows.empty() )
    {
        addProblem( problems, "vector series resolved zero rows (filter excludes everything "
                              "or the layer is empty)" );
        return Json::Value( Json::nullValue );
    }
    return rows;
}

Json::Value planSeriesPages( const Json::Value &template_spec,
                             const std::vector<SeriesRow> &rows,
                             std::vector<std::string> *problems )
{
    const Json::Value base = upgradeMapSpec( template_spec );
    if ( !base.isObject() || !base.isMember( "kind" ) || !base["kind"].isString() ||
         base["kind"].asString() != "map_spec" )
    {
        addProblem( problems, "series template must be a map_spec envelope" );
        return Json::Value( Json::nullValue );
    }
    if ( rows.empty() )
    {
        addProblem( problems, "a series needs at least one row" );
        return Json::Value( Json::nullValue );
    }
    if ( static_cast<int>( rows.size() ) > kMaxSeriesPages )
    {
        addProblem( problems, "a materialized series is capped at " +
                                std::to_string( kMaxSeriesPages ) + " pages" );
        return Json::Value( Json::nullValue );
    }
    // The template must be a single-page prototype: a `pages` array (other
    // than the single explicit page) would multiply ambiguously.
    if ( base.isMember( "pages" ) && base["pages"].isArray() && base["pages"].size() > 1 )
    {
        addProblem( problems, "series template must be a single-page document" );
        return Json::Value( Json::nullValue );
    }

    Json::Value spec = base;
    // The page clones are appended fresh from `base` for EVERY page
    // (including page 0, which keeps the template ids): drop the base
    // collections first or page 0 would exist twice (original + unmodified
    // clone).
    for ( int c = 0; c < kCollectionCount; ++c )
        spec[kCollections[c]] = Json::Value( Json::arrayValue );
    const int pageTotal = static_cast<int>( rows.size() );

    // Page 0 keeps the template's item ids; pages k ≥ 1 clone every item
    // with a `-p<k>` suffix and remap the id references to their siblings.
    std::map<std::string, std::string> idMap;
    if ( pageTotal > 1 )
    {
        for ( int c = 0; c < kCollectionCount; ++c )
        {
            const char *collection = kCollections[c];
            if ( !base.isMember( collection ) || !base[collection].isArray() )
                continue;
            for ( const auto &item : base[collection] )
            {
                if ( !item.isObject() || !item.isMember( "id" ) || !item["id"].isString() )
                    continue;
                const std::string oldId = item["id"].asString();
                for ( int k = 1; k < pageTotal; ++k )
                    idMap[oldId + "@p" + std::to_string( k )] =
                      oldId + "-p" + std::to_string( k );
            }
        }
    }

    Json::Value pages( Json::arrayValue );
    for ( int k = 0; k < pageTotal; ++k )
    {
        Json::Value pageEntry( Json::objectValue );
        if ( base.isMember( "page" ) && base["page"].isObject() )
        {
            for ( const char *member : { "width_mm", "height_mm", "orientation", "margin_mm" } )
                if ( base["page"].isMember( member ) )
                    pageEntry[member] = base["page"][member];
        }
        if ( !pageEntry.isMember( "width_mm" ) )
            pageEntry["width_mm"] = 297.0;
        if ( !pageEntry.isMember( "height_mm" ) )
            pageEntry["height_mm"] = 210.0;
        if ( rows[k].variables.isObject() )
            pageEntry["variables"] = rows[k].variables;
        Json::Value provenance( Json::objectValue );
        provenance["index"] = k;
        if ( !rows[k].feature_id.empty() )
            provenance["feature_id"] = rows[k].feature_id;
        if ( !rows[k].title.empty() )
            provenance["title"] = rows[k].title;
        pageEntry["series_row"] = provenance;
        if ( !rows[k].crs.empty() )
            pageEntry["crs"] = rows[k].crs;
        pages.append( pageEntry );

        // This page's reference map: oldId → oldId-p<k>.
        std::map<std::string, std::string> pageMap;
        if ( k > 0 )
        {
            const std::string keySuffix = "@p" + std::to_string( k );
            const std::string idSuffix = "-p" + std::to_string( k );
            for ( const auto &entry : idMap )
                if ( entry.first.size() > keySuffix.size() && entry.first.compare(
                       entry.first.size() - keySuffix.size(), keySuffix.size(),
                       keySuffix ) == 0 )
                    pageMap[entry.first.substr( 0, entry.first.size() - keySuffix.size() )] =
                      entry.second;
        }
        appendPageItems( spec, base, k, rows[k], pageTotal, pageMap, problems );
    }
    spec["pages"] = pages;
    return spec;
}

Json::Value planSeries( const Json::Value &template_spec, const Json::Value &definition,
                        std::vector<std::string> *problems )
{
    if ( !definition.isObject() || !definition.isMember( "type" ) ||
         !definition["type"].isString() )
    {
        addProblem( problems, "series definition needs a type (table|vector)" );
        return Json::Value( Json::nullValue );
    }
    const std::string type = definition["type"].asString();
    Json::Value rows = Json::Value( Json::nullValue );
    if ( type == "table" )
        rows = seriesDefinitionToRows( definition, problems );
    else if ( type == "vector" )
        rows = seriesDefinitionToRowsVector( definition, problems );
    else
        addProblem( problems, "unknown series type '" + type + "' (table|vector)" );
    if ( rows.isNull() || rows.empty() )
        return Json::Value( Json::nullValue );

    std::vector<SeriesRow> parsed;
    for ( const auto &row : rows )
    {
        SeriesRow entry;
        if ( row.isMember( "title" ) )
            entry.title = row["title"].asString();
        if ( row.isMember( "extent" ) )
            entry.extent = row["extent"];
        if ( row.isMember( "crs" ) )
            entry.crs = row["crs"].asString();
        if ( row.isMember( "feature_id" ) )
            entry.feature_id = row["feature_id"].asString();
        if ( row.isMember( "variables" ) )
            entry.variables = row["variables"];
        parsed.push_back( entry );
    }

    // Optional index page: materialized as the FINAL page so the content
    // pages' numbers stay stable.
    const bool indexPage = definition.isMember( "index_page" ) &&
                           definition["index_page"].isObject() &&
                           definition["index_page"].get( "enabled", false ).asBool();
    if ( indexPage )
    {
        if ( static_cast<int>( parsed.size() ) + 1 > kMaxSeriesPages )
        {
            addProblem( problems, "index page would exceed the " +
                                    std::to_string( kMaxSeriesPages ) + "-page bound" );
            return Json::Value( Json::nullValue );
        }
        const std::string indexTitle =
          definition["index_page"].isMember( "title" ) &&
              definition["index_page"]["title"].isString()
            ? definition["index_page"]["title"].asString()
            : std::string();
        std::vector<SeriesRow> withIndex = parsed;
        SeriesRow indexRow;
        indexRow.title = indexTitle.empty() ? std::string( "Index" ) : indexTitle;
        withIndex.push_back( indexRow );
        Json::Value spec = planSeriesPages( template_spec, withIndex, problems );
        if ( spec.isNull() )
            return spec;
        Json::Value &indexEntry = spec["pages"][spec["pages"].size() - 1];
        indexEntry["role"] = "index";
        Json::Value label( Json::objectValue );
        label["id"] = "series-index";
        label["page"] = spec["pages"].size() - 1;
        Json::Value rect( Json::arrayValue );
        rect.append( 20.0 );
        rect.append( 30.0 );
        rect.append( 257.0 );
        rect.append( 150.0 );
        label["rect_mm"] = rect;
        label["text"] = buildIndexText( parsed, indexTitle );
        spec["labels"].append( label );
        return spec;
    }
    return planSeriesPages( template_spec, parsed, problems );
}

} // namespace sicnu::agent::mapspec
