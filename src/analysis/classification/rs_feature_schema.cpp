// rs_feature_schema.cpp — see rs_feature_schema.h.
#include "rs_feature_schema.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace
{
// FNV-1a 64-bit — unseeded, deterministic across platforms/processes.
constexpr quint64 kFnvOffset = 14695981039346656037ull;
constexpr quint64 kFnvPrime = 1099511628211ull;

void fnvCombine( quint64 &hash, const char *data, size_t length )
{
  for ( size_t i = 0; i < length; ++i )
  {
    hash ^= static_cast<unsigned char>( data[i] );
    hash *= kFnvPrime;
  }
}

void fnvCombineUtf8( quint64 &hash, const QString &text )
{
  const QByteArray utf8 = text.toUtf8();
  fnvCombine( hash, utf8.constData(), static_cast<size_t>( utf8.size() ) );
}
} // namespace

QString RsFeatureSchema::kindToString( Kind kind )
{
  switch ( kind )
  {
    case Kind::Band:
      return QStringLiteral( "band" );
    case Kind::Index:
      return QStringLiteral( "index" );
    case Kind::Texture:
      return QStringLiteral( "texture" );
    case Kind::Terrain:
      return QStringLiteral( "terrain" );
    case Kind::Temporal:
      return QStringLiteral( "temporal" );
    case Kind::Other:
      return QStringLiteral( "other" );
  }
  return QStringLiteral( "other" );
}

bool RsFeatureSchema::kindFromString( const QString &text, Kind &outKind )
{
  if ( text == QLatin1String( "band" ) )
    outKind = Kind::Band;
  else if ( text == QLatin1String( "index" ) )
    outKind = Kind::Index;
  else if ( text == QLatin1String( "texture" ) )
    outKind = Kind::Texture;
  else if ( text == QLatin1String( "terrain" ) )
    outKind = Kind::Terrain;
  else if ( text == QLatin1String( "temporal" ) )
    outKind = Kind::Temporal;
  else if ( text == QLatin1String( "other" ) )
    outKind = Kind::Other;
  else
    return false;
  return true;
}

QString RsFeatureSchema::fingerprintFor( const QVector<Descriptor> &descriptors )
{
  if ( descriptors.isEmpty() )
    return QString();
  quint64 hash = kFnvOffset;
  fnvCombineUtf8( hash, QStringLiteral( "exp-rs-feature-schema/1" ) );
  for ( const Descriptor &d : descriptors )
  {
    fnvCombine( hash, "|", 1 );
    fnvCombineUtf8( hash, d.name );
    fnvCombine( hash, "|", 1 );
    fnvCombineUtf8( hash, kindToString( d.kind ) );
    fnvCombine( hash, "|", 1 );
    fnvCombineUtf8( hash, d.source );
  }
  return QString::number( hash, 16 ).rightJustified( 16, QLatin1Char( '0' ) );
}

bool RsFeatureSchema::append( const Descriptor &descriptor )
{
  if ( descriptor.name.isEmpty() )
    return false;
  for ( const Descriptor &d : mDescriptors )
  {
    if ( d.name == descriptor.name )
      return false;
  }
  mDescriptors.append( descriptor );
  return true;
}

bool RsFeatureSchema::append( const QString &name, Kind kind, const QString &source )
{
  Descriptor d;
  d.name = name;
  d.kind = kind;
  d.source = source;
  return append( d );
}

QVector<QString> RsFeatureSchema::names() const
{
  QVector<QString> out;
  out.reserve( mDescriptors.size() );
  for ( const Descriptor &d : mDescriptors )
    out.append( d.name );
  return out;
}

QJsonObject RsFeatureSchema::toJson() const
{
  QJsonObject obj;
  obj.insert( QStringLiteral( "version" ), 1 );
  obj.insert( QStringLiteral( "fingerprint" ), fingerprint() );
  QJsonArray arr;
  for ( const Descriptor &d : mDescriptors )
  {
    QJsonObject fo;
    fo.insert( QStringLiteral( "name" ), d.name );
    fo.insert( QStringLiteral( "kind" ), kindToString( d.kind ) );
    fo.insert( QStringLiteral( "source" ), d.source );
    arr.append( fo );
  }
  obj.insert( QStringLiteral( "features" ), arr );
  return obj;
}

bool RsFeatureSchema::fromJson( const QJsonObject &obj )
{
  mDescriptors.clear();
  if ( obj.value( QStringLiteral( "version" ) ).toInt() != 1 )
    return false;
  const QJsonArray arr = obj.value( QStringLiteral( "features" ) ).toArray();
  std::unordered_set<QString> seen;
  for ( const auto &v : arr )
  {
    const QJsonObject fo = v.toObject();
    Descriptor d;
    d.name = fo.value( QStringLiteral( "name" ) ).toString();
    d.source = fo.value( QStringLiteral( "source" ) ).toString();
    if ( !kindFromString( fo.value( QStringLiteral( "kind" ) ).toString(), d.kind ) )
    {
      mDescriptors.clear();
      return false;
    }
    if ( d.name.isEmpty() || !seen.insert( d.name ).second )
    {
      mDescriptors.clear();
      return false;
    }
    mDescriptors.append( d );
  }
  // Fingerprint recorded in the document must match the descriptors — this
  // is the drift gate (a tampered, reordered, or fingerprint-stripped
  // document is rejected: the field is mandatory and fail-closed).
  const QString recorded = obj.value( QStringLiteral( "fingerprint" ) ).toString();
  if ( recorded != fingerprint() )
  {
    mDescriptors.clear();
    return false;
  }
  return true;
}

// ------------------------------------------------------------ assembler ---

bool RsFeatureAssembler::addColumn( const Column &column )
{
  if ( column.name.isEmpty() )
    return false;
  for ( const Column &c : mColumns )
  {
    if ( c.name == column.name )
      return false;
  }
  if ( !mColumns.isEmpty() &&
       static_cast<int>( column.values.size() ) != mRowCount )
    return false;
  mRowCount = static_cast<int>( column.values.size() );
  mColumns.append( column );
  return true;
}

RsFeatureSchema RsFeatureAssembler::schema() const
{
  RsFeatureSchema schema;
  for ( const Column &c : mColumns )
    schema.append( c.name, c.kind, c.source );
  return schema;
}

bool RsFeatureAssembler::assemble( std::vector<float> &outRowMajor,
                                   std::vector<int> &outValidCounts ) const
{
  outRowMajor.clear();
  outValidCounts.clear();
  const int cols = columnCount();
  if ( cols == 0 )
    return false;
  outRowMajor.assign( static_cast<size_t>( mRowCount ) * cols,
                      std::numeric_limits<float>::quiet_NaN() );
  outValidCounts.assign( static_cast<size_t>( cols ), 0 );
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const bool hasNoData = !std::isnan( mNoData );
  for ( int c = 0; c < cols; ++c )
  {
    const auto &values = mColumns[static_cast<size_t>( c )].values;
    int valid = 0;
    for ( int r = 0; r < mRowCount; ++r )
    {
      float v = values[static_cast<size_t>( r )];
      if ( std::isnan( v ) || ( hasNoData && v == mNoData ) || !std::isfinite( v ) )
        v = nan;
      else
        ++valid;
      outRowMajor[static_cast<size_t>( r ) * cols + static_cast<size_t>( c )] = v;
    }
    outValidCounts[static_cast<size_t>( c )] = valid;
  }
  return true;
}
