// src/processing/algorithms/band_math_ast.h
#pragma once

#include <QString>
#include <QVector>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace BandMath
{

struct BandMathSourceLocation
{
    int line = 1;
    int column = 1;
};

struct BandMathError
{
    QString message;
    BandMathSourceLocation location;

    int column() const { return location.column; }

    QString formattedContext(const QString &expression) const
    {
        int col = std::max(1, location.column);
        QString pad(std::max(0, col - 1), ' ');
        return QString("%1 at line %2, col %3\n%4\n%5^")
            .arg(message)
            .arg(location.line)
            .arg(location.column)
            .arg(expression)
            .arg(pad);
    }
};

struct CompilationResult
{
    bool ok = false;
    QString errorMessage;
    int errorColumn = 0;
    std::vector<int> referencedBands;
};

/// Map of band number (1-based) -> pixel data array.
using BandData = std::map<int, std::vector<float>>;

// --- Bytecode OpCodes and Instructions ---

enum class OpCode : uint8_t
{
    LoadConst,       ///< dst = constVal
    LoadBand,        ///< dst = bandPtr[bandNum][offset..]
    Add,             ///< dst = src1 + src2
    Sub,             ///< dst = src1 - src2
    Mul,             ///< dst = src1 * src2
    Div,             ///< dst = safeDiv(src1, src2) (0 / unord -> NaN)
    Mod,             ///< dst = fmod(src1, src2)
    Pow,             ///< dst = pow(src1, src2)
    Neg,             ///< dst = -src1
    Not,             ///< dst = !src1 (1.0 if == 0, 0.0 if != 0, NaN if NaN)
    Abs,             ///< dst = abs(src1)
    Sqrt,            ///< dst = sqrt(src1) (NaN if < 0)
    Cbrt,            ///< dst = cbrt(src1)
    Exp,             ///< dst = exp(src1)
    Log,             ///< dst = log(src1) (NaN if <= 0)
    Log10,           ///< dst = log10(src1) (NaN if <= 0)
    Sin,             ///< dst = sin(src1)
    Cos,             ///< dst = cos(src1)
    Tan,             ///< dst = tan(src1)
    Asin,            ///< dst = asin(src1)
    Acos,            ///< dst = acos(src1)
    Atan,            ///< dst = atan(src1)
    Atan2,           ///< dst = atan2(src1, src2)
    Ceil,            ///< dst = ceil(src1)
    Floor,           ///< dst = floor(src1)
    Round,           ///< dst = round(src1)
    Min,             ///< dst = min(src1, src2) (symmetric NaN propagation)
    Max,             ///< dst = max(src1, src2) (symmetric NaN propagation)
    Clamp,           ///< dst = clamp(src1, src2, src3) (min(max(x, lo), hi))
    CmpLt,           ///< dst = (src1 < src2) ? 1.0 : 0.0 (NaN -> NaN)
    CmpLe,           ///< dst = (src1 <= src2) ? 1.0 : 0.0 (NaN -> NaN)
    CmpGt,           ///< dst = (src1 > src2) ? 1.0 : 0.0 (NaN -> NaN)
    CmpGe,           ///< dst = (src1 >= src2) ? 1.0 : 0.0 (NaN -> NaN)
    CmpEq,           ///< dst = (src1 == src2) ? 1.0 : 0.0 (NaN -> NaN)
    CmpNe,           ///< dst = (src1 != src2) ? 1.0 : 0.0 (NaN -> NaN)
    LogicAnd,        ///< dst = (src1 != 0 && src2 != 0) ? 1.0 : 0.0 (NaN -> NaN)
    LogicOr,         ///< dst = (src1 != 0 || src2 != 0) ? 1.0 : 0.0 (NaN -> NaN)
    Select,          ///< dst = (cond != 0) ? srcTrue : srcFalse (NaN cond -> NaN)
    Fma              ///< dst = src1 * src2 + src3
};

struct BytecodeInstruction
{
    OpCode op = OpCode::LoadConst;
    uint16_t dst = 0;
    uint16_t src1 = 0;
    uint16_t src2 = 0;
    uint16_t src3 = 0;
    float constVal = 0.0f;
    int bandNum = 0; // 1-based
};

class BytecodeEmitter
{
public:
    uint16_t allocReg()
    {
        uint16_t r = m_nextReg++;
        m_maxRegs = std::max(m_maxRegs, m_nextReg);
        return r;
    }

    void freeReg()
    {
        if (m_nextReg > 0) m_nextReg--;
    }

    uint16_t emitInstruction(const BytecodeInstruction &inst)
    {
        m_instructions.push_back(inst);
        return inst.dst;
    }

    uint16_t emitLoadConst(float val)
    {
        uint16_t r = allocReg();
        BytecodeInstruction inst;
        inst.op = OpCode::LoadConst;
        inst.dst = r;
        inst.constVal = val;
        emitInstruction(inst);
        return r;
    }

    uint16_t emitLoadBand(int bandNum)
    {
        uint16_t r = allocReg();
        BytecodeInstruction inst;
        inst.op = OpCode::LoadBand;
        inst.dst = r;
        inst.bandNum = bandNum;
        emitInstruction(inst);
        return r;
    }

    uint16_t maxRegs() const { return m_maxRegs; }
    const std::vector<BytecodeInstruction> &instructions() const { return m_instructions; }

private:
    std::vector<BytecodeInstruction> m_instructions;
    uint16_t m_nextReg = 0;
    uint16_t m_maxRegs = 0;
};

// --- AST Node Hierarchy ---

struct AstNode
{
    virtual ~AstNode() = default;
    virtual float eval(const BandData &bands, size_t pixel) const = 0;
    virtual void collectRefs(std::vector<int> &refs) const {}
    virtual void resolve(const BandData &bands) {}
    virtual uint16_t emitBytecode(BytecodeEmitter &emitter) const = 0;
    virtual bool isConstant() const { return false; }
    virtual float constantValue() const { return 0.0f; }
};

struct ConstantNode : AstNode
{
    float value;
    explicit ConstantNode(float v) : value(v) {}

    float eval(const BandData &, size_t) const override { return value; }
    bool isConstant() const override { return true; }
    float constantValue() const override { return value; }

    uint16_t emitBytecode(BytecodeEmitter &emitter) const override
    {
        return emitter.emitLoadConst(value);
    }
};

struct BandRefNode : AstNode
{
    int bandNum;
    const float *resolvedPtr = nullptr;
    size_t resolvedSize = 0;

    explicit BandRefNode(int n) : bandNum(n) {}

    float eval(const BandData &bands, size_t pixel) const override
    {
        if (resolvedPtr) {
            return (pixel < resolvedSize) ? resolvedPtr[pixel] : std::numeric_limits<float>::quiet_NaN();
        }
        auto it = bands.find(bandNum);
        if (it == bands.end() || pixel >= it->second.size())
            return std::numeric_limits<float>::quiet_NaN();
        return it->second[pixel];
    }

    void collectRefs(std::vector<int> &refs) const override
    {
        refs.push_back(bandNum);
    }

    void resolve(const BandData &bands) override
    {
        auto it = bands.find(bandNum);
        if (it != bands.end()) {
            resolvedPtr = it->second.data();
            resolvedSize = it->second.size();
        }
    }

    uint16_t emitBytecode(BytecodeEmitter &emitter) const override
    {
        return emitter.emitLoadBand(bandNum);
    }
};

struct UnaryOpNode : AstNode
{
    enum class Type { Neg, Not };
    Type type;
    std::unique_ptr<AstNode> child;

    UnaryOpNode(Type t, std::unique_ptr<AstNode> c)
        : type(t), child(std::move(c)) {}

    float eval(const BandData &bands, size_t pixel) const override
    {
        float val = child->eval(bands, pixel);
        if (!std::isfinite(val)) return std::numeric_limits<float>::quiet_NaN();
        if (type == Type::Neg) return -val;
        if (type == Type::Not) return (val == 0.0f) ? 1.0f : 0.0f;
        return std::numeric_limits<float>::quiet_NaN();
    }

    void collectRefs(std::vector<int> &refs) const override
    {
        child->collectRefs(refs);
    }

    void resolve(const BandData &bands) override
    {
        child->resolve(bands);
    }

    uint16_t emitBytecode(BytecodeEmitter &emitter) const override
    {
        uint16_t r = child->emitBytecode(emitter);
        BytecodeInstruction inst;
        inst.op = (type == Type::Neg) ? OpCode::Neg : OpCode::Not;
        inst.dst = r;
        inst.src1 = r;
        emitter.emitInstruction(inst);
        return r;
    }
};

struct BinaryOpNode : AstNode
{
    char op; // '+', '-', '*', '/', '%', '^'
    std::unique_ptr<AstNode> left, right;

    BinaryOpNode(char o, std::unique_ptr<AstNode> l, std::unique_ptr<AstNode> r)
        : op(o), left(std::move(l)), right(std::move(r)) {}

    float eval(const BandData &bands, size_t pixel) const override
    {
        float l = left->eval(bands, pixel);
        float r = right->eval(bands, pixel);
        if (!std::isfinite(l) || !std::isfinite(r))
            return std::numeric_limits<float>::quiet_NaN();

        switch (op) {
            case '+': return l + r;
            case '-': return l - r;
            case '*': return l * r;
            case '/':
                if (r == 0.0f) return std::numeric_limits<float>::quiet_NaN();
                return l / r;
            case '%':
                if (r == 0.0f) return std::numeric_limits<float>::quiet_NaN();
                return std::fmod(l, r);
            case '^': return std::pow(l, r);
            default: return std::numeric_limits<float>::quiet_NaN();
        }
    }

    void collectRefs(std::vector<int> &refs) const override
    {
        left->collectRefs(refs);
        right->collectRefs(refs);
    }

    void resolve(const BandData &bands) override
    {
        left->resolve(bands);
        right->resolve(bands);
    }

    uint16_t emitBytecode(BytecodeEmitter &emitter) const override
    {
        uint16_t r1 = left->emitBytecode(emitter);
        uint16_t r2 = right->emitBytecode(emitter);
        emitter.freeReg(); // free r2

        BytecodeInstruction inst;
        inst.dst = r1;
        inst.src1 = r1;
        inst.src2 = r2;
        switch (op) {
            case '+': inst.op = OpCode::Add; break;
            case '-': inst.op = OpCode::Sub; break;
            case '*': inst.op = OpCode::Mul; break;
            case '/': inst.op = OpCode::Div; break;
            case '%': inst.op = OpCode::Mod; break;
            case '^': inst.op = OpCode::Pow; break;
            default: inst.op = OpCode::Add; break;
        }
        emitter.emitInstruction(inst);
        return r1;
    }
};

struct ComparisonNode : AstNode
{
    std::string op; // "<", ">", "<=", ">=", "==", "!="
    std::unique_ptr<AstNode> left, right;

    ComparisonNode(std::string o, std::unique_ptr<AstNode> l, std::unique_ptr<AstNode> r)
        : op(std::move(o)), left(std::move(l)), right(std::move(r)) {}

    float eval(const BandData &bands, size_t pixel) const override
    {
        float l = left->eval(bands, pixel);
        float r = right->eval(bands, pixel);
        if (!std::isfinite(l) || !std::isfinite(r))
            return std::numeric_limits<float>::quiet_NaN();
        bool result = false;
        if      (op == "<")  result = l < r;
        else if (op == ">")  result = l > r;
        else if (op == "<=") result = l <= r;
        else if (op == ">=") result = l >= r;
        else if (op == "==") result = l == r;
        else if (op == "!=") result = l != r;
        return result ? 1.0f : 0.0f;
    }

    void collectRefs(std::vector<int> &refs) const override
    {
        left->collectRefs(refs);
        right->collectRefs(refs);
    }

    void resolve(const BandData &bands) override
    {
        left->resolve(bands);
        right->resolve(bands);
    }

    uint16_t emitBytecode(BytecodeEmitter &emitter) const override
    {
        uint16_t r1 = left->emitBytecode(emitter);
        uint16_t r2 = right->emitBytecode(emitter);
        emitter.freeReg();

        BytecodeInstruction inst;
        inst.dst = r1;
        inst.src1 = r1;
        inst.src2 = r2;
        if      (op == "<")  inst.op = OpCode::CmpLt;
        else if (op == "<=") inst.op = OpCode::CmpLe;
        else if (op == ">")  inst.op = OpCode::CmpGt;
        else if (op == ">=") inst.op = OpCode::CmpGe;
        else if (op == "==") inst.op = OpCode::CmpEq;
        else if (op == "!=") inst.op = OpCode::CmpNe;
        emitter.emitInstruction(inst);
        return r1;
    }
};

struct LogicalNode : AstNode
{
    bool isAnd;
    std::unique_ptr<AstNode> left, right;

    LogicalNode(bool andOp, std::unique_ptr<AstNode> l, std::unique_ptr<AstNode> r)
        : isAnd(andOp), left(std::move(l)), right(std::move(r)) {}

    float eval(const BandData &bands, size_t pixel) const override
    {
        float l = left->eval(bands, pixel);
        if (!std::isfinite(l)) return std::numeric_limits<float>::quiet_NaN();
        float r = right->eval(bands, pixel);
        if (!std::isfinite(r)) return std::numeric_limits<float>::quiet_NaN();
        bool result = isAnd ? (l != 0.0f && r != 0.0f) : (l != 0.0f || r != 0.0f);
        return result ? 1.0f : 0.0f;
    }

    void collectRefs(std::vector<int> &refs) const override
    {
        left->collectRefs(refs);
        right->collectRefs(refs);
    }

    void resolve(const BandData &bands) override
    {
        left->resolve(bands);
        right->resolve(bands);
    }

    uint16_t emitBytecode(BytecodeEmitter &emitter) const override
    {
        uint16_t r1 = left->emitBytecode(emitter);
        uint16_t r2 = right->emitBytecode(emitter);
        emitter.freeReg();

        BytecodeInstruction inst;
        inst.dst = r1;
        inst.src1 = r1;
        inst.src2 = r2;
        inst.op = isAnd ? OpCode::LogicAnd : OpCode::LogicOr;
        emitter.emitInstruction(inst);
        return r1;
    }
};

struct ConditionalNode : AstNode
{
    std::unique_ptr<AstNode> condition, trueExpr, falseExpr;

    ConditionalNode(std::unique_ptr<AstNode> c, std::unique_ptr<AstNode> t, std::unique_ptr<AstNode> f)
        : condition(std::move(c)), trueExpr(std::move(t)), falseExpr(std::move(f)) {}

    float eval(const BandData &bands, size_t pixel) const override
    {
        float cond = condition->eval(bands, pixel);
        if (!std::isfinite(cond)) return std::numeric_limits<float>::quiet_NaN();
        return (cond != 0.0f) ? trueExpr->eval(bands, pixel) : falseExpr->eval(bands, pixel);
    }

    void collectRefs(std::vector<int> &refs) const override
    {
        condition->collectRefs(refs);
        trueExpr->collectRefs(refs);
        falseExpr->collectRefs(refs);
    }

    void resolve(const BandData &bands) override
    {
        condition->resolve(bands);
        trueExpr->resolve(bands);
        falseExpr->resolve(bands);
    }

    uint16_t emitBytecode(BytecodeEmitter &emitter) const override
    {
        uint16_t rCond = condition->emitBytecode(emitter);
        uint16_t rTrue = trueExpr->emitBytecode(emitter);
        uint16_t rFalse = falseExpr->emitBytecode(emitter);
        emitter.freeReg(); // free rFalse
        emitter.freeReg(); // free rTrue

        uint16_t dst = rCond;
        BytecodeInstruction inst;
        inst.op = OpCode::Select;
        inst.dst = dst;
        inst.src1 = rCond;
        inst.src2 = rTrue;
        inst.src3 = rFalse;
        emitter.emitInstruction(inst);
        return dst;
    }
};

struct FunctionCallNode : AstNode
{
    std::string name;
    std::vector<std::unique_ptr<AstNode>> args;

    FunctionCallNode(std::string n, std::vector<std::unique_ptr<AstNode>> a)
        : name(std::move(n)), args(std::move(a)) {}

    float eval(const BandData &bands, size_t pixel) const override
    {
        std::string clean = name;
        if (clean.rfind("std::", 0) == 0) clean = clean.substr(5);

        if (args.empty()) {
            if (clean == "pi") return static_cast<float>(M_PI);
            return std::numeric_limits<float>::quiet_NaN();
        }

        if (args.size() == 1) {
            float a = args[0]->eval(bands, pixel);
            if (!std::isfinite(a)) return std::numeric_limits<float>::quiet_NaN();
            if (clean == "sin")   return std::sin(a);
            if (clean == "cos")   return std::cos(a);
            if (clean == "tan")   return std::tan(a);
            if (clean == "asin")  return std::asin(a);
            if (clean == "acos")  return std::acos(a);
            if (clean == "atan")  return std::atan(a);
            if (clean == "sqrt")  return (a < 0.0f) ? std::numeric_limits<float>::quiet_NaN() : std::sqrt(a);
            if (clean == "cbrt")  return std::cbrt(a);
            if (clean == "exp")   return std::exp(a);
            if (clean == "ln" || clean == "log") return (a <= 0.0f) ? std::numeric_limits<float>::quiet_NaN() : std::log(a);
            if (clean == "log10") return (a <= 0.0f) ? std::numeric_limits<float>::quiet_NaN() : std::log10(a);
            if (clean == "abs")   return std::fabs(a);
            if (clean == "ceil")  return std::ceil(a);
            if (clean == "floor") return std::floor(a);
            if (clean == "round") return std::round(a);
            return std::numeric_limits<float>::quiet_NaN();
        }

        if (args.size() == 2) {
            float a = args[0]->eval(bands, pixel);
            float b = args[1]->eval(bands, pixel);
            if (!std::isfinite(a) || !std::isfinite(b))
                return std::numeric_limits<float>::quiet_NaN();
            if (clean == "pow")   return std::pow(a, b);
            if (clean == "min")   return std::min(a, b);
            if (clean == "max")   return std::max(a, b);
            if (clean == "atan2") return std::atan2(a, b);
            return std::numeric_limits<float>::quiet_NaN();
        }

        if (args.size() == 3) {
            float a = args[0]->eval(bands, pixel);
            float b = args[1]->eval(bands, pixel);
            float c = args[2]->eval(bands, pixel);
            if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c))
                return std::numeric_limits<float>::quiet_NaN();
            if (clean == "clamp") return std::min(std::max(a, b), c);
            if (clean == "if")    return (a != 0.0f) ? b : c;
            return std::numeric_limits<float>::quiet_NaN();
        }

        return std::numeric_limits<float>::quiet_NaN();
    }

    void collectRefs(std::vector<int> &refs) const override
    {
        for (const auto &arg : args)
            arg->collectRefs(refs);
    }

    void resolve(const BandData &bands) override
    {
        for (auto &arg : args)
            arg->resolve(bands);
    }

    uint16_t emitBytecode(BytecodeEmitter &emitter) const override
    {
        std::string clean = name;
        if (clean.rfind("std::", 0) == 0) clean = clean.substr(5);

        if (args.empty()) {
            if (clean == "pi") {
                return emitter.emitLoadConst(static_cast<float>(M_PI));
            }
            return emitter.emitLoadConst(std::numeric_limits<float>::quiet_NaN());
        }

        if (args.size() == 1) {
            uint16_t r = args[0]->emitBytecode(emitter);
            BytecodeInstruction inst;
            inst.dst = r;
            inst.src1 = r;
            if      (clean == "sin")   inst.op = OpCode::Sin;
            else if (clean == "cos")   inst.op = OpCode::Cos;
            else if (clean == "tan")   inst.op = OpCode::Tan;
            else if (clean == "asin")  inst.op = OpCode::Asin;
            else if (clean == "acos")  inst.op = OpCode::Acos;
            else if (clean == "atan")  inst.op = OpCode::Atan;
            else if (clean == "sqrt")  inst.op = OpCode::Sqrt;
            else if (clean == "cbrt")  inst.op = OpCode::Cbrt;
            else if (clean == "exp")   inst.op = OpCode::Exp;
            else if (clean == "ln" || clean == "log") inst.op = OpCode::Log;
            else if (clean == "log10") inst.op = OpCode::Log10;
            else if (clean == "abs")   inst.op = OpCode::Abs;
            else if (clean == "ceil")  inst.op = OpCode::Ceil;
            else if (clean == "floor") inst.op = OpCode::Floor;
            else if (clean == "round") inst.op = OpCode::Round;
            else return r;
            emitter.emitInstruction(inst);
            return r;
        }

        if (args.size() == 2) {
            uint16_t r1 = args[0]->emitBytecode(emitter);
            uint16_t r2 = args[1]->emitBytecode(emitter);
            emitter.freeReg();

            BytecodeInstruction inst;
            inst.dst = r1;
            inst.src1 = r1;
            inst.src2 = r2;
            if      (clean == "pow")   inst.op = OpCode::Pow;
            else if (clean == "min")   inst.op = OpCode::Min;
            else if (clean == "max")   inst.op = OpCode::Max;
            else if (clean == "atan2") inst.op = OpCode::Atan2;
            else inst.op = OpCode::Min;
            emitter.emitInstruction(inst);
            return r1;
        }

        if (args.size() == 3) {
            uint16_t r1 = args[0]->emitBytecode(emitter);
            uint16_t r2 = args[1]->emitBytecode(emitter);
            uint16_t r3 = args[2]->emitBytecode(emitter);
            emitter.freeReg();
            emitter.freeReg();

            BytecodeInstruction inst;
            inst.dst = r1;
            inst.src1 = r1;
            inst.src2 = r2;
            inst.src3 = r3;
            if (clean == "clamp") {
                inst.op = OpCode::Clamp;
            } else if (clean == "if") {
                inst.op = OpCode::Select;
            }
            emitter.emitInstruction(inst);
            return r1;
        }

        return emitter.emitLoadConst(std::numeric_limits<float>::quiet_NaN());
    }
};

// --- Lexer & Parser with Exact Column Tracking ---

enum class TokenType
{
    Number,
    BandRef,
    Identifier,
    Plus,
    Minus,
    Mul,
    Div,
    Mod,
    Pow,
    Not,
    Question,
    Colon,
    LogicAnd,
    LogicOr,
    CmpLt,
    CmpLe,
    CmpGt,
    CmpGe,
    CmpEq,
    CmpNe,
    LParen,
    RParen,
    Comma,
    EndOfFile,
    Invalid
};

struct Token
{
    TokenType type = TokenType::Invalid;
    std::string text;
    float numberValue = 0.0f;
    int bandIndex = 0;
    int line = 1;
    int column = 1;
};

class Lexer
{
public:
    explicit Lexer(const QString &source)
        : m_src(source.toStdString()), m_pos(0), m_line(1), m_col(1) {}

    Token nextToken()
    {
        skipWhitespace();
        if (m_pos >= m_src.size()) {
            return Token{TokenType::EndOfFile, "", 0.0f, 0, m_line, m_col};
        }

        int startCol = m_col;
        int startLine = m_line;
        char c = m_src[m_pos];

        // Two-character operators
        if (m_pos + 1 < m_src.size()) {
            char next = m_src[m_pos + 1];
            if (c == '&' && next == '&') { advance(2); return Token{TokenType::LogicAnd, "&&", 0.0f, 0, startLine, startCol}; }
            if (c == '|' && next == '|') { advance(2); return Token{TokenType::LogicOr, "||", 0.0f, 0, startLine, startCol}; }
            if (c == '<' && next == '=') { advance(2); return Token{TokenType::CmpLe, "<=", 0.0f, 0, startLine, startCol}; }
            if (c == '>' && next == '=') { advance(2); return Token{TokenType::CmpGe, ">=", 0.0f, 0, startLine, startCol}; }
            if (c == '=' && next == '=') { advance(2); return Token{TokenType::CmpEq, "==", 0.0f, 0, startLine, startCol}; }
            if (c == '!' && next == '=') { advance(2); return Token{TokenType::CmpNe, "!=", 0.0f, 0, startLine, startCol}; }
        }

        // Single-character operators & delimiters
        switch (c) {
            case '+': advance(); return Token{TokenType::Plus, "+", 0.0f, 0, startLine, startCol};
            case '-': advance(); return Token{TokenType::Minus, "-", 0.0f, 0, startLine, startCol};
            case '*': advance(); return Token{TokenType::Mul, "*", 0.0f, 0, startLine, startCol};
            case '/': advance(); return Token{TokenType::Div, "/", 0.0f, 0, startLine, startCol};
            case '%': advance(); return Token{TokenType::Mod, "%", 0.0f, 0, startLine, startCol};
            case '^': advance(); return Token{TokenType::Pow, "^", 0.0f, 0, startLine, startCol};
            case '!': advance(); return Token{TokenType::Not, "!", 0.0f, 0, startLine, startCol};
            case '?': advance(); return Token{TokenType::Question, "?", 0.0f, 0, startLine, startCol};
            case ':': advance(); return Token{TokenType::Colon, ":", 0.0f, 0, startLine, startCol};
            case '<': advance(); return Token{TokenType::CmpLt, "<", 0.0f, 0, startLine, startCol};
            case '>': advance(); return Token{TokenType::CmpGt, ">", 0.0f, 0, startLine, startCol};
            case '(': advance(); return Token{TokenType::LParen, "(", 0.0f, 0, startLine, startCol};
            case ')': advance(); return Token{TokenType::RParen, ")", 0.0f, 0, startLine, startCol};
            case ',': advance(); return Token{TokenType::Comma, ",", 0.0f, 0, startLine, startCol};
            default: break;
        }

        // Band reference: bN / BN
        if ((c == 'b' || c == 'B') && m_pos + 1 < m_src.size() && std::isdigit(m_src[m_pos + 1])) {
            size_t start = m_pos++;
            m_col++;
            // Accumulate in 64-bit with a digit cap (#613): int accumulation
            // of a huge literal is signed-overflow UB before any validation.
            long long band = 0;
            int digits = 0;
            bool overflow = false;
            while (m_pos < m_src.size() && std::isdigit(m_src[m_pos])) {
                if (++digits > 9 || band > ( std::numeric_limits<int>::max() - ( m_src[m_pos] - '0' ) ) / 10 )
                    overflow = true;
                else
                    band = band * 10 + ( m_src[m_pos] - '0' );
                m_pos++;
                m_col++;
            }
            if ( overflow ) {
                // Band index out of representable range: reject as invalid
                // token instead of wrapping (#613).
                std::string text2 = m_src.substr(start, m_pos - start);
                return Token{TokenType::Invalid, text2, 0.0f, std::numeric_limits<int>::max(), startLine, startCol};
            }
            std::string text = m_src.substr(start, m_pos - start);
            return Token{TokenType::BandRef, text, 0.0f, static_cast<int>(band), startLine, startCol};
        }

        // Number literal (supports integers, decimals, scientific notation 1e-4)
        if (std::isdigit(c) || (c == '.' && m_pos + 1 < m_src.size() && std::isdigit(m_src[m_pos + 1]))) {
            size_t start = m_pos;
            bool hasDot = false;
            bool hasExp = false;
            while (m_pos < m_src.size()) {
                char ch = m_src[m_pos];
                if (std::isdigit(ch)) { advance(); continue; }
                if (ch == '.' && !hasDot && !hasExp) { hasDot = true; advance(); continue; }
                if ((ch == 'e' || ch == 'E') && !hasExp && m_pos > start) {
                    hasExp = true;
                    advance();
                    if (m_pos < m_src.size() && (m_src[m_pos] == '+' || m_src[m_pos] == '-')) {
                        advance();
                    }
                    continue;
                }
                break;
            }
            std::string text = m_src.substr(start, m_pos - start);
            float val = 0.0f;
            try {
                size_t consumed = 0;
                val = std::stof(text, &consumed);
                if ( consumed != text.size() )
                    throw std::invalid_argument("trailing");
            } catch (...) {
                // Malformed literal like "1e" or "1e+" (#613): reject the
                // token instead of silently producing an all-NaN raster.
                return Token{TokenType::Invalid, text, 0.0f, 0, startLine, startCol};
            }
            return Token{TokenType::Number, text, val, 0, startLine, startCol};
        }

        // Identifier (function name / macro name / namespace)
        if (std::isalpha(c) || c == '_') {
            size_t start = m_pos;
            while (m_pos < m_src.size() && (std::isalnum(m_src[m_pos]) || m_src[m_pos] == '_' || m_src[m_pos] == ':')) {
                advance();
            }
            std::string text = m_src.substr(start, m_pos - start);
            return Token{TokenType::Identifier, text, 0.0f, 0, startLine, startCol};
        }

        // Unrecognized character
        advance();
        return Token{TokenType::Invalid, std::string(1, c), 0.0f, 0, startLine, startCol};
    }

private:
    std::string m_src;
    size_t m_pos;
    int m_line;
    int m_col;

    void advance(size_t n = 1)
    {
        for (size_t i = 0; i < n && m_pos < m_src.size(); i++) {
            if (m_src[m_pos] == '\n') {
                m_line++;
                m_col = 1;
            } else {
                m_col++;
            }
            m_pos++;
        }
    }

    void skipWhitespace()
    {
        while (m_pos < m_src.size() && std::isspace(m_src[m_pos])) {
            advance();
        }
    }
};

class Parser
{
public:
    explicit Parser(const QString &expr)
        : m_expr(expr), m_lexer(expr)
    {
        m_current = m_lexer.nextToken();
    }

    std::unique_ptr<AstNode> parse()
    {
        if (m_current.type == TokenType::EndOfFile) {
            setError("Empty expression", 1, 1);
            return nullptr;
        }

        // Catch malformed leading operators like "+ b1"
        if (m_current.type == TokenType::Plus) {
            setError("Unexpected unary '+' operator", m_current.line, m_current.column);
            return nullptr;
        }

        auto root = parseExpression();
        if (!root) return nullptr;

        if (m_current.type != TokenType::EndOfFile) {
            setError(QString("Unexpected token '%1'").arg(QString::fromStdString(m_current.text)),
                     m_current.line, m_current.column);
            return nullptr;
        }
        return root;
    }

    bool hasError() const { return m_hasError; }
    const BandMathError &error() const { return m_error; }

private:
    QString m_expr;
    Lexer m_lexer;
    Token m_current;
    // Recursion depth guard (#613): parseUnary/parsePrimary/parseExpression
    // are mutually recursive with no bound; ~100k '(' or '!' characters in an
    // agent/user-supplied expression would overflow the stack (process crash).
    int m_depth = 0;
    static constexpr int kMaxDepth = 250;
    bool m_hasError = false;
    BandMathError m_error;

    void setError(const QString &msg, int line, int col)
    {
        if (!m_hasError) {
            m_hasError = true;
            m_error = BandMathError{msg, BandMathSourceLocation{line, col}};
        }
    }

    void advance()
    {
        m_current = m_lexer.nextToken();
    }

    bool match(TokenType type)
    {
        if (m_current.type == type) {
            advance();
            return true;
        }
        return false;
    }

    // Expression -> Ternary
    std::unique_ptr<AstNode> parseExpression()
    {
        return parseTernary();
    }

    // Ternary -> LogicOr ('?' Expression ':' Ternary)?
    std::unique_ptr<AstNode> parseTernary()
    {
        if (++m_depth > kMaxDepth) {
            setError("Expression nesting too deep (limit 250)", m_current.line, m_current.column);
            return nullptr;
        }
        struct DepthGuard
        {
            int &d;
            ~DepthGuard() { --d; }
        } depthGuard{ m_depth };

        auto cond = parseLogicOr();
        if (!cond) return nullptr;

        if (match(TokenType::Question)) {
            int qCol = m_current.column;
            auto trueBranch = parseExpression();
            if (!trueBranch) {
                if (!m_hasError) setError("Expected true expression after '?'", m_current.line, qCol);
                return nullptr;
            }
            if (!match(TokenType::Colon)) {
                setError("Expected ':' in ternary conditional", m_current.line, m_current.column);
                return nullptr;
            }
            auto falseBranch = parseTernary();
            if (!falseBranch) {
                if (!m_hasError) setError("Expected false expression after ':'", m_current.line, m_current.column);
                return nullptr;
            }
            return std::make_unique<ConditionalNode>(std::move(cond), std::move(trueBranch), std::move(falseBranch));
        }
        return cond;
    }

    // LogicOr -> LogicAnd ('||' LogicAnd)*
    std::unique_ptr<AstNode> parseLogicOr()
    {
        auto node = parseLogicAnd();
        if (!node) return nullptr;

        while (m_current.type == TokenType::LogicOr) {
            advance();
            auto right = parseLogicAnd();
            if (!right) {
                if (!m_hasError) setError("Expected operand after '||'", m_current.line, m_current.column);
                return nullptr;
            }
            node = std::make_unique<LogicalNode>(false, std::move(node), std::move(right));
        }
        return node;
    }

    // LogicAnd -> Comparison ('&&' Comparison)*
    std::unique_ptr<AstNode> parseLogicAnd()
    {
        auto node = parseComparison();
        if (!node) return nullptr;

        while (m_current.type == TokenType::LogicAnd) {
            advance();
            auto right = parseComparison();
            if (!right) {
                if (!m_hasError) setError("Expected operand after '&&'", m_current.line, m_current.column);
                return nullptr;
            }
            node = std::make_unique<LogicalNode>(true, std::move(node), std::move(right));
        }
        return node;
    }

    // Comparison -> Additive (('<' | '<=' | '>' | '>=' | '==' | '!=') Additive)?
    std::unique_ptr<AstNode> parseComparison()
    {
        auto node = parseAdditive();
        if (!node) return nullptr;

        std::string op;
        if      (m_current.type == TokenType::CmpLt) op = "<";
        else if (m_current.type == TokenType::CmpLe) op = "<=";
        else if (m_current.type == TokenType::CmpGt) op = ">";
        else if (m_current.type == TokenType::CmpGe) op = ">=";
        else if (m_current.type == TokenType::CmpEq) op = "==";
        else if (m_current.type == TokenType::CmpNe) op = "!=";

        if (!op.empty()) {
            advance();
            auto right = parseAdditive();
            if (!right) {
                if (!m_hasError) setError(QString("Expected operand after '%1'").arg(QString::fromStdString(op)),
                                         m_current.line, m_current.column);
                return nullptr;
            }
            return std::make_unique<ComparisonNode>(op, std::move(node), std::move(right));
        }
        return node;
    }

    // Additive -> Multiplicative (('+' | '-') Multiplicative)*
    std::unique_ptr<AstNode> parseAdditive()
    {
        auto node = parseMultiplicative();
        if (!node) return nullptr;

        while (m_current.type == TokenType::Plus || m_current.type == TokenType::Minus) {
            char op = (m_current.type == TokenType::Plus) ? '+' : '-';
            advance();
            auto right = parseMultiplicative();
            if (!right) {
                if (!m_hasError) setError(QString("Expected operand after '%1'").arg(op), m_current.line, m_current.column);
                return nullptr;
            }
            node = std::make_unique<BinaryOpNode>(op, std::move(node), std::move(right));
        }
        return node;
    }

    // Multiplicative -> Unary (('*' | '/' | '%') Unary)*
    std::unique_ptr<AstNode> parseMultiplicative()
    {
        auto node = parseUnary();
        if (!node) return nullptr;

        while (m_current.type == TokenType::Mul || m_current.type == TokenType::Div || m_current.type == TokenType::Mod) {
            char op = '*';
            if (m_current.type == TokenType::Div) op = '/';
            else if (m_current.type == TokenType::Mod) op = '%';
            advance();
            auto right = parseUnary();
            if (!right) {
                if (!m_hasError) setError(QString("Expected operand after '%1'").arg(op), m_current.line, m_current.column);
                return nullptr;
            }
            node = std::make_unique<BinaryOpNode>(op, std::move(node), std::move(right));
        }
        return node;
    }

    // Exponential -> Primary ('^' Unary)?  (right-associative; '^' binds
    // tighter than unary minus so -b2^2 parses as -(b2^2), the standard math
    // convention (#700), while 2^-3 remains a valid negative exponent)
    std::unique_ptr<AstNode> parseExponential()
    {
        auto node = parsePrimary();
        if (!node) return nullptr;

        if (m_current.type == TokenType::Pow) {
            advance();
            auto right = parseUnary();
            if (!right) {
                if (!m_hasError) setError("Expected operand after '^'", m_current.line, m_current.column);
                return nullptr;
            }
            return std::make_unique<BinaryOpNode>('^', std::move(node), std::move(right));
        }
        return node;
    }

    // Unary -> ('-' | '!') Unary | Exponential
    std::unique_ptr<AstNode> parseUnary()
    {
        if (++m_depth > kMaxDepth) {
            setError("Expression nesting too deep (limit 250)", m_current.line, m_current.column);
            return nullptr;
        }
        struct DepthGuard
        {
            int &d;
            ~DepthGuard() { --d; }
        } depthGuard{ m_depth };

        if (m_current.type == TokenType::Minus) {
            advance();
            auto child = parseUnary();
            if (!child) {
                if (!m_hasError) setError("Expected operand after '-'", m_current.line, m_current.column);
                return nullptr;
            }
            return std::make_unique<UnaryOpNode>(UnaryOpNode::Type::Neg, std::move(child));
        }
        if (m_current.type == TokenType::Not) {
            advance();
            auto child = parseUnary();
            if (!child) {
                if (!m_hasError) setError("Expected operand after '!'", m_current.line, m_current.column);
                return nullptr;
            }
            return std::make_unique<UnaryOpNode>(UnaryOpNode::Type::Not, std::move(child));
        }
        return parseExponential();
    }

    // Primary -> NUMBER | BAND_REF | '(' Expression ')' | FunctionCall
    std::unique_ptr<AstNode> parsePrimary()
    {
        Token tok = m_current;

        if (tok.type == TokenType::Number) {
            advance();
            return std::make_unique<ConstantNode>(tok.numberValue);
        }

        if (tok.type == TokenType::BandRef) {
            advance();
            if (tok.bandIndex < 1) {
                setError(QString("Invalid band index b%1 (must be >= 1)").arg(tok.bandIndex), tok.line, tok.column);
                return nullptr;
            }
            return std::make_unique<BandRefNode>(tok.bandIndex);
        }

        if (tok.type == TokenType::LParen) {
            int openCol = tok.column;
            advance();
            auto expr = parseExpression();
            if (!expr) return nullptr;
            if (!match(TokenType::RParen)) {
                setError("Unmatched opening parenthesis '('", tok.line, openCol);
                return nullptr;
            }
            return expr;
        }

        if (tok.type == TokenType::Identifier) {
            return parseFunctionCall();
        }

        setError(QString("Unexpected token '%1'").arg(QString::fromStdString(tok.text.empty() ? "EOF" : tok.text)),
                 tok.line, tok.column);
        return nullptr;
    }

    static int expectedFunctionArgs(const std::string &fnName)
    {
        static const std::unordered_map<std::string, int> kTable = {
            {"pi", 0},
            {"sin", 1}, {"cos", 1}, {"tan", 1},
            {"asin", 1}, {"acos", 1}, {"atan", 1},
            {"sqrt", 1}, {"cbrt", 1}, {"exp", 1},
            {"ln", 1}, {"log", 1}, {"log10", 1},
            {"abs", 1}, {"ceil", 1}, {"floor", 1}, {"round", 1},
            {"pow", 2}, {"min", 2}, {"max", 2},
            {"std::min", 2}, {"std::max", 2}, {"atan2", 2},
            {"clamp", 3}, {"if", 3},
            {"ndvi", 2}, {"ndwi", 2}, {"mndwi", 2},
            {"nbr", 2}, {"savi", 3}, {"evi", 3}, {"bsi", 4}
        };
        auto it = kTable.find(fnName);
        if (it != kTable.end()) return it->second;
        if (fnName.rfind("std::", 0) == 0) {
            auto it2 = kTable.find(fnName.substr(5));
            if (it2 != kTable.end()) return it2->second;
        }
        return -1;
    }

    std::unique_ptr<AstNode> parseFunctionCall()
    {
        Token nameTok = m_current;
        std::string name = nameTok.text;
        advance();

        if (!match(TokenType::LParen)) {
            setError(QString("Expected '(' after function name '%1'").arg(QString::fromStdString(name)),
                     nameTok.line, nameTok.column);
            return nullptr;
        }

        int expected = expectedFunctionArgs(name);
        if (expected < 0) {
            setError(QString("Unknown function '%1'").arg(QString::fromStdString(name)),
                     nameTok.line, nameTok.column);
            return nullptr;
        }

        std::vector<std::unique_ptr<AstNode>> args;
        if (m_current.type != TokenType::RParen) {
            while (true) {
                auto arg = parseExpression();
                if (!arg) return nullptr;
                args.push_back(std::move(arg));
                if (m_current.type == TokenType::RParen) break;
                if (!match(TokenType::Comma)) {
                    setError(QString("Expected ',' or ')' in argument list of '%1'").arg(QString::fromStdString(name)),
                             m_current.line, m_current.column);
                    return nullptr;
                }
            }
        }
        advance(); // consume RParen

        if (static_cast<int>(args.size()) != expected) {
            setError(QString("Function '%1' expects %2 argument(s), got %3")
                         .arg(QString::fromStdString(name))
                         .arg(expected)
                         .arg(args.size()),
                     nameTok.line, nameTok.column);
            return nullptr;
        }

        // Expand Remote Sensing macros to standard AST nodes
        std::string clean = name;
        if (clean.rfind("std::", 0) == 0) clean = clean.substr(5);

        if (clean == "ndvi" || clean == "ndwi" || clean == "mndwi" || clean == "nbr") {
            // (a - b) / (a + b)
            // Clone nodes by lowering or structure
            auto a1 = std::move(args[0]);
            auto b1 = std::move(args[1]);
            // NDVI AST: (a - b) / (a + b)
            // Re-parse or duplicate AST node tree
            return expandNormalizedDiff(std::move(a1), std::move(b1));
        }

        if (clean == "evi") {
            // 2.5 * (nir - red) / (nir + 6 * red - 7.5 * blue + 1)
            auto nir = std::move(args[0]);
            auto red = std::move(args[1]);
            auto blue = std::move(args[2]);
            return expandEvi(std::move(nir), std::move(red), std::move(blue));
        }

        if (clean == "savi") {
            // (nir - red) * (1 + L) / (nir + red + L)
            auto nir = std::move(args[0]);
            auto red = std::move(args[1]);
            auto L = std::move(args[2]);
            return expandSavi(std::move(nir), std::move(red), std::move(L));
        }

        if (clean == "bsi") {
            // ((swir + red) - (nir + blue)) / ((swir + red) + (nir + blue))
            auto swir = std::move(args[0]);
            auto red = std::move(args[1]);
            auto nir = std::move(args[2]);
            auto blue = std::move(args[3]);
            return expandBsi(std::move(swir), std::move(red), std::move(nir), std::move(blue));
        }

        return std::make_unique<FunctionCallNode>(name, std::move(args));
    }

    std::unique_ptr<AstNode> cloneNode(const AstNode *node)
    {
        if (const auto *c = dynamic_cast<const ConstantNode*>(node)) {
            return std::make_unique<ConstantNode>(c->value);
        }
        if (const auto *b = dynamic_cast<const BandRefNode*>(node)) {
            return std::make_unique<BandRefNode>(b->bandNum);
        }
        if (const auto *u = dynamic_cast<const UnaryOpNode*>(node)) {
            return std::make_unique<UnaryOpNode>(u->type, cloneNode(u->child.get()));
        }
        if (const auto *bi = dynamic_cast<const BinaryOpNode*>(node)) {
            return std::make_unique<BinaryOpNode>(bi->op, cloneNode(bi->left.get()), cloneNode(bi->right.get()));
        }
        if (const auto *cmp = dynamic_cast<const ComparisonNode*>(node)) {
            return std::make_unique<ComparisonNode>(cmp->op, cloneNode(cmp->left.get()), cloneNode(cmp->right.get()));
        }
        if (const auto *log = dynamic_cast<const LogicalNode*>(node)) {
            return std::make_unique<LogicalNode>(log->isAnd, cloneNode(log->left.get()), cloneNode(log->right.get()));
        }
        if (const auto *cond = dynamic_cast<const ConditionalNode*>(node)) {
            return std::make_unique<ConditionalNode>(cloneNode(cond->condition.get()), cloneNode(cond->trueExpr.get()), cloneNode(cond->falseExpr.get()));
        }
        if (const auto *fn = dynamic_cast<const FunctionCallNode*>(node)) {
            std::vector<std::unique_ptr<AstNode>> clonedArgs;
            for (const auto &arg : fn->args) {
                clonedArgs.push_back(cloneNode(arg.get()));
            }
            return std::make_unique<FunctionCallNode>(fn->name, std::move(clonedArgs));
        }
        return nullptr;
    }

    std::unique_ptr<AstNode> expandNormalizedDiff(std::unique_ptr<AstNode> a, std::unique_ptr<AstNode> b)
    {
        auto aClone = cloneNode(a.get());
        auto bClone = cloneNode(b.get());
        auto num = std::make_unique<BinaryOpNode>('-', std::move(a), std::move(b));
        auto den = std::make_unique<BinaryOpNode>('+', std::move(aClone), std::move(bClone));
        return std::make_unique<BinaryOpNode>('/', std::move(num), std::move(den));
    }

    std::unique_ptr<AstNode> expandEvi(std::unique_ptr<AstNode> nir, std::unique_ptr<AstNode> red, std::unique_ptr<AstNode> blue)
    {
        auto nir2 = cloneNode(nir.get());
        auto red2 = cloneNode(red.get());
        // 2.5 * (nir - red)
        auto diff = std::make_unique<BinaryOpNode>('-', std::move(nir), std::move(red));
        auto num = std::make_unique<BinaryOpNode>('*', std::make_unique<ConstantNode>(2.5f), std::move(diff));
        // nir + 6 * red - 7.5 * blue + 1
        auto sixRed = std::make_unique<BinaryOpNode>('*', std::make_unique<ConstantNode>(6.0f), std::move(red2));
        auto nirPlusSixRed = std::make_unique<BinaryOpNode>('+', std::move(nir2), std::move(sixRed));
        auto sevenFiveBlue = std::make_unique<BinaryOpNode>('*', std::make_unique<ConstantNode>(7.5f), std::move(blue));
        auto subBlue = std::make_unique<BinaryOpNode>('-', std::move(nirPlusSixRed), std::move(sevenFiveBlue));
        auto den = std::make_unique<BinaryOpNode>('+', std::move(subBlue), std::make_unique<ConstantNode>(1.0f));
        return std::make_unique<BinaryOpNode>('/', std::move(num), std::move(den));
    }

    std::unique_ptr<AstNode> expandSavi(std::unique_ptr<AstNode> nir, std::unique_ptr<AstNode> red, std::unique_ptr<AstNode> L)
    {
        auto nir2 = cloneNode(nir.get());
        auto red2 = cloneNode(red.get());
        auto L2 = cloneNode(L.get());
        // (nir - red) * (1 + L)
        auto diff = std::make_unique<BinaryOpNode>('-', std::move(nir), std::move(red));
        auto onePlusL = std::make_unique<BinaryOpNode>('+', std::make_unique<ConstantNode>(1.0f), std::move(L));
        auto num = std::make_unique<BinaryOpNode>('*', std::move(diff), std::move(onePlusL));
        // nir + red + L
        auto nirPlusRed = std::make_unique<BinaryOpNode>('+', std::move(nir2), std::move(red2));
        auto den = std::make_unique<BinaryOpNode>('+', std::move(nirPlusRed), std::move(L2));
        return std::make_unique<BinaryOpNode>('/', std::move(num), std::move(den));
    }

    std::unique_ptr<AstNode> expandBsi(std::unique_ptr<AstNode> swir, std::unique_ptr<AstNode> red, std::unique_ptr<AstNode> nir, std::unique_ptr<AstNode> blue)
    {
        auto swir2 = cloneNode(swir.get());
        auto red2 = cloneNode(red.get());
        auto nir2 = cloneNode(nir.get());
        auto blue2 = cloneNode(blue.get());
        // (swir + red) - (nir + blue)
        auto sr1 = std::make_unique<BinaryOpNode>('+', std::move(swir), std::move(red));
        auto nb1 = std::make_unique<BinaryOpNode>('+', std::move(nir), std::move(blue));
        auto num = std::make_unique<BinaryOpNode>('-', std::move(sr1), std::move(nb1));
        // (swir + red) + (nir + blue)
        auto sr2 = std::make_unique<BinaryOpNode>('+', std::move(swir2), std::move(red2));
        auto nb2 = std::make_unique<BinaryOpNode>('+', std::move(nir2), std::move(blue2));
        auto den = std::make_unique<BinaryOpNode>('+', std::move(sr2), std::move(nb2));
        return std::make_unique<BinaryOpNode>('/', std::move(num), std::move(den));
    }
};

} // namespace BandMath
