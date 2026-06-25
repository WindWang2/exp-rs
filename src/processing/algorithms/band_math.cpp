// src/processing/algorithms/band_math.cpp — Band math expression engine
#include "band_math.h"
#include "math_utils.h"
#include "core/sicnu_logging.h"
#include "framework/input_validator.h"

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <cctype>

namespace BandMath
{

static constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

// --- AST nodes ---

struct Node
{
    virtual ~Node() = default;
    virtual float eval(const BandData &bands, size_t pixel) const = 0;
    virtual void collectRefs(std::vector<int> &) const {}
    virtual void resolve(const BandData &bands) {} // Pre-resolve band pointers
};

struct ConstantNode : Node
{
    float value;
    explicit ConstantNode(float v) : value(v) {}
    float eval(const BandData &, size_t) const override { return value; }
};

struct BandRefNode : Node
{
    int bandNum;
    const float *resolvedPtr = nullptr;
    size_t resolvedSize = 0;
    explicit BandRefNode(int n) : bandNum(n) {}
    float eval(const BandData &bands, size_t pixel) const override
    {
        if (resolvedPtr) {
            return (pixel < resolvedSize) ? resolvedPtr[pixel] : NaN;
        }
        auto it = bands.find(bandNum);
        if (it == bands.end() || pixel >= it->second.size()) return NaN;
        return it->second[pixel];
    }
    void collectRefs(std::vector<int> &refs) const override { refs.push_back(bandNum); }
    void resolve(const BandData &bands) override
    {
        auto it = bands.find(bandNum);
        if (it != bands.end()) {
            resolvedPtr = it->second.data();
            resolvedSize = it->second.size();
        }
    }
};

struct BinaryOpNode : Node
{
    char op;
    std::unique_ptr<Node> left, right;
    BinaryOpNode(char o, std::unique_ptr<Node> l, std::unique_ptr<Node> r)
        : op(o), left(std::move(l)), right(std::move(r)) {}

    float eval(const BandData &bands, size_t pixel) const override
    {
        float l = left->eval(bands, pixel);
        float r = right->eval(bands, pixel);
        switch (op) {
            case '+': return l + r;
            case '-': return l - r;
            case '*': return l * r;
            case '/': return MathUtils::safeDiv(l, r);
            default: return NaN;
        }
    }
    void collectRefs(std::vector<int> &refs) const override {
        left->collectRefs(refs);
        right->collectRefs(refs);
    }
    void resolve(const BandData &bands) override {
        left->resolve(bands);
        right->resolve(bands);
    }
};

struct UnaryNegNode : Node
{
    std::unique_ptr<Node> child;
    explicit UnaryNegNode(std::unique_ptr<Node> c) : child(std::move(c)) {}
    float eval(const BandData &bands, size_t pixel) const override
    {
        return -child->eval(bands, pixel);
    }
    void collectRefs(std::vector<int> &refs) const override { child->collectRefs(refs); }
    void resolve(const BandData &bands) override { child->resolve(bands); }
};

// --- Recursive descent parser ---

class Parser
{
public:
    Parser(const QString &expr) : m_expr(expr.toStdString()), m_pos(0) {}

    std::unique_ptr<Node> parse()
    {
        auto node = parseExpr();
        if (m_pos < m_expr.size()) {
            m_error = true;
            return nullptr;
        }
        return node;
    }

    bool hasError() const { return m_error; }

private:
    std::string m_expr;
    size_t m_pos;
    bool m_error = false;

    void skipSpaces()
    {
        while (m_pos < m_expr.size() && std::isspace(m_expr[m_pos]))
            m_pos++;
    }

    char peek()
    {
        skipSpaces();
        return (m_pos < m_expr.size()) ? m_expr[m_pos] : '\0';
    }

    char advance()
    {
        skipSpaces();
        return (m_pos < m_expr.size()) ? m_expr[m_pos++] : '\0';
    }

    // expr → term (('+' | '-') term)*
    std::unique_ptr<Node> parseExpr()
    {
        auto node = parseTerm();
        if (!node) return nullptr;

        while (peek() == '+' || peek() == '-') {
            char op = advance();
            auto right = parseTerm();
            if (!right) { m_error = true; return nullptr; }
            node = std::make_unique<BinaryOpNode>(op, std::move(node), std::move(right));
        }
        return node;
    }

    // term → factor (('*' | '/') factor)*
    std::unique_ptr<Node> parseTerm()
    {
        auto node = parseFactor();
        if (!node) return nullptr;

        while (peek() == '*' || peek() == '/') {
            char op = advance();
            auto right = parseFactor();
            if (!right) { m_error = true; return nullptr; }
            node = std::make_unique<BinaryOpNode>(op, std::move(node), std::move(right));
        }
        return node;
    }

    // factor → NUMBER | BAND_REF | '(' expr ')' | ('+' | '-') factor
    std::unique_ptr<Node> parseFactor()
    {
        skipSpaces();
        if (m_pos >= m_expr.size()) { m_error = true; return nullptr; }

        char c = m_expr[m_pos];

        // Unary - only (reject leading +)
        if (c == '-') {
            m_pos++;
            auto child = parseFactor();
            if (!child) { m_error = true; return nullptr; }
            return std::make_unique<UnaryNegNode>(std::move(child));
        }

        // Parenthesized expression
        if (c == '(') {
            m_pos++;
            auto node = parseExpr();
            if (!node) return nullptr;
            if (advance() != ')') { m_error = true; return nullptr; }
            return node;
        }

        // Band reference: bN
        if (c == 'b' || c == 'B') {
            m_pos++;
            int num = 0;
            bool foundDigit = false;
            while (m_pos < m_expr.size() && std::isdigit(m_expr[m_pos])) {
                num = num * 10 + (m_expr[m_pos] - '0');
                m_pos++;
                foundDigit = true;
            }
            if (!foundDigit || num < 1) { m_error = true; return nullptr; }
            return std::make_unique<BandRefNode>(num);
        }

        // Number constant
        if (std::isdigit(c) || c == '.') {
            return parseNumber();
        }

        m_error = true;
        return nullptr;
    }

    std::unique_ptr<Node> parseNumber()
    {
        size_t start = m_pos;
        bool hasDot = false;
        while (m_pos < m_expr.size() && (std::isdigit(m_expr[m_pos]) || m_expr[m_pos] == '.')) {
            if (m_expr[m_pos] == '.') {
                if (hasDot) { m_error = true; return nullptr; }
                hasDot = true;
            }
            m_pos++;
        }
        if (m_pos == start) { m_error = true; return nullptr; }
        try {
            float val = std::stof(m_expr.substr(start, m_pos - start));
            return std::make_unique<ConstantNode>(val);
        } catch (const std::exception &) {
            m_error = true;
            return nullptr;
        }
    }
};

// --- Public API ---

bool evaluate(const QString &expression, const BandData &bands, float *out, size_t count)
{
    if (!out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "BandMath::evaluate: null output pointer");
        return false;
    }
    if (expression.isEmpty() || count == 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "BandMath::evaluate: invalid arguments");
        return false;
    }

    Parser parser(expression);
    auto ast = parser.parse();
    if (!ast || parser.hasError()) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("BandMath: parse error in expression: %1").arg(expression));
        return false;
    }

    // Validate that all referenced bands exist in the data
    std::vector<int> refs;
    ast->collectRefs(refs);
    for (int ref : refs) {
        if (bands.find(ref) == bands.end()) {
            SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("BandMath: band b%1 not found in input data").arg(ref));
            return false;
        }
    }

    // Pre-resolve band pointers to avoid per-pixel map lookups
    ast->resolve(bands);
    SICNU_LOG_INFO(SicnuLogTags::Algorithms, QString("BandMath: evaluating '%1' on %2 pixels, %3 bands")
                   .arg(expression).arg(count).arg(bands.size()));

    for (size_t i = 0; i < count; i++) {
        out[i] = ast->eval(bands, i);
    }
    return true;
}

} // namespace BandMath
