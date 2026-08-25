// src/processing/algorithms/band_math_simd.cpp — SIMD AST Band-Math Engine
#include "band_math_simd.h"
#include "band_math_ast.h"
#include "core/sicnu_logging.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace BandMath
{

static constexpr size_t kChunkSize = 256;
static constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

std::optional<BandMathBytecode> BandMathBytecode::compile(
    const QString &expression,
    QString &outError,
    int &outErrorCol,
    std::vector<int> *outReferencedBands)
{
    if (expression.trimmed().isEmpty()) {
        outError = QStringLiteral("Empty expression");
        outErrorCol = 1;
        return std::nullopt;
    }

    Parser parser(expression);
    auto ast = parser.parse();
    if (!ast || parser.hasError()) {
        outError = parser.error().message;
        outErrorCol = parser.error().column();
        return std::nullopt;
    }

    std::vector<int> refs;
    ast->collectRefs(refs);
    std::sort(refs.begin(), refs.end());
    refs.erase(std::unique(refs.begin(), refs.end()), refs.end());

    if (outReferencedBands) {
        *outReferencedBands = refs;
    }

    BytecodeEmitter emitter;
    uint16_t outReg = ast->emitBytecode(emitter);
    uint16_t numRegs = std::max<uint16_t>(emitter.maxRegs(), outReg + 1);

    outError.clear();
    outErrorCol = 0;
    return BandMathBytecode(emitter.instructions(), numRegs, outReg, refs);
}

SimdArchitecture Engine::detectOptimalArchitecture()
{
#if defined(__x86_64__) || defined(_M_X64)
    if (__builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma")) {
        return SimdArchitecture::AVX2_FMA;
    } else if (__builtin_cpu_supports("sse4.2")) {
        return SimdArchitecture::SSE4_2;
    }
#elif defined(__ARM_NEON) || defined(__aarch64__)
    return SimdArchitecture::ARM_NEON;
#endif
    return SimdArchitecture::OpenMP;
}

// --- Scalar Evaluation Implementation ---

static inline float evalScalarOp1(OpCode op, float a)
{
    if (!std::isfinite(a)) return kNaN;
    switch (op) {
        case OpCode::Neg: return -a;
        case OpCode::Not: return (a == 0.0f) ? 1.0f : 0.0f;
        case OpCode::Abs: return std::fabs(a);
        case OpCode::Sqrt: return (a < 0.0f) ? kNaN : std::sqrt(a);
        case OpCode::Cbrt: return std::cbrt(a);
        case OpCode::Exp: return std::exp(a);
        case OpCode::Log: return (a <= 0.0f) ? kNaN : std::log(a);
        case OpCode::Log10: return (a <= 0.0f) ? kNaN : std::log10(a);
        case OpCode::Sin: return std::sin(a);
        case OpCode::Cos: return std::cos(a);
        case OpCode::Tan: return std::tan(a);
        case OpCode::Asin: return (a < -1.0f || a > 1.0f) ? kNaN : std::asin(a);
        case OpCode::Acos: return (a < -1.0f || a > 1.0f) ? kNaN : std::acos(a);
        case OpCode::Atan: return std::atan(a);
        case OpCode::Ceil: return std::ceil(a);
        case OpCode::Floor: return std::floor(a);
        case OpCode::Round: return std::round(a);
        default: return kNaN;
    }
}

static inline float evalScalarOp2(OpCode op, float a, float b)
{
    if (!std::isfinite(a) || !std::isfinite(b)) return kNaN;
    switch (op) {
        case OpCode::Add: return a + b;
        case OpCode::Sub: return a - b;
        case OpCode::Mul: return a * b;
        case OpCode::Div: return (b == 0.0f) ? kNaN : (a / b);
        case OpCode::Mod: return (b == 0.0f) ? kNaN : std::fmod(a, b);
        case OpCode::Pow: return std::pow(a, b);
        case OpCode::Min: return std::min(a, b);
        case OpCode::Max: return std::max(a, b);
        case OpCode::Atan2: return std::atan2(a, b);
        case OpCode::CmpLt: return (a < b) ? 1.0f : 0.0f;
        case OpCode::CmpLe: return (a <= b) ? 1.0f : 0.0f;
        case OpCode::CmpGt: return (a > b) ? 1.0f : 0.0f;
        case OpCode::CmpGe: return (a >= b) ? 1.0f : 0.0f;
        case OpCode::CmpEq: return (a == b) ? 1.0f : 0.0f;
        case OpCode::CmpNe: return (a != b) ? 1.0f : 0.0f;
        case OpCode::LogicAnd: return (a != 0.0f && b != 0.0f) ? 1.0f : 0.0f;
        case OpCode::LogicOr: return (a != 0.0f || b != 0.0f) ? 1.0f : 0.0f;
        default: return kNaN;
    }
}

static inline float evalScalarOp3(OpCode op, float a, float b, float c)
{
    switch (op) {
        case OpCode::Select:
            if (!std::isfinite(a)) return kNaN;
            return (a != 0.0f) ? b : c;
        case OpCode::Clamp:
            if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) return kNaN;
            return std::min(std::max(a, b), c);
        case OpCode::Fma:
            if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) return kNaN;
            return std::fma(a, b, c);
        default: return kNaN;
    }
}

void BandMathBytecode::evaluateScalar(
    const std::map<int, const float*> &bandPointers,
    float *outBuffer,
    size_t count,
    float noDataVal) const
{
    if (!outBuffer || count == 0) return;

    std::vector<float> regs(m_numRegisters, 0.0f);
    const bool checkNoData = std::isfinite(noDataVal);

    for (size_t i = 0; i < count; i++) {
        for (const auto &inst : m_instructions) {
            switch (inst.op) {
                case OpCode::LoadConst:
                    regs[inst.dst] = inst.constVal;
                    break;
                case OpCode::LoadBand: {
                    auto it = bandPointers.find(inst.bandNum);
                    if (it != bandPointers.end() && it->second != nullptr) {
                        float v = it->second[i];
                        if (!std::isfinite(v) || (checkNoData && v == noDataVal)) {
                            regs[inst.dst] = kNaN;
                        } else {
                            regs[inst.dst] = v;
                        }
                    } else {
                        regs[inst.dst] = kNaN;
                    }
                    break;
                }
                case OpCode::Neg:
                case OpCode::Not:
                case OpCode::Abs:
                case OpCode::Sqrt:
                case OpCode::Cbrt:
                case OpCode::Exp:
                case OpCode::Log:
                case OpCode::Log10:
                case OpCode::Sin:
                case OpCode::Cos:
                case OpCode::Tan:
                case OpCode::Asin:
                case OpCode::Acos:
                case OpCode::Atan:
                case OpCode::Ceil:
                case OpCode::Floor:
                case OpCode::Round:
                    regs[inst.dst] = evalScalarOp1(inst.op, regs[inst.src1]);
                    break;
                case OpCode::Add:
                case OpCode::Sub:
                case OpCode::Mul:
                case OpCode::Div:
                case OpCode::Mod:
                case OpCode::Pow:
                case OpCode::Min:
                case OpCode::Max:
                case OpCode::Atan2:
                case OpCode::CmpLt:
                case OpCode::CmpLe:
                case OpCode::CmpGt:
                case OpCode::CmpGe:
                case OpCode::CmpEq:
                case OpCode::CmpNe:
                case OpCode::LogicAnd:
                case OpCode::LogicOr:
                    regs[inst.dst] = evalScalarOp2(inst.op, regs[inst.src1], regs[inst.src2]);
                    break;
                case OpCode::Select:
                case OpCode::Clamp:
                case OpCode::Fma:
                    regs[inst.dst] = evalScalarOp3(inst.op, regs[inst.src1], regs[inst.src2], regs[inst.src3]);
                    break;
            }
        }
        outBuffer[i] = regs[m_outputRegister];
    }
}

void BandMathBytecode::evaluateScalar(
    const std::vector<const float*> &bandPointers,
    float *outBuffer,
    size_t count,
    float noDataVal) const
{
    std::map<int, const float*> bandMap;
    for (size_t b = 0; b < bandPointers.size(); b++) {
        bandMap[static_cast<int>(b + 1)] = bandPointers[b];
    }
    evaluateScalar(bandMap, outBuffer, count, noDataVal);
}

// --- AVX2 Execution Kernel ---

#if defined(__x86_64__) || defined(_M_X64)

// Mask of lanes where x is NaN or Inf: |x| <= FLT_MAX is false for both the
// unordered NaN compare and Inf, so a single LE compare detects either. The
// scalar path (evalScalarOpN) rejects every non-finite operand with NaN, so
// the vector kernels must sanitize non-finite inputs the same way to keep
// SIMD/scalar parity (only finite-finite arithmetic may run raw IEEE).
__attribute__((target("avx2,fma")))
static inline __m256 avx2NonFiniteMask(__m256 x)
{
    const __m256 absMask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));
    const __m256 maxFloat = _mm256_set1_ps(std::numeric_limits<float>::max());
    const __m256 finite = _mm256_cmp_ps(_mm256_and_ps(x, absMask), maxFloat, _CMP_LE_OQ);
    const __m256 allOnes = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
    return _mm256_xor_ps(finite, allOnes);
}

__attribute__((target("avx2,fma")))
static inline __m256 avx2Sanitize2(__m256 r, __m256 a, __m256 b)
{
    const __m256 nanVal = _mm256_set1_ps(kNaN);
    const __m256 bad = _mm256_or_ps(avx2NonFiniteMask(a), avx2NonFiniteMask(b));
    return _mm256_blendv_ps(r, nanVal, bad);
}

__attribute__((target("avx2,fma")))
static inline __m256 avx2Sanitize3(__m256 r, __m256 a, __m256 b, __m256 c)
{
    const __m256 nanVal = _mm256_set1_ps(kNaN);
    __m256 bad = _mm256_or_ps(_mm256_or_ps(avx2NonFiniteMask(a), avx2NonFiniteMask(b)),
                              avx2NonFiniteMask(c));
    return _mm256_blendv_ps(r, nanVal, bad);
}

__attribute__((target("avx2,fma")))
static inline __m256 avx2Div(__m256 a, __m256 b)
{
    const __m256 zero = _mm256_setzero_ps();
    const __m256 nanVal = _mm256_set1_ps(kNaN);
    __m256 isZero = _mm256_cmp_ps(b, zero, _CMP_EQ_OQ);
    __m256 safeDen = _mm256_blendv_ps(b, _mm256_set1_ps(1.0f), isZero);
    __m256 divRes = _mm256_div_ps(a, safeDen);
    divRes = _mm256_blendv_ps(divRes, nanVal, isZero);
    return avx2Sanitize2(divRes, a, b);
}

__attribute__((target("avx2,fma")))
static inline __m256 avx2Min(__m256 a, __m256 b)
{
    __m256 m = _mm256_min_ps(a, b);
    return avx2Sanitize2(m, a, b);
}

__attribute__((target("avx2,fma")))
static inline __m256 avx2Max(__m256 a, __m256 b)
{
    __m256 m = _mm256_max_ps(a, b);
    return avx2Sanitize2(m, a, b);
}

// _mm256_cmp_ps requires its predicate as a compile-time immediate, so the
// predicate is a template parameter rather than a runtime argument.
template <int Predicate>
__attribute__((target("avx2,fma")))
static inline __m256 avx2Cmp(__m256 a, __m256 b)
{
    const __m256 one = _mm256_set1_ps(1.0f);
    __m256 mask = _mm256_cmp_ps(a, b, Predicate);
    __m256 val = _mm256_and_ps(mask, one);
    return avx2Sanitize2(val, a, b);
}

__attribute__((target("avx2,fma")))
static inline __m256 avx2LogicAnd(__m256 a, __m256 b)
{
    const __m256 zero = _mm256_setzero_ps();
    const __m256 one = _mm256_set1_ps(1.0f);
    __m256 aNz = _mm256_cmp_ps(a, zero, _CMP_NEQ_OQ);
    __m256 bNz = _mm256_cmp_ps(b, zero, _CMP_NEQ_OQ);
    __m256 both = _mm256_and_ps(aNz, bNz);
    __m256 val = _mm256_and_ps(both, one);
    return avx2Sanitize2(val, a, b);
}

__attribute__((target("avx2,fma")))
static inline __m256 avx2LogicOr(__m256 a, __m256 b)
{
    const __m256 zero = _mm256_setzero_ps();
    const __m256 one = _mm256_set1_ps(1.0f);
    __m256 aNz = _mm256_cmp_ps(a, zero, _CMP_NEQ_OQ);
    __m256 bNz = _mm256_cmp_ps(b, zero, _CMP_NEQ_OQ);
    __m256 either = _mm256_or_ps(aNz, bNz);
    __m256 val = _mm256_and_ps(either, one);
    return avx2Sanitize2(val, a, b);
}

__attribute__((target("avx2,fma")))
static inline __m256 avx2Select(__m256 cond, __m256 trueVal, __m256 falseVal)
{
    // Scalar Select only requires the condition to be finite; the selected
    // lane is returned raw (an overflowing sub-expression stays Inf).
    const __m256 zero = _mm256_setzero_ps();
    const __m256 nanVal = _mm256_set1_ps(kNaN);
    __m256 isNan = avx2NonFiniteMask(cond);
    __m256 isTrue = _mm256_cmp_ps(cond, zero, _CMP_NEQ_OQ);
    __m256 sel = _mm256_blendv_ps(falseVal, trueVal, isTrue);
    return _mm256_blendv_ps(sel, nanVal, isNan);
}

__attribute__((target("avx2,fma")))
static inline __m256 avx2Sqrt(__m256 a)
{
    const __m256 zero = _mm256_setzero_ps();
    __m256 sq = _mm256_sqrt_ps(a);
    __m256 isNeg = _mm256_cmp_ps(a, zero, _CMP_LT_OQ);
    const __m256 nanVal = _mm256_set1_ps(kNaN);
    __m256 guarded = _mm256_blendv_ps(sq, nanVal, isNeg);
    return avx2Sanitize2(guarded, a, a);
}

__attribute__((target("avx2,fma")))
static inline __m256 avx2Abs(__m256 a)
{
    const __m256 signMask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));
    return avx2Sanitize2(_mm256_and_ps(a, signMask), a, a);
}

__attribute__((target("avx2,fma")))
static inline __m256 avx2Neg(__m256 a)
{
    const __m256 signBit = _mm256_castsi256_ps(_mm256_set1_epi32(static_cast<int>(0x80000000)));
    return avx2Sanitize2(_mm256_xor_ps(a, signBit), a, a);
}

__attribute__((target("avx2,fma")))
static inline __m256 avx2Not(__m256 a)
{
    const __m256 zero = _mm256_setzero_ps();
    const __m256 one = _mm256_set1_ps(1.0f);
    __m256 isEqZero = _mm256_cmp_ps(a, zero, _CMP_EQ_OQ);
    __m256 val = _mm256_and_ps(isEqZero, one);
    return avx2Sanitize2(val, a, a);
}

__attribute__((target("avx2,fma")))
static void evaluateChunkAvx2(
    const std::vector<BytecodeInstruction> &instructions,
    uint16_t numRegisters,
    uint16_t outputRegister,
    const std::map<int, const float*> &bandPointers,
    float *outChunk,
    size_t chunkStart,
    size_t chunkLen,
    float noDataVal)
{
    std::vector<float> regBuffer(numRegisters * kChunkSize, 0.0f);
    auto getRegPtr = [&](uint16_t r) { return regBuffer.data() + r * kChunkSize; };

    const bool checkNoData = std::isfinite(noDataVal);
    const __m256 noDataVec = checkNoData ? _mm256_set1_ps(noDataVal) : _mm256_setzero_ps();
    const __m256 nanVal = _mm256_set1_ps(kNaN);

    const size_t vecLimit = (chunkLen / 8) * 8;

    for (const auto &inst : instructions) {
        float *dst = getRegPtr(inst.dst);
        const float *src1 = getRegPtr(inst.src1);
        const float *src2 = getRegPtr(inst.src2);
        const float *src3 = getRegPtr(inst.src3);

        switch (inst.op) {
            case OpCode::LoadConst: {
                __m256 c = _mm256_set1_ps(inst.constVal);
                for (size_t i = 0; i < vecLimit; i += 8) {
                    _mm256_storeu_ps(dst + i, c);
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = inst.constVal;
                }
                break;
            }
            case OpCode::LoadBand: {
                auto it = bandPointers.find(inst.bandNum);
                if (it != bandPointers.end() && it->second != nullptr) {
                    const float *src = it->second + chunkStart;
                    for (size_t i = 0; i < vecLimit; i += 8) {
                        __m256 v = _mm256_loadu_ps(src + i);
                        // Match the scalar LoadBand: NaN *and* Inf both map to
                        // NaN so downstream arithmetic never sees raw Inf.
                        __m256 bad = avx2NonFiniteMask(v);
                        if (checkNoData) {
                            __m256 isNd = _mm256_cmp_ps(v, noDataVec, _CMP_EQ_OQ);
                            bad = _mm256_or_ps(bad, isNd);
                        }
                        __m256 cleaned = _mm256_blendv_ps(v, nanVal, bad);
                        _mm256_storeu_ps(dst + i, cleaned);
                    }
                    for (size_t i = vecLimit; i < chunkLen; i++) {
                        float v = src[i];
                        if (!std::isfinite(v) || (checkNoData && v == noDataVal)) {
                            dst[i] = kNaN;
                        } else {
                            dst[i] = v;
                        }
                    }
                } else {
                    for (size_t i = 0; i < vecLimit; i += 8) {
                        _mm256_storeu_ps(dst + i, nanVal);
                    }
                    for (size_t i = vecLimit; i < chunkLen; i++) {
                        dst[i] = kNaN;
                    }
                }
                break;
            }
            case OpCode::Add: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2Sanitize2(_mm256_add_ps(a, b), a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::Add, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::Sub: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2Sanitize2(_mm256_sub_ps(a, b), a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::Sub, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::Mul: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2Sanitize2(_mm256_mul_ps(a, b), a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::Mul, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::Div: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2Div(a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::Div, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::Fma: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    __m256 c = _mm256_loadu_ps(src3 + i);
                    __m256 r = _mm256_fmadd_ps(a, b, c);
                    _mm256_storeu_ps(dst + i, avx2Sanitize3(r, a, b, c));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp3(OpCode::Fma, src1[i], src2[i], src3[i]);
                }
                break;
            }
            case OpCode::Min: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2Min(a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::Min, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::Max: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2Max(a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::Max, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::Clamp: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 x = _mm256_loadu_ps(src1 + i);
                    __m256 lo = _mm256_loadu_ps(src2 + i);
                    __m256 hi = _mm256_loadu_ps(src3 + i);
                    __m256 cl = avx2Min(avx2Max(x, lo), hi);
                    _mm256_storeu_ps(dst + i, cl);
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp3(OpCode::Clamp, src1[i], src2[i], src3[i]);
                }
                break;
            }
            case OpCode::CmpLt: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2Cmp<_CMP_LT_OQ>(a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::CmpLt, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::CmpLe: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2Cmp<_CMP_LE_OQ>(a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::CmpLe, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::CmpGt: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2Cmp<_CMP_GT_OQ>(a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::CmpGt, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::CmpGe: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2Cmp<_CMP_GE_OQ>(a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::CmpGe, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::CmpEq: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2Cmp<_CMP_EQ_OQ>(a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::CmpEq, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::CmpNe: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2Cmp<_CMP_NEQ_OQ>(a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::CmpNe, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::LogicAnd: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2LogicAnd(a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::LogicAnd, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::LogicOr: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    __m256 b = _mm256_loadu_ps(src2 + i);
                    _mm256_storeu_ps(dst + i, avx2LogicOr(a, b));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(OpCode::LogicOr, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::Select: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 cond = _mm256_loadu_ps(src1 + i);
                    __m256 tVal = _mm256_loadu_ps(src2 + i);
                    __m256 fVal = _mm256_loadu_ps(src3 + i);
                    _mm256_storeu_ps(dst + i, avx2Select(cond, tVal, fVal));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp3(OpCode::Select, src1[i], src2[i], src3[i]);
                }
                break;
            }
            case OpCode::Sqrt: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    _mm256_storeu_ps(dst + i, avx2Sqrt(a));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp1(OpCode::Sqrt, src1[i]);
                }
                break;
            }
            case OpCode::Abs: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    _mm256_storeu_ps(dst + i, avx2Abs(a));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp1(OpCode::Abs, src1[i]);
                }
                break;
            }
            case OpCode::Neg: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    _mm256_storeu_ps(dst + i, avx2Neg(a));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp1(OpCode::Neg, src1[i]);
                }
                break;
            }
            case OpCode::Not: {
                for (size_t i = 0; i < vecLimit; i += 8) {
                    __m256 a = _mm256_loadu_ps(src1 + i);
                    _mm256_storeu_ps(dst + i, avx2Not(a));
                }
                for (size_t i = vecLimit; i < chunkLen; i++) {
                    dst[i] = evalScalarOp1(OpCode::Not, src1[i]);
                }
                break;
            }
            default: {
                // Transcendental and other functions evaluated across the chunk
                for (size_t i = 0; i < chunkLen; i++) {
                    switch (inst.op) {
                        case OpCode::Mod:
                        case OpCode::Pow:
                        case OpCode::Atan2:
                            dst[i] = evalScalarOp2(inst.op, src1[i], src2[i]);
                            break;
                        case OpCode::Cbrt:
                        case OpCode::Exp:
                        case OpCode::Log:
                        case OpCode::Log10:
                        case OpCode::Sin:
                        case OpCode::Cos:
                        case OpCode::Tan:
                        case OpCode::Asin:
                        case OpCode::Acos:
                        case OpCode::Atan:
                        case OpCode::Ceil:
                        case OpCode::Floor:
                        case OpCode::Round:
                            dst[i] = evalScalarOp1(inst.op, src1[i]);
                            break;
                        default:
                            dst[i] = kNaN;
                            break;
                    }
                }
                break;
            }
        }
    }

    const float *outRegPtr = getRegPtr(outputRegister);
    std::memcpy(outChunk, outRegPtr, chunkLen * sizeof(float));
}
#endif

// --- OpenMP / Auto-vectorized Fallback Kernel ---

static void evaluateChunkOpenMp(
    const std::vector<BytecodeInstruction> &instructions,
    uint16_t numRegisters,
    uint16_t outputRegister,
    const std::map<int, const float*> &bandPointers,
    float *outChunk,
    size_t chunkStart,
    size_t chunkLen,
    float noDataVal)
{
    std::vector<float> regBuffer(numRegisters * kChunkSize, 0.0f);
    auto getRegPtr = [&](uint16_t r) { return regBuffer.data() + r * kChunkSize; };
    const bool checkNoData = std::isfinite(noDataVal);

    for (const auto &inst : instructions) {
        float *dst = getRegPtr(inst.dst);
        const float *src1 = getRegPtr(inst.src1);
        const float *src2 = getRegPtr(inst.src2);
        const float *src3 = getRegPtr(inst.src3);

        switch (inst.op) {
            case OpCode::LoadConst: {
                for (size_t i = 0; i < chunkLen; i++) {
                    dst[i] = inst.constVal;
                }
                break;
            }
            case OpCode::LoadBand: {
                auto it = bandPointers.find(inst.bandNum);
                if (it != bandPointers.end() && it->second != nullptr) {
                    const float *src = it->second + chunkStart;
                    for (size_t i = 0; i < chunkLen; i++) {
                        float v = src[i];
                        if (!std::isfinite(v) || (checkNoData && v == noDataVal)) {
                            dst[i] = kNaN;
                        } else {
                            dst[i] = v;
                        }
                    }
                } else {
                    for (size_t i = 0; i < chunkLen; i++) {
                        dst[i] = kNaN;
                    }
                }
                break;
            }
            case OpCode::Add:
            case OpCode::Sub:
            case OpCode::Mul:
            case OpCode::Div:
            case OpCode::Mod:
            case OpCode::Pow:
            case OpCode::Min:
            case OpCode::Max:
            case OpCode::Atan2:
            case OpCode::CmpLt:
            case OpCode::CmpLe:
            case OpCode::CmpGt:
            case OpCode::CmpGe:
            case OpCode::CmpEq:
            case OpCode::CmpNe:
            case OpCode::LogicAnd:
            case OpCode::LogicOr: {
                for (size_t i = 0; i < chunkLen; i++) {
                    dst[i] = evalScalarOp2(inst.op, src1[i], src2[i]);
                }
                break;
            }
            case OpCode::Neg:
            case OpCode::Not:
            case OpCode::Abs:
            case OpCode::Sqrt:
            case OpCode::Cbrt:
            case OpCode::Exp:
            case OpCode::Log:
            case OpCode::Log10:
            case OpCode::Sin:
            case OpCode::Cos:
            case OpCode::Tan:
            case OpCode::Asin:
            case OpCode::Acos:
            case OpCode::Atan:
            case OpCode::Ceil:
            case OpCode::Floor:
            case OpCode::Round: {
                for (size_t i = 0; i < chunkLen; i++) {
                    dst[i] = evalScalarOp1(inst.op, src1[i]);
                }
                break;
            }
            case OpCode::Select:
            case OpCode::Clamp:
            case OpCode::Fma: {
                for (size_t i = 0; i < chunkLen; i++) {
                    dst[i] = evalScalarOp3(inst.op, src1[i], src2[i], src3[i]);
                }
                break;
            }
        }
    }

    const float *outRegPtr = getRegPtr(outputRegister);
    std::memcpy(outChunk, outRegPtr, chunkLen * sizeof(float));
}

void BandMathBytecode::evaluateSimd(
    const std::map<int, const float*> &bandPointers,
    float *outBuffer,
    size_t count,
    float noDataVal) const
{
    if (!outBuffer || count == 0) return;

    SimdArchitecture arch = Engine::detectOptimalArchitecture();

#if defined(__x86_64__) || defined(_M_X64)
    if (arch == SimdArchitecture::AVX2_FMA) {
        #pragma omp parallel for schedule(static) if (count > 4096)
        for (size_t chunkStart = 0; chunkStart < count; chunkStart += kChunkSize) {
            size_t chunkLen = std::min<size_t>(kChunkSize, count - chunkStart);
            evaluateChunkAvx2(m_instructions, m_numRegisters, m_outputRegister,
                              bandPointers, outBuffer + chunkStart, chunkStart, chunkLen, noDataVal);
        }
        return;
    }
#endif

    #pragma omp parallel for schedule(static) if (count > 4096)
    for (size_t chunkStart = 0; chunkStart < count; chunkStart += kChunkSize) {
        size_t chunkLen = std::min<size_t>(kChunkSize, count - chunkStart);
        evaluateChunkOpenMp(m_instructions, m_numRegisters, m_outputRegister,
                            bandPointers, outBuffer + chunkStart, chunkStart, chunkLen, noDataVal);
    }
}

void BandMathBytecode::evaluateSimd(
    const std::vector<const float*> &bandPointers,
    float *outBuffer,
    size_t count,
    float noDataVal) const
{
    std::map<int, const float*> bandMap;
    for (size_t b = 0; b < bandPointers.size(); b++) {
        bandMap[static_cast<int>(b + 1)] = bandPointers[b];
    }
    evaluateSimd(bandMap, outBuffer, count, noDataVal);
}

// --- Polymorphic Evaluator & Engine Implementation ---

class BytecodeEvaluatorImpl : public Evaluator
{
public:
    explicit BytecodeEvaluatorImpl(BandMathBytecode bytecode, SimdArchitecture arch)
        : m_bytecode(std::move(bytecode)), m_arch(arch) {}

    bool evaluate(const std::map<int, const float*> &bandBuffers, float *out, size_t count) const override
    {
        if (!out || count == 0) return false;
        m_bytecode.evaluateSimd(bandBuffers, out, count);
        return true;
    }

    SimdArchitecture architecture() const override { return m_arch; }

private:
    BandMathBytecode m_bytecode;
    SimdArchitecture m_arch;
};

std::unique_ptr<Evaluator> Engine::compile(
    const QString &expression,
    CompilationResult *compResult)
{
    QString err;
    int errCol = 0;
    std::vector<int> refs;
    auto bytecode = BandMathBytecode::compile(expression, err, errCol, &refs);
    if (!bytecode) {
        if (compResult) {
            compResult->ok = false;
            compResult->errorMessage = err;
            compResult->errorColumn = errCol;
            compResult->referencedBands.clear();
        }
        return nullptr;
    }

    if (compResult) {
        compResult->ok = true;
        compResult->errorMessage.clear();
        compResult->errorColumn = 0;
        compResult->referencedBands = refs;
    }

    return std::make_unique<BytecodeEvaluatorImpl>(std::move(*bytecode), detectOptimalArchitecture());
}

} // namespace BandMath
