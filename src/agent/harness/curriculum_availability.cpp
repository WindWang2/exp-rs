// src/agent/harness/curriculum_availability.cpp
#include "agent/harness/curriculum_availability.h"

#include "agent/harness/curriculum_catalog.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

namespace sicnu::agent::harness {

namespace {

constexpr const char *kSchema = "sicnu.curriculum.availability/1";

Json::Value parseJsonFile( const std::filesystem::path &path )
{
    std::ifstream in( path, std::ios::binary );
    if ( !in ) return {};
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    Json::Value root;
    std::string errs;
    if ( !Json::parseFromStream( builder, in, &root, &errs ) ) return {};
    return root;
}

/// Operator ids declared by a lab's steps. Reads the lab document directly
/// (direct labspec, or the registry source file for aliased labs). External
/// labs belong to the owning track — their steps are not this course's facts.
std::set<std::string> labOperators( const std::string &labId, const std::string &labsDir )
{
    std::set<std::string> operators;
    const CurriculumPaths paths{ labsDir, "" };
    const std::string resolution = resolveLabReference( labId, paths );

    std::filesystem::path docPath;
    if ( resolution == "labspec" )
    {
        docPath = std::filesystem::path( labsDir ) / ( labId + ".lab.json" );
    }
    else if ( resolution == "registry" )
    {
        const Json::Value registry = parseJsonFile( std::filesystem::path( labsDir ) / "lab-registry.json" );
        const Json::Value canonical = registry["canonical"];
        if ( canonical.isObject() )
            for ( const auto &entryId : canonical.getMemberNames() )
            {
                const Json::Value entry = canonical[entryId];
                bool matches = ( entryId == labId );
                if ( !matches && entry["aliases"].isArray() )
                    for ( const auto &alias : entry["aliases"] )
                        if ( alias.isString() && alias.asString() == labId ) matches = true;
                if ( matches && entry["source"].isString() )
                {
                    docPath = std::filesystem::path( labsDir )
                              / std::filesystem::path( entry["source"].asString() ).filename();
                    break;
                }
            }
    }
    else
    {
        return operators;  // external / unknown: no operator claims here
    }

    const Json::Value doc = parseJsonFile( docPath );
    if ( !doc["steps"].isArray() ) return operators;
    for ( const auto &step : doc["steps"] )
        if ( step.isObject() && step["operator_id"].isString() )
            operators.insert( step["operator_id"].asString() );
    return operators;
}

std::string operatorState( const std::string &operatorId, const CurriculumOperatorProbes &probes )
{
    const bool registered = probes.registered && probes.registered( operatorId );
    if ( !registered ) return "unknown";
    const bool note = probes.capabilityNote && probes.capabilityNote( operatorId );
    return note ? "available" : "registered_no_capability_note";
}

} // namespace

Json::Value buildAvailabilityReport( const Json::Value &manifest, const std::string &labsDir,
                                     const std::string &packsDir,
                                     const CurriculumOperatorProbes &probes )
{
    Json::Value report{ Json::objectValue };
    report["schema"] = kSchema;

    const auto failWith = [&]( const std::string &code, const std::string &message ) {
        report["ok"] = false;
        Json::Value issues{ Json::arrayValue };
        Json::Value item{ Json::objectValue };
        item["code"] = code;
        item["path"] = "manifest";
        item["message_zh"] = message;
        issues.append( item );
        report["issues"] = issues;
        return report;
    };

    if ( !manifest.isObject() || !manifest["modules"].isArray() )
        return failWith( "manifest_unavailable", "课程清单不可用。" );

    if ( !probes.registered || !probes.capabilityNote )
        return failWith( "probe_unavailable",
                         "可用性探针未注入；报告拒绝在无证据的情况下猜测算子可用性。" );

    report["ok"] = true;

    Json::Value moduleArray{ Json::arrayValue };
    for ( const auto &module : manifest["modules"] )
    {
        Json::Value moduleOut{ Json::objectValue };
        moduleOut["module_id"] = module["id"];

        Json::Value labArray{ Json::arrayValue };
        if ( module["labs"].isArray() )
            for ( const auto &lab : module["labs"] )
            {
                if ( !lab.isObject() || !lab["lab_id"].isString() ) continue;
                const std::string labId = lab["lab_id"].asString();

                Json::Value labOut{ Json::objectValue };
                labOut["lab_id"] = labId;
                const CurriculumPaths paths{ labsDir, packsDir };
                labOut["resolvable"] = resolveLabReference( labId, paths );

                Json::Value operatorArray{ Json::arrayValue };
                for ( const auto &operatorId : labOperators( labId, labsDir ) )
                {
                    Json::Value item{ Json::objectValue };
                    item["operator_id"] = operatorId;
                    item["state"] = operatorState( operatorId, probes );
                    operatorArray.append( item );
                }
                labOut["operators"] = operatorArray;

                Json::Value packArray{ Json::arrayValue };
                if ( lab["required_data_packs"].isArray() )
                    for ( const auto &pack : lab["required_data_packs"] )
                    {
                        if ( !pack.isString() ) continue;
                        Json::Value item{ Json::objectValue };
                        item["name"] = pack.asString();
                        std::error_code ec;
                        const std::filesystem::path packPath =
                            std::filesystem::path( packsDir ) / ( pack.asString() + ".pack.json" );
                        bool present = false;
                        if ( std::filesystem::is_regular_file( packPath, ec ) )
                        {
                            const Json::Value packDoc = parseJsonFile( packPath );
                            present = packDoc["schema_version"].isString()
                                      && packDoc["schema_version"].asString() == "sicnu.lab-pack/1";
                        }
                        item["present"] = present;
                        packArray.append( item );
                    }
                labOut["data_packs"] = packArray;
                labArray.append( labOut );
            }

        moduleOut["labs"] = labArray;
        moduleArray.append( moduleOut );
    }
    report["modules"] = moduleArray;

    // Forward references are surfaced verbatim: the manifest's own honest
    // declaration. This layer cannot decide model-runtime availability, so it
    // states the declaration instead of guessing a state.
    Json::Value forwardArray{ Json::arrayValue };
    if ( manifest["forward_references"].isArray() )
        for ( const auto &forward : manifest["forward_references"] )
        {
            if ( !forward.isObject() ) continue;
            Json::Value item{ Json::objectValue };
            item["capability"] = forward["capability"];
            item["state"] = "declared_forward_reference";
            if ( forward.isMember( "reason_zh" ) ) item["reason_zh"] = forward["reason_zh"];
            if ( forward.isMember( "wiring" ) ) item["wiring"] = forward["wiring"];
            forwardArray.append( item );
        }
    report["forward_references"] = forwardArray;
    return report;
}

} // namespace sicnu::agent::harness
