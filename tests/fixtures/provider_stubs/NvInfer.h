// Compile-stub of the TensorRT v10 API surface used by
// tensorrt_provider.cpp — -fsyntax-only TU gating only.
#pragma once
#include "NvInferVersion.h"
#include <cstddef>
using cudaStream_t = void *;

namespace nvinfer1
{
enum class TensorIOMode : int { kINPUT, kOUTPUT };

struct Dims
{
    int nbDims;
    std::int64_t d[8];
};

class ILogger
{
  public:
    enum Severity : int { kINTERNAL_ERROR, kERROR, kWARNING, kINFO, kVERBOSE };
    virtual ~ILogger() = default;
    virtual void log( Severity severity, const char *msg ) noexcept = 0;
};

class IRuntime
{
  public:
    virtual ~IRuntime() = default;
    class ICudaEngine *deserializeCudaEngine( const void *, std::size_t );
};

class ICudaEngine
{
  public:
    virtual ~ICudaEngine() = default;
    virtual int getNbIOTensors() const noexcept = 0;
    virtual const char *getIOTensorName( std::size_t ) const noexcept = 0;
    virtual TensorIOMode getTensorIOMode( const char * ) const noexcept = 0;
    virtual class IExecutionContext *createExecutionContext() noexcept = 0;
};

class IExecutionContext
{
  public:
    virtual ~IExecutionContext() = default;
    virtual Dims getTensorShape( const char * ) const noexcept = 0;
    virtual bool setTensorAddress( const char *, void * ) noexcept = 0;
    virtual bool enqueueV3( cudaStream_t ) noexcept = 0;
};

inline IRuntime *createInferRuntime( ILogger & ) { return nullptr; }
} // namespace nvinfer1
