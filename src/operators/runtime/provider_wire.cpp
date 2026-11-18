// src/operators/runtime/provider_wire.cpp — shared wire serialization.
#include "operators/runtime/provider_wire.h"

#include <QByteArray>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>

namespace sicnu::operators::runtime {

namespace {

/// Rank bound of TensorBlob (its isValid() contract is 1..6 dims). Checked
/// here BEFORE the base64 payload is decoded so an over-ranked declaration is
/// refused without paying for the bytes first.
constexpr qsizetype kWireMaxRank = 6;

/// Canonical base64 geometry for @p byteCount bytes: the number of DATA
/// alphabet characters (everything except '=' padding) and how many '='
/// padding characters a canonical encoder appends. The gate below requires
/// exactly this geometry. That is deliberately STRICTER than Qt's lenient
/// fromBase64 tolerance: a payload with extra trailing data characters or
/// extra '=' can still decode to byteCount bytes under the accumulate-and-
/// discard-tail model, so the canonical count is a sufficient — not the
/// only — condition for decoded == byteCount. The strictness is the
/// admission contract: canonical encoders (Qt toBase64, Python
/// base64.b64encode, every conforming responder) always satisfy it, while
/// anything else is a malformed or hostile payload refused BEFORE the
/// decode allocates. Padding may be present, partial or omitted — all three
/// are canonical-equivalent and decode identically.
struct Base64Geometry
{
  std::size_t dataChars = 0;
  std::size_t padChars = 0;
};

/// Saturating: a requirement past this can never match a real payload (the
/// transport caps the body long before), so it reads as unrepresentable.
Base64Geometry base64GeometryFor( std::size_t byteCount )
{
  constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
  if ( byteCount > kMax / 4 * 3 )
    return { kMax, kMax };
  const std::size_t groups = ( byteCount + 2 ) / 3; // 3-byte groups
  const std::size_t remainder = byteCount % 3;
  const std::size_t pad = remainder == 0 ? 0 : 3 - remainder;
  return { groups * 4 - pad, pad };
}

/// Counts base64 alphabet characters in the Latin-1 view of the payload and
/// reports whether every '=' is trailing (after the last data character) —
/// O(n) scan, O(1) memory. Everything else (whitespace, garbage) is ignored
/// by Qt's fromBase64 too, so a legal payload never fails this scan.
void base64ShapeOf( const QByteArray &payload, std::size_t *dataChars,
                    std::size_t *padChars, bool *padTrailing )
{
  std::size_t data = 0;
  std::size_t pad = 0;
  bool sawPad = false;
  bool dataAfterPad = false;
  for ( const char c : payload )
  {
    const bool upper = c >= 'A' && c <= 'Z';
    const bool lower = c >= 'a' && c <= 'z';
    const bool digit = c >= '0' && c <= '9';
    if ( c == '=' )
    {
      sawPad = true;
      ++pad;
    }
    else if ( upper || lower || digit || c == '+' || c == '/' )
    {
      if ( sawPad )
        dataAfterPad = true;
      ++data;
    }
  }
  *dataChars = data;
  *padChars = pad;
  *padTrailing = !dataAfterPad;
}

QJsonObject encodeWireTensor( const std::string &name, const TensorBlob &blob )
{
  if ( blob.dtype == TensorDType::Float16 )
    throw std::runtime_error( "tensor '" + name + "': float16 transport refused on the "
                              "provider wire (no exact dtype token binding)" );
  QJsonObject obj;
  obj.insert( QStringLiteral( "name" ), QString::fromStdString( name ) );
  QJsonArray shape;
  for ( std::int64_t d : blob.shape )
    shape.append( static_cast<qint64>( d ) );
  obj.insert( QStringLiteral( "shape" ), shape );
  obj.insert( QStringLiteral( "dtype" ), QString::fromLatin1( tensorDTypeToken( blob.dtype ) ) );
  obj.insert( QStringLiteral( "data_base64" ),
              QString::fromLatin1(
                QByteArray( reinterpret_cast<const char *>( blob.bytes.data() ),
                            static_cast<qsizetype>( blob.bytes.size() ) )
                  .toBase64() ) );
  return obj;
}

} // namespace

QJsonObject encodeInferRequest( const std::vector<NamedTensor> &inputs,
                                const std::vector<std::string> &outputNames,
                                const std::string &artifactPath, const std::string &digest )
{
  QJsonObject doc;
  doc.insert( QStringLiteral( "protocol" ), QString::fromLatin1( kProviderWireProtocol ) );
  doc.insert( QStringLiteral( "artifact" ), QString::fromStdString( artifactPath ) );
  doc.insert( QStringLiteral( "digest" ), QString::fromStdString( digest ) );
  QJsonArray inputArray;
  for ( const NamedTensor &nt : inputs )
  {
    if ( !nt.second.isValid() )
      throw std::runtime_error( "input '" + nt.first
                                + "' is structurally invalid and cannot be serialized" );
    inputArray.append( encodeWireTensor( nt.first, nt.second ) );
  }
  doc.insert( QStringLiteral( "inputs" ), inputArray );
  QJsonArray outputArray;
  for ( const std::string &name : outputNames )
    outputArray.append( QString::fromStdString( name ) );
  doc.insert( QStringLiteral( "output_names" ), outputArray );
  return doc;
}

TensorBlob decodeWireTensor( const QJsonObject &tensor, const std::string &what )
{
  TensorBlob blob;
  const QJsonArray shape = tensor.value( QStringLiteral( "shape" ) ).toArray();
  if ( shape.size() > kWireMaxRank )
    throw std::runtime_error( what + ": tensor rank " + std::to_string( shape.size() )
                              + " exceeds the wire maximum rank "
                              + std::to_string( kWireMaxRank ) );
  blob.shape.reserve( shape.size() );
  for ( qsizetype index = 0; index < shape.size(); ++index )
  {
    const QJsonValue component = shape.at( index );
    const std::string componentName =
      what + ": shape component " + std::to_string( static_cast<long long>( index ) );
    if ( !component.isDouble() )
      throw std::runtime_error( componentName + " is not a number" );
    const double value = component.toDouble();
    // 2^63 is the first double that does not fit in int64. A fractional
    // component is refused, not truncated. In-range integers, including
    // negatives, are stored so isValid() keeps its own range check.
    if ( !std::isfinite( value ) || std::trunc( value ) != value
         || value < -9223372036854775808.0 || value >= 9223372036854775808.0 )
      throw std::runtime_error( componentName + " is not a finite integral int64" );
    blob.shape.push_back( static_cast<std::int64_t>( value ) );
  }
  const std::string dtypeToken =
    tensor.value( QStringLiteral( "dtype" ) ).toString().toStdString();
  if ( !tensorDTypeFromToken( dtypeToken, &blob.dtype ) )
    throw std::runtime_error( what + ": unknown tensor dtype '" + dtypeToken + "'" );
  if ( blob.dtype == TensorDType::Float16 )
    throw std::runtime_error( what + ": float16 transport refused (no exact binding)" );

  // Pre-decode admission (WP10): refuse impossible payloads BEFORE the base64
  // decode allocates. expectedByteCount() saturates at SIZE_MAX — a shape
  // whose byte volume is unrepresentable can never be materialized. The
  // data-character geometry then requires the CANONICAL encoding of the
  // declared byte count (strictly stricter than Qt's lenient tolerance: a
  // payload with surplus trailing data characters or surplus '=' can still
  // decode to expectedBytes bytes, and is refused here — see
  // Base64Geometry). The gate therefore never admits a payload whose decoded
  // size differs from the declaration, while canonical encoders always pass;
  // a hostile or corrupt responder pays only the O(n) scan, never the
  // decoded buffer.
  const std::size_t expectedBytes = blob.expectedByteCount();
  const Base64Geometry geometry = base64GeometryFor( expectedBytes );
  if ( geometry.dataChars == std::numeric_limits<std::size_t>::max() )
    throw std::runtime_error( what + ": declared shape/dtype byte count is outside the wire's "
                                    "admissible range (tensor is structurally invalid)" );
  const QByteArray payload =
    tensor.value( QStringLiteral( "data_base64" ) ).toString().toLatin1();
  std::size_t dataChars = 0;
  std::size_t padChars = 0;
  bool padTrailing = true;
  base64ShapeOf( payload, &dataChars, &padChars, &padTrailing );
  if ( dataChars != geometry.dataChars || padChars > geometry.padChars || !padTrailing )
    throw std::runtime_error( what + ": base64 payload length is inconsistent with the declared "
                                    "shape (needs exactly "
                              + std::to_string( expectedBytes )
                              + " bytes; tensor is structurally invalid)" );

  const QByteArray raw = QByteArray::fromBase64( payload );
  blob.bytes.assign( raw.constBegin(), raw.constEnd() );
  if ( !blob.isValid() )
    throw std::runtime_error( what + ": tensor is structurally invalid (shape/byte-count "
                                    "mismatch or out-of-range dims)" );
  return blob;
}

std::vector<NamedTensor> decodeInferOutputs( const QJsonArray &outputs )
{
  if ( outputs.empty() )
    throw std::runtime_error( "provider response carries no outputs (output invalid)" );
  std::vector<NamedTensor> result;
  // Bounded pre-reserve (WP10): the response body is capped by the transport
  // (runtime.provider.max_body_mb), but the ENTRY count it can carry is far
  // above any legal graph head count; reserving one NamedTensor per entry up
  // front could demand gigabytes before the first entry is even validated.
  // The vector grows as entries decode — every growth step is paid for by
  // bytes the transport already admitted.
  result.reserve( static_cast<std::size_t>( outputs.size() ) < 4096
                    ? static_cast<std::size_t>( outputs.size() )
                    : 4096 );
  std::set<std::string> seenNames;
  for ( const auto &entry : outputs )
  {
    if ( !entry.isObject() )
      throw std::runtime_error( "provider output entry is not an object (output invalid)" );
    const QJsonObject obj = entry.toObject();
    const QJsonValue nameValue = obj.value( QStringLiteral( "name" ) );
    // Absent (or explicit null — the same "no name" spelling checkWireProtocol
    // accepts for `protocol`) is the documented unnamed-head form. A
    // present-but mistyped name (number/bool/object/array) used to collapse
    // to '' and bind the wrong graph head — refuse the type.
    if ( !nameValue.isString() && !nameValue.isUndefined() && !nameValue.isNull() )
      throw std::runtime_error( "provider output entry has a non-string 'name' field "
                                "(output invalid)" );
    const std::string name = nameValue.toString().toStdString();
    // Two outputs answering to the same NON-EMPTY name are an ambiguous head
    // binding (consumers resolve heads by name, first match). Repeated ''
    // stays legal: positional responders may answer several unnamed heads.
    if ( !name.empty() && !seenNames.insert( name ).second )
      throw std::runtime_error( "provider output '" + name
                                + "' is declared more than once (output invalid)" );
    result.push_back(
      NamedTensor{ name, decodeWireTensor( obj, "provider output '" + name + "'" ) } );
  }
  return result;
}

void checkWireProtocol( const QJsonObject &document )
{
  const QJsonValue protocolValue = document.value( QStringLiteral( "protocol" ) );
  // Omitted (or explicit null) = pre-versioned responder: the output schema
  // below still applies. A PRESENT field of the wrong type (number, bool,
  // object, array) used to collapse to the empty string and silently read as
  // pre-versioned — a typed responder must answer with a string version.
  if ( protocolValue.isUndefined() || protocolValue.isNull() )
    return;
  if ( !protocolValue.isString() )
    throw std::runtime_error( "provider wire protocol field must be a string when present "
                              "(got a non-string value); this runtime implements '"
                              + std::string( kProviderWireProtocol ) + "'" );
  const std::string protocol = protocolValue.toString().toStdString();
  if ( protocol.empty() )
    return; // legacy empty token: pre-versioned responder compat
  if ( protocol != kProviderWireProtocol )
    throw std::runtime_error( "provider speaks wire protocol '" + protocol + "' but this "
                              "runtime implements '" + kProviderWireProtocol + "'" );
}

} // namespace sicnu::operators::runtime
