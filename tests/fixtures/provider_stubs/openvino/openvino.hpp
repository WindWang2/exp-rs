// Minimal compile-stub of the OpenVINO C++ API surface used by
// src/operators/runtime/openvino_provider.cpp — for -fsyntax-only TU gating
// on hosts without the real SDK. NOT a runtime implementation.
#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace ov
{
class Shape : public std::vector<std::int64_t>
{
  public:
    Shape() = default;
    Shape( std::initializer_list<std::int64_t> l ) : std::vector<std::int64_t>( l ) {}
    Shape( const std::vector<std::int64_t> &v ) : std::vector<std::int64_t>( v ) {}
};

namespace element
{
class Type
{
  public:
    std::string get_type_name() const { return "f32"; }
    bool operator==( const Type & ) const { return true; }
    bool operator!=( const Type &o ) const { return !( *this == o ); }
};
inline const Type f32{};
} // namespace element

class Tensor
{
  public:
    Tensor() = default;
    Tensor( const element::Type &, const Shape & ) {}
    template<typename T>
    T *data()
    {
      return nullptr;
    }
    template<typename T>
    const T *data() const
    {
      return nullptr;
    }
    element::Type get_element_type() const { return element::f32; }
    Shape get_shape() const { return {}; }
};

class Node
{
};

template<typename N>
class Output
{
  public:
    std::string get_any_name() const { return {}; }
    Shape get_shape() const { return {}; }
};

class Model
{
  public:
    Output<const Node> input() const { return {}; }
    Output<const Node> output() const { return {}; }
};

class InferRequest
{
  public:
    void set_tensor( const std::string &, const Tensor & ) {}
    void infer() {}
    Tensor get_tensor( const std::string & ) const { return {}; }
};

class CompiledModel
{
  public:
    InferRequest create_infer_request() { return {}; }
};

class Core
{
  public:
    std::shared_ptr<Model> read_model( const std::string & ) const { return nullptr; }
    CompiledModel compile_model( const std::shared_ptr<Model> &, const std::string & ) const
    {
      return {};
    }
    std::vector<std::string> get_available_devices() const { return {}; }
};

struct Version
{
    std::string buildNumber;
};

inline Version get_openvino_version() { return {}; }
} // namespace ov
