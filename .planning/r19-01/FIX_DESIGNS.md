# R19-01 FIX_DESIGNS — 实现设计（RED 证明后执行）

## T1.1 format_profiles `profileForPath` 两段式扩展名（#1637-2）
现状（format_profiles.cpp:586-603）：`rfind('.')` 取最后一段小写扩展名 → 按注册顺序首个 contains 命中。
根因：单段扩展名永远无法命中声明 "zarr.json"（两段）；声明顺序使 GeoJSON 的 "json" 遮蔽一切后注册的单 "json"。
修法（通用最长后缀匹配，平局取先注册）：
```
对每个 profile 的每个声明扩展 ext（先按 ext.size() 降序考虑跨 profile 语义：
  best = nullptr; bestLen = 0
  for profile in mProfiles (注册序):
    for ext in profile.extensions:
      if ext.size() <= bestLen: continue          // 不可能更优
      if cleanPathLower.size() > ext.size()
         && cleanPathLower.compare(p-e, e, ext)==0  // 后缀相等
         && cleanPathLower[p-e-1] == '.':           // 必须由 '.' 引导
        best = &profile; bestLen = ext.size()
  return best
```
- `cleanPathLower = lowerAscii(去 query/fragment 后的 percentDecode 结果)`
- 平局（同长度，如 GeoJSON vs STAC 的 "json"）→ 先注册者（GeoJSON）——与现行为完全一致（兼容钉）。
- "json"（无点）→ 无 '.' 引导 → nullptr（同现行为）。
- 波及面：仅 4 个调用点（probe/doctor/capabilities/self tests）；无既有 zarr 钉住。

## T1.2 RSOperator::execute 强制 schema 数值区间（#1578）
现状：setRange 只写 minimum/maximum 元数据；72 处声明无运行时强制（rs_json_params 无界取值直拼）。
修法（统一执行边界，rs_operator.cpp execute() 内、run() 之前）：
```
void RSOperator::validateNumericRanges(const Json::Value &params) const {
  const Json::Value props = schema()["properties"];
  for (name : props.getMemberNames()) {
    const Json::Value &decl = props[name];
    if (!decl.isMember("minimum") || !decl.isMember("maximum")) continue;
    if (!params.isMember(name) || !params[name].isNumeric()) continue;  // 缺省/类型交给既有取值器
    const double v = params[name].asDouble();
    if (v < decl["minimum"].asDouble() || v > decl["maximum"].asDouble())
      throw RSOperatorError(ErrorCode::InvalidParameter,
        "parameter '" + name + "' value " + ... + " outside declared range [min,max]");
  }
}
```
- 边界含端点（含端点是现行声明语义，如 threshold [0,1] 合法值 0/1）。
- 只影响显式声明 range 的参数；未声明者零变化（测试钉住）。
- 每次执行构建一次 schema()（小 JSON，execute 频率为每 run 一次而非每 tile——chunked_run 包一层）。
- 跨轨风险 X1：R18-A 文件在运行时受同一强制；越界值本属缺陷。PR 说明 + rebase 后跑其测试族。

## T1.3 CN 产品元数据有界读（#1637-3 孪生）
现状：cn_product_metadata.cpp:127 readFileText `buffer << in.rdbuf()` 整文件无上限；3 个 parse 入口经 scanXmlPaths。
修法：
```
constexpr std::uintmax_t kMaxMetadataXmlBytes = 4 * 1024 * 1024;  // 真实 CRESDA/PMI XML 为 KB 级
readFileText: 先 filesystem::file_size（经 pathFromUtf8）判定；超限 → GeoError(ErrorCode::InvalidMetadata,
  "product metadata exceeds the 4 MiB bound (...): refused before read")
```
- file_size 失败（特殊文件）→ 维持原读取路径（不因 stat 失败拒绝）。
- RED 已钉 InvalidMetadata + "metadata" 关键字。
