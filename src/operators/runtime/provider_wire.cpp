// src/operators/runtime/provider_wire.cpp — shared wire serialization.
#include "operators/runtime/provider_wire.h"

#include <QByteArray>

namespace sicnu::operators::runtime {

namespace {

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
  blob.shape.reserve( shape.size() );
  for ( const auto &d : shape )
    blob.shape.push_back( static_cast<std::int64_t>( d.toDouble() ) );
  const std::string dtypeToken =
    tensor.value( QStringLiteral( "dtype" ) ).toString().toStdString();
  if ( !tensorDTypeFromToken( dtypeToken, &blob.dtype ) )
    throw std::runtime_error( what + ": unknown tensor dtype '" + dtypeToken + "'" );
  if ( blob.dtype == TensorDType::Float16 )
    throw std::runtime_error( what + ": float16 transport refused (no exact binding)" );
  const QByteArray raw = QByteArray::fromBase64(
    tensor.value( QStringLiteral( "data_base64" ) ).toString().toLatin1() );
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
  result.reserve( static_cast<std::size_t>( outputs.size() ) );
  for ( const auto &entry : outputs )
  {
    if ( !entry.isObject() )
      throw std::runtime_error( "provider output entry is not an object (output invalid)" );
    const QJsonObject obj = entry.toObject();
    const std::string name = obj.value( QStringLiteral( "name" ) ).toString().toStdString();
    result.push_back(
      NamedTensor{ name, decodeWireTensor( obj, "provider output '" + name + "'" ) } );
  }
  return result;
}

void checkWireProtocol( const QJsonObject &document )
{
  const std::string protocol =
    document.value( QStringLiteral( "protocol" ) ).toString().toStdString();
  if ( protocol.empty() )
    return; // pre-versioned responder: the output schema below still applies
  if ( protocol != kProviderWireProtocol )
    throw std::runtime_error( "provider speaks wire protocol '" + protocol + "' but this "
                                "runtime implements '" + kProviderWireProtocol + "'" );
}

} // namespace sicnu::operators::runtime
