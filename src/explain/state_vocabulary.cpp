#include "explain/state_vocabulary.h"

#include <algorithm>

namespace sicnu::explain
{

const std::vector<std::string> &knownStateTokens()
{
  static const std::vector<std::string> tokens = {
    StateTokenDN,       StateTokenRadiance, StateTokenTOA, StateTokenBOA,
    StateTokenIndex,    StateTokenMask,     StateTokenAny, StateTokenNone,
  };
  return tokens;
}

bool isKnownStateToken( const std::string &token )
{
  return std::find( knownStateTokens().begin(), knownStateTokens().end(), token ) !=
         knownStateTokens().end();
}

bool isKnownPortDataType( const std::string &type )
{
  return type == PortDataTypeRaster || type == PortDataTypeVector ||
         type == PortDataTypeTable || type == PortDataTypeScalar ||
         type == PortDataTypeMask;
}

} // namespace sicnu::explain
