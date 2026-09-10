// src/operators/runtime/tensor_blob.cpp — TensorBlob implementation.
#include "operators/runtime/tensor_blob.h"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace sicnu::operators::runtime {

const char *tensorDTypeToken( TensorDType dtype )
{
    switch ( dtype )
    {
        case TensorDType::Float32: return "float32";
        case TensorDType::Float64: return "float64";
        case TensorDType::Int32: return "int32";
        case TensorDType::Int64: return "int64";
        case TensorDType::UInt8: return "uint8";
        case TensorDType::Int8: return "int8";
        case TensorDType::Float16: return "float16";
    }
    return "float32";
}

bool tensorDTypeFromToken( const std::string &token, TensorDType *out )
{
    TensorDType parsed = TensorDType::Float32;
    if ( token == "float32" || token == "float" || token == "fp32" )
        parsed = TensorDType::Float32;
    else if ( token == "float64" || token == "double" || token == "fp64" )
        parsed = TensorDType::Float64;
    else if ( token == "int32" || token == "int" )
        parsed = TensorDType::Int32;
    else if ( token == "int64" || token == "long" )
        parsed = TensorDType::Int64;
    else if ( token == "uint8" )
        parsed = TensorDType::UInt8;
    else if ( token == "int8" )
        parsed = TensorDType::Int8;
    else if ( token == "float16" || token == "half" || token == "fp16" )
        parsed = TensorDType::Float16;
    else
        return false;
    if ( out )
        *out = parsed;
    return true;
}

std::size_t tensorDTypeSize( TensorDType dtype )
{
    switch ( dtype )
    {
        case TensorDType::Float32: return 4;
        case TensorDType::Float64: return 8;
        case TensorDType::Int32: return 4;
        case TensorDType::Int64: return 8;
        case TensorDType::UInt8: return 1;
        case TensorDType::Int8: return 1;
        case TensorDType::Float16: return 2;
    }
    return 4;
}

int tensorDTypeCvDepth( TensorDType dtype )
{
    switch ( dtype )
    {
        case TensorDType::Float32: return CV_32F;
        case TensorDType::Float64: return CV_64F;
        case TensorDType::Int32: return CV_32S;
        case TensorDType::UInt8: return CV_8U;
        case TensorDType::Int8: return CV_8S;
        case TensorDType::Int64: return -1; // no exact cv depth — refuse, never bit-cast
        case TensorDType::Float16: return -1;
    }
    return -1;
}

bool TensorBlob::isValid() const
{
    if ( shape.empty() || shape.size() > 6 )
        return false;
    for ( std::int64_t d : shape )
        if ( d <= 0 )
            return false;
    return bytes.size() == expectedByteCount();
}

std::int64_t TensorBlob::countOrZero() const
{
    std::int64_t n = 1;
    for ( std::int64_t d : shape )
    {
        if ( d <= 0 )
            return 0;
        // Overflow guard: a declared shape whose product overflows int64 is
        // a corrupt contract, reported as zero rather than wrapped garbage.
        if ( n > std::numeric_limits<std::int64_t>::max() / d )
            return 0;
        n *= d;
    }
    return n;
}

const float *TensorBlob::dataFloat32() const
{
    if ( dtype != TensorDType::Float32 )
        throw std::runtime_error( std::string( "tensor dtype is " ) + tensorDTypeToken( dtype )
                                  + ", float32 view refused" );
    if ( !isValid() )
        throw std::runtime_error( "tensor blob is structurally invalid (shape/bytes mismatch)" );
    return reinterpret_cast<const float *>( bytes.data() );
}

TensorBlob TensorBlob::fromFloat32( std::vector<std::int64_t> shape, const float *data,
                                    std::size_t count )
{
    TensorBlob blob;
    blob.shape = std::move( shape );
    blob.dtype = TensorDType::Float32;
    blob.bytes.resize( count * sizeof( float ) );
    if ( count > 0 )
        std::memcpy( blob.bytes.data(), data, count * sizeof( float ) );
    return blob;
}

TensorBlob TensorBlob::zeros( std::vector<std::int64_t> shape, TensorDType dtype )
{
    TensorBlob blob;
    blob.shape = std::move( shape );
    blob.dtype = dtype;
    // Overflow/zero-dim guard: an absurd declared shape stays a DECLARED but
    // structurally invalid blob (no bytes) instead of a wrapped allocation.
    const std::int64_t count = blob.countOrZero();
    if ( count > 0 )
        blob.bytes.assign( static_cast<std::size_t>( count ) * tensorDTypeSize( dtype ), 0 );
    return blob;
}

TensorBlob TensorBlob::fromMat( const cv::Mat &mat )
{
    if ( mat.empty() )
        throw std::runtime_error( "cannot build a tensor from an empty cv::Mat" );
    TensorDType dtype = TensorDType::Float32;
    switch ( mat.depth() )
    {
        case CV_32F: dtype = TensorDType::Float32; break;
        case CV_64F: dtype = TensorDType::Float64; break;
        case CV_32S: dtype = TensorDType::Int32; break;
        case CV_8U: dtype = TensorDType::UInt8; break;
        case CV_8S: dtype = TensorDType::Int8; break;
        default:
            throw std::runtime_error( "cv::Mat depth has no exact tensor dtype (bit-casting "
                                      "refused)" );
    }
    TensorBlob blob;
    blob.dtype = dtype;
    blob.shape.assign( mat.size.p, mat.size.p + mat.dims );
    const std::size_t total = static_cast<std::size_t>( mat.total() ) * mat.elemSize();
    blob.bytes.resize( total );
    if ( mat.isContinuous() )
        std::memcpy( blob.bytes.data(), mat.ptr<const std::uint8_t>(), total );
    else
    {
        // Multi-dim Mats can be non-continuous when ROI'd; copy row ranges.
        std::size_t offset = 0;
        for ( int r = 0; r < mat.rows; ++r )
        {
            std::memcpy( blob.bytes.data() + offset, mat.ptr<const std::uint8_t>( r ),
                         static_cast<std::size_t>( mat.step ) );
            offset += static_cast<std::size_t>( mat.step );
        }
    }
    return blob;
}

cv::Mat TensorBlob::toMat() const
{
    const int depth = tensorDTypeCvDepth( dtype );
    if ( depth < 0 )
        throw std::runtime_error( std::string( "tensor dtype " ) + tensorDTypeToken( dtype )
                                  + " has no exact cv::Mat representation (bit-casting refused)" );
    if ( !isValid() )
        throw std::runtime_error( "tensor blob is structurally invalid (shape/bytes mismatch)" );
    std::vector<int> dims( shape.begin(), shape.end() );
    cv::Mat mat( static_cast<int>( dims.size() ), dims.data(), depth );
    // cv::Mat allocates continuous storage; multi-dim Mats expose rows as the
    // flattened tail, so a single contiguous copy of total()*elemSize() is
    // exact for freshly-allocated headers.
    const std::size_t total = static_cast<std::size_t>( mat.total() ) * mat.elemSize();
    if ( total != bytes.size() )
        throw std::runtime_error( "tensor/cv byte-count disagreement (internal contract bug)" );
    std::memcpy( mat.ptr<std::uint8_t>(), bytes.data(), total );
    return mat;
}

} // namespace sicnu::operators::runtime
