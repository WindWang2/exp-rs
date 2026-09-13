// src/agent/harness/capability_pages.cpp
#include "capability_pages.h"

#include "capability_catalog.h"
#include "capability_relations.h"

#include <algorithm>
#include <map>
#include <sstream>

namespace sicnu::agent::harness {

namespace {

std::string joinList( const Json::Value &items, const std::string &separator )
{
  std::string out;
  for ( const Json::Value &item : items )
  {
    if ( !item.isString() )
      continue;
    if ( !out.empty() )
      out += separator;
    out += item.asString();
  }
  return out;
}

std::string determinismNote( const std::string &grade, bool stochastic )
{
  if ( stochastic )
    return "随机性（跨运行可能不同，注意可复现性）";
  if ( grade == "tolerance" )
    return "容差级（并行执行与串行结果在 1e-6 相对容差内一致）";
  if ( grade == "bit_exact" )
    return "逐位一致（bit_exact）";
  return grade.empty() ? "未知" : grade;
}

/// One operator section inside a family page.
void appendOperator( std::ostringstream &out, const std::string &id )
{
  CapabilityCatalog &catalog = CapabilityCatalog::instance();
  CapabilityRelations &relations = CapabilityRelations::instance();
  const Json::Value block = catalog.capability( id );

  const std::string grade = catalog.determinismOf( id );
  const bool stochastic = catalog.isStochastic( id );

  out << "## " << id << "\n\n";

  if ( block[ "summary" ].isString() && !block[ "summary" ].asString().empty() )
    out << block[ "summary" ].asString() << "\n\n";

  out << "- 确定性：" << determinismNote( grade, stochastic ) << "\n";
  out << "- 模态：" << joinList( block[ "modality" ], "、" ) << "\n";
  const Json::Value &bandRoles = block[ "band_roles" ];
  if ( bandRoles.isObject() && !bandRoles.empty() )
  {
    std::string roles;
    for ( const std::string &role : bandRoles.getMemberNames() )
    {
      if ( !roles.empty() )
        roles += "、";
      roles += role + "×" + std::to_string( bandRoles[ role ].asInt() );
    }
    out << "- 波段角色要求：" << roles << "\n";
  }
  const Json::Value &crs = block[ "crs" ];
  if ( crs.isObject() )
  {
    if ( crs[ "requires_shared_grid" ].asBool() )
      out << "- 网格要求：输入必须位于同一网格（先用 "
          << relations.gridFixer() << " 对齐）\n";
    if ( crs[ "requires_projected" ].asBool() )
      out << "- 坐标系要求：需要投影坐标系\n";
  }

  // io contract (compact, mechanical)
  const Json::Value &io = block[ "io" ];
  if ( io.isObject() )
  {
    auto renderRows = []( std::ostringstream &sink, const Json::Value &rows,
                          const std::string &label ) {
      std::string rendered;
      for ( const Json::Value &row : rows )
      {
        if ( !row.isObject() || !row[ "name" ].isString() )
          continue;
        if ( !rendered.empty() )
          rendered += "、";
        rendered += row[ "name" ].asString();
        if ( row[ "data_kind" ].isString() )
          rendered += "（" + row[ "data_kind" ].asString() + "）";
        else if ( row[ "type" ].isString() )
          rendered += "（" + row[ "type" ].asString() + "）";
      }
      if ( !rendered.empty() )
        sink << "- " << label << "：" << rendered << "\n";
    };
    renderRows( out, io[ "inputs" ], "输入" );
    renderRows( out, io[ "outputs" ], "输出" );
    renderRows( out, io[ "parameters" ], "参数" );
  }

  if ( block[ "prerequisites" ].isArray() && !block[ "prerequisites" ].empty() )
    out << "- 前置条件：" << joinList( block[ "prerequisites" ], "；" ) << "\n";
  if ( block[ "limitations" ].isArray() && !block[ "limitations" ].empty() )
    out << "- 局限：" << joinList( block[ "limitations" ], "；" ) << "\n";

  const Json::Value &applicability = block[ "applicability" ];
  if ( applicability.isObject() && !applicability.empty() )
  {
    if ( applicability[ "land_cover" ].isArray() && !applicability[ "land_cover" ].empty() )
      out << "- 适用地物：" << joinList( applicability[ "land_cover" ], "、" ) << "\n";
    if ( applicability[ "scenes" ].isArray() && !applicability[ "scenes" ].empty() )
      out << "- 适用场景：" << joinList( applicability[ "scenes" ], "、" ) << "\n";
    if ( applicability[ "notes" ].isString() && !applicability[ "notes" ].asString().empty() )
      out << "- 适用性备注：" << applicability[ "notes" ].asString() << "\n";
  }

  const Json::Value &failureModes = block[ "failure_modes" ];
  if ( failureModes.isArray() && !failureModes.empty() )
  {
    out << "- 失败模式：\n";
    for ( const Json::Value &failure : failureModes )
    {
      out << "  - `" << failure[ "code" ].asString() << "` — "
          << failure[ "when" ].asString() << "。处置：" << failure[ "remedy" ].asString() << "\n";
    }
  }

  const Json::Value &teaching = block[ "teaching_use" ];
  if ( teaching.isObject() && !teaching.empty() )
  {
    if ( teaching[ "concepts" ].isArray() && !teaching[ "concepts" ].empty() )
      out << "- 教学概念：" << joinList( teaching[ "concepts" ], "、" ) << "\n";
    if ( teaching[ "courses" ].isArray() && !teaching[ "courses" ].empty() )
      out << "- 适用课程：" << joinList( teaching[ "courses" ], "、" ) << "\n";
    if ( teaching[ "exercise" ].isString() && !teaching[ "exercise" ].asString().empty() )
      out << "- 典型练习：" << teaching[ "exercise" ].asString() << "\n";
  }

  // relation context (from the graph, not prose)
  const Json::Value links = chainFrom( id );
  std::string upstream;
  for ( const Json::Value &edge : links[ "upstream" ] )
  {
    if ( !upstream.empty() )
      upstream += "、";
    upstream += edge[ "from" ].asString();
  }
  std::string downstream;
  for ( const Json::Value &edge : links[ "downstream" ] )
  {
    if ( !downstream.empty() )
      downstream += "、";
    downstream += edge[ "to" ].asString();
  }
  if ( !upstream.empty() )
    out << "- 可接上游：" << upstream << "\n";
  if ( !downstream.empty() )
    out << "- 可接下游：" << downstream << "\n";

  out << "\n";
}

std::string renderFamilyPage( const std::string &family )
{
  CapabilityCatalog &catalog = CapabilityCatalog::instance();
  const std::vector<std::string> ids = catalog.byFamily( family );
  std::ostringstream out;
  out << "<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。"
         " 修改请改对应 sidecar 后重新生成。 -->\n\n";
  out << "# " << familyDisplayName( family ) << "（" << family << "）\n\n";
  out << "共 " << ids.size() << " 个算子。数据源："
         "`data/processing/algorithm_meta/capability/`，本页为生成产物。\n\n";
  for ( const std::string &id : ids )
    appendOperator( out, id );
  return out.str();
}

std::string renderIndexPage()
{
  CapabilityCatalog &catalog = CapabilityCatalog::instance();
  CapabilityRelations &relations = CapabilityRelations::instance();
  std::ostringstream out;
  out << "<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。 -->\n\n";
  out << "# RS 算子能力知识索引\n\n";
  const std::vector<std::string> ids = catalog.entryIds();
  out << "覆盖 " << ids.size() << " 个 `rs:` 算子（要求 111/111）。"
         "逐算子元数据见 `data/processing/algorithm_meta/capability/`；"
         "关系图见同目录 `capability_relations.json`。\n\n";
  out << "| 算子族 | 数量 | 页面 |\n|---|---|---|\n";
  for ( const std::string &family : capabilityFamilies() )
  {
    out << "| " << familyDisplayName( family ) << "（" << family << "） | "
        << catalog.byFamily( family ).size() << " | [capability-" << family
        << ".md](capability-" << family << ".md) |\n";
  }
  out << "\n## 查询 API（D9 消费，保持稳定）\n\n";
  out << "`byFamily` / `byInputModality` / `chainFrom` / `requiresGrid` / `determinismOf` /"
         " `harness:compose_chain`；清单页硬预算 64 KiB，失败模式目录 8 KiB。\n\n";
  out << "## 需注意可复现性的算子\n\n";
  out << "容差级（tolerance，ADR 0124）或随机性算子：\n\n";
  size_t surfaced = 0;
  for ( const std::string &id : ids )
  {
    if ( catalog.isStochastic( id ) || catalog.determinismOf( id ) == "tolerance" )
    {
      out << "- " << id << "（" << determinismNote( catalog.determinismOf( id ),
                                                    catalog.isStochastic( id ) )
          << "）\n";
      ++surfaced;
    }
  }
  if ( surfaced == 0 )
    out << "（无）\n";
  return out.str();
}

} // namespace

std::string familyDisplayName( const std::string &family )
{
  static const std::map<std::string, std::string> kNames = {
    { "optical", "光学预处理" },
    { "spectral", "光谱指数与波段运算" },
    { "sar", "雷达 SAR 处理" },
    { "terrain", "地形分析" },
    { "temporal", "时序分析" },
    { "classification", "分类与机器学习" },
    { "change", "变化检测" },
    { "obia", "面向对象影像分析" },
    { "hyperspectral", "高光谱分析" },
    { "raster_spatial", "栅格空间分析" },
    { "io", "数据导入" },
  };
  const auto it = kNames.find( family );
  return it == kNames.end() ? family : it->second;
}

std::vector<KnowledgePage> renderCapabilityKnowledgePages()
{
  auto &catalog = CapabilityCatalog::instance();
  auto &relations = CapabilityRelations::instance();
  if ( !catalog.loaded() )
    catalog.reload();
  if ( !relations.loaded() )
    relations.reload();

  std::vector<KnowledgePage> pages;
  for ( const std::string &family : capabilityFamilies() )
  {
    KnowledgePage page;
    page.relativePath = "pi/knowledge/capability-" + family + ".md";
    page.content = renderFamilyPage( family );
    pages.push_back( std::move( page ) );
  }
  KnowledgePage index;
  index.relativePath = "pi/knowledge/capability-index.md";
  index.content = renderIndexPage();
  pages.push_back( std::move( index ) );
  std::sort( pages.begin(), pages.end(),
             []( const KnowledgePage &a, const KnowledgePage &b ) {
               return a.relativePath < b.relativePath;
             } );
  return pages;
}

} // namespace sicnu::agent::harness
