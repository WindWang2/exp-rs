// src/agent/harness/lab_diagnostics.cpp
#include "lab_diagnostics.h"

#include "harness_actions.h"

#include "help/help_id.h"

#include <cmath>
#include <utility>

namespace sicnu::agent::harness {

namespace {

using sicnu::help::DiagnosticFamily;
using sicnu::help::HelpId;

LabDiagnosis makeDiagnosis( const std::string &signature, const std::string &symptomZh,
                            const char *causeZh, const char *verifyZh, const char *actionKey,
                            DiagnosticFamily family, const char *originCode )
{
  LabDiagnosis d;
  d.matched = true;
  d.signature = signature;
  d.symptomZh = symptomZh;
  d.causeZh = causeZh;
  d.verifyZh = verifyZh;
  d.actionKey = actionKey;
  d.diagnosticFamily = sicnu::help::diagnosticFamilyName( family ).toStdString();
  d.diagnosticCode = originCode;
  d.helpId = HelpId::diagnosticId( family, originCode ).toStdString();
  return d;
}

} // namespace

Json::Value LabDiagnosis::toJson() const
{
  Json::Value v( Json::objectValue );
  v["matched"] = matched;
  v["signature"] = signature;
  v["symptom_zh"] = symptomZh;
  v["cause_zh"] = causeZh;
  v["verify_zh"] = verifyZh;
  if ( !actionKey.empty() )
    v["verify_action"] = actionKey;
  if ( !diagnosticCode.empty() )
  {
    v["diagnostic_family"] = diagnosticFamily;
    v["diagnostic_code"] = diagnosticCode;
    v["help_id"] = helpId;
  }
  return v;
}

LabObservation parseLabObservation( const Json::Value &doc )
{
  LabObservation obs;
  if ( !doc.isObject() )
    return obs;
  obs.present = true;
  obs.kind = doc.get( "kind", "" ).asString();
  obs.indexName = doc.get( "index", "" ).asString();

  const auto number = [ &doc ]( const char *key, double fallback ) {
    return doc.isMember( key ) && doc[key].isNumeric() ? doc[key].asDouble() : fallback;
  };
  obs.min = number( "min", 0.0 );
  obs.max = number( "max", 0.0 );
  obs.mean = number( "mean", 0.0 );
  obs.nodataFraction = number( "nodata_fraction", 0.0 );
  obs.validFraction = number( "valid_fraction", 1.0 );
  obs.density = number( "density", 1.0 );
  obs.kappa = number( "kappa", -2.0 );
  obs.pixelSizeA = number( "pixel_size_a", 0.0 );
  obs.pixelSizeB = number( "pixel_size_b", 0.0 );
  obs.crsA = doc.get( "crs_a", "" ).asString();
  obs.crsB = doc.get( "crs_b", "" ).asString();
  return obs;
}

LabDiagnosis diagnoseLabObservation( const LabObservation &observation )
{
  if ( !observation.present )
    return LabDiagnosis{};

  // Most specific first. Accuracy facts beat geometry; geometry beats
  // content, because a wrong frame invalidates every content reading.
  if ( observation.kind == "accuracy" && observation.kappa >= -1.0 &&
       std::fabs( observation.kappa ) < 0.1 )
  {
    return makeDiagnosis(
      "kappa_near_zero",
      "分类精度评价的 Kappa 系数接近 0，分类结果处于随机水平甚至更差。",
      "最可能的原因是参考样本与分类体系没有对齐：类别名不一致、样本标注错误或样本量过少。",
      "用 check_training 核对参考样本的类别映射与样本量：抽查 3–5 个样本位置，逐一看标注是否与真实地物一致。",
      "check_training", sicnu::help::DiagnosticFamily::Harness, "TRAINING_INVALID" );
  }

  if ( !observation.crsA.empty() && !observation.crsB.empty() && observation.crsA != observation.crsB )
  {
    return makeDiagnosis(
      "crs_mismatch",
      "两期（或两幅）数据的坐标系不一致，叠加显示或运算时位置对不上。",
      "最可能的原因是其中一幅影像没有投影（或投影定义丢失），未统一到参考坐标系。",
      "先只做一件事：用 reproject_to_reference 把其中一幅重投影到另一幅的坐标系，再叠加目视检查。",
      "reproject_to_reference", sicnu::help::DiagnosticFamily::Harness, "CRS_MISMATCH" );
  }

  if ( observation.pixelSizeA > 0.0 && observation.pixelSizeB > 0.0 &&
       std::fabs( observation.pixelSizeA / observation.pixelSizeB - 1.0 ) > 0.01 )
  {
    return makeDiagnosis(
      "scale_stripes",
      "两幅输入的分辨率不一致（像元大小相差超过 1%）——若输出出现规则的条带或锯齿边界，最可能由此累积。",
      "最可能的原因是低分辨率输入在逐块重采样时把错位累积成了条带。",
      "用 align_to_reference 把低分辨率输入重采样到与参考一致后再重新运算，观察条带是否消失。",
      "align_to_reference", sicnu::help::DiagnosticFamily::Harness, "GRID_MISMATCH" );
  }

  if ( observation.nodataFraction >= 0.999 || observation.validFraction <= 0.001 )
  {
    return makeDiagnosis(
      "all_nodata",
      "输出全部为 NoData，没有任何有效像元。",
      "最可能的原因是输入的 NoData 值没有正确声明（或掩膜把所有像元都排除了），导致按掩膜运算后全为空。",
      "用 check_dataset 查看该数据的 nodata 元数据是否与其真实填充值一致。",
      "check_dataset", sicnu::help::DiagnosticFamily::Preflight, "nodata_declared" );
  }

  if ( observation.kind == "raster_stats" && !observation.indexName.empty() &&
       observation.max < 0.0 )
  {
    return makeDiagnosis(
      "all_negative_index",
      observation.indexName + " 的所有像元值都为负（最大值 " + std::to_string( observation.max ) +
        " < 0）。对有植被覆盖的场景而言，该指数不应整体为负。",
      "最可能的原因是红光与近红外两个波段选反了（角色互换），公式的分子被整体反号。",
      "用 inspect_bands 核对红光（RED）与近红外（NIR）波段的角色后，交换波段重新计算一遍即可验证。",
      "inspect_bands", sicnu::help::DiagnosticFamily::Harness, "BAND_ROLE_UNRESOLVED" );
  }

  if ( observation.kind == "mask" && observation.density <= 1e-6 )
  {
    return makeDiagnosis(
      "blank_change_mask",
      "变化检测掩膜为全空白：没有一个像元被判定为变化。",
      "最可能的原因是两期影像辐射基准不一致（未定标/未归一化）把差值整体压到了阈值以下，也可能是阈值设置过紧。",
      "用 normalize_radiometry 对两期影像做辐射归一化后，再按原阈值重算变化掩膜。",
      "normalize_radiometry", sicnu::help::DiagnosticFamily::Harness, "OUTPUT_INVALID" );
  }

  return LabDiagnosis{}; // honest miss: no signature matched
}

} // namespace sicnu::agent::harness
