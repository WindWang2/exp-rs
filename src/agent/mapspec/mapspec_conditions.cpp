// src/agent/mapspec/mapspec_conditions.cpp
#include "mapspec_conditions.h"

#include <cctype>
#include <memory>

namespace sicnu::agent::mapspec {

namespace {

constexpr int kMaxTokens = 64;
constexpr int kMaxDepth = 8;

struct Token
{
    enum class Kind { Number, String, Ident, Op, LParen, RParen, End };
    Kind kind = Kind::End;
    std::string text;
    double number = 0.0;
};

bool tokenize( const std::string &expr, std::vector<Token> &tokens, std::string &error )
{
  size_t i = 0;
  while ( i < expr.size() )
  {
    const char c = expr[i];
    if ( std::isspace( static_cast<unsigned char>( c ) ) )
    {
      ++i;
      continue;
    }
    Token token;
    if ( std::isdigit( static_cast<unsigned char>( c ) ) ||
         ( c == '-' && i + 1 < expr.size() &&
           std::isdigit( static_cast<unsigned char>( expr[i + 1] ) ) ) )
    {
      const size_t start = i;
      ++i;
      while ( i < expr.size() &&
              ( std::isdigit( static_cast<unsigned char>( expr[i] ) ) || expr[i] == '.' ) )
        ++i;
      token.kind = Token::Kind::Number;
      token.text = expr.substr( start, i - start );
      try
      {
        token.number = std::stod( token.text );
      }
      catch ( const std::exception & )
      {
        error = "malformed number '" + token.text + "'";
        return false;
      }
    }
    else if ( c == '"' )
    {
      const size_t start = ++i;
      while ( i < expr.size() && expr[i] != '"' )
        ++i;
      if ( i >= expr.size() )
      {
        error = "unterminated string literal";
        return false;
      }
      token.kind = Token::Kind::String;
      token.text = expr.substr( start, i - start );
      ++i;
    }
    else if ( std::isalpha( static_cast<unsigned char>( c ) ) || c == '_' )
    {
      const size_t start = i;
      ++i;
      while ( i < expr.size() &&
              ( std::isalnum( static_cast<unsigned char>( expr[i] ) ) || expr[i] == '_' ||
                expr[i] == '.' || expr[i] == '-' ) )
        ++i;
      token.kind = Token::Kind::Ident;
      token.text = expr.substr( start, i - start );
    }
    else if ( c == '(' )
    {
      token.kind = Token::Kind::LParen;
      ++i;
    }
    else if ( c == ')' )
    {
      token.kind = Token::Kind::RParen;
      ++i;
    }
    else
    {
      const std::string two = expr.substr( i, 2 );
      if ( two == "==" || two == "!=" || two == ">=" || two == "<=" )
      {
        token.kind = Token::Kind::Op;
        token.text = two;
        i += 2;
      }
      else if ( c == '>' || c == '<' )
      {
        token.kind = Token::Kind::Op;
        token.text = std::string( 1, c );
        ++i;
      }
      else
      {
        error = "unexpected character '" + std::string( 1, c ) + "' at offset " +
                std::to_string( i );
        return false;
      }
    }
    tokens.push_back( std::move( token ) );
    if ( tokens.size() > kMaxTokens )
    {
      error = "expression exceeds the token budget";
      return false;
    }
  }
  return true;
}

struct ConditionAst;
using ConditionPtr = std::unique_ptr<ConditionAst>;

struct ConditionAst
{
    enum class Op { And, Or, Compare, Has, Literal, Path, Bool };
    Op op = Op::Literal;
    std::string text;    // path, literal string, or comparison operator
    double number = 0.0;
    bool isNumber = false;
    bool boolValue = false;
    std::vector<ConditionPtr> children;
};

class Parser
{
  public:
    explicit Parser( const std::vector<Token> &tokens ) : mTokens( tokens ) {}

    ConditionPtr parse( std::string &error )
    {
      ConditionPtr node = parseOr( 0, error );
      if ( !error.empty() )
        return nullptr;
      if ( mPos != mTokens.size() )
      {
        error = "unexpected trailing tokens";
        return nullptr;
      }
      return node;
    }

  private:
    bool peekIdent( const char *word ) const
    {
      return mPos < mTokens.size() && mTokens[mPos].kind == Token::Kind::Ident &&
             mTokens[mPos].text == word;
    }

    ConditionPtr parseOr( int depth, std::string &error )
    {
      if ( depth > kMaxDepth )
      {
        error = "expression exceeds the nesting budget";
        return nullptr;
      }
      ConditionPtr left = parseAnd( depth, error );
      if ( !error.empty() )
        return nullptr;
      while ( peekIdent( "or" ) )
      {
        ++mPos;
        ConditionPtr right = parseAnd( depth, error );
        if ( !error.empty() )
          return nullptr;
        ConditionPtr node = std::make_unique<ConditionAst>();
        node->op = ConditionAst::Op::Or;
        node->children.push_back( std::move( left ) );
        node->children.push_back( std::move( right ) );
        left = std::move( node );
      }
      return left;
    }

    ConditionPtr parseAnd( int depth, std::string &error )
    {
      if ( depth > kMaxDepth )
      {
        error = "expression exceeds the nesting budget";
        return nullptr;
      }
      ConditionPtr left = parseCompare( depth, error );
      if ( !error.empty() )
        return nullptr;
      while ( peekIdent( "and" ) )
      {
        ++mPos;
        ConditionPtr right = parseCompare( depth, error );
        if ( !error.empty() )
          return nullptr;
        ConditionPtr node = std::make_unique<ConditionAst>();
        node->op = ConditionAst::Op::And;
        node->children.push_back( std::move( left ) );
        node->children.push_back( std::move( right ) );
        left = std::move( node );
      }
      return left;
    }

    ConditionPtr parseCompare( int depth, std::string &error )
    {
      if ( depth > kMaxDepth )
      {
        error = "expression exceeds the nesting budget";
        return nullptr;
      }
      if ( mPos < mTokens.size() && mTokens[mPos].kind == Token::Kind::LParen )
      {
        ++mPos;
        ConditionPtr inner = parseOr( depth + 1, error );
        if ( !error.empty() )
          return nullptr;
        if ( mPos >= mTokens.size() || mTokens[mPos].kind != Token::Kind::RParen )
        {
          error = "expected ')'";
          return nullptr;
        }
        ++mPos;
        return inner;
      }

      ConditionPtr lhs = parseOperand( error );
      if ( !error.empty() )
        return nullptr;
      if ( mPos < mTokens.size() && mTokens[mPos].kind == Token::Kind::Op )
      {
        ConditionPtr node = std::make_unique<ConditionAst>();
        node->op = ConditionAst::Op::Compare;
        node->text = mTokens[mPos].text;
        ++mPos;
        ConditionPtr rhs = parseOperand( error );
        if ( !error.empty() )
          return nullptr;
        node->children.push_back( std::move( lhs ) );
        node->children.push_back( std::move( rhs ) );
        return node;
      }
      return lhs;
    }

    ConditionPtr parseOperand( std::string &error )
    {
      if ( mPos >= mTokens.size() )
      {
        error = "unexpected end of expression";
        return nullptr;
      }
      const Token &token = mTokens[mPos];
      ConditionPtr node = std::make_unique<ConditionAst>();
      if ( token.kind == Token::Kind::Number )
      {
        node->op = ConditionAst::Op::Literal;
        node->number = token.number;
        node->isNumber = true;
        ++mPos;
        return node;
      }
      if ( token.kind == Token::Kind::String )
      {
        node->op = ConditionAst::Op::Literal;
        node->text = token.text;
        ++mPos;
        return node;
      }
      if ( token.kind == Token::Kind::Ident )
      {
        if ( token.text == "true" || token.text == "false" )
        {
          node->op = ConditionAst::Op::Bool;
          node->boolValue = token.text == "true";
          ++mPos;
          return node;
        }
        if ( token.text == "has" )
        {
          ++mPos;
          if ( mPos >= mTokens.size() || mTokens[mPos].kind != Token::Kind::LParen )
          {
            error = "has() needs a parenthesized path";
            return nullptr;
          }
          ++mPos;
          if ( mPos >= mTokens.size() || mTokens[mPos].kind != Token::Kind::Ident )
          {
            error = "has() needs a path argument";
            return nullptr;
          }
          node->op = ConditionAst::Op::Has;
          node->text = mTokens[mPos].text;
          ++mPos;
          if ( mPos >= mTokens.size() || mTokens[mPos].kind != Token::Kind::RParen )
          {
            error = "has() needs a closing parenthesis";
            return nullptr;
          }
          ++mPos;
          return node;
        }
        if ( token.text == "and" || token.text == "or" )
        {
          error = "unexpected keyword '" + token.text + "'";
          return nullptr;
        }
        node->op = ConditionAst::Op::Path;
        node->text = token.text;
        ++mPos;
        return node;
      }
      error = "expected an operand";
      return nullptr;
    }

    const std::vector<Token> &mTokens;
    size_t mPos = 0;
};

ConditionPtr compileOrEmpty( const std::string &expr, std::string &error )
{
  if ( expr.size() > kConditionMaxLength )
  {
    error = "expression exceeds the length budget";
    return nullptr;
  }
  std::vector<Token> tokens;
  if ( !tokenize( expr, tokens, error ) )
    return nullptr;
  if ( tokens.empty() )
  {
    error = "empty expression";
    return nullptr;
  }
  Parser parser( tokens );
  return parser.parse( error );
}

const Json::Value *resolvePath( const Json::Value &context, const std::string &path )
{
  const Json::Value *current = &context;
  size_t start = 0;
  while ( start <= path.size() )
  {
    const size_t dot = path.find( '.', start );
    const std::string segment =
      dot == std::string::npos ? path.substr( start ) : path.substr( start, dot - start );
    if ( segment.empty() || !current->isObject() || !current->isMember( segment ) )
      return nullptr;
    current = &( *current )[segment];
    if ( dot == std::string::npos )
      break;
    start = dot + 1;
  }
  return current;
}

// Comparison with a total ordering across the three value kinds the grammar
// supports: numbers, strings, booleans. Mixed-kind ordering is an error;
// mixed-kind equality is well-defined (false, or !false for !=).
bool compareValues( const Json::Value &lhs, const std::string &op, const Json::Value &rhs,
                    std::string &error )
{
  const bool numeric = lhs.isNumeric() && rhs.isNumeric();
  const bool strings = lhs.isString() && rhs.isString();
  const bool bools = lhs.isBool() && rhs.isBool();
  if ( !numeric && !strings && !bools )
  {
    if ( op == "==" )
      return false;
    if ( op == "!=" )
      return true;
    error = "cannot order values of different kinds";
    return false;
  }
  int ordering = 0;
  if ( numeric )
  {
    const double a = lhs.asDouble();
    const double b = rhs.asDouble();
    ordering = a < b ? -1 : ( a > b ? 1 : 0 );
  }
  else if ( strings )
  {
    const int raw = lhs.asString().compare( rhs.asString() );
    ordering = raw < 0 ? -1 : ( raw > 0 ? 1 : 0 );
  }
  else
  {
    const int a = lhs.asBool() ? 1 : 0;
    const int b = rhs.asBool() ? 1 : 0;
    ordering = a < b ? -1 : ( a > b ? 1 : 0 );
  }
  if ( op == "==" )
    return ordering == 0;
  if ( op == "!=" )
    return ordering != 0;
  if ( op == "<" )
    return ordering < 0;
  if ( op == "<=" )
    return ordering <= 0;
  if ( op == ">" )
    return ordering > 0;
  if ( op == ">=" )
    return ordering >= 0;
  error = "unknown comparison operator '" + op + "'";
  return false;
}

Json::Value operandValue( const ConditionAst &node, const Json::Value &context, bool *ok,
                          std::string &error )
{
  if ( node.op == ConditionAst::Op::Literal && node.isNumber )
    return Json::Value( node.number );
  if ( node.op == ConditionAst::Op::Literal )
    return Json::Value( node.text );
  if ( node.op == ConditionAst::Op::Bool )
    return Json::Value( node.boolValue );
  const Json::Value *resolved = resolvePath( context, node.text );
  if ( resolved == nullptr )
  {
    error = "unknown context path '" + node.text + "'";
    *ok = false;
    return Json::Value();
  }
  return *resolved;
}

bool evaluateAst( const ConditionAst &node, const Json::Value &context, std::string &error )
{
  switch ( node.op )
  {
    case ConditionAst::Op::And:
    {
      // Issue #804: both operands are evaluated even when the first decides
      // the result, but only DECISION-RELEVANT errors fail the condition —
      // a clean false on the left decides the conjunction no matter what the
      // right operand would have reported. Evaluating every error as fatal
      // (the first #804 fix) regressed the only presence-guard idiom the
      // grammar offers ("has(x) or x.status == \"ok\"") and could un-hide
      // gated content; deciding-operand-wins keeps diagnostics flowing where
      // they can change the outcome.
      std::string leftError;
      const bool left = evaluateAst( *node.children[0], context, leftError );
      if ( !leftError.empty() )
      {
        error = leftError;
        return false;
      }
      if ( !left )
        return false;
      std::string rightError;
      const bool right = evaluateAst( *node.children[1], context, rightError );
      if ( !rightError.empty() )
      {
        error = rightError;
        return false;
      }
      return right;
    }
    case ConditionAst::Op::Or:
    {
      std::string leftError;
      const bool left = evaluateAst( *node.children[0], context, leftError );
      if ( !leftError.empty() )
      {
        error = leftError;
        return false;
      }
      if ( left )
        return true;
      std::string rightError;
      const bool right = evaluateAst( *node.children[1], context, rightError );
      if ( !rightError.empty() )
      {
        error = rightError;
        return false;
      }
      return right;
    }
    case ConditionAst::Op::Compare:
    {
      bool ok = true;
      std::string operandError;
      const Json::Value lhs = operandValue( *node.children[0], context, &ok, operandError );
      if ( !ok )
      {
        error = operandError;
        return false;
      }
      const Json::Value rhs = operandValue( *node.children[1], context, &ok, operandError );
      if ( !ok )
      {
        error = operandError;
        return false;
      }
      std::string compareError;
      const bool result = compareValues( lhs, node.text, rhs, compareError );
      if ( !compareError.empty() )
      {
        error = compareError;
        return false;
      }
      return result;
    }
    case ConditionAst::Op::Has:
      return resolvePath( context, node.text ) != nullptr;
    case ConditionAst::Op::Bool:
      return node.boolValue;
    case ConditionAst::Op::Path:
    {
      const Json::Value *resolved = resolvePath( context, node.text );
      if ( resolved == nullptr )
      {
        error = "unknown context path '" + node.text + "'";
        return false;
      }
      if ( !resolved->isBool() )
      {
        error = "path '" + node.text + "' is not a boolean";
        return false;
      }
      return resolved->asBool();
    }
    case ConditionAst::Op::Literal:
      error = "a bare literal is not a condition";
      return false;
  }
  error = "unknown condition node";
  return false;
}

} // namespace

bool validateConditionSyntax( const std::string &expr, std::vector<std::string> *problems )
{
  std::string error;
  const ConditionPtr ast = compileOrEmpty( expr, error );
  if ( ast == nullptr )
  {
    if ( problems )
      problems->push_back( error );
    return false;
  }
  // Issue #804: a whole condition that is a bare number/string literal can
  // never evaluate — reject it here so validation catches what evaluation
  // would only report at runtime ("a bare literal is not a condition").
  if ( ast->op == ConditionAst::Op::Literal )
  {
    if ( problems )
      problems->push_back( "a bare literal is not a condition" );
    return false;
  }
  return true;
}

std::set<std::string> conditionPaths( const std::string &expr )
{
  std::string error;
  const ConditionPtr ast = compileOrEmpty( expr, error );
  std::set<std::string> paths;
  if ( ast == nullptr )
    return paths;
  std::vector<const ConditionAst *> stack{ ast.get() };
  while ( !stack.empty() )
  {
    const ConditionAst *node = stack.back();
    stack.pop_back();
    if ( node->op == ConditionAst::Op::Path || node->op == ConditionAst::Op::Has )
      paths.insert( node->text );
    for ( const auto &child : node->children )
      stack.push_back( child.get() );
  }
  return paths;
}

bool evaluateCondition( const std::string &expr, const Json::Value &context, bool *value,
                        std::string *error )
{
  std::string localError;
  const ConditionPtr ast = compileOrEmpty( expr, localError );
  if ( ast == nullptr )
  {
    if ( error )
      *error = localError;
    return false;
  }
  const bool result = evaluateAst( *ast, context, localError );
  if ( !localError.empty() )
  {
    if ( error )
      *error = localError;
    return false;
  }
  if ( value )
    *value = result;
  return true;
}

} // namespace sicnu::agent::mapspec
