// tests/test_w3_provider_misc_regression.cpp — W3 regression guards
// Issues 364, 376, 377, 398 S1/S2
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/algorithm_descriptor.h"
#include "processing/framework/schema_validator.h"
#include "processing/providers/otb_tools/algorithms/otb_segmentation.h"
#include "operators/otb/otb_segmentation_operator.h"
#include "operators/rs/rs_image_fusion_operator.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>

using namespace sicnu::processing;

// 364: validator accepts both string and integer enum forms
TEST_CASE("W3 364: enum validator accepts string and integer", "[w3][364]") {
    AlgorithmDescriptor desc;
    PortDescriptor p;
    p.name = "MODE";
    p.type = DataType::Enum;
    p.enumOptions = {"meanshift","watershed","mprofiles","cc"};
    p.required = true;
    desc.inputs.push_back(p);

    Json::Value okStr(Json::objectValue); okStr["MODE"]="mprofiles";
    REQUIRE(validateParameters(okStr, desc).ok());

    Json::Value okInt(Json::objectValue); okInt["MODE"]=2;
    REQUIRE(validateParameters(okInt, desc).ok());

    Json::Value badStr(Json::objectValue); badStr["MODE"]="not_exist";
    REQUIRE_FALSE(validateParameters(badStr, desc).ok());

    Json::Value badInt(Json::objectValue); badInt["MODE"]=99;
    REQUIRE_FALSE(validateParameters(badInt, desc).ok());

    Json::Value badType(Json::objectValue); badType["MODE"]=true;
    REQUIRE_FALSE(validateParameters(badType, desc).ok());
}

// 364: default string "average" maps to int via adapter logic (simulated)
// We replicate the adapter's lookup directly.
TEST_CASE("W3 364: enum string maps to index (adapter logic)", "[w3][364]") {
    AlgorithmDescriptor desc;
    PortDescriptor p;
    p.name = "ALGORITHM";
    p.type = DataType::Enum;
    p.enumOptions = {"nearest","average","invdist"};
    desc.inputs.push_back(p);
    // simulate adapter string->int conversion
    auto convert = [&](const std::string &s)->int{
        for(size_t i=0;i<p.enumOptions.size();++i) if(p.enumOptions[i]==s) return (int)i;
        return -1;
    };
    REQUIRE(convert("nearest")==0);
    REQUIRE(convert("average")==1);
    REQUIRE(convert("invdist")==2);
    REQUIRE(convert("unknown")==-1);
}

// 377: otb_segmentation modes/per-mode argv
TEST_CASE("W3 377: otb_segmentation no lsms, cc/mprofiles correct keys", "[w3][377]") {
    OtbSegmentationAlgorithm algo;
    algo.initAlgorithm();
    auto defs = algo.parameterDefinitions();
    bool hasLsms=false;
    for(auto *d: defs) if(d->name()=="MODE"){
        auto *enumParam = dynamic_cast<const QgsProcessingParameterEnum*>(d);
        REQUIRE(enumParam!=nullptr);
        REQUIRE(enumParam->options().size()==4);
        REQUIRE_FALSE(enumParam->options().contains("lsms"));
        for(auto &o: enumParam->options()) if(o=="lsms") hasLsms=true;
    }
    REQUIRE_FALSE(hasLsms);
    // check per-mode params exist
    QStringList names; for(auto *p: defs) names<<p->name();
    REQUIRE(names.contains("CC_EXPR"));
    REQUIRE(names.contains("MPROFILES_SIZE"));
    REQUIRE(names.contains("MPROFILES_START"));
    REQUIRE(names.contains("MPROFILES_STEP"));
    REQUIRE(names.contains("MPROFILES_SIGMA"));

    // buildArgs per mode
    auto build = [&](int mode, QVariantMap extra)->QStringList{
        QVariantMap params;
        params["INPUT"]="/tmp/in.tif";
        params["MODE"]=mode;
        params["SPATIAL_RADIUS"]=5;
        params["RANGE_RADIUS"]=15.0;
        params["MIN_REGION_SIZE"]=100;
        params["MAX_ITERATION"]=100;
        params["THRESHOLD"]=0.01;
        params["CC_EXPR"]="(p1b1>0)";
        params["MPROFILES_SIZE"]=5;
        params["MPROFILES_START"]=1;
        params["MPROFILES_STEP"]=1;
        params["MPROFILES_SIGMA"]=1.0;
        params["OUTPUT"]="/tmp/out.shp";
        for(auto it=extra.begin(); it!=extra.end(); ++it) params[it.key()]=it.value();
        QgsProcessingContext ctx;
        // use wrapper's buildArgs via derived access
        struct Tester: public OtbSegmentationAlgorithm{ QStringList call(const QVariantMap &p){ QgsProcessingContext c; return buildArgs(p,c,nullptr);} };
        Tester t; return t.call(params);
    };

    QStringList msArgs=build(0,{});
    REQUIRE(msArgs.contains("-filter.meanshift.spatialr"));
    REQUIRE_FALSE(msArgs.contains("-filter.meanshift.threshold"));

    QStringList wsArgs=build(1,{});
    REQUIRE(wsArgs.contains("-filter.watershed.threshold"));
    REQUIRE_FALSE(wsArgs.contains("-filter.watershed.expr"));

    QStringList mpArgs=build(2,{});
    REQUIRE(mpArgs.contains("-filter.mprofiles.size"));
    REQUIRE(mpArgs.contains("-filter.mprofiles.start"));
    REQUIRE(mpArgs.contains("-filter.mprofiles.step"));
    REQUIRE(mpArgs.contains("-filter.mprofiles.sigma"));
    REQUIRE_FALSE(mpArgs.contains("-filter.mprofiles.threshold"));

    QStringList ccArgs=build(3,{});
    REQUIRE(ccArgs.contains("-filter.cc.expr"));
    REQUIRE_FALSE(ccArgs.contains("-filter.cc.threshold"));
}

// 398 S1: typed defaults round-trip
TEST_CASE("W3 398 S1: integer default emits as number not decimal string", "[w3][398][S1]") {
    PortDescriptor p;
    p.name="POWER";
    p.type=DataType::Integer;
    p.defaultValue="3"; // integer string as stored after fix
    Json::Value schema = p.toJsonSchema();
    REQUIRE(schema["default"].isInt());
    REQUIRE(schema["default"].asInt()==3);

    p.type=DataType::Numeric;
    p.defaultValue="3.000000";
    Json::Value schema2 = p.toJsonSchema();
    REQUIRE(schema2["default"].isNumeric());

    // echo default validates
    AlgorithmDescriptor desc;
    PortDescriptor ip; ip.name="count"; ip.type=DataType::Integer; ip.defaultValue="3"; ip.required=false;
    desc.inputs.push_back(ip);
    Json::Value params(Json::objectValue);
    params["count"]= schema["default"]; // numeric 3
    REQUIRE(validateParameters(params, desc).ok());
    // also string "3" validates
    Json::Value params2(Json::objectValue); params2["count"]="3";
    REQUIRE(validateParameters(params2, desc).ok());
}

// 398 S2: operator schemas declare previously missing params
TEST_CASE("W3 398 S2: otb_segmentation and image_fusion schemas declare missing params", "[w3][398][S2]") {
    sicnu::operators::otb::OtbSegmentationOperator seg;
    Json::Value s = seg.schema();
    REQUIRE(s["properties"].isMember("profileSize"));
    REQUIRE(s["properties"].isMember("startRadius"));
    REQUIRE(s["properties"].isMember("radiusStep"));
    REQUIRE(s["properties"].isMember("sigma"));
    REQUIRE(s["properties"].isMember("ccExpression"));

    sicnu::operators::rs::RsImageFusionOperator fus;
    Json::Value f = fus.schema();
    REQUIRE(f["properties"].isMember("msWeights"));
    REQUIRE(f["properties"]["msWeights"]["type"].asString()=="array");
}
