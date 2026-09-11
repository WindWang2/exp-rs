// tests/support/onnx_fixture_builder.h — deterministic hand-encoded ONNX
// model fixtures for the real ONNX Runtime provider lane.
//
// The committed tests/data/test_infer_identity.onnx proves the project
// already ships hand-encoded minimal ONNX models. This header generalizes
// that trick: a tiny protobuf wire-format writer specialized to the ONNX
// schema subset (ModelProto → GraphProto → NodeProto/TensorProto/
// ValueInfoProto) so tests can BUILD their graphs in code — no onnx python
// package, no network, no checked-in binary blobs. Every model produced
// here is byte-deterministic: equal builders always emit equal files.
//
// Wire-format references: protobuf encoding
// (field key = (number<<3)|wire_type; wire types varint=0, fixed64=1,
// len=2, fixed32=5) and onnx/onnx.proto3 (ModelProto 1.x: ir_version=1,
// producer_name=2, graph=7, opset_import=8; GraphProto: node=1, name=2,
// initializer=5, input=11, output=12; NodeProto: input=1, output=2,
// name=3, op_type=4, attribute=5; TensorProto: dims=1 packed, data_type=2,
// float_data=4, int64_data=7 packed, name=8, raw_data=9; ValueInfoProto:
// name=1, type=2; TypeProto.tensor_type=1{elem_type=1, shape=2};
// TensorShapeProto.dim=1{dim_value=1|dim_param=2}; AttributeProto:
// name=1, f=2, i=3, s=4, ints=8 packed, type=20; OperatorSetIdProto:
// domain=1, version=2). The models validate through ORT itself at runtime —
// a mis-encoding fails the test loudly instead of silently passing.
#ifndef SICNU_TESTS_ONNX_FIXTURE_BUILDER_H
#define SICNU_TESTS_ONNX_FIXTURE_BUILDER_H

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace onnxfixture {

/// ONNX tensor element types (subset used by fixtures).
enum ElemType : int
{
    Float = 1,
    Uint8 = 2,
    Int8 = 3,
    UInt16 = 4,
    Int32 = 6,
    Int64 = 7,
    Double = 11
};

/// One tensor dimension: a fixed value or a named dynamic parameter.
using Dim = std::variant<std::int64_t, std::string>;
inline Dim fixed( std::int64_t v ) { return Dim{ v }; }
inline Dim dynamic( std::string param ) { return Dim{ std::move( param ) }; }

// --- protobuf wire primitives ---------------------------------------------

inline void putKey( std::string &out, int field, int wireType )
{
    std::uint64_t key = ( static_cast<std::uint64_t>( field ) << 3 ) | wireType;
    while ( key >= 0x80 )
    {
        out.push_back( static_cast<char>( ( key & 0x7f ) | 0x80 ) );
        key >>= 7;
    }
    out.push_back( static_cast<char>( key ) );
}

inline void putVarint( std::string &out, std::uint64_t value )
{
    while ( value >= 0x80 )
    {
        out.push_back( static_cast<char>( ( value & 0x7f ) | 0x80 ) );
        value >>= 7;
    }
    out.push_back( static_cast<char>( value ) );
}

inline void putVarintField( std::string &out, int field, std::uint64_t value )
{
    putKey( out, field, 0 );
    putVarint( out, value );
}

/// Zigzag for negative int64 fields carried as raw varints (dim_value is a
/// plain int64 in proto3 — negative dims never occur in fixtures, but keep
/// the encoding honest anyway).
inline void putInt64Field( std::string &out, int field, std::int64_t value )
{
    putVarintField( out, field, static_cast<std::uint64_t>( value ) );
}

inline void putLengthDelimited( std::string &out, int field, const std::string &payload )
{
    putKey( out, field, 2 );
    putVarint( out, payload.size() );
    out.append( payload );
}

inline void putStringField( std::string &out, int field, const std::string &value )
{
    putLengthDelimited( out, field, value );
}

// --- ONNX message builders -------------------------------------------------

/// AttributeProto: INT scalar ("to" for Cast, "keepdims" for reductions).
inline std::string intAttr( const std::string &name, std::int64_t value )
{
    std::string attr;
    putStringField( attr, 1, name );   // name
    putInt64Field( attr, 3, value );   // i
    putVarintField( attr, 20, 2 );     // type = INT
    return attr;
}

/// AttributeProto: INTS packed vector ("axes" for ReduceMean on opset <= 18).
inline std::string intsAttr( const std::string &name, const std::vector<std::int64_t> &values )
{
    std::string packed;
    for ( std::int64_t v : values )
        putVarint( packed, static_cast<std::uint64_t>( v ) );
    std::string attr;
    putStringField( attr, 1, name );   // name
    putLengthDelimited( attr, 8, packed ); // ints (packed)
    putVarintField( attr, 20, 7 );     // type = INTS
    return attr;
}

inline std::string floatAttr( const std::string &name, float value )
{
    std::string attr;
    putStringField( attr, 1, name );
    const std::uint32_t bits = [value] {
        std::uint32_t b = 0;
        std::memcpy( &b, &value, sizeof( b ) );
        return b;
    }();
    putKey( attr, 2, 5 ); // f, fixed32
    for ( int i = 0; i < 4; ++i )
        attr.push_back( static_cast<char>( ( bits >> ( 8 * i ) ) & 0xff ) );
    putVarintField( attr, 20, 1 ); // type = FLOAT
    return attr;
}

inline std::string encodeShape( const std::vector<Dim> &shape )
{
    std::string dims;
    for ( const Dim &d : shape )
    {
        std::string dim;
        if ( std::holds_alternative<std::int64_t>( d ) )
            putInt64Field( dim, 1, std::get<std::int64_t>( d ) ); // dim_value
        else
            putStringField( dim, 2, std::get<std::string>( d ) ); // dim_param
        putLengthDelimited( dims, 1, dim );
    }
    return dims;
}

/// TypeProto: tensor_type { elem_type, shape }.
inline std::string tensorType( int elemType, const std::vector<Dim> &shape )
{
    std::string tensor;
    putVarintField( tensor, 1, static_cast<std::uint64_t>( elemType ) );
    putLengthDelimited( tensor, 2, encodeShape( shape ) );
    std::string type;
    putLengthDelimited( type, 1, tensor );
    return type;
}

inline std::string valueInfo( const std::string &name, int elemType, const std::vector<Dim> &shape )
{
    std::string info;
    putStringField( info, 1, name );
    putLengthDelimited( info, 2, tensorType( elemType, shape ) );
    return info;
}

/// TensorProto with raw little-endian bytes (the exact on-disk encoding).
inline std::string rawTensor( const std::string &name, int elemType,
                              const std::vector<Dim> &dims, const std::string &rawBytes )
{
    std::string tensor;
    for ( const Dim &d : dims )
    {
        const std::int64_t v = std::holds_alternative<std::int64_t>( d )
                                 ? std::get<std::int64_t>( d )
                                 : 1; // dynamic dims never appear on initializers
        putInt64Field( tensor, 1, v ); // repeated int64 dims (unpackable per entry is fine too)
    }
    putVarintField( tensor, 2, static_cast<std::uint64_t>( elemType ) );
    putStringField( tensor, 8, name );
    putLengthDelimited( tensor, 9, rawBytes ); // raw_data
    return tensor;
}

inline std::string floatRaw( const std::vector<float> &values )
{
    std::string raw( values.size() * sizeof( float ), '\0' );
    std::memcpy( raw.data(), values.data(), values.size() * sizeof( float ) );
    return raw;
}

/// NodeProto: inputs/outputs by name, op_type, attributes.
inline std::string node( const std::string &opType, const std::vector<std::string> &inputs,
                         const std::vector<std::string> &outputs,
                         const std::vector<std::string> &attributes = {} )
{
    std::string n;
    for ( const std::string &in : inputs )
        putStringField( n, 1, in );
    for ( const std::string &out : outputs )
        putStringField( n, 2, out );
    for ( const std::string &attr : attributes )
        putLengthDelimited( n, 5, attr );
    putStringField( n, 4, opType );
    return n;
}

/// Assembles a full ModelProto. Graph inputs/outputs are ValueInfoProtos;
/// initializers are raw TensorProtos; opset fixed at 11 (ReduceMean keeps
/// its axes ATTRIBUTE; Sum/Add/Cast/MatMul/Identity all stable).
inline std::string buildModel( const std::vector<std::string> &nodes,
                               const std::vector<std::string> &initializers,
                               const std::vector<std::string> &graphInputs,
                               const std::vector<std::string> &graphOutputs,
                               const std::string &graphName )
{
    std::string graph;
    for ( const std::string &n : nodes )
        putLengthDelimited( graph, 1, n );
    putStringField( graph, 2, graphName );
    for ( const std::string &t : initializers )
        putLengthDelimited( graph, 5, t );
    for ( const std::string &i : graphInputs )
        putLengthDelimited( graph, 11, i );
    for ( const std::string &o : graphOutputs )
        putLengthDelimited( graph, 12, o );

    std::string opset;
    putStringField( opset, 1, "" );  // default domain
    putInt64Field( opset, 2, 11 );

    std::string model;
    putInt64Field( model, 1, 8 );    // ir_version 8 (matches the committed fixture)
    putStringField( model, 2, "exp-rs-fixture" ); // producer_name
    putLengthDelimited( model, 7, graph );
    putLengthDelimited( model, 8, opset );
    return model;
}

// --- fixture graphs ---------------------------------------------------------

inline const float *floatData( const std::vector<float> &v ) { return v.data(); }

/// Writes a model file (creating parent dirs); returns the written size.
inline std::uintmax_t writeModel( const std::filesystem::path &path, const std::string &model )
{
    std::filesystem::create_directories( path.parent_path() );
    std::ofstream file( path, std::ios::binary | std::ios::trunc );
    file.write( model.data(), static_cast<std::streamsize>( model.size() ) );
    file.close();
    return static_cast<std::uintmax_t>( model.size() );
}

/// sum(a, b) with two named N-D float inputs (broadcast not used: identical
/// shapes) — THE named multi-input known-answer fixture.
///   inputs: "a", "b" — shape @p shape, float32
///   output: "sum" — same shape
inline std::string sumDual( const std::vector<Dim> &shape )
{
    const std::vector<Dim> &s = shape;
    return buildModel(
        { node( "Sum", { "a", "b" }, { "sum" } ) },
        {},
        { valueInfo( "a", Float, s ), valueInfo( "b", Float, s ) },
        { valueInfo( "sum", Float, s ) },
        "sum_dual_fixture" );
}

/// dual-head model over one 4-D input "x":
///   "logits" = Identity(x)                       — rank 4, float32
///   "pooled" = ReduceMean(x, axes=[1], keepdims=0) — rank 3, float32
///   "total"  = ReduceMean(x, axes=[1,2,3], keepdims=1) — rank 4, (N,1,1,1)
/// Exercises multi-head outputs, differing output ranks and named selection.
inline std::string dualHead( const std::vector<Dim> &shape )
{
    return buildModel(
        { node( "Identity", { "x" }, { "logits" } ),
          node( "ReduceMean", { "x" }, { "pooled" },
                { intsAttr( "axes", { 1 } ), intAttr( "keepdims", 0 ) } ),
          node( "ReduceMean", { "x" }, { "total" },
                { intsAttr( "axes", { 1, 2, 3 } ), intAttr( "keepdims", 1 ) } ) },
        {},
        { valueInfo( "x", Float, shape ) },
        { valueInfo( "logits", Float, shape ),
          valueInfo( "pooled", Float, { shape[0], shape[2], shape[3] } ),
          valueInfo( "total", Float, { shape[0], fixed( 1 ), fixed( 1 ), fixed( 1 ) } ) },
        "dual_head_fixture" );
}

/// dynamic-shape add: y = x + bias, x [N,C,H,W] fully dynamic, bias [C].
/// Exercises dynamic dims + initializer broadcast in one forward.
/// Overload with a concrete channel count: y = x + bias over dynamic dims.
/// ONNX broadcasting aligns from the TRAILING axis, so the [C] bias is
/// stored [C,1,1] to reach the channel axis of a 4-D [N,C,H,W] input.
inline std::string dynamicAdd( int channels )
{
    std::vector<float> bias( static_cast<std::size_t>( channels ) );
    for ( int i = 0; i < channels; ++i )
        bias[static_cast<std::size_t>( i )] = static_cast<float>( i );
    return buildModel(
        { node( "Add", { "x", "bias" }, { "y" } ) },
        { rawTensor( "bias", Float, { fixed( channels ), fixed( 1 ), fixed( 1 ) }, floatRaw( bias ) ) },
        { valueInfo( "x", Float, { dynamic( "N" ), dynamic( "C" ), dynamic( "H" ), dynamic( "W" ) } ) },
        { valueInfo( "y", Float, { dynamic( "N" ), dynamic( "C" ), dynamic( "H" ), dynamic( "W" ) } ) },
        "dynamic_add_fixture" );
}

/// dtype lane: casts the float input "x" into three named outputs:
///   "as_int64" (trunc), "as_double", "as_uint8" (values must fit [0,255]).
inline std::string castLanes( const std::vector<Dim> &shape )
{
    return buildModel(
        { node( "Cast", { "x" }, { "as_int64" }, { intAttr( "to", Int64 ) } ),
          node( "Cast", { "x" }, { "as_double" }, { intAttr( "to", Double ) } ),
          node( "Cast", { "x" }, { "as_uint8" }, { intAttr( "to", Uint8 ) } ) },
        {},
        { valueInfo( "x", Float, shape ) },
        { valueInfo( "as_int64", Int64, shape ),
          valueInfo( "as_double", Double, shape ),
          valueInfo( "as_uint8", Uint8, shape ) },
        "cast_lanes_fixture" );
}

/// Sequential chain of int64-typed MatMuls over a square 2-D input:
///   y_{i+1} = MatMul(y_i, W); y_0 = "x" — [dim, dim] float32.
/// Slow enough that a forward takes seconds: the in-forward cancellation
/// fixture. @p iterations chained multiplies; W = deterministic
/// idempotent-ish matrix so partial work stays well-defined (values don't
/// matter — the forward is canceled or completed, never compared).
inline std::string slowMatmulChain( int dim, int iterations )
{
    std::vector<std::string> nodes;
    std::vector<std::string> initializers;
    std::vector<float> w( static_cast<std::size_t>( dim ) * dim, 0.0f );
    // Identity matrix: chained MatMuls keep x (well-conditioned, no NaN/inf).
    for ( int i = 0; i < dim; ++i )
        w[static_cast<std::size_t>( i ) * dim + i] = 1.0f;
    initializers.push_back(
        rawTensor( "W", Float, { fixed( dim ), fixed( dim ) }, floatRaw( w ) ) );
    std::string current = "x";
    for ( int i = 0; i < iterations; ++i )
    {
        const std::string next = "y" + std::to_string( i );
        nodes.push_back( node( "MatMul", { current, "W" }, { next } ) );
        current = next;
    }
    return buildModel(
        nodes,
        initializers,
        { valueInfo( "x", Float, { fixed( dim ), fixed( dim ) } ) },
        { valueInfo( current, Float, { fixed( dim ), fixed( dim ) } ) },
        "slow_matmul_fixture" );
}

} // namespace onnxfixture

#endif // SICNU_TESTS_ONNX_FIXTURE_BUILDER_H
