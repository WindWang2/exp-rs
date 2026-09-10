// src/operators/runtime/provider_wire.h — Platform 7.0 external provider
// wire contract. ONE shared serialization for every out-of-process provider
// (HTTP, Python worker, ...): JSON envelope, base64 tensor payloads. External
// providers are plain ModelRuntimeRegistry factories — never a second
// catalog or runtime.
//
// Request document:
//   { "protocol": "exp-rs-infer/1",
//     "artifact": "<resolved artifact path>",
//     "digest":   "<content digest when known>",
//     "inputs":   [ { "name": "<contract input name>", "shape": [ints],
//                     "dtype": "<tensor dtype token>",
//                     "data_base64": "<raw little-endian row-major bytes>" } ],
//     "output_names": ["<requested head>", ...] }
// Response document (success):
//   { "outputs": [ { "name": "<graph head name or ''>",
//                    "shape": [ints], "dtype": "<token>",
//                    "data_base64": "..." } ] }
// Response document (failure):
//   { "error": "<human-readable reason>" }
#pragma once

#include "operators/runtime/tensor_blob.h"

#include <QJsonArray>
#include <QJsonObject>

#include <stdexcept>
#include <string>
#include <vector>

namespace sicnu::operators::runtime {

inline constexpr const char *kProviderWireProtocol = "exp-rs-infer/1";

/// Serializes one inference request (throwing std::runtime_error on
/// non-serializable dtypes — typed refusal, never bit-casting).
QJsonObject encodeInferRequest( const std::vector<NamedTensor> &inputs,
                                const std::vector<std::string> &outputNames,
                                const std::string &artifactPath, const std::string &digest );

/// Parses output tensors from a response "outputs" array. Throws
/// std::runtime_error with an output-invalid flavored message on shape,
/// dtype or byte-count violations.
std::vector<NamedTensor> decodeInferOutputs( const QJsonArray &outputs );

/// Extracts the wire version; throws on a foreign protocol.
void checkWireProtocol( const QJsonObject &document );

/// Parses one wire tensor object (shared by encoders that round-trip).
TensorBlob decodeWireTensor( const QJsonObject &tensor, const std::string &what );

} // namespace sicnu::operators::runtime
