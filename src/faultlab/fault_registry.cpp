// fault_registry.cpp — the fault-family catalog (data only; transforms live
// in fault_transforms.cpp). Ten required fault families from the track
// brief, with "grid shift / CRS mismatch" and "temporal shuffle/gap" split
// into per-variant rows because each variant has a distinct diagnosis and
// observable profile.
#include "fault_registry.h"

namespace sicnu::faultlab
{

const char *faultDomainName( FaultDomain domain )
{
    switch ( domain )
    {
        case FaultDomain::Metadata:
            return "metadata";
        case FaultDomain::Geometry:
            return "geometry";
        case FaultDomain::Temporal:
            return "temporal";
        case FaultDomain::Ml:
            return "ml";
        case FaultDomain::Artifact:
            return "artifact";
    }
    return "unknown";
}

bool FaultFamilyInfo::hasParam( const std::string &name ) const
{
    for ( const auto &param : requiredParams )
    {
        if ( param == name )
        {
            return true;
        }
    }
    for ( const auto &param : optionalParams )
    {
        if ( param == name )
        {
            return true;
        }
    }
    return false;
}

const std::vector<FaultFamilyInfo> &faultFamilyCatalog()
{
    static const std::vector<FaultFamilyInfo> catalog = {
        { "band_role_swap",
          "Band role swap",
          "波段角色互换",
          FaultDomain::Metadata,
          "major",
          { "LO-03", "LO-04" },
          "all_negative_index",
          { "band_roles", "index_mean" },
          { "role_a", "role_b" },
          {},
          "temp_copy",
          false },
        { "omit_quality_mask",
          "Omit quality mask",
          "遗漏质量掩膜",
          FaultDomain::Metadata,
          "major",
          { "LO-05" },
          kDiagnosisUnmatched,
          { "band_count", "band_roles", "valid_fraction" },
          {},
          { "role" },
          "temp_copy",
          false },
        { "wrong_scale_offset",
          "Wrong scale/offset",
          "错误的缩放/偏移",
          FaultDomain::Metadata,
          "major",
          { "LO-06" },
          kDiagnosisUnmatched,
          { "band.mean", "band.min", "band.max", "band.scale", "band.offset" },
          { "role", "gain" },
          { "offset", "metadata" },
          "temp_copy",
          false },
        { "nodata_as_data",
          "NoData-as-data",
          "无效值当作有效数据",
          FaultDomain::Metadata,
          "major",
          { "LO-07" },
          kDiagnosisUnmatched,
          { "nodata_fraction", "finite_fraction", "band.mean" },
          {},
          { "fill_value" },
          "temp_copy",
          false },
        { "grid_shift",
          "Grid shift",
          "网格位移",
          FaultDomain::Geometry,
          "major",
          { "LO-08" },
          kDiagnosisUnmatched, // stripes are a resampling symptom the
                               // observation level cannot see; the origin
                               // delta observable is the detection
          { "geo_transform.origin_x", "geo_transform.origin_y" },
          { "dx", "dy" },
          {},
          "temp_copy",
          false },
        { "crs_mismatch",
          "CRS mismatch",
          "坐标参照系不匹配",
          FaultDomain::Geometry,
          "major",
          { "LO-09" },
          "crs_mismatch",
          { "crs" },
          { "crs" },
          {},
          "temp_copy",
          false },
        { "temporal_shuffle",
          "Temporal shuffle",
          "时序乱序",
          FaultDomain::Temporal,
          "moderate",
          { "LO-10" },
          kDiagnosisUnmatched,
          { "acquisition_dates", "band_roles" },
          {},
          {},
          "temp_copy",
          true },
        { "temporal_gap",
          "Temporal gap",
          "时序缺档",
          FaultDomain::Temporal,
          "moderate",
          { "LO-10" },
          kDiagnosisUnmatched,
          { "acquisition_dates", "band_count" },
          { "epoch_index" },
          {},
          "temp_copy",
          false },
        { "train_test_spatial_leakage",
          "Train/test spatial leakage",
          "训练/测试空间泄漏",
          FaultDomain::Ml,
          "major",
          { "LO-11" },
          kDiagnosisUnmatched,
          { "leakage.overlap_fraction", "leakage.test_count" },
          { "mode" },
          { "cell_size" },
          "temp_copy",
          false },
        { "threshold_misuse",
          "Threshold misuse",
          "阈值误用",
          FaultDomain::Ml,
          "moderate",
          { "LO-12" },
          "kappa_near_zero",
          { "threshold", "positive_fraction", "kappa" },
          { "threshold" },
          {},
          "temp_copy",
          false },
        { "model_channel_mismatch",
          "Model channel mismatch",
          "模型通道顺序不匹配",
          FaultDomain::Ml,
          "major",
          { "LO-13" },
          kDiagnosisUnmatched,
          { "channel_order", "model_output_mean" },
          { "permutation" },
          {},
          "temp_copy",
          false },
        { "provenance_removal",
          "Provenance removal",
          "溯源信息移除",
          FaultDomain::Artifact,
          "major",
          { "LO-14" },
          kDiagnosisUnmatched,
          { "provenance.generator_present" },
          {},
          { "scope" },
          "temp_copy",
          false },
    };
    return catalog;
}

const FaultFamilyInfo *findFaultFamily( const std::string &id )
{
    for ( const auto &family : faultFamilyCatalog() )
    {
        if ( family.id == id )
        {
            return &family;
        }
    }
    return nullptr;
}

} // namespace sicnu::faultlab
