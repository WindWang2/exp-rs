// rs_class_order.cpp — see rs_class_order.h for the contract.
#include "rs_class_order.h"

#include <algorithm>
#include <vector>

QVector<int> RsClassOrder::sortedClassIds( const QVector<int> &labels )
{
  std::vector<int> ids;
  ids.reserve( static_cast<size_t>( labels.size() ) );
  for ( int v : labels )
    ids.push_back( v );
  std::sort( ids.begin(), ids.end() );
  ids.erase( std::unique( ids.begin(), ids.end() ), ids.end() );
  return QVector<int>( ids.cbegin(), ids.cend() );
}

bool RsClassOrder::isValid( const QVector<int> &classIds )
{
  if ( classIds.isEmpty() )
    return false;
  for ( int i = 1; i < classIds.size(); ++i )
  {
    if ( classIds[i] <= classIds[i - 1] )
      return false;
  }
  return true;
}

bool RsClassOrder::matchesColumnCount( const QVector<int> &classIds, int probColumns )
{
  return isValid( classIds ) && classIds.size() == probColumns;
}

int RsClassOrder::columnOf( const QVector<int> &classIds, int classId )
{
  const auto it = std::lower_bound( classIds.cbegin(), classIds.cend(), classId );
  if ( it == classIds.cend() || *it != classId )
    return -1;
  return static_cast<int>( it - classIds.cbegin() );
}

QJsonDocument RsClassOrder::toJson( const QVector<int> &classIds )
{
  return QJsonDocument( toJsonArray( classIds ) );
}

bool RsClassOrder::fromJson( const QJsonDocument &doc, QVector<int> &classIds )
{
  if ( !doc.isArray() )
  {
    classIds.clear();
    return false;
  }
  return fromJsonArray( doc.array(), classIds );
}

bool RsClassOrder::fromJsonArray( const QJsonArray &arr, QVector<int> &classIds )
{
  classIds.clear();
  classIds.reserve( arr.size() );
  for ( const auto &v : arr )
  {
    if ( !v.isDouble() )
      return false;
    const double d = v.toDouble();
    const int id = static_cast<int>( d );
    if ( static_cast<double>( id ) != d )
      return false;
    classIds.append( id );
  }
  if ( !isValid( classIds ) )
  {
    classIds.clear();
    return false;
  }
  return true;
}

QJsonArray RsClassOrder::toJsonArray( const QVector<int> &classIds )
{
  QJsonArray arr;
  for ( int id : classIds )
    arr.append( id );
  return arr;
}
