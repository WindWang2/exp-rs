// src/processing/algorithms/band_math.cpp — Band math expression engine
#include "band_math.h"
#include "math_utils.h"
#include "core/sicnu_logging.h"
#include "framework/input_validator.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
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

// --- Function call support ---

/// Evaluate a named function with the given evaluated argument values.
static float callFunction(const std::string &name, const float *args, size_t argCount)
{
    std::string cleanName = name;
    if (cleanName.rfind("std::", 0) == 0) {
        cleanName = cleanName.substr(5);
    }

    // Zero-argument functions.
    if (argCount == 0) {
        if (cleanName == "pi") return static_cast<float>(M_PI);
        return NaN;
    }

    // Single-argument functions.
    if (argCount == 1) {
        const float a = args[0];
        if (cleanName == "sin")   return std::sin(a);
        if (cleanName == "cos")   return std::cos(a);
        if (cleanName == "tan")   return std::tan(a);
        if (cleanName == "exp")   return std::exp(a);
        if (cleanName == "ln")    return std::log(a);
        if (cleanName == "log")   return std::log(a);
        if (cleanName == "log10") return std::log10(a);
        if (cleanName == "sqrt")  return std::sqrt(a);
        if (cleanName == "abs")   return std::fabs(a);
        if (cleanName == "asin")  return std::asin(a);
        if (cleanName == "acos")  return std::acos(a);
        if (cleanName == "atan")  return std::atan(a);
        return NaN;
    }

    // Two-argument functions.
    if (argCount == 2) {
        const float a = args[0], b = args[1];
        // NoData (NaN/Inf) must propagate regardless of argument position:
        // std::min/std::max are comparison-based and asymmetric under NaN, so
        // min(b1, b2) could differ from min(b2, b1) at NoData pixels.
        if (!std::isfinite(a) || !std::isfinite(b))
            return NaN;
        if (cleanName == "pow")   return std::pow(a, b);
        if (cleanName == "min")   return std::min(a, b);
        if (cleanName == "max")   return std::max(a, b);
        if (cleanName == "atan2") return std::atan2(a, b);
        return NaN;
    }

    return NaN;
}

/// Expected argument count for a known function name (-1 = variadic/unknown).
static int expectedArgCount(const std::string &name)
{
    static const std::unordered_map<std::string, int> table = {
        {"pi", 0},
        {"sin", 1}, {"cos", 1}, {"tan", 1},
        {"exp", 1}, {"ln", 1}, {"log", 1}, {"log10", 1},
        {"sqrt", 1}, {"abs", 1},
        {"asin", 1}, {"acos", 1}, {"atan", 1},
        {"pow", 2}, {"min", 2}, {"max", 2},
        {"std::min", 2}, {"std::max", 2}, {"atan2", 2},
    };
    auto it = table.find(name);
    if (it != table.end()) return it->second;
    if (name.rfind("std::", 0) == 0) {
        auto it2 = table.find(name.substr(5));
        if (it2 != table.end()) return it2->second;
    }
    return -1;
}

struct FunctionCallNode : Node
{
    std::string name;
    std::vector<std::unique_ptr<Node>> args;

    FunctionCallNode(const std::string &n, std::vector<std::unique_ptr<Node>> a)
        : name(n), args(std::move(a)) {}

    float eval(const BandData &bands, size_t pixel) const override
    {
        if (args.empty()) {
            return callFunction(name, nullptr, 0);
        } else if (args.size() == 1) {
            float v0 = args[0]->eval(bands, pixel);
            return callFunction(name, &v0, 1);
        } else if (args.size() == 2) {
            float v[2] = { args[0]->eval(bands, pixel), args[1]->eval(bands, pixel) };
            return callFunction(name, v, 2);
        }
        std::vector<float> vals;
        vals.reserve(args.size());
        for (const auto &arg : args)
            vals.push_back(arg->eval(bands, pixel));
        return callFunction(name, vals.data(), vals.size());
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
};

// --- Comparison and logical operators ---

struct ComparisonNode : Node
{
    std::string op;  // "<", ">", "<=", ">=", "==", "!="
    std::unique_ptr<Node> left, right;
    ComparisonNode(std::string o, std::unique_ptr<Node> l, std::unique_ptr<Node> r)
        : op(std::move(o)), left(std::move(l)), right(std::move(r)) {}

    float eval(const BandData &bands, size_t pixel) const override
    {
        float l = left->eval(bands, pixel);
        float r = right->eval(bands, pixel);
        if (!std::isfinite(l) || !std::isfinite(r)) return NaN;
        bool result = false;
        if      (op == "<")  result = l < r;
        else if (op == ">")  result = l > r;
        else if (op == "<=") result = l <= r;
        else if (op == ">=") result = l >= r;
        else if (op == "==") result = l == r;
        else if (op == "!=") result = l != r;
        return result ? 1.0f : 0.0f;
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

struct LogicalNode : Node
{
    bool isAnd;  // true = &&, false = ||
    std::unique_ptr<Node> left, right;
    LogicalNode(bool andOp, std::unique_ptr<Node> l, std::unique_ptr<Node> r)
        : isAnd(andOp), left(std::move(l)), right(std::move(r)) {}

    float eval(const BandData &bands, size_t pixel) const override
    {
        float l = left->eval(bands, pixel);
        if (!std::isfinite(l)) return NaN;
        float r = right->eval(bands, pixel);
        if (!std::isfinite(r)) return NaN;
        bool result = isAnd ? (l != 0.0f && r != 0.0f) : (l != 0.0f || r != 0.0f);
        return result ? 1.0f : 0.0f;
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

struct ConditionalNode : Node
{
    std::unique_ptr<Node> condition, trueExpr, falseExpr;
    ConditionalNode(std::unique_ptr<Node> c, std::unique_ptr<Node> t, std::unique_ptr<Node> f)
        : condition(std::move(c)), trueExpr(std::move(t)), falseExpr(std::move(f)) {}

    float eval(const BandData &bands, size_t pixel) const override
    {
        float cond = condition->eval(bands, pixel);
        if (!std::isfinite(cond)) return NaN;
        return (cond != 0.0f) ? trueExpr->eval(bands, pixel) : falseExpr->eval(bands, pixel);
    }
    void collectRefs(std::vector<int> &refs) const override {
        condition->collectRefs(refs);
        trueExpr->collectRefs(refs);
        falseExpr->collectRefs(refs);
    }
    void resolve(const BandData &bands) override {
        condition->resolve(bands);
        trueExpr->resolve(bands);
        falseExpr->resolve(bands);
    }
};

// --- Recursive descent parser ---
//
// Grammar (precedence low → high):
//   expr        → ternary
//   ternary     → logic_or ('?' expr ':' ternary)?
//   logic_or    → logic_and ('||' logic_and)*
//   logic_and   → comparison ('&&' comparison)*
//   comparison  → additive (('<'|'>'|'<='|'>='|'=='|'!=') additive)?
//   additive    → term (('+'|'-') term)*
//   term        → unary (('*'|'/') unary)*
//   unary       → '-' unary | primary
//   primary     → NUMBER | BAND_REF | '(' expr ')' | FUNCTION '(' args ')'

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

    /// Try to match a two-character operator; advance and return true if matched.
    bool matchTwo(char first, char second)
    {
        skipSpaces();
        if (m_pos + 1 < m_expr.size()
            && m_expr[m_pos] == first && m_expr[m_pos + 1] == second) {
            m_pos += 2;
            return true;
        }
        return false;
    }

    // expr → ternary
    std::unique_ptr<Node> parseExpr()
    {
        return parseTernary();
    }

    // ternary → logic_or ('?' expr ':' ternary)?
    std::unique_ptr<Node> parseTernary()
    {
        auto node = parseLogicOr();
        if (!node) return nullptr;

        skipSpaces();
        if (m_pos < m_expr.size() && m_expr[m_pos] == '?') {
            m_pos++;
            auto trueExpr = parseExpr();
            if (!trueExpr) { m_error = true; return nullptr; }
            if (advance() != ':') { m_error = true; return nullptr; }
            auto falseExpr = parseTernary();
            if (!falseExpr) { m_error = true; return nullptr; }
            return std::make_unique<ConditionalNode>(std::move(node), std::move(trueExpr), std::move(falseExpr));
        }
        return node;
    }

    // logic_or → logic_and ('||' logic_and)*
    std::unique_ptr<Node> parseLogicOr()
    {
        auto node = parseLogicAnd();
        if (!node) return nullptr;
        while (matchTwo('|', '|')) {
            auto right = parseLogicAnd();
            if (!right) { m_error = true; return nullptr; }
            node = std::make_unique<LogicalNode>(false, std::move(node), std::move(right));
        }
        return node;
    }

    // logic_and → comparison ('&&' comparison)*
    std::unique_ptr<Node> parseLogicAnd()
    {
        auto node = parseComparison();
        if (!node) return nullptr;
        while (matchTwo('&', '&')) {
            auto right = parseComparison();
            if (!right) { m_error = true; return nullptr; }
            node = std::make_unique<LogicalNode>(true, std::move(node), std::move(right));
        }
        return node;
    }

    // comparison → additive (('<'|'>'|'<='|'>='|'=='|'!=') additive)?
    std::unique_ptr<Node> parseComparison()
    {
        auto node = parseAdditive();
        if (!node) return nullptr;

        std::string op;
        if (matchTwo('<', '='))      op = "<=";
        else if (matchTwo('>', '=')) op = ">=";
        else if (matchTwo('=', '=')) op = "==";
        else if (matchTwo('!', '=')) op = "!=";
        else {
            char c = peek();
            if (c == '<') { advance(); op = "<"; }
            else if (c == '>') { advance(); op = ">"; }
        }

        if (op.empty())
            return node;

        auto right = parseAdditive();
        if (!right) { m_error = true; return nullptr; }
        return std::make_unique<ComparisonNode>(op, std::move(node), std::move(right));
    }

    // additive → term (('+'|'-') term)*   (former parseExpr)
    std::unique_ptr<Node> parseAdditive()
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

    // term → unary (('*'|'/') unary)*   (former parseTerm)
    std::unique_ptr<Node> parseTerm()
    {
        auto node = parseUnary();
        if (!node) return nullptr;

        while (peek() == '*' || peek() == '/') {
            char op = advance();
            auto right = parseUnary();
            if (!right) { m_error = true; return nullptr; }
            node = std::make_unique<BinaryOpNode>(op, std::move(node), std::move(right));
        }
        return node;
    }

    // unary → '-' unary | primary
    std::unique_ptr<Node> parseUnary()
    {
        skipSpaces();
        if (m_pos < m_expr.size() && m_expr[m_pos] == '-') {
            m_pos++;
            auto child = parseUnary();
            if (!child) { m_error = true; return nullptr; }
            return std::make_unique<UnaryNegNode>(std::move(child));
        }
        return parsePrimary();
    }

    // primary → NUMBER | BAND_REF | '(' expr ')' | FUNCTION '(' args ')'
    std::unique_ptr<Node> parsePrimary()
    {
        skipSpaces();
        if (m_pos >= m_expr.size()) { m_error = true; return nullptr; }

        char c = m_expr[m_pos];

        // Parenthesized expression
        if (c == '(') {
            m_pos++;
            auto node = parseExpr();
            if (!node) return nullptr;
            if (advance() != ')') { m_error = true; return nullptr; }
            return node;
        }

        // Band reference: bN (but NOT a function name starting with 'b')
        if ((c == 'b' || c == 'B') && m_pos + 1 < m_expr.size() && std::isdigit(m_expr[m_pos + 1])) {
            m_pos++;
            int num = 0;
            while (m_pos < m_expr.size() && std::isdigit(m_expr[m_pos])) {
                num = num * 10 + (m_expr[m_pos] - '0');
                m_pos++;
            }
            if (num < 1) { m_error = true; return nullptr; }
            return std::make_unique<BandRefNode>(num);
        }

        // Identifier → function call
        if (std::isalpha(c) || c == '_') {
            return parseFunctionCall();
        }

        // Number constant
        if (std::isdigit(c) || c == '.') {
            return parseNumber();
        }

        m_error = true;
        return nullptr;
    }

    // Parse a function call: NAME '(' args ')'
    std::unique_ptr<Node> parseFunctionCall()
    {
        size_t start = m_pos;
        while (m_pos < m_expr.size() && (std::isalnum(m_expr[m_pos]) || m_expr[m_pos] == '_' || m_expr[m_pos] == ':'))
            m_pos++;
        std::string name = m_expr.substr(start, m_pos - start);

        // Expect '('
        if (advance() != '(') { m_error = true; return nullptr; }

        // Validate function name is known.
        int expected = expectedArgCount(name);
        if (expected < 0) { m_error = true; return nullptr; }

        // Parse comma-separated argument list.
        std::vector<std::unique_ptr<Node>> args;
        skipSpaces();
        if (peek() != ')') {
            // At least one argument.
            while (true) {
                auto arg = parseExpr();
                if (!arg) { m_error = true; return nullptr; }
                args.push_back(std::move(arg));
                char sep = advance();
                if (sep == ')') break;
                if (sep != ',') { m_error = true; return nullptr; }
            }
        } else {
            if (advance() != ')') { m_error = true; return nullptr; }
        }

        // Validate argument count.
        if (static_cast<int>(args.size()) != expected) { m_error = true; return nullptr; }

        return std::make_unique<FunctionCallNode>(name, std::move(args));
    }

    std::unique_ptr<Node> parseNumber()
    {
        size_t start = m_pos;
        bool hasDot = false;
        // Support scientific notation: digits, '.', 'e'/'E', '+/-' after e.
        bool hasExp = false;
        while (m_pos < m_expr.size()) {
            char ch = m_expr[m_pos];
            if (std::isdigit(ch)) { m_pos++; continue; }
            if (ch == '.' && !hasDot && !hasExp) { hasDot = true; m_pos++; continue; }
            if ((ch == 'e' || ch == 'E') && !hasExp && m_pos > start) {
                hasExp = true; m_pos++;
                if (m_pos < m_expr.size() && (m_expr[m_pos] == '+' || m_expr[m_pos] == '-'))
                    m_pos++;
                continue;
            }
            break;
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

std::vector<int> referencedBands(const QString &expression)
{
    Parser parser(expression);
    auto ast = parser.parse();
    if (!ast || parser.hasError())
        return {};
    std::vector<int> refs;
    ast->collectRefs(refs);
    std::sort(refs.begin(), refs.end());
    refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
    return refs;
}

bool processFile(const QString &sourcePath, const QString &outputPath,
                 const QString &expression, QString *errorMessage)
{
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(sourcePath)) {
        if (errorMessage)
            *errorMessage = srcDataset.lastError();
        return false;
    }

    const int width = srcDataset.width();
    const int height = srcDataset.height();
    const int bandCount = srcDataset.bandCount();
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

    Parser parser(expression);
    auto ast = parser.parse();
    if (!ast || parser.hasError()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to parse expression");
        return false;
    }

    std::vector<int> requiredBands;
    ast->collectRefs(requiredBands);
    std::sort(requiredBands.begin(), requiredBands.end());
    requiredBands.erase(std::unique(requiredBands.begin(), requiredBands.end()), requiredBands.end());

    BandData bands;
    for (int b : requiredBands) {
        if (b < 1 || b > bandCount) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Band index %1 out of range (1..%2)").arg(b).arg(bandCount);
            return false;
        }
        std::vector<float> buffer(pixelCount);
        if (!srcDataset.readBandData(b, buffer.data(), width, height)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Failed to read band %1").arg(b);
            return false;
        }
        bool hasNodata = false;
        double nodataVal = srcDataset.bandNoDataValue(b, &hasNodata);
        // Compare in float space: the pixels are float, so casting the
        // declared NoData to float matches exactly regardless of magnitude.
        // A fixed absolute tolerance (e.g. 1e-6) is far below one ULP for
        // large sentinels like -3.4e38 and would never match them.
        const float nodataF = static_cast<float>(nodataVal);
        const bool nodataValid = hasNodata && std::isfinite(nodataVal);
        for (float &val : buffer) {
            // Mask Inf too (regardless of a NoData declaration): callFunction
            // treats any non-finite operand as NoData, so file-level input
            // must be uniformly finite-or-NaN (#449).
            if (!std::isfinite(val) || (nodataValid && val == nodataF)) {
                val = std::numeric_limits<float>::quiet_NaN();
            }
        }
        bands[b] = std::move(buffer);
    }

    std::vector<float> output(pixelCount);
    ast->resolve(bands);
    SICNU_LOG_INFO(SicnuLogTags::Algorithms, QString("BandMath: evaluating '%1' on %2 pixels, %3 bands")
                   .arg(expression).arg(pixelCount).arg(bands.size()));

    for (size_t i = 0; i < pixelCount; i++) {
        output[i] = ast->eval(bands, i);
    }

    std::vector<std::vector<float>> outBands = {std::move(output)};
    QString writeError;
    if (!writeGdalOutput(outputPath, width, height, outBands,
                         srcDataset.geoTransform(), srcDataset.projection(), &writeError,
                         std::numeric_limits<double>::quiet_NaN())) {
        if (errorMessage)
            *errorMessage = writeError;
        return false;
    }

    return true;
}

} // namespace BandMath
