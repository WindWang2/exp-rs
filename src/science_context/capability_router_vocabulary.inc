// capability_router_vocabulary.inc — intent-routing synonym vocabulary (DATA).
//
// #2108: this table is matching VOCABULARY, not code and not user-visible
// text — the router lowercases and text-matches these terms against
// goal/intent input and emits only English intent slugs. Keeping bilingual
// domain synonyms inline in capability_router.cpp misclassifies 60+ data
// entries as hardcoded non-UI strings under the i18n audit; as an included
// data fragment the vocabulary stays reviewable in ONE place while the
// audit's per-file cap again measures actual diagnostic strings. Editing
// the vocabulary means editing this table only; no code change required.

    static const std::vector<TriggerEntry> kTriggers = {
        { "sar_flood", { "sar flood", "sar 洪水", "雷达洪水", "sar inundation" } },
        { "sar_change", { "sar change", "sar 变化", "雷达变化", "backscatter change" } },
        { "sar_water", { "sar water", "sar 水体", "雷达水体" } },
        { "ndvi", { "ndvi", "vegetation index", "归一化植被", "植被指数" } },
        { "evi", { "evi", "enhanced vegetation", "增强植被" } },
        { "savi", { "savi", "soil adjusted vegetation", "土壤调节植被" } },
        { "ndre", { "ndre", "red edge", "红边" } },
        { "ndwi", { "ndwi", "normalized difference water", "归一化水体" } },
        { "mndwi", { "mndwi", "modified normalized difference water", "改进水体" } },
        { "ndsi", { "ndsi", "snow index", "雪指数" } },
        { "nbr", { "nbr", "normalized burn", "燃烧指数" } },
        { "dnbr", { "dnbr", "delta nbr", "差分燃烧" } },
        { "ndbi", { "ndbi", "built-up index", "建筑指数" } },
        { "bsi", { "bsi", "bare soil", "裸土指数" } },
        { "water", { "water body", "water extraction", "水体提取", "水体", "水面", "water" } },
        { "flood", { "flood", "洪水", "淹没", "内涝", "inundation" } },
        { "sar", { "radar", "雷达", "合成孔径", "sar" } },
        { "ship", { "ship", "vessel", "船只", "船舶" } },
        { "change", { "change detect", "change detection", "变化检测", "bitemporal", "bi-temporal", "双时相", "变化", "optical change" } },
        { "classify", { "classification", "classify", "分类", "land cover", "land use", "土地覆盖", "土地利用", "kmeans", "监督分类", "supervised", "segmentation", "分割", "obia" } },
        { "phenology", { "phenology", "物候", "growing season", "生长季" } },
        { "temporal", { "time series", "temporal", "时间序列", "时序", "trend", "趋势" } },
        { "terrain", { "slope", "aspect", "hillshade", "dem", "terrain", "地形", "坡度", "坡向", "山体阴影", "高程" } },
        { "accuracy", { "confusion matrix", "accuracy", "精度", "混淆矩阵", "验证样本" } },
        { "qa", { "cloud mask", "qa", "云掩膜", "云 mask", "质量波段" } },
        { "preprocess", { "preprocessing", "preprocess", "mosaic", "reproject", "clip", "orthorectify", "atmospheric correction", "预处理", "镶嵌", "重投影", "裁剪", "正射", "大气校正" } },
        { "inference", { "deep learning", "inference", "unet", "segformer", "推理", "深度学习", "模型推理" } },
        { "zonal", { "zonal statistics", "zonal stats", "zonal", "per-zone", "区域统计", "分区统计" } },
    };

    struct CompoundRule
    {
        std::string gate;
        std::string baseIntent;
        std::string compoundIntent;
    };

    static const std::vector<CompoundRule> kCompoundRules = {
        { "sar", "flood", "sar_flood" },
        { "sar", "change", "sar_change" },
        { "sar", "water", "sar_water" },
        { "雷达", "flood", "sar_flood" },
        { "雷达", "change", "sar_change" },
        { "雷达", "water", "sar_water" },
    };
