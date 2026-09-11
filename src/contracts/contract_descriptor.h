/***************************************************************************
 * contract_descriptor.h — canonical typed contract descriptor (M1)
 *
 * Minimal shared descriptor projected FROM the authoritative runtime
 * metadata (operator schema() today; other surfaces are compared against
 * it, never merged into it). Domain-specific content stays in `extensions`
 * so no module is forced into one giant class.
 *
 * Canonical JSON projection: schema "exp.contract.descriptor.v1", members
 * sorted, deterministic — byte-stable output for snapshot byte-compare.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::contracts {

struct ContractParam
{
    std::string name;
    std::string type; // JSON schema type: string|number|integer|boolean|array|object
    std::string description;
    bool required = false;
    bool hasDefault = false;
    std::string defaultValue; // JSON-serialized default, "" when absent
    std::vector<std::string> enumValues;
    bool hasRange = false;
    double minimum = 0.0;
    double maximum = 0.0;
};

struct ContractDescriptor
{
    std::string id;         // authoritative id, e.g. "rs:infer"
    std::string kind = "operator";
    std::string title;
    std::string description;
    std::string determinismGrade; // "bit-exact" | "tolerance" | ""
    std::vector<ContractParam> params; // sorted by name
    std::vector<ContractParam> outputs; // sorted by name
    std::vector<std::string> requiredParams; // sorted, unique
    Json::Value extensions; // root keys outside the canonical set (x-*, …)

    /// Projects an operator schema() root (as produced by makeRootSchema)
    /// into the canonical descriptor. Returns false with @p error set when
    /// the schema root is structurally invalid (missing properties object,
    /// param object without name/type, key/name mismatch).
    static bool fromOperatorSchema( const std::string &id,
                                    const Json::Value &schemaRoot,
                                    ContractDescriptor &out,
                                    std::string &error );

    /// Deterministic canonical JSON. Round-trip guarantee:
    /// fromCanonicalJson(toJson(d)) == d for every descriptor produced by
    /// fromOperatorSchema.
    Json::Value toJson() const;

    /// Inverse of toJson(). Returns false on schema/version mismatch.
    static bool fromCanonicalJson( const Json::Value &json,
                                   ContractDescriptor &out,
                                   std::string &error );

    /// Names only — convenient for set comparisons in the guards.
    std::vector<std::string> paramNames() const;
};

} // namespace sicnu::contracts
