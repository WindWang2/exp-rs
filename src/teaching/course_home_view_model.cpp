#include "teaching/course_home_view_model.h"

#include <map>
#include <set>

namespace sicnu::teaching {
namespace {

std::string strOf( const Json::Value &v, const char *key )
{
  if ( !v.isObject() || !v.isMember( key ) || !v[key].isString() ) return {};
  return v[key].asString();
}

int intOf( const Json::Value &v, const char *key, int fallback = 0 )
{
  if ( !v.isObject() || !v.isMember( key ) ) return fallback;
  if ( v[key].isInt() ) return v[key].asInt();
  if ( v[key].isNumeric() ) return static_cast<int>( v[key].asDouble() );
  return fallback;
}

bool boolOf( const Json::Value &v, const char *key, bool fallback = false )
{
  if ( !v.isObject() || !v.isMember( key ) || !v[key].isBool() ) return fallback;
  return v[key].asBool();
}

std::set<std::string> completedSet( const Json::Value &progressSummary )
{
  std::set<std::string> out;
  if ( !progressSummary.isObject() ) return out;
  const auto &ids = progressSummary["completed_lab_ids"];
  if ( !ids.isArray() ) return out;
  for ( const auto &id : ids ) {
    if ( id.isString() ) out.insert( id.asString() );
  }
  return out;
}

struct AvailLab {
  std::string resolution = "unknown";
  bool anyUnknownOp = false;
  bool anyWarnOp = false;
  bool anyMissingPack = false;
  bool seen = false;
  std::vector<std::string> blockers;
  std::vector<std::string> warnings;
};

std::map<std::string, AvailLab> indexAvailability( const Json::Value &availability )
{
  std::map<std::string, AvailLab> out;
  if ( !availability.isObject() ) return out;
  const auto &modules = availability["modules"];
  if ( !modules.isArray() ) return out;
  for ( const auto &mod : modules ) {
    const auto &labs = mod["labs"];
    if ( !labs.isArray() ) continue;
    for ( const auto &lab : labs ) {
      if ( !lab.isObject() || !lab.isMember( "lab_id" ) || !lab["lab_id"].isString() ) continue;
      AvailLab a;
      a.seen = true;
      a.resolution = strOf( lab, "resolvable" );
      if ( a.resolution.empty() ) a.resolution = "unknown";
      if ( a.resolution == "unknown" ) {
        a.blockers.push_back( "实验引用不可解析（evid:availability.resolvable）" );
      }
      const auto &ops = lab["operators"];
      if ( ops.isArray() ) {
        for ( const auto &op : ops ) {
          const std::string state = strOf( op, "state" );
          const std::string oid = strOf( op, "operator_id" );
          if ( state == "unknown" ) {
            a.anyUnknownOp = true;
            a.blockers.push_back( "算子不可用: " + oid + "（evid:availability.operator）" );
          } else if ( state == "registered_no_capability_note" ) {
            a.anyWarnOp = true;
            a.warnings.push_back( "算子缺能力镜像: " + oid + "（evid:availability.operator）" );
          }
        }
      }
      const auto &packs = lab["data_packs"];
      if ( packs.isArray() ) {
        for ( const auto &p : packs ) {
          const std::string name = strOf( p, "name" );
          const bool present = boolOf( p, "present", false );
          if ( !present ) {
            a.anyMissingPack = true;
            a.blockers.push_back( "数据包缺失: " + name + "（evid:availability.data_pack）" );
          }
        }
      }
      out[lab["lab_id"].asString()] = std::move( a );
    }
  }
  return out;
}

LabUiStatus deriveLabStatus( bool completed, const AvailLab *avail, bool prereqBlocked )
{
  if ( completed ) return LabUiStatus::Completed;
  if ( prereqBlocked ) return LabUiStatus::Unavailable;
  if ( !avail || !avail->seen ) return LabUiStatus::Unknown;
  if ( avail->resolution == "unknown" || avail->anyUnknownOp || avail->anyMissingPack )
    return LabUiStatus::Unavailable;
  if ( avail->anyWarnOp ) return LabUiStatus::Ready;
  return LabUiStatus::Ready;
}

} // namespace

CourseHomeViewModel CourseHomeViewModel::fromDocuments( const Json::Value &manifest,
                                                        const Json::Value &progressSummary,
                                                        const Json::Value &availability,
                                                        ExperienceMode mode )
{
  CourseHomeViewModel vm;
  vm.mode = mode;
  if ( !manifest.isObject() ) {
    vm.issuesZh.push_back( "课程清单缺失或非对象（fail-closed）" );
    return vm;
  }
  const std::string schema = strOf( manifest, "schema" );
  if ( schema != "sicnu.curriculum/1" ) {
    vm.issuesZh.push_back( "未知课程 schema: " + ( schema.empty() ? "<empty>" : schema ) );
    return vm;
  }
  vm.courseId = strOf( manifest, "id" );
  vm.titleZh = strOf( manifest, "title_zh" );
  if ( vm.titleZh.empty() ) vm.titleZh = strOf( manifest, "title" );
  vm.audienceZh = strOf( manifest, "audience_zh" );

  const auto done = completedSet( progressSummary );
  if ( progressSummary.isObject() && progressSummary.isMember( "overall_percent" )
       && progressSummary["overall_percent"].isNumeric() ) {
    vm.overallPercent = static_cast<int>( progressSummary["overall_percent"].asDouble() );
  }
  const auto availMap = indexAvailability( availability );
  const bool availMissing = !availability.isObject()
                            || strOf( availability, "schema" ) != "sicnu.curriculum.availability/1";

  // Prerequisite module completion: a module is "satisfied" when all its
  // non-optional labs are in the completed set (or it has zero labs).
  std::map<std::string, bool> moduleSatisfied;
  const auto &mods = manifest["modules"];
  if ( !mods.isArray() ) {
    vm.issuesZh.push_back( "课程清单缺少 modules 数组" );
    return vm;
  }

  // First pass: build cards without prereq status.
  for ( const auto &mod : mods ) {
    if ( !mod.isObject() ) continue;
    CourseModuleCard mc;
    mc.moduleId = strOf( mod, "id" );
    mc.index = intOf( mod, "index" );
    mc.titleZh = strOf( mod, "title_zh" );
    if ( mc.titleZh.empty() ) mc.titleZh = strOf( mod, "title" );
    mc.summaryZh = strOf( mod, "summary_zh" );
    mc.estimatedMinutes = intOf( mod, "estimated_effort_minutes" );
    mc.optional = boolOf( mod, "optional", false );
    const auto &outs = mod["learning_outcomes"];
    if ( outs.isArray() ) {
      for ( const auto &o : outs )
        if ( o.isString() ) mc.learningOutcomes.push_back( o.asString() );
    }
    const auto &pre = mod["prerequisite_modules"];
    if ( pre.isArray() ) {
      for ( const auto &p : pre )
        if ( p.isString() ) mc.prerequisiteModules.push_back( p.asString() );
    }
    const auto &labs = mod["labs"];
    if ( labs.isArray() ) {
      for ( const auto &labRef : labs ) {
        if ( !labRef.isObject() ) continue;
        CourseLabCard lc;
        lc.labId = strOf( labRef, "lab_id" );
        lc.moduleId = mc.moduleId;
        lc.role = strOf( labRef, "role" );
        if ( lc.role.empty() ) lc.role = "core";
        lc.estimatedMinutes = intOf( labRef, "estimated_effort_minutes" );
        const auto &packs = labRef["required_data_packs"];
        if ( packs.isArray() ) {
          for ( const auto &p : packs )
            if ( p.isString() ) lc.requiredDataPacks.push_back( p.asString() );
        }
        lc.learningGoals = mc.learningOutcomes; // shared module goals (no restatement)
        const bool completed = done.count( lc.labId ) > 0;
        const AvailLab *ap = nullptr;
        auto it = availMap.find( lc.labId );
        if ( it != availMap.end() ) ap = &it->second;
        if ( availMissing ) {
          lc.status = completed ? LabUiStatus::Completed : LabUiStatus::Unknown;
          lc.warningsZh.push_back( "可用性报告缺失 → UNKNOWN（evid:availability.missing）" );
        } else if ( ap ) {
          lc.resolution = ap->resolution;
          lc.resolvable = ap->resolution != "unknown";
          lc.blockersZh = ap->blockers;
          lc.warningsZh = ap->warnings;
          lc.status = deriveLabStatus( completed, ap, false );
        } else {
          lc.status = completed ? LabUiStatus::Completed : LabUiStatus::Unknown;
          lc.warningsZh.push_back( "可用性报告未覆盖该实验（evid:availability.gap）" );
        }
        // Title stays id until UI enriches from LabSpec (projection must not invent).
        lc.titleZh = lc.labId;
        if ( completed ) ++mc.labsDone;
        ++mc.labsTotal;
        mc.labs.push_back( std::move( lc ) );
      }
    }
    // Module satisfaction for DAG (core labs only).
    bool sat = true;
    for ( const auto &lc : mc.labs ) {
      if ( lc.role == "optional" ) continue;
      if ( done.count( lc.labId ) == 0 ) { sat = false; break; }
    }
    if ( mc.labsTotal == 0 ) sat = true;
    moduleSatisfied[mc.moduleId] = sat;
    vm.modules.push_back( std::move( mc ) );
  }

  // Second pass: apply prerequisite module blockers.
  for ( auto &mc : vm.modules ) {
    bool prereqBlocked = false;
    for ( const auto &pid : mc.prerequisiteModules ) {
      auto it = moduleSatisfied.find( pid );
      if ( it == moduleSatisfied.end() || !it->second ) {
        prereqBlocked = true;
        for ( auto &lc : mc.labs ) {
          if ( lc.status == LabUiStatus::Completed ) continue;
          lc.blockersZh.push_back( "先修模块未完成: " + pid + "（evid:curriculum.prereq）" );
          if ( lc.status != LabUiStatus::Unavailable )
            lc.status = LabUiStatus::Unavailable;
        }
        break;
      }
    }
    (void) prereqBlocked;
    // Module rollup status.
    if ( mc.labsTotal == 0 ) {
      mc.status = LabUiStatus::Unknown;
    } else if ( mc.labsDone == mc.labsTotal ) {
      mc.status = LabUiStatus::Completed;
    } else {
      bool anyReady = false, anyUnavail = false, anyUnknown = false, anyProgress = false;
      for ( const auto &lc : mc.labs ) {
        if ( lc.status == LabUiStatus::Completed ) continue;
        if ( lc.status == LabUiStatus::Ready ) anyReady = true;
        else if ( lc.status == LabUiStatus::Unavailable ) anyUnavail = true;
        else if ( lc.status == LabUiStatus::Unknown ) anyUnknown = true;
        else anyProgress = true;
      }
      if ( anyProgress ) mc.status = LabUiStatus::InProgress;
      else if ( anyReady ) mc.status = LabUiStatus::Ready;
      else if ( anyUnavail && !anyReady ) mc.status = LabUiStatus::Unavailable;
      else if ( anyUnknown ) mc.status = LabUiStatus::Unknown;
      else mc.status = LabUiStatus::NotStarted;
    }
  }

  // Continue entry: first non-completed lab that is Ready (or Unknown if no avail).
  for ( const auto &mc : vm.modules ) {
    for ( const auto &lc : mc.labs ) {
      if ( lc.status == LabUiStatus::Completed ) continue;
      if ( lc.status == LabUiStatus::Unavailable ) continue;
      vm.continueLabId = lc.labId;
      vm.continueModuleId = lc.moduleId;
      break;
    }
    if ( !vm.continueLabId.empty() ) break;
  }

  vm.ok = vm.issuesZh.empty();
  return vm;
}

Json::Value CourseHomeViewModel::toJson() const
{
  Json::Value root( Json::objectValue );
  root["schema"] = schema;
  root["ok"] = ok;
  root["course_id"] = courseId;
  root["title_zh"] = titleZh;
  root["audience_zh"] = audienceZh;
  root["overall_percent"] = overallPercent;
  root["mode"] = experienceModeWire( mode );
  root["continue_lab_id"] = continueLabId;
  root["continue_module_id"] = continueModuleId;
  Json::Value issues( Json::arrayValue );
  for ( const auto &i : issuesZh ) issues.append( i );
  root["issues_zh"] = issues;
  Json::Value mods( Json::arrayValue );
  for ( const auto &mc : modules ) {
    Json::Value m( Json::objectValue );
    m["module_id"] = mc.moduleId;
    m["index"] = mc.index;
    m["title_zh"] = mc.titleZh;
    m["summary_zh"] = mc.summaryZh;
    m["estimated_minutes"] = mc.estimatedMinutes;
    m["optional"] = mc.optional;
    m["labs_done"] = mc.labsDone;
    m["labs_total"] = mc.labsTotal;
    m["status"] = labUiStatusWire( mc.status );
    m["status_zh"] = labUiStatusLabelZh( mc.status );
    m["status_icon"] = labUiStatusIconToken( mc.status );
    Json::Value outcomes( Json::arrayValue );
    for ( const auto &o : mc.learningOutcomes ) outcomes.append( o );
    m["learning_outcomes"] = outcomes;
    Json::Value prereqs( Json::arrayValue );
    for ( const auto &p : mc.prerequisiteModules ) prereqs.append( p );
    m["prerequisite_modules"] = prereqs;
    Json::Value labs( Json::arrayValue );
    for ( const auto &lc : mc.labs ) {
      Json::Value l( Json::objectValue );
      l["lab_id"] = lc.labId;
      l["module_id"] = lc.moduleId;
      l["title_zh"] = lc.titleZh;
      l["role"] = lc.role;
      l["estimated_minutes"] = lc.estimatedMinutes;
      l["status"] = labUiStatusWire( lc.status );
      l["status_zh"] = labUiStatusLabelZh( lc.status );
      l["status_icon"] = labUiStatusIconToken( lc.status );
      l["resolvable"] = lc.resolvable;
      l["resolution"] = lc.resolution;
      Json::Value packs( Json::arrayValue );
      for ( const auto &p : lc.requiredDataPacks ) packs.append( p );
      l["required_data_packs"] = packs;
      Json::Value blockers( Json::arrayValue );
      for ( const auto &b : lc.blockersZh ) blockers.append( b );
      l["blockers_zh"] = blockers;
      Json::Value warnings( Json::arrayValue );
      for ( const auto &w : lc.warningsZh ) warnings.append( w );
      l["warnings_zh"] = warnings;
      labs.append( l );
    }
    m["labs"] = labs;
    mods.append( m );
  }
  root["modules"] = mods;
  return root;
}

} // namespace sicnu::teaching
