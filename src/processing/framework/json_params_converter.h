// src/processing/framework/json_params_converter.h
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <json/json.h>

namespace sicnu::processing {

/// Converts a JSON object to a QVariantMap for typed parameter handoff:
/// strings → QString, bools → bool, integers → qint64, other numerics →
/// double, anything else → its styled JSON string. Non-object input yields an
/// empty map. Shared by TaskCenter (job parameters) and ToolCallDispatcher
/// (tool-call arguments) so the two converters cannot drift apart.
inline QVariantMap jsonParamsToVariantMap( const Json::Value &params )
{
  QVariantMap variantMap;
  if ( !params.isObject() )
    return variantMap;

  for ( const auto &key : params.getMemberNames() )
  {
    const Json::Value &val = params[key];
    if ( val.isString() )
      variantMap[QString::fromStdString( key )] = QString::fromStdString( val.asString() );
    else if ( val.isBool() )
      variantMap[QString::fromStdString( key )] = val.asBool();
    else if ( val.isInt() || val.isUInt() || val.isInt64() || val.isUInt64() )
      variantMap[QString::fromStdString( key )] = static_cast<qint64>( val.asInt64() );
    else if ( val.isNumeric() )
      variantMap[QString::fromStdString( key )] = val.asDouble();
    else
      variantMap[QString::fromStdString( key )] = QString::fromStdString( val.toStyledString() );
  }
  return variantMap;
}

/// Lossless JSON object → QVariantMap (recursive). Shared by the MCP server
/// (operator metadata/schemas/results) and any Json↔QVariant adapter.
inline QVariant jsonValueToVariant( const Json::Value &value );
inline QVariantMap jsonObjectToVariantMap( const Json::Value &obj );

inline QVariantMap jsonObjectToVariantMap( const Json::Value &obj )
{
  QVariantMap map;
  if ( !obj.isObject() )
    return map;
  for ( const auto &name : obj.getMemberNames() )
  {
    map.insert( QString::fromStdString( name ), jsonValueToVariant( obj[name] ) );
  }
  return map;
}

/// Lossless JSON value → QVariant (recursive): null → invalid QVariant,
/// bool → bool, ints → int/qlonglong, double → double, string → QString,
/// array → QVariantList, object → QVariantMap.
inline QVariant jsonValueToVariant( const Json::Value &value )
{
  if ( value.isNull() )
    return QVariant();
  if ( value.isBool() )
    return value.asBool();
  if ( value.isInt() || value.isUInt() )
    return value.asInt();
  if ( value.isInt64() || value.isUInt64() )
    return static_cast<qlonglong>( value.asInt64() );
  if ( value.isDouble() )
    return value.asDouble();
  if ( value.isString() )
    return QString::fromStdString( value.asString() );
  if ( value.isArray() )
  {
    QVariantList list;
    for ( Json::ArrayIndex i = 0; i < value.size(); ++i )
      list.append( jsonValueToVariant( value[i] ) );
    return list;
  }
  if ( value.isObject() )
    return jsonObjectToVariantMap( value );
  return QVariant();
}

/// Lossless QVariant → JSON value (recursive). Unknown QVariant types fall
/// back to their string form.
inline Json::Value variantToJsonValue( const QVariant &value )
{
  if ( !value.isValid() || value.isNull() )
    return Json::Value( Json::nullValue );

  switch ( value.userType() )
  {
    case QMetaType::Bool:
      return Json::Value( value.toBool() );
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
      return Json::Value( static_cast<Json::Int64>( value.toLongLong() ) );
    case QMetaType::Double:
    case QMetaType::Float:
      return Json::Value( value.toDouble() );
    case QMetaType::QString:
      return Json::Value( value.toString().toStdString() );
    case QMetaType::QVariantList:
    {
      Json::Value arr( Json::arrayValue );
      const QVariantList list = value.toList();
      for ( const QVariant &item : list )
        arr.append( variantToJsonValue( item ) );
      return arr;
    }
    case QMetaType::QStringList:
    {
      Json::Value arr( Json::arrayValue );
      const QStringList list = value.toStringList();
      for ( const QString &item : list )
        arr.append( item.toStdString() );
      return arr;
    }
    case QMetaType::QVariantMap:
    {
      Json::Value obj( Json::objectValue );
      const QVariantMap map = value.toMap();
      for ( auto it = map.constBegin(); it != map.constEnd(); ++it )
        obj[it.key().toStdString()] = variantToJsonValue( it.value() );
      return obj;
    }
    default:
      // Fallback: stringify unknown types
      return Json::Value( value.toString().toStdString() );
  }
}

/// Lossless QJsonValue → JSON value (recursive). Shared by the agent dock
/// (tool-call envelopes) and app/test callers so QJson↔Json::Value
/// conversions cannot drift apart.
inline Json::Value jsonValueFromQJson( const QJsonValue &value )
{
  if ( value.isUndefined() || value.isNull() )
    return Json::Value( Json::nullValue );
  if ( value.isBool() )
    return Json::Value( value.toBool() );
  if ( value.isDouble() )
    return Json::Value( value.toDouble() );
  if ( value.isString() )
    return Json::Value( value.toString().toStdString() );
  if ( value.isArray() )
  {
    Json::Value arr( Json::arrayValue );
    const QJsonArray array = value.toArray();
    for ( const QJsonValue &item : array )
      arr.append( jsonValueFromQJson( item ) );
    return arr;
  }
  if ( value.isObject() )
  {
    Json::Value obj( Json::objectValue );
    const QJsonObject object = value.toObject();
    for ( auto it = object.constBegin(); it != object.constEnd(); ++it )
      obj[it.key().toStdString()] = jsonValueFromQJson( it.value() );
    return obj;
  }
  return Json::Value( Json::nullValue );
}

} // namespace sicnu::processing
