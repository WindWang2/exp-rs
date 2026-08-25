// src/processing/algorithms/band_math_simd.h
#pragma once

#include "band_math_ast.h"

#include <QString>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace BandMath
{

enum class SimdArchitecture
{
    Scalar,
    SSE4_2,
    AVX2_FMA,
    ARM_NEON,
    OpenMP
};

class BandMathBytecode
{
public:
    BandMathBytecode() = default;
    BandMathBytecode(std::vector<BytecodeInstruction> instructions,
                     uint16_t numRegisters,
                     uint16_t outputRegister,
                     std::vector<int> referencedBands)
        : m_instructions(std::move(instructions))
        , m_numRegisters(numRegisters)
        , m_outputRegister(outputRegister)
        , m_referencedBands(std::move(referencedBands))
    {}

    static std::optional<BandMathBytecode> compile(
        const QString &expression,
        QString &outError,
        int &outErrorCol,
        std::vector<int> *outReferencedBands = nullptr);

    /// Evaluates compiled bytecode using optimal SIMD execution kernel.
    void evaluateSimd(
        const std::map<int, const float*> &bandPointers,
        float *outBuffer,
        size_t count,
        float noDataVal = std::numeric_limits<float>::quiet_NaN()) const;

    void evaluateSimd(
        const std::vector<const float*> &bandPointers,
        float *outBuffer,
        size_t count,
        float noDataVal = std::numeric_limits<float>::quiet_NaN()) const;

    /// Evaluates bytecode using scalar loop for differential oracle testing.
    void evaluateScalar(
        const std::map<int, const float*> &bandPointers,
        float *outBuffer,
        size_t count,
        float noDataVal = std::numeric_limits<float>::quiet_NaN()) const;

    void evaluateScalar(
        const std::vector<const float*> &bandPointers,
        float *outBuffer,
        size_t count,
        float noDataVal = std::numeric_limits<float>::quiet_NaN()) const;

    const std::vector<BytecodeInstruction> &instructions() const { return m_instructions; }
    uint16_t numRegisters() const { return m_numRegisters; }
    uint16_t outputRegister() const { return m_outputRegister; }
    const std::vector<int> &referencedBands() const { return m_referencedBands; }

private:
    std::vector<BytecodeInstruction> m_instructions;
    uint16_t m_numRegisters = 0;
    uint16_t m_outputRegister = 0;
    std::vector<int> m_referencedBands;
};

class Evaluator
{
public:
    virtual ~Evaluator() = default;

    virtual bool evaluate(
        const std::map<int, const float*> &bandBuffers,
        float *out,
        size_t count ) const = 0;

    virtual SimdArchitecture architecture() const = 0;
};

class Engine
{
public:
    static std::unique_ptr<Evaluator> compile(
        const QString &expression,
        CompilationResult *compResult = nullptr );

    static SimdArchitecture detectOptimalArchitecture();
};

} // namespace BandMath

namespace rs::algorithms
{
    using BytecodeOp = BandMath::BytecodeInstruction;
    using BandMathBytecode = BandMath::BandMathBytecode;
}
