#include "teaching/lab_readiness.h"

namespace sicnu::teaching {
namespace {

std::string strOf( const Json::Value &v, const char *key )
{
  if ( !v.isObject() || !v.isMember( key ) || !v[key].isString() ) return {};
  return v[key].asString();
}

bool boolOf( const Json::Value &v, const char *key, bool fallback = false )
{
  if ( !v.isObject() || !v.isMember( key ) || !v[key].isBool() ) return fallback;
  return v[key].asBool();
}

void pushItem( std::vector<ReadinessItem> &items, const std::string &id,
               const std::string &category, const std::string &severity,
               const std::string &reasonZh, const std::string &evidence, bool ok )
{
  ReadinessItem it;
  it.id = id;
  it.category = category;
  it.severity = severity;
  it.reasonZh = reasonZh;
  it.evidenceSource = evidence;
  it.ok = ok;
  items.push_back( std::move( it ) );
}

} // namespace

LabReadiness LabReadiness::aggregate( const std::string &labId,
                                      const Json::Value &availabilityLabSlice,
                                      const Json::Value &passportFacts,
                                      const Json::Value &inspectorFacts,
                                      const Json::Value &scientificFlags,
                                      bool offlineMode )
{
  LabReadiness r;
  r.labId = labId;
  r.offlineCapable = offlineMode;

  if ( labId.empty() ) {
    r.issuesZh.push_back( "labId 为空" );
    r.level = ReadinessLevel::Unknown;
    return r;
  }

  bool sawBlock = false;
  bool sawWarn = false;
  bool sawUnknown = false;

  if ( !availabilityLabSlice.isObject() ) {
    pushItem( r.items, "avail.missing", "operator", "unknown",
              "可用性切片缺失", "availability.missing", false );
    sawUnknown = true;
  } else {
    const std::string resolution = strOf( availabilityLabSlice, "resolvable" );
    if ( resolution == "unknown" || resolution.empty() ) {
      pushItem( r.items, "avail.resolve", "prereq", "block",
                "实验引用不可解析", "availability.resolvable", false );
      sawBlock = true;
    } else {
      pushItem( r.items, "avail.resolve", "prereq", "info",
                "实验引用: " + resolution, "availability.resolvable", true );
    }
    const auto &ops = availabilityLabSlice["operators"];
    if ( ops.isArray() ) {
      for ( const auto &op : ops ) {
        const std::string oid = strOf( op, "operator_id" );
        const std::string state = strOf( op, "state" );
        if ( state == "unknown" ) {
          pushItem( r.items, "op." + oid, "operator", "block",
                    "未知算子: " + oid, "availability.operator", false );
          sawBlock = true;
        } else if ( state == "registered_no_capability_note" ) {
          pushItem( r.items, "op." + oid, "operator", "warn",
                    "算子缺能力镜像: " + oid, "availability.operator", true );
          sawWarn = true;
        } else if ( state == "available" ) {
          pushItem( r.items, "op." + oid, "operator", "info",
                    "算子可用: " + oid, "availability.operator", true );
        } else {
          pushItem( r.items, "op." + oid, "operator", "unknown",
                    "算子状态未知: " + oid + "/" + state, "availability.operator", false );
          sawUnknown = true;
        }
      }
    }
    const auto &packs = availabilityLabSlice["data_packs"];
    if ( packs.isArray() ) {
      for ( const auto &p : packs ) {
        const std::string name = strOf( p, "name" );
        const bool present = boolOf( p, "present", false );
        if ( !present ) {
          pushItem( r.items, "pack." + name, "pack", "block",
                    "数据包缺失: " + name, "availability.data_pack", false );
          sawBlock = true;
        } else {
          pushItem( r.items, "pack." + name, "pack", "info",
                    "数据包就绪: " + name, "availability.data_pack", true );
        }
      }
    }
  }

  if ( !passportFacts.isObject() ) {
    pushItem( r.items, "passport.missing", "passport", "unknown",
              "数据护照未注入", "passport.missing", false );
    sawUnknown = true;
  } else if ( passportFacts.isMember( "ok" ) && passportFacts["ok"].isBool()
              && !passportFacts["ok"].asBool() ) {
    pushItem( r.items, "passport.fail", "passport", "block",
              strOf( passportFacts, "reason_zh" ).empty()
                ? "数据护照校验失败"
                : strOf( passportFacts, "reason_zh" ),
              "passport", false );
    sawBlock = true;
  } else if ( passportFacts.isMember( "ok" ) ) {
    pushItem( r.items, "passport.ok", "passport", "info", "数据护照通过", "passport", true );
  }

  if ( !inspectorFacts.isObject() ) {
    pushItem( r.items, "inspector.missing", "inspector", "unknown",
              "检查器事实未注入", "inspector.missing", false );
    sawUnknown = true;
  } else if ( inspectorFacts.isMember( "blocking" ) && inspectorFacts["blocking"].isBool()
              && inspectorFacts["blocking"].asBool() ) {
    pushItem( r.items, "inspector.block", "inspector", "block",
              strOf( inspectorFacts, "reason_zh" ).empty()
                ? "检查器阻断"
                : strOf( inspectorFacts, "reason_zh" ),
              "inspector", false );
    sawBlock = true;
  } else if ( inspectorFacts.isMember( "warnings" ) && inspectorFacts["warnings"].isArray()
              && !inspectorFacts["warnings"].empty() ) {
    pushItem( r.items, "inspector.warn", "inspector", "warn",
              "检查器有警告", "inspector", true );
    sawWarn = true;
  } else {
    pushItem( r.items, "inspector.ok", "inspector", "info", "检查器无阻断", "inspector", true );
  }

  if ( scientificFlags.isObject() ) {
    if ( boolOf( scientificFlags, "conflict", false ) ) {
      pushItem( r.items, "sci.conflict", "scientific", "block",
                strOf( scientificFlags, "reason_zh" ).empty()
                  ? "科学合同冲突"
                  : strOf( scientificFlags, "reason_zh" ),
                "scientific.conflict", false );
      sawBlock = true;
    } else if ( boolOf( scientificFlags, "unknown", false ) ) {
      pushItem( r.items, "sci.unknown", "scientific", "unknown",
                "科学校验未知", "scientific.unknown", false );
      sawUnknown = true;
    } else {
      pushItem( r.items, "sci.ok", "scientific", "info", "无科学冲突", "scientific", true );
    }
  }

  if ( offlineMode ) {
    pushItem( r.items, "offline", "offline", "info",
              "离线实验模式（不依赖外网）", "offline.mode", true );
  }

  if ( sawBlock ) r.level = ReadinessLevel::Blocked;
  else if ( sawUnknown ) r.level = ReadinessLevel::Unknown;
  else if ( sawWarn ) r.level = ReadinessLevel::ReadyWithWarnings;
  else r.level = ReadinessLevel::Ready;
  return r;
}

Json::Value LabReadiness::toJson() const
{
  Json::Value root( Json::objectValue );
  root["schema"] = schema;
  root["lab_id"] = labId;
  root["level"] = readinessLevelWire( level );
  root["level_zh"] = readinessLevelLabelZh( level );
  root["offline_capable"] = offlineCapable;
  Json::Value issues( Json::arrayValue );
  for ( const auto &i : issuesZh ) issues.append( i );
  root["issues_zh"] = issues;
  Json::Value arr( Json::arrayValue );
  for ( const auto &it : items ) {
    Json::Value o( Json::objectValue );
    o["id"] = it.id;
    o["category"] = it.category;
    o["severity"] = it.severity;
    o["reason_zh"] = it.reasonZh;
    o["evidence_source"] = it.evidenceSource;
    o["ok"] = it.ok;
    arr.append( o );
  }
  root["items"] = arr;
  return root;
}

} // namespace sicnu::teaching
