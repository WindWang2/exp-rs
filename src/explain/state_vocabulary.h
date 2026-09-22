/***************************************************************************
 * state_vocabulary.h — projected radiometric-state token vocabulary
 *
 * The closed set of state tokens the explain layer understands. It mirrors
 * the workflow IR 2.0 PortFact radiometricState vocabulary
 * ("DN|Radiance|TOA|BOA|Index|Mask|*|None") so explanations and workflow
 * documents speak the same language. This is a *projection*: the authority
 * for state semantics stays with the workflow IR and the radiometric FSM.
 * test_explain_projection pins the two vocabularies together.
 ***************************************************************************/
#pragma once

#include <string>
#include <vector>

namespace sicnu::explain
{

constexpr const char *StateTokenDN = "DN";
constexpr const char *StateTokenRadiance = "Radiance";
constexpr const char *StateTokenTOA = "TOA";
constexpr const char *StateTokenBOA = "BOA";
constexpr const char *StateTokenIndex = "Index";
constexpr const char *StateTokenMask = "Mask";
constexpr const char *StateTokenAny = "*";
constexpr const char *StateTokenNone = "None";

const std::vector<std::string> &knownStateTokens();
bool isKnownStateToken( const std::string &token );

// Port data types, mirroring the workflow IR 2.0 PortFact dataType set.
constexpr const char *PortDataTypeRaster = "Raster";
constexpr const char *PortDataTypeVector = "Vector";
constexpr const char *PortDataTypeTable = "Table";
constexpr const char *PortDataTypeScalar = "Scalar";
constexpr const char *PortDataTypeMask = "Mask";
bool isKnownPortDataType( const std::string &type );

} // namespace sicnu::explain
