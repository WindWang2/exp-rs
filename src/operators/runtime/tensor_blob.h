// src/operators/runtime/tensor_blob.h — Platform 7.0 N-D tensor value type.
//
// The historical provider transport is a 4-D CV_32F cv::Mat; modern remote
// sensing models need 3-D sequences, 5-D temporal stacks and non-float
// dtypes at the provider boundary. TensorBlob is the runtime-side value
// type: an owning, contiguous, row-major blob with an explicit shape and
// dtype. Engines keep their cv::Mat fast paths; the cv bridge below is the
// typed (refusing, never bit-casting) conversion seam.
#pragma once

#include <opencv2/core.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::operators::runtime {

/// Tensor element dtypes the runtime layer can carry. "float16" is carried
/// as raw IEEE-754 bits (uint16) — the cv bridge refuses it (no CV_16F
/// handling in the engines); N-D providers may consume/produce it directly.
enum class TensorDType
{
    Float32,
    Float64,
    Int32,
    Int64,
    UInt8,
    Int8,
    Float16
};

const char *tensorDTypeToken( TensorDType dtype );
/// Strict token parse ("float32", "float", "fp32", "int64", "uint8", ...).
/// Returns false on unknown tokens — callers must refuse, never guess.
bool tensorDTypeFromToken( const std::string &token, TensorDType *out );
std::size_t tensorDTypeSize( TensorDType dtype );
/// The cv::Mat depth carrying this dtype exactly (bit-safe), or -1 when none
/// (Int64 / Float16) — the bridge refuses those instead of bit-casting.
int tensorDTypeCvDepth( TensorDType dtype );

/**
 * One N-D dense tensor (1..6 dims), row-major, owning its bytes.
 * `isValid()` is the load-state contract; every consumer validates before
 * reading and every producer constructs through factories that keep the
 * invariants (shape/dtype/bytes agreement) true by construction.
 */
struct TensorBlob
{
    std::vector<std::int64_t> shape;
    TensorDType dtype = TensorDType::Float32;
    std::vector<std::uint8_t> bytes;

    int rank() const { return static_cast<int>( shape.size() ); }
    std::size_t elementCount() const
    {
        std::size_t n = 1;
        for ( std::int64_t d : shape )
            n *= static_cast<std::size_t>( d < 0 ? 0 : d );
        return n;
    }
    std::size_t byteCount() const { return bytes.size(); }
    /// Expected byte size for the declared shape+dtype (bytes.size() must
    /// equal this for a valid blob).
    std::size_t expectedByteCount() const
    {
        return elementCount() * tensorDTypeSize( dtype );
    }
    bool empty() const { return shape.empty() || bytes.empty(); }
    /// Structural validity: rank in [1,6], positive dims, exact byte count.
    bool isValid() const;

    /// Total shape product as int64 (overflow-checked) — for providers that
    /// need the count without materializing.
    std::int64_t countOrZero() const;

    /// float32 view — the common engine path. Throws std::runtime_error when
    /// the blob is not Float32 or structurally invalid (typed refusal).
    const float *dataFloat32() const;

    static TensorBlob fromFloat32( std::vector<std::int64_t> shape, const float *data,
                                   std::size_t count );
    static TensorBlob zeros( std::vector<std::int64_t> shape, TensorDType dtype );

    /// cv::Mat bridge — exact dtype mapping only (CV_32F/64F/32S/8U for
    /// Float32/Float64/Int32/UInt8/Int8; Int64/Float16 have no CV depth and
    /// refuse). Multi-dim Mats are supported. fromMat throws on empty mats.
    static TensorBlob fromMat( const cv::Mat &mat );
    cv::Mat toMat() const;
};

/// One named input/output for N-D forward passes (Platform 7.0 named bind):
/// the name is the manifest input contract's name and MUST match the graph's
/// own tensor name for providers that declare namedBind capability.
using NamedTensor = std::pair<std::string, TensorBlob>;

} // namespace sicnu::operators::runtime
